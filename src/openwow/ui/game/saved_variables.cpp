#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/game/saved_variables.h"
#include "openwow/core/storm_string.h"
#include "openwow/core/storm_utils.h"
#include "openwow/ui/game/ui_load_status_log.h"
#include "openwow/ui/lua_base_overrides.h"
#include "openwow/ui/lua_legacy_length.h"
#include "openwow/platform/filesystem/filesystem.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/sfile_core.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

namespace openwow::ui::game {

namespace {

class SavedVariableNameRegistry {
public:
  [[nodiscard]] std::vector<std::string> Snapshot() const {
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const auto &bucket : buckets_) {
      for (const std::size_t entry_index : bucket) {
        names.push_back(entries_[entry_index].name);
      }
    }
    return names;
  }

  void Register(std::string_view name) {
    std::string owned_name(name);
    const std::uint32_t hash = openwow::core::SStrHashCI(owned_name.c_str());
    if (Contains(hash, owned_name.c_str())) {
      return;
    }

    if (!initialized()) {
      InitBuckets();
    }

    std::uint32_t bucket_index = hash & bucket_mask_;
    if (MaybeGrowAndRehash(bucket_index)) {
      bucket_index = hash & bucket_mask_;
    }

    entries_.push_back(Entry{hash, std::move(owned_name)});
    buckets_[bucket_index].insert(buckets_[bucket_index].begin(), entries_.size() - 1);
  }

  void Reset() {
    entries_.clear();
    buckets_.clear();
    bucket_mask_ = kUninitializedMask;
    probe_counter_ = 0;
  }

private:
  struct Entry {
    std::uint32_t hash = 0;
    std::string name;
  };

  static constexpr std::uint32_t kUninitializedMask = std::numeric_limits<std::uint32_t>::max();
  static constexpr std::uint32_t kInitialBucketCount = 4;
  static constexpr std::uint32_t kInitialBucketMask = kInitialBucketCount - 1;
  static constexpr std::uint32_t kMaxBucketMask = 0x1FFF;
  static constexpr std::uint32_t kRehashProbeThreshold = 13;

  [[nodiscard]] bool initialized() const {
    return bucket_mask_ != kUninitializedMask;
  }

  [[nodiscard]] bool Contains(const std::uint32_t hash, const char *name) const {
    (void)hash;
    for (const Entry &entry : entries_) {
      if (openwow::text::EqualsIgnoreCaseAscii(entry.name, name)) {
        return true;
      }
    }
    return false;
  }

  void InitBuckets() {
    buckets_.assign(kInitialBucketCount, {});
    bucket_mask_ = kInitialBucketMask;
    probe_counter_ = 0;
  }

  [[nodiscard]] bool MaybeGrowAndRehash(const std::uint32_t bucket_index) {
    if (!initialized() || bucket_mask_ >= kMaxBucketMask) {
      return false;
    }

    if (probe_counter_ <= 3) {
      probe_counter_ = 0;
    } else {
      probe_counter_ -= 3;
    }

    for (const std::size_t entry_index : buckets_[bucket_index]) {
      (void)entry_index;
      ++probe_counter_;
      if (probe_counter_ > kRehashProbeThreshold) {
        probe_counter_ = 0;
        Rehash(bucket_mask_ * 2 + 2);
        return true;
      }
    }

    return false;
  }

  void Rehash(const std::uint32_t new_bucket_count) {
    auto old_buckets = std::move(buckets_);
    buckets_.assign(new_bucket_count, {});
    bucket_mask_ = new_bucket_count - 1;

    for (const auto &bucket : old_buckets) {
      for (const std::size_t entry_index : bucket) {
        const std::uint32_t bucket_index = entries_[entry_index].hash & bucket_mask_;
        buckets_[bucket_index].insert(buckets_[bucket_index].begin(), entry_index);
      }
    }
  }

  std::vector<Entry> entries_;
  std::vector<std::vector<std::size_t>> buckets_;
  std::uint32_t bucket_mask_ = kUninitializedMask;
  std::uint32_t probe_counter_ = 0;
};

SavedVariableNameRegistry &GetSavedVariableRegistry(const SavedVariableRegistrationScope scope) {
  static SavedVariableNameRegistry s_account_names;
  static SavedVariableNameRegistry s_character_names;
  return scope == SavedVariableRegistrationScope::kAccount ? s_account_names : s_character_names;
}

}

std::vector<std::string> GetSavedVariableNames() {
  return GetSavedVariableRegistry(SavedVariableRegistrationScope::kAccount).Snapshot();
}

std::vector<std::string> GetSavedVariableNamesPerChar() {
  return GetSavedVariableRegistry(SavedVariableRegistrationScope::kPerCharacter).Snapshot();
}

void RegisterSavedVariableName(SavedVariableRegistrationScope scope, std::string_view name) {
  GetSavedVariableRegistry(scope).Register(name);
}

void ClearSavedVariableRegistrations() {
  GetSavedVariableRegistry(SavedVariableRegistrationScope::kAccount).Reset();
  GetSavedVariableRegistry(SavedVariableRegistrationScope::kPerCharacter).Reset();
}

namespace {

void AppendStatus(UiLoadStatusSink *sink, std::intptr_t code, std::string_view message) {
  if (sink == nullptr || message.empty()) {
    return;
  }
  sink->AppendStatus(code, message);
}

constexpr int kMaxIndentDepth = 127;

std::string MakeIndent(int depth) {
  if (depth <= 0) {
    return {};
  }
  const auto bounded_depth = std::min(depth, kMaxIndentDepth);
  return std::string(static_cast<std::size_t>(bounded_depth), '\t');
}

std::string EscapeLuaString(const char *s, std::size_t len) {
  std::string out;
  out.reserve(len + 16);
  out.push_back('"');
  for (std::size_t i = 0; i < len; ++i) {
    switch (s[i]) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\0':
      out += "\\000";
      break;
    default:
      out.push_back(s[i]);
      break;
    }
  }
  out.push_back('"');
  return out;
}

std::string SerializeLuaValueImpl(lua_State *L, int index, int depth,
                                  std::unordered_set<const void *> &visited_tables);

std::string SerializeLuaFunction(lua_State *L, int index) {
  index = lua_absindex(L, index);
  lua_pushvalue(L, index);
  lua_Debug ar{};
  if (lua_getinfo(L, ">n", &ar) != 0 && ar.name != nullptr && ar.name[0] != '\0') {
    return "nil --[[ skipped function " + std::string(ar.name) + " (may be aliased) ]]";
  }
  return "nil --[[ skipped inline function ]]";
}

bool IsArrayKey(double value, std::size_t array_length) {
  if (!std::isfinite(value)) {
    return false;
  }
  double integral_part = 0.0;
  if (std::modf(value, &integral_part) != 0.0) {
    return false;
  }
  if (integral_part < 1.0) {
    return false;
  }
  return integral_part <= static_cast<double>(array_length);
}

std::string SerializeLuaTable(lua_State *L, int index, int depth,
                              std::unordered_set<const void *> &visited_tables) {
  index = lua_absindex(L, index);
  const void *table_identity = lua_topointer(L, index);
  if (!visited_tables.insert(table_identity).second) {
    return "nil --[[ skipped recursive table ]]";
  }

  std::string out = "{\r\n";
  const std::size_t array_length = openwow::ui::LuaLegacyLength(L, index);
  const std::string entry_indent = MakeIndent(depth + 1);
  const std::string closing_indent = MakeIndent(depth);

  for (std::size_t array_index = 1; array_index <= array_length; ++array_index) {
    lua_rawgeti(L, index, static_cast<lua_Integer>(array_index));
    out += entry_indent;
    out += SerializeLuaValueImpl(L, -1, depth + 1, visited_tables);
    out += ", -- [";
    out += std::to_string(array_index);
    out += "]\r\n";
    lua_pop(L, 1);
  }

  lua_pushnil(L);
  while (lua_next(L, index) != 0) {
    const int key_type = lua_type(L, -2);
    bool handled = false;
    switch (key_type) {
    case LUA_TNIL:
    case LUA_TBOOLEAN:
    case LUA_TSTRING:
      break;
    case LUA_TNUMBER: {
      const double key_value = lua_tonumber(L, -2);
      if (std::isnan(key_value)) {
        out += entry_indent;
        out += "--[[ skipped entry with nan key ]]\r\n";
        handled = true;
        break;
      }
      if (!std::isfinite(key_value)) {
        out += entry_indent;
        out += "--[[ skipped entry with inf key ]]\r\n";
        handled = true;
        break;
      }
      if (IsArrayKey(key_value, array_length)) {
        handled = true;
      }
      break;
    }
    default:
      out += entry_indent;
      out += "--[[ skipped entry with ";
      out += lua_typename(L, key_type);
      out += " key ]]\r\n";
      handled = true;
      break;
    }

    if (!handled) {
      out += entry_indent;
      out += "[";
      out += SerializeLuaValueImpl(L, -2, depth + 1, visited_tables);
      out += "] = ";
      out += SerializeLuaValueImpl(L, -1, depth + 1, visited_tables);
      out += ",\r\n";
    }

    lua_pop(L, 1);
  }

  visited_tables.erase(table_identity);
  out += closing_indent;
  out.push_back('}');
  return out;
}

std::string SerializeLuaValueImpl(lua_State *L, int index, int depth,
                                  std::unordered_set<const void *> &visited_tables) {
  index = lua_absindex(L, index);
  switch (lua_type(L, index)) {
  case LUA_TNIL:
    return "nil";
  case LUA_TBOOLEAN:
    return lua_toboolean(L, index) ? "true" : "false";
  case LUA_TNUMBER: {
    const double value = lua_tonumber(L, index);
    if (std::isnan(value)) {
      return "nil --[[ nan ]]";
    }
    if (!std::isfinite(value)) {
      return "nil --[[ inf ]]";
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.16g", value);
    return buf;
  }
  case LUA_TSTRING: {
    std::size_t len = 0;
    const char *s = lua_tolstring(L, index, &len);
    return EscapeLuaString(s, len);
  }
  case LUA_TTABLE:
    return SerializeLuaTable(L, index, depth, visited_tables);
  case LUA_TFUNCTION:
    return SerializeLuaFunction(L, index);
  default:
    return "nil --[[ skipped " + std::string(lua_typename(L, lua_type(L, index))) + " ]]";
  }
}

enum class SaveFileResult {
  kSuccessExistingFile,
  kSuccessNewFile,
  kOpenFailed,
};

SaveFileResult SaveVariablesToFile(lua_State *L, const std::string &dir_path,
                                   const std::vector<std::string> &var_names) {
  if (!L) {
    return SaveFileResult::kOpenFailed;
  }

  std::error_code ec;
  std::filesystem::create_directories(dir_path, ec);
  if (ec) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "SavedVariables: cannot create directory " + dir_path);
    return SaveFileResult::kOpenFailed;
  }

  const std::string file_path = dir_path + "/SavedVariables.lua";
  const std::string bak_path = file_path + ".bak";
  const bool had_regular_file = openwow::platform::filesystem::PathIsRegularFile(file_path);
  if (had_regular_file && !openwow::platform::filesystem::PathIsRegularFile(bak_path)) {

    (void)openwow::platform::filesystem::CopyFilePath(file_path, bak_path, false);
  }

  std::string serialized;
  std::unordered_set<const void *> visited_tables;
  for (const auto &var_name : var_names) {
    lua_getglobal(L, var_name.c_str());
    serialized += "\r\n";
    serialized += var_name;
    serialized += " = ";
    serialized += SerializeLuaValueImpl(L, -1, 0, visited_tables);
    lua_pop(L, 1);
    visited_tables.clear();
  }

  serialized += "\r\n";
  if (!openwow::platform::filesystem::AtomicWriteFile(file_path, serialized)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "SavedVariables: crash-durable commit failed");
    return SaveFileResult::kOpenFailed;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "SavedVariables: saved " +
                                                         std::to_string(var_names.size()) +
                                                         " variables");
  return had_regular_file ? SaveFileResult::kSuccessExistingFile : SaveFileResult::kSuccessNewFile;
}

bool EnsureSavedVariablesDirectory(const std::string &dir_path) {
  std::error_code ec;
  std::filesystem::create_directories(dir_path, ec);
  if (!ec) {
    return true;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                     "SavedVariables: cannot create directory " + dir_path);
  return false;
}

bool ExecuteSavedVariablesFile(lua_State *L, const std::string &file_path,
                               UiLoadStatusSink *status_sink) {
  if (!L) {
    return false;
  }

  std::error_code exists_error;
  if (!std::filesystem::exists(file_path, exists_error) && !exists_error) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "SavedVariables: no existing file " + file_path);
    AppendStatus(status_sink, 2,
                 "Error loading " + ToWoWClientLogPath(file_path));
    return false;
  }

  std::ifstream input(file_path, std::ios::binary);
  if (!input.is_open()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "SavedVariables: cannot open " + file_path + " for reading");
    AppendStatus(status_sink, 2, "Error loading " + ToWoWClientLogPath(file_path));
    return false;
  }

  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  const std::string chunk_name = "@" + file_path;
  if (openwow::ui::LoadClientLuaChunk(L, std::string_view(source), chunk_name.c_str()) != 0) {
    const char *err = lua_tostring(L, -1);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "SavedVariables: Lua compile error in " +
                                                           file_path + ": " +
                                                           (err ? err : "(null)"));
    AppendStatus(status_sink, 2, err ? err : "(null)");
    lua_pop(L, 1);
    return false;
  }

  if (lua_pcall(L, 0, 0, 0) != 0) {
    const char *err = lua_tostring(L, -1);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "SavedVariables: Lua runtime error in " +
                                                           file_path + ": " +
                                                           (err ? err : "(null)"));
    AppendStatus(status_sink, 2, err ? err : "(null)");
    lua_pop(L, 1);
    return false;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "SavedVariables: loaded " + file_path);
  return true;
}

bool LoadVariablesFromDirectory(lua_State *L, const std::string &dir_path,
                                UiLoadStatusSink *status_sink) {
  if (!EnsureSavedVariablesDirectory(dir_path)) {
    return false;
  }

  return ExecuteSavedVariablesFile(L, dir_path + "/SavedVariables.lua", status_sink);
}

}

std::string SerializeLuaValue(lua_State *L, int index, int depth) {
  std::unordered_set<const void *> visited_tables;
  return SerializeLuaValueImpl(L, index, depth, visited_tables);
}

bool LoadAllSavedVariables(lua_State *L, const std::string &account_name,
                           const std::string &realm_name, const std::string &char_name,
                           UiLoadStatusSink *status_sink) {
  if (!L) {
    return false;
  }
  if (!openwow::platform::filesystem::IsSafePathComponent(account_name) ||
      (!realm_name.empty() &&
       !openwow::platform::filesystem::IsSafePathComponent(realm_name)) ||
      (!char_name.empty() &&
       !openwow::platform::filesystem::IsSafePathComponent(char_name))) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "SavedVariables: invalid persistence identity, skipping load");
    return false;
  }

  const std::string account_dir = "WTF/Account/" + account_name;
  bool ok = LoadVariablesFromDirectory(L, account_dir, status_sink);

  if (!realm_name.empty() && !char_name.empty()) {
    ok = LoadVariablesFromDirectory(L, account_dir + "/" + realm_name + "/" + char_name,
                                    status_sink) &&
         ok;
  }

  return ok;
}

void SaveAllSavedVariables(lua_State *L, const std::string &account_name,
                           const std::string &realm_name, const std::string &char_name) {
  if (!L)
    return;
  if (!openwow::platform::filesystem::IsSafePathComponent(account_name) ||
      (!realm_name.empty() &&
       !openwow::platform::filesystem::IsSafePathComponent(realm_name)) ||
      (!char_name.empty() &&
       !openwow::platform::filesystem::IsSafePathComponent(char_name))) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "SavedVariables: invalid persistence identity, skipping save");
    return;
  }

  const std::string acct_dir = "WTF/Account/" + account_name;
  bool invalidate_loose_manifest = false;

  const auto account_result = SaveVariablesToFile(
      L, acct_dir, GetSavedVariableRegistry(SavedVariableRegistrationScope::kAccount).Snapshot());
  if (account_result == SaveFileResult::kOpenFailed) {
    return;
  }
  GetSavedVariableRegistry(SavedVariableRegistrationScope::kAccount).Reset();
  invalidate_loose_manifest |= account_result == SaveFileResult::kSuccessNewFile;

  if (!realm_name.empty() && !char_name.empty()) {
    const std::string char_dir = acct_dir + "/" + realm_name + "/" + char_name;
    const auto character_result = SaveVariablesToFile(
        L, char_dir,
        GetSavedVariableRegistry(SavedVariableRegistrationScope::kPerCharacter).Snapshot());
    if (character_result == SaveFileResult::kOpenFailed) {
      return;
    }
    GetSavedVariableRegistry(SavedVariableRegistrationScope::kPerCharacter).Reset();
    invalidate_loose_manifest |= character_result == SaveFileResult::kSuccessNewFile;
  }

  if (invalidate_loose_manifest) {
    openwow::vfs::InvalidateSFileLooseManifestCache();
  }
}

}

#include "openwow/data/archive_system.h"
#include "openwow/audio/codecs/ogg/ogg_decompress.h"
#include "openwow/core/client_init.h"
#include "openwow/core/cvar.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/streaming_init.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/platform/adapters/win32/win32_compat.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/storage/persistence/profile_reader.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/vfs/retail/sfile_archive.h"
#include "openwow/vfs/retail/sfile_configuration.h"
#include "openwow/vfs/retail/sound_cache/sound_cache.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
#include <StormLib.h>
#include "openwow/foundation/compiler/printf_format.h"
#endif

namespace openwow::data {

namespace {

constexpr std::size_t kInitialRegisteredArchiveHandleCapacity = 64;

STORM_HANDLE* g_registered_archive_handles = nullptr;
std::size_t g_registered_archive_handle_capacity = 0;
std::size_t g_registered_archive_handle_count = 0;
std::size_t g_registered_archive_table_base_index = 0;
ArchiveCloseCallbackForTests g_archive_close_callback_for_tests = nullptr;

struct ArchiveSlotInfo {
  std::uint32_t type{0};
  std::uint32_t flags{0};
  std::uint32_t reserved0{0};
  std::uint32_t reserved1{0};
};
static_assert(sizeof(ArchiveSlotInfo) == 16);

ArchiveSlotInfo g_archive_slot_info[kMaxArchiveSlots] = {};

std::uint32_t g_streaming_flags = 0;

std::string g_streaming_error_text;

ArchiveLocaleInfo g_current_locale = {};

int g_startup_level = 0;

struct BuildPatchMPQListEntry {
  int pass{0};
  bool include_secondary_patch_set{false};
  bool prefix_with_retail_install_path{false};
  const char* directory_pattern{nullptr};
  const char* file_pattern{nullptr};
};

constexpr std::array<BuildPatchMPQListEntry, 12> kBuildPatchMPQListEntries = {{
    {0, false, false, "Data\\", "patch-?.MPQ"},
    {0, false, false, "Data\\%s\\", "patch-%s-?.MPQ"},
    {1, false, false, "Data\\", "patch.MPQ"},
    {1, false, false, "Data\\%s\\", "patch-%s.MPQ"},
    {1, true, false, "Data\\", "patch-4.MPQ"},
    {1, true, false, "Data\\%s\\", "patch-%s-4.MPQ"},
    {1, true, false, "Data\\", "patch-3.MPQ"},
    {1, true, false, "Data\\%s\\", "patch-%s-3.MPQ"},
    {1, true, false, "..\\Data\\", "patch-2.MPQ"},
    {1, true, false, "..\\Data\\%s\\", "patch-%s-2.MPQ"},
    {1, true, false, "..\\Data\\", "patch.MPQ"},
    {1, true, false, "..\\Data\\%s\\", "patch-%s.MPQ"},
}};

struct ArchiveTableEntry {
  const char* name;
  std::uint32_t type;
  std::uint32_t layout_mask;
};

constexpr std::array<ArchiveTableEntry, kMaxArchiveSlots> kArchiveTable = {{
    {"alternate.MPQ", 3, 3},
    {"interface.MPQ", 1, 1},
    {"misc.MPQ", 1, 1},
    {"model.MPQ", 1, 1},
    {"texture.MPQ", 1, 1},
    {"terrain.MPQ", 1, 1},
    {"wmo.MPQ", 1, 1},
    {"sound.MPQ", 1, 1},
    {"fonts.MPQ", 1, 1},
    {"dbc.MPQ", 1, 1},
    {"speech.MPQ", 1, 1},
    {"expansionloc.MPQ", 2, 1},
    {"lichkingloc.MPQ", 2, 1},
    {"expansionspeech.MPQ", 2, 1},
    {"lichkingspeech.MPQ", 2, 1},
    {"expansion.MPQ", 2, 3},
    {"lichking.MPQ", 2, 3},
    {"common.MPQ", 1, 2},
    {"common-2.MPQ", 2, 2},
    {"****\\locale-****.MPQ", 1, 2},
    {"****\\speech-****.MPQ", 1, 2},
    {"****\\expansion-locale-****.MPQ", 2, 2},
    {"****\\lichking-locale-****.MPQ", 2, 2},
    {"****\\expansion-speech-****.MPQ", 2, 2},
    {"****\\lichking-speech-****.MPQ", 2, 2},
    {"development.MPQ", 2, 1},
    {"streaming.MPQ", 4, 1},
    {"streamingloc.MPQ", 4, 1},
}};

constexpr std::array<const char*, 9> kWowIniLocaleTable = {
    "enUS", "koKR", "frFR", "deDE", "zhCN",
    "zhTW", "esES", "esMX", "ruRU",
};

bool ResizeRegisteredArchiveHandleArray(const std::size_t new_capacity) {
  g_registered_archive_handle_capacity = new_capacity;

  STORM_HANDLE* const old_handles = g_registered_archive_handles;
  STORM_HANDLE* new_handles = nullptr;
  if (new_capacity != 0) {
    new_handles = new (std::nothrow) STORM_HANDLE[new_capacity]{};
  }

  if (new_handles != nullptr) {
    const std::size_t handles_to_copy =
        std::min(new_capacity, g_registered_archive_handle_count);
    for (std::size_t index = 0; index < handles_to_copy; ++index) {
      new_handles[index] = old_handles[index];
    }
  }

  g_registered_archive_handles = new_handles;
  delete[] old_handles;
  return new_handles != nullptr || new_capacity == 0;
}

bool EnsureRegisteredArchiveHandleCapacity(const std::size_t required_capacity) {
  if (required_capacity == 0) {
    return true;
  }
  if (g_registered_archive_handles != nullptr &&
      required_capacity <= g_registered_archive_handle_capacity) {
    return true;
  }
  return ResizeRegisteredArchiveHandleArray(required_capacity);
}

bool CloseRegisteredArchiveHandle(const STORM_HANDLE handle) {
  if (handle == nullptr) {
    return false;
  }

  if (g_archive_close_callback_for_tests != nullptr) {
    return g_archive_close_callback_for_tests(handle);
  }

  return openwow::vfs::SFileCloseArchiveWrapped(handle);
}

char* DuplicateString(const char* s) {
  if (!s) return nullptr;
  std::size_t len = std::strlen(s);
  char* dup = new char[len + 1];
  std::memcpy(dup, s, len + 1);
  return dup;
}

std::string FormatPattern(const char* pattern, const char* locale) {
  std::string result;
  const char* p = pattern;
  while (*p) {
    if (*p == '%' && *(p + 1) == 's') {
      result += locale;
      p += 2;
    } else {
      result += *p;
      ++p;
    }
  }
  return result;
}

std::filesystem::path StartupStatePathToNative(std::string path) {
#if !defined(_WIN32)
  std::replace(path.begin(), path.end(), '\\',
               std::filesystem::path::preferred_separator);
#endif
  return std::filesystem::path(path);
}

std::filesystem::path ResolveStartupClientRoot() {
  const auto& state = GetStartupFileSystemState();
  if (!state.executable_base_path.empty()) {
    return StartupStatePathToNative(state.executable_base_path);
  }
  return std::filesystem::current_path();
}

ArchiveProbeNativeRoots BuildDefaultArchiveProbeNativeRoots() {
  const auto startup_root = ResolveStartupClientRoot();
  const auto& startup_state = GetStartupFileSystemState();
  ArchiveProbeNativeRoots roots;
  roots.data_root = startup_root / "Data";
  roots.parent_data_root = startup_root.parent_path() / "Data";
  if (!startup_state.retail_install_path_cache.empty()) {
    roots.retail_data_root =
        StartupStatePathToNative(startup_state.retail_install_path_cache) / "Data";
  }
  return roots;
}

std::optional<std::filesystem::path> BuildDefaultArchiveProbeNativePath(
    const int root_index,
    const char* name,
    const char* locale_token) {
  if (!name) {
    return std::nullopt;
  }

  return BuildArchiveProbePathNative(root_index,
                                     BuildDefaultArchiveProbeNativeRoots(),
                                     name,
                                     locale_token);
}

bool DefaultBuildArchiveProbePath(const int attempt,
                                  const int buffer_size,
                                  const char* name,
                                  char* path_out,
                                  const char* locale_token) {
  if (!name || !path_out || buffer_size <= 0) {
    return false;
  }

  const auto path =
      BuildArchiveProbePathExact(attempt,
                                 name,
                                 locale_token,
                                 GetStartupFileSystemState().retail_install_path_cache);
  if (!path.has_value()) {
    return false;
  }

  std::strncpy(path_out, path->c_str(),
               static_cast<std::size_t>(buffer_size - 1));
  path_out[buffer_size - 1] = '\0';
  return true;
}

bool DefaultHasCommonArchiveLayout() {
  return ProbeCommonArchiveLayout();
}

bool DefaultOpenArchive(const char* path, const std::int32_t priority,
                        const std::uint32_t flags, void** out_handle,
                        std::uint32_t* out_error) {
  const bool ok = openwow::vfs::SFileOpenArchiveWrapped(
      path, priority, flags, out_handle);
  if (!ok && out_error) {
    *out_error = openwow::platform::GetPlatformLastError();
  }
  return ok;
}

bool DefaultOpenPatchArchive(void* base_archive, const char* path,
                             const std::int32_t priority, const int flags,
                             void** out_handle, std::uint32_t* out_error) {
  const bool ok = openwow::vfs::SFileOpenPatchArchiveWrapped(
      base_archive, path, priority, flags, out_handle);
  if (!ok && out_error) {
    *out_error = openwow::platform::GetPlatformLastError();
  }
  return ok;
}

std::string ReadWowIniValue(const std::string& key) {
  const std::string wow_ini_path =
      (ResolveStartupClientRoot() / "Wow.ini").string();
  const auto value =
      openwow::storage::persistence::ReadFirstProfileValueFromFile(
          openwow::storage::persistence::ProfileFilePath(wow_ini_path),
          {
              .section = openwow::storage::persistence::ProfileSectionName(
                  "WoW Config"),
              .key = openwow::storage::persistence::ProfileKeyName(key),
          });
  if (!value) {
    return {};
  }

  return std::string(value->Text());
}

void DefaultSetCVarString(const std::string& name, const std::string& value) {
  openwow::ui::game::CVarSystem::Instance().SetCVar(name, value, true);
}

}

void DefaultLoadLoginConfigs(const int reload, const char* locale) {
  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  cvars.RegisterCVar("decorateAccountName", "0",
                     openwow::ui::game::CVarFlags::None,
                     "Decorate account names");
  cvars.RegisterCVar("realmListbn", "",
                     openwow::ui::game::CVarFlags::None,
                     "Address of Battle.net server");
  cvars.RegisterCVar("portal", "",
                     openwow::ui::game::CVarFlags::None,
                     "Name of Battle.net portal to use");
  cvars.RegisterCVar("serverAlert", "SERVER_ALERT_URL",
                     openwow::ui::game::CVarFlags::None,
                     "Get the glue-string tag for the URL");
  cvars.RegisterCVar("realmList", "us.logon.worldofwarcraft.com:3724",
                     openwow::ui::game::CVarFlags::None,
                     "Address of realm list server");

  if (!reload) {
    return;
  }

  std::string locale_prefix;
  if (locale && *locale) {
    locale_prefix = "data\\";
    locale_prefix += locale;
    locale_prefix += "\\";
  }

  if (!locale_prefix.empty()) {
    const std::string battlenet_path = locale_prefix + "realmlistbn.wtf";
    if (!openwow::core::ida::CVar_LoadFromFile(battlenet_path)) {
      openwow::core::ida::CVar_LoadFromFile("realmlistbn.wtf");
    }
  } else {
    openwow::core::ida::CVar_LoadFromFile("realmlistbn.wtf");
  }

  if (!locale_prefix.empty()) {
    const std::string realm_list_path = locale_prefix + "realmlist.wtf";
    if (!openwow::core::ida::CVar_LoadFromFile(realm_list_path)) {
      openwow::core::ida::CVar_LoadFromFile("realmlist.wtf");
    }
  } else {
    openwow::core::ida::CVar_LoadFromFile("realmlist.wtf");
  }
}

namespace {

std::uint32_t LocaleTagFromString(const char* locale) {
  std::uint32_t tag = 0;
  if (!locale) {
    return tag;
  }

  for (int i = 0; i < 4 && locale[i] != '\0'; ++i) {
    tag |= static_cast<std::uint32_t>(
               static_cast<unsigned char>(locale[i]))
           << (i * 8);
  }
  return tag;
}

int BuildPatchMPQList_CompareStringPtrsNoCaseDesc(const char* left,
                                                  const char* right) {
  return -openwow::core::SStrCmpNoCase(left, right, 0x7FFFFFFFu);
}

int ComparePatchWildcardEntries(const std::vector<std::string>& entries,
                                std::size_t left,
                                std::size_t right) {
  return BuildPatchMPQList_CompareStringPtrsNoCaseDesc(
      entries[left].c_str(),
      entries[right].c_str());
}

void BuildPatchMPQList_ShortSort(std::vector<std::string>* entries,
                                 std::size_t low,
                                 std::size_t high) {
  if (entries == nullptr || entries->empty() || low >= high) {
    return;
  }

  while (high > low) {
    std::size_t selected = low;
    for (std::size_t scan = low + 1; scan <= high; ++scan) {
      if (ComparePatchWildcardEntries(*entries, scan, selected) > 0) {
        selected = scan;
      }
    }

    if (selected != high) {
      std::swap((*entries)[selected], (*entries)[high]);
    }
    --high;
  }
}

void SortPatchWildcardEntries(std::vector<std::string>* entries) {
  if (entries == nullptr || entries->size() < 2) {
    return;
  }

  constexpr std::size_t kShortSortThreshold = 8;
  std::array<std::size_t, 30> low_stack{};
  std::array<std::size_t, 30> high_stack{};
  int stack_depth = 0;
  std::size_t low = 0;
  std::size_t high = entries->size() - 1;

  for (;;) {
    const std::size_t range_count = high - low + 1;
    if (range_count <= kShortSortThreshold) {
      BuildPatchMPQList_ShortSort(entries, low, high);
    } else {
      const std::size_t middle = low + (range_count >> 1);
      if (middle != low) {
        std::swap((*entries)[low], (*entries)[middle]);
      }

      std::size_t loguy = low;
      std::size_t higuy = high + 1;
      while (true) {
        do {
          ++loguy;
        } while (loguy <= high &&
                 ComparePatchWildcardEntries(*entries, loguy, low) <= 0);

        do {
          --higuy;
        } while (higuy > low &&
                 ComparePatchWildcardEntries(*entries, higuy, low) >= 0);

        if (loguy > higuy) {
          break;
        }

        if (loguy != higuy) {
          std::swap((*entries)[loguy], (*entries)[higuy]);
        }
      }

      if (low != higuy) {
        std::swap((*entries)[low], (*entries)[higuy]);
      }

      const auto left_metric = static_cast<std::ptrdiff_t>(higuy) -
                               static_cast<std::ptrdiff_t>(low) - 1;
      const auto right_metric = static_cast<std::ptrdiff_t>(high) -
                                static_cast<std::ptrdiff_t>(loguy);
      if (left_metric < right_metric) {
        if (loguy < high) {
          low_stack[stack_depth] = loguy;
          high_stack[stack_depth] = high;
          ++stack_depth;
        }

        if (low + 1 < higuy) {
          high = higuy - 1;
          continue;
        }
      } else {
        if (low + 1 < higuy) {
          low_stack[stack_depth] = low;
          high_stack[stack_depth] = higuy - 1;
          ++stack_depth;
        }

        if (loguy < high) {
          low = loguy;
          continue;
        }
      }
    }

    const int next = --stack_depth;
    if (next < 0) {
      return;
    }

    low = low_stack[next];
    high = high_stack[next];
  }
}

std::filesystem::path ResolvePatchDirectory(
    const std::filesystem::path& root,
    const std::string& relative_dir) {
  std::string normalized = relative_dir;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  return (root / std::filesystem::path(normalized)).lexically_normal();
}

std::string JoinStormPathPrefix(const std::string& left,
                                const std::string& right) {
  if (left.empty()) {
    return right;
  }

  std::string result = left;
  const bool left_has_sep = result.back() == '\\' || result.back() == '/';
  const bool right_has_sep = !right.empty() &&
                             (right.front() == '\\' || right.front() == '/');
  if (left_has_sep && right_has_sep) {
    result.pop_back();
  } else if (!left_has_sep && !right_has_sep) {
    result.push_back('\\');
  }
  result += right;
  return result;
}

OPENWOW_PRINTF_FORMAT(1, 2) void WriteFormattedStderrMessage(const char* format, ...) {
  std::array<char, 4096> buffer{};

  if (format != nullptr) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    buffer.back() = '\0';
  }

  std::fputs(buffer.data(), stderr);
}

void ReportPatchDirectoryOpenFailure(const std::filesystem::path& directory) {
  const std::string path_text = directory.string();
  WriteFormattedStderrMessage("failed to open %s\n", path_text.c_str());
  openwow::core::StormSetLastError(8);
}

void AppendPatchListMatches(std::vector<std::string>* entries,
                            const std::filesystem::path& client_root,
                            const std::string& retail_install_path,
                            const char* locale,
                            const BuildPatchMPQListEntry& entry) {
  const std::string relative_dir =
      FormatPattern(entry.directory_pattern, locale);
  const std::string pattern = FormatPattern(entry.file_pattern, locale);
  const bool use_retail_prefix =
      entry.prefix_with_retail_install_path && !retail_install_path.empty();
  const std::filesystem::path search_root =
      use_retail_prefix ? std::filesystem::path(retail_install_path)
                        : client_root;
  const std::filesystem::path dir =
      ResolvePatchDirectory(search_root, relative_dir);
  const std::string stored_prefix = use_retail_prefix
                                        ? JoinStormPathPrefix(retail_install_path,
                                                              relative_dir)
                                        : relative_dir;
  const std::string search_path = use_retail_prefix
                                      ? JoinStormPathPrefix(retail_install_path,
                                                            relative_dir)
                                      : relative_dir;

  if (!openwow::vfs::SFileFindFiles(
          search_path.c_str(), pattern.c_str(),
          [entries, &stored_prefix](const openwow::vfs::SFileFindData &find_data) {
            if (find_data.entry_name) {
              entries->push_back(stored_prefix + find_data.entry_name);
            }
            return false;
          },
          false)) {
    ReportPatchDirectoryOpenFailure(dir);
  }
}

bool IsChineseLocale(const char* locale) {
  return std::strcmp(locale, "zhCN") == 0 ||
         std::strcmp(locale, "enCN") == 0 ||
         std::strcmp(locale, "zhTW") == 0 ||
         std::strcmp(locale, "enTW") == 0;
}

int FontSystemModeFromLocaleIndex(int locale_index) {
  switch (locale_index) {
    case 1:
    case 2:
    case 4:
    case 5:
      return 1;
    case 8:
      return 2;
    default:
      return 0;
  }
}

}

bool OpenArchiveByName(const char* name,
                       std::int32_t priority,
                       int slot_index,
                       const char* locale_ptr,
                       const ArchiveSystemCallbacks& callbacks) {
  if (slot_index < 0) return false;
  if (!EnsureRegisteredArchiveHandleCapacity(
          static_cast<std::size_t>(slot_index) + 1)) {
    return false;
  }

  char path_buf[0x104] = {};
  int attempt = 0;

  while (true) {
    const bool path_ok = callbacks.build_archive_probe_path
                             ? callbacks.build_archive_probe_path(
                                   attempt, 0x104, name, path_buf, locale_ptr)
                             : DefaultBuildArchiveProbePath(
                                   attempt, 0x104, name, path_buf, locale_ptr);
    if (!path_ok) {
      ++attempt;
      if (attempt >= kOpenArchiveRetries) {
        return false;
      }
      continue;
    }

    void* handle = nullptr;
    std::uint32_t err = 0;
    const bool opened =
        callbacks.open_archive
            ? callbacks.open_archive(
                  path_buf, priority, kSFileOpenFlags, &handle, &err)
            : [&]() {
                const auto native_path =
                    BuildDefaultArchiveProbeNativePath(
                        attempt, name, locale_ptr);
                if (!native_path.has_value()) {
                  return false;
                }
                return DefaultOpenArchive(native_path->string().c_str(),
                                          priority,
                                          kSFileOpenFlags,
                                          &handle,
                                          &err);
              }();
    if (opened) {
      g_registered_archive_handles[slot_index] = handle;
      g_registered_archive_handle_count = std::max(
          g_registered_archive_handle_count,
          static_cast<std::size_t>(slot_index) + 1);
      return true;
    }

    g_registered_archive_handles[slot_index] = nullptr;
    if (err == kStormErrCorrupt || err == kStormErrAccessDenied ||
        err == kStormErrNotReady || err == kStormErrDiskFull ||
        err == kStormErrBadFormat || err == kStormErrBadSignature) {
      return false;
    }

    ++attempt;
    if (attempt >= kOpenArchiveRetries) {
      return false;
    }
  }
}

PatchFileList BuildPatchMPQList(const char* locale,
                                bool include_secondary_patch_set) {
  PatchFileList result = {};
  std::vector<std::string> collected;
  const char* locale_token = locale ? locale : "";
  const auto& startup_state = GetStartupFileSystemState();
  const std::filesystem::path client_root = ResolveStartupClientRoot();

  for (int pass = 0; pass <= 1; ++pass) {
    for (const auto& entry : kBuildPatchMPQListEntries) {
      if (entry.pass != pass ||
          entry.include_secondary_patch_set != include_secondary_patch_set) {
        continue;
      }

      AppendPatchListMatches(
          &collected,
          client_root,
          startup_state.retail_install_path_cache,
          locale_token,
          entry);
    }

    if (pass == 0) {
      SortPatchWildcardEntries(&collected);
    }
  }

  if (!collected.empty()) {
    result.count = static_cast<int>(collected.size());
    result.array = new char*[result.count];
    for (int i = 0; i < result.count; ++i) {
      result.array[i] = DuplicateString(collected[i].c_str());
    }
  }

  return result;
}

void PatchFileList_Destroy(PatchFileList* list) {
  for (int i = 0; i < list->count; ++i) {
    if (list->array[i]) {

      delete[] list->array[i];
    }
  }

  if (list->array) {
    delete[] list->array;
  }
}

bool LoadAllArchives(const ArchiveSystemCallbacks& callbacks) {
  g_registered_archive_table_base_index = 0;
  const int layout_flags =
      (callbacks.has_common_archive_layout
           ? callbacks.has_common_archive_layout()
           : DefaultHasCommonArchiveLayout())
      + 1;

  for (int i = 0; i < kMaxArchiveSlots; ++i) {
    g_archive_slot_info[i] = {};
  }
  if (!EnsureRegisteredArchiveHandleCapacity(kInitialRegisteredArchiveHandleCapacity)) {
    return false;
  }
  std::fill_n(g_registered_archive_handles, g_registered_archive_handle_capacity,
              nullptr);
  g_registered_archive_handle_count = 0;

  std::array<std::uint8_t, kMaxArchiveSlots> opened_archive_table = {};
  std::string locale_token = ((layout_flags & 2) != 0)
                                 ? (callbacks.cvar_get_string
                                        ? callbacks.cvar_get_string("locale")
                                        : openwow::ui::game::CVarSystem::Instance().GetCVar(
                                              "locale"))
                                 : "----";
  if (locale_token.empty()) {
    locale_token = "----";
  }

  PatchFileList patch_list = BuildPatchMPQList(locale_token.c_str());

  bool success = true;
  std::size_t slot = 0;
  std::int32_t priority = kPatchBasePriority;
  for (int i = patch_list.count - 1; i >= 0; --i) {
    if (!patch_list.array[i]) {
      continue;
    }
    if (!EnsureRegisteredArchiveHandleCapacity(slot + 1)) {
      success = false;
      break;
    }

    void* handle = nullptr;
    std::uint32_t err = 0;
    const bool opened =
        callbacks.open_archive
            ? callbacks.open_archive(patch_list.array[i], priority,
                                     kSFileOpenFlags, &handle, &err)
            : DefaultOpenArchive(patch_list.array[i], priority,
                                 kSFileOpenFlags, &handle, &err);
    if (opened) {
      g_registered_archive_handles[slot] = handle;
      ++slot;
      ++priority;
    }
  }

  const std::size_t patch_archive_count = slot;
  if (success) {
    for (std::size_t entry_index = 0; entry_index < kArchiveTable.size();
         ++entry_index) {
      const auto& entry = kArchiveTable[entry_index];
      if (entry.type != 3) {
        continue;
      }
      if (OpenArchiveByName(entry.name, priority, static_cast<int>(slot),
                            locale_token.c_str(), callbacks)) {
        opened_archive_table[entry_index] = 1;
        ++slot;
        ++priority;
      }
    }
  }

  if (success) {
    const bool is_chinese = IsChineseLocale(locale_token.c_str());
    for (std::size_t patch_index = 0; patch_index < patch_archive_count;
         ++patch_index) {
      for (std::size_t entry_index = 0; entry_index < kArchiveTable.size();
           ++entry_index) {
        const auto& entry = kArchiveTable[entry_index];
        if (entry.type != 3 ||
            !(opened_archive_table[entry_index] || is_chinese)) {
          continue;
        }
        if (!EnsureRegisteredArchiveHandleCapacity(slot + 1)) {
          success = false;
          break;
        }

        void* handle = nullptr;
        std::uint32_t err = 0;
        const bool opened =
            callbacks.open_patch_archive
                ? callbacks.open_patch_archive(
                      g_registered_archive_handles[patch_index], entry.name,
                      priority, 0, &handle, &err)
                : DefaultOpenPatchArchive(
                      g_registered_archive_handles[patch_index], entry.name,
                      priority, 0, &handle, &err);
        if (opened) {
          g_registered_archive_handles[slot] = handle;
          ++slot;
          ++priority;
        }
      }
      if (!success) {
        break;
      }
    }
  }

  const std::size_t table_base = slot;
  g_registered_archive_table_base_index = table_base;
  bool online_mode =
      callbacks.is_online_mode ? callbacks.is_online_mode() : false;
  std::int32_t fallback_priority = 63;
  if (success) {
    for (std::size_t entry_index = 0; entry_index < kArchiveTable.size();
         ++entry_index) {
      const auto& entry = kArchiveTable[entry_index];
      if (!EnsureRegisteredArchiveHandleCapacity(slot + 1)) {
        success = false;
        break;
      }

      g_registered_archive_handles[slot] = nullptr;
      g_archive_slot_info[entry_index].type = entry.type;
      g_archive_slot_info[entry_index].flags = entry.layout_mask;

      if (entry.type == 3 || entry.type == 4) {
        ++slot;
        continue;
      }

      const bool archive_required = entry.type != 2;
      const bool enabled = (layout_flags & static_cast<int>(entry.layout_mask)) != 0;
      const bool can_open = enabled && (!online_mode || entry.type != 2);
      if (can_open &&
          !OpenArchiveByName(entry.name, fallback_priority,
                             static_cast<int>(slot), locale_token.c_str(),
                             callbacks) &&
          archive_required) {
        ArchiveOpenFailureContext failure{};
        failure.archive_name = entry.name;
        failure.resolved_data_path =
            ReplaceArchiveLocalePlaceholdersExact(std::string("Data\\") + entry.name,
                                                  locale_token.c_str());
        failure.locale_token = locale_token;
        failure.streaming_manifest_active = (g_streaming_flags & 1u) != 0;
        failure.storm_error = openwow::platform::GetPlatformLastError();
        failure.streaming_status_text = openwow::vfs::GetStreamingStatusMessageText();

        PatchFileList_Destroy(&patch_list);
        if (callbacks.handle_archive_open_failure) {
          callbacks.handle_archive_open_failure(failure);
        } else {
          std::exit(failure.storm_error != 0
                        ? static_cast<int>(failure.storm_error)
                        : 1);
        }
        return false;
      }

      ++slot;
      --fallback_priority;
      opened_archive_table[entry_index] = 1;
    }
  }

  if (success && online_mode) {
    fallback_priority = 64;
  }
  const bool open_streaming_archives =
      success &&
      (callbacks.should_open_streaming_archives
           ? callbacks.should_open_streaming_archives()
           : IsStreamingInitialized());
  if (open_streaming_archives) {
    for (std::size_t entry_index = 0; entry_index < kArchiveTable.size();
         ++entry_index) {
      const auto& entry = kArchiveTable[entry_index];
      if (entry.type != 4) {
        continue;
      }
      if (OpenArchiveByName(entry.name, fallback_priority,
                            static_cast<int>(slot), locale_token.c_str(),
                            callbacks)) {
        opened_archive_table[entry_index] = 1;
        ++slot;
      }
    }
  }

  if (!success) {
    PatchFileList_Destroy(&patch_list);
    return false;
  }

  g_registered_archive_handle_count = slot;
  g_startup_level = 0;
  if (table_base + 16 < g_registered_archive_handle_count &&
      g_registered_archive_handles[table_base + 16] != nullptr) {
    g_startup_level = 2;
  } else if (table_base + 15 < g_registered_archive_handle_count &&
             g_registered_archive_handles[table_base + 15] != nullptr) {
    g_startup_level = 1;
  }

  g_current_locale = {};
  const auto read_wow_ini = [&](const std::string& key) {
    return callbacks.read_wow_ini ? callbacks.read_wow_ini(key)
                                  : ReadWowIniValue(key);
  };
  g_current_locale.region = read_wow_ini("Region");
  g_current_locale.language = read_wow_ini("Language");
  g_current_locale.country = read_wow_ini("Country");

  if (!g_current_locale.language.empty() && !g_current_locale.country.empty()) {
    std::string wow_ini_locale =
        (g_current_locale.language + g_current_locale.country).substr(0, 4);
    const bool en_gb =
        openwow::core::SStrCmpNoCase(wow_ini_locale.c_str(), "enGB",
                                     0x7FFFFFFF) == 0;

    for (std::size_t index = 0; index < kWowIniLocaleTable.size(); ++index) {
      const bool matches_locale =
          openwow::core::SStrCmpNoCase(wow_ini_locale.c_str(),
                                       kWowIniLocaleTable[index],
                                       0x7FFFFFFF) == 0;
      const bool matches_en_gb_alias =
          en_gb &&
          openwow::core::SStrCmpNoCase(kWowIniLocaleTable[index], "enUS",
                                       0x7FFFFFFF) == 0;
      if (!matches_locale && !matches_en_gb_alias) {
        continue;
      }

      g_current_locale.locale_index = static_cast<int>(index);
      const int font_mode =
          FontSystemModeFromLocaleIndex(g_current_locale.locale_index);
      if (callbacks.set_font_system_mode) {
        callbacks.set_font_system_mode(font_mode);
      }

      if ((layout_flags & 1) != 0) {
        if (callbacks.cvar_set_string) {
          callbacks.cvar_set_string("locale", wow_ini_locale);
        } else {
          DefaultSetCVarString("locale", wow_ini_locale);
        }
        locale_token = wow_ini_locale;
      }
      break;
    }
  }

  if (callbacks.load_login_configs) {
    callbacks.load_login_configs(1, locale_token.c_str());
  } else {
    DefaultLoadLoginConfigs(1, locale_token.c_str());
  }

  if (callbacks.load_signature_file) {
    callbacks.load_signature_file(LocaleTagFromString(locale_token.c_str()));
  } else {
    openwow::core::LoadSignatureFile(LocaleTagFromString(locale_token.c_str()));
  }

  PatchFileList_Destroy(&patch_list);
  return success;
}

void CleanupRegisteredArchiveHandlesForShutdown() {
  if (g_registered_archive_handles == nullptr ||
      g_registered_archive_handle_count == 0) {
    return;
  }

  for (std::size_t index = g_registered_archive_handle_count; index > 0;
       --index) {
    const auto handle = g_registered_archive_handles[index - 1];
    if (handle == nullptr) {
      continue;
    }

    (void)CloseRegisteredArchiveHandle(handle);
  }
}

int InitPatchList(char* manifest_file,
                  const ArchiveSystemCallbacks& callbacks) {
  if (callbacks.init_timer_baseline) {
    callbacks.init_timer_baseline(1);
  }

  bool manifest_exists = false;
  if (callbacks.file_exists) {
    manifest_exists = callbacks.file_exists(manifest_file);
  } else if (manifest_file) {
    std::error_code ec;
    manifest_exists = std::filesystem::exists(manifest_file, ec);
  }

  if (manifest_exists && manifest_file) {
    g_streaming_flags |= 1u;

    const int result = openwow::vfs::InitStreamingFromManifest(manifest_file);

    if (callbacks.file_delete) {
      callbacks.file_delete("WoW.stor");
    } else {
      std::error_code ec;
      std::filesystem::remove("WoW.stor", ec);
    }

    if (result) {
      g_streaming_flags |= 2u;
    } else {
      g_streaming_error_text = openwow::vfs::GetStreamingStatusMessageText();
    }
  }

  bool online = false;
  if (callbacks.is_online_mode) {
    online = callbacks.is_online_mode();
  }
  if (online) {
    if (callbacks.init_online_sound_cache) {
      callbacks.init_online_sound_cache();
    } else {
      openwow::vfs::SFile2_InitSoundCache(
          reinterpret_cast<void*>(openwow::audio::OggVorbis_DecodeToWAV));
    }
  }

  openwow::vfs::SetStreamingFlags(
      static_cast<std::uint8_t>(g_streaming_flags & 1u),
      static_cast<std::uint8_t>((g_streaming_flags & 2u) != 0),
      static_cast<std::uint8_t>((g_streaming_flags & 4u) != 0),
      static_cast<std::uint8_t>((g_streaming_flags & 8u) != 0),
      kClientBuild);

  return 0;
}

int InitStreamingSubsystem(char has_new_account,
                           const ArchiveSystemCallbacks& callbacks) {

  int speed_test = 0;
  {
    bool reg_ok = false;
    if (callbacks.read_registry_value) {
      reg_ok = callbacks.read_registry_value(
          "World of Warcraft Trial", "SpeedTest", 1, &speed_test);
    }
    if (!reg_ok) {
      speed_test = 0;
    }
  }

  Streaming_ConfigureBgPreloadSleep(speed_test);

  Streaming_SetSpeedTest(speed_test);

  std::string locale;
  if (callbacks.cvar_get_string) {
    locale = callbacks.cvar_get_string("locale");
  }

  Streaming_SetLocale(locale.c_str());

  Streaming_SetBuildNumber(kClientBuild);

  Streaming_ReportStats(true, has_new_account != 0);

  return Streaming_RegisterFrameCallback(Streaming_RecordFrameDeltaHistogram);
}

const STORM_HANDLE* GetArchiveSlots() {
  return g_registered_archive_handles;
}

std::size_t GetArchiveHandleCount() {
  return g_registered_archive_handle_count;
}

std::size_t GetArchiveTableBaseIndex() {
  return g_registered_archive_table_base_index;
}

const ArchiveLocaleInfo& GetCurrentLocaleInfo() {
  return g_current_locale;
}

std::uint32_t GetStreamingFlags() {
  return g_streaming_flags;
}

const std::string& GetStreamingErrorText() {
  return g_streaming_error_text;
}

int GetStartupLevel() {
  return g_startup_level;
}

void SetStartupLevel(const int startup_level) {
  g_startup_level = startup_level;
}

std::size_t GetArchiveHandleCapacityForTests() {
  return g_registered_archive_handle_capacity;
}

bool ResizeRegisteredArchiveHandleArrayForTests(const std::size_t new_capacity) {
  return ResizeRegisteredArchiveHandleArray(new_capacity);
}

bool SetArchiveHandleForTests(const std::size_t index, STORM_HANDLE handle) {
  if (index >= g_registered_archive_handle_capacity ||
      g_registered_archive_handles == nullptr) {
    return false;
  }

  g_registered_archive_handles[index] = handle;
  g_registered_archive_handle_count = std::max(
      g_registered_archive_handle_count, index + 1);
  return true;
}

void SetArchiveTableBaseIndexForTests(const std::size_t index) {
  g_registered_archive_table_base_index = index;
}

void SetArchiveCloseCallbackForTests(const ArchiveCloseCallbackForTests callback) {
  g_archive_close_callback_for_tests = callback;
}

void SortPatchWildcardEntriesForTests(std::vector<std::string>* entries) {
  SortPatchWildcardEntries(entries);
}

void ResetArchiveSystemForTests() {
  delete[] g_registered_archive_handles;
  g_registered_archive_handles = nullptr;
  g_registered_archive_handle_capacity = 0;
  g_registered_archive_handle_count = 0;
  g_registered_archive_table_base_index = 0;
  g_archive_close_callback_for_tests = nullptr;
  for (int i = 0; i < kMaxArchiveSlots; ++i) {
    g_archive_slot_info[i] = {};
  }
  g_streaming_flags = 0;
  g_streaming_error_text.clear();
  g_current_locale = {};
  g_startup_level = 0;
}

}

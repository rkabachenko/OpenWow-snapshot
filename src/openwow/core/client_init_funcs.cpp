
#include "openwow/core/client_init.h"
#include "openwow/core/client_init_internal.h"
#include "openwow/core/client_misc.h"
#include "openwow/core/cobject_heap.h"
#include "openwow/core/mpq_internals.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/cvar.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/archive_system.h"
#include "openwow/game/commerce/auctions/auction_state.h"
#include "openwow/game/barber_shop.h"
#include "openwow/game/battlenet_events.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/currency_system.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/lfg_system.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/realm_list.h"
#include "openwow/net/realm_config_tables.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/vfs/retail/sfile_archive.h"
#include "openwow/vfs/retail/sfile_runtime.h"
#include "openwow/vfs/retail/streaming/data_preload_controller.h"

extern "C" {
#include <lua.hpp>
}

#include "openwow/ui/frame_script_events.h"
#include "openwow/ui/addon_manager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
#include <StormLib.h>
#endif

namespace openwow::core {

namespace {
std::int32_t g_streaming_integrity_flag = 0;
std::int32_t g_streaming_integrity_callback = 0;
constexpr std::array<const char *, 6> kEnterWorldReadOnlyCVarNames = {
    "realmList", "realmName", "decorateAccountName",
    "realmListbn", "portal", "serverAlert"};

void RegisterEnterWorldMovementCVars() {
  using openwow::ui::game::CVarFlags;

  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  cvars.RegisterCVar("showfootprintparticles", "1", CVarFlags::Archive,
                     "toggles rendering of footprint particles");
  cvars.RegisterCVar("pathDistTol", "1", CVarFlags::None,
                     "Sets acceptable distance from pathing destination in yards");
}

void BootstrapEnterWorldGameUiRuntime() {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  for (const char *name : kEnterWorldReadOnlyCVarNames) {
    if (!cvars.Exists(name)) {
      continue;
    }
    cvars.AddFlags(name, openwow::ui::game::CVarFlags::ReadOnly);
  }

  openwow::game::ResetMirrorTimersForEnterWorldInit();
  openwow::game::Chat_RegisterOpcodes();
  openwow::game::LFGSystem::Get().Reset();
  openwow::game::CalendarSystem::Get().Reset();
  openwow::game::BarberShop::Get().Reset();
  openwow::game::CurrencySystem::Get().InitCurrencyCategories();
}

std::string LowercaseArchiveIntegrityHashPath(const char *filename) {
  std::string normalized;
  if (!filename) {
    return normalized;
  }

  normalized.reserve(259);
  for (; *filename != '\0' && normalized.size() < 259; ++filename) {
    normalized.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(*filename))));
  }
  return normalized;
}

std::uint64_t ComputeArchiveIntegrityPathKey(
    const std::string_view normalized_path) {
  const auto hash_pair = JenkinsHashLittle2(
      std::span<const std::uint8_t>(
          reinterpret_cast<const std::uint8_t *>(normalized_path.data()),
          normalized_path.size()),
      2u, 1u);
  return (static_cast<std::uint64_t>(hash_pair.second) << 32) |
         static_cast<std::uint64_t>(hash_pair.first | 0x3Fu);
}

std::string ToggleArchiveIntegrityPathSlashes(const std::string &path) {
  std::string candidate = path;
  bool changed = false;
  for (char &ch : candidate) {
    if (ch == '/') {
      ch = '\\';
      changed = true;
    } else if (ch == '\\') {
      ch = '/';
      changed = true;
    }
  }
  if (!changed) {
    return {};
  }
  return candidate;
}

class ArchiveIntegritySignatureTable {
 public:
  using DigestWords = std::array<std::uint32_t, 4>;

  void Clear() {
    entries_.clear();
    count_ = 0;
  }

  bool Lookup(const std::uint64_t key, DigestWords &digest) const {
    std::size_t slot = 0;
    if (!FindSlot(key, slot)) {
      return false;
    }
    digest = entries_[slot].digest;
    return true;
  }

  bool Contains(const std::uint64_t key) const {
    DigestWords ignored{};
    return Lookup(key, ignored);
  }

  bool Insert(const std::uint64_t key, const DigestWords &digest) {
    if (entries_.empty() || count_ >= (entries_.size() * 3u) / 4u) {
      const std::size_t grown_capacity =
          std::max<std::size_t>(entries_.size() * 2u, 8u);
      Rehash(grown_capacity);
    }

    std::size_t slot = 0;
    if (FindSlot(key, slot)) {
      return false;
    }

    entries_[slot].key = key;
    entries_[slot].digest = digest;
    ++count_;
    return true;
  }

 private:
  struct Entry {
    std::uint64_t key = 0;
    DigestWords digest{};
  };

  bool FindSlot(const std::uint64_t key, std::size_t &slot) const {
    if (entries_.empty()) {
      slot = 0;
      return false;
    }

    const std::size_t mask = entries_.size() - 1u;
    slot = static_cast<std::size_t>(static_cast<std::uint32_t>(key)) & mask;
    while (entries_[slot].key != 0) {
      if (entries_[slot].key == key) {
        return true;
      }
      slot = (slot + 1u) & mask;
    }
    return false;
  }

  void Rehash(const std::size_t new_capacity) {
    const auto old_entries = std::move(entries_);
    const std::size_t old_count = count_;

    entries_.assign(new_capacity, {});
    for (const Entry &entry : old_entries) {
      if (entry.key != 0) {
        InsertRehashedEntry(entry);
      }
    }
    count_ = old_count;
  }

  void InsertRehashedEntry(const Entry &entry) {
    const std::size_t mask = entries_.size() - 1u;
    std::size_t slot =
        static_cast<std::size_t>(static_cast<std::uint32_t>(entry.key)) & mask;
    while (entries_[slot].key != 0) {
      slot = (slot + 1u) & mask;
    }
    entries_[slot] = entry;
  }

  std::vector<Entry> entries_;
  std::size_t count_ = 0;
};

struct ArchiveIntegrityState {
  std::mutex mutex;
  ArchiveIntegritySignatureTable signatures_by_key;
  std::unordered_set<std::uint64_t> marked_archive_file_keys;
  detail::ArchiveIntegrityLookup archive_lookup;
  std::optional<std::vector<std::uint8_t>> signature_file_contents_for_tests;
  std::optional<std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>>
      signature_verification_key_for_tests;
};

ArchiveIntegrityState &GetArchiveIntegrityState() {
  static ArchiveIntegrityState state;
  return state;
}

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
constexpr std::uint32_t kArchiveLookupStopFlag = 0x02000000u;
#endif

bool DefaultArchiveIntegrityLookup(const std::string &requested_path) {
  if (requested_path.empty()) {
    return false;
  }

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  auto lookup_registered_path = [](const char *path) {
    openwow::vfs::SFileArchiveLookupInfo lookup_info;
    const auto result =
        openwow::vfs::LookupRegisteredArchiveFile(nullptr, path, &lookup_info);
    if (result == openwow::vfs::SFileArchiveLookupResult::kPatchMarker
        || (lookup_info.file_flags & kArchiveLookupStopFlag) != 0u) {
      return openwow::vfs::SFileArchiveLookupResult::kPatchMarker;
    }
    return result;
  };

  const auto primary_registered_result = lookup_registered_path(requested_path.c_str());
  if (primary_registered_result == openwow::vfs::SFileArchiveLookupResult::kPatchMarker) {
    return false;
  }
  if (primary_registered_result == openwow::vfs::SFileArchiveLookupResult::kArchive) {
    return true;
  }

  const std::string alternate_path =
      ToggleArchiveIntegrityPathSlashes(requested_path);

  if (!alternate_path.empty() && alternate_path != requested_path) {
    const auto alternate_registered_result = lookup_registered_path(alternate_path.c_str());
    if (alternate_registered_result == openwow::vfs::SFileArchiveLookupResult::kPatchMarker) {
      return false;
    }
    if (alternate_registered_result == openwow::vfs::SFileArchiveLookupResult::kArchive) {
      return true;
    }
  }
#else
  (void)requested_path;
#endif

  return false;
}

detail::ArchiveIntegrityLookup GetArchiveIntegrityLookupLocked(const ArchiveIntegrityState &state) {
  if (state.archive_lookup) {
    return state.archive_lookup;
  }
  return DefaultArchiveIntegrityLookup;
}

constexpr std::string_view kSignatureFilePublicModulusHex =
    "4f6fd465568588d9aae6f70572bb2a20e024382b77b33dc2d32d525cb7a30fe6"
    "0f80938d5cc4815e9d588ecaa66f273652580dce65e2b57ad239748aacaac10e"
    "e5672b79c1fe089654182a3431a406204b12d2476353363819888e502f2fd168"
    "c85c9d3bdefe5621d1594d1c321b704b40e25e0c0e4b84d5d75200506581ce17"
    "5325499e40e904942ca7935c5d54c8f96ed069fce58fc0b676f2eb34679ff8d0"
    "34b9ea9e80d0298db4b2eb3ef44fd0844315f419e3c652a21840080721f420ec"
    "e65a9c2b08f6179772a6d7ab640b0495930dcc8f023091a2ee30e65f2fd413b5"
    "5dca5dcf7dc36f6b9dbc6d864c91c551e8fda63310d1128f0580da42570cebc0";

bool DecodeHexNibble(const char ch, std::uint8_t &value) {
  if (ch >= '0' && ch <= '9') {
    value = static_cast<std::uint8_t>(ch - '0');
    return true;
  }
  if (ch >= 'a' && ch <= 'f') {
    value = static_cast<std::uint8_t>(10 + (ch - 'a'));
    return true;
  }
  if (ch >= 'A' && ch <= 'F') {
    value = static_cast<std::uint8_t>(10 + (ch - 'A'));
    return true;
  }
  return false;
}

bool DecodeHexBytes(const std::string_view text, std::uint8_t *out_bytes,
                    const std::size_t out_size) {
  if (text.size() != out_size * 2 || out_bytes == nullptr) {
    return false;
  }

  for (std::size_t i = 0; i < out_size; ++i) {
    std::uint8_t high = 0;
    std::uint8_t low = 0;
    if (!DecodeHexNibble(text[i * 2], high) ||
        !DecodeHexNibble(text[i * 2 + 1], low)) {
      return false;
    }
    out_bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
  }

  return true;
}

const std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>> &
DefaultSignatureVerificationKey() {
  static const auto key = [] {
    std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>> result;
    result.first.resize(kSignatureFilePublicModulusHex.size() / 2);
    DecodeHexBytes(kSignatureFilePublicModulusHex, result.first.data(),
                   result.first.size());
    result.second = {0x01, 0x00, 0x01, 0x00};
    return result;
  }();
  return key;
}

std::optional<std::vector<std::uint8_t>> LoadSignatureFileBytes() {
  {
    auto &state = GetArchiveIntegrityState();
    std::lock_guard lock(state.mutex);
    if (state.signature_file_contents_for_tests) {
      return state.signature_file_contents_for_tests;
    }
  }

  void *loaded_data = nullptr;
  std::size_t loaded_size = 0;
  if (!openwow::vfs::SFileReadFileToBuffer_Wrapper(
          "signaturefile", &loaded_data, &loaded_size, 0, 0)) {
    return std::nullopt;
  }

  const auto cleanup = [&]() {
    openwow::vfs::SFileFreeLoadedData(loaded_data);
    loaded_data = nullptr;
  };

  std::vector<std::uint8_t> result(loaded_size);
  if (loaded_size != 0 && loaded_data != nullptr) {
    std::memcpy(result.data(), loaded_data, loaded_size);
  }
  cleanup();
  return result;
}

std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
ResolveSignatureVerificationKey() {
  auto &state = GetArchiveIntegrityState();
  std::lock_guard lock(state.mutex);
  if (state.signature_verification_key_for_tests) {
    return *state.signature_verification_key_for_tests;
  }
  return DefaultSignatureVerificationKey();
}

bool ParseDecimalLine(const std::string_view text, int &out_value) {
  if (text.empty()) {
    return false;
  }

  std::string owned(text);
  char *end = nullptr;
  const long parsed = std::strtol(owned.c_str(), &end, 10);
  if (end == owned.c_str() || *end != '\0') {
    return false;
  }

  out_value = static_cast<int>(parsed);
  return true;
}

std::uint32_t ReadLittleEndianTag(const std::string_view text) {
  std::uint32_t value = 0;
  const auto count = std::min<std::size_t>(text.size(), 4);
  for (std::size_t i = 0; i < count; ++i) {
    value |= static_cast<std::uint32_t>(
                 static_cast<unsigned char>(text[i]))
             << (i * 8);
  }
  return value;
}

bool NextSignatureFileLine(const std::vector<std::uint8_t> &file_bytes,
                           std::size_t &cursor, std::string_view &out_line) {
  if (cursor > file_bytes.size()) {
    return false;
  }

  const auto *begin = reinterpret_cast<const char *>(file_bytes.data());
  const auto total = file_bytes.size();
  std::size_t line_end = cursor;
  while (line_end + 1 < total) {
    if (file_bytes[line_end] == '\r' && file_bytes[line_end + 1] == '\n') {
      out_line = std::string_view(begin + cursor, line_end - cursor);
      cursor = line_end + 2;
      return true;
    }
    ++line_end;
  }

  return false;
}
}

void *ClientAlloc(const std::size_t size) {
  return SMemAlloc(size, ".\\Client.cpp", 3055, 0);
}

void ClientFree(void *ptr) {
  (void)SMemFree(ptr, ".\\Client.cpp", 3059, 0);
}

std::uint8_t GetExpansionLevel() {
  return static_cast<std::uint8_t>(openwow::data::GetStartupLevel());
}

void SetExpansionLevel(const std::uint8_t level) {
  openwow::data::SetStartupLevel(level);
}

SignatureFileResult LoadSignatureFile(const std::uint32_t locale_tag) {
  SignatureFileResult result;
  result.checksum = locale_tag;

  InitArchiveIntegrity();
  SetArchiveIntegrityValidationMode(true);

  const auto file_bytes = LoadSignatureFileBytes();
  if (!file_bytes) {
    return result;
  }

  const auto verification_key = ResolveSignatureVerificationKey();
  if (verification_key.first.empty() || verification_key.second.empty() ||
      !SSignature_VerifyData(
          file_bytes->data(), file_bytes->size(), verification_key.first.data(),
          static_cast<int>(verification_key.first.size()),
          verification_key.second.data(),
          static_cast<int>(verification_key.second.size()))) {
    return result;
  }

  std::size_t cursor = 0;
  std::string_view format_version_line;
  std::string_view signature_version_line;
  std::string_view scope_line;
  if (!NextSignatureFileLine(*file_bytes, cursor, format_version_line) ||
      !NextSignatureFileLine(*file_bytes, cursor, signature_version_line) ||
      !NextSignatureFileLine(*file_bytes, cursor, scope_line)) {
    return result;
  }

  int format_version = 0;
  int signature_version = 0;
  if (!ParseDecimalLine(format_version_line, format_version) ||
      !ParseDecimalLine(signature_version_line, signature_version)) {
    return result;
  }

  result.signature_version = signature_version;
  if (format_version != 2 || signature_version != 2 ||
      scope_line != "RELEASE") {
    return result;
  }

  const bool has_expansion = GetExpansionLevel() != 0;
  while (cursor < file_bytes->size()) {
    if (cursor + 4 <= file_bytes->size() &&
        std::memcmp(file_bytes->data() + cursor, "NGIS", 4) == 0) {
      break;
    }

    std::string_view line;
    if (!NextSignatureFileLine(*file_bytes, cursor, line)) {
      break;
    }
    if (line.size() < 40) {
      continue;
    }

    const std::uint32_t line_tag = ReadLittleEndianTag(line.substr(0, 4));
    if (line_tag != 0x65736162u && line_tag != locale_tag) {
      continue;
    }

    const char file_type = line[5];
    if (file_type != 'c' && !(file_type == 'e' && has_expansion)) {
      continue;
    }

    std::array<std::uint8_t, 16> hash{};
    if (!DecodeHexBytes(line.substr(7, 32), hash.data(), hash.size())) {
      continue;
    }

    const std::string path(line.substr(40));
    RegisterArchiveIntegrityHashForFile(path.c_str(), hash.data());
  }

  result.ok = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "Client_LoadSignatureFile: loaded (version=2, scope=RELEASE)");
  return result;
}

namespace {

void AppendBugReportWorldContext(std::string &report) {
  if (!openwow::core::GetRetailDebugActivePlayerState()) {
    return;
  }

  std::array<char, 512> line{};
  AppendRealmInfoToCrashDump(line.data(), static_cast<int>(line.size()));
  report += line.data();

  line.fill('\0');
  AppendLocalZoneInfoToCrashDump(nullptr, line.data(), static_cast<int>(line.size()));
  report += line.data();
}

}

std::string BuildBugReport() {
  std::string report;
  report.reserve(8192);

  report += "WoWBuild: ";
  report += std::to_string(kWoWBuild);
  report += "\r\n";

  AppendBugReportWorldContext(report);

  {
    lua_State* L =
        openwow::ui::frame_script_events::FrameScript_GetLuaStateTyped();
    if (L) {
      const int kb = static_cast<int>(lua_gc(L, LUA_GCCOUNT, 0));
      report += "Total lua memory: ";
      report += std::to_string(kb);
      report += "KB\r\n";
    }
  }

  {
    const auto& addon_manager = openwow::ui::AddonManager::Get();
    const size_t num_addons = addon_manager.GetNumAddons();
    if (num_addons > 0) {
      report += "Add Ons: ";
      bool first = true;
      for (size_t i = 0; i < num_addons; ++i) {
        const auto* info = addon_manager.GetAddonInfo(i);
        if (info && info->enabled) {
          if (!first) report += ", ";
          first = false;
          report += info->name;
        }
      }
      report += "\r\n";
    }
  }

  report += "Settings: \r\n";

  {
    std::vector<char> cvar_buf(8192, '\0');
    ida::CVar_AppendAllToBuffer(cvar_buf.data(),
                           static_cast<int>(cvar_buf.size()), 0, 0x40);
    if (cvar_buf[0] != '\0') {
      report += cvar_buf.data();
    }
  }

  return report;
}

bool EnterWorldInit(const EnterWorldInitParams &params,
                    openwow::audio::SoundRuntime& sound_runtime) {

  if (const auto selected_realm = openwow::game::RealmList::Get().GetSelectedRealm()) {
    (void)openwow::net::RealmConfigTables::Get()
        .UpdateSelectedRealmPlayerKillingAllowed(
            static_cast<std::uint32_t>(selected_realm->type));
  }

  MoveLogFile_ref();
  ObjectMgr_InitTypeRegistry();
  RegisterEnterWorldMovementCVars();
  BootstrapEnterWorldGameUiRuntime();
  sound_runtime.RegisterEnterWorldAudioCallbacks();
  openwow::game::CGCorpse_C::RegisterFieldHandlers();

  sound_runtime.InitializeZoneMusicRepeatDelayState();
  Console_RegisterDebugCommands();

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "Client_EnterWorldInit: map=" + std::to_string(params.map_id));

  openwow::game::BattleNetUI::ApplyNameFormat();

  return true;
}

void ProcessRunOnceFiles() {
  detail::ProcessRunOnceFilesWithCallback(
      [](const std::string &filename) { ida::CVar_LoadFromFile(filename); });
}

void SetConvertedTrialFlag(const bool converted) {
  openwow::vfs::SetDataPreloadConvertedTrialFlag(converted);
}

void InitArchiveIntegrity() {
  auto &state = GetArchiveIntegrityState();
  std::lock_guard lock(state.mutex);
  state.marked_archive_file_keys.clear();

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                     "InitArchiveIntegrity: archive integrity scan");
}

bool RegisterArchiveIntegrityHashForFile(const char *filename,
                                         const std::uint8_t expected_hash[16]) {
  const std::string normalized_path =
      LowercaseArchiveIntegrityHashPath(filename);
  if (normalized_path.empty()) {
    return false;
  }
  const std::uint64_t path_key = ComputeArchiveIntegrityPathKey(normalized_path);
  const std::string requested_path = filename != nullptr ? std::string(filename)
                                                         : std::string{};

  auto &state = GetArchiveIntegrityState();
  detail::ArchiveIntegrityLookup lookup;
  {
    std::lock_guard lock(state.mutex);
    lookup = GetArchiveIntegrityLookupLocked(state);
  }

  if (!lookup(requested_path)) {
    return false;
  }

  ArchiveIntegritySignatureTable::DigestWords signature_words{};
  std::memcpy(signature_words.data(), expected_hash, sizeof(signature_words));

  {
    std::lock_guard lock(state.mutex);
    if (!state.signatures_by_key.Insert(path_key, signature_words)) {
      return false;
    }
    state.marked_archive_file_keys.insert(path_key);
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                     "RegisterArchiveIntegrityHashForFile: " + normalized_path);
  return true;
}

void SetArchiveIntegrityValidationMode(const bool enable) {
  SetStreamingIntegrityFlag(enable ? 1 : 0);
  if (enable) {

    SetStreamingIntegrityCallback(1);
  }
}

void SetStreamingIntegrityFlag(const std::int32_t value) {
  g_streaming_integrity_flag = value;
}

void SetStreamingIntegrityCallback(const std::int32_t callback_id) {
  g_streaming_integrity_callback = callback_id;
}

namespace detail {

bool LookupArchiveIntegrityDigestForPath(const char* path,
                                         std::uint8_t out_digest[16]) {
  if (out_digest == nullptr) {
    return false;
  }

  const std::string normalized_path = LowercaseArchiveIntegrityHashPath(path);
  if (normalized_path.empty()) {
    return false;
  }

  const std::uint64_t path_key = ComputeArchiveIntegrityPathKey(normalized_path);
  ArchiveIntegritySignatureTable::DigestWords digest_words{};
  {
    auto& state = GetArchiveIntegrityState();
    std::lock_guard lock(state.mutex);
    if (!state.signatures_by_key.Lookup(path_key, digest_words)) {
      return false;
    }
  }

  std::memcpy(out_digest, digest_words.data(), sizeof(digest_words));
  return true;
}

bool ShouldResolveArchiveIntegrityDigestForPath(const char* path) {
  const std::string normalized_path = LowercaseArchiveIntegrityHashPath(path);
  if (normalized_path.empty()) {
    return false;
  }

  const std::uint64_t path_key = ComputeArchiveIntegrityPathKey(normalized_path);
  auto& state = GetArchiveIntegrityState();
  std::lock_guard lock(state.mutex);
  return state.marked_archive_file_keys.contains(path_key);
}

void ResetArchiveIntegrityForTests() {
  auto &state = GetArchiveIntegrityState();
  std::lock_guard lock(state.mutex);
  state.signatures_by_key.Clear();
  state.marked_archive_file_keys.clear();
  state.archive_lookup = {};
  state.signature_file_contents_for_tests.reset();
  state.signature_verification_key_for_tests.reset();
  g_streaming_integrity_flag = 0;
  g_streaming_integrity_callback = 0;
}

void SetArchiveIntegrityLookupForTests(ArchiveIntegrityLookup lookup) {
  auto &state = GetArchiveIntegrityState();
  std::lock_guard lock(state.mutex);
  state.archive_lookup = std::move(lookup);
}

bool HasArchiveIntegritySignatureForTests(const std::string &path) {
  auto &state = GetArchiveIntegrityState();
  const std::string normalized_path =
      LowercaseArchiveIntegrityHashPath(path.c_str());
  if (normalized_path.empty()) {
    return false;
  }
  const std::uint64_t path_key = ComputeArchiveIntegrityPathKey(normalized_path);
  std::lock_guard lock(state.mutex);
  return state.signatures_by_key.Contains(path_key);
}

bool IsArchiveIntegrityFileMarkedForTests(const std::string &path) {
  auto &state = GetArchiveIntegrityState();
  const std::string normalized_path =
      LowercaseArchiveIntegrityHashPath(path.c_str());
  if (normalized_path.empty()) {
    return false;
  }
  const std::uint64_t path_key = ComputeArchiveIntegrityPathKey(normalized_path);
  std::lock_guard lock(state.mutex);
  return state.marked_archive_file_keys.contains(path_key);
}

std::int32_t GetStreamingIntegrityFlagForTests() {
  return g_streaming_integrity_flag;
}

std::int32_t GetStreamingIntegrityCallbackForTests() {
  return g_streaming_integrity_callback;
}

void SetSignatureFileContentsForTests(std::vector<std::uint8_t> bytes) {
  auto &state = GetArchiveIntegrityState();
  std::lock_guard lock(state.mutex);
  state.signature_file_contents_for_tests = std::move(bytes);
}

void SetSignatureVerificationKeyForTests(std::vector<std::uint8_t> modulus,
                                         std::vector<std::uint8_t> exponent) {
  auto &state = GetArchiveIntegrityState();
  std::lock_guard lock(state.mutex);
  state.signature_verification_key_for_tests =
      std::make_pair(std::move(modulus), std::move(exponent));
}

}

std::optional<std::array<std::uint8_t, 16>> FindArchiveIntegrityDigestForFile(
    const char* const filename) {
  std::array<std::uint8_t, 16> digest{};
  if (!detail::LookupArchiveIntegrityDigestForPath(filename, digest.data())) {
    return std::nullopt;
  }
  return digest;
}

}

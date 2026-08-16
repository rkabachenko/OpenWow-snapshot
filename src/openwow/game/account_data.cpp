
#include "openwow/game/account_data.h"
#include "openwow/core/md5.h"
#include "openwow/game/chat_cache.h"
#include "openwow/platform/filesystem/filesystem.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/network/serialization/zlib_compression.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <zlib.h>

namespace openwow::game {

using openwow::diagnostics::Log;
using openwow::diagnostics::LogLevel;

namespace {

constexpr std::size_t kAccountDataDigestSize = 16;
constexpr std::size_t kAccountDataSyncRecordSize = 20;

bool WriteAccountDataFile(const std::filesystem::path& path, const std::string& data) {
  return openwow::platform::filesystem::AtomicWriteFile(path, data);
}

}

const char* AccountDataTypeName(AccountDataType type) {
  switch (type) {
    case AccountDataType::GlobalConfig:        return "GlobalConfig";
    case AccountDataType::PerCharacterConfig:  return "PerCharacterConfig";
    case AccountDataType::GlobalBindings:      return "GlobalBindings";
    case AccountDataType::PerCharacterBindings: return "PerCharacterBindings";
    case AccountDataType::GlobalMacros:        return "GlobalMacros";
    case AccountDataType::PerCharacterMacros:  return "PerCharacterMacros";
    case AccountDataType::PerCharacterLayout:  return "PerCharacterLayout";
    case AccountDataType::PerCharacterChat:    return "PerCharacterChat";
    default:                                   return "Unknown";
  }
}

AccountData& AccountData::Get() {
  static AccountData instance;
  return instance;
}

void AccountData::SetAccountDataTimes(
    const std::array<std::uint32_t, 8>& timestamps) {
  std::lock_guard lock(mutex_);
  for (std::size_t i = 0; i < 8; ++i) {
    entries_[i].synchronized_timestamp = timestamps[i];
  }
  if (HasUploadableEntryLocked()) {
    ScheduleUploadLocked();
  }
  Log(LogLevel::kInfo, "AccountData: received data times from server");
}

void AccountData::SetNextUploadSequence(const std::uint32_t next_sequence) {
  std::lock_guard lock(mutex_);
  next_upload_sequence_ = std::max(next_upload_sequence_, next_sequence);
}

void AccountData::SetAccountData(AccountDataType type, std::uint32_t timestamp,
                                 const std::string& data) {
  auto idx = static_cast<std::size_t>(type);
  if (idx >= 8) return;

  std::lock_guard lock(mutex_);
  entries_[idx].data = data;
  entries_[idx].timestamp = timestamp;
  entries_[idx].synchronized_timestamp = timestamp;
  entries_[idx].dirty = false;
  entries_[idx].disk_dirty = true;
  entries_[idx].server_download_pending = false;
  entries_[idx].synchronized_digest = ComputeDataDigest(data);
  entries_[idx].synchronized_digest_mismatch = false;
  ++entries_[idx].mutation_generation;
  next_upload_sequence_ = std::max(next_upload_sequence_, timestamp + 1u);

  if (persistence_identity_.has_value()) {
    if (!SaveToDiskLocked(*persistence_identity_)) {
      ScheduleUploadLocked();
    }
  }

  Log(LogLevel::kInfo, "AccountData: set " + std::string(AccountDataTypeName(type)) +
                           " (" + std::to_string(data.size()) + " bytes)");
}

std::string AccountData::GetData(AccountDataType type) const {
  auto idx = static_cast<std::size_t>(type);
  std::lock_guard lock(mutex_);
  if (idx >= 8) return {};
  return entries_[idx].data;
}

std::uint32_t AccountData::GetTimestamp(AccountDataType type) const {
  auto idx = static_cast<std::size_t>(type);
  std::lock_guard lock(mutex_);
  if (idx >= 8) return 0;
  return entries_[idx].timestamp;
}

std::uint32_t AccountData::GetSynchronizedTimestamp(AccountDataType type) const {
  auto idx = static_cast<std::size_t>(type);
  std::lock_guard lock(mutex_);
  if (idx >= 8) return 0;
  return entries_[idx].synchronized_timestamp;
}

void AccountData::MarkDirty(AccountDataType type) {
  auto idx = static_cast<std::size_t>(type);
  if (idx >= 8) return;
  std::lock_guard lock(mutex_);
  auto& entry = entries_[idx];
  entry.dirty = true;
  entry.disk_dirty = true;
  ++entry.mutation_generation;
  ScheduleUploadLocked();
  if (persistence_identity_.has_value()) {
    (void)SaveToDiskLocked(*persistence_identity_);
  }
}

void AccountData::SyncLocalData(AccountDataType type, const std::string& data) {
  auto idx = static_cast<std::size_t>(type);
  if (idx >= 8) {
    return;
  }

  std::lock_guard lock(mutex_);
  if (entries_[idx].data == data) {
    if (entries_[idx].disk_dirty && persistence_identity_.has_value()) {
      (void)SaveToDiskLocked(*persistence_identity_);
    }
    return;
  }

  entries_[idx].data = data;
  entries_[idx].dirty = true;
  entries_[idx].disk_dirty = true;
  ++entries_[idx].mutation_generation;
  ScheduleUploadLocked();
  if (persistence_identity_.has_value()) {
    (void)SaveToDiskLocked(*persistence_identity_);
  }
}

bool AccountData::IsDirty(AccountDataType type) const {
  auto idx = static_cast<std::size_t>(type);
  if (idx >= 8) return false;
  std::lock_guard lock(mutex_);
  return entries_[idx].dirty;
}

void AccountData::ClearDirty(AccountDataType type) {
  auto idx = static_cast<std::size_t>(type);
  if (idx >= 8) return;
  std::lock_guard lock(mutex_);
  entries_[idx].dirty = false;
  if (!HasUploadableEntryLocked()) {
    upload_scheduled_ = false;
  }
}

std::uint32_t AccountData::GetDirtyMask() const {
  std::lock_guard lock(mutex_);
  std::uint32_t mask = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    if (entries_[i].dirty) mask |= (1u << i);
  }
  return mask;
}

bool AccountData::ShouldUpload(AccountDataType type) const {
  const auto idx = static_cast<std::size_t>(type);
  if (idx >= entries_.size()) {
    return false;
  }

  std::lock_guard lock(mutex_);
  const auto& entry = entries_[idx];
  return entry.dirty || entry.synchronized_digest_mismatch
         || entry.timestamp > entry.synchronized_timestamp;
}

std::optional<AccountData::UploadPayload> AccountData::SnapshotForUpload(
    const AccountDataType type) const {
  const auto idx = static_cast<std::size_t>(type);
  if (idx >= entries_.size()) {
    return std::nullopt;
  }

  std::lock_guard lock(mutex_);
  const auto& entry = entries_[idx];
  if (!entry.dirty && !entry.synchronized_digest_mismatch &&
      entry.timestamp <= entry.synchronized_timestamp) {
    return std::nullopt;
  }
  return UploadPayload{entry.data, entry.mutation_generation};
}

std::uint32_t AccountData::AllocateUploadSequence() {
  std::lock_guard lock(mutex_);
  return next_upload_sequence_++;
}

void AccountData::MarkUploaded(AccountDataType type,
                               const std::uint32_t sequence,
                               const UploadPayload& payload) {
  const auto idx = static_cast<std::size_t>(type);
  if (idx >= entries_.size()) {
    return;
  }

  std::lock_guard lock(mutex_);
  auto& entry = entries_[idx];
  entry.timestamp = sequence;
  entry.synchronized_timestamp = sequence;
  entry.disk_dirty = true;
  entry.server_download_pending = false;
  entry.synchronized_digest = ComputeDataDigest(payload.data);
  const bool same_generation =
      entry.mutation_generation == payload.generation &&
      entry.data == payload.data;
  entry.dirty = !same_generation;
  entry.synchronized_digest_mismatch =
      !same_generation &&
      ComputeDataDigest(entry.data) != entry.synchronized_digest;
  next_upload_sequence_ = std::max(next_upload_sequence_, sequence + 1u);
  if (persistence_identity_.has_value()) {
    (void)SaveToDiskLocked(*persistence_identity_);
  }
}

bool AccountData::IsUploadDue(const UploadClock::time_point now) const {
  std::lock_guard lock(mutex_);
  return upload_scheduled_ && now >= upload_deadline_;
}

void AccountData::FinishUploadAttempt(const bool had_failure,
                                      const UploadClock::time_point now) {
  std::lock_guard lock(mutex_);
  if (had_failure || HasUploadableEntryLocked()) {
    ScheduleUploadLocked(now);
  } else {
    upload_scheduled_ = false;
  }
}

bool AccountData::ShouldDownload(AccountDataType type) const {
  const auto idx = static_cast<std::size_t>(type);
  if (idx >= entries_.size()) {
    return false;
  }

  std::lock_guard lock(mutex_);
  const auto& entry = entries_[idx];
  return entry.dirty || entry.timestamp != entry.synchronized_timestamp;
}

bool AccountData::MarkServerDownloadPending(AccountDataType type) {
  const auto idx = static_cast<std::size_t>(type);
  if (idx >= entries_.size()) {
    return false;
  }

  std::lock_guard lock(mutex_);
  auto& entry = entries_[idx];
  if (entry.server_download_pending) {
    return false;
  }

  entry.server_download_pending = true;
  return true;
}

void AccountData::ClearServerDownloadPending(AccountDataType type) {
  const auto idx = static_cast<std::size_t>(type);
  if (idx >= entries_.size()) {
    return;
  }

  std::lock_guard lock(mutex_);
  entries_[idx].server_download_pending = false;
}

AccountData::ServerDownloadResolution AccountData::ResolveServerDownload(
    AccountDataType type, const std::uint32_t server_timestamp) {
  const auto idx = static_cast<std::size_t>(type);
  if (idx >= entries_.size()) {
    return {};
  }

  std::lock_guard lock(mutex_);
  auto& entry = entries_[idx];
  if (!entry.server_download_pending) {
    return {};
  }

  entry.server_download_pending = false;
  return {
      .had_pending_request = true,
      .should_apply_payload = entry.dirty || entry.timestamp < server_timestamp,
  };
}

std::vector<std::uint8_t> AccountData::Compress(const std::string& data) {
  if (data.empty()) {
    return {};
  }

  const auto compressed = network::serialization::CompressZlib(
      reinterpret_cast<const std::uint8_t*>(data.data()), data.size(),
      network::serialization::ZlibCompressionLevel::kBestSpeed);
  if (compressed.empty()) {
    Log(LogLevel::kWarn, "AccountData: compress failed");
    return {};
  }
  return compressed;
}

std::string AccountData::Decompress(const std::vector<std::uint8_t>& compressed,
                                    std::uint32_t uncompressed_size) {
  if (compressed.empty() || uncompressed_size == 0) return {};

  std::string result(uncompressed_size, '\0');
  uLongf dest_len = uncompressed_size;
  int rc = uncompress(reinterpret_cast<Bytef*>(result.data()), &dest_len,
                      compressed.data(),
                      static_cast<uLong>(compressed.size()));
  if (rc != Z_OK) {
    Log(LogLevel::kWarn, "AccountData: decompress failed with error " +
                             std::to_string(rc));
    return {};
  }
  result.resize(dest_len);
  return result;
}

bool AccountData::IsConfigCacheType(const AccountDataType type) {
  return type == AccountDataType::GlobalConfig
         || type == AccountDataType::PerCharacterConfig;
}

bool AccountData::IsScopeType(const AccountDataType type,
                              const bool per_character_scope) {
  return IsPerCharacterData(type) == per_character_scope;
}

std::uint32_t AccountData::ReadLe32(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0])
         | (static_cast<std::uint32_t>(bytes[1]) << 8u)
         | (static_cast<std::uint32_t>(bytes[2]) << 16u)
         | (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

void AccountData::WriteLe32(std::uint8_t* bytes, const std::uint32_t value) {
  bytes[0] = static_cast<std::uint8_t>(value & 0xFFu);
  bytes[1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
  bytes[2] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
  bytes[3] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

std::array<std::uint8_t, 16> AccountData::ComputeDataDigest(
    const std::string& data) {
  return openwow::core::MD5_Digest(data.data(), data.size());
}

std::string AccountData::BuildSyncMetadataPath(const std::string& wtf_dir,
                                               const std::string& account_name,
                                               const std::string& realm_name,
                                               const std::string& char_name,
                                               const bool per_character_scope) {
  namespace fs = std::filesystem;

  fs::path base = fs::path(wtf_dir) / "Account" / account_name;
  if (per_character_scope) {
    base = base / realm_name / char_name;
  }

  return (base / "cache.md5").string();
}

bool AccountData::ShouldClearConfigCacheOnLoadLocked(const AccountDataType type) const {
  switch (type) {
    case AccountDataType::GlobalConfig:
      return clear_global_config_cache_on_load_;
    case AccountDataType::PerCharacterConfig:
      return clear_character_config_cache_on_load_;
    case AccountDataType::GlobalBindings:
    case AccountDataType::PerCharacterBindings:
    case AccountDataType::GlobalMacros:
    case AccountDataType::PerCharacterMacros:
    case AccountDataType::PerCharacterLayout:
    case AccountDataType::PerCharacterChat:
    case AccountDataType::NumTypes:
      return false;
  }

  return false;
}

bool AccountData::ClearConfigCacheFileLocked(const std::string& wtf_dir,
                                             const std::string& account_name,
                                             const std::string& realm_name,
                                             const std::string& char_name,
                                             const AccountDataType type) {
  namespace fs = std::filesystem;

  if (!IsConfigCacheType(type)) {
    return false;
  }

  const auto idx = static_cast<std::size_t>(type);
  const fs::path path = BuildPath(wtf_dir, account_name, realm_name, char_name, type);
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  const bool wrote_file = !ec && WriteAccountDataFile(path, "");

  auto& entry = entries_[idx];
  entry.data.clear();
  entry.timestamp = 0;
  entry.synchronized_timestamp = 0;
  entry.dirty = false;
  entry.disk_dirty = !wrote_file;
  entry.server_download_pending = false;
  entry.synchronized_digest.fill(0u);
  entry.synchronized_digest_mismatch = false;
  return wrote_file;
}

void AccountData::ResetLoadedStateLocked() {
  for (auto& entry : entries_) {
    entry.data.clear();
    entry.timestamp = 0;
    entry.synchronized_timestamp = 0;
    entry.dirty = false;
    entry.disk_dirty = false;
    entry.server_download_pending = false;
    entry.synchronized_digest.fill(0u);
    entry.synchronized_digest_mismatch = false;
    entry.mutation_generation = 0;
  }
  upload_scheduled_ = false;
}

void AccountData::UpdateSynchronizedDigestMismatchLocked(
    const AccountDataType type, const bool file_was_loaded) {
  const auto idx = static_cast<std::size_t>(type);
  if (idx >= entries_.size()) {
    return;
  }

  auto& entry = entries_[idx];
  if (!file_was_loaded) {
    entry.synchronized_digest_mismatch =
        entry.synchronized_digest != std::array<std::uint8_t, 16>{};
    return;
  }

  entry.synchronized_digest_mismatch =
      ComputeDataDigest(entry.data) != entry.synchronized_digest;
}

bool AccountData::LoadScopeSyncMetadataLocked(const std::string& wtf_dir,
                                              const std::string& account_name,
                                              const std::string& realm_name,
                                              const std::string& char_name,
                                              const bool per_character_scope) {
  namespace fs = std::filesystem;

  if (per_character_scope && (realm_name.empty() || char_name.empty())) {
    return false;
  }

  const fs::path path = BuildSyncMetadataPath(
      wtf_dir, account_name, realm_name, char_name, per_character_scope);
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return false;
  }

  std::string bytes((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
  std::size_t record_count = 0;
  for (std::uint8_t raw_type = 0;
       raw_type < static_cast<std::uint8_t>(AccountDataType::NumTypes);
       ++raw_type) {
    if (IsScopeType(static_cast<AccountDataType>(raw_type),
                    per_character_scope)) {
      ++record_count;
    }
  }

  if (bytes.size() != record_count * kAccountDataSyncRecordSize) {
    return false;
  }

  std::size_t offset = 0;
  for (std::uint8_t raw_type = 0;
       raw_type < static_cast<std::uint8_t>(AccountDataType::NumTypes);
       ++raw_type) {
    const auto type = static_cast<AccountDataType>(raw_type);
    if (!IsScopeType(type, per_character_scope)) {
      continue;
    }

    auto& entry = entries_[raw_type];
    std::memcpy(entry.synchronized_digest.data(),
                bytes.data() + offset,
                kAccountDataDigestSize);
    entry.timestamp = ReadLe32(
        reinterpret_cast<const std::uint8_t*>(bytes.data() + offset
                                              + kAccountDataDigestSize));

    entry.synchronized_timestamp = entry.timestamp;
    entry.synchronized_digest_mismatch = false;
    offset += kAccountDataSyncRecordSize;
  }

  return true;
}

bool AccountData::WriteScopeSyncMetadataLocked(const std::string& wtf_dir,
                                               const std::string& account_name,
                                               const std::string& realm_name,
                                               const std::string& char_name,
                                               const bool per_character_scope) {
  namespace fs = std::filesystem;

  if (per_character_scope && (realm_name.empty() || char_name.empty())) {
    return false;
  }

  const fs::path path = BuildSyncMetadataPath(
      wtf_dir, account_name, realm_name, char_name, per_character_scope);
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) {
    return false;
  }

  std::string serialized;
  serialized.reserve(kAccountDataSyncRecordSize * entries_.size());
  std::array<std::uint8_t, kAccountDataSyncRecordSize> record{};
  for (std::uint8_t raw_type = 0;
       raw_type < static_cast<std::uint8_t>(AccountDataType::NumTypes);
       ++raw_type) {
    const auto type = static_cast<AccountDataType>(raw_type);
    if (!IsScopeType(type, per_character_scope)) {
      continue;
    }

    const auto& entry = entries_[raw_type];
    record.fill(0u);
    std::memcpy(record.data(),
                entry.synchronized_digest.data(),
                kAccountDataDigestSize);
    WriteLe32(record.data() + kAccountDataDigestSize, entry.timestamp);
    serialized.append(reinterpret_cast<const char*>(record.data()),
                      record.size());
  }

  return openwow::platform::filesystem::AtomicWriteFile(path, serialized);
}

void AccountData::SaveBindings(const std::string& content, bool is_global) {
  auto type = is_global ? AccountDataType::GlobalBindings
                        : AccountDataType::PerCharacterBindings;
  SyncLocalData(type, content);
}

void AccountData::SaveMacros(const std::string& content, bool is_global) {
  auto type = is_global ? AccountDataType::GlobalMacros
                        : AccountDataType::PerCharacterMacros;
  SyncLocalData(type, content);
}

void AccountData::SaveChat(const std::string& content) {
  SyncLocalData(AccountDataType::PerCharacterChat, content);
}

void AccountData::SaveLayout(const std::string& content) {
  SyncLocalData(AccountDataType::PerCharacterLayout, content);
}

void AccountData::ClearType(AccountDataType type) {
  const auto idx = static_cast<std::size_t>(type);
  if (idx >= entries_.size()) {
    return;
  }

  std::lock_guard lock(mutex_);
  entries_[idx].data.clear();
  entries_[idx].timestamp = 0;
  entries_[idx].synchronized_timestamp = 0;
  entries_[idx].dirty = false;
  entries_[idx].disk_dirty = false;
  entries_[idx].server_download_pending = false;
  entries_[idx].synchronized_digest.fill(0u);
  entries_[idx].synchronized_digest_mismatch = false;
  ++entries_[idx].mutation_generation;
  if (!HasUploadableEntryLocked()) {
    upload_scheduled_ = false;
  }
}

void AccountData::SetConfigCacheClearEnabled(const bool enabled) {
  std::lock_guard lock(mutex_);
  clear_global_config_cache_on_load_ = enabled;
  clear_character_config_cache_on_load_ = enabled;
}

bool AccountData::IsConfigCacheClearEnabled(const AccountDataType type) const {
  std::lock_guard lock(mutex_);
  return ShouldClearConfigCacheOnLoadLocked(type);
}

std::string AccountData::GetBindings(bool is_global) const {
  auto type = is_global ? AccountDataType::GlobalBindings
                        : AccountDataType::PerCharacterBindings;
  return GetData(type);
}

std::string AccountData::GetMacros(bool is_global) const {
  auto type = is_global ? AccountDataType::GlobalMacros
                        : AccountDataType::PerCharacterMacros;
  return GetData(type);
}

std::string AccountData::GetChat() const {
  return GetData(AccountDataType::PerCharacterChat);
}

std::string AccountData::GetLayout() const {
  return GetData(AccountDataType::PerCharacterLayout);
}

std::string AccountData::FileNameForType(AccountDataType type) {
  switch (type) {
    case AccountDataType::GlobalConfig:        return "config-cache.wtf";
    case AccountDataType::PerCharacterConfig:  return "config-cache.wtf";
    case AccountDataType::GlobalBindings:      return "bindings-cache.wtf";
    case AccountDataType::PerCharacterBindings: return "bindings-cache.wtf";
    case AccountDataType::GlobalMacros:        return "macros-cache.txt";
    case AccountDataType::PerCharacterMacros:  return "macros-cache.txt";
    case AccountDataType::PerCharacterLayout:  return "layout-local.txt";
    case AccountDataType::PerCharacterChat:    return "chat-cache.txt";
    default:                                   return "unknown.dat";
  }
}

std::string AccountData::BuildPath(const std::string& wtf_dir,
                                   const std::string& account_name,
                                   const std::string& realm_name,
                                   const std::string& char_name,
                                   AccountDataType type) {
  namespace fs = std::filesystem;
  fs::path base = fs::path(wtf_dir) / "Account" / account_name;

  if (IsPerCharacterData(type)) {
    base = base / realm_name / char_name;
  }

  return (base / FileNameForType(type)).string();
}

bool AccountData::LoadTypeFromDiskLocked(const std::string& wtf_dir,
                                         const std::string& account_name,
                                         const std::string& realm_name,
                                         const std::string& char_name,
                                         AccountDataType type) {
  namespace fs = std::filesystem;

  const auto idx = static_cast<std::size_t>(type);
  if (idx >= 8) {
    return false;
  }

  if (!openwow::platform::filesystem::IsSafePathComponent(account_name) ||
      (IsPerCharacterData(type) &&
       (!openwow::platform::filesystem::IsSafePathComponent(realm_name) ||
        !openwow::platform::filesystem::IsSafePathComponent(char_name)))) {
    return false;
  }

  const fs::path path = BuildPath(wtf_dir, account_name, realm_name, char_name, type);
  bool loaded = false;

  std::error_code ec;
  if (!fs::exists(path, ec) || ec) {

    if (ShouldClearConfigCacheOnLoadLocked(type)) {
      return ClearConfigCacheFileLocked(
          wtf_dir, account_name, realm_name, char_name, type);
    }
    UpdateSynchronizedDigestMismatchLocked(type, false);
    return false;
  }

  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    if (ShouldClearConfigCacheOnLoadLocked(type)) {
      return ClearConfigCacheFileLocked(
          wtf_dir, account_name, realm_name, char_name, type);
    }
    UpdateSynchronizedDigestMismatchLocked(type, false);
    return false;
  }

  std::ostringstream ss;
  ss << in.rdbuf();
  entries_[idx].data = ss.str();
  entries_[idx].dirty = false;
  entries_[idx].disk_dirty = false;
  loaded = true;
  UpdateSynchronizedDigestMismatchLocked(type, true);

  Log(LogLevel::kInfo, "AccountData: loaded " +
                           std::string(AccountDataTypeName(type)) + " (" +
                           std::to_string(entries_[idx].data.size()) +
                           " bytes)");

  if (ShouldClearConfigCacheOnLoadLocked(type)) {
    return ClearConfigCacheFileLocked(
               wtf_dir, account_name, realm_name, char_name, type)
           || loaded;
  }

  return loaded;
}

bool AccountData::LoadTypeFromDisk(const std::string& wtf_dir,
                                   const std::string& account_name,
                                   const std::string& realm_name,
                                   const std::string& char_name,
                                   AccountDataType type) {
  std::lock_guard lock(mutex_);
  return LoadTypeFromDiskLocked(
      wtf_dir, account_name, realm_name, char_name, type);
}

bool AccountData::CapturePersistenceIdentityLocked(
    const std::string& wtf_dir, const std::string& account_name,
    const std::string& realm_name, const std::string& char_name) {
  namespace fs = std::filesystem;

  if (wtf_dir.empty() || !openwow::platform::filesystem::IsSafePathComponent(account_name) ||
      !openwow::platform::filesystem::IsSafePathComponent(realm_name) ||
      !openwow::platform::filesystem::IsSafePathComponent(char_name)) {
    return false;
  }

  const fs::path wtf_path(wtf_dir);
  const fs::path account_root = wtf_path / "Account";
  const auto resolved_account =
      openwow::platform::filesystem::ResolveExistingPathComponentCaseInsensitive(account_root,
                                                                 account_name);
  if (!resolved_account.has_value()) {
    return false;
  }
  const fs::path account_path = account_root / *resolved_account;
  const auto resolved_realm =
      openwow::platform::filesystem::ResolveExistingPathComponentCaseInsensitive(account_path,
                                                                 realm_name);
  if (!resolved_realm.has_value()) {
    return false;
  }
  const fs::path realm_path = account_path / *resolved_realm;
  const auto resolved_character =
      openwow::platform::filesystem::ResolveExistingPathComponentCaseInsensitive(realm_path,
                                                                 char_name);
  if (!resolved_character.has_value()) {
    return false;
  }

  const fs::path character_path = realm_path / *resolved_character;
  if (!openwow::platform::filesystem::IsSafeChildPath(wtf_path, character_path)) {
    return false;
  }

  PersistenceIdentity candidate{
      .wtf_dir = wtf_path,
      .account_name = *resolved_account,
      .realm_name = *resolved_realm,
      .character_name = *resolved_character,
  };
  if (persistence_identity_.has_value()) {
    const auto& current = *persistence_identity_;
    return current.wtf_dir == candidate.wtf_dir &&
           current.account_name == candidate.account_name &&
           current.realm_name == candidate.realm_name &&
           current.character_name == candidate.character_name;
  }

  persistence_identity_ = std::move(candidate);
  return true;
}

void AccountData::LoadFromDisk(const std::string& wtf_dir,
                               const std::string& account_name,
                               const std::string& realm_name,
                               const std::string& char_name) {
  std::lock_guard lock(mutex_);

  if (!CapturePersistenceIdentityLocked(wtf_dir, account_name, realm_name,
                                        char_name)) {
    Log(LogLevel::kWarn,
        "AccountData: rejected invalid or changing persistence identity");
    return;
  }
  const auto& identity = *persistence_identity_;

  ResetLoadedStateLocked();
  (void)LoadScopeSyncMetadataLocked(
      identity.wtf_dir.string(), identity.account_name, identity.realm_name,
      identity.character_name, false);
  (void)LoadScopeSyncMetadataLocked(
      identity.wtf_dir.string(), identity.account_name, identity.realm_name,
      identity.character_name, true);

  for (std::uint8_t i = 0; i < 8; ++i) {
    (void)LoadTypeFromDiskLocked(identity.wtf_dir.string(),
                                 identity.account_name,
                                 identity.realm_name,
                                 identity.character_name,
                                 static_cast<AccountDataType>(i));
  }

  if (HasUploadableEntryLocked()) {
    ScheduleUploadLocked();
  }
}

void AccountData::SaveToDisk(const std::string& wtf_dir,
                             const std::string& account_name,
                             const std::string& realm_name,
                             const std::string& char_name) {
  std::lock_guard lock(mutex_);
  if (!CapturePersistenceIdentityLocked(wtf_dir, account_name, realm_name,
                                        char_name)) {
    Log(LogLevel::kWarn,
        "AccountData: rejected invalid or changing persistence identity");
    return;
  }
  (void)SaveToDiskLocked(*persistence_identity_);
}

bool AccountData::SaveToDiskLocked(const PersistenceIdentity& identity) {
  namespace fs = std::filesystem;
  bool write_account_sync_metadata = false;
  bool write_character_sync_metadata = false;
  bool success = true;

  for (std::uint8_t i = 0; i < 8; ++i) {
    if (!entries_[i].disk_dirty) continue;

    auto type = static_cast<AccountDataType>(i);
    const fs::path path =
        BuildPath(identity.wtf_dir.string(), identity.account_name,
                  identity.realm_name, identity.character_name, type);
    if (IsPerCharacterData(type)) {
      write_character_sync_metadata = true;
    } else {
      write_account_sync_metadata = true;
    }

    fs::path dir = path.parent_path();
    std::error_code ec;
    fs::create_directories(dir, ec);

    if (ec || !WriteAccountDataFile(path, entries_[i].data)) {
      Log(LogLevel::kWarn, "AccountData: local slot commit failed for " +
                               std::string(AccountDataTypeName(type)));
      success = false;
      continue;
    }

    entries_[i].disk_dirty = false;

    Log(LogLevel::kInfo, "AccountData: committed " +
                             std::string(AccountDataTypeName(type)));
  }

  if (write_account_sync_metadata
      && !WriteScopeSyncMetadataLocked(
          identity.wtf_dir.string(), identity.account_name,
          identity.realm_name, identity.character_name, false)) {
    Log(LogLevel::kWarn, "AccountData: cannot write account cache.md5");
    success = false;
    for (std::uint8_t i = 0; i < 8; ++i) {
      if (!IsPerCharacterData(static_cast<AccountDataType>(i))) {
        entries_[i].disk_dirty = true;
      }
    }
  }

  if (write_character_sync_metadata
      && !WriteScopeSyncMetadataLocked(
          identity.wtf_dir.string(), identity.account_name,
          identity.realm_name, identity.character_name, true)) {
    Log(LogLevel::kWarn, "AccountData: cannot write character cache.md5");
    success = false;
    for (std::uint8_t i = 0; i < 8; ++i) {
      if (IsPerCharacterData(static_cast<AccountDataType>(i))) {
        entries_[i].disk_dirty = true;
      }
    }
  }

  return success;
}

bool AccountData::FlushBoundPersistence() {
  std::lock_guard lock(mutex_);
  return persistence_identity_.has_value() &&
         SaveToDiskLocked(*persistence_identity_);
}

bool AccountData::HasBoundPersistenceIdentity() const {
  std::lock_guard lock(mutex_);
  return persistence_identity_.has_value();
}

void AccountData::ReleaseBoundPersistenceIdentity() {
  std::lock_guard lock(mutex_);
  persistence_identity_.reset();
  upload_scheduled_ = false;
}

void AccountData::ScheduleUploadLocked(const UploadClock::time_point now) {
  upload_scheduled_ = true;
  upload_deadline_ = now + kUploadCoalesceDelay;
}

bool AccountData::HasUploadableEntryLocked() const {
  const bool has_server_work =
      std::any_of(entries_.begin(), entries_.end(), [](const DataEntry& entry) {
        return entry.dirty || entry.synchronized_digest_mismatch ||
               entry.timestamp > entry.synchronized_timestamp;
      });
  const bool has_bound_disk_work =
      persistence_identity_.has_value() &&
      std::any_of(entries_.begin(), entries_.end(), [](const DataEntry& entry) {
        return entry.disk_dirty;
      });
  return has_server_work || has_bound_disk_work;
}

void AccountData::DeactivateSlotsByScope(const bool per_character_scope) {
  std::lock_guard lock(mutex_);
  for (std::uint8_t i = 0; i < 8; ++i) {
    if (IsScopeType(static_cast<AccountDataType>(i), per_character_scope)) {
      entries_[i].server_download_pending = false;
    }
  }
}

void AccountData::Clear() {
  std::lock_guard lock(mutex_);
  for (auto& entry : entries_) {
    entry.data.clear();
    entry.timestamp = 0;
    entry.synchronized_timestamp = 0;
    entry.dirty = false;
    entry.disk_dirty = false;
    entry.server_download_pending = false;
    entry.synchronized_digest.fill(0u);
    entry.synchronized_digest_mismatch = false;
    entry.mutation_generation = 0;
  }
  next_upload_sequence_ = 0;
  clear_global_config_cache_on_load_ = false;
  clear_character_config_cache_on_load_ = false;
  persistence_identity_.reset();
  upload_scheduled_ = false;
  upload_deadline_ = {};
  ResetChatCacheRuntimeState();
}

}

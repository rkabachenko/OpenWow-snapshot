
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class AccountDataType : std::uint8_t {
  GlobalConfig = 0,
  PerCharacterConfig = 1,
  GlobalBindings = 2,
  PerCharacterBindings = 3,
  GlobalMacros = 4,
  PerCharacterMacros = 5,
  PerCharacterLayout = 6,
  PerCharacterChat = 7,
  NumTypes = 8,
};

inline bool IsPerCharacterData(AccountDataType type) {
  auto t = static_cast<std::uint8_t>(type);
  return t == 1 || t == 3 || t == 5 || t == 6 || t == 7;
}

const char* AccountDataTypeName(AccountDataType type);

class AccountData {
 public:
  using UploadClock = std::chrono::steady_clock;
  static constexpr auto kUploadCoalesceDelay = std::chrono::seconds(30);

  static AccountData& Get();

  struct ServerDownloadResolution {
    bool had_pending_request = false;
    bool should_apply_payload = false;
  };

  struct UploadPayload {
    std::string data;
    std::uint64_t generation = 0;
  };

  void SetAccountDataTimes(const std::array<std::uint32_t, 8>& timestamps);

  void SetNextUploadSequence(std::uint32_t next_sequence);

  void SetAccountData(AccountDataType type, std::uint32_t timestamp,
                      const std::string& data);

  [[nodiscard]] std::string GetData(AccountDataType type) const;
  [[nodiscard]] std::uint32_t GetTimestamp(AccountDataType type) const;
  [[nodiscard]] std::uint32_t GetSynchronizedTimestamp(AccountDataType type) const;

  void MarkDirty(AccountDataType type);
  [[nodiscard]] bool IsDirty(AccountDataType type) const;
  void ClearDirty(AccountDataType type);
  void SyncLocalData(AccountDataType type, const std::string& data);

  [[nodiscard]] std::uint32_t GetDirtyMask() const;
  [[nodiscard]] bool ShouldUpload(AccountDataType type) const;
  [[nodiscard]] std::optional<UploadPayload> SnapshotForUpload(
      AccountDataType type) const;
  [[nodiscard]] std::uint32_t AllocateUploadSequence();
  void MarkUploaded(AccountDataType type, std::uint32_t sequence,
                    const UploadPayload& payload);
  [[nodiscard]] bool IsUploadDue(
      UploadClock::time_point now = UploadClock::now()) const;

  void FinishUploadAttempt(bool had_failure,
                           UploadClock::time_point now = UploadClock::now());
  [[nodiscard]] bool ShouldDownload(AccountDataType type) const;
  [[nodiscard]] bool MarkServerDownloadPending(AccountDataType type);
  void ClearServerDownloadPending(AccountDataType type);
  [[nodiscard]] ServerDownloadResolution ResolveServerDownload(
      AccountDataType type, std::uint32_t server_timestamp);

  void LoadFromDisk(const std::string& wtf_dir, const std::string& account_name,
                    const std::string& realm_name, const std::string& char_name);
  [[nodiscard]] bool LoadTypeFromDisk(const std::string& wtf_dir,
                                      const std::string& account_name,
                                      const std::string& realm_name,
                                      const std::string& char_name,
                                      AccountDataType type);

  void SaveToDisk(const std::string& wtf_dir, const std::string& account_name,
                  const std::string& realm_name, const std::string& char_name);

  [[nodiscard]] bool FlushBoundPersistence();
  [[nodiscard]] bool HasBoundPersistenceIdentity() const;
  void ReleaseBoundPersistenceIdentity();

  static std::vector<std::uint8_t> Compress(const std::string& data);
  static std::string Decompress(const std::vector<std::uint8_t>& compressed,
                                std::uint32_t uncompressed_size);

  void SaveBindings(const std::string& content, bool is_global);
  void SaveMacros(const std::string& content, bool is_global);
  void SaveChat(const std::string& content);
  void SaveLayout(const std::string& content);
  void ClearType(AccountDataType type);
  void SetConfigCacheClearEnabled(bool enabled);
  [[nodiscard]] bool IsConfigCacheClearEnabled(AccountDataType type) const;

  [[nodiscard]] std::string GetBindings(bool is_global) const;
  [[nodiscard]] std::string GetMacros(bool is_global) const;
  [[nodiscard]] std::string GetChat() const;
  [[nodiscard]] std::string GetLayout() const;

  void DeactivateSlotsByScope(bool per_character_scope);

  void Clear();

 private:
  AccountData() = default;

  struct DataEntry {
    std::string data;
    std::uint32_t timestamp = 0;
    std::uint32_t synchronized_timestamp = 0;
    bool dirty = false;
    bool disk_dirty = false;
    bool server_download_pending = false;
    std::array<std::uint8_t, 16> synchronized_digest{};
    bool synchronized_digest_mismatch = false;
    std::uint64_t mutation_generation = 0;
  };

  struct PersistenceIdentity {
    std::filesystem::path wtf_dir;
    std::string account_name;
    std::string realm_name;
    std::string character_name;
  };

  mutable std::mutex mutex_;
  std::array<DataEntry, 8> entries_{};
  std::uint32_t next_upload_sequence_ = 0;
  bool clear_global_config_cache_on_load_ = false;
  bool clear_character_config_cache_on_load_ = false;
  std::optional<PersistenceIdentity> persistence_identity_;
  bool upload_scheduled_ = false;
  UploadClock::time_point upload_deadline_{};

  static std::string BuildPath(const std::string& wtf_dir,
                               const std::string& account_name,
                               const std::string& realm_name,
                               const std::string& char_name,
                               AccountDataType type);
  static std::string FileNameForType(AccountDataType type);
  static bool IsScopeType(AccountDataType type, bool per_character_scope);
  static bool IsConfigCacheType(AccountDataType type);
  static std::uint32_t ReadLe32(const std::uint8_t* bytes);
  static void WriteLe32(std::uint8_t* bytes, std::uint32_t value);
  static std::array<std::uint8_t, 16> ComputeDataDigest(const std::string& data);
  static std::string BuildSyncMetadataPath(const std::string& wtf_dir,
                                           const std::string& account_name,
                                           const std::string& realm_name,
                                           const std::string& char_name,
                                           bool per_character_scope);
  bool ClearConfigCacheFileLocked(const std::string& wtf_dir,
                                  const std::string& account_name,
                                  const std::string& realm_name,
                                  const std::string& char_name,
                                  AccountDataType type);
  [[nodiscard]] bool ShouldClearConfigCacheOnLoadLocked(AccountDataType type) const;
  void ResetLoadedStateLocked();
  void UpdateSynchronizedDigestMismatchLocked(AccountDataType type,
                                              bool file_was_loaded);
  bool LoadScopeSyncMetadataLocked(const std::string& wtf_dir,
                                   const std::string& account_name,
                                   const std::string& realm_name,
                                   const std::string& char_name,
                                   bool per_character_scope);
  bool WriteScopeSyncMetadataLocked(const std::string& wtf_dir,
                                    const std::string& account_name,
                                    const std::string& realm_name,
                                    const std::string& char_name,
                                    bool per_character_scope);
  bool LoadTypeFromDiskLocked(const std::string& wtf_dir,
                              const std::string& account_name,
                              const std::string& realm_name,
                              const std::string& char_name,
                              AccountDataType type);
  [[nodiscard]] bool CapturePersistenceIdentityLocked(
      const std::string& wtf_dir, const std::string& account_name,
      const std::string& realm_name, const std::string& char_name);
  [[nodiscard]] bool SaveToDiskLocked(const PersistenceIdentity& identity);
  void ScheduleUploadLocked(UploadClock::time_point now = UploadClock::now());
  [[nodiscard]] bool HasUploadableEntryLocked() const;
};

}

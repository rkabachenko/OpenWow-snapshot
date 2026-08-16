#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::net::wotlk {

inline constexpr std::array<std::uint8_t, 16> kEmptyStringMd5Digest = {
    0xD4, 0x1D, 0x8C, 0xD9, 0x8F, 0x00, 0xB2, 0x04,
    0xE9, 0x80, 0x09, 0x98, 0xEC, 0xF8, 0x42, 0x7E,
};

struct RealmAddonClientInfo {
  std::string name;
  std::string local_version;
  std::filesystem::path addon_directory;
  bool enabled{true};

  bool is_secure{false};
  std::uint8_t public_key_state{0};
  bool has_public_key{false};
  std::array<std::uint8_t, 256> public_key{};
  std::uint32_t public_key_revision{0};
  std::string update_url;

  std::uint8_t server_state{0};
  std::uint8_t server_public_key_state{0};
  bool server_has_public_key{false};
  bool server_public_key_embedded{false};
  std::array<std::uint8_t, 256> server_public_key{};
  std::uint32_t server_public_key_revision{0};
  bool server_public_key_newer{false};
  bool server_has_url{false};
  std::string server_update_url;

  std::uint32_t security_level{1};
  bool is_corrupt{false};
  bool has_secure_content_digest{false};
  std::array<std::uint8_t, 16> secure_content_digest{};
};

struct RealmAddonCatalogEntry {
  std::uint32_t id{0};
  std::array<std::uint8_t, 16> name_digest{kEmptyStringMd5Digest};
  std::array<std::uint8_t, 16> version_digest{kEmptyStringMd5Digest};
  std::uint32_t revision{0};
  std::uint32_t flags{0};
};

using AddonContentFileLoader =
    std::function<bool(const std::string& client_path,
                       std::vector<std::uint8_t>& bytes)>;

struct RealmAddonHandshakePorts {
  std::function<std::vector<RealmAddonClientInfo>()> discover_client_addons;
  std::function<void(const RealmAddonClientInfo&)> synchronize_server_info;
  std::function<std::filesystem::path()> resolve_catalog_cache_file_path;

  AddonContentFileLoader load_client_file;
};

[[nodiscard]] std::uint32_t ComputeRealmAddonInfoCrc32(
    const std::uint8_t* data,
    std::size_t size);

[[nodiscard]] std::array<std::uint8_t, 16> ComputeAddonContentDigest(
    const std::string& addon_name,
    const AddonContentFileLoader& load);

class RealmAddonHandshakeState {
 public:
  static RealmAddonHandshakeState& Instance();

  void SetBuiltinCatalogEntries(std::vector<RealmAddonCatalogEntry> entries);
  void BindBuiltinCatalogFromDbc(const openwow::data::dbc::DbcLoader* dbc);
  void SetClientAddons(std::vector<RealmAddonClientInfo> addons);
  void SetRuntimePorts(RealmAddonHandshakePorts ports);
  void ClearRuntimePorts();

  void SetCatalogEntries(std::vector<RealmAddonCatalogEntry> entries);

  [[nodiscard]] std::vector<RealmAddonClientInfo> SnapshotClientAddons() const;

  [[nodiscard]] std::vector<RealmAddonCatalogEntry> SnapshotCatalogEntries() const;
  [[nodiscard]] std::uint32_t catalog_revision_max() const;
  [[nodiscard]] std::uint32_t GetAddonSecurity(const std::string& addon_name) const;

  [[nodiscard]] std::vector<std::uint8_t> BuildSerializedClientInfo() const;

  [[nodiscard]] bool ProcessServerInfo(const std::uint8_t* data,
                                       std::size_t size,
                                       std::size_t* consumed = nullptr);

  void ResetForTests();
  void SetCatalogCacheFilePathForTests(std::filesystem::path path);

 private:
  struct RuntimeCatalogEntry {
    RealmAddonCatalogEntry entry;
    bool built_in{false};
  };

  struct CatalogLookupResult {
    RuntimeCatalogEntry* entry{nullptr};
    bool created{false};
  };

  static void RebuildCatalogIndex(
      std::vector<RuntimeCatalogEntry>& entries,
      std::unordered_map<std::uint32_t, std::size_t>& index);
  [[nodiscard]] static CatalogLookupResult FindOrCreateCatalogEntryById(
      std::vector<RuntimeCatalogEntry>& entries,
      std::unordered_map<std::uint32_t, std::size_t>& index,
      std::uint32_t id,
      bool built_in_on_create);

  void LoadClientAddonsIfEmpty();
  void LoadCatalogCacheIfNeeded();
  void RebuildCatalogFromSourcesLocked(
      const std::vector<RealmAddonCatalogEntry>& cache_file_entries);
  [[nodiscard]] std::uint32_t catalog_revision_max_locked() const;
  [[nodiscard]] std::vector<RealmAddonCatalogEntry>
  SnapshotCatalogEntriesLocked() const;

  mutable std::mutex mutex_;
  std::vector<RealmAddonClientInfo> client_addons_;
  std::vector<RealmAddonCatalogEntry> builtin_catalog_source_entries_;
  std::vector<RealmAddonCatalogEntry> injected_cache_file_entries_;
  bool has_injected_cache_file_entries_{false};
  std::vector<RuntimeCatalogEntry> catalog_entries_;
  std::unordered_map<std::uint32_t, std::size_t> catalog_entry_index_;
  std::filesystem::path catalog_cache_file_path_override_;
  bool catalog_cache_loaded_{false};
  RealmAddonHandshakePorts runtime_ports_;
};

}

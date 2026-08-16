#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#include "openwow/ui/retail_client_build.h"
#include "openwow/vfs/virtual_file_system.h"

namespace openwow::ui {

struct AddonInfo {
  std::string name;
  std::string title;
  std::string notes;
  std::string author;
  std::string version;
  uint32_t revision = 0;
  uint32_t interface_version = 0;
  std::string directory;
  std::string toc_path;

  bool enabled = true;
  bool load_on_demand = false;
  bool is_blizzard = false;
  bool is_secure = false;
  bool out_of_date = false;

  std::vector<std::string> dependencies;
  std::vector<std::string> optional_deps;
  std::vector<std::string> load_with;
  std::vector<std::string> load_managers;
  std::vector<std::string> saved_variables;
  std::vector<std::string> saved_variables_per_char;

  std::vector<std::pair<std::string, std::string>> metadata_fields;

  int registration_order = -1;
  bool dep_missing = false;
  std::string missing_dep_name;

  std::uint8_t public_key_state = 0;
  bool has_public_key = false;
  std::array<std::uint8_t, 256> public_key{};
  std::string update_url;

  size_t memory_usage = 0;

  size_t toc_file_entry_count = 0;
};

class AddonManager {
 public:

  static AddonManager& Get();

  void ScanAddons(const std::string& interfacePath = "Interface");
  void ScanAddons(const openwow::vfs::VirtualFileSystem& vfs,
                  const std::string& interfacePath = "/Interface");

  [[nodiscard]] static std::vector<AddonInfo> DiscoverAddons(
      const openwow::vfs::VirtualFileSystem& vfs,
      const std::string& interfacePath = "/Interface");

  void PublishDiscoveredAddons(std::vector<AddonInfo> addons);
  [[nodiscard]] bool HasCompletedDiscovery() const;

  [[nodiscard]] size_t GetNumAddons() const;
  [[nodiscard]] const AddonInfo* GetAddonInfo(size_t index) const;
  [[nodiscard]] const AddonInfo* GetAddonInfo(const std::string& name) const;
  [[nodiscard]] int GetAddonIndex(const std::string& name) const;
  [[nodiscard]] std::vector<AddonInfo> SnapshotAddons() const;
  [[nodiscard]] std::vector<AddonInfo> SnapshotAddonsByRegistrationOrder() const;

  void EnableAddon(const std::string& name);
  void DisableAddon(const std::string& name);
  void EnableAllAddons();
  void DisableAllAddons();
  [[nodiscard]] bool IsAddonEnabled(const std::string& name) const;

  [[nodiscard]] bool IsAddonLoadOnDemand(const std::string& name) const;

  [[nodiscard]] size_t GetAddonMemoryUsage(const std::string& name) const;
  [[nodiscard]] size_t GetTotalMemoryUsage() const;

  static constexpr uint32_t kClientInterfaceVersion = kRetailInterfaceVersion;

  void Reset();

  void AddAddon(AddonInfo info);

 private:
  AddonManager() = default;

  void ResolveLoadOrder();

  AddonInfo* FindMutable(const std::string& name);

  std::vector<AddonInfo> addons_;
  std::unordered_map<std::string, size_t> addon_index_;
  bool discovery_complete_{false};
  mutable std::mutex mutex_;
};

}

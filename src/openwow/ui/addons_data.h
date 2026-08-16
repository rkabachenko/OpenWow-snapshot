#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "openwow/core/cmap_hashtable.h"

namespace openwow::ui {

struct AddonInfo;

struct AddOnState {
  std::string name;
  uint8_t loaded = 0;
  uint8_t load_finished = 0;
  uint32_t interface_version = 0;
  uint32_t crc = 0;
  uint32_t security = 1;
  uint8_t secure = 0;
  uint8_t is_blizzard = 0;
  uint8_t is_corrupt = 0;
  uint8_t default_enabled = 1;
  uint8_t load_on_demand = 0;
  uint8_t server_state = 0;
  std::string url_string;
  uint32_t toc_content_hash = 0;
  std::vector<std::string> optional_deps;
  std::vector<std::string> dependencies;
  std::vector<std::string> load_with;
  std::vector<std::string> load_managers;
  std::vector<std::string> saved_variables;
  std::vector<std::string> saved_variables_per_character;
  std::unordered_map<std::string, std::string> metadata;
  std::string toc_path;
  uint8_t has_new_version = 0;
  uint8_t has_server_hash = 0;
  char server_hash[256] = {};

  bool has_secure_content_digest = false;
  std::array<std::uint8_t, 16> secure_content_digest{};

  void Init() {
    name.clear();
    loaded = 0;
    load_finished = 0;
    interface_version = 0;
    crc = 0;
    security = 1;
    secure = 0;
    is_blizzard = 0;
    is_corrupt = 0;
    default_enabled = 1;
    load_on_demand = 0;
    server_state = 0;
    url_string.clear();
    toc_content_hash = 0;
    optional_deps.clear();
    dependencies.clear();
    load_with.clear();
    load_managers.clear();
    saved_variables.clear();
    saved_variables_per_character.clear();
    metadata.clear();
    toc_path.clear();
    has_new_version = 0;
    has_server_hash = 0;
    std::memset(server_hash, 0, sizeof(server_hash));
    has_secure_content_digest = false;
    secure_content_digest.fill(0);
  }
};

enum class AddonStatusLabel : uint32_t {
  Loadable = 0,
  Missing = 1,
  Disabled = 2,
  Banned = 3,
  Corrupt = 4,
  Insecure = 5,
  DemandLoaded = 6,
  InterfaceVersion = 7,
  Incompatible = 8,
  Secure = 9,
};

struct AddonLoadabilityResult {
  bool loadable = false;
  AddonStatusLabel reason = AddonStatusLabel::Loadable;
  AddonStatusLabel dependency_reason = AddonStatusLabel::Loadable;
};

enum class AddonLoadState : uint32_t {
  Disabled = 0,
  Enabled = 1,
  Banned = 2,
};

class AddOnsData {
public:
  static AddOnsData &Get();

  const char *GetMetadata(const char *addon_name, const char *field_name) const;

  int GetLoadState(const char *addon_name) const;

  int GetCharacterLoadState(const char *addon_name, const char *character_name,
                            bool use_load_state_fallback) const;

  uint32_t GetSecurity(const char *addon_name) const;
  [[nodiscard]] const char *GetSecurityLabel(const char *addon_name) const;
  static const char *GetSecurityLabel(uint32_t security_level);

  [[nodiscard]] bool IsAddonLoaded(const char *addon_name) const;
  [[nodiscard]] bool IsAddonLoadFinished(const char *addon_name) const;
  [[nodiscard]] bool IsAddonLoadOnDemand(const char *addon_name) const;
  [[nodiscard]] const char *GetAddonUrl(const char *addon_name) const;

  [[nodiscard]] bool HasNewVersion(const char *addon_name) const;

  void SetAddonLoadedState(const char *addon_name, bool loaded, bool finished);
  void SetAddonLoadOnDemand(const char *addon_name, bool load_on_demand);

  [[nodiscard]] AddonLoadabilityResult EvaluateLoadability(
      const char *addon_name, bool allow_load_on_demand, const char *character_name) const;
  static const char *FormatLoadReason(AddonStatusLabel reason,
                                      AddonStatusLabel dependency_reason,
                                      std::string &storage);

  void RemoveState(const char *character_name);
  void ClearSavedStates();

  void LoadSavedStateForCharacter(const std::string &account_name, const std::string &realm_name,
                                  const std::string &character_name);

  void ReloadSavedState(const std::string &account_name, const std::string &realm_name,
                        const char *character_name);

  void ReloadSavedStates();

  void SetSavedAddonEnabled(const char *addon_name, const char *character_name, bool enabled);

  void SetAllVisibleAddonsSavedEnabled(const char *character_name, bool enabled);

  bool SaveSavedStates();

  void RegisterAddon(const std::string &name, const AddOnState &state);
  void ImportFromAddonManagerSnapshot(const std::vector<AddonInfo> &addons);

  void Clear();
  [[nodiscard]] size_t GetAddonCount() const {
    return sorted_names_.size();
  }
  [[nodiscard]] const std::string *GetAddonNameByIndex(size_t index) const;
  [[nodiscard]] const AddOnState *FindAddon(const char *name) const;
  [[nodiscard]] const AddOnState *FindAddon(const std::string &name) const;
  void SetServerState(const char *addon_name, std::uint8_t server_state);
  void ApplyServerInfo(const char *addon_name, std::uint8_t server_state,
                       std::uint32_t security_level, bool is_corrupt,
                       bool has_new_version,
                       const std::array<std::uint8_t, 16> *secure_content_digest = nullptr);

private:
  AddOnsData() = default;
  void RebuildVisibleAddonNames();

  struct SavedStateList {
    std::string account_name;
    std::string realm_name;
    std::string character_name;
    bool dirty{false};
    bool used_account_level_fallback{false};
    std::unordered_map<std::string, bool> addon_enabled;

    std::vector<std::string> addon_order;
  };

  std::unordered_map<std::string, AddOnState> addons_;

  std::vector<std::string> sorted_names_;

  std::unordered_map<std::string, SavedStateList> saved_state_lists_;

  openwow::core::CMapTable registry_hash_table_;

  SavedStateList *FindSavedStateList(const char *character_name);
  const SavedStateList *FindSavedStateList(const char *character_name) const;
  SavedStateList *FindSavedStateList(const std::string &account_name,
                                     const std::string &realm_name,
                                     const char *character_name);
  const SavedStateList *FindSavedStateList(const std::string &account_name,
                                           const std::string &realm_name,
                                           const char *character_name) const;
};

}

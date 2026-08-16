
#include "openwow/ui/addons_data.h"

#include "openwow/core/storm_string.h"
#include "openwow/ui/addon_manager.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/retail_client_build.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>

namespace openwow::ui {

namespace {

using AddonMap = std::unordered_map<std::string, AddOnState>;

bool AsciiCaseInsensitiveEquals(const std::string_view lhs, const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const auto lhs_ch =
        static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(lhs[i])));
    const auto rhs_ch =
        static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(rhs[i])));
    if (lhs_ch != rhs_ch) {
      return false;
    }
  }
  return true;
}

bool ParseSavedAddonEnabledValue(const std::string_view value) {
  return !AsciiCaseInsensitiveEquals(value, "disabled");
}

std::string ToSavedStateKeyComponent(const std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const unsigned char ch : value) {
    result.push_back(static_cast<char>(std::tolower(ch)));
  }
  return result;
}

std::string MakeSavedStateKey(const std::string_view account_name,
                              const std::string_view realm_name,
                              const std::string_view character_name) {
  std::string key = ToSavedStateKeyComponent(account_name);
  key.push_back('\x1f');
  key += ToSavedStateKeyComponent(realm_name);
  key.push_back('\x1f');
  key += ToSavedStateKeyComponent(character_name);
  return key;
}

AddonMap::const_iterator FindAddonIterator(const AddonMap &addons, const std::string_view name) {
  for (auto it = addons.begin(); it != addons.end(); ++it) {
    if (AsciiCaseInsensitiveEquals(it->first, name)) {
      return it;
    }
  }
  return addons.end();
}

AddonMap::iterator FindAddonIterator(AddonMap &addons, const std::string_view name) {
  for (auto it = addons.begin(); it != addons.end(); ++it) {
    if (AsciiCaseInsensitiveEquals(it->first, name)) {
      return it;
    }
  }
  return addons.end();
}

bool UpsertAddonState(AddonMap &addons, const std::string &name, AddOnState state) {
  auto it = FindAddonIterator(addons, name);
  if (it == addons.end()) {
    addons.emplace(name, std::move(state));
    return true;
  }

  if (it->first == name) {
    it->second = std::move(state);
    return false;
  }

  addons.erase(it);
  addons.emplace(name, std::move(state));
  return false;
}

const std::string *FindMetadataValue(const AddOnState &state, const std::string_view field_name) {
  for (const auto &[key, value] : state.metadata) {
    if (AsciiCaseInsensitiveEquals(key, field_name)) {
      return &value;
    }
  }
  return nullptr;
}

std::string_view ResolveVisibleAddonSortKey(const AddOnState &state) {
  if (const auto *title = FindMetadataValue(state, "Title"); title != nullptr) {
    return *title;
  }
  return state.name;
}

int CompareVisibleAddonSortKeys(const AddOnState &lhs, const AddOnState &rhs) {
  return openwow::core::SStrCmpNoCase(ResolveVisibleAddonSortKey(lhs).data(),
                                      ResolveVisibleAddonSortKey(rhs).data(), 0x7FFFFFFFu);
}

const bool *FindSavedStateValue(const std::unordered_map<std::string, bool> &addon_enabled,
                                const std::string_view addon_name) {
  for (const auto &[saved_name, enabled] : addon_enabled) {
    if (AsciiCaseInsensitiveEquals(saved_name, addon_name)) {
      return &enabled;
    }
  }
  return nullptr;
}

bool *FindSavedStateValue(std::unordered_map<std::string, bool> &addon_enabled,
                          const std::string_view addon_name) {
  for (auto &[saved_name, enabled] : addon_enabled) {
    if (AsciiCaseInsensitiveEquals(saved_name, addon_name)) {
      return &enabled;
    }
  }
  return nullptr;
}

bool ReadRetailSavedStateToken(const std::string_view content, std::size_t &cursor,
                               std::string &token) {
  constexpr std::size_t kMaximumTokenLength = 0x3ff;
  token.clear();

  bool quoted = false;
  while (cursor < content.size() && content[cursor] != '\0') {
    const char ch = content[cursor];
    if (ch == '"') {
      ++cursor;
      quoted = true;
      break;
    }
    if (ch != '\r' && ch != '\n') {
      break;
    }
    ++cursor;
  }

  while (cursor < content.size() && content[cursor] != '\0') {
    const char ch = content[cursor];
    if (ch == '"') {
      quoted = !quoted;
      if (quoted && !token.empty()) {

        return true;
      }

      ++cursor;
      if (!quoted) {
        return cursor < content.size() && content[cursor] != '\0';
      }
      continue;
    }

    if (!quoted && (ch == '\r' || ch == '\n')) {
      ++cursor;
      return cursor < content.size() && content[cursor] != '\0';
    }

    if (token.size() < kMaximumTokenLength) {
      token.push_back(ch);
    }
    ++cursor;
  }

  return false;
}

void UpsertMetadataValue(std::unordered_map<std::string, std::string> &metadata,
                         std::string key,
                         std::string value) {
  for (auto it = metadata.begin(); it != metadata.end(); ++it) {
    if (AsciiCaseInsensitiveEquals(it->first, key)) {
      metadata.erase(it);
      break;
    }
  }
  metadata.emplace(std::move(key), std::move(value));
}

void PopulateSnapshotMetadata(const AddonInfo &addon, AddOnState &state) {
  state.metadata.clear();
  UpsertMetadataValue(state.metadata, "Title", addon.name);

  if (!addon.metadata_fields.empty()) {
    for (const auto &[key, value] : addon.metadata_fields) {
      UpsertMetadataValue(state.metadata, key, value);
    }
    return;
  }

  if (!addon.title.empty()) {
    UpsertMetadataValue(state.metadata, "Title", addon.title);
  }
  if (!addon.notes.empty()) {
    UpsertMetadataValue(state.metadata, "Notes", addon.notes);
  }
  if (!addon.author.empty()) {
    UpsertMetadataValue(state.metadata, "Author", addon.author);
  }
  if (!addon.version.empty()) {
    UpsertMetadataValue(state.metadata, "Version", addon.version);
  }
}

constexpr std::array<const char *, 10> kAddonStatusLabels = {
    "LOADABLE",
    "MISSING",
    "DISABLED",
    "BANNED",
    "CORRUPT",
    "INSECURE",
    "DEMAND_LOADED",
    "INTERFACE_VERSION",
    "INCOMPATIBLE",
    "SECURE",
};

constexpr std::uint32_t kMinCompatibleInterfaceVersion = 20000;
const char *GetAddonStatusLabel(const AddonStatusLabel label) {
  return kAddonStatusLabels.at(static_cast<std::size_t>(label));
}

std::string GetAddonTocPath(const AddOnState &state) {
  if (!state.toc_path.empty()) {
    return state.toc_path;
  }
  return "/Interface/AddOns/" + state.name + "/" + state.name + ".toc";
}

AddonLoadabilityResult EvaluateLoadabilityRecursive(const AddOnsData &data, const char *addon_name,
                                                    const bool allow_load_on_demand,
                                                    const char *character_name,
                                                    std::vector<std::string_view> &active_chain) {
  const auto *addon = data.FindAddon(addon_name);
  if (addon == nullptr) {
    return {
        .loadable = false,
        .reason = AddonStatusLabel::Missing,
        .dependency_reason = AddonStatusLabel::Loadable,
    };
  }

  for (const auto active_name : active_chain) {
    if (AsciiCaseInsensitiveEquals(active_name, addon->name)) {
      return {
          .loadable = true,
          .reason = AddonStatusLabel::Loadable,
          .dependency_reason = AddonStatusLabel::Loadable,
      };
    }
  }

  if (data.GetCharacterLoadState(addon->name.c_str(), character_name, true) == 0) {
    return {
        .loadable = false,
        .reason = AddonStatusLabel::Disabled,
        .dependency_reason = AddonStatusLabel::Loadable,
    };
  }

  if (addon->security == static_cast<std::uint32_t>(AddonLoadState::Banned)) {
    return {
        .loadable = false,
        .reason = AddonStatusLabel::Banned,
        .dependency_reason = AddonStatusLabel::Loadable,
    };
  }

  if (addon->is_corrupt != 0) {
    return {
        .loadable = false,
        .reason = AddonStatusLabel::Corrupt,
        .dependency_reason = AddonStatusLabel::Loadable,
    };
  }

  if (addon->interface_version < kMinCompatibleInterfaceVersion) {
    return {
        .loadable = false,
        .reason = AddonStatusLabel::Incompatible,
        .dependency_reason = AddonStatusLabel::Loadable,
    };
  }

  if (addon->interface_version != openwow::ui::kRetailInterfaceVersion &&
      openwow::ui::game::CVarSystem::Instance().GetCVarBool("checkAddonVersion")) {
    return {
        .loadable = false,
        .reason = AddonStatusLabel::InterfaceVersion,
        .dependency_reason = AddonStatusLabel::Loadable,
    };
  }

  active_chain.push_back(addon->name);
  for (const auto &dependency_name : addon->dependencies) {
    if (data.IsAddonLoaded(dependency_name.c_str())) {
      continue;
    }

    const auto dependency =
        EvaluateLoadabilityRecursive(data, dependency_name.c_str(), true, character_name,
                                     active_chain);
    if (!dependency.loadable) {
      active_chain.pop_back();
      return {
          .loadable = false,
          .reason = AddonStatusLabel::Loadable,
          .dependency_reason =
              dependency.reason != AddonStatusLabel::Loadable ? dependency.reason
                                                              : dependency.dependency_reason,
      };
    }
  }
  active_chain.pop_back();

  if (!allow_load_on_demand && addon->load_on_demand != 0) {
    return {
        .loadable = false,
        .reason = AddonStatusLabel::DemandLoaded,
        .dependency_reason = AddonStatusLabel::Loadable,
    };
  }

  return {
      .loadable = true,
      .reason = AddonStatusLabel::Loadable,
      .dependency_reason = AddonStatusLabel::Loadable,
  };
}

}

AddOnsData &AddOnsData::Get() {
  static AddOnsData instance;
  return instance;
}

const char *AddOnsData::GetMetadata(const char *addon_name, const char *field_name) const {
  if (!addon_name || !field_name)
    return nullptr;

  const auto it = FindAddonIterator(addons_, addon_name);
  if (it == addons_.end())
    return nullptr;

  const auto *value = FindMetadataValue(it->second, field_name);
  return value != nullptr ? value->c_str() : nullptr;
}

int AddOnsData::GetLoadState(const char *addon_name) const {
  if (!addon_name)
    return 0;

  const auto it = FindAddonIterator(addons_, addon_name);
  if (it == addons_.end())
    return 0;

  std::size_t state_count = 0;
  std::size_t enabled_count = 0;
  for (const auto &[_, saved_state] : saved_state_lists_) {
    const bool *enabled = FindSavedStateValue(saved_state.addon_enabled, addon_name);
    if (enabled == nullptr) {
      continue;
    }
    ++state_count;
    if (*enabled) {
      ++enabled_count;
    }
  }

  if (state_count == 0) {
    return it->second.default_enabled ? 2 : 0;
  }
  if (enabled_count == 0) {
    return 0;
  }
  return enabled_count == state_count ? 2 : 1;
}

int AddOnsData::GetCharacterLoadState(const char *addon_name, const char *character_name,
                                      const bool use_load_state_fallback) const {
  if (!addon_name) {
    return 0;
  }

  const auto it = FindAddonIterator(addons_, addon_name);
  if (it == addons_.end()) {
    return 0;
  }

  std::size_t state_count = 0;
  std::size_t enabled_count = 0;
  int aggregate_state = -1;

  const auto fallback_enabled = [&]() {
    if (!use_load_state_fallback) {
      return it->second.default_enabled != 0;
    }

    if (aggregate_state < 0) {
      aggregate_state = GetLoadState(addon_name);
    }

    if (aggregate_state == 1) {
      return it->second.default_enabled != 0;
    }
    return aggregate_state == 2;
  };

  for (const auto &[_, saved_state] : saved_state_lists_) {
    if (character_name != nullptr &&
        !AsciiCaseInsensitiveEquals(saved_state.character_name, character_name)) {
      continue;
    }

    bool enabled = fallback_enabled();
    if (const bool *saved_value = FindSavedStateValue(saved_state.addon_enabled, addon_name);
        saved_value != nullptr) {
      enabled = *saved_value;
    }

    ++state_count;
    if (enabled) {
      ++enabled_count;
    }
  }

  if (state_count == 0) {
    return it->second.default_enabled ? 2 : 0;
  }
  if (enabled_count == 0) {
    return 0;
  }
  return enabled_count == state_count ? 2 : 1;
}

uint32_t AddOnsData::GetSecurity(const char *addon_name) const {
  if (!addon_name)
    return 1;

  const auto it = FindAddonIterator(addons_, addon_name);
  if (it == addons_.end())
    return 1;

  return it->second.security;
}

const char *AddOnsData::GetSecurityLabel(const char *addon_name) const {
  return GetSecurityLabel(GetSecurity(addon_name));
}

const char *AddOnsData::GetSecurityLabel(const uint32_t security_level) {
  switch (security_level) {
  case 0:
    return GetAddonStatusLabel(AddonStatusLabel::Secure);
  case 2:
    return GetAddonStatusLabel(AddonStatusLabel::Banned);
  default:
    return GetAddonStatusLabel(AddonStatusLabel::Insecure);
  }
}

bool AddOnsData::IsAddonLoaded(const char *addon_name) const {
  const auto *addon = FindAddon(addon_name);
  return addon != nullptr && addon->loaded != 0;
}

bool AddOnsData::IsAddonLoadFinished(const char *addon_name) const {
  const auto *addon = FindAddon(addon_name);
  return addon != nullptr && addon->load_finished != 0;
}

bool AddOnsData::IsAddonLoadOnDemand(const char *addon_name) const {
  const auto *addon = FindAddon(addon_name);
  return addon != nullptr && addon->load_on_demand != 0;
}

const char *AddOnsData::GetAddonUrl(const char *addon_name) const {
  const auto *addon = FindAddon(addon_name);
  if (addon == nullptr || addon->url_string.empty() || addon->server_state == 2) {
    return nullptr;
  }
  return addon->url_string.c_str();
}

bool AddOnsData::HasNewVersion(const char *addon_name) const {
  const auto *addon = FindAddon(addon_name);
  return addon != nullptr && addon->has_new_version != 0;
}

void AddOnsData::SetAddonLoadedState(const char *addon_name, const bool loaded, const bool finished) {
  if (addon_name == nullptr) {
    return;
  }

  const auto it = FindAddonIterator(addons_, addon_name);
  if (it == addons_.end()) {
    return;
  }

  auto &addon = addons_[it->first];
  addon.loaded = loaded ? 1 : 0;
  addon.load_finished = finished ? 1 : 0;
}

void AddOnsData::SetAddonLoadOnDemand(const char *addon_name, const bool load_on_demand) {
  if (addon_name == nullptr) {
    return;
  }

  const auto it = FindAddonIterator(addons_, addon_name);
  if (it == addons_.end()) {
    return;
  }

  addons_[it->first].load_on_demand = load_on_demand ? 1u : 0u;
}

AddonLoadabilityResult AddOnsData::EvaluateLoadability(const char *addon_name,
                                                       const bool allow_load_on_demand,
                                                       const char *character_name) const {
  std::vector<std::string_view> active_chain;
  return EvaluateLoadabilityRecursive(*this, addon_name, allow_load_on_demand, character_name,
                                      active_chain);
}

const char *AddOnsData::FormatLoadReason(const AddonStatusLabel reason,
                                         const AddonStatusLabel dependency_reason,
                                         std::string &storage) {
  if (reason != AddonStatusLabel::Loadable) {
    return GetAddonStatusLabel(reason);
  }

  storage = "DEP_";
  storage += GetAddonStatusLabel(dependency_reason);
  return storage.c_str();
}

void AddOnsData::RebuildVisibleAddonNames() {
  sorted_names_.clear();
  sorted_names_.reserve(addons_.size());
  for (const auto &[name, state] : addons_) {
    if (state.server_state == 2) {
      continue;
    }
    sorted_names_.push_back(name);
  }
  std::sort(sorted_names_.begin(), sorted_names_.end(),
            [&](const std::string &lhs_name, const std::string &rhs_name) {
              const auto lhs = addons_.find(lhs_name);
              const auto rhs = addons_.find(rhs_name);
              if (lhs == addons_.end() || rhs == addons_.end()) {
                return openwow::core::SStrCmpNoCase(lhs_name.c_str(), rhs_name.c_str(),
                                                    0x7FFFFFFFu) < 0;
              }
              return CompareVisibleAddonSortKeys(lhs->second, rhs->second) < 0;
            });
}

void AddOnsData::RemoveState(const char *character_name) {
  if (!character_name) {
    ClearSavedStates();
    return;
  }

  for (auto it = saved_state_lists_.begin(); it != saved_state_lists_.end();) {
    if (AsciiCaseInsensitiveEquals(it->second.character_name, character_name)) {
      it = saved_state_lists_.erase(it);
    } else {
      ++it;
    }
  }
}

void AddOnsData::ClearSavedStates() {
  saved_state_lists_.clear();
}

void AddOnsData::SetSavedAddonEnabled(const char *addon_name, const char *character_name,
                                      const bool enabled) {
  if (addon_name == nullptr) {
    return;
  }

  const auto update_state = [&](SavedStateList &saved_state) {
    if (bool *saved_value = FindSavedStateValue(saved_state.addon_enabled, addon_name);
        saved_value != nullptr) {
      if (*saved_value != enabled) {
        *saved_value = enabled;
        saved_state.dirty = true;
      }
      return;
    }

    saved_state.addon_enabled.emplace(addon_name, enabled);
    saved_state.addon_order.emplace_back(addon_name);
    saved_state.dirty = true;
  };

  if (character_name != nullptr) {
    for (auto &[_, saved_state] : saved_state_lists_) {
      if (AsciiCaseInsensitiveEquals(saved_state.character_name, character_name)) {
        update_state(saved_state);
      }
    }
    return;
  }

  for (auto &[_, saved_state] : saved_state_lists_) {
    update_state(saved_state);
  }
}

void AddOnsData::SetAllVisibleAddonsSavedEnabled(const char *character_name,
                                                 const bool enabled) {
  for (const auto &addon_name : sorted_names_) {
    SetSavedAddonEnabled(addon_name.c_str(), character_name, enabled);
  }
}

void AddOnsData::RegisterAddon(const std::string &name, const AddOnState &state) {
  auto stored_state = state;
  const auto it = FindAddonIterator(addons_, name);
  if (it != addons_.end()) {
    stored_state.server_state = it->second.server_state;
  }
  stored_state.name = name;
  if (stored_state.toc_path.empty()) {
    stored_state.toc_path = GetAddonTocPath(stored_state);
  }

  if (it == addons_.end()) {
    (void)registry_hash_table_.InsertHashedKey(
        openwow::core::SStrHashCI(name.c_str()));
  }

  UpsertAddonState(addons_, name, std::move(stored_state));
  RebuildVisibleAddonNames();
}

void AddOnsData::ImportFromAddonManagerSnapshot(const std::vector<AddonInfo> &addons) {
  AddonMap rebuilt;
  rebuilt.reserve(addons.size());
  registry_hash_table_.Reset();

  for (const auto &addon : addons) {
    AddOnState state{};
    state.Init();
    const AddOnState *existing = FindAddon(addon.name);
    if (existing != nullptr) {
      state = *existing;
    }

    state.name = addon.name;
    state.interface_version = addon.interface_version;

    state.crc = addon.revision;
    state.secure = addon.is_secure ? 1u : 0u;
    if (existing == nullptr) {

      state.security = 1u;
      state.is_blizzard = 0u;
      state.is_corrupt = 0;
    }
    state.url_string = addon.update_url;
    state.default_enabled = addon.enabled ? 1u : 0u;
    state.load_on_demand = addon.load_on_demand ? 1u : 0u;
    state.optional_deps = addon.optional_deps;
    state.dependencies = addon.dependencies;
    state.load_with = addon.load_with;
    state.load_managers = addon.load_managers;
    state.saved_variables = addon.saved_variables;
    state.saved_variables_per_character = addon.saved_variables_per_char;
    state.toc_path = addon.toc_path.empty() ? GetAddonTocPath(state) : addon.toc_path;

    state.toc_content_hash = static_cast<std::uint32_t>(addon.toc_file_entry_count);

    PopulateSnapshotMetadata(addon, state);

    const bool inserted_new_addon = UpsertAddonState(rebuilt, addon.name, std::move(state));
    if (inserted_new_addon) {
      (void)registry_hash_table_.InsertHashedKey(
          openwow::core::SStrHashCI(addon.name.c_str()));
    }
  }

  addons_ = std::move(rebuilt);
  RebuildVisibleAddonNames();
}

void AddOnsData::Clear() {
  addons_.clear();
  sorted_names_.clear();
  ClearSavedStates();
  registry_hash_table_.Reset();
}

const std::string *AddOnsData::GetAddonNameByIndex(const size_t index) const {
  if (index >= sorted_names_.size()) {
    return nullptr;
  }
  return &sorted_names_[index];
}

const AddOnState *AddOnsData::FindAddon(const char *name) const {
  if (!name) {
    return nullptr;
  }
  const auto it = FindAddonIterator(addons_, name);
  return it != addons_.end() ? &it->second : nullptr;
}

const AddOnState *AddOnsData::FindAddon(const std::string &name) const {
  return FindAddon(name.c_str());
}

void AddOnsData::SetServerState(const char *addon_name, const std::uint8_t server_state) {
  if (addon_name == nullptr) {
    return;
  }

  const auto it = FindAddonIterator(addons_, addon_name);
  if (it == addons_.end()) {
    return;
  }

  AddOnState &addon = addons_[it->first];
  if (addon.server_state == server_state) {
    return;
  }

  addon.server_state = server_state;
  addon.is_blizzard = server_state == 2 ? 1u : 0u;
  RebuildVisibleAddonNames();
}

void AddOnsData::ApplyServerInfo(const char *addon_name,
                                 const std::uint8_t server_state,
                                 const std::uint32_t security_level,
                                 const bool is_corrupt,
                                 const bool has_new_version,
                                 const std::array<std::uint8_t, 16> *secure_content_digest) {
  if (addon_name == nullptr) {
    return;
  }

  const auto it = FindAddonIterator(addons_, addon_name);
  if (it == addons_.end()) {
    return;
  }

  AddOnState &addon = addons_[it->first];
  const bool visibility_changed = addon.server_state != server_state;
  const std::uint8_t corrupt_flag = is_corrupt ? 1u : 0u;
  const std::uint8_t new_version_flag = has_new_version ? 1u : 0u;
  const bool has_digest = secure_content_digest != nullptr;
  const bool digest_changed =
      addon.has_secure_content_digest != has_digest ||
      (has_digest && addon.secure_content_digest != *secure_content_digest);
  if (!visibility_changed && addon.security == security_level &&
      addon.is_corrupt == corrupt_flag &&
      addon.has_new_version == new_version_flag && !digest_changed) {
    return;
  }

  addon.server_state = server_state;
  addon.is_blizzard = server_state == 2 ? 1u : 0u;
  addon.security = security_level;
  addon.is_corrupt = corrupt_flag;
  addon.has_new_version = new_version_flag;
  addon.has_secure_content_digest = has_digest;
  if (has_digest) {
    addon.secure_content_digest = *secure_content_digest;
  } else {
    addon.secure_content_digest.fill(0);
  }
  if (visibility_changed) {
    RebuildVisibleAddonNames();
  }
}

void AddOnsData::LoadSavedStateForCharacter(const std::string &account_name,
                                            const std::string &realm_name,
                                            const std::string &character_name) {
  if (account_name.empty() || realm_name.empty() || character_name.empty()) {
    return;
  }

  SavedStateList loaded_state{};
  loaded_state.account_name = account_name;
  loaded_state.realm_name = realm_name;
  loaded_state.character_name = character_name;
  auto parse_file = [&](const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
      return false;
    }

    const std::string content((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    std::size_t cursor = 0;
    std::string record;
    while (ReadRetailSavedStateToken(content, cursor, record)) {
      const std::size_t separator = record.find(':');
      if (separator == std::string::npos) {
        if (record.empty()) {
          break;
        }
        continue;
      }

      const std::string_view addon_name_view(record.data(), separator);
      std::string_view value_view =
          std::string_view(record.data() + separator + 1,
                           record.size() - separator - 1);
      while (!value_view.empty() && value_view.front() == ' ') {
        value_view.remove_prefix(1);
      }
      if (addon_name_view.empty() || value_view.empty()) {
        if (record.empty()) {
          break;
        }
        continue;
      }

      const std::string addon_name(addon_name_view);
      if (bool *saved_value = FindSavedStateValue(loaded_state.addon_enabled, addon_name);
          saved_value != nullptr) {
        *saved_value = ParseSavedAddonEnabledValue(value_view);
      } else {
        loaded_state.addon_enabled.emplace(addon_name,
                                           ParseSavedAddonEnabledValue(value_view));
        loaded_state.addon_order.push_back(addon_name);
      }

      if (record.empty()) {
        break;
      }
    }
    return true;
  };

  const auto account_root = std::filesystem::path("WTF") / "Account" / account_name;
  const auto character_file = account_root / realm_name / character_name / "AddOns.txt";
  if (!parse_file(character_file)) {
    loaded_state.used_account_level_fallback = parse_file(account_root / "AddOns.txt");
    loaded_state.dirty = loaded_state.used_account_level_fallback;
  }

  saved_state_lists_.insert_or_assign(MakeSavedStateKey(loaded_state.account_name,
                                                        loaded_state.realm_name,
                                                        loaded_state.character_name),
                                      std::move(loaded_state));
}

void AddOnsData::ReloadSavedState(const std::string &account_name,
                                  const std::string &realm_name,
                                  const char *character_name) {
  if (character_name == nullptr || character_name[0] == '\0') {
    ReloadSavedStates();
    return;
  }

  SavedStateList *saved_state = FindSavedStateList(account_name, realm_name, character_name);
  if (saved_state == nullptr) {
    saved_state = FindSavedStateList(character_name);
  }
  if (saved_state != nullptr) {
    const std::string &reload_account =
        saved_state->account_name.empty() ? account_name : saved_state->account_name;
    const std::string &reload_realm =
        saved_state->realm_name.empty() ? realm_name : saved_state->realm_name;
    if (reload_account.empty() || reload_realm.empty()) {
      return;
    }

    LoadSavedStateForCharacter(reload_account, reload_realm, saved_state->character_name);
    return;
  }

  if (account_name.empty() || realm_name.empty()) {
    return;
  }

  LoadSavedStateForCharacter(account_name, realm_name, character_name);
}

void AddOnsData::ReloadSavedStates() {
  struct SavedStateReloadRequest {
    std::string account_name;
    std::string realm_name;
    std::string character_name;
  };

  std::vector<SavedStateReloadRequest> reloads;
  reloads.reserve(saved_state_lists_.size());
  for (const auto &[_, saved_state] : saved_state_lists_) {
    if (saved_state.account_name.empty() || saved_state.realm_name.empty() ||
        saved_state.character_name.empty()) {
      continue;
    }
    reloads.push_back({
        .account_name = saved_state.account_name,
        .realm_name = saved_state.realm_name,
        .character_name = saved_state.character_name,
    });
  }

  for (const auto &reload : reloads) {
    LoadSavedStateForCharacter(reload.account_name, reload.realm_name, reload.character_name);
  }
}

AddOnsData::SavedStateList *AddOnsData::FindSavedStateList(const char *character_name) {
  if (character_name == nullptr) {
    return nullptr;
  }

  for (auto &[_, saved_state] : saved_state_lists_) {
    if (AsciiCaseInsensitiveEquals(saved_state.character_name, character_name)) {
      return &saved_state;
    }
  }
  return nullptr;
}

const AddOnsData::SavedStateList *AddOnsData::FindSavedStateList(const char *character_name) const {
  if (character_name == nullptr) {
    return nullptr;
  }

  for (const auto &[_, saved_state] : saved_state_lists_) {
    if (AsciiCaseInsensitiveEquals(saved_state.character_name, character_name)) {
      return &saved_state;
    }
  }
  return nullptr;
}

AddOnsData::SavedStateList *AddOnsData::FindSavedStateList(const std::string &account_name,
                                                           const std::string &realm_name,
                                                           const char *character_name) {
  if (account_name.empty() || realm_name.empty() || character_name == nullptr) {
    return nullptr;
  }
  const auto it = saved_state_lists_.find(MakeSavedStateKey(account_name, realm_name,
                                                            character_name));
  return it != saved_state_lists_.end() ? &it->second : nullptr;
}

const AddOnsData::SavedStateList *
AddOnsData::FindSavedStateList(const std::string &account_name,
                               const std::string &realm_name,
                               const char *character_name) const {
  if (account_name.empty() || realm_name.empty() || character_name == nullptr) {
    return nullptr;
  }
  const auto it = saved_state_lists_.find(MakeSavedStateKey(account_name, realm_name,
                                                            character_name));
  return it != saved_state_lists_.end() ? &it->second : nullptr;
}

bool AddOnsData::SaveSavedStates() {
  bool any_new_file_created = false;

  for (auto &[_, saved_state] : saved_state_lists_) {
    if (!saved_state.dirty) {
      continue;
    }
    saved_state.dirty = false;

    const auto character_dir = std::filesystem::path("WTF") / "Account" /
                               saved_state.account_name / saved_state.realm_name /
                               saved_state.character_name;
    std::error_code ec;
    if (!std::filesystem::is_directory(character_dir, ec)) {
      std::filesystem::create_directories(character_dir, ec);
      if (ec) {
        continue;
      }
    }

    const auto addons_file = character_dir / "AddOns.txt";
    if (!std::filesystem::exists(addons_file, ec)) {
      any_new_file_created = true;
    }

    std::ofstream output(addons_file, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      continue;
    }

    for (const auto &addon_name : saved_state.addon_order) {
      const bool *enabled = FindSavedStateValue(saved_state.addon_enabled, addon_name);
      if (enabled == nullptr) {
        continue;
      }
      output << addon_name << ": " << (*enabled ? "enabled" : "disabled") << "\r\n";
    }
  }

  return any_new_file_created;
}

}

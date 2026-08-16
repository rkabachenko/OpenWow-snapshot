
#include "openwow/ui/addon_manager.h"

#include "openwow/data/archive_system.h"
#include "openwow/ui/addons_data.h"
#include "openwow/ui/toc_parser.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/client_path_identity.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace openwow::ui {

using openwow::text::ToLowerAscii;
using openwow::text::Trim;

namespace {

std::string CanonicalizeAddonKey(std::string_view name) {
  return ToLowerAscii(std::string(name));
}

std::string NormalizeAddonVirtualRoot(std::string path) {
  return openwow::vfs::MakeClientPathIdentity(path).display_path;
}

bool AsciiCaseInsensitiveEquals(const std::string_view lhs, const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
        std::tolower(static_cast<unsigned char>(rhs[index]))) {
      return false;
    }
  }
  return true;
}

bool AsciiCaseInsensitiveStartsWith(const std::string_view value,
                                    const std::string_view prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }
  return AsciiCaseInsensitiveEquals(value.substr(0, prefix.size()), prefix);
}

std::filesystem::path ComposeAddonMetadataPath(
    const std::filesystem::path& addon_directory,
    const std::string& addon_name,
    const std::string_view extension) {
  return addon_directory / (addon_name + std::string(extension));
}

void TrimRightShortcutValue(std::string& value) {
  while (!value.empty()) {
    const char ch = value.back();
    if (ch != '\r' && ch != '\n' && ch != ' ') {
      break;
    }
    value.pop_back();
  }
}

void ResetAddonSecurityMetadata(AddonInfo& info) {
  info.public_key_state = 0;
  info.has_public_key = false;
  info.public_key.fill(0);
  info.update_url.clear();
}

void LoadAddonPublicKeyMetadata(const std::filesystem::path& addon_directory,
                                AddonInfo& info) {
  std::ifstream input(
      ComposeAddonMetadataPath(addon_directory, info.name, ".pub"),
      std::ios::binary);
  if (!input) {
    return;
  }

  const std::vector<std::uint8_t> bytes(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
  if (bytes.size() != (info.public_key.size() + 1u)) {
    return;
  }

  info.public_key_state = bytes.front();
  info.has_public_key = info.public_key_state != 0;
  std::copy(bytes.begin() + 1, bytes.end(), info.public_key.begin());
}

void LoadAddonUpdateUrlMetadata(const std::filesystem::path& addon_directory,
                                AddonInfo& info) {
  info.update_url.clear();

  std::ifstream input(
      ComposeAddonMetadataPath(addon_directory, info.name, ".url"),
      std::ios::binary);
  if (!input) {
    return;
  }

  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  const std::size_t separator = contents.find('=');
  if (separator == std::string::npos) {
    return;
  }

  info.update_url = contents.substr(separator + 1);
  TrimRightShortcutValue(info.update_url);
}

void LoadAddonSecurityMetadata(const std::filesystem::path& addon_directory,
                               AddonInfo& info) {
  ResetAddonSecurityMetadata(info);
  if (!info.is_secure) {
    return;
  }

  LoadAddonPublicKeyMetadata(addon_directory, info);
  LoadAddonUpdateUrlMetadata(addon_directory, info);
}

void LoadAddonSecurityMetadata(
    const openwow::vfs::VirtualFileSystem& vfs,
    const std::string& addon_virtual_directory,
    AddonInfo& info) {
  ResetAddonSecurityMetadata(info);
  if (!info.is_secure) {
    return;
  }

  const std::string metadata_stem =
      addon_virtual_directory + "/" + info.name;
  if (const auto public_key = vfs.ReadFileBytes(metadata_stem + ".pub");
      public_key.has_value() &&
      public_key->size() == info.public_key.size() + 1u) {
    info.public_key_state = public_key->front();
    info.has_public_key = info.public_key_state != 0;
    std::copy(public_key->begin() + 1, public_key->end(),
              info.public_key.begin());
  }

  if (const auto shortcut = vfs.ReadTextFile(metadata_stem + ".url");
      shortcut.has_value()) {
    const std::size_t separator = shortcut->find('=');
    if (separator != std::string::npos) {
      info.update_url = shortcut->substr(separator + 1);
      TrimRightShortcutValue(info.update_url);
    }
  }
}

std::string GetCurrentAddonLocaleToken() {
  const auto& locale = openwow::data::GetCurrentLocaleInfo();
  std::string token = locale.language + locale.country;
  if (token.size() < 4u) {
    return "enUS";
  }
  token.resize(4u);
  return token;
}

bool IsAddonMetadataDirective(const std::string_view key) {
  return AsciiCaseInsensitiveStartsWith(key, "Title") ||
         AsciiCaseInsensitiveStartsWith(key, "Notes") ||
         AsciiCaseInsensitiveStartsWith(key, "Author") ||
         AsciiCaseInsensitiveStartsWith(key, "Version") ||
         AsciiCaseInsensitiveStartsWith(key, "X-");
}

std::optional<std::string> NormalizeAddonMetadataDirectiveKey(
    const std::string_view raw_key,
    const std::string_view current_locale) {
  static constexpr std::string_view kSupportedLocales[] = {
      "enUS", "koKR", "frFR", "deDE", "zhCN",
      "zhTW", "esES", "esMX", "ruRU",
  };

  const std::size_t dash = raw_key.rfind('-');
  if (dash == std::string_view::npos || dash + 5u > raw_key.size()) {
    return std::string(raw_key);
  }

  const std::string_view suffix = raw_key.substr(dash + 1u, 4u);
  for (const auto locale : kSupportedLocales) {
    if (!AsciiCaseInsensitiveEquals(suffix, locale)) {
      continue;
    }
    if (!AsciiCaseInsensitiveEquals(current_locale, locale)) {
      return std::nullopt;
    }
    return std::string(raw_key.substr(0, dash));
  }

  return std::string(raw_key);
}

void UpsertMetadataField(AddonInfo& info, std::string key, std::string value) {
  auto it = std::find_if(info.metadata_fields.begin(), info.metadata_fields.end(),
                         [&](const auto& entry) {
                           return AsciiCaseInsensitiveEquals(entry.first, key);
                         });
  if (it != info.metadata_fields.end()) {
    *it = {std::move(key), std::move(value)};
    return;
  }
  info.metadata_fields.emplace_back(std::move(key), std::move(value));
}

const std::string* FindMetadataField(
    const std::vector<std::pair<std::string, std::string>>& metadata_fields,
    const std::string_view key) {
  const auto it = std::find_if(metadata_fields.begin(), metadata_fields.end(),
                               [&](const auto& entry) {
                                 return AsciiCaseInsensitiveEquals(entry.first, key);
                               });
  return it != metadata_fields.end() ? &it->second : nullptr;
}

void PopulateAddonMetadataFields(const TOCFile& toc, AddonInfo& info) {
  info.metadata_fields.clear();
  info.title = info.name;
  info.notes.clear();
  info.author.clear();
  info.version.clear();
  UpsertMetadataField(info, "Title", info.name);

  const std::string current_locale = GetCurrentAddonLocaleToken();
  for (const auto& [raw_key, raw_value] : toc.directive_entries) {
    if (!IsAddonMetadataDirective(raw_key)) {
      continue;
    }

    const auto normalized_key =
        NormalizeAddonMetadataDirectiveKey(raw_key, current_locale);
    if (!normalized_key.has_value() || normalized_key->empty()) {
      continue;
    }

    UpsertMetadataField(info, *normalized_key, raw_value);
  }

  if (const auto* title = FindMetadataField(info.metadata_fields, "Title");
      title != nullptr) {
    info.title = *title;
  }
  if (const auto* notes = FindMetadataField(info.metadata_fields, "Notes");
      notes != nullptr) {
    info.notes = *notes;
  }
  if (const auto* author = FindMetadataField(info.metadata_fields, "Author");
      author != nullptr) {
    info.author = *author;
  }
  if (const auto* version = FindMetadataField(info.metadata_fields, "Version");
      version != nullptr) {
    info.version = *version;
  }
}

void PopulateAddonInfoFromParsedToc(const TOCFile& toc,
                                    const std::string& addon_name,
                                    const std::string& directory,
                                    const std::string& toc_path,
                                    const int registration_order,
                                    AddonInfo& info) {
  info = {};
  info.name = addon_name;
  info.title = toc.title;
  info.notes = toc.notes;
  info.author = toc.author;
  info.version = toc.version;
  info.revision = toc.GetRevision();
  info.interface_version = toc.GetInterfaceVersion();
  info.directory = directory;
  info.toc_path = toc_path;
  info.enabled = toc.IsDefaultEnabled();
  info.load_on_demand = toc.IsLoadOnDemand();
  info.dependencies = toc.dependencies;
  info.optional_deps = toc.optional_deps;
  info.load_with = toc.load_with;
  info.load_managers = toc.load_managers;
  info.registration_order = registration_order;

  if (!toc.saved_variables.empty()) {
    info.saved_variables = TOCParser::SplitComma(toc.saved_variables);
  }
  if (!toc.saved_variables_per_character.empty()) {
    info.saved_variables_per_char = TOCParser::SplitComma(toc.saved_variables_per_character);
  }

  PopulateAddonMetadataFields(toc, info);
  info.is_blizzard = addon_name.rfind("Blizzard_", 0) == 0;
  info.is_secure = toc.IsSecure();
  info.out_of_date = info.interface_version > 0 &&
                     info.interface_version != AddonManager::kClientInterfaceVersion;
}

std::optional<std::filesystem::path> ResolveAddonDirectory(
    const openwow::vfs::VirtualFileSystem& vfs,
    const std::string& toc_path) {
  const auto resolved_toc_path = vfs.Resolve(toc_path);
  if (!resolved_toc_path.has_value()) {
    return std::nullopt;
  }
  return resolved_toc_path->parent_path();
}

std::vector<std::string> SplitLooseVirtualPath(std::string virtual_directory) {
  std::replace(virtual_directory.begin(), virtual_directory.end(), '\\', '/');
  while (!virtual_directory.empty() && virtual_directory.front() == '/') {
    virtual_directory.erase(virtual_directory.begin());
  }

  std::vector<std::string> components;
  std::size_t start = 0;
  while (start < virtual_directory.size()) {
    const std::size_t end = virtual_directory.find('/', start);
    const std::string component = virtual_directory.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    if (!component.empty()) {
      components.push_back(component);
    }
    start = end == std::string::npos ? virtual_directory.size() : end + 1;
  }
  return components;
}

std::optional<std::filesystem::path> ResolveLooseDirectoryInMount(
    const openwow::vfs::MountPoint& mount,
    const std::vector<std::string>& components) {
  if (!mount.enabled || mount.source_root.empty() ||
      mount.kind == openwow::vfs::MountKind::kMpqArchive) {
    return std::nullopt;
  }

  std::filesystem::path current = mount.source_root;
  for (const auto& component : components) {
    std::error_code ec;
    if (!std::filesystem::is_directory(current, ec)) {
      return std::nullopt;
    }

    std::optional<std::filesystem::path> matched;
    for (const auto& entry : std::filesystem::directory_iterator(current, ec)) {
      if (ec) {
        break;
      }
      if (AsciiCaseInsensitiveEquals(entry.path().filename().string(), component)) {
        matched = entry.path();
        break;
      }
    }
    if (!matched.has_value()) {
      return std::nullopt;
    }
    current = *matched;
  }
  return current;
}

struct LooseAddonDirectory {
  std::string canonical_name;
  std::string display_name;
  std::filesystem::path directory;
};

std::vector<LooseAddonDirectory> CollectLooseAddonDirectories(
    const openwow::vfs::VirtualFileSystem& vfs,
    const std::string& addons_virtual_root) {
  std::vector<LooseAddonDirectory> directories;
  std::unordered_set<std::string> seen;
  const auto root_components = SplitLooseVirtualPath(addons_virtual_root);
  for (const auto& mount : vfs.mounts()) {
    const auto root = ResolveLooseDirectoryInMount(mount, root_components);
    if (!root.has_value()) {
      continue;
    }

    std::vector<LooseAddonDirectory> mount_directories;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(*root, ec)) {
      if (ec) {
        break;
      }
      if (!entry.is_directory(ec) || ec) {
        ec.clear();
        continue;
      }
      const std::string name = entry.path().filename().string();
      if (name.empty() || name.front() == '.') {
        continue;
      }
      mount_directories.push_back({CanonicalizeAddonKey(name), name,
                                   entry.path()});
    }
    std::sort(mount_directories.begin(), mount_directories.end(),
              [](const auto& lhs, const auto& rhs) {
                if (lhs.canonical_name != rhs.canonical_name) {
                  return lhs.canonical_name < rhs.canonical_name;
                }
                return lhs.display_name < rhs.display_name;
              });
    for (auto& directory : mount_directories) {

      if (seen.insert(directory.canonical_name).second) {
        directories.push_back(std::move(directory));
      }
    }
  }
  return directories;
}

void ResolveAddonDependencies(std::vector<AddonInfo>& addons) {
  std::unordered_set<std::string> names;
  names.reserve(addons.size());
  for (const auto& addon : addons) {
    names.insert(CanonicalizeAddonKey(addon.name));
  }

  for (auto& addon : addons) {
    addon.dep_missing = false;
    addon.missing_dep_name.clear();
    for (const auto& dependency : addon.dependencies) {
      if (!names.contains(CanonicalizeAddonKey(dependency))) {
        addon.dep_missing = true;
        addon.missing_dep_name = dependency;
        break;
      }
    }
  }
}

}

AddonManager& AddonManager::Get() {
  static AddonManager instance;
  return instance;
}

void AddonManager::ScanAddons(const std::string& interfacePath) {
  bool has_addons_dir = true;
  {
    std::lock_guard lock(mutex_);
    addons_.clear();
    addon_index_.clear();

    const std::string addons_dir = interfacePath + "/AddOns";
    std::error_code ec;
    if (!std::filesystem::exists(addons_dir, ec)) {
      has_addons_dir = false;
    }

    if (has_addons_dir) {
      for (const auto& entry :
           std::filesystem::directory_iterator(addons_dir, ec)) {
        if (!entry.is_directory()) continue;

        const std::string dir_name = entry.path().filename().string();
        const std::string toc_path =
            entry.path().string() + "/" + dir_name + ".toc";

        if (!std::filesystem::exists(toc_path, ec)) continue;

        auto toc = TOCParser::ParseFile(toc_path);
        if (!toc.has_value()) continue;

        AddonInfo info;
        PopulateAddonInfoFromParsedToc(*toc, dir_name, entry.path().string(), toc_path,
                                       static_cast<int>(addons_.size()), info);
        {
          std::ifstream input(toc_path, std::ios::binary);
          const std::string contents((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
          info.toc_file_entry_count = TOCParser::CountVisibleFileEntries(contents, true);
        }

        LoadAddonSecurityMetadata(entry.path(), info);

        addon_index_[CanonicalizeAddonKey(info.name)] = addons_.size();
        addons_.push_back(std::move(info));
      }
    }

    ResolveLoadOrder();
    discovery_complete_ = true;
  }
  AddOnsData::Get().ImportFromAddonManagerSnapshot(SnapshotAddons());
}

void AddonManager::ScanAddons(const openwow::vfs::VirtualFileSystem& vfs,
                              const std::string& interfacePath) {
  PublishDiscoveredAddons(DiscoverAddons(vfs, interfacePath));
}

std::vector<AddonInfo> AddonManager::DiscoverAddons(
    const openwow::vfs::VirtualFileSystem& vfs,
    const std::string& interfacePath) {
  std::vector<AddonInfo> addons;
  const std::string addons_dir =
      NormalizeAddonVirtualRoot(interfacePath + "/AddOns");
  const auto addons_identity =
      openwow::vfs::MakeClientPathIdentity(addons_dir);
  std::unordered_set<std::string> seen_addons;

  const auto loose_directories =
      CollectLooseAddonDirectories(vfs, addons_dir);
  std::unordered_map<std::string, const LooseAddonDirectory*>
      loose_directory_by_name;
  loose_directory_by_name.reserve(loose_directories.size());
  for (const auto& directory : loose_directories) {
    loose_directory_by_name.emplace(directory.canonical_name, &directory);
  }

  const auto register_addon = [&](const std::string& addon_name) {
    const std::string canonical_name = CanonicalizeAddonKey(addon_name);
    if (addon_name.empty() || seen_addons.contains(canonical_name)) {
      return;
    }

    const auto toc_identity = openwow::vfs::MakeClientPathIdentity(
        addons_dir + "/" + addon_name + "/" + addon_name + ".toc");
    if (toc_identity.empty()) {
      return;
    }
    const std::string& toc_path = toc_identity.display_path;
    const auto toc_text = vfs.ReadTextFile(toc_path);
    if (!toc_text.has_value()) {
      return;
    }

    auto toc = TOCParser::Parse(*toc_text, toc_path);
    if (!toc.has_value()) {
      return;
    }

    if (!seen_addons.insert(canonical_name).second) {
      return;
    }

    const auto loose_directory =
        loose_directory_by_name.find(canonical_name);
    const bool has_loose_directory =
        loose_directory != loose_directory_by_name.end();
    const auto resolved_directory = has_loose_directory
        ? std::optional<std::filesystem::path>(loose_directory->second->directory)
        : ResolveAddonDirectory(vfs, toc_path);
    const std::string addon_virtual_directory =
        addons_dir + "/" + addon_name;
    const std::string directory =
        resolved_directory.has_value() ? resolved_directory->string()
                                       : std::filesystem::path(toc_path)
                                             .parent_path()
                                             .string();

    AddonInfo info;
    PopulateAddonInfoFromParsedToc(
        *toc, addon_name, directory, toc_path,
        static_cast<int>(addons.size()), info);

    info.toc_file_entry_count =
        TOCParser::CountVisibleFileEntries(*toc_text, true);
    LoadAddonSecurityMetadata(vfs, addon_virtual_directory, info);
    addons.push_back(std::move(info));
  };

  const std::string root_prefix = addons_identity.lookup_path + "/";
  const auto archive_files = vfs.EnumerateFilesByMountKind(
      addons_dir, true, openwow::vfs::MountKind::kMpqArchive);
  for (const auto& path : archive_files) {
    const auto identity =
        openwow::vfs::MakeClientPathIdentity(path.generic_string());
    if (identity.empty() || identity.lookup_path.rfind(root_prefix, 0) != 0 ||
        !AsciiCaseInsensitiveEquals(path.extension().string(), ".toc")) {
      continue;
    }

    const std::filesystem::path relative_path(
        identity.display_path.substr(root_prefix.size()));
    if (relative_path.empty() ||
        std::distance(relative_path.begin(), relative_path.end()) < 2) {
      continue;
    }
    register_addon(relative_path.begin()->string());
  }

  for (const auto& directory : loose_directories) {
    register_addon(directory.display_name);
  }

  ResolveAddonDependencies(addons);
  return addons;
}

void AddonManager::PublishDiscoveredAddons(std::vector<AddonInfo> addons) {
  ResolveAddonDependencies(addons);
  {
    std::lock_guard lock(mutex_);
    addons_ = std::move(addons);
    addon_index_.clear();
    addon_index_.reserve(addons_.size());
    for (std::size_t index = 0; index < addons_.size(); ++index) {
      addon_index_[CanonicalizeAddonKey(addons_[index].name)] = index;
    }
    discovery_complete_ = true;
  }
  AddOnsData::Get().ImportFromAddonManagerSnapshot(SnapshotAddons());
}

bool AddonManager::HasCompletedDiscovery() const {
  std::lock_guard lock(mutex_);
  return discovery_complete_;
}

size_t AddonManager::GetNumAddons() const {
  std::lock_guard lock(mutex_);
  return addons_.size();
}

const AddonInfo* AddonManager::GetAddonInfo(size_t index) const {
  std::lock_guard lock(mutex_);
  if (index >= addons_.size()) return nullptr;
  return &addons_[index];
}

const AddonInfo* AddonManager::GetAddonInfo(const std::string& name) const {
  std::lock_guard lock(mutex_);
  auto it = addon_index_.find(CanonicalizeAddonKey(name));
  if (it == addon_index_.end()) return nullptr;
  return &addons_[it->second];
}

int AddonManager::GetAddonIndex(const std::string& name) const {
  std::lock_guard lock(mutex_);
  auto it = addon_index_.find(CanonicalizeAddonKey(name));
  if (it == addon_index_.end()) return -1;
  return static_cast<int>(it->second);
}

std::vector<AddonInfo> AddonManager::SnapshotAddons() const {
  std::lock_guard lock(mutex_);
  return addons_;
}

std::vector<AddonInfo> AddonManager::SnapshotAddonsByRegistrationOrder() const {
  std::lock_guard lock(mutex_);
  auto addons = addons_;
  std::stable_sort(addons.begin(), addons.end(), [](const AddonInfo& lhs, const AddonInfo& rhs) {
    return lhs.registration_order < rhs.registration_order;
  });
  return addons;
}

void AddonManager::EnableAddon(const std::string& name) {
  {
    std::lock_guard lock(mutex_);
    if (auto* info = FindMutable(name)) {
      info->enabled = true;
    }
  }
  AddOnsData::Get().ImportFromAddonManagerSnapshot(SnapshotAddons());
}

void AddonManager::DisableAddon(const std::string& name) {
  {
    std::lock_guard lock(mutex_);
    if (auto* info = FindMutable(name)) {
      info->enabled = false;
    }
  }
  AddOnsData::Get().ImportFromAddonManagerSnapshot(SnapshotAddons());
}

void AddonManager::EnableAllAddons() {
  {
    std::lock_guard lock(mutex_);
    for (auto& addon : addons_) {
      addon.enabled = true;
    }
  }
  AddOnsData::Get().ImportFromAddonManagerSnapshot(SnapshotAddons());
}

void AddonManager::DisableAllAddons() {
  {
    std::lock_guard lock(mutex_);
    for (auto& addon : addons_) {
      addon.enabled = false;
    }
  }
  AddOnsData::Get().ImportFromAddonManagerSnapshot(SnapshotAddons());
}

bool AddonManager::IsAddonEnabled(const std::string& name) const {
  std::lock_guard lock(mutex_);
  auto it = addon_index_.find(CanonicalizeAddonKey(name));
  if (it == addon_index_.end()) return false;
  return addons_[it->second].enabled;
}

bool AddonManager::IsAddonLoadOnDemand(const std::string& name) const {
  std::lock_guard lock(mutex_);
  auto it = addon_index_.find(CanonicalizeAddonKey(name));
  if (it == addon_index_.end()) return false;
  return addons_[it->second].load_on_demand;
}

size_t AddonManager::GetAddonMemoryUsage(const std::string& name) const {
  std::lock_guard lock(mutex_);
  auto it = addon_index_.find(CanonicalizeAddonKey(name));
  if (it == addon_index_.end()) return 0;
  return addons_[it->second].memory_usage;
}

size_t AddonManager::GetTotalMemoryUsage() const {
  std::lock_guard lock(mutex_);
  size_t total = 0;
  for (const auto& addon : addons_) {
    total += addon.memory_usage;
  }
  return total;
}

void AddonManager::Reset() {
  {
    std::lock_guard lock(mutex_);
    addons_.clear();
    addon_index_.clear();
    discovery_complete_ = false;
  }
  AddOnsData::Get().ImportFromAddonManagerSnapshot(SnapshotAddons());
}

void AddonManager::AddAddon(AddonInfo info) {
  {
    std::lock_guard lock(mutex_);
    if (info.registration_order < 0) {
      info.registration_order = static_cast<int>(addons_.size());
    }
    addon_index_[CanonicalizeAddonKey(info.name)] = addons_.size();
    addons_.push_back(std::move(info));
    ResolveLoadOrder();
    discovery_complete_ = true;
  }
  AddOnsData::Get().ImportFromAddonManagerSnapshot(SnapshotAddons());
}

void AddonManager::ResolveLoadOrder() {

  ResolveAddonDependencies(addons_);
}

AddonInfo* AddonManager::FindMutable(const std::string& name) {
  auto it = addon_index_.find(CanonicalizeAddonKey(name));
  if (it == addon_index_.end()) return nullptr;
  return &addons_[it->second];
}

}

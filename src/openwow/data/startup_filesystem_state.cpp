#include "openwow/data/startup_filesystem_state.h"

#include "openwow/core/storm_path.h"
#include "openwow/core/streaming_storage.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/ui/xml/xml_tree.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace openwow::data {

namespace {

const std::array<const char*, kStartupLocaleRingSize>& StartupLocaleRing() {
  static constexpr std::array<const char*, kStartupLocaleRingSize> kRing = {
      "deDE", "enGB", "enUS", "esES", "frFR", "koKR",
      "zhCN", "zhTW", "enCN", "enTW", "esMX", "ruRU",
  };
  return kRing;
}

StartupLocaleAvailability AllStartupLocalesAvailable() {
  StartupLocaleAvailability availability{};
  availability.fill(true);
  return availability;
}

StartupLocaleAvailability& MutableStartupLocaleAvailability() {
  static StartupLocaleAvailability availability = AllStartupLocalesAvailable();
  return availability;
}

StartupFileSystemState& MutableStartupFileSystemState() {
  static StartupFileSystemState state;
  return state;
}

std::function<void(const std::string&)>& MutableStormBasePathSinkForTests() {
  static std::function<void(const std::string&)> sink;
  return sink;
}

std::string& MutableCachedStartupWorkingDirectory() {
  static std::string path;
  return path;
}

constexpr std::size_t kStartupPathCapacity = 260;
constexpr std::size_t kCachedWorkingDirectoryCapacity = 0x400u;

std::filesystem::path StartupStatePathToNative(std::string path) {
#if !defined(_WIN32)
  std::replace(path.begin(), path.end(), '\\',
               std::filesystem::path::preferred_separator);
#endif
  return std::filesystem::path(path);
}

void StorePathWithTrailingBackslash(std::string* target,
                                    const std::string& path) {
  std::array<char, kStartupPathCapacity> buffer{};
  const int length = openwow::core::CopyStormPath(
      buffer.data(), path.c_str(), static_cast<int>(buffer.size()));
  if (buffer[0] != '\0') {
    const char tail = buffer[static_cast<std::size_t>(length - 1)];
    if (tail != '\\' && tail != '/') {
      openwow::core::AppendStormPath(buffer.data(), "\\",
                                     static_cast<int>(buffer.size()));
    }
  }
  target->assign(buffer.data());
}

void ForwardStormBasePath(const std::string& path) {
  auto& sink = MutableStormBasePathSinkForTests();
  if (sink) {
    sink(path);
  }
  (void)path;
}

bool StartupLocaleTagMatches(const std::string_view lhs, const char* rhs) {
  if (lhs.size() < 4 || rhs == nullptr) {
    return false;
  }

  for (std::size_t i = 0; i < 4; ++i) {
    char a = lhs[i];
    char b = rhs[i];
    if (a >= 'A' && a <= 'Z') {
      a = static_cast<char>(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
      b = static_cast<char>(b - 'A' + 'a');
    }
    if (a != b) {
      return false;
    }
  }

  return true;
}

bool IsDataDirectory(const std::filesystem::path& path) {
  return openwow::text::EqualsIgnoreCaseAscii(path.filename().string(), "Data");
}

std::filesystem::path ResolveDataDir(const std::filesystem::path& root) {
  if (IsDataDirectory(root)) {
    return root;
  }
  return root / "Data";
}

std::filesystem::path ResolveStartupClientRootForArchiveProbe(
    const std::filesystem::path& game_root) {
  const auto& state = MutableStartupFileSystemState();
  if (!state.executable_base_path.empty()) {
    return StartupStatePathToNative(state.executable_base_path);
  }
  if (!game_root.empty()) {
    return game_root;
  }
  return std::filesystem::current_path();
}

std::filesystem::path ResolveStartupClientDirectory(
    const std::filesystem::path& startup_root) {
  if (!startup_root.empty() && startup_root.filename().empty()) {
    return startup_root.parent_path();
  }
  return startup_root;
}

std::filesystem::path ResolveSiblingDataDir(const std::filesystem::path& root) {
  const auto data_dir = ResolveDataDir(root);
  if (data_dir.empty()) {
    return {};
  }

  const auto upper_root = data_dir.parent_path().parent_path();
  if (upper_root.empty()) {
    return {};
  }

  return upper_root / "Data";
}

std::filesystem::path ResolveRetailInstallRootForArchiveProbe(
    const std::filesystem::path& retail_install_root) {
  if (!retail_install_root.empty()) {
    return retail_install_root;
  }

  const auto& state = MutableStartupFileSystemState();
  if (state.retail_install_path_cache.empty()) {
    return {};
  }

  return StartupStatePathToNative(state.retail_install_path_cache);
}

bool PathIsRegularFile(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec);
}

ArchiveProbeNativeRoots ResolveArchiveProbeNativeRootsForCommonLayout(
    const std::filesystem::path& game_root,
    const std::filesystem::path& retail_install_root) {
  const auto startup_root = ResolveStartupClientRootForArchiveProbe(game_root);
  ArchiveProbeNativeRoots roots;
  roots.data_root = ResolveDataDir(startup_root);

  if (!MutableStartupFileSystemState().executable_base_path.empty()) {
    const auto upper_root =
        ResolveStartupClientDirectory(startup_root).parent_path();
    if (!upper_root.empty()) {
      roots.parent_data_root = ResolveDataDir(upper_root);
    }
  } else {
    roots.parent_data_root = ResolveSiblingDataDir(startup_root);
  }

  const auto retail_root =
      ResolveRetailInstallRootForArchiveProbe(retail_install_root);
  if (!retail_root.empty()) {
    roots.retail_data_root = ResolveDataDir(retail_root);
  }

  return roots;
}

}

std::string ReplaceArchiveLocalePlaceholdersExact(
    const std::string_view template_path, const char* locale_token) {
  std::string result(template_path);
  std::size_t pos = 0;
  while ((pos = result.find("****", pos)) != std::string::npos) {
    if (!locale_token) {
      return {};
    }

    result.replace(pos, 4, std::string(locale_token, locale_token + 4));
    pos += 4;
  }
  return result;
}

std::optional<std::string> BuildArchiveProbePathExact(
    const int root_index,
    const std::string_view suffix_template,
    const char* locale_token,
    const std::string_view retail_install_path) {
  std::string path;
  switch (root_index) {
    case 0:
      path = "Data\\";
      break;
    case 1:
      path = "..\\Data\\";
      break;
    case 2:
      if (retail_install_path.empty()) {
        return std::nullopt;
      }
      path.assign(retail_install_path);
      path += "Data\\";
      break;
    default:
      return std::nullopt;
  }

  const std::string suffix =
      ReplaceArchiveLocalePlaceholdersExact(suffix_template, locale_token);
  if (suffix.empty() && suffix_template.find("****") != std::string_view::npos) {
    return std::nullopt;
  }

  path += suffix;
  return path;
}

std::optional<std::filesystem::path> BuildArchiveProbePathNative(
    const int root_index,
    const ArchiveProbeNativeRoots& roots,
    const std::string_view suffix_template,
    const char* locale_token) {
  std::filesystem::path base_root;
  switch (root_index) {
    case 0:
      base_root = roots.data_root;
      break;
    case 1:
      base_root = roots.parent_data_root;
      break;
    case 2:
      base_root = roots.retail_data_root;
      break;
    default:
      return std::nullopt;
  }

  if (base_root.empty()) {
    return std::nullopt;
  }

  const std::string relative =
      ReplaceArchiveLocalePlaceholdersExact(suffix_template, locale_token);
  if (relative.empty() && suffix_template.find("****") != std::string_view::npos) {
    return std::nullopt;
  }

  return (base_root / StartupStatePathToNative(relative)).lexically_normal();
}

bool ProbeCommonArchiveLayout(
    const std::filesystem::path& game_root,
    const std::filesystem::path& retail_install_root) {
  const auto roots = ResolveArchiveProbeNativeRootsForCommonLayout(
      game_root,
      retail_install_root);
  for (int root_index = 0; root_index < 3; ++root_index) {
    const auto probe_path =
        BuildArchiveProbePathNative(root_index, roots, "common.MPQ", "");
    if (probe_path.has_value() && PathIsRegularFile(*probe_path)) {
      return true;
    }
  }

  return false;
}

void ClearStartupBasePathInitFlag() {
  MutableStartupFileSystemState().base_path_init_flag = false;
}

void SetStartupStormOpenFlags(std::uint32_t flags) {
  auto& state = MutableStartupFileSystemState();
  state.storm_open_flags = flags & 3u;
  state.forwarded_storm_open_flags = flags;
  openwow::core::StreamingStorage::Instance().SetStreamingStateValue(flags);
}

void SetStartupExecutableBasePath(const std::string& path) {
  StorePathWithTrailingBackslash(
      &MutableStartupFileSystemState().executable_base_path, path);
  (void)openwow::core::StreamingStorage::Instance().SetBasePath(path.c_str());
  ForwardStormBasePath(path);
}

void SetStartupArchiveDataPath(const std::string& path) {
  StorePathWithTrailingBackslash(
      &MutableStartupFileSystemState().archive_data_path, path);
}

void SetStartupLocaleDataPath(const std::string& path) {
  StorePathWithTrailingBackslash(
      &MutableStartupFileSystemState().locale_data_path, path);
}

const std::string& GetCachedStartupWorkingDirectory() {
  std::string& cached_path = MutableCachedStartupWorkingDirectory();
  if (cached_path.empty()) {
    std::array<char, kCachedWorkingDirectoryCapacity> buffer{};
    (void)openwow::vfs::FileSystem_GetWorkingDirectory(
        buffer.data(), static_cast<int>(buffer.size()));
    cached_path.assign(buffer.data());
  }
  return cached_path;
}

std::string ResolveStartupRetailInstallPath(
    bool online_mode,
    bool ptr_product,
    const std::function<std::string(const std::string& company_key,
                                    const std::string& value_name)>& registry_read) {
  if (!online_mode) {
    return {};
  }

  auto& state = MutableStartupFileSystemState();
  if (state.retail_install_path_cache.empty() && registry_read) {
    state.retail_install_path_cache = registry_read(
        ptr_product ? "World of Warcraft\\PTR" : "World of Warcraft",
        "RetailInstallPath");
  }
  return state.retail_install_path_cache;
}

InitFileSystemResult InitializeStartupFileSystem(
    const InitFileSystemInputs& inputs,
    const std::function<bool(const std::string& base_path)>&
        change_working_directory) {
  ClearStartupBasePathInitFlag();
  SetStartupStormOpenFlags(0);

  InitFileSystemResult result;
  result.selected_base_path = inputs.command_line_base_path.empty()
                                  ? inputs.module_directory
                                  : inputs.command_line_base_path;

  if (change_working_directory) {
    result.working_directory_change_result =
        change_working_directory(result.selected_base_path);
  }

  SetStartupExecutableBasePath(result.selected_base_path);
  MutableStartupFileSystemState().client_init_archive_gate = 0;
  SetStartupArchiveDataPath(inputs.archive_data_path);
  return result;
}

void SetStartupRetailInstallPathCache(const std::string& path) {
  MutableStartupFileSystemState().retail_install_path_cache = path;
}

bool IsWoWTProduct(
    const std::function<std::optional<std::string>()>& blizzard_component_reader) {
  if (!blizzard_component_reader) {
    return false;
  }

  const std::optional<std::string> xml = blizzard_component_reader();
  if (!xml) {
    return false;
  }

  auto* tree = openwow::ui::xml::XMLTree_Parse(xml->data(), xml->size());
  if (!tree) {
    return false;
  }

  bool matches = false;
  if (const auto* root = tree->root) {
    for (const auto& attr : root->attributes.entries) {
      if (!openwow::text::EqualsIgnoreCaseAscii(attr.name, "product")) {
        continue;
      }

      if (openwow::text::EqualsIgnoreCaseAscii(attr.value, "WoWT")) {
        matches = true;
      }
      break;
    }
  }

  openwow::ui::xml::XMLTree_Free(tree);
  return matches;
}

const StartupFileSystemState& GetStartupFileSystemState() {
  return MutableStartupFileSystemState();
}

const std::array<const char*, kStartupLocaleRingSize>& GetStartupLocaleRing() {
  return StartupLocaleRing();
}

int FindStartupLocaleRingIndexOrEnUSFallback(
    const std::string_view locale_code) {
  const auto& locale_ring = StartupLocaleRing();
  for (std::size_t i = 0; i < locale_ring.size(); ++i) {
    if (StartupLocaleTagMatches(locale_code, locale_ring[i])) {
      return static_cast<int>(i);
    }
  }
  return 2;
}

const StartupLocaleAvailability& GetStartupLocaleAvailability() {
  return MutableStartupLocaleAvailability();
}

void SetStartupLocaleAvailability(
    const StartupLocaleAvailability& availability) {
  MutableStartupLocaleAvailability() = availability;
}

void SetStartupStormBasePathSinkForTests(
    std::function<void(const std::string& path)> sink) {
  MutableStormBasePathSinkForTests() = std::move(sink);
}

void ResetCachedStartupWorkingDirectoryForTests() {
  MutableCachedStartupWorkingDirectory().clear();
}

void ResetStartupFileSystemStateForTests(const StartupFileSystemState& state) {
  MutableStartupFileSystemState() = state;
  MutableStartupLocaleAvailability() = AllStartupLocalesAvailable();
  ResetCachedStartupWorkingDirectoryForTests();
}

}

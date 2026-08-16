#include "openwow/data/login_resource_validator.h"

#include "openwow/core/storm_string.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openwow/data/startup_filesystem_state.h"
#include "openwow/platform/filesystem/filesystem.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/sfile_core.h"

namespace openwow::data {

using openwow::diagnostics::Log;
using openwow::diagnostics::LogLevel;
using openwow::text::ToLowerAscii;

namespace {

constexpr const char* kDefaultStartupLocale = "enUS";
constexpr const char* kSplitLayoutLocaleToken = "----";

constexpr const char* kAlternateArchiveName = "alternate.MPQ";
constexpr std::array<const char*, 6> kSplitArchiveTable = {
    "interface.MPQ",
    "misc.MPQ",
    "model.MPQ",
    "texture.MPQ",
    "terrain.MPQ",
    "wmo.MPQ",
};

constexpr std::array<const char*, 19> kCommonArchiveTable = {
    "dbc.MPQ",
    "speech.MPQ",
    "streaming.MPQ",
    "streamingloc.MPQ",
    "expansion.MPQ",
    "expansionloc.MPQ",
    "expansionspeech.MPQ",
    "common-2.MPQ",
    "common.MPQ",
    "lichking.MPQ",
    "lichkingloc.MPQ",
    "lichkingspeech.MPQ",
    "****\\locale-****.MPQ",
    "****\\speech-****.MPQ",
    "****\\expansion-locale-****.MPQ",
    "****\\expansion-speech-****.MPQ",
    "****\\lichking-locale-****.MPQ",
    "****\\lichking-speech-****.MPQ",
    "development.MPQ",
};

std::filesystem::path StartupStatePathToNative(std::string path) {
#if !defined(_WIN32)
  std::replace(path.begin(), path.end(), '\\',
               std::filesystem::path::preferred_separator);
#endif
  return std::filesystem::path(path);
}

bool EqualsAsciiInsensitive(const std::string& left, const std::string& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(left[i])) !=
        std::tolower(static_cast<unsigned char>(right[i])))
      return false;
  }
  return true;
}

void AppendUniquePath(std::vector<std::filesystem::path>* paths,
                      const std::filesystem::path& path) {
  if (paths == nullptr || path.empty()) {
    return;
  }

  const auto normalized = path.lexically_normal();
  for (const auto& existing : *paths) {
    if (existing.lexically_normal() == normalized) {
      return;
    }
  }

  paths->push_back(normalized);
}

bool IsDataDirectory(const std::filesystem::path& path) {
  return EqualsAsciiInsensitive(path.filename().string(), "Data");
}

std::filesystem::path ResolveStartupClientRoot(
    const std::filesystem::path& game_root) {
  const auto& state = GetStartupFileSystemState();
  if (!state.executable_base_path.empty()) {
    return StartupStatePathToNative(state.executable_base_path);
  }
  return game_root;
}

std::filesystem::path ResolveRetailInstallRoot(
    const std::filesystem::path& retail_install_root) {
  if (!retail_install_root.empty()) {
    return retail_install_root;
  }

  const auto& state = GetStartupFileSystemState();
  if (state.retail_install_path_cache.empty()) {
    return {};
  }
  return StartupStatePathToNative(state.retail_install_path_cache);
}

std::filesystem::path ResolveDataDir(const std::filesystem::path& game_root) {
  if (IsDataDirectory(game_root)) return game_root;
  auto data = game_root / "Data";
  return data;
}

std::filesystem::path ResolveConfiguredArchiveDataDir(
    const std::filesystem::path& client_root) {
  const auto& state = GetStartupFileSystemState();
  if (!state.executable_base_path.empty() && !state.archive_data_path.empty()) {
    std::string archive_data_path = state.archive_data_path;
    std::replace(archive_data_path.begin(), archive_data_path.end(), '\\', '/');
    return (client_root / std::filesystem::path(archive_data_path))
        .lexically_normal();
  }
  return ResolveDataDir(client_root);
}

std::filesystem::path ResolveStartupClientDirectory(
    const std::filesystem::path& startup_root) {
  if (!startup_root.empty() && startup_root.filename().empty()) {
    return startup_root.parent_path();
  }
  return startup_root;
}

std::vector<std::filesystem::path> CollectLoginFilesystemRoots(
    const std::filesystem::path& configured_game_root) {
  std::vector<std::filesystem::path> roots;
  const auto game_root = ResolveStartupClientRoot(configured_game_root);
  if (game_root.empty()) {
    return roots;
  }

  AppendUniquePath(&roots, game_root);

  const auto extracted_data_dir = ResolveConfiguredArchiveDataDir(game_root);
  if (!extracted_data_dir.empty()) {
    std::error_code ec;
    if (std::filesystem::is_directory(extracted_data_dir, ec) && !ec) {
      AppendUniquePath(&roots, extracted_data_dir);
    }
  }

  if (IsDataDirectory(game_root)) {
    const auto parent = game_root.parent_path();
    std::error_code ec;
    if (!parent.empty() && std::filesystem::is_directory(parent, ec) && !ec) {
      AppendUniquePath(&roots, parent);
    }
  }

  return roots;
}

bool IsKnownLocale(const std::string& locale) {
  for (const char* l : GetStartupLocaleRing()) {
    if (locale == l) return true;
  }
  return false;
}

std::filesystem::path ResolveSiblingDataDir(const std::filesystem::path& game_root) {
  const auto data_dir = ResolveDataDir(game_root);
  if (data_dir.empty()) return {};
  const auto data_parent = data_dir.parent_path();
  if (data_parent.empty()) return {};
  const auto upper_root = data_parent.parent_path();
  if (upper_root.empty()) return {};
  return upper_root / "Data";
}

std::string NormalizeConfiguredLocale(const std::string& configured_locale) {
  if (configured_locale.empty() || configured_locale == "****") {
    return kDefaultStartupLocale;
  }
  return configured_locale;
}

std::string NormalizeStartupProbeLocale(const std::string& configured_locale) {
  std::string normalized = NormalizeConfiguredLocale(configured_locale);
  if (normalized.size() > 4) {
    normalized.resize(4);
  }
  return normalized;
}

std::optional<std::filesystem::path> ResolveExistingRelativePathCaseInsensitive(
    const std::filesystem::path& root,
    const std::string_view relative_path) {
  if (root.empty()) {
    return std::nullopt;
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec) || ec) {
    return std::nullopt;
  }

  std::filesystem::path current = root;
  std::string component;
  component.reserve(relative_path.size());

  const auto flush_component = [&]() -> bool {
    if (component.empty() || component == ".") {
      component.clear();
      return true;
    }
    if (component == "..") {
      component.clear();
      return false;
    }

    std::optional<std::filesystem::path> matched;
    for (const auto& entry : std::filesystem::directory_iterator(current, ec)) {
      if (ec) {
        component.clear();
        return false;
      }
      if (openwow::text::EqualsIgnoreCaseAscii(entry.path().filename().string(),
                                               component)) {
        matched = entry.path();
        break;
      }
    }

    component.clear();
    if (!matched.has_value()) {
      return false;
    }

    current = *matched;
    return true;
  };

  for (const char ch : relative_path) {
    if (ch == '\\' || ch == '/') {
      if (!flush_component()) {
        return std::nullopt;
      }
      continue;
    }
    component.push_back(ch);
  }

  if (!flush_component()) {
    return std::nullopt;
  }

  return current;
}

bool RemoveResolvedDirectoryTree(const std::filesystem::path& path) {
  if (path.empty()) {
    return false;
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(path, ec) || ec) {
    return false;
  }

  std::filesystem::remove_all(path, ec);
  return !ec;
}

bool BackupResolvedDirectoryToDotOld(const std::filesystem::path& source,
                                     const std::string& destination_leaf_name) {
  if (source.empty() || destination_leaf_name.empty()) {
    return false;
  }

  std::error_code ec;
  std::filesystem::rename(source, source.parent_path() / destination_leaf_name, ec);
  return !ec;
}

bool BackupNamedDirectoryIfPresent(const std::filesystem::path& root,
                                   const std::string_view relative_path) {
  const auto source = ResolveExistingRelativePathCaseInsensitive(root, relative_path);
  if (!source.has_value()) {
    return false;
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(*source, ec) || ec) {
    return false;
  }

  const std::filesystem::path relative_fs_path(relative_path);
  const std::string destination_leaf =
      relative_fs_path.filename().string() + ".old";
  const std::string destination_relative =
      (relative_fs_path.parent_path() / destination_leaf).generic_string();

  if (const auto existing_destination =
          ResolveExistingRelativePathCaseInsensitive(root, destination_relative);
      existing_destination.has_value()) {
    if (!RemoveResolvedDirectoryTree(*existing_destination)) {
      return true;
    }
  }

  (void)BackupResolvedDirectoryToDotOld(*source, destination_leaf);
  return true;
}

bool AddonHasMatchingToc(const std::filesystem::path& addon_directory,
                         const std::string& addon_name) {
  const auto toc =
      ResolveExistingRelativePathCaseInsensitive(addon_directory, addon_name + ".toc");
  if (!toc.has_value()) {
    return false;
  }

  std::error_code ec;
  return std::filesystem::is_regular_file(*toc, ec) && !ec;
}

bool BackupLegacyBlizzardAddonsInRoot(const std::filesystem::path& root) {
  const auto addons_root =
      ResolveExistingRelativePathCaseInsensitive(root, "Interface/AddOns");
  if (!addons_root.has_value()) {
    return false;
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(*addons_root, ec) || ec) {
    return false;
  }

  bool attempted = false;
  for (const auto& entry : std::filesystem::directory_iterator(*addons_root, ec)) {
    if (ec) {
      break;
    }

    const auto addon_name = entry.path().filename().string();
    if (!openwow::core::SStrWildcardMatch(addon_name.c_str(), "Blizzard*")) {
      continue;
    }
    if (!AddonHasMatchingToc(entry.path(), addon_name)) {
      continue;
    }

    attempted = true;
    const auto existing_destination =
        ResolveExistingRelativePathCaseInsensitive(*addons_root, addon_name + ".old");
    if (existing_destination.has_value()
        && !RemoveResolvedDirectoryTree(*existing_destination)) {
      continue;
    }

    (void)BackupResolvedDirectoryToDotOld(entry.path(), addon_name + ".old");
  }

  return attempted;
}

ArchiveProbeNativeRoots ResolveArchiveProbeNativeRoots(
    const std::filesystem::path& game_root,
    const std::filesystem::path& retail_install_root) {
  const auto startup_root = ResolveStartupClientRoot(game_root);
  ArchiveProbeNativeRoots roots;
  roots.data_root = ResolveDataDir(startup_root);
  if (!GetStartupFileSystemState().executable_base_path.empty()) {
    const auto upper_root =
        ResolveStartupClientDirectory(startup_root).parent_path();
    if (!upper_root.empty()) {
      roots.parent_data_root = ResolveDataDir(upper_root);
    }
  } else {
    roots.parent_data_root = ResolveSiblingDataDir(startup_root);
  }
  if (!retail_install_root.empty()) {
    roots.retail_data_root = ResolveDataDir(retail_install_root);
  }
  return roots;
}

bool BuildArchiveProbePath(int root_index,
                           const std::filesystem::path& game_root,
                           const std::filesystem::path& retail_install_root,
                           const std::string& suffix_template,
                           const std::string& placeholder_value,
                           std::filesystem::path* out) {

  const auto probe_path = BuildArchiveProbePathNative(
      root_index,
      ResolveArchiveProbeNativeRoots(game_root, retail_install_root),
      suffix_template,
      placeholder_value.c_str());
  if (!probe_path.has_value() || !out) {
    return false;
  }

  *out = *probe_path;
  return true;
}

std::optional<std::filesystem::path> FindArchiveAcrossRoots(
    const std::filesystem::path& game_root,
    const std::filesystem::path& retail_install_root,
    const std::string& archive_name) {
  for (int root_index = 0; root_index < 3; ++root_index) {
    std::filesystem::path candidate;
    if (BuildArchiveProbePath(root_index,
                              game_root,
                              retail_install_root,
                              archive_name,
                              "",
                              &candidate) &&
        openwow::platform::filesystem::PathIsRegularFile(candidate)) {
      return candidate;
    }
  }
  return std::nullopt;
}

bool HasCommonArchiveLayout(const std::filesystem::path& game_root,
                            const std::filesystem::path& retail_install_root) {
  return ProbeCommonArchiveLayout(game_root, retail_install_root);
}

bool AnyExists(const openwow::vfs::VirtualFileSystem& vfs,
               const std::vector<std::string>& candidates) {
  for (const auto& c : candidates)
    if (vfs.Exists(c)) return true;
  return false;
}

std::vector<std::filesystem::path> CollectNumberedPatches(
    const std::filesystem::path& dir,
    const std::string& base_stem) {
  std::vector<std::pair<int, std::filesystem::path>> found;
  const auto lower_base = ToLowerAscii(base_stem);
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) break;
    if (!std::filesystem::is_regular_file(entry, ec) || ec) continue;
    const auto stem = entry.path().stem().string();
    const auto ext  = entry.path().extension().string();
    if (!EqualsAsciiInsensitive(ext, ".MPQ")) continue;
    const auto lower_stem = ToLowerAscii(stem);
    if (lower_stem.size() <= lower_base.size()) continue;
    if (lower_stem.substr(0, lower_base.size()) != lower_base) continue;
    const auto tail = lower_stem.substr(lower_base.size());
    if (tail.empty() || tail[0] != '-') continue;
    const auto num_str = tail.substr(1);
    if (num_str.empty()) continue;
    bool all_digits = true;
    for (char c : num_str) { if (c < '0' || c > '9') { all_digits = false; break; } }
    if (!all_digits) continue;
    found.emplace_back(std::stoi(num_str), entry.path());
  }
  std::sort(found.begin(), found.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<std::filesystem::path> result;
  result.reserve(found.size());
  for (auto& [n, p] : found) result.push_back(std::move(p));
  return result;
}

std::vector<std::filesystem::path> BuildStartupPatchChain(
    const std::filesystem::path& data_dir,
    const std::string& locale_token) {

  std::vector<std::filesystem::path> patch_chain;

  const auto global_patch = data_dir / "patch.MPQ";
  if (openwow::platform::filesystem::PathIsRegularFile(global_patch)) {
    patch_chain.push_back(global_patch);
  }

  auto global_numbered = CollectNumberedPatches(data_dir, "patch");
  std::sort(global_numbered.begin(), global_numbered.end(),
            [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
              return openwow::core::SStrCmpNoCase(
                         lhs.filename().string().c_str(),
                         rhs.filename().string().c_str(),
                         0x7FFFFFFFu) < 0;
            });
  for (const auto& p : global_numbered) {
    patch_chain.push_back(p);
  }

  if (locale_token.size() >= 4) {
    const auto locale_patch =
        data_dir / locale_token / ("patch-" + locale_token + ".MPQ");
    if (openwow::platform::filesystem::PathIsRegularFile(locale_patch)) {
      patch_chain.push_back(locale_patch);
    }

    auto locale_numbered = CollectNumberedPatches(
        data_dir / locale_token, "patch-" + locale_token);
    std::sort(locale_numbered.begin(), locale_numbered.end(),
              [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
                return openwow::core::SStrCmpNoCase(
                           lhs.filename().string().c_str(),
                           rhs.filename().string().c_str(),
                           0x7FFFFFFFu) < 0;
              });
    for (const auto& p : locale_numbered) {
      patch_chain.push_back(p);
    }
  }

  return patch_chain;
}

bool TryMountArchiveByName(openwow::vfs::VirtualFileSystem* vfs,
                           const std::filesystem::path& game_root,
                           const std::filesystem::path& retail_install_root,
                           const std::string& archive_name,
                           const std::vector<std::filesystem::path>& patch_chain,
                           int priority) {
  const auto archive_path =
      FindArchiveAcrossRoots(game_root, retail_install_root, archive_name);
  if (!archive_path.has_value()) {
    return false;
  }

  Log(LogLevel::kInfo,
      "[MPQ] Mount priority " + std::to_string(priority) + ": " +
          archive_path->string());
  for (const auto& patch : patch_chain) {
    Log(LogLevel::kInfo, "[MPQ]   + patch: " + patch.string());
  }

  vfs->Mount({
      .id = "mpq:" + ToLowerAscii(archive_name),
      .kind = openwow::vfs::MountKind::kMpqArchive,
      .source_root = *archive_path,
      .mpq_patches = patch_chain,
      .priority = priority,
      .enabled = true,
  });
  return true;
}

}

bool BackupLegacyGlueFilesystemOverrides(const std::string& game_data_root) {
  bool attempted = false;
  for (const auto& root :
       CollectLoginFilesystemRoots(std::filesystem::path(game_data_root))) {
    attempted = BackupNamedDirectoryIfPresent(root, "Interface/GlueXML") || attempted;
    attempted = BackupNamedDirectoryIfPresent(root, "Interface/FrameXML") || attempted;
    attempted = BackupLegacyBlizzardAddonsInRoot(root) || attempted;
  }
  if (attempted) {
    openwow::vfs::InvalidateSFileLooseManifestCache();
  }
  return attempted;
}

std::string DetectLocale(const std::string& game_data_root,
                         const std::string& preferred_locale) {
  namespace fs = std::filesystem;

  if (!preferred_locale.empty() && IsKnownLocale(preferred_locale)) {
    Log(LogLevel::kInfo, "[Locale] Using configured locale: " + preferred_locale);
    return preferred_locale;
  }

  const auto data_dir = ResolveDataDir(fs::path(game_data_root));
  if (data_dir.empty() || !fs::is_directory(data_dir)) {
    Log(LogLevel::kWarn, "[Locale] Cannot resolve Data directory from: " + game_data_root);
    return {};
  }

  for (const char* loc : GetStartupLocaleRing()) {
    auto probe = data_dir / loc / ("locale-" + std::string(loc) + ".MPQ");
    if (openwow::platform::filesystem::PathIsRegularFile(probe)) {
      Log(LogLevel::kInfo, "[Locale] Detected locale: " + std::string(loc)
                               + " (found " + probe.string() + ")");
      return std::string(loc);
    }
  }

  Log(LogLevel::kWarn, "[Locale] No locale-{L}.MPQ found under " + data_dir.string());
  return {};
}

openwow::vfs::VirtualFileSystem BuildLoginVfs(const std::string& game_data_root,
                                              const std::string& enhanced_assets_root,
                                              const std::string& locale,
                                              const std::string& retail_install_root) {
  return BuildLoginVfs(game_data_root, {}, enhanced_assets_root, locale, retail_install_root);
}

openwow::vfs::VirtualFileSystem BuildLoginVfs(const std::string& game_data_root,
                                              MountProgressFn progress,
                                              const std::string& enhanced_assets_root,
                                              const std::string& locale,
                                              const std::string& retail_install_root) {
  namespace fs = std::filesystem;
  openwow::vfs::VirtualFileSystem vfs;

  const auto game_root = ResolveStartupClientRoot(fs::path(game_data_root));
  if (!game_root.empty()) {
    const auto resolved_retail_root =
        ResolveRetailInstallRoot(fs::path(retail_install_root));

    vfs.Mount({
        .id = "base-game-data",
        .kind = openwow::vfs::MountKind::kFilesystem,
        .source_root = game_root,
        .priority = 100,
        .enabled = true,
    });

    const auto extracted_data_dir = ResolveConfiguredArchiveDataDir(game_root);
    if (!extracted_data_dir.empty() && fs::is_directory(extracted_data_dir)) {
      vfs.Mount({
          .id = "base-game-data-data-subdir",
          .kind = openwow::vfs::MountKind::kFilesystem,
          .source_root = extracted_data_dir,
          .priority = 90,
          .enabled = true,
      });

      if (IsDataDirectory(game_root)) {
        const auto parent = game_root.parent_path();
        if (!parent.empty() && fs::is_directory(parent)) {
          vfs.Mount({
              .id = "base-game-data-parent",
              .kind = openwow::vfs::MountKind::kFilesystem,
              .source_root = parent,
              .priority = 80,
              .enabled = true,
            });
        }
      }
    }

    const bool has_common_archive =
        HasCommonArchiveLayout(game_root, resolved_retail_root);
    const std::string locale_token =
        has_common_archive
            ? DetectLocaleRing(NormalizeConfiguredLocale(locale),
                               game_root.string(),
                               resolved_retail_root.string())
            : kSplitLayoutLocaleToken;
    const auto patch_chain =
        BuildStartupPatchChain(ResolveDataDir(game_root), locale_token);

    int total = 1;
    total += has_common_archive ? static_cast<int>(kCommonArchiveTable.size())
                                : static_cast<int>(kSplitArchiveTable.size());
    int cur = 0;

    auto fire_progress = [&](const std::string& name) {
      if (progress) progress(name, ++cur, total);
    };

    int priority = 200;
    fire_progress(kAlternateArchiveName);
    TryMountArchiveByName(
        &vfs, game_root, resolved_retail_root, kAlternateArchiveName, patch_chain, priority++);
    if (has_common_archive) {
      for (const char* archive_name : kCommonArchiveTable) {
        const std::string expanded =
            ReplaceArchiveLocalePlaceholdersExact(archive_name,
                                                  locale_token.c_str());
        fire_progress(expanded);
        TryMountArchiveByName(
            &vfs, game_root, resolved_retail_root, expanded, patch_chain, priority++);
      }
    } else {
      for (const char* archive_name : kSplitArchiveTable) {
        fire_progress(archive_name);
        TryMountArchiveByName(
            &vfs, game_root, resolved_retail_root, archive_name, patch_chain, priority++);
      }
    }
  }

  if (!enhanced_assets_root.empty()) {
    vfs.Mount({
        .id = "enhanced-override",
        .kind = openwow::vfs::MountKind::kEnhancedOverride,
        .source_root = enhanced_assets_root,
        .priority = 1000,
        .enabled = true,
    });
  }

  vfs.PrewarmMpqArchives();

  vfs.PrewarmFileEnumeration("/Interface/AddOns", true);

  return vfs;
}

std::uint8_t DetermineStartupExpansionLevel(
    const openwow::vfs::VirtualFileSystem& vfs) {

  if (vfs.CanOpenMpqMount("mpq:lichking.mpq")) {
    return 2;
  }
  if (vfs.CanOpenMpqMount("mpq:expansion.mpq")) {
    return 1;
  }
  return 0;
}

LoginResourceValidationResult ValidateLoginResources(const openwow::vfs::VirtualFileSystem& vfs) {
  LoginResourceValidationResult result;

  if (!AnyExists(vfs,
                 {
                     "/Interface/GlueXML/GlueParent.xml",
                     "/Interface/GlueXML/AccountLogin.xml",
                     "/Interface/GlueXML/RealmList.xml",
                     "/Interface/GlueXML/CharacterSelect.xml",
                 })) {
    result.missing_paths.push_back("/Interface/GlueXML/*.xml");
  }

  if (!AnyExists(vfs,
                 {
                     "/Interface/GlueXML/GlueParent.lua",
                     "/Interface/GlueXML/GlueStrings.lua",
                 })) {
    result.missing_paths.push_back("/Interface/GlueXML/*.lua");
  }

  result.ok = result.missing_paths.empty();
  return result;
}

LoginResourceValidationResult ValidateLoginResources(const std::string& game_data_root) {
  const auto vfs = BuildLoginVfs(game_data_root);
  return ValidateLoginResources(vfs);
}

std::string DetectLocaleRing(const std::string& preferred_locale,
                             const std::string& game_data_root,
                             const std::string& retail_install_root) {
  namespace fs = std::filesystem;
  const auto game_root = ResolveStartupClientRoot(fs::path(game_data_root));
  const auto resolved_retail_root =
      ResolveRetailInstallRoot(fs::path(retail_install_root));
  const auto& locale_ring = GetStartupLocaleRing();
  const std::string normalized_preferred =
      NormalizeStartupProbeLocale(preferred_locale);

  if (!HasCommonArchiveLayout(game_root, resolved_retail_root)) {
    StartupLocaleAvailability all_available{};
    all_available.fill(true);
    SetStartupLocaleAvailability(all_available);
    return normalized_preferred;
  }

  StartupLocaleAvailability available{};
  for (std::size_t i = 0; i < locale_ring.size(); ++i) {
    const char* lc = locale_ring[i];
    for (int root_index = 0; root_index < 3; ++root_index) {
      std::filesystem::path probe;
      if (BuildArchiveProbePath(root_index,
                                game_root,
                                resolved_retail_root,
                                "****\\locale-****.MPQ",
                                lc,
                                &probe) &&
          openwow::platform::filesystem::PathIsRegularFile(probe)) {
        available[i] = true;
        break;
      }
    }
  }
  SetStartupLocaleAvailability(available);

  const int start =
      FindStartupLocaleRingIndexOrEnUSFallback(normalized_preferred);

  for (std::size_t k = 0; k < locale_ring.size(); ++k) {
    const std::size_t idx =
        (static_cast<std::size_t>(start) + k) % locale_ring.size();
    if (available[idx]) {
      Log(LogLevel::kInfo,
          "[Locale] Ring-detected locale: " + std::string(locale_ring[idx]));
      return std::string(locale_ring[idx]);
    }
  }

  Log(LogLevel::kWarn,
      "[Locale] No locale-{L}.MPQ found in any ring slot; falling back to enUS.");
  return "enUS";
}

}

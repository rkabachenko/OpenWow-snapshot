#pragma once

#include <array>
#include <filesystem>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::data {

struct StartupFileSystemState {

  bool base_path_init_flag{true};

  bool force_filesystem_path_resolution{false};
  std::uint32_t storm_open_flags{0};
  std::uint32_t forwarded_storm_open_flags{0};
  std::uint32_t client_init_archive_gate{0};
  std::string executable_base_path;
  std::string archive_data_path;
  std::string locale_data_path;
  std::string retail_install_path_cache;
};

struct InitFileSystemInputs {
  std::string command_line_base_path;
  std::string module_directory;
  std::string archive_data_path{"Data"};
};

struct InitFileSystemResult {
  std::string selected_base_path;
  bool working_directory_change_result{false};
};

struct ArchiveProbeNativeRoots {
  std::filesystem::path data_root;
  std::filesystem::path parent_data_root;
  std::filesystem::path retail_data_root;
};

inline constexpr std::size_t kStartupLocaleRingSize = 12;
using StartupLocaleAvailability = std::array<bool, kStartupLocaleRingSize>;

void ClearStartupBasePathInitFlag();

void SetStartupStormOpenFlags(std::uint32_t flags);

void SetStartupExecutableBasePath(const std::string& path);

void SetStartupArchiveDataPath(const std::string& path);

void SetStartupLocaleDataPath(const std::string& path);

const std::string& GetCachedStartupWorkingDirectory();

std::string ResolveStartupRetailInstallPath(
    bool online_mode,
    bool ptr_product,
    const std::function<std::string(const std::string& company_key,
                                    const std::string& value_name)>& registry_read);

std::string ReplaceArchiveLocalePlaceholdersExact(
    std::string_view template_path, const char* locale_token);
std::optional<std::string> BuildArchiveProbePathExact(
    int root_index,
    std::string_view suffix_template,
    const char* locale_token,
    std::string_view retail_install_path);
std::optional<std::filesystem::path> BuildArchiveProbePathNative(
    int root_index,
    const ArchiveProbeNativeRoots& roots,
    std::string_view suffix_template,
    const char* locale_token);

bool ProbeCommonArchiveLayout(
    const std::filesystem::path& game_root = {},
    const std::filesystem::path& retail_install_root = {});

InitFileSystemResult InitializeStartupFileSystem(
    const InitFileSystemInputs& inputs,
    const std::function<bool(const std::string& base_path)>&
        change_working_directory = {});

void SetStartupRetailInstallPathCache(const std::string& path);

const StartupFileSystemState& GetStartupFileSystemState();

const std::array<const char*, kStartupLocaleRingSize>& GetStartupLocaleRing();
int FindStartupLocaleRingIndexOrEnUSFallback(std::string_view locale_code);
const StartupLocaleAvailability& GetStartupLocaleAvailability();
void SetStartupLocaleAvailability(const StartupLocaleAvailability& availability);

bool IsWoWTProduct(
    const std::function<std::optional<std::string>()>& blizzard_component_reader = {});

void SetStartupStormBasePathSinkForTests(
    std::function<void(const std::string& path)> sink);

void ResetCachedStartupWorkingDirectoryForTests();

void ResetStartupFileSystemStateForTests(const StartupFileSystemState& state = {});

}

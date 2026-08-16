#pragma once

#include "openwow/vfs/virtual_file_system.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openwow::data {

struct LoginResourceValidationResult {
  bool ok{false};
  std::vector<std::string> missing_paths;
};

std::string DetectLocale(const std::string& game_data_root,
                         const std::string& preferred_locale = "");

std::string DetectLocaleRing(const std::string& preferred_locale,
                             const std::string& game_data_root,
                             const std::string& retail_install_root = "");

bool BackupLegacyGlueFilesystemOverrides(const std::string& game_data_root);

openwow::vfs::VirtualFileSystem BuildLoginVfs(const std::string& game_data_root,
                                              const std::string& enhanced_assets_root = "",
                                              const std::string& locale = "",
                                              const std::string& retail_install_root = "");

using MountProgressFn = std::function<void(const std::string&, int, int)>;

openwow::vfs::VirtualFileSystem BuildLoginVfs(const std::string& game_data_root,
                                              MountProgressFn progress,
                                              const std::string& enhanced_assets_root = "",
                                              const std::string& locale = "",
                                              const std::string& retail_install_root = "");

std::uint8_t DetermineStartupExpansionLevel(
    const openwow::vfs::VirtualFileSystem& vfs);

LoginResourceValidationResult ValidateLoginResources(const openwow::vfs::VirtualFileSystem& vfs);
LoginResourceValidationResult ValidateLoginResources(const std::string& game_data_root);

}

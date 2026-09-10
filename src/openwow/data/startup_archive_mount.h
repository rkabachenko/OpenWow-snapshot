#pragma once

#include "openwow/data/startup_filesystem_state.h"
#include "openwow/vfs/virtual_file_system.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace openwow::data {

struct StartupArchiveTableEntry {
  const char* name;
  std::uint32_t type;
  std::uint32_t layout_mask;
};

inline constexpr std::size_t kStartupArchiveTableSize = 28;

inline constexpr std::uint32_t kArchiveTypeSplitLayout = 1;
inline constexpr std::uint32_t kArchiveTypeOptional = 2;
inline constexpr std::uint32_t kArchiveTypeAlternate = 3;
inline constexpr std::uint32_t kArchiveTypeStreaming = 4;

inline constexpr std::uint32_t kArchiveLayoutFlagsSplit = 1;
inline constexpr std::uint32_t kArchiveLayoutFlagsCommon = 2;

inline constexpr std::array<StartupArchiveTableEntry, kStartupArchiveTableSize>
    kStartupArchiveTable = {{
        {"alternate.MPQ", 3, 3},
        {"interface.MPQ", 1, 1},
        {"misc.MPQ", 1, 1},
        {"model.MPQ", 1, 1},
        {"texture.MPQ", 1, 1},
        {"terrain.MPQ", 1, 1},
        {"wmo.MPQ", 1, 1},
        {"sound.MPQ", 1, 1},
        {"fonts.MPQ", 1, 1},
        {"dbc.MPQ", 1, 1},
        {"speech.MPQ", 1, 1},
        {"expansionloc.MPQ", 2, 1},
        {"lichkingloc.MPQ", 2, 1},
        {"expansionspeech.MPQ", 2, 1},
        {"lichkingspeech.MPQ", 2, 1},
        {"expansion.MPQ", 2, 3},
        {"lichking.MPQ", 2, 3},
        {"common.MPQ", 1, 2},
        {"common-2.MPQ", 2, 2},
        {"****\\locale-****.MPQ", 1, 2},
        {"****\\speech-****.MPQ", 1, 2},
        {"****\\expansion-locale-****.MPQ", 2, 2},
        {"****\\lichking-locale-****.MPQ", 2, 2},
        {"****\\expansion-speech-****.MPQ", 2, 2},
        {"****\\lichking-speech-****.MPQ", 2, 2},
        {"development.MPQ", 2, 1},
        {"streaming.MPQ", 4, 1},
        {"streamingloc.MPQ", 4, 1},
    }};

inline constexpr int kStockBaseArchiveTopPriority = 0x3F;
inline constexpr int kStockPatchArchiveBasePriority = 0x40;

inline constexpr int kStormArchivePriorityBias = 200;

inline constexpr int kLooseClientRootPriority = 300;
inline constexpr int kLooseDataDirectoryPriority = 290;
inline constexpr int kLooseParentRootPriority = 280;
static_assert(kLooseParentRootPriority >
                  kStormArchivePriorityBias + kStockPatchArchiveBasePriority,
              "the loose band must outrank every archive, as stock's resolver does");

inline constexpr int kEnhancedOverridePriority = 1000;
inline constexpr int kRuntimeOverridePriority = 900;

using MountProgressFn = std::function<void(const std::string&, int, int)>;

std::filesystem::path ResolveStartupClientRoot(
    const std::filesystem::path& game_root);

std::filesystem::path ResolveRetailInstallRoot(
    const std::filesystem::path& retail_install_root);

bool IsDataDirectory(const std::filesystem::path& path);

std::filesystem::path ResolveDataDir(const std::filesystem::path& game_root);

std::filesystem::path ResolveExistingDataDir(
    const std::filesystem::path& game_root);

ArchiveProbeNativeRoots ResolveArchiveProbeNativeRoots(
    const std::filesystem::path& game_root,
    const std::filesystem::path& retail_install_root);

bool HasCommonArchiveLayout(const std::filesystem::path& game_root,
                            const std::filesystem::path& retail_install_root);

std::vector<std::filesystem::path> BuildStartupPatchChain(
    const std::filesystem::path& data_dir, const std::string& locale_token);

struct StartupArchiveMountOptions {
  std::filesystem::path game_root;
  std::filesystem::path retail_install_root;

  std::string locale_token;

  std::uint32_t layout_flags{kArchiveLayoutFlagsSplit};

  bool online_mode{false};

  bool streaming_ready{false};
  MountProgressFn progress;
};

void MountStartupArchives(openwow::vfs::VirtualFileSystem* vfs,
                          const StartupArchiveMountOptions& options);

}

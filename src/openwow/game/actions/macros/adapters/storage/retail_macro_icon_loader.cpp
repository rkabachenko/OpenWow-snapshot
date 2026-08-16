#include "openwow/game/actions/macros/application/macro_catalog.h"

#include "openwow/data/archive_system.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/game/actions/macros/rules/retail_macro_icon_catalog.h"
#include "openwow/vfs/sfile_core.h"

#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openwow::game {
namespace {

using actions::macros::rules::MacroIconFileEntry;
using actions::macros::rules::MacroIconFileAttributes;
using actions::macros::rules::RetailMacroIconSources;

std::vector<std::string> ReadArchiveListfile(void* archive) {
  std::vector<std::string> entries;
  if (archive == nullptr) {
    return entries;
  }
  openwow::vfs::SFileEnumListfile(
      archive,
      [&](const char* filename, int) {
        if (filename != nullptr) {
          entries.emplace_back(filename);
        }
        return true;
      },
      0);
  return entries;
}

void AppendArchiveListfile(void* archive,
                           std::vector<std::string>& destination) {
  auto entries = ReadArchiveListfile(archive);
  destination.insert(
      destination.end(),
      std::make_move_iterator(entries.begin()),
      std::make_move_iterator(entries.end()));
}

void CaptureArchiveSources(RetailMacroIconSources& sources) {
  const auto* slots = openwow::data::GetArchiveSlots();
  const auto count = openwow::data::GetArchiveHandleCount();
  const auto base = openwow::data::GetArchiveTableBaseIndex();
  if (slots == nullptr || base > count) {
    return;
  }

  for (std::size_t index = 0; index < base && index < count; ++index) {
    AppendArchiveListfile(
        slots[index], sources.patch_archive_entries);
  }

  if (base + 1u < count && slots[base + 1u] != nullptr) {
    sources.preferred_interface_archive_entries =
        ReadArchiveListfile(slots[base + 1u]);
  } else if (base + 19u < count && slots[base + 19u] != nullptr) {
    sources.locale_archive_entries =
        ReadArchiveListfile(slots[base + 19u]);
  }
}

std::string IconDirectoryPath(const std::string_view base_path) {
  std::string path(base_path);
  if (!path.empty() && path.back() != '\\' && path.back() != '/') {
    path.push_back('\\');
  }
  path.append("Interface\\Icons\\");
  return path;
}

std::vector<MacroIconFileEntry> ReadDirectory(
    const std::string& path) {
  std::vector<MacroIconFileEntry> entries;
  if (path.empty()) {
    return entries;
  }
  openwow::vfs::SFileFindFiles(
      path.c_str(), "*",
      [&](const openwow::vfs::SFileFindData& entry) {
        if (entry.entry_name != nullptr) {
          entries.push_back({
              .name = entry.entry_name,
              .attributes =
                  static_cast<MacroIconFileAttributes>(
                      entry.file_attributes),
          });
        }
        return false;
      },
      false);
  return entries;
}

RetailMacroIconSources CaptureRetailMacroIconSources() {
  RetailMacroIconSources sources;
  CaptureArchiveSources(sources);

  const auto& startup = openwow::data::GetStartupFileSystemState();
  if (!startup.archive_data_path.empty()) {
    sources.compressed_data_entries =
        ReadDirectory(IconDirectoryPath(startup.archive_data_path));
  }
  sources.plain_interface_entries =
      ReadDirectory("Interface\\Icons\\");
  return sources;
}

}

void MacroCatalog::LoadIconList() {
  icon_library_.LoadIfNeeded(
      [](std::vector<std::string>& macro_icons,
         std::vector<std::string>& item_icons) {
        auto catalog =
            actions::macros::rules::BuildRetailMacroIconCatalog(
                CaptureRetailMacroIconSources());
        macro_icons = std::move(catalog.macro_icons);
        item_icons = std::move(catalog.item_icons);
      });
}

}

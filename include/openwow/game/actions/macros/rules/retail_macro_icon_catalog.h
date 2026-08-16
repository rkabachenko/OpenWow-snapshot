#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game::actions::macros::rules {

enum class MacroIconFileAttributes : std::uint32_t {
  kNone = 0u,
  kHidden = 0x02u,
  kDirectory = 0x10u,
};

struct MacroIconFileEntry {
  std::string name;
  MacroIconFileAttributes attributes{MacroIconFileAttributes::kNone};
};

struct RetailMacroIconSources {
  std::vector<std::string> patch_archive_entries;
  std::optional<std::vector<std::string>>
      preferred_interface_archive_entries;
  std::optional<std::vector<std::string>> locale_archive_entries;
  std::vector<MacroIconFileEntry> compressed_data_entries;
  std::vector<MacroIconFileEntry> plain_interface_entries;
};

struct RetailMacroIconCatalog {
  std::vector<std::string> macro_icons;
  std::vector<std::string> item_icons;
};

[[nodiscard]] RetailMacroIconCatalog BuildRetailMacroIconCatalog(
    const RetailMacroIconSources& sources);

}

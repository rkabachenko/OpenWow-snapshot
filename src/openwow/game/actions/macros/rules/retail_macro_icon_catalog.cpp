#include "openwow/game/actions/macros/rules/retail_macro_icon_catalog.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace openwow::game::actions::macros::rules {
namespace {

constexpr std::string_view kDefaultMacroIcon = "INV_Misc_QuestionMark";
constexpr std::string_view kInterfaceIconsPath = "Interface\\Icons\\";
constexpr std::string_view kAbilityIconPrefix = "Ability_";
constexpr std::string_view kSpellIconPrefix = "Spell_";
constexpr std::string_view kItemIconPrefix = "INV_";

bool HasAttribute(const MacroIconFileAttributes attributes,
                  const MacroIconFileAttributes expected) {
  return (static_cast<std::uint32_t>(attributes) &
          static_cast<std::uint32_t>(expected)) != 0u;
}

bool EqualsNoCase(const std::string_view lhs,
                  const std::string_view rhs) {
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

bool StartsWithNoCase(const std::string_view value,
                      const std::string_view prefix) {
  return value.size() >= prefix.size() &&
         EqualsNoCase(value.substr(0, prefix.size()), prefix);
}

void AppendTextureStem(std::vector<std::string>& destination,
                       const std::string_view name) {
  const auto extension_offset = name.find('.');
  if (extension_offset == std::string_view::npos) {
    return;
  }
  const auto extension = name.substr(extension_offset);
  if (EqualsNoCase(extension, ".blp") ||
      EqualsNoCase(extension, ".tga")) {
    destination.emplace_back(name.substr(0, extension_offset));
  }
}

std::string_view RemoveCompressedSuffixes(std::string_view name) {
  constexpr std::string_view kCompressedSuffix = ".bz";
  while (name.size() >= kCompressedSuffix.size() &&
         EqualsNoCase(
             name.substr(name.size() - kCompressedSuffix.size()),
             kCompressedSuffix)) {
    name.remove_suffix(kCompressedSuffix.size());
  }
  return name;
}

void AppendPrefixedTexture(RetailMacroIconCatalog& catalog,
                           const std::string_view leaf_name) {
  if (StartsWithNoCase(leaf_name, kAbilityIconPrefix) ||
      StartsWithNoCase(leaf_name, kSpellIconPrefix)) {
    AppendTextureStem(catalog.macro_icons, leaf_name);
  }
  if (StartsWithNoCase(leaf_name, kItemIconPrefix)) {
    AppendTextureStem(catalog.item_icons, leaf_name);
  }
}

void AppendArchiveEntries(RetailMacroIconCatalog& catalog,
                          const std::vector<std::string>& paths) {
  for (const auto& path : paths) {
    if (StartsWithNoCase(path, kInterfaceIconsPath)) {
      AppendPrefixedTexture(
          catalog, std::string_view(path).substr(kInterfaceIconsPath.size()));
    }
  }
}

bool IconNameLess(const std::string& lhs, const std::string& rhs) {
  const bool lhs_is_placeholder = EqualsNoCase(lhs, kDefaultMacroIcon);
  const bool rhs_is_placeholder = EqualsNoCase(rhs, kDefaultMacroIcon);
  if (lhs_is_placeholder != rhs_is_placeholder) {
    return lhs_is_placeholder;
  }
  return std::lexicographical_compare(
      lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
      [](const char left, const char right) {
        return std::tolower(static_cast<unsigned char>(left)) <
               std::tolower(static_cast<unsigned char>(right));
      });
}

void SortAndDeduplicate(std::vector<std::string>& names) {
  std::stable_sort(names.begin(), names.end(), IconNameLess);
  names.erase(
      std::unique(names.begin(), names.end(),
                  [](const std::string& lhs, const std::string& rhs) {
                    return EqualsNoCase(lhs, rhs);
                  }),
      names.end());
}

}

RetailMacroIconCatalog BuildRetailMacroIconCatalog(
    const RetailMacroIconSources& sources) {
  RetailMacroIconCatalog catalog{
      .macro_icons = {std::string(kDefaultMacroIcon)},
      .item_icons = {std::string(kDefaultMacroIcon)},
  };

  AppendArchiveEntries(catalog, sources.patch_archive_entries);
  if (sources.preferred_interface_archive_entries) {
    AppendArchiveEntries(
        catalog, *sources.preferred_interface_archive_entries);
  } else if (sources.locale_archive_entries) {
    AppendArchiveEntries(catalog, *sources.locale_archive_entries);
  }

  for (const auto& entry : sources.compressed_data_entries) {
    if (!HasAttribute(
            entry.attributes, MacroIconFileAttributes::kDirectory)) {
      AppendPrefixedTexture(catalog, RemoveCompressedSuffixes(entry.name));
    }
  }
  for (const auto& entry : sources.plain_interface_entries) {
    if (!HasAttribute(
            entry.attributes, MacroIconFileAttributes::kDirectory)) {
      AppendTextureStem(catalog.macro_icons, entry.name);
      AppendTextureStem(catalog.item_icons, entry.name);
    }
  }

  SortAndDeduplicate(catalog.macro_icons);
  SortAndDeduplicate(catalog.item_icons);
  return catalog;
}

}

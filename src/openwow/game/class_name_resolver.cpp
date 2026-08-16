#include "openwow/game/class_name_resolver.h"

namespace openwow::game {

ClassNameResolution ResolveLocalizedClassName(
    const data::dbc::DbcStore<data::dbc::ChrClassesEntry> &chr_classes,
    const std::uint8_t class_id,
    const std::uint32_t sex) {
  const auto *entry = chr_classes.LookupEntry(class_id);
  if (entry == nullptr) {

    return ClassNameResolution{.name = {}, .resolved_sex = sex};
  }

  return ClassNameResolution{
      .name = entry->DisplayNameForSex(sex),
      .resolved_sex = entry->ResolveDisplaySex(sex),
  };
}

ClassNameResolution GetUnitClassDisplayName(
    const data::dbc::DbcStore<data::dbc::ChrClassesEntry> &chr_classes,
    const std::string_view unit_name,
    const bool is_player,
    const std::uint8_t class_id,
    const std::uint32_t sex) {

  if (!is_player) {
    return ClassNameResolution{.name = unit_name, .resolved_sex = sex};
  }

  return ResolveLocalizedClassName(chr_classes, class_id, sex);
}

}

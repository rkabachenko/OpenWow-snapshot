#pragma once

#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

#include <cstdint>
#include <string_view>

namespace openwow::game {

struct ClassNameResolution {

  std::string_view name;

  std::uint32_t resolved_sex{2};
};

[[nodiscard]] ClassNameResolution ResolveLocalizedClassName(
    const data::dbc::DbcStore<data::dbc::ChrClassesEntry> &chr_classes,
    std::uint8_t class_id,
    std::uint32_t sex);

[[nodiscard]] ClassNameResolution GetUnitClassDisplayName(
    const data::dbc::DbcStore<data::dbc::ChrClassesEntry> &chr_classes,
    std::string_view unit_name,
    bool is_player,
    std::uint8_t class_id,
    std::uint32_t sex);

}

#pragma once

#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"

#include <cstdint>

namespace openwow::data::dbc {

[[nodiscard]] inline bool IsAreaFlaggedSnow(const DbcLoader& dbc,
                                             const std::uint32_t area_id) {
  if (area_id == 0u) {
    return false;
  }
  const auto* const area = dbc.area_table().LookupEntry(area_id);
  if (area == nullptr) {
    return false;
  }

  std::uint32_t flags = area->flags;
  if ((flags & AreaFlags::kAreaFlagUNK1) == 0u) {
    if (const auto* const parent = dbc.area_table().LookupEntry(area->parent_area);
        parent != nullptr) {
      flags = parent->flags;
    }
  }
  return (flags & AreaFlags::kAreaFlagSnow) != 0u;
}

}

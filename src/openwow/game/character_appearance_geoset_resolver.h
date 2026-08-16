#pragma once

#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_store.h"

#include <array>
#include <cstdint>

namespace openwow::game {

struct ResolvedFacialHairGeosets {
  bool found = false;
  std::array<std::uint32_t, 5> raw_geoset_values{};
  std::uint32_t group100 = 0;
  std::uint32_t group300 = 0;
  std::uint32_t group200 = 0;
  std::uint32_t group1600 = 0;
  std::uint32_t group1700 = 0;
  std::uint32_t accessory702 = 0;

  [[nodiscard]] std::array<std::uint32_t, 5> OrderedGeosets() const {
    return {group100, group300, group200, group1600, group1700};
  }
};

inline std::uint32_t ResolveHairGeosetId(
    const std::uint32_t race_id, const std::uint32_t sex_id, const std::uint32_t variation_id,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharHairGeosetsEntry>* hair_geosets) {
  if (hair_geosets == nullptr) {
    return 1;
  }

  for (const auto& entry : hair_geosets->entries()) {
    if (entry.race_id == race_id && entry.sex_id == sex_id && entry.variation_id == variation_id) {
      return entry.geoset_id > 0 ? entry.geoset_id : 1;
    }
  }

  return 1;
}

inline ResolvedFacialHairGeosets ResolveFacialHairGeosets(
    const std::uint32_t race_id, const std::uint32_t sex_id, const std::uint32_t variation_id,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry>*
        facial_hair_styles) {
  ResolvedFacialHairGeosets resolved{};
  if (facial_hair_styles == nullptr) {
    return resolved;
  }

  for (const auto& entry : facial_hair_styles->entries()) {
    if (entry.race_id != race_id || entry.sex_id != sex_id || entry.variation_id != variation_id) {
      continue;
    }

    resolved.found = true;
    resolved.raw_geoset_values = entry.geoset;
    resolved.group100 = 100u + entry.geoset[0];
    resolved.group300 = 300u + entry.geoset[1];
    resolved.group200 = 200u + entry.geoset[2];
    resolved.group1600 = 1600u + entry.geoset[3];
    resolved.group1700 = 1700u + entry.geoset[4];
    resolved.accessory702 = 702u;
    return resolved;
  }

  return resolved;
}

}

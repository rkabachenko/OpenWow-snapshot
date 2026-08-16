#pragma once

#include "openwow/render/models/characters/character_appearance_texture_sources.h"

#include <array>
#include <cstdint>

namespace openwow::data::dbc {
template <typename T> class DbcStore;
struct CharHairGeosetsEntry;
struct CharacterFacialHairStylesEntry;
struct CharSectionsEntry;
struct HelmetGeosetVisDataEntry;
struct ItemDisplayInfoEntry;
}

namespace openwow::render {

struct CharacterAppearanceGeosetStores {
  const openwow::data::dbc::DbcStore<
      openwow::data::dbc::CharHairGeosetsEntry> *hair_geosets{nullptr};
  const openwow::data::dbc::DbcStore<
      openwow::data::dbc::CharacterFacialHairStylesEntry>
      *facial_hair_styles{nullptr};
  const openwow::data::dbc::DbcStore<
      openwow::data::dbc::ItemDisplayInfoEntry> *item_display_info{nullptr};
  const openwow::data::dbc::DbcStore<
      openwow::data::dbc::HelmetGeosetVisDataEntry>
      *helmet_geoset_vis_data{nullptr};
  const openwow::data::dbc::DbcStore<
      openwow::data::dbc::CharSectionsEntry> *char_sections{nullptr};
};

struct CharacterAppearanceGeosetState {
  std::uint16_t hair{1u};

  std::array<std::uint16_t, 5> facial_hair{101u, 301u, 201u, 1601u, 1701u};
  std::uint16_t facial_accessory{702u};
  bool force_1700_family_1703{false};

  std::uint16_t glove{0u};
  bool controls_glove{false};
  std::uint16_t body_800{0u};
  bool controls_body_800{false};
  std::uint16_t boot{0u};
  bool controls_boot{false};
  std::uint16_t secondary_lower{0u};
  bool controls_secondary_lower{false};
  std::uint16_t torso_upper{0u};
  bool controls_torso_upper{false};
  std::uint16_t leg{0u};
  bool controls_leg{false};
  std::uint16_t tabard{0u};
  bool controls_tabard{false};
  std::uint16_t robe{0u};
  bool controls_robe{false};
  std::uint16_t cape{0u};
  bool controls_cape{false};
  std::uint16_t belt{0u};
  bool controls_belt{false};

  [[nodiscard]] bool IsVisible(std::uint16_t section_id) const noexcept;

  [[nodiscard]] bool operator==(
      const CharacterAppearanceGeosetState &) const noexcept = default;
};

[[nodiscard]] bool IsCreatureGeosetSectionVisible(
    std::uint16_t section_id, std::uint32_t creature_geoset_data) noexcept;

[[nodiscard]] CharacterAppearanceGeosetState
BuildCharacterAppearanceGeosetState(
    const CharacterAppearanceSelection &selection,
    const CharacterAppearanceGeosetStores &stores);

}

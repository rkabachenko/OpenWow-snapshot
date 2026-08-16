#pragma once

#include "openwow/game/guild_manager.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace openwow::data::dbc {
class DbcLoader;
template <typename T> class DbcStore;
struct CharSectionsEntry;
struct ChrRacesEntry;
struct ItemDisplayInfoEntry;
}

namespace openwow::render {

struct CharacterAppearanceTextureSources {
  std::string base_skin;
  std::string face_lower;
  std::string face_upper;
  std::string facial_hair_lower;
  std::string facial_hair_upper;
  std::string hair;
  std::string scalp_lower;
  std::string scalp_upper;
  std::string underwear_pelvis;
  std::string underwear_torso;
  std::string extra_skin;
  std::string cape;
  std::uint8_t gender{0};
  std::array<std::uint32_t, 19> equipment_display_ids{};
  std::optional<openwow::game::GuildEmblem> guild_tabard_emblem;

  [[nodiscard]] bool HasBody() const noexcept { return !base_skin.empty(); }
};

struct CharacterAppearanceSelection {
  std::uint8_t race{0};
  std::uint8_t gender{0};

  std::uint8_t class_id{0};
  std::uint8_t skin_color{0};
  std::uint8_t face{0};
  std::uint8_t hair_style{0};
  std::uint8_t hair_color{0};
  std::uint8_t facial_hair{0};
  std::array<std::uint32_t, 19> equipment_display_ids{};

  [[nodiscard]] bool operator==(
      const CharacterAppearanceSelection&) const noexcept = default;
};

[[nodiscard]] CharacterAppearanceTextureSources
BuildCharacterAppearanceTextureSources(
    const CharacterAppearanceSelection &selection,
    const openwow::data::dbc::DbcLoader *dbc);

[[nodiscard]] CharacterAppearanceTextureSources
BuildCharacterAppearanceTextureSources(
    const CharacterAppearanceSelection &selection,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry>
        *char_sections,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemDisplayInfoEntry>
        *item_display_info,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ChrRacesEntry>
        *chr_races);

}

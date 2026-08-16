#include "openwow/render/models/characters/character_appearance_geosets.h"

#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/character_appearance_geoset_resolver.h"
#include "openwow/render/models/characters/equipment_renderer.h"

#include <cstddef>

namespace openwow::render {
namespace {

constexpr std::uint16_t kDefaultGlove = 0x191u;
constexpr std::uint16_t kDefaultBoot = 0x1F5u;
constexpr std::uint16_t kDefaultBody600 = 0x259u;
constexpr std::uint16_t kDefaultBody800 = 0x321u;
constexpr std::uint16_t kDefaultSecondaryLower = 0x385u;
constexpr std::uint16_t kDefaultTorsoUpper = 0x3E9u;
constexpr std::uint16_t kDefaultLeg = 0x44Du;
constexpr std::uint16_t kDefaultTabard = 0x4B1u;
constexpr std::uint16_t kDefaultRobe = 0x515u;
constexpr std::uint16_t kDefaultBody1400 = 0x579u;
constexpr std::uint16_t kDefaultCape = 0x5DDu;
constexpr std::uint16_t kDefaultBelt = 0x709u;

constexpr std::uint8_t kDeathKnightClassId = 6u;
constexpr std::uint16_t kDeathKnightEyeGlowSection = 0x6A7u;
constexpr std::uint32_t kCharSectionsDeathKnightFlag = 0x4u;

bool IsControlledFamily(const std::uint16_t section_id,
                        const std::uint16_t selected,
                        const bool controlled,
                        const std::uint16_t stock_default) noexcept {
  return section_id == (controlled ? selected : stock_default);
}

std::uint32_t HelmetRaceBit(const std::uint8_t race) noexcept {
  return race < 32u ? (std::uint32_t{1} << race) : 0u;
}

const openwow::data::dbc::CharSectionsEntry *FindSection(
    const CharacterAppearanceSelection &selection,
    const openwow::data::dbc::DbcStore<
        openwow::data::dbc::CharSectionsEntry> *sections,
    const std::uint32_t base_section, const std::uint32_t type,
    const std::uint32_t variation) {
  if (sections == nullptr) {
    return nullptr;
  }
  for (auto it = sections->entries().rbegin();
       it != sections->entries().rend(); ++it) {
    if (it->race_id == selection.race && it->sex_id == selection.gender &&
        it->base_section == base_section && it->type == type &&
        it->variation == variation) {
      return &*it;
    }
  }
  return nullptr;
}

}

bool CharacterAppearanceGeosetState::IsVisible(
    const std::uint16_t section_id) const noexcept {
  if (section_id == 0u) {
    return true;
  }
  if (section_id >= 0x191u && section_id <= 0x1F3u) {
    return IsControlledFamily(section_id, glove, controls_glove,
                              kDefaultGlove);
  }
  if (section_id >= 0x321u && section_id <= 0x383u) {
    return IsControlledFamily(section_id, body_800, controls_body_800,
                              kDefaultBody800);
  }
  if (section_id >= 0x1F5u && section_id <= 0x257u) {
    return IsControlledFamily(section_id, boot, controls_boot, kDefaultBoot);
  }
  if (section_id >= 0x259u && section_id <= 0x2BBu) {
    return section_id == kDefaultBody600;
  }
  if (section_id >= 0x385u && section_id <= 0x3E7u) {
    return IsControlledFamily(section_id, secondary_lower,
                              controls_secondary_lower,
                              kDefaultSecondaryLower);
  }
  if (section_id >= 0x3E9u && section_id <= 0x44Bu) {
    return IsControlledFamily(section_id, torso_upper, controls_torso_upper,
                              kDefaultTorsoUpper);
  }
  if (section_id >= 0x44Du && section_id <= 0x4AFu) {
    return IsControlledFamily(section_id, leg, controls_leg, kDefaultLeg);
  }
  if (section_id >= 0x4B1u && section_id <= 0x513u) {
    return IsControlledFamily(section_id, tabard, controls_tabard,
                              kDefaultTabard);
  }
  if (section_id >= 0x515u && section_id <= 0x577u) {
    return IsControlledFamily(section_id, robe, controls_robe, kDefaultRobe);
  }
  if (section_id >= 0x579u && section_id <= 0x5DBu) {
    return section_id == kDefaultBody1400;
  }
  if (section_id >= 0x5DDu && section_id <= 0x63Fu) {
    return IsControlledFamily(section_id, cape, controls_cape, kDefaultCape);
  }
  if (section_id >= 0x709u && section_id <= 0x76Bu) {
    return IsControlledFamily(section_id, belt, controls_belt, kDefaultBelt);
  }
  if (section_id < 100u) {
    return section_id == hair;
  }
  for (std::size_t index = 0u; index < 4u; ++index) {
    if (facial_hair[index] != 0u && facial_hair[index] == section_id) {
      return true;
    }
  }
  const std::uint16_t selected_1700 =
      force_1700_family_1703 ? kDeathKnightEyeGlowSection : facial_hair[4];
  if (selected_1700 != 0u && selected_1700 == section_id) {
    return true;
  }
  if (facial_accessory != 0u && facial_accessory == section_id) {
    return true;
  }
  return section_id > 2000u;
}

bool IsCreatureGeosetSectionVisible(
    const std::uint16_t section_id,
    const std::uint32_t creature_geoset_data) noexcept {

  if (creature_geoset_data == 0u) {
    return true;
  }

  constexpr std::uint32_t kCreatureGeosetFamilyCount = 8u;
  constexpr std::uint16_t kCreatureGeosetFirstFamilyBase = 100u;
  constexpr std::uint16_t kCreatureGeosetFamilySpan = 100u;
  constexpr std::uint32_t kCreatureGeosetFamilyMask = 0xFu;
  constexpr std::uint32_t kCreatureGeosetFamilyBits = 4u;
  for (std::uint32_t family = 0u; family < kCreatureGeosetFamilyCount; ++family) {
    const auto value = static_cast<std::uint16_t>(
        (creature_geoset_data >> (family * kCreatureGeosetFamilyBits)) &
        kCreatureGeosetFamilyMask);
    if (value == 0u) {
      continue;
    }
    const auto base = static_cast<std::uint16_t>(
        kCreatureGeosetFirstFamilyBase + family * kCreatureGeosetFamilySpan);
    if (section_id < base ||
        section_id >= static_cast<std::uint16_t>(base + kCreatureGeosetFamilySpan)) {
      continue;
    }
    return section_id == static_cast<std::uint16_t>(base + value);
  }
  return true;
}

CharacterAppearanceGeosetState BuildCharacterAppearanceGeosetState(
    const CharacterAppearanceSelection &selection,
    const CharacterAppearanceGeosetStores &stores) {
  CharacterAppearanceGeosetState state;
  state.hair = static_cast<std::uint16_t>(
      openwow::game::ResolveHairGeosetId(
          selection.race, selection.gender, selection.hair_style,
          stores.hair_geosets));

  const auto facial = openwow::game::ResolveFacialHairGeosets(
      selection.race, selection.gender, selection.facial_hair,
      stores.facial_hair_styles);
  if (facial.found) {
    state.facial_hair[0] = static_cast<std::uint16_t>(facial.group100);
    state.facial_hair[1] = static_cast<std::uint16_t>(facial.group300);
    state.facial_hair[2] = static_cast<std::uint16_t>(facial.group200);
    state.facial_hair[3] = static_cast<std::uint16_t>(facial.group1600);
    state.facial_hair[4] = static_cast<std::uint16_t>(facial.group1700);
    state.facial_accessory =
        static_cast<std::uint16_t>(facial.accessory702);
  }

  state.force_1700_family_1703 = selection.class_id == kDeathKnightClassId;
  if (!state.force_1700_family_1703) {
    if (const auto *face = FindSection(selection, stores.char_sections, 1u,
                                       selection.face,
                                       selection.skin_color);
        face != nullptr) {
      state.force_1700_family_1703 =
          (face->flags & kCharSectionsDeathKnightFlag) != 0u;
    }
  }

  if (stores.item_display_info == nullptr ||
      stores.helmet_geoset_vis_data == nullptr) {
    return state;
  }

  const auto resolve_item = [&](const std::size_t slot) {
    const std::uint32_t display_id =
        slot < selection.equipment_display_ids.size()
            ? selection.equipment_display_ids[slot]
            : 0u;
    return display_id != 0u
               ? stores.item_display_info->LookupEntry(display_id)
               : nullptr;
  };

  const auto *shirt = resolve_item(kSlotBody);
  const auto *chest = resolve_item(kSlotChest);
  const auto *waist = resolve_item(kSlotWaist);
  const auto *legs = resolve_item(kSlotLegs);
  const auto *feet = resolve_item(kSlotFeet);
  const auto *wrists = resolve_item(kSlotWrists);
  const auto *hands = resolve_item(kSlotHands);
  const auto *back = resolve_item(kSlotBack);
  const auto *tabard = resolve_item(kSlotTabard);

  std::uint32_t row1_component_mask = 0u;
  if (shirt != nullptr) {
    row1_component_mask |= 1u << 0u;
  }
  if (chest != nullptr) {
    row1_component_mask |=
        1u << (chest->geoset_control_1 != 0u ? 5u : 1u);
  }
  if (wrists != nullptr) {
    row1_component_mask |= 1u << 2u;
  }
  if (hands != nullptr) {
    row1_component_mask |=
        1u << (hands->geoset_control_1 != 0u ? 6u : 3u);
  }
  if (shirt != nullptr && (row1_component_mask & 0x7Eu) == 0u &&
      shirt->geoset_control_1 != 0u) {
    state.controls_body_800 = true;
    state.body_800 = static_cast<std::uint16_t>(
        kDefaultBody800 + shirt->geoset_control_1);
  }

  if (hands != nullptr && hands->geoset_control_1 != 0u) {
    state.controls_glove = true;
    state.glove = static_cast<std::uint16_t>(
        kDefaultGlove + hands->geoset_control_1);
  }
  if (feet != nullptr && feet->geoset_control_1 != 0u) {
    state.controls_boot = true;
    state.boot = static_cast<std::uint16_t>(
        kDefaultBoot + feet->geoset_control_1);
  }
  if (waist != nullptr && waist->geoset_control_1 != 0u) {
    state.controls_belt = true;
    state.belt = static_cast<std::uint16_t>(
        kDefaultBelt + waist->geoset_control_1);
  }
  if (back != nullptr && back->geoset_control_1 != 0u) {
    state.controls_cape = true;
    state.cape = static_cast<std::uint16_t>(
        kDefaultCape + back->geoset_control_1);
  }
  const bool suppress_secondary_legs =
      back != nullptr && (back->flags & 0x4u) != 0u;
  if (tabard != nullptr && tabard->geoset_control_1 != 0u) {
    state.controls_tabard = true;
    state.tabard = static_cast<std::uint16_t>(
        kDefaultTabard + tabard->geoset_control_1);
  }
  if (state.tabard == 0u && shirt != nullptr &&
      shirt->geoset_control_2 != 0u) {
    state.controls_torso_upper = true;
    state.torso_upper = static_cast<std::uint16_t>(
        kDefaultTorsoUpper + shirt->geoset_control_2);
  } else if (shirt != nullptr && shirt->geoset_control_2 != 0u) {
    state.controls_torso_upper = true;
  }
  if (!suppress_secondary_legs && legs != nullptr &&
      legs->geoset_control_2 != 0u) {
    state.controls_secondary_lower = true;
    state.secondary_lower = static_cast<std::uint16_t>(
        kDefaultSecondaryLower + legs->geoset_control_2);
  } else if (state.boot != 0u) {
    state.controls_secondary_lower = true;
    state.secondary_lower = kDefaultSecondaryLower;
  } else if (legs != nullptr && legs->geoset_control_2 != 0u) {
    state.controls_secondary_lower = true;
  }
  if (legs != nullptr && legs->geoset_control_1 != 0u) {
    state.controls_leg = true;
    if (legs->geoset_control_1 > 2u || state.tabard == 0u) {
      state.leg = static_cast<std::uint16_t>(
          kDefaultLeg + legs->geoset_control_1);
    }
  }
  if (chest != nullptr && chest->geoset_control_3 != 0u) {
    state.controls_robe = true;
    state.robe = static_cast<std::uint16_t>(
        kDefaultRobe + chest->geoset_control_3);
  } else if (!suppress_secondary_legs && legs != nullptr &&
             legs->geoset_control_3 != 0u) {
    state.controls_robe = true;
    state.robe = static_cast<std::uint16_t>(
        kDefaultRobe + legs->geoset_control_3);
  } else if (legs != nullptr && legs->geoset_control_3 != 0u) {
    state.controls_robe = true;
  }
  if (state.robe != 0u) {
    state.controls_boot = true;
    state.controls_secondary_lower = true;
    state.controls_leg = true;
    state.boot = 0u;
    state.secondary_lower = 0u;
    state.leg = 0u;
  }

  const auto *head = resolve_item(kSlotHead);
  if (head == nullptr) {
    return state;
  }
  const std::uint32_t helmet_vis_id =
      selection.gender == 1u ? head->helmet_geoset_vis_female
                             : head->helmet_geoset_vis_male;
  const auto *helmet =
      helmet_vis_id != 0u
          ? stores.helmet_geoset_vis_data->LookupEntry(helmet_vis_id)
          : nullptr;
  if (helmet == nullptr) {
    return state;
  }

  const std::uint32_t race_bit = HelmetRaceBit(selection.race);
  if ((helmet->hide_geoset[0] & race_bit) != 0u) {
    state.hair = 1u;
  }
  if ((helmet->hide_geoset[1] & race_bit) != 0u) {
    state.facial_hair[0] = 101u;
  }
  if ((helmet->hide_geoset[2] & race_bit) != 0u) {
    state.facial_hair[2] = 201u;
  }
  if ((helmet->hide_geoset[3] & race_bit) != 0u) {
    state.facial_hair[1] = 301u;
  }
  if ((helmet->hide_geoset[4] & race_bit) != 0u) {
    state.facial_accessory = 701u;
  }
  if ((helmet->hide_geoset[5] & race_bit) != 0u) {
    state.facial_hair[3] = 1601u;
  }
  if ((helmet->hide_geoset[6] & race_bit) != 0u) {
    state.facial_hair[4] = 1701u;
  }
  return state;
}

}

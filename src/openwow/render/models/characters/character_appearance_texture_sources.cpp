#include "openwow/render/models/characters/character_appearance_texture_sources.h"

#include "openwow/data/formats/dbc/dbc_loader.h"

#include <string_view>

namespace openwow::render {
namespace {

constexpr std::size_t kBackEquipmentSlot = 14u;

const openwow::data::dbc::CharSectionsEntry *FindSection(
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry>
        &char_sections,
    const CharacterAppearanceSelection &selection,
    const std::uint32_t base_section, const std::uint32_t type,
    const std::uint32_t variation) {

  const auto &entries = char_sections.entries();
  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    if (it->race_id == selection.race && it->sex_id == selection.gender &&
        it->base_section == base_section && it->type == type &&
        it->variation == variation) {
      return &*it;
    }
  }
  return nullptr;
}

std::string CopyTexture(const openwow::data::dbc::CharSectionsEntry *entry,
                        const std::size_t index) {
  return entry != nullptr && index < entry->texture_name.size()
             ? std::string(entry->texture_name[index])
             : std::string{};
}

std::string BuildCapeTexturePath(const std::string_view stem) {
  if (stem.empty()) {
    return {};
  }
  std::string path = "Item\\ObjectComponents\\Cape\\";
  path.append(stem);
  path.append(".blp");
  return path;
}

}

CharacterAppearanceTextureSources BuildCharacterAppearanceTextureSources(
    const CharacterAppearanceSelection &selection,
    const openwow::data::dbc::DbcLoader *const dbc) {
  return BuildCharacterAppearanceTextureSources(
      selection, dbc != nullptr ? &dbc->char_sections() : nullptr,
      dbc != nullptr ? &dbc->item_display_info() : nullptr,
      dbc != nullptr ? &dbc->chr_races() : nullptr);
}

CharacterAppearanceTextureSources BuildCharacterAppearanceTextureSources(
    const CharacterAppearanceSelection &selection,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry>
        *const char_sections,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ItemDisplayInfoEntry>
        *const item_display_info,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::ChrRacesEntry>
        *const chr_races) {
  CharacterAppearanceTextureSources sources;
  sources.gender = selection.gender;
  sources.equipment_display_ids = selection.equipment_display_ids;
  if (char_sections == nullptr || selection.race == 0u) {
    return sources;
  }

  const auto *base = FindSection(*char_sections, selection, 0u, 0u,
                                 selection.skin_color);
  const auto *face = FindSection(*char_sections, selection, 1u, selection.face,
                                 selection.skin_color);
  const auto *facial_hair = FindSection(*char_sections, selection, 2u,
                                        selection.facial_hair,
                                        selection.hair_color);
  const auto *hair = FindSection(*char_sections, selection, 3u,
                                 selection.hair_style,
                                 selection.hair_color);
  const auto *underwear = FindSection(*char_sections, selection, 4u, 0u,
                                      selection.skin_color);

  sources.base_skin = CopyTexture(base, 0u);
  sources.extra_skin = CopyTexture(base, 1u);
  sources.face_lower = CopyTexture(face, 0u);
  sources.face_upper = CopyTexture(face, 1u);
  sources.facial_hair_lower = CopyTexture(facial_hair, 0u);
  sources.facial_hair_upper = CopyTexture(facial_hair, 1u);
  sources.hair = CopyTexture(hair, 0u);

  constexpr std::uint32_t kChrRacesBaldHairSubstituteFlag = 0x8u;
  constexpr std::uint8_t kSexMale = 0u;
  if (sources.hair.empty() && selection.gender == kSexMale &&
      selection.hair_style == 0u && chr_races != nullptr) {
    if (const auto *race = chr_races->LookupEntry(selection.race);
        race != nullptr &&
        (race->flags & kChrRacesBaldHairSubstituteFlag) != 0u) {
      const auto *substitute =
          FindSection(*char_sections, selection, 3u, 1u, selection.hair_color);
      sources.hair = CopyTexture(substitute, 0u);
    }
  }
  sources.scalp_lower = CopyTexture(hair, 1u);
  sources.scalp_upper = CopyTexture(hair, 2u);
  sources.underwear_pelvis = CopyTexture(underwear, 0u);
  sources.underwear_torso = CopyTexture(underwear, 1u);

  const std::uint32_t cape_display_id =
      selection.equipment_display_ids[kBackEquipmentSlot];
  if (item_display_info != nullptr && cape_display_id != 0u) {
    if (const auto *cape = item_display_info->LookupEntry(cape_display_id);
        cape != nullptr) {
      sources.cape = BuildCapeTexturePath(cape->texture_name_left);
    }
  }
  return sources;
}

}

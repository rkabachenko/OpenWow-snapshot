
#include "openwow/game/display_info_resolver.h"

#include <array>
#include <string_view>
#include <utility>

namespace openwow::game {

namespace {

constexpr std::array<std::uint16_t, DisplayRecordGeosetSelection::kFamilyCount>
    kDisplayRecordGeosetFamilyBases = {100, 200, 300, 400, 500, 600, 700, 800};

std::string BuildDisplayRecordTexturePath(std::string_view model_path,
                                          std::string_view texture_name) {
  if (texture_name.empty()) {
    return {};
  }

  const auto separator = model_path.find_last_of('\\');
  if (separator == std::string_view::npos) {
    return std::string(texture_name);
  }

  std::string full_path(model_path.substr(0, separator + 1));
  full_path.append(texture_name);
  return full_path;
}

}

DisplayInfoResolver& DisplayInfoResolver::Get() {
  static DisplayInfoResolver instance;
  return instance;
}

const DisplayInfoEntry* DisplayInfoResolver::GetDisplayInfo(
    std::uint32_t display_id) const {
  auto it = entries_.find(display_id);
  return it != entries_.end() ? &it->second : nullptr;
}

const ExtraDisplayInfoEntry* DisplayInfoResolver::GetExtraDisplayInfo(
    std::uint32_t id) const {
  auto it = extra_entries_.find(id);
  return it != extra_entries_.end() ? &it->second : nullptr;
}

std::string DisplayInfoResolver::ResolveModelPath(
    std::uint32_t display_id) const {
  auto* entry = GetDisplayInfo(display_id);
  if (!entry) return {};
  return entry->model_path;
}

float DisplayInfoResolver::ResolveModelScale(
    std::uint32_t display_id) const {
  auto* entry = GetDisplayInfo(display_id);
  if (!entry) return 1.0f;
  return entry->model_scale;
}

DisplayRecordGeosetSelection DisplayInfoResolver::ResolveDisplayRecordGeosets(
    std::uint32_t display_id) const {
  const auto* entry = GetDisplayInfo(display_id);
  if (entry == nullptr) {
    return {};
  }

  return DecodePackedGeosetSelection(entry->packed_geoset_selection);
}

CreatureDisplayModelOverrides DisplayInfoResolver::ResolveCreatureModelOverrides(
    std::uint32_t display_id) const {
  CreatureDisplayModelOverrides overrides;

  const auto* entry = GetDisplayInfo(display_id);
  if (entry == nullptr) {
    return overrides;
  }

  for (std::size_t index = 0; index < overrides.texture_paths.size(); ++index) {
    overrides.texture_paths[index] =
        BuildDisplayRecordTexturePath(entry->model_path, entry->texture_variation[index]);
  }

  const auto particle_colors = GetParticleColorRecord(entry->particle_color_id);
  if (particle_colors.has_value()) {
    overrides.has_particle_colors = true;
    overrides.particle_colors = *particle_colors;
  }

  return overrides;
}

DisplayRecordGeosetSelection DisplayInfoResolver::DecodePackedGeosetSelection(
    std::uint32_t packed_geoset_selection) {
  DisplayRecordGeosetSelection selection;

  for (std::size_t family_index = 0; family_index < kDisplayRecordGeosetFamilyBases.size();
       ++family_index) {
    const auto geoset_value =
        static_cast<std::uint16_t>((packed_geoset_selection >> (family_index * 4u)) & 0xFu);
    if (geoset_value == 0) {
      continue;
    }

    selection.section_ids[family_index] =
        static_cast<std::uint16_t>(kDisplayRecordGeosetFamilyBases[family_index] + geoset_value);
  }

  return selection;
}

std::string DisplayInfoResolver::GetRaceModelPath(std::uint8_t race,
                                                    std::uint8_t gender) {

  const bool female = (gender == 1);
  switch (race) {
    case 1:
      return female ? "Character\\Human\\Female\\HumanFemale.m2"
                    : "Character\\Human\\Male\\HumanMale.m2";
    case 2:
      return female ? "Character\\Orc\\Female\\OrcFemale.m2"
                    : "Character\\Orc\\Male\\OrcMale.m2";
    case 3:
      return female ? "Character\\Dwarf\\Female\\DwarfFemale.m2"
                    : "Character\\Dwarf\\Male\\DwarfMale.m2";
    case 4:
      return female ? "Character\\NightElf\\Female\\NightElfFemale.m2"
                    : "Character\\NightElf\\Male\\NightElfMale.m2";
    case 5:
      return female ? "Character\\Scourge\\Female\\ScourgeFemale.m2"
                    : "Character\\Scourge\\Male\\ScourgeMale.m2";
    case 6:
      return female ? "Character\\Tauren\\Female\\TaurenFemale.m2"
                    : "Character\\Tauren\\Male\\TaurenMale.m2";
    case 7:
      return female ? "Character\\Gnome\\Female\\GnomeFemale.m2"
                    : "Character\\Gnome\\Male\\GnomeMale.m2";
    case 8:
      return female ? "Character\\Troll\\Female\\TrollFemale.m2"
                    : "Character\\Troll\\Male\\TrollMale.m2";
    case 10:
      return female ? "Character\\BloodElf\\Female\\BloodElfFemale.m2"
                    : "Character\\BloodElf\\Male\\BloodElfMale.m2";
    case 11:
      return female ? "Character\\Draenei\\Female\\DraeneiFemale.m2"
                    : "Character\\Draenei\\Male\\DraeneiMale.m2";
    default:
      return {};
  }
}

std::string DisplayInfoResolver::GetShapeshiftModelPath(
    std::uint8_t form, std::uint8_t race, std::uint8_t ) {

  switch (form) {
    case 1:
      return (race == 6) ? "Creature\\TaurenCat\\TaurenCat.m2"
                         : "Creature\\NightElfCat\\NightElfCat.m2";
    case 3:
      return "Creature\\Stag\\Stag.m2";
    case 4:
      return "Creature\\SeaLion\\SeaLion.m2";
    case 5:
      return (race == 6) ? "Creature\\TaurenBear\\TaurenBear.m2"
                         : "Creature\\NightElfBear\\NightElfBear.m2";
    case 8:
      return (race == 6) ? "Creature\\TaurenBear\\TaurenBear.m2"
                         : "Creature\\NightElfBear\\NightElfBear.m2";
    case 2:
      return "Creature\\TreeOfLife\\TreeOfLife.m2";
    case 16:
      return "Creature\\GhostWolf\\GhostWolf.m2";
    case 27:
      return "Creature\\SpiritOfRedemption\\SpiritOfRedemption.m2";
    case 28:
      return {};
    case 29:
      return (race == 6) ? "Creature\\TaurenBird\\TaurenBird.m2"
                         : "Creature\\NightElfBird\\NightElfBird.m2";
    case 31:
      return (race == 6) ? "Creature\\TaurenMoonkin\\TaurenMoonkin.m2"
                         : "Creature\\Moonkin\\Moonkin.m2";
    default:
      return {};
  }
}

DisplayInfoResolver::EquipmentVisual
DisplayInfoResolver::ResolveEquipmentVisual(
    std::uint32_t item_display_id,
    std::uint8_t slot) const {
  EquipmentVisual visual;
  visual.item_display_id = item_display_id;
  visual.inventory_type = slot;

  if (item_display_id == 0) return visual;

  auto* entry = GetDisplayInfo(item_display_id);
  if (entry) {
    visual.model_path = entry->model_path;
  }
  return visual;
}

DisplayInfoResolver::MountVisual DisplayInfoResolver::ResolveMountVisual(
    std::uint32_t mount_display_id) const {
  MountVisual mv;
  mv.display_id = mount_display_id;

  auto* entry = GetDisplayInfo(mount_display_id);
  if (entry) {
    mv.model_path = entry->model_path;
    mv.scale = entry->model_scale;
  } else {
    mv.scale = 1.0f;
  }
  return mv;
}

void DisplayInfoResolver::AddDisplayInfo(DisplayInfoEntry entry) {
  if (entry.display_id == 0) return;
  entries_[entry.display_id] = std::move(entry);
}

void DisplayInfoResolver::AddExtraDisplayInfo(ExtraDisplayInfoEntry entry) {
  if (entry.id == 0) return;
  extra_entries_[entry.id] = std::move(entry);
}

void DisplayInfoResolver::AddParticleColorRecord(
    std::uint32_t id, DisplayParticleColorRecord record) {
  if (id == 0) {
    return;
  }

  particle_color_records_[id] = std::move(record);
}

void DisplayInfoResolver::LoadDefaults() {

  struct RaceDefault {
    std::uint32_t display_id;
    const char* model_path;
  };
  static constexpr RaceDefault kRaceDefaults[] = {

      {49, "Character\\Human\\Male\\HumanMale.m2"},

      {50, "Character\\Human\\Female\\HumanFemale.m2"},

      {51, "Character\\Orc\\Male\\OrcMale.m2"},

      {52, "Character\\Orc\\Female\\OrcFemale.m2"},

      {53, "Character\\Dwarf\\Male\\DwarfMale.m2"},

      {54, "Character\\Dwarf\\Female\\DwarfFemale.m2"},

      {55, "Character\\NightElf\\Male\\NightElfMale.m2"},

      {56, "Character\\NightElf\\Female\\NightElfFemale.m2"},

      {57, "Character\\Scourge\\Male\\ScourgeMale.m2"},

      {58, "Character\\Scourge\\Female\\ScourgeFemale.m2"},

      {59, "Character\\Tauren\\Male\\TaurenMale.m2"},

      {60, "Character\\Tauren\\Female\\TaurenFemale.m2"},

      {1563, "Character\\Gnome\\Male\\GnomeMale.m2"},

      {1564, "Character\\Gnome\\Female\\GnomeFemale.m2"},

      {1478, "Character\\Troll\\Male\\TrollMale.m2"},

      {1479, "Character\\Troll\\Female\\TrollFemale.m2"},

      {15476, "Character\\BloodElf\\Male\\BloodElfMale.m2"},

      {15475, "Character\\BloodElf\\Female\\BloodElfFemale.m2"},

      {16125, "Character\\Draenei\\Male\\DraeneiMale.m2"},

      {16126, "Character\\Draenei\\Female\\DraeneiFemale.m2"},
  };

  for (const auto& rd : kRaceDefaults) {
    DisplayInfoEntry e{};
    e.display_id = rd.display_id;
    e.model_path = rd.model_path;
    e.model_scale = 1.0f;
    entries_.emplace(e.display_id, std::move(e));
  }
}

void DisplayInfoResolver::Clear() {
  entries_.clear();
  extra_entries_.clear();
  particle_color_records_.clear();
}

std::size_t DisplayInfoResolver::Count() const {
  return entries_.size();
}

std::size_t DisplayInfoResolver::ExtraCount() const {
  return extra_entries_.size();
}

std::optional<DisplayParticleColorRecord> DisplayInfoResolver::GetParticleColorRecord(
    std::uint32_t id) const {
  if (id == 0) {
    return std::nullopt;
  }

  const auto it = particle_color_records_.find(id);
  if (it == particle_color_records_.end()) {
    return std::nullopt;
  }

  return it->second;
}

}

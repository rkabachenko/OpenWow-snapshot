#include "openwow/data/formats/dbc/dbc_structures.h"
#include "dbc_schema_decode.h"

namespace openwow::data::dbc {

OPENWOW_DBC_SCHEMA(AreaTableEntry,
    e.id = f.GetUInt32(row, 0);
    e.map_id = f.GetUInt32(row, 1);
    e.parent_area = f.GetUInt32(row, 2);
    e.area_bit = f.GetUInt32(row, 3);
    e.flags = f.GetUInt32(row, 4);
    e.sound_provider_pref = f.GetUInt32(row, 5);
    e.sound_provider_pref_uw = f.GetUInt32(row, 6);
    e.sound_ambience_id = f.GetUInt32(row, 7);
    e.zone_music_id = f.GetUInt32(row, 8);
    e.zone_intro_music_id = f.GetUInt32(row, 9);
    e.exploration_level = f.GetUInt32(row, 10);
    e.name = f.GetLocalizedString(row, 11);
    e.faction_group_mask = f.GetUInt32(row, 28);
    DBC_U32_ARRAY(liquid_type_id, 29)
    e.min_elevation = f.GetFloat(row, 33);
    e.ambient_multiplier = f.GetFloat(row, 34);
    e.light_id = f.GetUInt32(row, 35);
)

CharStartOutfitEntry CharStartOutfitEntry::Load(const DbcFile &f, std::uint32_t row) {

  constexpr std::uint32_t kIdOffset = 0u;
  constexpr std::uint32_t kRaceOffset = 4u;
  constexpr std::uint32_t kClassOffset = 5u;
  constexpr std::uint32_t kGenderOffset = 6u;
  constexpr std::uint32_t kOutfitIdOffset = 7u;
  constexpr std::uint32_t kItemIdsOffset = 8u;
  constexpr std::uint32_t kDisplayIdsOffset =
      kItemIdsOffset + kItemSlotCount * DbcHeader::kWordSize;
  constexpr std::uint32_t kInventoryTypesOffset =
      kDisplayIdsOffset + kItemSlotCount * DbcHeader::kWordSize;
  constexpr std::uint32_t kItemSlotStride = DbcHeader::kWordSize;

  CharStartOutfitEntry e{};
  e.id = f.GetUInt32AtOffset(row, kIdOffset);
  e.race = f.GetByte(row, kRaceOffset);
  e.class_id = f.GetByte(row, kClassOffset);
  e.gender = f.GetByte(row, kGenderOffset);
  e.outfit_id = f.GetByte(row, kOutfitIdOffset);

  for (std::size_t i = 0; i < kItemSlotCount; ++i) {
    const auto offset =
        static_cast<std::uint32_t>(i) * kItemSlotStride;
    e.item_id[i] = f.GetInt32AtOffset(row, kItemIdsOffset + offset);
    e.display_id[i] = f.GetInt32AtOffset(row, kDisplayIdsOffset + offset);
    e.inv_type[i] = f.GetInt32AtOffset(row, kInventoryTypesOffset + offset);
  }
  return e;
}

OPENWOW_DBC_SCHEMA(ChrClassesEntry,
  DBC_U32(id, 0)
  DBC_U32(primary_stat_type, 1)
  DBC_U32(power_type, 2)
  DBC_STRING(pet_name_token, 3)
  DBC_LOCALIZED(name, 4)
  DBC_LOCALIZED(name_female, 21)
  DBC_LOCALIZED(name_male, 38)
  DBC_STRING(client_file_string, 55)
  DBC_U32(spell_family, 56)
  DBC_U32(class_flags, 57)
  DBC_U32(cinematic_sequence_id, 58)
  DBC_U32(required_expansion, 59)
)

std::string_view ChrClassesEntry::DisplayNameForSex(const std::uint32_t sex_id) const {
  if (sex_id == 0) {
    if (!name_male.empty()) {
      return name_male;
    }
    if (!name_female.empty()) {
      return name_female;
    }
    return name;
  }

  if (sex_id == 1) {
    if (!name_female.empty()) {
      return name_female;
    }
    if (!name_male.empty()) {
      return name_male;
    }
    return name;
  }

  return name;
}

std::uint32_t ChrClassesEntry::ResolveDisplaySex(const std::uint32_t sex_id) const {
  if (sex_id == 0) {
    if (!name_male.empty()) {
      return 0;
    }
    if (!name_female.empty()) {
      return 1;
    }
  } else if (sex_id == 1) {
    if (!name_female.empty()) {
      return 1;
    }
    if (!name_male.empty()) {
      return 0;
    }
  }

  return sex_id;
}

OPENWOW_DBC_SCHEMA(ChrRacesEntry,
  DBC_U32(id, 0)
  DBC_U32(flags, 1)
  DBC_U32(faction_id, 2)
  DBC_U32(exploration_sound_id, 3)
  DBC_U32(model_male, 4)
  DBC_U32(model_female, 5)
  DBC_STRING(model_client_prefix, 6)
  DBC_U32(default_language_id, 7)
  DBC_U32(creature_type, 8)
  DBC_U32(resurrection_sickness_spell_id, 9)
  DBC_U32(splash_sound_id, 10)
  DBC_STRING(client_file_string, 11)
  DBC_U32(cinematic_sequence_id, 12)
  DBC_U32(team_id, 13)
  DBC_LOCALIZED(name, 14)
  DBC_LOCALIZED(name_female, 31)
  DBC_LOCALIZED(name_male, 48)
  DBC_STRING(facial_hair_male, 65)
  DBC_STRING(facial_hair_female, 66)
  DBC_STRING(hair_customization, 67)
  DBC_U32(required_expansion, 68)
)

std::string_view ChrRacesEntry::DisplayNameForSex(const std::uint32_t sex_id) const {
  if (sex_id == 0) {
    if (!name_male.empty()) {
      return name_male;
    }
    if (!name_female.empty()) {
      return name_female;
    }
    return name;
  }

  if (sex_id == 1) {
    if (!name_female.empty()) {
      return name_female;
    }
    if (!name_male.empty()) {
      return name_male;
    }
    return name;
  }

  return name;
}

std::uint32_t ChrRacesEntry::ResolveDisplaySex(const std::uint32_t sex_id) const {
  if (sex_id == 0) {
    if (!name_male.empty()) {
      return 0;
    }
    if (!name_female.empty()) {
      return 1;
    }
  } else if (sex_id == 1) {
    if (!name_female.empty()) {
      return 1;
    }
    if (!name_male.empty()) {
      return 0;
    }
  }

  return sex_id;
}

OPENWOW_DBC_SCHEMA(CinematicSequencesEntry,
    e.id = f.GetUInt32(row, 0);
    e.sound_id = f.GetUInt32(row, 1);
    for (std::size_t index = 0; index < e.camera_ids.size(); ++index) {
      e.camera_ids[index] = f.GetUInt32(row, static_cast<std::uint32_t>(index + 2));
    }
)

OPENWOW_DBC_SCHEMA(CinematicCameraEntry,
  DBC_U32(id, 0)
  DBC_STRING(model, 1)
  DBC_U32(sound_id, 2)
  DBC_F32(origin_x, 3)
  DBC_F32(origin_y, 4)
  DBC_F32(origin_z, 5)
  DBC_F32(origin_facing, 6)
)

OPENWOW_DBC_SCHEMA(CreatureDisplayInfoEntry,
    e.id = f.GetUInt32(row, 0);
    e.model_id = f.GetUInt32(row, 1);
    e.sound_id = f.GetUInt32(row, 2);
    e.extra_info = f.GetUInt32(row, 3);
    e.scale = f.GetFloat(row, 4);
    e.model_alpha = f.GetUInt32(row, 5);
    e.texture_variation[0] = f.GetString(row, 6);
    e.texture_variation[1] = f.GetString(row, 7);
    e.texture_variation[2] = f.GetString(row, 8);
    e.portrait_texture_name = f.GetString(row, 9);
    e.size_class = f.GetInt32(row, 10);
    e.blood_id = f.GetUInt32(row, 11);
    e.npc_sound_id = f.GetUInt32(row, 12);
    e.particle_color_id = f.GetUInt32(row, 13);
    e.creature_geoset_data = f.GetUInt32(row, 14);
    e.object_effect_package_id = f.GetUInt32(row, 15);
)

OPENWOW_DBC_SCHEMA(CreatureModelDataEntry,
    e.id = f.GetUInt32(row, 0);
    e.flags = f.GetUInt32(row, 1);
    e.model_name = f.GetString(row, 2);
    e.size_class = f.GetInt32(row, 3);
    e.scale = f.GetFloat(row, 4);
    e.blood_id = f.GetUInt32(row, 5);
    e.footprint_texture_id = f.GetUInt32(row, 6);
    e.footprint_texture_length = f.GetFloat(row, 7);
    e.footprint_texture_width = f.GetFloat(row, 8);
    e.footprint_particle_scale = f.GetFloat(row, 9);
    e.foley_material_id = f.GetUInt32(row, 10);
    e.footstep_shake_size = f.GetUInt32(row, 11);
    e.death_thud_shake_size = f.GetUInt32(row, 12);
    e.sound_id = f.GetUInt32(row, 13);
    e.collision_width = f.GetFloat(row, 14);
    e.collision_height = f.GetFloat(row, 15);
    e.mount_height = f.GetFloat(row, 16);
    for (std::uint32_t index = 0; index < e.geo_box_min.size(); ++index) {
      e.geo_box_min[index] = f.GetFloat(row, 17 + index);
      e.geo_box_max[index] = f.GetFloat(row, 20 + index);
    }
    e.world_effect_scale = f.GetFloat(row, 23);
    e.attached_effect_scale = f.GetFloat(row, 24);
    e.missile_collision_radius = f.GetFloat(row, 25);
    e.missile_collision_push = f.GetFloat(row, 26);
    e.missile_collision_raise = f.GetFloat(row, 27);
)

OPENWOW_DBC_SCHEMA(GameObjectDisplayInfoEntry,
  DBC_U32(id, 0)
  DBC_STRING(filename, 1)
  DBC_U32_ARRAY(sound_ids, 2)
  DBC_F32(min_x, 12)
  DBC_F32(min_y, 13)
  DBC_F32(min_z, 14)
  DBC_F32(max_x, 15)
  DBC_F32(max_y, 16)
  DBC_F32(max_z, 17)
  DBC_U32(object_effect_package_id, 18)
)

OPENWOW_DBC_SCHEMA(ItemDisplayInfoEntry,
  DBC_U32(id, 0)
  DBC_STRING(model_name_left, 1)
  DBC_STRING(model_name_right, 2)
  DBC_STRING(texture_name_left, 3)
  DBC_STRING(texture_name_right, 4)
  DBC_STRING(inventory_icon, 5)
  DBC_STRING(inventory_icon_2, 6)
  DBC_U32(geoset_control_1, 7)
  DBC_U32(geoset_control_2, 8)
  DBC_U32(geoset_control_3, 9)
  DBC_U32(flags, 10)
  DBC_U32(spell_visual_id, 11)
  DBC_U32(group_sound_index, 12)
  DBC_U32(helmet_geoset_vis_male, 13)
  DBC_U32(helmet_geoset_vis_female, 14)
  DBC_STRING_ARRAY(component_texture_name, 15)
  DBC_U32(item_visuals_id, 23)
  DBC_U32(particle_color_id, 24)
)

OPENWOW_DBC_SCHEMA(LightEntry,
    e.id = f.GetUInt32(row, 0);
    e.map_id = f.GetUInt32(row, 1);
    e.x = f.GetFloat(row, 2);
    e.y = f.GetFloat(row, 3);
    e.z = f.GetFloat(row, 4);
    e.falloff_start = f.GetFloat(row, 5);
    e.falloff_end = f.GetFloat(row, 6);
    DBC_U32_ARRAY(light_params, 7)
    NormalizeLightEntryRetailCoordinates(e);
)

void NormalizeLightEntryRetailCoordinates(LightEntry &entry,
                                          const bool normalize_coordinates,
                                          const bool special_zero_origin_mode) {
  if (!normalize_coordinates ||
      (entry.x == 0.0f && entry.y == 0.0f && entry.z == 0.0f)) {
    return;
  }

  constexpr float kDatabaseUnitsToWorld = 1.0f / 36.0f;
  constexpr float kWorldMapOrigin = 17066.666f;
  const float origin = special_zero_origin_mode ? 0.0f : kWorldMapOrigin;
  const float raw_x = entry.x;
  const float raw_y = entry.y;
  const float raw_z = entry.z;

  entry.x = origin - raw_z * kDatabaseUnitsToWorld;
  entry.y = origin - raw_x * kDatabaseUnitsToWorld;
  entry.z = raw_y * kDatabaseUnitsToWorld;
  entry.falloff_start *= kDatabaseUnitsToWorld;
  entry.falloff_end *= kDatabaseUnitsToWorld;
}

OPENWOW_DBC_SCHEMA(LightIntBandEntry,
  DBC_U32(id, 0)
  DBC_U32(num_entries, 1)
  DBC_U32_ARRAY(times, 2)
  DBC_U32_ARRAY(values, 18)
)

OPENWOW_DBC_SCHEMA(LightFloatBandEntry,
  DBC_U32(id, 0)
  DBC_U32(num_entries, 1)
  DBC_U32_ARRAY(times, 2)
  DBC_F32_ARRAY(values, 18)
)

OPENWOW_DBC_SCHEMA(LoadingScreensEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
  DBC_STRING(path, 2)
  DBC_U32(has_wide_screen, 3)
)

OPENWOW_DBC_SCHEMA(MapEntry,
  DBC_U32(id, 0)
  DBC_STRING(internal_name, 1)
  DBC_U32(map_type, 2)
  DBC_U32(flags, 3)
  DBC_U32(is_pvp, 4)
  DBC_LOCALIZED(name, 5)
  DBC_U32(linked_zone, 22)
  DBC_LOCALIZED(description_horde, 23)
  DBC_LOCALIZED(description_alliance, 40)
  DBC_U32(loading_screen_id, 57)
  DBC_F32(minimap_icon_scale, 58)
  DBC_I32(entrance_map, 59)
  DBC_F32(entrance_x, 60)
  DBC_F32(entrance_y, 61)
  DBC_U32(time_of_day_override, 62)
  DBC_U32(expansion_id, 63)
  DBC_U32(raid_offset, 64)
  DBC_U32(max_players, 65)
)

OPENWOW_DBC_SCHEMA(SoundEntriesEntry,
  DBC_U32(id, 0)
  DBC_U32(sound_type, 1)
  DBC_STRING(name, 2)
  DBC_STRING_ARRAY(file, 3)
  DBC_U32_ARRAY(freq, 13)
  DBC_STRING(directory_base, 23)
  DBC_F32(volume, 24)
  DBC_U32(flags, 25)
  DBC_F32(min_distance, 26)
  DBC_F32(distance_cutoff, 27)
  DBC_F32(eax_definition, 28)
  DBC_U32(sound_entries_advanced_id, 29)
)

OPENWOW_DBC_SCHEMA(SpellIconEntry,
  DBC_U32(id, 0)
  DBC_STRING(icon_path, 1)
)

OPENWOW_DBC_SCHEMA(SpellVisualEntry,
  DBC_U32(id, 0)
  DBC_U32(precast_kit, 1)
  DBC_U32(cast_kit, 2)
  DBC_U32(impact_kit, 3)
  DBC_U32(state_kit, 4)
  DBC_U32(state_done_kit, 5)
  DBC_U32(channel_kit, 6)
  DBC_U32(has_missile, 7)
  DBC_I32(missile_model, 8)
  DBC_U32(missile_path_type, 9)
  DBC_I32(missile_destination_attachment, 10)
  DBC_U32(missile_sound_id, 11)
  DBC_U32(anim_event_sound_id, 12)
  DBC_U32(flags, 13)
  DBC_U32(caster_impact_kit, 14)
  DBC_U32(target_impact_kit, 15)
  DBC_I32(missile_attachment_id, 16)
  DBC_I32(missile_follow_ground_height, 17)
  DBC_I32(missile_follow_ground_drop_speed, 18)
  DBC_I32(missile_follow_ground_approach, 19)
  DBC_U32(missile_follow_ground_flags, 20)
  DBC_U32(missile_motion_id, 21)
  DBC_U32(missile_targeting_kit, 22)
  DBC_U32(instant_area_kit, 23)
  DBC_U32(impact_area_kit, 24)
  DBC_U32(persistent_area_kit, 25)
  DBC_F32(missile_cast_offset_x, 26)
  DBC_F32(missile_cast_offset_y, 27)
  DBC_F32(missile_cast_offset_z, 28)
  DBC_F32(missile_impact_offset_x, 29)
  DBC_F32(missile_impact_offset_y, 30)
  DBC_F32(missile_impact_offset_z, 31)
)

OPENWOW_DBC_SCHEMA(SpellVisualKitEntry,
  DBC_U32(id, 0)
  DBC_I32(start_anim_id, 1)
  DBC_I32(anim_id, 2)
  DBC_U32(head_effect, 3)
  DBC_U32(chest_effect, 4)
  DBC_U32(base_effect, 5)
  DBC_U32(left_hand_effect, 6)
  DBC_U32(right_hand_effect, 7)
  DBC_U32(breath_effect, 8)
  DBC_U32(left_weapon_effect, 9)
  DBC_U32(right_weapon_effect, 10)
  DBC_U32(special1_effect, 11)
  DBC_U32(special2_effect, 12)
  DBC_U32(special3_effect, 13)
  DBC_U32(world_effect, 14)
  DBC_U32(sound_id, 15)
  DBC_U32(shake_id, 16)
  DBC_U32_ARRAY(proc_type, 17)
  DBC_F32_ARRAY(proc_param_zero, 21)
  DBC_F32_ARRAY(proc_param_one, 25)
  DBC_F32_ARRAY(proc_param_two, 29)
  DBC_F32_ARRAY(proc_param_three, 33)
  DBC_U32(flags, 37)
)

OPENWOW_DBC_SCHEMA(SpellVisualEffectNameEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
  DBC_STRING(file_path, 2)
  DBC_F32(area_effect_size, 3)
  DBC_F32(scale, 4)
  DBC_F32(min_allowed_scale, 5)
  DBC_F32(max_allowed_scale, 6)
)

OPENWOW_DBC_SCHEMA(AnimationDataEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
  DBC_U32(weapon_flags, 2)
  DBC_U32(body_flags, 3)
  DBC_U32(flags, 4)
  DBC_U32(fallback, 5)
  DBC_U32(behavior_id, 6)
  DBC_U32(behavior_tier, 7)
)

OPENWOW_DBC_SCHEMA(SkillLineEntry,
  DBC_U32(id, 0)
  DBC_I32(category_id, 1)
  DBC_U32(skill_cost_id, 2)
  DBC_LOCALIZED(name, 3)
  DBC_LOCALIZED(description, 20)
  DBC_U32(spell_icon_id, 37)
  DBC_LOCALIZED(alternate_verb, 38)
  DBC_U32(can_link, 55)
)

OPENWOW_DBC_SCHEMA(SkillLineAbilityEntry,
  DBC_U32(id, 0)
  DBC_U32(skill_id, 1)
  DBC_U32(spell_id, 2)
  DBC_U32(race_mask, 3)
  DBC_U32(class_mask, 4)
  DBC_U32(exclude_race_mask, 5)
  DBC_U32(exclude_class_mask, 6)
  DBC_U32(min_skill_rank, 7)
  DBC_U32(superseded_by_spell, 8)
  DBC_U32(acquire_method, 9)
  DBC_U32(trivial_skill_hi, 10)
  DBC_U32(trivial_skill_lo, 11)
  DBC_U32(num_skill_ups, 12)
  DBC_U32(unique_bit, 13)
)

OPENWOW_DBC_SCHEMA(GtCombatRatingsEntry,
  DBC_ROW_ID()
  DBC_F32(value, 0)
)

OPENWOW_DBC_SCHEMA(SpellCastTimesEntry,
  DBC_U32(id, 0)
  DBC_I32(base_cast_time, 1)
  DBC_I32(per_level, 2)
  DBC_I32(minimum, 3)
)

OPENWOW_DBC_SCHEMA(SpellRangeEntry,
  DBC_U32(id, 0)
  DBC_F32(range_min, 1)
  DBC_F32(range_min_friendly, 2)
  DBC_F32(range_max, 3)
  DBC_F32(range_max_friendly, 4)
  DBC_U32(flags, 5)
  DBC_LOCALIZED(display_name, 6)
  DBC_LOCALIZED(display_name_short, 23)
)

OPENWOW_DBC_SCHEMA(SpellEntry,
    e.id = f.GetUInt32(row, 0);
    e.category = f.GetUInt32(row, 1);
    e.dispel = f.GetUInt32(row, 2);
    e.mechanic = f.GetUInt32(row, 3);

    e.attributes = f.GetUInt32(row, 4);
    e.attributes_ex = f.GetUInt32(row, 5);
    e.attributes_ex2 = f.GetUInt32(row, 6);
    e.attributes_ex3 = f.GetUInt32(row, 7);
    e.attributes_ex4 = f.GetUInt32(row, 8);
    e.attributes_ex5 = f.GetUInt32(row, 9);
    e.attributes_ex6 = f.GetUInt32(row, 10);
    e.attributes_ex7 = f.GetUInt32(row, 11);

    e.stances = f.GetUInt32(row, 12);
    e.stances_high = f.GetUInt32(row, 13);
    e.stances_not = f.GetUInt32(row, 14);
    e.stances_not_high = f.GetUInt32(row, 15);

    e.targets = f.GetUInt32(row, 16);
    e.target_creature_type = f.GetUInt32(row, 17);
    e.requires_spell_focus = f.GetUInt32(row, 18);
    e.facing_caster_flags = f.GetUInt32(row, 19);

    e.caster_aura_state = f.GetUInt32(row, 20);
    e.target_aura_state = f.GetUInt32(row, 21);
    e.caster_aura_state_not = f.GetUInt32(row, 22);
    e.target_aura_state_not = f.GetUInt32(row, 23);
    e.caster_aura_spell = f.GetUInt32(row, 24);
    e.target_aura_spell = f.GetUInt32(row, 25);
    e.exclude_caster_aura_spell = f.GetUInt32(row, 26);
    e.exclude_target_aura_spell = f.GetUInt32(row, 27);

    e.casting_time_index = f.GetUInt32(row, 28);
    e.recovery_time = f.GetUInt32(row, 29);
    e.category_recovery_time = f.GetUInt32(row, 30);

    e.interrupt_flags = f.GetUInt32(row, 31);
    e.aura_interrupt_flags = f.GetUInt32(row, 32);
    e.channel_interrupt_flags = f.GetUInt32(row, 33);

    e.proc_flags = f.GetUInt32(row, 34);
    e.proc_chance = f.GetUInt32(row, 35);
    e.proc_charges = f.GetUInt32(row, 36);

    e.max_level = f.GetUInt32(row, 37);
    e.base_level = f.GetUInt32(row, 38);
    e.spell_level = f.GetUInt32(row, 39);

    e.duration_index = f.GetUInt32(row, 40);
    e.power_type = f.GetUInt32(row, 41);
    e.mana_cost = f.GetUInt32(row, 42);
    e.mana_cost_per_level = f.GetUInt32(row, 43);
    e.mana_per_second = f.GetUInt32(row, 44);
    e.mana_per_second_per_level = f.GetUInt32(row, 45);
    e.range_index = f.GetUInt32(row, 46);
    e.speed = f.GetFloat(row, 47);
    e.modal_next_spell = f.GetUInt32(row, 48);
    e.stack_amount = f.GetUInt32(row, 49);

    DBC_U32_ARRAY(totem, 50)

    for (int i = 0; i < kMaxSpellReagents; ++i) {
      e.reagent[i] = f.GetInt32(row, 52 + i);
      e.reagent_count[i] = f.GetUInt32(row, 60 + i);
    }

    e.equipped_item_class = f.GetInt32(row, 68);
    e.equipped_item_sub_class_mask = f.GetInt32(row, 69);
    e.equipped_item_inv_type_mask = f.GetInt32(row, 70);

    for (int i = 0; i < kMaxSpellEffects; ++i) {
      e.effect[i] = f.GetUInt32(row, 71 + i);
      e.effect_die_sides[i] = f.GetInt32(row, 74 + i);
      e.effect_real_points_per_lvl[i] = f.GetFloat(row, 77 + i);
      e.effect_base_points[i] = f.GetInt32(row, 80 + i);
      e.effect_mechanic[i] = f.GetUInt32(row, 83 + i);
      e.effect_implicit_target_a[i] = f.GetUInt32(row, 86 + i);
      e.effect_implicit_target_b[i] = f.GetUInt32(row, 89 + i);
      e.effect_radius_index[i] = f.GetUInt32(row, 92 + i);
      e.effect_apply_aura[i] = f.GetUInt32(row, 95 + i);
      e.effect_amplitude[i] = f.GetUInt32(row, 98 + i);
      e.effect_value_multiplier[i] = f.GetFloat(row, 101 + i);
      e.effect_chain_target[i] = f.GetUInt32(row, 104 + i);
      e.effect_item_type[i] = f.GetUInt32(row, 107 + i);
      e.effect_misc_value[i] = f.GetInt32(row, 110 + i);
      e.effect_misc_value_b[i] = f.GetInt32(row, 113 + i);
      e.effect_trigger_spell[i] = f.GetUInt32(row, 116 + i);
      e.effect_points_per_combo[i] = f.GetFloat(row, 119 + i);
    }

    DBC_U32_ARRAY(effect_spell_class_mask, 122)

    e.spell_visual[0] = f.GetUInt32(row, 131);
    e.spell_visual[1] = f.GetUInt32(row, 132);
    e.spell_icon_id = f.GetUInt32(row, 133);
    e.active_icon_id = f.GetUInt32(row, 134);
    e.spell_priority = f.GetUInt32(row, 135);

    e.spell_name = f.GetLocalizedString(row, 136);
    e.rank = f.GetLocalizedString(row, 153);
    e.description = f.GetLocalizedString(row, 170);
    e.tooltip = f.GetLocalizedString(row, 187);

    e.mana_cost_percentage = f.GetUInt32(row, 204);
    e.start_recovery_category = f.GetUInt32(row, 205);
    e.start_recovery_time = f.GetUInt32(row, 206);
    e.max_target_level = f.GetUInt32(row, 207);
    e.spell_family_name = f.GetUInt32(row, 208);
    e.spell_family_flags[0] = f.GetUInt32(row, 209);
    e.spell_family_flags[1] = f.GetUInt32(row, 210);
    e.spell_family_flags[2] = f.GetUInt32(row, 211);
    e.max_affected_targets = f.GetUInt32(row, 212);
    e.dmg_class = f.GetUInt32(row, 213);
    e.prevention_type = f.GetUInt32(row, 214);
    e.stance_bar_order = f.GetUInt32(row, 215);

    DBC_F32_ARRAY(effect_damage_multiplier, 216)

    e.minimum_faction_id = f.GetUInt32(row, 219);
    e.minimum_reputation = f.GetInt32(row, 220);
    e.required_aura_vision = f.GetUInt32(row, 221);

    e.totem_category[0] = f.GetUInt32(row, 222);
    e.totem_category[1] = f.GetUInt32(row, 223);
    e.area_group_id = f.GetInt32(row, 224);
    e.school_mask = f.GetUInt32(row, 225);
    e.rune_cost_id = f.GetUInt32(row, 226);
    e.spell_missile_id = f.GetUInt32(row, 227);
    e.power_display_id = f.GetUInt32(row, 228);

    DBC_F32_ARRAY(effect_bonus_multiplier, 229)

    e.spell_description_variable_id = f.GetUInt32(row, 232);
    e.spell_difficulty_id = f.GetUInt32(row, 233);
)

OPENWOW_DBC_SCHEMA(TalentEntry,
  DBC_U32(id, 0)
  DBC_U32(tab_id, 1)
  DBC_U32(tier_id, 2)
  DBC_U32(column_index, 3)
  DBC_U32_ARRAY(spell_rank, 4)
  DBC_U32_ARRAY(prereq_talent, 13)
  DBC_U32_ARRAY(prereq_rank, 16)
  DBC_U32(flags, 19)
  DBC_U32(required_spell_id, 20)
  DBC_U32_ARRAY(pet_talent_mask, 21)
)

OPENWOW_DBC_SCHEMA(TalentTabEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
  DBC_U32(spell_icon_id, 18)
  DBC_U32(race_mask, 19)
  DBC_U32(class_mask, 20)
  DBC_U32(pet_talent_mask, 21)
  DBC_U32(order_index, 22)
  DBC_STRING(background_file, 23)
)

OPENWOW_DBC_SCHEMA(TaxiNodesEntry,
    e.id = f.GetUInt32(row, 0);
    e.map_id = f.GetUInt32(row, 1);
    e.x = f.GetFloat(row, 2);
    e.y = f.GetFloat(row, 3);
    e.z = f.GetFloat(row, 4);
    e.name = f.GetLocalizedString(row, 5);
    e.mount_creature_id[0] = f.GetUInt32(row, 22);
    e.mount_creature_id[1] = f.GetUInt32(row, 23);
)

OPENWOW_DBC_SCHEMA(TaxiPathEntry,
  DBC_U32(id, 0)
  DBC_U32(from_node_id, 1)
  DBC_U32(to_node_id, 2)
  DBC_U32(cost, 3)
)

OPENWOW_DBC_SCHEMA(TaxiPathNodeEntry,
  DBC_U32(id, 0)
  DBC_U32(path_id, 1)
  DBC_U32(node_index, 2)
  DBC_U32(map_id, 3)
  DBC_F32(x, 4)
  DBC_F32(y, 5)
  DBC_F32(z, 6)
  DBC_U32(flags, 7)
  DBC_U32(delay, 8)
  DBC_U32(arrival_event_id, 9)
  DBC_U32(departure_event_id, 10)
)

}

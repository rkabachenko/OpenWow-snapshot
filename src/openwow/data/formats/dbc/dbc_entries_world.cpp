
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "dbc_schema_decode.h"

namespace openwow::data::dbc {

OPENWOW_DBC_SCHEMA(BarberShopStyleEntry,
  DBC_U32(id, 0)
  DBC_U32(type, 1)
  DBC_LOCALIZED(name, 2)
  DBC_LOCALIZED(description, 19)
  DBC_F32(cost_modifier, 36)
  DBC_U32(race, 37)
  DBC_U32(sex, 38)
  DBC_U32(data, 39)
)

OPENWOW_DBC_SCHEMA(WorldMapOverlayEntry,
  DBC_U32(id, 0)
  DBC_U32(map_area_id, 1)
  DBC_U32_ARRAY(area_id, 2)
  DBC_U32(map_point_x, 6)
  DBC_U32(map_point_y, 7)
  DBC_STRING(texture_name, 8)
  DBC_U32(texture_width, 9)
  DBC_U32(texture_height, 10)
  DBC_U32(offset_x, 11)
  DBC_U32(offset_y, 12)
  DBC_U32(hit_rect_top, 13)
  DBC_U32(hit_rect_left, 14)
  DBC_U32(hit_rect_bottom, 15)
  DBC_U32(hit_rect_right, 16)
)

OPENWOW_DBC_SCHEMA(WorldSafeLocsEntry,
  DBC_U32(id, 0)
  DBC_U32(map_id, 1)
  DBC_F32(x, 2)
  DBC_F32(y, 3)
  DBC_F32(z, 4)
  DBC_LOCALIZED(name, 5)
)

OPENWOW_DBC_SCHEMA(LightSkyboxEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
  DBC_U32(flags, 2)
)

OPENWOW_DBC_SCHEMA(GroundEffectTextureEntry,
    e.id = f.GetUInt32(row, 0);
    for (int i = 0; i < 4; ++i) {
      e.doodad_id[i] = f.GetUInt32(row, 1 + i);
      e.doodad_weight[i] = f.GetInt32(row, 5 + i);
    }
    e.sound = f.GetUInt32(row, 9);
    e.density = f.GetUInt32(row, 10);
)

OPENWOW_DBC_SCHEMA(GroundEffectDoodadEntry,
  DBC_U32(id, 0)
  DBC_STRING(doodad_path, 1)
  DBC_U32(flags, 2)
)

OPENWOW_DBC_SCHEMA(WMOAreaTableEntry,
  DBC_U32(id, 0)
  DBC_U32(wmo_id, 1)
  DBC_U32(name_set_id, 2)
  DBC_I32(wmo_group_id, 3)
  DBC_U32(sound_pref, 4)
  DBC_U32(sound_pref_uw, 5)
  DBC_U32(ambience_id, 6)
  DBC_U32(zone_music, 7)
  DBC_U32(intro_sound, 8)
  DBC_U32(flags, 9)
  DBC_U32(area_table_id, 10)
  DBC_LOCALIZED(name, 11)
)

OPENWOW_DBC_SCHEMA(AreaTriggerEntry,
  DBC_U32(id, 0)
  DBC_U32(map_id, 1)
  DBC_F32(x, 2)
  DBC_F32(y, 3)
  DBC_F32(z, 4)
  DBC_F32(radius, 5)
  DBC_F32(box_length, 6)
  DBC_F32(box_width, 7)
  DBC_F32(box_height, 8)
  DBC_F32(box_yaw, 9)
)

OPENWOW_DBC_SCHEMA(BattlemasterListEntry,
  DBC_U32(id, 0)
  DBC_I32_ARRAY(map_id, 1)
  DBC_U32(instance_type, 9)
  DBC_U32(groups_allowed, 10)
  DBC_LOCALIZED(name, 11)
  DBC_U32(max_group_size, 28)
  DBC_U32(holiday_world_state, 29)
  DBC_U32(min_level, 30)
  DBC_U32(max_level, 31)
)

OPENWOW_DBC_SCHEMA(DurabilityCostsEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(cost, 1)
)

OPENWOW_DBC_SCHEMA(DurabilityQualityEntry,
  DBC_U32(id, 0)
  DBC_F32(quality_mod, 1)
)

OPENWOW_DBC_SCHEMA(LiquidTypeEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
  DBC_U32(flags, 2)
  DBC_U32(sound_bank, 3)
  DBC_U32(sound_id, 4)
  DBC_U32(spell_id, 5)
  DBC_F32(max_darken_depth, 6)
  DBC_F32(fog_darken_intensity, 7)
  DBC_F32(amb_darken_intensity, 8)
  DBC_F32(dir_darken_intensity, 9)
  DBC_U32(light_id, 10)
  DBC_F32(particle_scale, 11)
  DBC_U32(particle_movement, 12)
  DBC_U32(particle_tex_slots, 13)
  DBC_U32(liquid_material_id, 14)
  DBC_STRING_ARRAY(textures, 15)
  DBC_U32_ARRAY(colors, 21)
  DBC_F32_ARRAY(float_params, 23)
  DBC_U32_ARRAY(int_params, 41)
)

OPENWOW_DBC_SCHEMA(MovieEntry,
  DBC_U32(id, 0)
  DBC_STRING(filename, 1)
  DBC_U32(volume, 2)
)

OPENWOW_DBC_SCHEMA(StableSlotPricesEntry,
  DBC_U32(id, 0)
  DBC_U32(cost, 1)
)

OPENWOW_DBC_SCHEMA(VehicleEntry,
    e.id = f.GetUInt32(row, 0);
    e.flags = f.GetUInt32(row, 1);
    e.turn_speed = f.GetFloat(row, 2);
    e.pitch_speed = f.GetFloat(row, 3);
    e.pitch_min = f.GetFloat(row, 4);
    e.pitch_max = f.GetFloat(row, 5);
    DBC_U32_ARRAY(seat_id, 6)
    e.mouse_look_offset_pitch = f.GetFloat(row, 14);
    e.camera_fade_dist_scalar_min = f.GetFloat(row, 15);
    e.camera_fade_dist_scalar_max = f.GetFloat(row, 16);
    e.camera_pitch_offset = f.GetFloat(row, 17);
    e.yaw_left_limit = f.GetFloat(row, 18);
    e.yaw_right_limit = f.GetFloat(row, 19);
    e.mssl_trgt_turn_lingering = f.GetFloat(row, 20);
    e.mssl_trgt_pitch_lingering = f.GetFloat(row, 21);
    e.mssl_trgt_mouse_lingering = f.GetFloat(row, 22);
    e.mssl_trgt_end_opacity = f.GetFloat(row, 23);
    e.mssl_trgt_arc_speed = f.GetFloat(row, 24);
    e.mssl_trgt_arc_repeat = f.GetFloat(row, 25);
    e.mssl_trgt_arc_width = f.GetFloat(row, 26);
    e.mssl_trgt_impact_radius[0] = f.GetFloat(row, 27);
    e.mssl_trgt_impact_radius[1] = f.GetFloat(row, 28);
    e.mssl_trgt_arc_texture = f.GetString(row, 29);
    e.mssl_trgt_impact_texture = f.GetString(row, 30);
    e.mssl_trgt_impact_model[0] = f.GetString(row, 31);
    e.mssl_trgt_impact_model[1] = f.GetString(row, 32);
    e.camera_yaw_offset = f.GetFloat(row, 33);
    e.ui_locomotion_type = f.GetUInt32(row, 34);
    e.mssl_trgt_impact_tex_radius = f.GetFloat(row, 35);
    e.vehicle_ui_indicator_id = f.GetUInt32(row, 36);
    DBC_U32_ARRAY(power_display_id, 37)
)

OPENWOW_DBC_SCHEMA(VehicleSeatEntry,
  DBC_U32(id, 0)
  DBC_U32(flags, 1)
  DBC_I32(attachment_id, 2)
  DBC_F32(attachment_offset_x, 3)
  DBC_F32(attachment_offset_y, 4)
  DBC_F32(attachment_offset_z, 5)
  DBC_F32(enter_pre_delay, 6)
  DBC_F32(enter_speed, 7)
  DBC_F32(enter_gravity, 8)
  DBC_F32(enter_min_duration, 9)
  DBC_F32(enter_max_duration, 10)
  DBC_F32(enter_min_arc_height, 11)
  DBC_F32(enter_max_arc_height, 12)
  DBC_I32(enter_anim_start, 13)
  DBC_I32(enter_anim_loop, 14)
  DBC_I32(ride_anim_start, 15)
  DBC_I32(ride_anim_loop, 16)
  DBC_I32(ride_upper_anim_start, 17)
  DBC_I32(ride_upper_anim_loop, 18)
  DBC_F32(exit_pre_delay, 19)
  DBC_F32(exit_speed, 20)
  DBC_F32(exit_gravity, 21)
  DBC_F32(exit_min_duration, 22)
  DBC_F32(exit_max_duration, 23)
  DBC_F32(exit_min_arc_height, 24)
  DBC_F32(exit_max_arc_height, 25)
  DBC_I32(exit_anim_start, 26)
  DBC_I32(exit_anim_loop, 27)
  DBC_I32(exit_anim_end, 28)
  DBC_F32(passenger_yaw, 29)
  DBC_F32(passenger_pitch, 30)
  DBC_F32(passenger_roll, 31)
  DBC_I32(passenger_attachment_id, 32)
  DBC_I32(vehicle_enter_anim, 33)
  DBC_I32(vehicle_exit_anim, 34)
  DBC_I32(vehicle_ride_anim_loop, 35)
  DBC_I32(vehicle_enter_anim_bone, 36)
  DBC_I32(vehicle_exit_anim_bone, 37)
  DBC_I32(vehicle_ride_anim_loop_bone, 38)
  DBC_F32(vehicle_enter_anim_delay, 39)
  DBC_F32(vehicle_exit_anim_delay, 40)
  DBC_U32(flags_b, 41)
  DBC_U32(enter_ui_sound_id, 42)
  DBC_U32(exit_ui_sound_id, 43)
  DBC_U32(temporary_portrait_type, 44)
  DBC_U32(transition_flags, 45)
  DBC_F32(camera_entering_delay, 46)
  DBC_F32(camera_entering_duration, 47)
  DBC_F32(camera_exiting_delay, 48)
  DBC_F32(camera_exiting_duration, 49)
  DBC_F32(camera_offset_x, 50)
  DBC_F32(camera_offset_y, 51)
  DBC_F32(camera_offset_z, 52)
  DBC_F32(aim_distance, 53)
  DBC_F32(camera_facing_chase_rate, 54)
  DBC_F32(vehicle_camera_clamp_distance, 55)
  DBC_F32(vehicle_camera_min_distance, 56)
  DBC_F32(vehicle_camera_max_distance, 57)
)

OPENWOW_DBC_SCHEMA(ItemSetEntry,
    e.id = f.GetUInt32(row, 0);
    e.name = f.GetLocalizedString(row, 1);
    DBC_U32_ARRAY(item_id, 18)
    for (int i = 0; i < 8; ++i) {
      e.bonus_spell[i] = f.GetUInt32(row, 35 + i);
      e.bonus_threshold[i] = f.GetUInt32(row, 43 + i);
    }
    e.required_skill = f.GetUInt32(row, 51);
    e.required_skill_rank = f.GetUInt32(row, 52);
)

OPENWOW_DBC_SCHEMA(TransportAnimationEntry,
  DBC_U32(id, 0)
  DBC_U32(transport_id, 1)
  DBC_U32(time_index, 2)
  DBC_F32(x, 3)
  DBC_F32(y, 4)
  DBC_F32(z, 5)
  DBC_U32(sequence_id, 6)
)

OPENWOW_DBC_SCHEMA(CharSectionsEntry,
  DBC_U32(id, 0)
  DBC_U32(race_id, 1)
  DBC_U32(sex_id, 2)
  DBC_U32(base_section, 3)
  DBC_STRING_ARRAY(texture_name, 4)
  DBC_U32(flags, 7)
  DBC_U32(type, 8)
  DBC_U32(variation, 9)
)

OPENWOW_DBC_SCHEMA(CharHairGeosetsEntry,
  DBC_U32(id, 0)
  DBC_U32(race_id, 1)
  DBC_U32(sex_id, 2)
  DBC_U32(variation_id, 3)
  DBC_U32(geoset_id, 4)
  DBC_U32(bald, 5)
)

CharBaseInfoEntry CharBaseInfoEntry::Load(const DbcFile &f, std::uint32_t row) {

  constexpr std::uint32_t kRaceOffset = 0u;
  constexpr std::uint32_t kClassOffset = 1u;

  CharBaseInfoEntry e{};
  e.race_id = f.GetByte(row, kRaceOffset);
  e.class_id = f.GetByte(row, kClassOffset);
  e.id = row;
  return e;
}

OPENWOW_DBC_SCHEMA(CharacterFacialHairStylesEntry,
    e.race_id = f.GetUInt32(row, 0);
    e.sex_id = f.GetUInt32(row, 1);
    e.variation_id = f.GetUInt32(row, 2);
    e.id = CharacterFacialHairStylesEntry::ComposeKey(
        e.race_id, e.sex_id, e.variation_id);
    DBC_U32_ARRAY(geoset, 3)
)

OPENWOW_DBC_SCHEMA(CreatureDisplayInfoExtraEntry,
  DBC_U32(id, 0)
  DBC_U32(display_race_id, 1)
  DBC_U32(display_sex_id, 2)
  DBC_U32(skin_id, 3)
  DBC_U32(face_id, 4)
  DBC_U32(hair_style_id, 5)
  DBC_U32(hair_color_id, 6)
  DBC_U32(facial_hair_id, 7)
  DBC_U32_ARRAY(item_display, 8)
  DBC_U32(flags, 19)
  DBC_STRING(bake_name, 20)
)

OPENWOW_DBC_SCHEMA(CreatureMovementInfoEntry,
  DBC_U32(id, 0)
  DBC_F32(smooth_facing_chase_rate, 1)
)

OPENWOW_DBC_SCHEMA(CreatureSoundDataEntry,
  DBC_U32(id, 0)
  DBC_U32(sound_exertion_id, 1)
  DBC_U32(sound_exertion_critical_id, 2)
  DBC_U32(sound_injury_id, 3)
  DBC_U32(sound_injury_critical_id, 4)
  DBC_U32(sound_injury_crushing_blow_id, 5)
  DBC_U32(sound_death_id, 6)
  DBC_U32(sound_stun_id, 7)
  DBC_U32(sound_stand_id, 8)
  DBC_U32(sound_footstep_id, 9)
  DBC_U32(sound_aggro_id, 10)
  DBC_U32(sound_wing_flap_id, 11)
  DBC_U32(sound_wing_glide_id, 12)
  DBC_U32(sound_alert_id, 13)
  DBC_U32(sound_fidget0, 14)
  DBC_U32(sound_fidget1, 15)
  DBC_U32(sound_fidget2, 16)
  DBC_U32(sound_fidget3, 17)
  DBC_U32(sound_fidget4, 18)
  DBC_U32(custom_attack0, 19)
  DBC_U32(custom_attack1, 20)
  DBC_U32(custom_attack2, 21)
  DBC_U32(custom_attack3, 22)
  DBC_U32(npc_sound_id, 23)
  DBC_U32(loop_sound_id, 24)
  DBC_U32(creature_impact_type, 25)
  DBC_U32(sound_jump_start_id, 26)
  DBC_U32(sound_jump_end_id, 27)
  DBC_U32(sound_pet_attack_id, 28)
  DBC_U32(sound_pet_order_id, 29)
  DBC_U32(sound_pet_dismiss_id, 30)
  DBC_F32(fidget_delay_seconds_min, 31)
  DBC_F32(fidget_delay_seconds_max, 32)
  DBC_U32(birth_sound_id, 33)
  DBC_U32(spell_cast_directed_sound_id, 34)
  DBC_U32(submerge_sound_id, 35)
  DBC_U32(submerged_sound_id, 36)
  DBC_U32(creature_sound_data_id_pet, 37)
)

OPENWOW_DBC_SCHEMA(SoundAmbienceEntry,
  DBC_U32(id, 0)
  DBC_U32(ambience_day, 1)
  DBC_U32(ambience_night, 2)
)

OPENWOW_DBC_SCHEMA(ZoneMusicEntry,
    e.id = f.GetUInt32(row, 0);
    e.set_name = f.GetString(row, 1);
    for (std::uint32_t i = 0; i < e.sounds.size(); ++i) {
      e.silence_interval_min[i] = f.GetUInt32(row, 2 + i);
      e.silence_interval_max[i] = f.GetUInt32(row, 4 + i);
      e.sounds[i] = f.GetUInt32(row, 6 + i);
    }
)

OPENWOW_DBC_SCHEMA(ZoneIntroMusicTableEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
  DBC_U32(sound_id, 2)
  DBC_U32(priority, 3)
  DBC_U32(min_delay_minutes, 4)
)

OPENWOW_DBC_SCHEMA(WeatherEntry,
  DBC_U32(id, 0)
  DBC_U32(ambience_id, 1)
  DBC_U32(effect_type, 2)
  DBC_F32(effect_color_r, 3)
  DBC_F32(effect_color_g, 4)
  DBC_F32(effect_color_b, 5)
  DBC_U32(transition_sky_box, 6)
  DBC_STRING(effect_texture, 7)
)

OPENWOW_DBC_SCHEMA(WorldMapContinentEntry,
  DBC_U32(id, 0)
  DBC_U32(map_id, 1)
  DBC_U32(left_boundary, 2)
  DBC_U32(right_boundary, 3)
  DBC_U32(top_boundary, 4)
  DBC_U32(bottom_boundary, 5)
  DBC_F32(continent_offset_x, 6)
  DBC_F32(continent_offset_y, 7)
  DBC_F32(scale, 8)
  DBC_F32(taxi_min_x, 9)
  DBC_F32(taxi_min_y, 10)
  DBC_F32(taxi_max_x, 11)
  DBC_F32(taxi_max_y, 12)
  DBC_U32(world_map_id, 13)
)

OPENWOW_DBC_SCHEMA(WorldMapTransformsEntry,
  DBC_U32(id, 0)
  DBC_U32(map_id, 1)
  DBC_F32(region_min_x, 2)
  DBC_F32(region_min_y, 3)
  DBC_F32(region_max_x, 4)
  DBC_F32(region_max_y, 5)
  DBC_U32(new_map_id, 6)
  DBC_F32(region_offset_x, 7)
  DBC_F32(region_offset_y, 8)
  DBC_I32(new_dungeon_map_id, 9)
)

OPENWOW_DBC_SCHEMA(WorldStateUIEntry,
  DBC_U32(id, 0)
  DBC_I32(map_id, 1)
  DBC_U32(area_id, 2)
  DBC_U32(phase_shift, 3)
  DBC_STRING(icon, 4)
  DBC_LOCALIZED(text, 5)
  DBC_LOCALIZED(tooltip, 22)
  DBC_U32(world_state_id, 39)
  DBC_U32(type, 40)
  DBC_STRING(dynamic_icon, 41)
  DBC_LOCALIZED(dynamic_tooltip, 42)
  DBC_STRING(extended_ui, 59)
  DBC_U32(extended_ui_state_variable0, 60)
  DBC_U32(extended_ui_state_variable1, 61)
  DBC_U32(extended_ui_state_variable2, 62)
)

OPENWOW_DBC_SCHEMA(WorldStateZoneSoundsEntry,
  DBC_ROW_ID()
  DBC_U32(world_state_id, 0)
  DBC_U32(world_state_value, 1)
  DBC_U32(area_id, 2)
  DBC_U32(wmo_area_id, 3)
  DBC_U32(sound_ambience_id, 4)
  DBC_U32(zone_music_id, 5)
  DBC_U32(zone_intro_music_id, 6)
  DBC_U32(sound_provider_preferences_id, 7)
)

OPENWOW_DBC_SCHEMA(SoundEntriesAdvancedEntry,
  DBC_U32(id, 0)
  DBC_U32(sound_entry_id, 1)
  DBC_F32(inner_radius_2d, 2)
  DBC_U32(time_a_ms, 3)
  DBC_U32(time_b_ms, 4)
  DBC_U32(time_c_ms, 5)
  DBC_U32(time_d_ms, 6)
  DBC_U32(random_offset_range_ms, 7)
  DBC_U32(usage, 8)
  DBC_U32(time_interval_min_ms, 9)
  DBC_U32(time_interval_max_ms, 10)
  DBC_U32(volume_slider_category, 11)
  DBC_F32(duck_to_sfx, 12)
  DBC_F32(duck_to_music, 13)
  DBC_F32(duck_to_ambience, 14)
  DBC_F32(inner_radius_of_influence, 15)
  DBC_F32(outer_radius_of_influence, 16)
  DBC_U32(time_to_duck_ms, 17)
  DBC_U32(time_to_unduck_ms, 18)
  DBC_F32(inside_angle, 19)
  DBC_F32(outside_angle, 20)
  DBC_F32(outside_volume, 21)
  DBC_F32(outer_radius_2d, 22)
  DBC_STRING(name, 23)
)

OPENWOW_DBC_SCHEMA(SoundFilterEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
)

OPENWOW_DBC_SCHEMA(SoundFilterElemEntry,
  DBC_U32(id, 0)
  DBC_U32(sound_filter_id, 1)
  DBC_U32(order_index, 2)
  DBC_U32(filter_type, 3)
  DBC_F32_ARRAY(params, 4)
)

OPENWOW_DBC_SCHEMA(SoundProviderPreferencesEntry,
  DBC_U32(id, 0)
  DBC_STRING(description, 1)
  DBC_U32(flags, 2)
  DBC_I32(room_flags, 3)
  DBC_F32(decay_time, 4)
  DBC_F32(environment_size, 5)
  DBC_F32(environment_diffusion, 6)
  DBC_I32(room, 7)
  DBC_I32(room_hf, 8)
  DBC_F32(decay_hf_ratio, 9)
  DBC_I32(reflections, 10)
  DBC_F32(reflections_delay, 11)
  DBC_I32(reverb, 12)
  DBC_F32(reverb_delay, 13)
  DBC_F32(room_rolloff_factor, 14)
  DBC_F32(air_absorption_hf, 15)
  DBC_I32(room_lf, 16)
  DBC_F32(decay_lf_ratio, 17)
  DBC_F32(echo_time, 18)
  DBC_F32(echo_depth, 19)
  DBC_F32(modulation_time, 20)
  DBC_F32(modulation_depth, 21)
  DBC_F32(hf_reference, 22)
  DBC_F32(lf_reference, 23)
)

OPENWOW_DBC_SCHEMA(SoundWaterTypeEntry,
  DBC_U32(id, 0)
  DBC_U32(sound_type, 1)
  DBC_U32(sound_subtype, 2)
  DBC_U32(sound_id, 3)
)

OPENWOW_DBC_SCHEMA(SoundEmittersEntry,
  DBC_U32(id, 0)
  DBC_F32(x, 1)
  DBC_F32(y, 2)
  DBC_F32(z, 3)
  DBC_F32(dir_x, 4)
  DBC_F32(dir_y, 5)
  DBC_F32(dir_z, 6)
  DBC_U32(sound_entries_id, 7)
  DBC_U32(map_id, 8)
  DBC_STRING(name, 9)
)

OPENWOW_DBC_SCHEMA(LiquidMaterialEntry,
  DBC_U32(id, 0)
  DBC_U32(lvf, 1)
  DBC_U32(flags, 2)
)

OPENWOW_DBC_SCHEMA(LoadingScreenTaxiSplinesEntry,
    e.id = f.GetUInt32(row, 0);
    e.taxi_path_id = f.GetUInt32(row, 1);
    for (std::size_t i = 0; i < 8; ++i)
      e.loc_x[i] = f.GetFloat(row, static_cast<std::uint32_t>(i + 2));
    for (std::size_t i = 0; i < 8; ++i)
      e.loc_y[i] = f.GetFloat(row, static_cast<std::uint32_t>(i + 10));
    e.leg_index = f.GetUInt32(row, 18);
)

OPENWOW_DBC_SCHEMA(TerrainTypeEntry,
  DBC_U32(id, 0)
  DBC_STRING(description, 1)
  DBC_U32(footstep_spray_run, 2)
  DBC_U32(footstep_spray_walk, 3)
  DBC_U32(sound_id, 4)
  DBC_U32(flags, 5)
)

OPENWOW_DBC_SCHEMA(TerrainTypeSoundsEntry,
  DBC_U32(id, 0)
)

OPENWOW_DBC_SCHEMA(FootprintTexturesEntry,
  DBC_U32(id, 0)
  DBC_STRING(texture_path, 1)
)

OPENWOW_DBC_SCHEMA(FootstepTerrainLookupEntry,
  DBC_U32(id, 0)
  DBC_U32(creature_footstep_id, 1)
  DBC_U32(terrain_sound_id, 2)
  DBC_U32(sound_id, 3)
  DBC_U32(sound_id_splash, 4)
)

OPENWOW_DBC_SCHEMA(CameraShakesEntry,
  DBC_U32(id, 0)
  DBC_U32(shake_type, 1)
  DBC_U32(direction, 2)
  DBC_F32(amplitude, 3)
  DBC_F32(frequency, 4)
  DBC_F32(duration, 5)
  DBC_F32(phase, 6)
  DBC_F32(coefficient, 7)
)

OPENWOW_DBC_SCHEMA(ScreenEffectEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
  DBC_U32(effect, 2)
  DBC_U32_ARRAY(param, 3)
  DBC_U32(light_params_slot, 7)
  DBC_U32(sound_ambience_id, 8)
  DBC_U32(zone_music_id, 9)
)

OPENWOW_DBC_SCHEMA(DestructibleModelDataEntry,
  DBC_U32(id, 0)
  DBC_U32(state0_impact_effect_doodad_set, 1)
  DBC_U32(state0_ambient_doodad_set, 2)
  DBC_U32(state1_wmo_display_id, 3)
  DBC_U32(state1_destruction_doodad_set, 4)
  DBC_U32(state1_impact_effect_doodad_set, 5)
  DBC_U32(state1_ambient_doodad_set, 6)
  DBC_U32(state2_wmo_display_id, 7)
  DBC_U32(state2_destruction_doodad_set, 8)
  DBC_U32(state2_impact_effect_doodad_set, 9)
  DBC_U32(state2_ambient_doodad_set, 10)
  DBC_U32(state3_wmo_display_id, 11)
  DBC_U32(state3_init_doodad_set, 12)
  DBC_U32(state3_ambient_doodad_set, 13)
  DBC_U32(eject_direction, 14)
  DBC_U32(rebuild_effect_display_id, 15)
  DBC_U32(field_40, 16)
  DBC_U32(rebuild_transition_mode, 17)
  DBC_U32(rebuild_transition_speed, 18)
)

OPENWOW_DBC_SCHEMA(HelmetGeosetVisDataEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(hide_geoset, 1)
)

OPENWOW_DBC_SCHEMA(UnitBloodEntry,
  DBC_U32(id, 0)
  DBC_U32(combat_blood_spurt_front0, 1)
  DBC_U32(combat_blood_spurt_front1, 2)
  DBC_U32(combat_blood_spurt_back0, 3)
  DBC_U32(combat_blood_spurt_back1, 4)
  DBC_STRING_ARRAY(ground_blood, 5)
)

OPENWOW_DBC_SCHEMA(UnitBloodLevelsEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(violence_level, 1)
)

OPENWOW_DBC_SCHEMA(VocalUISoundsEntry,
  DBC_U32(id, 0)
  DBC_U32(vocal_ui_enum, 1)
  DBC_U32(race_id, 2)
  DBC_U32(normal_sound_m, 3)
  DBC_U32(normal_sound_f, 4)
  DBC_U32(pissed_sound_m, 5)
  DBC_U32(pissed_sound_f, 6)
)

OPENWOW_DBC_SCHEMA(UISoundLookupsEntry,
  DBC_U32(id, 0)
  DBC_U32(sound_id, 1)
  DBC_STRING(lookup_name, 2)
)

OPENWOW_DBC_SCHEMA(WeaponImpactSoundsEntry,
    e.id = f.GetUInt32(row, 0);
    e.weapon_subclass_id = f.GetUInt32(row, 1);
    e.parry_type = f.GetUInt32(row, 2);
    for (int i = 0; i < 10; ++i) {
      e.impact_sound[i] = f.GetUInt32(row, 3 + i);
      e.crit_impact_sound[i] = f.GetUInt32(row, 13 + i);
    }
)

OPENWOW_DBC_SCHEMA(SheatheSoundLookupsEntry,
  DBC_U32(id, 0)
  DBC_U32(class_id, 1)
  DBC_U32(subclass_id, 2)
  DBC_U32(material, 3)
  DBC_U32(check_material, 4)
  DBC_U32(sheathe_sound, 5)
  DBC_U32(unsheathe_sound, 6)
)

OPENWOW_DBC_SCHEMA(NPCSoundsEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(sound, 1)
)

OPENWOW_DBC_SCHEMA(DeathThudLookupsEntry,
  DBC_U32(id, 0)
  DBC_U32(size_class, 1)
  DBC_U32(terrain_type_sound, 2)
  DBC_U32(sound_entry, 3)
  DBC_U32(sound_entry_water, 4)
)

OPENWOW_DBC_SCHEMA(ParticleColorEntry,
    e.id = f.GetUInt32(row, 0);
    for (int i = 0; i < 3; ++i) {
      e.start[i] = f.GetUInt32(row, 1 + i);
      e.mid[i] = f.GetUInt32(row, 4 + i);
      e.end[i] = f.GetUInt32(row, 7 + i);
    }
)

OPENWOW_DBC_SCHEMA(PaperDollItemFrameEntry,
  DBC_ROW_ID()
  DBC_STRING(item_button_name, 0)
  DBC_STRING(slot_icon, 1)
  DBC_U32(slot_number, 2)
)

OPENWOW_DBC_SCHEMA(PageTextMaterialEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
)

OPENWOW_DBC_SCHEMA(MaterialEntry,
  DBC_U32(id, 0)
  DBC_U32(flags, 1)
  DBC_U32(foley_sound, 2)
  DBC_U32(sheathe_sound, 3)
  DBC_U32(unsheathe_sound, 4)
)

OPENWOW_DBC_SCHEMA(ObjectEffectEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
  DBC_U32(object_effect_group_id, 2)
  DBC_U32(trigger_type, 3)
  DBC_U32(event_type, 4)
  DBC_U32(effect_rec_type, 5)
  DBC_U32(effect_rec_id, 6)
  DBC_U32(attachment, 7)
  DBC_F32(offset_x, 8)
  DBC_F32(offset_y, 9)
  DBC_F32(offset_z, 10)
  DBC_U32(object_effect_modifier_id, 11)
)

OPENWOW_DBC_SCHEMA(ObjectEffectGroupEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
)

OPENWOW_DBC_SCHEMA(ObjectEffectModifierEntry,
    e.id = f.GetUInt32(row, 0);
    e.input_type = f.GetUInt32(row, 1);
    e.map_type = f.GetUInt32(row, 2);
    e.output_type = f.GetUInt32(row, 3);
    e.map_params[0] = f.GetFloat(row, 4);
    e.map_params[1] = f.GetFloat(row, 5);
    e.map_params[2] = f.GetFloat(row, 6);
    e.map_params[3] = f.GetFloat(row, 7);
)

OPENWOW_DBC_SCHEMA(ObjectEffectPackageEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
)

OPENWOW_DBC_SCHEMA(ObjectEffectPackageElemEntry,
  DBC_U32(id, 0)
  DBC_U32(object_effect_package_id, 1)
  DBC_U32(object_effect_group_id, 2)
  DBC_U32(state_type, 3)
)

OPENWOW_DBC_SCHEMA(TransportPhysicsEntry,
  DBC_U32(id, 0)
  DBC_F32(wave_amp, 1)
  DBC_F32(wave_time_scale, 2)
  DBC_F32(roll_amp, 3)
  DBC_F32(roll_time_scale, 4)
  DBC_F32(pitch_amp, 5)
  DBC_F32(pitch_time_scale, 6)
  DBC_F32(max_bank, 7)
  DBC_F32(max_bank_turn_speed, 8)
  DBC_F32(speed_damp_thresh, 9)
  DBC_F32(speed_damp, 10)
)

OPENWOW_DBC_SCHEMA(TransportRotationEntry,
  DBC_U32(id, 0)
  DBC_U32(transport_id, 1)
  DBC_U32(time_index, 2)
  DBC_F32(x, 3)
  DBC_F32(y, 4)
  DBC_F32(z, 5)
  DBC_F32(w, 6)
)

OPENWOW_DBC_SCHEMA(VehicleUIIndicatorEntry,
  DBC_U32(id, 0)
  DBC_STRING(background_texture, 1)
)

OPENWOW_DBC_SCHEMA(VehicleUIIndSeatEntry,
  DBC_U32(id, 0)
  DBC_U32(vehicle_ui_indicator_id, 1)
  DBC_U32(virtual_seat_index, 2)
  DBC_F32(x_pos, 3)
  DBC_F32(y_pos, 4)
)

OPENWOW_DBC_SCHEMA(MovieFileDataEntry,
  DBC_U32(id, 0)
  DBC_U32(width, 1)
)

OPENWOW_DBC_SCHEMA(MovieVariationEntry,
  DBC_U32(id, 0)
  DBC_U32(movie_id, 1)
  DBC_U32(file_data_id, 2)
)

OPENWOW_DBC_SCHEMA(AttackAnimKitsEntry,
  DBC_U32(id, 0)
  DBC_U32(field_04, 1)
  DBC_U32(field_08, 2)
  DBC_U32(field_0C, 3)
  DBC_U32(field_10, 4)
)

OPENWOW_DBC_SCHEMA(AttackAnimTypesEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
)

OPENWOW_DBC_SCHEMA(DungeonMapEntry,
  DBC_U32(id, 0)
  DBC_U32(map_id, 1)
  DBC_U32(floor_index, 2)
  DBC_F32(min_x, 3)
  DBC_F32(max_x, 4)
  DBC_F32(min_y, 5)
  DBC_F32(max_y, 6)
  DBC_U32(parent_world_map_id, 7)
)

OPENWOW_DBC_SCHEMA(DungeonMapChunkEntry,
  DBC_U32(id, 0)
  DBC_U32(map_id, 1)
  DBC_U32(wmo_group_id, 2)
  DBC_U32(dungeon_map_id, 3)
  DBC_F32(min_z, 4)
)

OPENWOW_DBC_SCHEMA(WorldChunkSoundsEntry,
    e.id = row;
    for (std::uint32_t column = 0; column < e.columns.size(); ++column) {
      e.columns[column] = f.GetUInt32(row, column);
    }
)

OPENWOW_DBC_SCHEMA(GameObjectArtKitEntry,
    e.id = f.GetUInt32(row, 0);
    for (std::size_t i = 0; i < e.strings.size(); ++i)
      e.strings[i] = f.GetString(row, static_cast<std::uint32_t>(i + 1));
)

OPENWOW_DBC_SCHEMA(SoundSamplePreferencesEntry,
  DBC_U32(id, 0)
  DBC_U32(unk1, 1)
  DBC_U32(unk2, 2)
  DBC_U32(eax_environment, 3)
  DBC_F32(eax_decay_time, 4)
  DBC_F32(eax_env_size, 5)
  DBC_F32(eax_env_diffusion, 6)
  DBC_I32(eax_room, 7)
  DBC_I32(eax_room_hf, 8)
  DBC_F32(eax_room_rolloff, 9)
  DBC_U32(field_10, 10)
  DBC_F32(field_11, 11)
  DBC_U32(field_12, 12)
  DBC_F32(field_13, 13)
  DBC_F32(field_14, 14)
  DBC_F32(field_15, 15)
  DBC_U32(field_16, 16)
)

OPENWOW_DBC_SCHEMA(WeaponSwingSounds2Entry,
  DBC_U32(id, 0)
  DBC_U32(swing_type, 1)
  DBC_U32(crit, 2)
  DBC_U32(sound_id, 3)
)

}

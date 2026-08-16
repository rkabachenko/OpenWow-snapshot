#pragma once

#include "openwow/data/formats/dbc/dbc_file.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::data::dbc {

struct BarberShopStyleEntry {
  std::uint32_t id;
  std::uint32_t type;
  std::string_view name;
  std::string_view description;
  float cost_modifier;
  std::uint32_t race;
  std::uint32_t sex;
  std::uint32_t data;

  static BarberShopStyleEntry Load(const DbcFile &f, std::uint32_t row);
};

struct WorldMapOverlayEntry {
  std::uint32_t id;
  std::uint32_t map_area_id;
  std::array<std::uint32_t, 4> area_id;
  std::uint32_t map_point_x;
  std::uint32_t map_point_y;
  std::string_view texture_name;
  std::uint32_t texture_width;
  std::uint32_t texture_height;
  std::uint32_t offset_x;
  std::uint32_t offset_y;
  std::uint32_t hit_rect_top;
  std::uint32_t hit_rect_left;
  std::uint32_t hit_rect_bottom;
  std::uint32_t hit_rect_right;

  static WorldMapOverlayEntry Load(const DbcFile &f, std::uint32_t row);
};

struct WorldSafeLocsEntry {
  std::uint32_t id;
  std::uint32_t map_id;
  float x;
  float y;
  float z;
  std::string_view name;

  static WorldSafeLocsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct LightSkyboxEntry {
  std::uint32_t id;
  std::string_view name;
  std::uint32_t flags;

  static LightSkyboxEntry Load(const DbcFile &f, std::uint32_t row);
};

struct GroundEffectTextureEntry {

  std::uint32_t id;
  std::array<std::uint32_t, 4> doodad_id;
  std::array<std::int32_t, 4> doodad_weight;
  std::uint32_t sound;
  std::uint32_t density;

  static GroundEffectTextureEntry Load(const DbcFile &f, std::uint32_t row);
};

struct GroundEffectDoodadEntry {

  std::uint32_t id;
  std::string_view doodad_path;
  std::uint32_t flags;

  static GroundEffectDoodadEntry Load(const DbcFile &f, std::uint32_t row);
};

struct WMOAreaTableEntry {

  std::uint32_t id;
  std::uint32_t wmo_id;
  std::uint32_t name_set_id;
  std::int32_t wmo_group_id;
  std::uint32_t sound_pref;
  std::uint32_t sound_pref_uw;
  std::uint32_t ambience_id;
  std::uint32_t zone_music;
  std::uint32_t intro_sound;
  std::uint32_t flags;
  std::uint32_t area_table_id;
  std::string_view name;

  static WMOAreaTableEntry Load(const DbcFile &f, std::uint32_t row);
};

struct AreaTriggerEntry {

  std::uint32_t id;
  std::uint32_t map_id;
  float x;
  float y;
  float z;
  float radius;
  float box_length;
  float box_width;
  float box_height;
  float box_yaw;

  static AreaTriggerEntry Load(const DbcFile &f, std::uint32_t row);
};

struct BattlemasterListEntry {

  std::uint32_t id;
  std::array<std::int32_t, 8> map_id;
  std::uint32_t instance_type;
  std::uint32_t groups_allowed;
  std::string_view name;
  std::uint32_t max_group_size;
  std::uint32_t holiday_world_state;
  std::uint32_t min_level;
  std::uint32_t max_level;

  static BattlemasterListEntry Load(const DbcFile &f, std::uint32_t row);
};

struct DurabilityCostsEntry {
  std::uint32_t id;
  std::array<std::uint32_t, 29> cost;

  static DurabilityCostsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct DurabilityQualityEntry {

  std::uint32_t id;
  float quality_mod;

  static DurabilityQualityEntry Load(const DbcFile &f, std::uint32_t row);
};

struct LiquidTypeEntry {

  std::uint32_t id;
  std::string_view name;
  std::uint32_t flags;
  std::uint32_t sound_bank;
  std::uint32_t sound_id;
  std::uint32_t spell_id;
  float max_darken_depth;
  float fog_darken_intensity;
  float amb_darken_intensity;
  float dir_darken_intensity;
  std::uint32_t light_id;
  float particle_scale;
  std::uint32_t particle_movement;
  std::uint32_t particle_tex_slots;
  std::uint32_t liquid_material_id;
  std::array<std::string_view, 6> textures;
  std::array<std::uint32_t, 2> colors;
  std::array<float, 18> float_params;
  std::array<std::uint32_t, 4> int_params;

  static LiquidTypeEntry Load(const DbcFile &f, std::uint32_t row);
};

struct MovieEntry {
  std::uint32_t id;
  std::string_view filename;
  std::uint32_t volume;

  static MovieEntry Load(const DbcFile &f, std::uint32_t row);
};

struct StableSlotPricesEntry {
  std::uint32_t id;
  std::uint32_t cost;

  static StableSlotPricesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct VehicleEntry {
  std::uint32_t id;
  std::uint32_t flags;
  float turn_speed;
  float pitch_speed;
  float pitch_min;
  float pitch_max;
  std::array<std::uint32_t, 8> seat_id;
  float mouse_look_offset_pitch;
  float camera_fade_dist_scalar_min;
  float camera_fade_dist_scalar_max;
  float camera_pitch_offset;
  float yaw_left_limit;
  float yaw_right_limit;
  float mssl_trgt_turn_lingering;
  float mssl_trgt_pitch_lingering;
  float mssl_trgt_mouse_lingering;
  float mssl_trgt_end_opacity;
  float mssl_trgt_arc_speed;
  float mssl_trgt_arc_repeat;
  float mssl_trgt_arc_width;
  std::array<float, 2> mssl_trgt_impact_radius;
  std::string mssl_trgt_arc_texture;
  std::string mssl_trgt_impact_texture;
  std::array<std::string, 2> mssl_trgt_impact_model;
  float camera_yaw_offset;
  std::uint32_t ui_locomotion_type;
  float mssl_trgt_impact_tex_radius;
  std::uint32_t vehicle_ui_indicator_id;
  std::array<std::uint32_t, 3> power_display_id;

  static VehicleEntry Load(const DbcFile &f, std::uint32_t row);
};

struct VehicleSeatEntry {
  std::uint32_t id;
  std::uint32_t flags;
  std::int32_t  attachment_id;
  float attachment_offset_x;
  float attachment_offset_y;
  float attachment_offset_z;
  float enter_pre_delay;
  float enter_speed;
  float enter_gravity;
  float enter_min_duration;
  float enter_max_duration;
  float enter_min_arc_height;
  float enter_max_arc_height;
  std::int32_t  enter_anim_start;
  std::int32_t  enter_anim_loop;
  std::int32_t  ride_anim_start;
  std::int32_t  ride_anim_loop;
  std::int32_t  ride_upper_anim_start;
  std::int32_t  ride_upper_anim_loop;
  float exit_pre_delay;
  float exit_speed;
  float exit_gravity;
  float exit_min_duration;
  float exit_max_duration;
  float exit_min_arc_height;
  float exit_max_arc_height;
  std::int32_t  exit_anim_start;
  std::int32_t  exit_anim_loop;
  std::int32_t  exit_anim_end;
  float passenger_yaw;
  float passenger_pitch;
  float passenger_roll;
  std::int32_t  passenger_attachment_id;
  std::int32_t  vehicle_enter_anim;
  std::int32_t  vehicle_exit_anim;
  std::int32_t  vehicle_ride_anim_loop;
  std::int32_t  vehicle_enter_anim_bone;
  std::int32_t  vehicle_exit_anim_bone;
  std::int32_t  vehicle_ride_anim_loop_bone;
  float vehicle_enter_anim_delay;
  float vehicle_exit_anim_delay;
  std::uint32_t flags_b;
  std::uint32_t enter_ui_sound_id;
  std::uint32_t exit_ui_sound_id;
  std::uint32_t temporary_portrait_type;
  std::uint32_t transition_flags;
  float camera_entering_delay;
  float camera_entering_duration;
  float camera_exiting_delay;
  float camera_exiting_duration;
  float camera_offset_x;
  float camera_offset_y;
  float camera_offset_z;
  float aim_distance;
  float camera_facing_chase_rate;
  float vehicle_camera_clamp_distance;
  float vehicle_camera_min_distance;
  float vehicle_camera_max_distance;

  static VehicleSeatEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemSetEntry {

  std::uint32_t id;
  std::string_view name;
  std::array<std::uint32_t, 17> item_id;
  std::array<std::uint32_t, 8> bonus_spell;
  std::array<std::uint32_t, 8> bonus_threshold;
  std::uint32_t required_skill;
  std::uint32_t required_skill_rank;

  static ItemSetEntry Load(const DbcFile &f, std::uint32_t row);
};

struct TransportAnimationEntry {
  std::uint32_t id;
  std::uint32_t transport_id;
  std::uint32_t time_index;
  float x;
  float y;
  float z;
  std::uint32_t sequence_id;

  static TransportAnimationEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CharSectionsEntry {
  std::uint32_t id;
  std::uint32_t race_id;
  std::uint32_t sex_id;
  std::uint32_t base_section;
  std::array<std::string_view, 3> texture_name;
  std::uint32_t flags;
  std::uint32_t type;
  std::uint32_t variation;

  static CharSectionsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CharHairGeosetsEntry {
  std::uint32_t id;
  std::uint32_t race_id;
  std::uint32_t sex_id;
  std::uint32_t variation_id;
  std::uint32_t geoset_id;
  std::uint32_t bald;

  static CharHairGeosetsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CharBaseInfoEntry {
  static constexpr std::uint32_t kRetailFieldCount = 2u;
  static constexpr std::uint32_t kRetailRecordSize = 2u;

  std::uint32_t id;
  std::uint32_t race_id;
  std::uint32_t class_id;

  static CharBaseInfoEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CharacterFacialHairStylesEntry {
  static constexpr std::uint32_t kSexKeyStride = 100u;
  static constexpr std::uint32_t kRaceKeyStride = 10000u;

  [[nodiscard]] static constexpr std::uint32_t ComposeKey(
      const std::uint32_t race_id, const std::uint32_t sex_id,
      const std::uint32_t variation_id) {
    return race_id * kRaceKeyStride + sex_id * kSexKeyStride + variation_id;
  }

  std::uint32_t id;
  std::uint32_t race_id;
  std::uint32_t sex_id;
  std::uint32_t variation_id;
  std::array<std::uint32_t, 5> geoset;

  static CharacterFacialHairStylesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CreatureDisplayInfoExtraEntry {

  std::uint32_t id;
  std::uint32_t display_race_id;
  std::uint32_t display_sex_id;
  std::uint32_t skin_id;
  std::uint32_t face_id;
  std::uint32_t hair_style_id;
  std::uint32_t hair_color_id;
  std::uint32_t facial_hair_id;
  std::array<std::uint32_t, 11> item_display;
  std::uint32_t flags;
  std::string_view bake_name;

  static CreatureDisplayInfoExtraEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CreatureMovementInfoEntry {

  std::uint32_t id;
  float smooth_facing_chase_rate;

  static CreatureMovementInfoEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CreatureSoundDataEntry {

  std::uint32_t id;
  std::uint32_t sound_exertion_id;
  std::uint32_t sound_exertion_critical_id;
  std::uint32_t sound_injury_id;
  std::uint32_t sound_injury_critical_id;
  std::uint32_t sound_injury_crushing_blow_id;
  std::uint32_t sound_death_id;
  std::uint32_t sound_stun_id;
  std::uint32_t sound_stand_id;
  std::uint32_t sound_footstep_id;
  std::uint32_t sound_aggro_id;
  std::uint32_t sound_wing_flap_id;
  std::uint32_t sound_wing_glide_id;
  std::uint32_t sound_alert_id;
  std::uint32_t sound_fidget0;
  std::uint32_t sound_fidget1;
  std::uint32_t sound_fidget2;
  std::uint32_t sound_fidget3;
  std::uint32_t sound_fidget4;
  std::uint32_t custom_attack0;
  std::uint32_t custom_attack1;
  std::uint32_t custom_attack2;
  std::uint32_t custom_attack3;
  std::uint32_t npc_sound_id;
  std::uint32_t loop_sound_id;
  std::uint32_t creature_impact_type;
  std::uint32_t sound_jump_start_id;
  std::uint32_t sound_jump_end_id;
  std::uint32_t sound_pet_attack_id;
  std::uint32_t sound_pet_order_id;
  std::uint32_t sound_pet_dismiss_id;
  float fidget_delay_seconds_min;
  float fidget_delay_seconds_max;
  std::uint32_t birth_sound_id;
  std::uint32_t spell_cast_directed_sound_id;
  std::uint32_t submerge_sound_id;
  std::uint32_t submerged_sound_id;
  std::uint32_t creature_sound_data_id_pet;

  static CreatureSoundDataEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SoundAmbienceEntry {

  std::uint32_t id;
  std::uint32_t ambience_day;
  std::uint32_t ambience_night;

  static SoundAmbienceEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ZoneMusicEntry {
  std::uint32_t id;
  std::string_view set_name;
  std::array<std::uint32_t, 2> silence_interval_min;
  std::array<std::uint32_t, 2> silence_interval_max;
  std::array<std::uint32_t, 2> sounds;

  static ZoneMusicEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ZoneIntroMusicTableEntry {
  std::uint32_t id;
  std::string_view name;
  std::uint32_t sound_id;
  std::uint32_t priority;
  std::uint32_t min_delay_minutes;

  static ZoneIntroMusicTableEntry Load(const DbcFile &f, std::uint32_t row);
};

struct WeatherEntry {
  std::uint32_t id;
  std::uint32_t ambience_id;
  std::uint32_t effect_type;
  float effect_color_r;
  float effect_color_g;
  float effect_color_b;
  std::uint32_t transition_sky_box;
  std::string_view effect_texture;

  static WeatherEntry Load(const DbcFile &f, std::uint32_t row);
};

struct WorldMapContinentEntry {
  std::uint32_t id;
  std::uint32_t map_id;
  std::uint32_t left_boundary;
  std::uint32_t right_boundary;
  std::uint32_t top_boundary;
  std::uint32_t bottom_boundary;
  float continent_offset_x;
  float continent_offset_y;
  float scale;
  float taxi_min_x;
  float taxi_min_y;
  float taxi_max_x;
  float taxi_max_y;
  std::uint32_t world_map_id;

  static WorldMapContinentEntry Load(const DbcFile &f, std::uint32_t row);
};

struct WorldMapTransformsEntry {
  std::uint32_t id;
  std::uint32_t map_id;
  float region_min_x;
  float region_min_y;
  float region_max_x;
  float region_max_y;
  std::uint32_t new_map_id;
  float region_offset_x;
  float region_offset_y;
  std::int32_t new_dungeon_map_id;

  static WorldMapTransformsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct WorldStateUIEntry {
  std::uint32_t id;
  std::int32_t map_id;
  std::uint32_t area_id;
  std::uint32_t phase_shift;
  std::string_view icon;
  std::string_view text;
  std::string_view tooltip;
  std::uint32_t world_state_id;
  std::uint32_t type;
  std::string_view dynamic_icon;
  std::string_view dynamic_tooltip;
  std::string_view extended_ui;
  std::uint32_t extended_ui_state_variable0;
  std::uint32_t extended_ui_state_variable1;
  std::uint32_t extended_ui_state_variable2;

  static WorldStateUIEntry Load(const DbcFile &f, std::uint32_t row);
};

struct WorldStateZoneSoundsEntry {
  std::uint32_t id;
  std::uint32_t world_state_id;
  std::uint32_t world_state_value;
  std::uint32_t area_id;
  std::uint32_t wmo_area_id;
  std::uint32_t sound_ambience_id;
  std::uint32_t zone_music_id;
  std::uint32_t zone_intro_music_id;
  std::uint32_t sound_provider_preferences_id;

  static WorldStateZoneSoundsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SoundEntriesAdvancedEntry {
  std::uint32_t id;
  std::uint32_t sound_entry_id;
  float inner_radius_2d;
  std::uint32_t time_a_ms;
  std::uint32_t time_b_ms;
  std::uint32_t time_c_ms;
  std::uint32_t time_d_ms;
  std::uint32_t random_offset_range_ms;
  std::uint32_t usage;
  std::uint32_t time_interval_min_ms;
  std::uint32_t time_interval_max_ms;
  std::uint32_t volume_slider_category;
  float duck_to_sfx;
  float duck_to_music;
  float duck_to_ambience;
  float inner_radius_of_influence;
  float outer_radius_of_influence;
  std::uint32_t time_to_duck_ms;
  std::uint32_t time_to_unduck_ms;
  float inside_angle;
  float outside_angle;
  float outside_volume;
  float outer_radius_2d;
  std::string_view name;

  static SoundEntriesAdvancedEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SoundFilterEntry {
  std::uint32_t id;
  std::string_view name;

  static SoundFilterEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SoundFilterElemEntry {

  std::uint32_t id;
  std::uint32_t sound_filter_id;
  std::uint32_t order_index;
  std::uint32_t filter_type;
  std::array<float, 9> params;

  static SoundFilterElemEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SoundProviderPreferencesEntry {
  std::uint32_t id;
  std::string_view description;
  std::uint32_t flags;
  std::int32_t room_flags;
  float decay_time;
  float environment_size;
  float environment_diffusion;
  std::int32_t room;
  std::int32_t room_hf;
  float decay_hf_ratio;
  std::int32_t reflections;
  float reflections_delay;
  std::int32_t reverb;
  float reverb_delay;
  float room_rolloff_factor;
  float air_absorption_hf;
  std::int32_t room_lf;
  float decay_lf_ratio;
  float echo_time;
  float echo_depth;
  float modulation_time;
  float modulation_depth;
  float hf_reference;
  float lf_reference;

  static SoundProviderPreferencesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SoundWaterTypeEntry {
  std::uint32_t id;
  std::uint32_t sound_type;
  std::uint32_t sound_subtype;
  std::uint32_t sound_id;

  static SoundWaterTypeEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SoundEmittersEntry {

  std::uint32_t id;
  float x;
  float y;
  float z;
  float dir_x;
  float dir_y;
  float dir_z;
  std::uint32_t sound_entries_id;
  std::uint32_t map_id;
  std::string_view name;

  static SoundEmittersEntry Load(const DbcFile &f, std::uint32_t row);
};

struct LiquidMaterialEntry {

  std::uint32_t id;
  std::uint32_t lvf;
  std::uint32_t flags;

  static LiquidMaterialEntry Load(const DbcFile &f, std::uint32_t row);
};

struct LoadingScreenTaxiSplinesEntry {
  std::uint32_t id;
  std::uint32_t taxi_path_id;
  std::array<float, 8> loc_x;
  std::array<float, 8> loc_y;
  std::uint32_t leg_index;

  static LoadingScreenTaxiSplinesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct TerrainTypeEntry {
  std::uint32_t id;
  std::string_view description;
  std::uint32_t footstep_spray_run;
  std::uint32_t footstep_spray_walk;
  std::uint32_t sound_id;
  std::uint32_t flags;

  static TerrainTypeEntry Load(const DbcFile &f, std::uint32_t row);
};

struct TerrainTypeSoundsEntry {
  std::uint32_t id;

  static TerrainTypeSoundsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct FootprintTexturesEntry {
  std::uint32_t id;
  std::string_view texture_path;

  static FootprintTexturesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct FootstepTerrainLookupEntry {
  std::uint32_t id;
  std::uint32_t creature_footstep_id;
  std::uint32_t terrain_sound_id;
  std::uint32_t sound_id;
  std::uint32_t sound_id_splash;

  static FootstepTerrainLookupEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CameraShakesEntry {
  std::uint32_t id;
  std::uint32_t shake_type;
  std::uint32_t direction;
  float amplitude;
  float frequency;
  float duration;
  float phase;
  float coefficient;

  static CameraShakesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ScreenEffectEntry {
  std::uint32_t id;
  std::string_view name;
  std::uint32_t effect;
  std::array<std::uint32_t, 4> param;
  std::uint32_t light_params_slot;
  std::uint32_t sound_ambience_id;
  std::uint32_t zone_music_id;

  static ScreenEffectEntry Load(const DbcFile &f, std::uint32_t row);
};

struct DestructibleModelDataEntry {
  std::uint32_t id;
  std::uint32_t state0_impact_effect_doodad_set;
  std::uint32_t state0_ambient_doodad_set;
  std::uint32_t state1_wmo_display_id;
  std::uint32_t state1_destruction_doodad_set;
  std::uint32_t state1_impact_effect_doodad_set;
  std::uint32_t state1_ambient_doodad_set;
  std::uint32_t state2_wmo_display_id;
  std::uint32_t state2_destruction_doodad_set;
  std::uint32_t state2_impact_effect_doodad_set;
  std::uint32_t state2_ambient_doodad_set;
  std::uint32_t state3_wmo_display_id;
  std::uint32_t state3_init_doodad_set;
  std::uint32_t state3_ambient_doodad_set;
  std::uint32_t eject_direction;
  std::uint32_t rebuild_effect_display_id;
  std::uint32_t field_40;

  std::uint32_t rebuild_transition_mode;

  std::uint32_t rebuild_transition_speed;

  static DestructibleModelDataEntry Load(const DbcFile &f, std::uint32_t row);
};

struct HelmetGeosetVisDataEntry {
  std::uint32_t id;
  std::array<std::uint32_t, 7> hide_geoset;

  static HelmetGeosetVisDataEntry Load(const DbcFile &f, std::uint32_t row);
};

struct UnitBloodEntry {
  std::uint32_t id;
  std::uint32_t combat_blood_spurt_front0;
  std::uint32_t combat_blood_spurt_front1;
  std::uint32_t combat_blood_spurt_back0;
  std::uint32_t combat_blood_spurt_back1;
  std::array<std::string_view, 5> ground_blood;

  static UnitBloodEntry Load(const DbcFile &f, std::uint32_t row);
};

struct UnitBloodLevelsEntry {
  std::uint32_t id;
  std::array<std::uint32_t, 3> violence_level;

  static UnitBloodLevelsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct VocalUISoundsEntry {

  std::uint32_t id;
  std::uint32_t vocal_ui_enum;
  std::uint32_t race_id;
  std::uint32_t normal_sound_m;
  std::uint32_t normal_sound_f;
  std::uint32_t pissed_sound_m;
  std::uint32_t pissed_sound_f;

  static VocalUISoundsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct UISoundLookupsEntry {

  std::uint32_t id;
  std::uint32_t sound_id;
  std::string_view lookup_name;

  static UISoundLookupsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct WeaponImpactSoundsEntry {
  std::uint32_t id;
  std::uint32_t weapon_subclass_id;
  std::uint32_t parry_type;
  std::array<std::uint32_t, 10> impact_sound;
  std::array<std::uint32_t, 10> crit_impact_sound;

  static WeaponImpactSoundsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SheatheSoundLookupsEntry {
  std::uint32_t id;
  std::uint32_t class_id;
  std::uint32_t subclass_id;
  std::uint32_t material;
  std::uint32_t check_material;
  std::uint32_t sheathe_sound;
  std::uint32_t unsheathe_sound;

  static SheatheSoundLookupsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct NPCSoundsEntry {
  std::uint32_t id;
  std::array<std::uint32_t, 4> sound;

  static NPCSoundsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct DeathThudLookupsEntry {

  std::uint32_t id;
  std::uint32_t size_class;
  std::uint32_t terrain_type_sound;
  std::uint32_t sound_entry;
  std::uint32_t sound_entry_water;

  static DeathThudLookupsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ParticleColorEntry {

  std::uint32_t id;
  std::array<std::uint32_t, 3> start;
  std::array<std::uint32_t, 3> mid;
  std::array<std::uint32_t, 3> end;

  static ParticleColorEntry Load(const DbcFile &f, std::uint32_t row);
};

struct PaperDollItemFrameEntry {

  std::uint32_t id;
  std::string_view item_button_name;
  std::string_view slot_icon;
  std::uint32_t slot_number;

  static PaperDollItemFrameEntry Load(const DbcFile &f, std::uint32_t row);
};

struct PageTextMaterialEntry {
  std::uint32_t id;
  std::string_view name;

  static PageTextMaterialEntry Load(const DbcFile &f, std::uint32_t row);
};

struct MaterialEntry {

  std::uint32_t id;
  std::uint32_t flags;
  std::uint32_t foley_sound;
  std::uint32_t sheathe_sound;
  std::uint32_t unsheathe_sound;

  static MaterialEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ObjectEffectEntry {

  std::uint32_t id;
  std::string_view name;
  std::uint32_t object_effect_group_id;
  std::uint32_t trigger_type;
  std::uint32_t event_type;
  std::uint32_t effect_rec_type;
  std::uint32_t effect_rec_id;
  std::uint32_t attachment;
  float offset_x;
  float offset_y;
  float offset_z;
  std::uint32_t object_effect_modifier_id;

  static ObjectEffectEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ObjectEffectGroupEntry {

  std::uint32_t id;
  std::string_view name;

  static ObjectEffectGroupEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ObjectEffectModifierEntry {
  std::uint32_t id;
  std::uint32_t input_type;
  std::uint32_t map_type;
  std::uint32_t output_type;
  std::array<float, 4> map_params{};

  static ObjectEffectModifierEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ObjectEffectPackageEntry {
  std::uint32_t id;
  std::string_view name;

  static ObjectEffectPackageEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ObjectEffectPackageElemEntry {
  std::uint32_t id;
  std::uint32_t object_effect_package_id;
  std::uint32_t object_effect_group_id;
  std::uint32_t state_type;

  static ObjectEffectPackageElemEntry Load(const DbcFile &f, std::uint32_t row);
};

struct TransportPhysicsEntry {
  std::uint32_t id;
  float wave_amp;
  float wave_time_scale;
  float roll_amp;
  float roll_time_scale;
  float pitch_amp;
  float pitch_time_scale;
  float max_bank;
  float max_bank_turn_speed;
  float speed_damp_thresh;
  float speed_damp;

  static TransportPhysicsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct TransportRotationEntry {
  std::uint32_t id;
  std::uint32_t transport_id;
  std::uint32_t time_index;
  float x;
  float y;
  float z;
  float w;

  static TransportRotationEntry Load(const DbcFile &f, std::uint32_t row);
};

struct VehicleUIIndicatorEntry {
  std::uint32_t id;
  std::string_view background_texture;

  static VehicleUIIndicatorEntry Load(const DbcFile &f, std::uint32_t row);
};

struct VehicleUIIndSeatEntry {
  std::uint32_t id;
  std::uint32_t vehicle_ui_indicator_id;
  std::uint32_t virtual_seat_index;
  float x_pos;
  float y_pos;

  static VehicleUIIndSeatEntry Load(const DbcFile &f, std::uint32_t row);
};

struct MovieFileDataEntry {
  std::uint32_t id;
  std::uint32_t width;

  static MovieFileDataEntry Load(const DbcFile &f, std::uint32_t row);
};

struct MovieVariationEntry {
  std::uint32_t id;
  std::uint32_t movie_id;
  std::uint32_t file_data_id;

  static MovieVariationEntry Load(const DbcFile &f, std::uint32_t row);
};

struct AttackAnimKitsEntry {

  std::uint32_t id;
  std::uint32_t field_04;
  std::uint32_t field_08;
  std::uint32_t field_0C;
  std::uint32_t field_10;

  static AttackAnimKitsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct AttackAnimTypesEntry {
  std::uint32_t id;
  std::string_view name;

  static AttackAnimTypesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct DungeonMapEntry {
  std::uint32_t id;
  std::uint32_t map_id;
  std::uint32_t floor_index;
  float min_x;
  float max_x;
  float min_y;
  float max_y;
  std::uint32_t parent_world_map_id;

  static DungeonMapEntry Load(const DbcFile &f, std::uint32_t row);
};

struct DungeonMapChunkEntry {

  std::uint32_t id;
  std::uint32_t map_id;
  std::uint32_t wmo_group_id;
  std::uint32_t dungeon_map_id;
  float min_z;

  static DungeonMapChunkEntry Load(const DbcFile &f, std::uint32_t row);
};

struct WorldChunkSoundsEntry {
  std::uint32_t id;
  std::array<std::uint32_t, 9> columns;

  static WorldChunkSoundsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct GameObjectArtKitEntry {
  std::uint32_t id;
  std::array<std::string_view, 7> strings;

  static GameObjectArtKitEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SoundSamplePreferencesEntry {

  std::uint32_t id;
  std::uint32_t unk1;
  std::uint32_t unk2;
  std::uint32_t eax_environment;
  float eax_decay_time;
  float eax_env_size;
  float eax_env_diffusion;
  std::int32_t eax_room;
  std::int32_t eax_room_hf;
  float eax_room_rolloff;
  std::uint32_t field_10;
  float field_11;
  std::uint32_t field_12;
  float field_13;
  float field_14;
  float field_15;
  std::uint32_t field_16;

  static SoundSamplePreferencesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct WeaponSwingSounds2Entry {
  std::uint32_t id;
  std::uint32_t swing_type;
  std::uint32_t crit;
  std::uint32_t sound_id;

  static WeaponSwingSounds2Entry Load(const DbcFile &f, std::uint32_t row);
};

}

#pragma once

#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_file.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace openwow::data::dbc {

struct AreaTableEntry {

  std::uint32_t id;
  std::uint32_t map_id;
  std::uint32_t parent_area;
  std::uint32_t area_bit;
  std::uint32_t flags;
  std::uint32_t sound_provider_pref{0};
  std::uint32_t sound_provider_pref_uw{0};
  std::uint32_t sound_ambience_id{0};
  std::uint32_t zone_music_id{0};
  std::uint32_t zone_intro_music_id{0};
  std::uint32_t exploration_level{0};
  std::string_view name;
  std::uint32_t faction_group_mask{0};
  std::array<std::uint32_t, 4> liquid_type_id{};
  float min_elevation{0.0F};
  float ambient_multiplier{0.0F};
  std::uint32_t light_id{0};

  static AreaTableEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CharStartOutfitEntry {
  static constexpr std::uint32_t kRetailFieldCount = 77u;
  static constexpr std::uint32_t kRetailRecordSize = 296u;
  static constexpr std::size_t kItemSlotCount = 24u;

  std::uint32_t id;
  std::uint8_t race;
  std::uint8_t class_id;
  std::uint8_t gender;
  std::uint8_t outfit_id;
  std::array<std::int32_t, kItemSlotCount> item_id;
  std::array<std::int32_t, kItemSlotCount> display_id;
  std::array<std::int32_t, kItemSlotCount> inv_type;

  static CharStartOutfitEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ChrClassesEntry {

  std::uint32_t id;
  std::uint32_t primary_stat_type;
  std::uint32_t power_type;
  std::string_view pet_name_token;
  std::string_view name;
  std::string_view name_female;
  std::string_view name_male;
  std::string_view client_file_string;
  std::uint32_t spell_family;
  std::uint32_t class_flags;
  std::uint32_t cinematic_sequence_id;
  std::uint32_t required_expansion;

  static ChrClassesEntry Load(const DbcFile &f, std::uint32_t row);
  [[nodiscard]] std::string_view DisplayNameForSex(std::uint32_t sex_id) const;
  [[nodiscard]] std::uint32_t ResolveDisplaySex(std::uint32_t sex_id) const;
};

struct ChrRacesEntry {

  std::uint32_t id;
  std::uint32_t flags;
  std::uint32_t faction_id;
  std::uint32_t exploration_sound_id;
  std::uint32_t model_male;
  std::uint32_t model_female;
  std::string_view model_client_prefix;
  std::uint32_t default_language_id;
  std::uint32_t creature_type;
  std::uint32_t resurrection_sickness_spell_id;
  std::uint32_t splash_sound_id;
  std::string_view client_file_string;
  std::uint32_t cinematic_sequence_id;
  std::uint32_t team_id;
  std::string_view name;
  std::string_view name_female;
  std::string_view name_male;
  std::string_view facial_hair_male;
  std::string_view facial_hair_female;
  std::string_view hair_customization;
  std::uint32_t required_expansion;

  static ChrRacesEntry Load(const DbcFile &f, std::uint32_t row);
  [[nodiscard]] std::string_view DisplayNameForSex(std::uint32_t sex_id) const;
  [[nodiscard]] std::uint32_t ResolveDisplaySex(std::uint32_t sex_id) const;
};

struct CinematicSequencesEntry {
  std::uint32_t id;
  std::uint32_t sound_id;
  std::array<std::uint32_t, 8> camera_ids;

  static CinematicSequencesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CinematicCameraEntry {

  std::uint32_t id;
  std::string_view model;
  std::uint32_t sound_id;
  float origin_x;
  float origin_y;
  float origin_z;
  float origin_facing;

  static CinematicCameraEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CreatureDisplayInfoEntry {
  std::uint32_t id;
  std::uint32_t model_id;
  std::uint32_t sound_id;
  std::uint32_t extra_info;
  float scale;
  std::uint32_t model_alpha;
  std::array<std::string_view, 3> texture_variation;
  std::string_view portrait_texture_name;
  std::int32_t  size_class;
  std::uint32_t blood_id;
  std::uint32_t npc_sound_id;
  std::uint32_t particle_color_id;
  std::uint32_t creature_geoset_data;
  std::uint32_t object_effect_package_id;

  static CreatureDisplayInfoEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CreatureModelDataEntry {

  std::uint32_t id;
  std::uint32_t flags;
  std::string_view model_name;
  std::int32_t  size_class;
  float scale;
  std::uint32_t blood_id;
  std::uint32_t footprint_texture_id;
  float footprint_texture_length;
  float footprint_texture_width;
  float footprint_particle_scale;
  std::uint32_t foley_material_id;
  std::uint32_t footstep_shake_size;
  std::uint32_t death_thud_shake_size;
  std::uint32_t sound_id;
  float collision_width;
  float collision_height;
  float mount_height;
  std::array<float, 3> geo_box_min;
  std::array<float, 3> geo_box_max;
  float world_effect_scale;
  float attached_effect_scale;
  float missile_collision_radius;
  float missile_collision_push;
  float missile_collision_raise;

  static CreatureModelDataEntry Load(const DbcFile &f, std::uint32_t row);
};

struct GameObjectDisplayInfoEntry {
  std::uint32_t id;
  std::string_view filename;

  std::array<std::uint32_t, 10> sound_ids{};

  float min_x;
  float min_y;
  float min_z;
  float max_x;
  float max_y;
  float max_z;

  std::uint32_t object_effect_package_id{0};

  static GameObjectDisplayInfoEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemDisplayInfoEntry {

  std::uint32_t id;
  std::string_view model_name_left;
  std::string_view model_name_right;
  std::string_view texture_name_left;
  std::string_view texture_name_right;
  std::string_view inventory_icon;
  std::string_view inventory_icon_2;
  std::uint32_t geoset_control_1;
  std::uint32_t geoset_control_2;
  std::uint32_t geoset_control_3;
  std::uint32_t flags;
  std::uint32_t spell_visual_id;
  std::uint32_t group_sound_index;
  std::uint32_t helmet_geoset_vis_male;
  std::uint32_t helmet_geoset_vis_female;
  std::array<std::string_view, 8> component_texture_name{};
  std::uint32_t item_visuals_id;
  std::uint32_t particle_color_id;

  static ItemDisplayInfoEntry Load(const DbcFile &f, std::uint32_t row);
};

struct LightEntry {
  std::uint32_t id;
  std::uint32_t map_id;
  float x;
  float y;
  float z;
  float falloff_start;
  float falloff_end;
  std::array<std::uint32_t, 8> light_params{};

  static LightEntry Load(const DbcFile &f, std::uint32_t row);
};

void NormalizeLightEntryRetailCoordinates(LightEntry &entry,
                                          bool normalize_coordinates = true,
                                          bool special_zero_origin_mode = false);

struct LightIntBandEntry {
  std::uint32_t id;
  std::uint32_t num_entries;
  std::array<std::uint32_t, 16> times{};
  std::array<std::uint32_t, 16> values{};

  static LightIntBandEntry Load(const DbcFile &f, std::uint32_t row);
};

struct LightFloatBandEntry {
  std::uint32_t id;
  std::uint32_t num_entries;
  std::array<std::uint32_t, 16> times{};
  std::array<float, 16> values{};

  static LightFloatBandEntry Load(const DbcFile &f, std::uint32_t row);
};

struct LoadingScreensEntry {
  std::uint32_t id;
  std::string_view name;
  std::string_view path;
  std::uint32_t has_wide_screen;

  static LoadingScreensEntry Load(const DbcFile &f, std::uint32_t row);
};

struct MapEntry {
  std::uint32_t id;
  std::string_view internal_name;
  std::uint32_t map_type;
  std::uint32_t flags;
  std::uint32_t is_pvp;
  std::string_view name;
  std::uint32_t linked_zone;
  std::string_view description_horde;
  std::string_view description_alliance;
  std::uint32_t loading_screen_id;
  float minimap_icon_scale;
  std::int32_t entrance_map;
  float entrance_x;
  float entrance_y;
  std::uint32_t time_of_day_override;
  std::uint32_t expansion_id;
  std::uint32_t raid_offset;
  std::uint32_t max_players;

  static MapEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SoundEntriesEntry {
  std::uint32_t id;
  std::uint32_t sound_type;
  std::string_view name;
  std::array<std::string_view, 10> file;
  std::array<std::uint32_t, 10> freq;
  std::string_view directory_base;
  float volume;
  std::uint32_t flags;
  float min_distance;
  float distance_cutoff;
  float eax_definition;
  std::uint32_t sound_entries_advanced_id;

  [[nodiscard]] std::uint32_t FileCount() const {
    std::uint32_t n = 0;
    for (const auto &f : file) {
      if (f.empty()) {
        break;
      }
      ++n;
    }
    return n;
  }

  static SoundEntriesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellIconEntry {
  std::uint32_t id;
  std::string_view icon_path;

  static SpellIconEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellVisualEntry {
  std::uint32_t id;
  std::uint32_t precast_kit;
  std::uint32_t cast_kit;
  std::uint32_t impact_kit;
  std::uint32_t state_kit;
  std::uint32_t state_done_kit;
  std::uint32_t channel_kit;
  std::uint32_t has_missile;
  std::int32_t missile_model;
  std::uint32_t missile_path_type;
  std::int32_t missile_destination_attachment;
  std::uint32_t missile_sound_id;
  std::uint32_t anim_event_sound_id;
  std::uint32_t flags;
  std::uint32_t caster_impact_kit;
  std::uint32_t target_impact_kit;
  std::int32_t missile_attachment_id;
  std::int32_t missile_follow_ground_height;
  std::int32_t missile_follow_ground_drop_speed;
  std::int32_t missile_follow_ground_approach;
  std::uint32_t missile_follow_ground_flags;
  std::uint32_t missile_motion_id;
  std::uint32_t missile_targeting_kit;
  std::uint32_t instant_area_kit;
  std::uint32_t impact_area_kit;
  std::uint32_t persistent_area_kit;
  float missile_cast_offset_x;
  float missile_cast_offset_y;
  float missile_cast_offset_z;
  float missile_impact_offset_x;
  float missile_impact_offset_y;
  float missile_impact_offset_z;

  static SpellVisualEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellVisualKitEntry {

  std::uint32_t id;

  std::int32_t start_anim_id;
  std::int32_t anim_id;
  std::uint32_t head_effect;
  std::uint32_t chest_effect;
  std::uint32_t base_effect;
  std::uint32_t left_hand_effect;
  std::uint32_t right_hand_effect;
  std::uint32_t breath_effect;
  std::uint32_t left_weapon_effect;
  std::uint32_t right_weapon_effect;
  std::uint32_t special1_effect;
  std::uint32_t special2_effect;
  std::uint32_t special3_effect;
  std::uint32_t world_effect;
  std::uint32_t sound_id;
  std::uint32_t shake_id;
  std::uint32_t proc_type[4];
  float proc_param_zero[4];
  float proc_param_one[4];
  float proc_param_two[4];
  float proc_param_three[4];
  std::uint32_t flags;

  static SpellVisualKitEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellVisualEffectNameEntry {

  std::uint32_t id;
  std::string_view name;
  std::string_view file_path;
  float area_effect_size;
  float scale;
  float min_allowed_scale;
  float max_allowed_scale;

  static SpellVisualEffectNameEntry Load(const DbcFile &f, std::uint32_t row);
};

struct AnimationDataEntry {
  std::uint32_t id;
  std::string_view name;
  std::uint32_t weapon_flags;
  std::uint32_t body_flags;
  std::uint32_t flags;
  std::uint32_t fallback;
  std::uint32_t behavior_id;
  std::uint32_t behavior_tier;

  static AnimationDataEntry Load(const DbcFile &f, std::uint32_t row);
};

struct TalentEntry {

  std::uint32_t id;
  std::uint32_t tab_id;
  std::uint32_t tier_id;
  std::uint32_t column_index;
  std::array<std::uint32_t, 9> spell_rank;
  std::array<std::uint32_t, 3> prereq_talent;
  std::array<std::uint32_t, 3> prereq_rank;
  std::uint32_t flags;
  std::uint32_t required_spell_id;

  std::array<std::uint32_t, 2> pet_talent_mask;

  static TalentEntry Load(const DbcFile &f, std::uint32_t row);
};

struct TalentTabEntry {
  std::uint32_t id;
  std::string_view name;
  std::uint32_t spell_icon_id;
  std::uint32_t race_mask;
  std::uint32_t class_mask;
  std::uint32_t pet_talent_mask;
  std::uint32_t order_index;
  std::string_view background_file;

  static TalentTabEntry Load(const DbcFile &f, std::uint32_t row);
};

struct TaxiNodesEntry {
  std::uint32_t id;
  std::uint32_t map_id;
  float x;
  float y;
  float z;
  std::string_view name;
  std::array<std::uint32_t, 2> mount_creature_id;

  static TaxiNodesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct TaxiPathEntry {
  std::uint32_t id;
  std::uint32_t from_node_id;
  std::uint32_t to_node_id;
  std::uint32_t cost;

  static TaxiPathEntry Load(const DbcFile &f, std::uint32_t row);
};

struct TaxiPathNodeEntry {
  std::uint32_t id;
  std::uint32_t path_id;
  std::uint32_t node_index;
  std::uint32_t map_id;
  float x;
  float y;
  float z;
  std::uint32_t flags;
  std::uint32_t delay;
  std::uint32_t arrival_event_id;
  std::uint32_t departure_event_id;

  static TaxiPathNodeEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellCastTimesEntry {
  std::uint32_t id;
  std::int32_t base_cast_time;
  std::int32_t per_level;
  std::int32_t minimum;

  static SpellCastTimesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SkillLineEntry {
  std::uint32_t id;
  std::int32_t category_id;
  std::uint32_t skill_cost_id;
  std::string_view name;
  std::string_view description;
  std::uint32_t spell_icon_id;
  std::string_view alternate_verb;
  std::uint32_t can_link;

  static SkillLineEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SkillLineAbilityEntry {
  std::uint32_t id;
  std::uint32_t skill_id;
  std::uint32_t spell_id;
  std::uint32_t race_mask;
  std::uint32_t class_mask;
  std::uint32_t exclude_race_mask;
  std::uint32_t exclude_class_mask;
  std::uint32_t min_skill_rank;
  std::uint32_t superseded_by_spell;
  std::uint32_t acquire_method;
  std::uint32_t trivial_skill_hi;
  std::uint32_t trivial_skill_lo;
  std::uint32_t num_skill_ups;
  std::uint32_t unique_bit;

  static SkillLineAbilityEntry Load(const DbcFile &f, std::uint32_t row);
};

struct GtCombatRatingsEntry {
  std::uint32_t id;
  float value;

  static GtCombatRatingsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellRangeEntry {
  std::uint32_t id;
  float range_min;
  float range_min_friendly;
  float range_max;
  float range_max_friendly;
  std::uint32_t flags;
  std::string_view display_name;
  std::string_view display_name_short;

  static SpellRangeEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellEntry {
  std::uint32_t id;
  std::uint32_t category;
  std::uint32_t dispel;
  std::uint32_t mechanic;

  std::uint32_t attributes;
  std::uint32_t attributes_ex;
  std::uint32_t attributes_ex2;
  std::uint32_t attributes_ex3;
  std::uint32_t attributes_ex4;
  std::uint32_t attributes_ex5;
  std::uint32_t attributes_ex6;
  std::uint32_t attributes_ex7;

  std::uint32_t stances;
  std::uint32_t stances_high;
  std::uint32_t stances_not;
  std::uint32_t stances_not_high;

  std::uint32_t targets;
  std::uint32_t target_creature_type;
  std::uint32_t requires_spell_focus;
  std::uint32_t facing_caster_flags;

  std::uint32_t caster_aura_state;
  std::uint32_t target_aura_state;
  std::uint32_t caster_aura_state_not;
  std::uint32_t target_aura_state_not;
  std::uint32_t caster_aura_spell;
  std::uint32_t target_aura_spell;
  std::uint32_t exclude_caster_aura_spell;
  std::uint32_t exclude_target_aura_spell;

  std::uint32_t casting_time_index;
  std::uint32_t recovery_time;
  std::uint32_t category_recovery_time;

  std::uint32_t interrupt_flags;
  std::uint32_t aura_interrupt_flags;
  std::uint32_t channel_interrupt_flags;

  std::uint32_t proc_flags;
  std::uint32_t proc_chance;
  std::uint32_t proc_charges;

  std::uint32_t max_level;
  std::uint32_t base_level;
  std::uint32_t spell_level;

  std::uint32_t duration_index;
  std::uint32_t power_type;
  std::uint32_t mana_cost;
  std::uint32_t mana_cost_per_level;
  std::uint32_t mana_per_second;
  std::uint32_t mana_per_second_per_level;
  std::uint32_t range_index;
  float speed;

  std::uint32_t modal_next_spell;
  std::uint32_t stack_amount;

  std::array<std::uint32_t, kMaxSpellTotems> totem;

  std::array<std::int32_t, kMaxSpellReagents> reagent;
  std::array<std::uint32_t, kMaxSpellReagents> reagent_count;

  std::int32_t equipped_item_class;
  std::int32_t equipped_item_sub_class_mask;
  std::int32_t equipped_item_inv_type_mask;

  std::array<std::uint32_t, kMaxSpellEffects> effect;
  std::array<std::int32_t, kMaxSpellEffects> effect_die_sides;
  std::array<float, kMaxSpellEffects> effect_real_points_per_lvl;
  std::array<std::int32_t, kMaxSpellEffects> effect_base_points;
  std::array<std::uint32_t, kMaxSpellEffects> effect_mechanic;
  std::array<std::uint32_t, kMaxSpellEffects> effect_implicit_target_a;
  std::array<std::uint32_t, kMaxSpellEffects> effect_implicit_target_b;
  std::array<std::uint32_t, kMaxSpellEffects> effect_radius_index;
  std::array<std::uint32_t, kMaxSpellEffects> effect_apply_aura;
  std::array<std::uint32_t, kMaxSpellEffects> effect_amplitude;
  std::array<float, kMaxSpellEffects> effect_value_multiplier;
  std::array<std::uint32_t, kMaxSpellEffects> effect_chain_target;
  std::array<std::uint32_t, kMaxSpellEffects> effect_item_type;
  std::array<std::int32_t, kMaxSpellEffects> effect_misc_value;
  std::array<std::int32_t, kMaxSpellEffects> effect_misc_value_b;
  std::array<std::uint32_t, kMaxSpellEffects> effect_trigger_spell;
  std::array<float, kMaxSpellEffects> effect_points_per_combo;
  std::array<std::uint32_t, 9> effect_spell_class_mask;

  std::array<std::uint32_t, 2> spell_visual;
  std::uint32_t spell_icon_id;
  std::uint32_t active_icon_id;
  std::uint32_t spell_priority;

  std::string_view spell_name;
  std::string_view rank;
  std::string_view description;
  std::string_view tooltip;

  std::uint32_t mana_cost_percentage;
  std::uint32_t start_recovery_category;
  std::uint32_t start_recovery_time;
  std::uint32_t max_target_level;
  std::uint32_t spell_family_name;
  std::array<std::uint32_t, 3> spell_family_flags;
  std::uint32_t max_affected_targets;
  std::uint32_t dmg_class;
  std::uint32_t prevention_type;
  std::uint32_t stance_bar_order;

  std::array<float, kMaxSpellEffects> effect_damage_multiplier;

  std::uint32_t minimum_faction_id;
  std::int32_t minimum_reputation;
  std::uint32_t required_aura_vision;

  std::array<std::uint32_t, kMaxSpellTotems> totem_category;
  std::int32_t area_group_id;
  std::uint32_t school_mask;
  std::uint32_t rune_cost_id;
  std::uint32_t spell_missile_id;
  std::uint32_t power_display_id;

  std::array<float, kMaxSpellEffects> effect_bonus_multiplier;

  std::uint32_t spell_description_variable_id;
  std::uint32_t spell_difficulty_id;

  static SpellEntry Load(const DbcFile &f, std::uint32_t row);
};

}

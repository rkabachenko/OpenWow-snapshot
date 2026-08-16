#pragma once

#include "openwow/data/formats/dbc/dbc_file.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace openwow::data::dbc {

struct SpellDurationEntry {
  std::uint32_t id;
  std::int32_t duration;
  std::int32_t duration_per_level;
  std::int32_t max_duration;

  static SpellDurationEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellRadiusEntry {
  std::uint32_t id;
  float radius;
  float radius_per_level;
  float max_radius;

  static SpellRadiusEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellRuneCostEntry {
  std::uint32_t id;
  std::uint32_t blood;
  std::uint32_t unholy;
  std::uint32_t frost;
  std::uint32_t runic_power;

  static SpellRuneCostEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellShapeshiftFormEntry {
  std::uint32_t id;
  std::uint32_t bonus_action_bar;
  std::string_view name;
  std::uint32_t flags;
  std::uint32_t creature_type;
  std::uint32_t attack_icon_id;
  std::uint32_t combat_round_time;
  std::array<std::uint32_t, 4> creature_display_id;
  std::array<std::uint32_t, 8> override_actions{};

  static SpellShapeshiftFormEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellItemEnchantmentEntry {

  std::uint32_t id;
  std::array<std::uint32_t, 3> type;
  std::array<std::int32_t, 3> amount;
  std::array<std::int32_t, 3> amount_max;
  std::array<std::uint32_t, 3> spell_id;
  std::uint32_t field_13;
  std::string_view description;
  std::uint32_t aura_id;
  std::uint32_t slot;
  std::uint32_t gem_id;
  std::uint32_t enchantment_condition;
  std::array<std::uint32_t, 3> tail_fields;

  static SpellItemEnchantmentEntry Load(const DbcFile &f, std::uint32_t row);
};

struct FactionEntry {
  std::uint32_t id;
  std::int32_t reputation_list_id;
  std::array<std::uint32_t, 4> base_rep_race_mask;
  std::array<std::uint32_t, 4> base_rep_class_mask;
  std::array<std::int32_t, 4> base_rep_value;
  std::array<std::uint32_t, 4> reputation_flags;
  std::uint32_t parent_faction_id;
  float parent_faction_mod0;
  float parent_faction_mod1;
  std::uint32_t parent_faction_cap0;
  std::uint32_t parent_faction_cap1;
  std::string_view name;
  std::string_view description;

  static FactionEntry Load(const DbcFile &f, std::uint32_t row);
};

struct FactionTemplateEntry {
  static constexpr std::size_t kRelationSlotCount = 4u;

  std::uint32_t id;
  std::uint32_t faction;
  std::uint32_t flags;
  std::uint32_t faction_group;
  std::uint32_t friend_group;
  std::uint32_t enemy_group;
  std::array<std::uint32_t, kRelationSlotCount> enemies;
  std::array<std::uint32_t, kRelationSlotCount> friends;

  static FactionTemplateEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ChatChannelsEntry {

  std::uint32_t id;
  std::uint32_t flags;
  std::uint32_t faction_group;
  std::string_view pattern;
  std::string_view name;

  static ChatChannelsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ChatProfanityEntry {
  static constexpr std::uint32_t kAllLanguages =
      std::numeric_limits<std::uint32_t>::max();

  std::uint32_t id;
  std::string_view text;
  std::uint32_t language;

  static ChatProfanityEntry Load(const DbcFile &f, std::uint32_t row);
};

struct QuestSortEntry {
  std::uint32_t id;
  std::string_view name;

  static QuestSortEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CharTitlesEntry {
  std::uint32_t id;
  std::uint32_t condition_id;
  std::string_view name_male;
  std::string_view name_female;
  std::uint32_t mask_id;

  static CharTitlesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CurrencyTypesEntry {
  std::uint32_t id;
  std::uint32_t item_id;
  std::uint32_t category;
  std::uint32_t bit_index;

  static CurrencyTypesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct TotemCategoryEntry {
  std::uint32_t id;
  std::string_view name;
  std::uint32_t totem_category_type;
  std::uint32_t totem_category_mask;

  static TotemCategoryEntry Load(const DbcFile &f, std::uint32_t row);
};

struct GemPropertiesEntry {
  std::uint32_t id;
  std::uint32_t enchant_id;
  std::uint32_t maxcount_inv;
  std::uint32_t maxcount_item;
  std::uint32_t type;

  static GemPropertiesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct GlyphPropertiesEntry {

  std::uint32_t id;
  std::uint32_t spell_id;
  std::uint32_t type;
  std::uint32_t spell_icon_id;

  static GlyphPropertiesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct GlyphSlotEntry {

  std::uint32_t id;
  std::uint32_t type;
  std::uint32_t order;

  static GlyphSlotEntry Load(const DbcFile &f, std::uint32_t row);
};

struct LockEntry {
  std::uint32_t id;
  std::array<std::uint32_t, 8> type;
  std::array<std::uint32_t, 8> index;
  std::array<std::uint32_t, 8> skill;
  std::array<std::uint32_t, 8> action;

  static LockEntry Load(const DbcFile &f, std::uint32_t row);
};

struct OverrideSpellDataEntry {

  std::uint32_t id;
  std::array<std::uint32_t, 10> spell;
  std::uint32_t flags;

  static OverrideSpellDataEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SummonPropertiesEntry {

  std::uint32_t id;
  std::uint32_t category;
  std::uint32_t faction;
  std::uint32_t type;
  std::uint32_t slot;
  std::uint32_t flags;

  static SummonPropertiesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemRandomPropertiesEntry {

  std::uint32_t id;
  std::string_view internal_name;
  std::array<std::uint32_t, 5> enchantment;
  std::string_view name;

  static ItemRandomPropertiesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemRandomSuffixEntry {

  std::uint32_t id;
  std::string_view name;
  std::string_view internal_name;
  std::array<std::uint32_t, 5> enchantment;
  std::array<std::uint32_t, 5> allocation_pct;

  static ItemRandomSuffixEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemExtendedCostEntry {
  std::uint32_t id;
  std::uint32_t honor_points;
  std::uint32_t arena_points;
  std::uint32_t arena_slot;
  std::array<std::uint32_t, 5> item_id;
  std::array<std::uint32_t, 5> item_count;
  std::uint32_t personal_arena_rating;
  std::uint32_t item_purchase_group;

  static ItemExtendedCostEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CreatureFamilyEntry {
  std::uint32_t id;
  float min_scale;
  std::uint32_t min_scale_level;
  float max_scale;
  std::uint32_t max_scale_level;
  std::uint32_t skill_line_0;
  std::uint32_t skill_line_1;
  std::uint32_t pet_food_mask;
  std::int32_t pet_talent_type;
  std::uint32_t category;
  std::string_view name;
  std::string_view icon_file;

  static CreatureFamilyEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CreatureTypeEntry {
  std::uint32_t id;
  std::string_view name;
  std::uint32_t flags;

  static CreatureTypeEntry Load(const DbcFile &f, std::uint32_t row);
};

#define OPENWOW_DEFINE_GT_ENTRY(Name)                                                              \
  struct Name##Entry {                                                                             \
    std::uint32_t id;                                                                              \
    float value;                                                                                   \
    static Name##Entry Load(const DbcFile &f, std::uint32_t row);                                  \
  }

OPENWOW_DEFINE_GT_ENTRY(GtBarberShopCostBase);
OPENWOW_DEFINE_GT_ENTRY(GtChanceToMeleeCrit);
OPENWOW_DEFINE_GT_ENTRY(GtChanceToMeleeCritBase);
OPENWOW_DEFINE_GT_ENTRY(GtChanceToSpellCrit);
OPENWOW_DEFINE_GT_ENTRY(GtChanceToSpellCritBase);
OPENWOW_DEFINE_GT_ENTRY(GtOCTRegenHP);
OPENWOW_DEFINE_GT_ENTRY(GtOCTRegenMP);
OPENWOW_DEFINE_GT_ENTRY(GtRegenHPPerSpt);
OPENWOW_DEFINE_GT_ENTRY(GtRegenMPPerSpt);
OPENWOW_DEFINE_GT_ENTRY(GtNPCManaCostScaler);

#undef OPENWOW_DEFINE_GT_ENTRY

struct GtOCTClassCombatRatingScalarEntry {
  std::uint32_t id;
  float value;

  static GtOCTClassCombatRatingScalarEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellCategoryEntry {
  std::uint32_t id;
  std::uint32_t flags;

  static SpellCategoryEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellDifficultyEntry {

  std::uint32_t id;
  std::array<std::uint32_t, 4> spell_id;

  static SpellDifficultyEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellDispelTypeEntry {

  std::uint32_t id;
  std::string_view name;
  std::uint32_t mask;
  std::uint32_t immunity_possible;
  std::string_view internal_name;

  static SpellDispelTypeEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellMechanicEntry {

  std::uint32_t id;
  std::string_view name;

  static SpellMechanicEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellFocusObjectEntry {
  std::uint32_t id;
  std::string_view name;

  static SpellFocusObjectEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellItemEnchantmentConditionEntry {
  static constexpr std::uint32_t kRetailFieldCount = 31u;
  static constexpr std::uint32_t kRetailRecordSize = 64u;
  static constexpr std::size_t kConditionCount = 5u;

  std::uint32_t id;
  std::array<std::uint8_t, kConditionCount> lt_operand_type;
  std::array<std::uint32_t, kConditionCount> lt_operand;
  std::array<std::uint8_t, kConditionCount> operator_type;
  std::array<std::uint8_t, kConditionCount> rt_operand_type;
  std::array<std::uint32_t, kConditionCount> rt_operand;
  std::array<std::uint8_t, kConditionCount> logic;

  static SpellItemEnchantmentConditionEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellDescriptionVariablesEntry {
  std::uint32_t id;
  std::string_view variables;

  static SpellDescriptionVariablesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellChainEffectsEntry {
  static constexpr std::uint32_t kRetailFieldCount = 48u;
  static constexpr std::uint32_t kRetailRecordSize = 177u;

  std::uint32_t id;
  float avg_seg_len;
  float width;
  float noise_scale;
  float tex_coord_scale;
  std::uint32_t seg_duration;
  std::uint32_t seg_delay;
  std::string_view texture;
  std::uint32_t flags;
  std::uint32_t joint_count;
  float joint_offset_radius;
  std::uint32_t joints_per_minor_joint;
  std::uint32_t minor_joints_per_major_joint;
  float minor_joint_scale;
  float major_joint_scale;
  float joint_move_speed;
  float joint_smoothness;
  float min_duration_between_joint_jumps;
  float max_duration_between_joint_jumps;
  float wave_height;
  float wave_freq;
  float wave_speed;
  float min_wave_angle;
  float max_wave_angle;
  float min_wave_spin;
  float max_wave_spin;
  float arc_height;
  float min_arc_angle;
  float max_arc_angle;
  float min_arc_spin;
  float max_arc_spin;
  float delay_between_effects;
  float min_flicker_on_duration;
  float max_flicker_on_duration;
  float min_flicker_off_duration;
  float max_flicker_off_duration;
  float pulse_speed;
  float pulse_on_length;
  float pulse_fade_length;
  std::uint8_t alpha;
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
  std::uint8_t blend_mode;
  std::string_view combo;
  std::uint32_t render_layer;
  float texture_length;
  float wave_phase;

  static SpellChainEffectsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellMissileEntry {

  std::uint32_t id;
  std::uint32_t flags;
  float default_pitch_min;
  float default_pitch_max;
  float default_speed_min;
  float default_speed_max;
  float randomize_facing_min;
  float randomize_facing_max;
  float randomize_pitch_min;
  float randomize_pitch_max;
  float randomize_speed_min;
  float randomize_speed_max;
  float gravity;
  float max_duration;
  float collision_radius;

  static SpellMissileEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellMissileMotionEntry {

  std::uint32_t id;
  std::string_view script_name;
  std::string_view script_body;
  std::uint32_t field_3;
  std::uint32_t instance_count;

  static SpellMissileMotionEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellEffectCameraShakesEntry {
  std::uint32_t id;
  std::array<std::uint32_t, 3> camera_shake;

  static SpellEffectCameraShakesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellVisualKitAreaModelEntry {

  std::uint32_t id;
  std::string_view model_path;
  std::uint32_t aux_u32;

  static SpellVisualKitAreaModelEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SpellVisualKitModelAttachEntry {

  std::uint32_t id;
  std::uint32_t parent_spell_visual_kit_id;
  std::uint32_t spell_visual_effect_name_id;
  std::uint32_t attachment_id;
  float offset_x;
  float offset_y;
  float offset_z;
  float yaw;
  float pitch;
  float roll;

  static SpellVisualKitModelAttachEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemEntry {
  std::uint32_t id;
  std::uint32_t class_id;
  std::uint32_t subclass_id;
  std::int32_t sound_override_subclass;
  std::uint32_t material;
  std::uint32_t display_info_id;
  std::uint32_t inventory_type;
  std::uint32_t sheathe_type;

  static ItemEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemBagFamilyEntry {

  std::uint32_t id;
  std::string_view name;

  static ItemBagFamilyEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemClassEntry {

  std::uint32_t id;
  std::uint32_t subclass_map;
  std::uint32_t flags;
  std::string_view name;

  static ItemClassEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemSubClassEntry {
  static constexpr std::uint32_t kSubClassKeyStride = 256u;

  [[nodiscard]] static constexpr std::uint32_t ComposeKey(
      const std::uint32_t class_id, const std::uint32_t subclass_id) {
    return class_id * kSubClassKeyStride + subclass_id;
  }

  std::uint32_t id;
  std::uint32_t class_id;
  std::uint32_t subclass_id;
  std::uint32_t prereq_proficiency;
  std::uint32_t postreq_proficiency;
  std::uint32_t flags;
  std::uint32_t display_flags;
  std::uint32_t weapon_parry_seq;
  std::uint32_t weapon_ready_seq;
  std::uint32_t weapon_attack_seq;
  std::uint32_t weapon_swing_size;
  std::string_view display_name;
  std::string_view verbose_name;

  static ItemSubClassEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemSubClassMaskEntry {

  std::uint32_t id;
  std::uint32_t class_id;
  std::uint32_t mask;
  std::string_view name;

  static ItemSubClassMaskEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemCondExtCostsEntry {
  std::uint32_t id;
  std::uint32_t cond_extended_cost;
  std::uint32_t item_extended_cost_entry;
  std::uint32_t unknown_3;

  static ItemCondExtCostsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemGroupSoundsEntry {
  std::uint32_t id;
  std::array<std::uint32_t, 4> sound;

  static ItemGroupSoundsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemLimitCategoryEntry {
  std::uint32_t id;
  std::string_view name;
  std::uint32_t quantity;
  std::uint32_t flags;

  static ItemLimitCategoryEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemPetFoodEntry {
  std::uint32_t id;
  std::string_view name;

  static ItemPetFoodEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemPurchaseGroupEntry {
  std::uint32_t id;
  std::array<std::uint32_t, 8> item_id;
  std::string_view name;

  static ItemPurchaseGroupEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemVisualEffectsEntry {
  std::uint32_t id;
  std::string_view model;

  static ItemVisualEffectsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ItemVisualsEntry {
  std::uint32_t id;
  std::array<std::uint32_t, 5> slot;

  static ItemVisualsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SkillRaceClassInfoEntry {
  std::uint32_t id;
  std::uint32_t skill_id;
  std::uint32_t race_mask;
  std::uint32_t class_mask;
  std::uint32_t flags;
  std::uint32_t min_level;
  std::uint32_t skill_tier_id;
  std::uint32_t skill_cost_index;

  static SkillRaceClassInfoEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SkillTiersEntry {
  std::uint32_t id;
  std::array<std::uint32_t, 16> cost;
  std::array<std::uint32_t, 16> value;

  static SkillTiersEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SkillCostsDataEntry {
  std::uint32_t id;
  std::uint32_t skill_costs_id;
  std::uint32_t cost0;
  std::uint32_t cost1;
  std::uint32_t cost2;

  static SkillCostsDataEntry Load(const DbcFile &f, std::uint32_t row);
};

struct SkillLineCategoryEntry {
  std::uint32_t id;
  std::string_view name;
  std::uint32_t sort_index;

  static SkillLineCategoryEntry Load(const DbcFile &f, std::uint32_t row);
};

struct QuestFactionRewardEntry {
  std::uint32_t id;
  std::array<std::int32_t, 10> difficulty;

  static QuestFactionRewardEntry Load(const DbcFile &f, std::uint32_t row);
};

struct QuestInfoEntry {
  std::uint32_t id;
  std::string_view name;

  static QuestInfoEntry Load(const DbcFile &f, std::uint32_t row);
};

struct QuestXPEntry {

  std::uint32_t id;
  std::array<std::uint32_t, 10> difficulty;

  static QuestXPEntry Load(const DbcFile &f, std::uint32_t row);
};

struct RandPropPointsEntry {

  std::uint32_t id;
  std::array<std::uint32_t, 5> epic;
  std::array<std::uint32_t, 5> rare;
  std::array<std::uint32_t, 5> uncommon;

  static RandPropPointsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ScalingStatDistributionEntry {
  std::uint32_t id;
  std::array<std::int32_t, 10> stat_id;
  std::array<std::uint32_t, 10> bonus;
  std::uint32_t max_level;

  static ScalingStatDistributionEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ScalingStatValuesEntry {
  std::uint32_t id;
  std::array<std::uint32_t, 23> values{};

  [[nodiscard]] std::uint32_t GetField(std::uint32_t field) const;

  static ScalingStatValuesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct ResistancesEntry {

  std::uint32_t id;
  std::uint32_t flags;
  std::uint32_t fizzle_sound_id;
  std::string_view name;

  static ResistancesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct MapDifficultyEntry {

  std::uint32_t id;
  std::uint32_t map_id;
  std::uint32_t difficulty;
  std::string_view message;
  std::uint32_t raid_duration;
  std::uint32_t max_players;
  std::string_view difficulty_string;

  static MapDifficultyEntry Load(const DbcFile &f, std::uint32_t row);
};

struct PvpDifficultyEntry {

  std::uint32_t id;
  std::uint32_t map_id;
  std::uint32_t range_index;
  std::uint32_t min_level;
  std::uint32_t max_level;
  std::uint32_t unknown_14;

  static PvpDifficultyEntry Load(const DbcFile &f, std::uint32_t row);
};

struct AuctionHouseEntry {

  std::uint32_t id;
  std::uint32_t faction_id;
  std::uint32_t deposit_rate;
  std::uint32_t consignment_rate;
  std::string_view name;

  static AuctionHouseEntry Load(const DbcFile &f, std::uint32_t row);
};

struct BankBagSlotPricesEntry {

  std::uint32_t id;
  std::uint32_t cost;

  static BankBagSlotPricesEntry Load(const DbcFile &f, std::uint32_t row);
};

struct LfgDungeonsEntry {
  std::uint32_t id;
  std::string_view name;
  std::uint32_t min_level;
  std::uint32_t max_level;
  std::uint32_t rec_level;
  std::uint32_t rec_min_level;
  std::uint32_t rec_max_level;
  std::uint32_t map_id;
  std::uint32_t difficulty;
  std::uint32_t flags;
  std::uint32_t type_id;
  std::int32_t faction;
  std::string_view texture_filename;
  std::uint32_t expansion_level;
  std::uint32_t order_index;
  std::uint32_t group_id;
  std::string_view description;

  static LfgDungeonsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct LfgDungeonGroupEntry {

  std::uint32_t id;
  std::string_view name;
  std::uint32_t order_index;
  std::uint32_t parent_group_id;
  std::uint32_t type_id;

  static LfgDungeonGroupEntry Load(const DbcFile &f, std::uint32_t row);
};

struct LfgDungeonExpansionEntry {

  std::uint32_t id;
  std::uint32_t lfg_id;
  std::uint32_t expansion;
  std::uint32_t random_id;
  std::uint32_t hard_level_min;
  std::uint32_t hard_level_max;
  std::uint32_t target_level_min;
  std::uint32_t target_level_max;

  static LfgDungeonExpansionEntry Load(const DbcFile &f, std::uint32_t row);
};

struct MailTemplateEntry {

  std::uint32_t id;
  std::string_view subject;
  std::string_view body;

  static MailTemplateEntry Load(const DbcFile &f, std::uint32_t row);
};

struct PowerDisplayEntry {
  static constexpr std::uint32_t kRetailFieldCount = 6u;
  static constexpr std::uint32_t kRetailRecordSize = 15u;

  std::uint32_t id;
  std::uint32_t actual_type;
  std::string_view tag;
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;

  static PowerDisplayEntry Load(const DbcFile &f, std::uint32_t row);
};

struct TeamContributionPointsEntry {

  std::uint32_t id;
  float data;

  static TeamContributionPointsEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CreatureSpellDataEntry {

  std::uint32_t id;
  std::array<std::uint32_t, 8> raw_fields;

  static CreatureSpellDataEntry Load(const DbcFile &f, std::uint32_t row);
};

struct DungeonEncounterEntry {
  std::uint32_t id;
  std::uint32_t map_id;
  std::uint32_t difficulty;
  std::uint32_t order_index;
  std::uint32_t bit;
  std::string_view name;
  std::uint32_t spell_icon_id;

  static DungeonEncounterEntry Load(const DbcFile &f, std::uint32_t row);
};

struct AchievementCategoryEntry {

  std::uint32_t id;
  std::uint32_t parent_category;
  std::string_view name;
  std::uint32_t sort_order;

  static AchievementCategoryEntry Load(const DbcFile &f, std::uint32_t row);
};

struct CurrencyCategoryEntry {

  std::uint32_t id;
  std::uint32_t flags;
  std::string_view name;

  static CurrencyCategoryEntry Load(const DbcFile &f, std::uint32_t row);
};

}


#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "dbc_schema_decode.h"

namespace openwow::data::dbc {

OPENWOW_DBC_SCHEMA(SpellDurationEntry,
  DBC_U32(id, 0)
  DBC_I32(duration, 1)
  DBC_I32(duration_per_level, 2)
  DBC_I32(max_duration, 3)
)

OPENWOW_DBC_SCHEMA(SpellRadiusEntry,
  DBC_U32(id, 0)
  DBC_F32(radius, 1)
  DBC_F32(radius_per_level, 2)
  DBC_F32(max_radius, 3)
)

OPENWOW_DBC_SCHEMA(SpellRuneCostEntry,
  DBC_U32(id, 0)
  DBC_U32(blood, 1)
  DBC_U32(unholy, 2)
  DBC_U32(frost, 3)
  DBC_U32(runic_power, 4)
)

OPENWOW_DBC_SCHEMA(SpellShapeshiftFormEntry,
  DBC_U32(id, 0)
  DBC_U32(bonus_action_bar, 1)
  DBC_LOCALIZED(name, 2)
  DBC_U32(flags, 19)
  DBC_U32(creature_type, 20)
  DBC_U32(attack_icon_id, 21)
  DBC_U32(combat_round_time, 22)
  DBC_U32_ARRAY(creature_display_id, 23)
  DBC_U32_ARRAY(override_actions, 27)
)

OPENWOW_DBC_SCHEMA(SpellItemEnchantmentEntry,
    e.id = f.GetUInt32(row, 0);
    for (int i = 0; i < 3; ++i) {
      e.type[i]     = f.GetUInt32(row, 1 + i);
      e.amount[i]   = f.GetInt32(row, 4 + i);
      e.amount_max[i] = f.GetInt32(row, 7 + i);
      e.spell_id[i] = f.GetUInt32(row, 10 + i);
    }
    e.field_13              = f.GetUInt32(row, 13);
    e.description           = f.GetLocalizedString(row, 14);
    e.aura_id               = f.GetUInt32(row, 31);
    e.slot                  = f.GetUInt32(row, 32);
    e.gem_id                = f.GetUInt32(row, 33);
    e.enchantment_condition = f.GetUInt32(row, 34);
    DBC_U32_ARRAY(tail_fields, 35)
)

OPENWOW_DBC_SCHEMA(FactionEntry,
    e.id                  = f.GetUInt32(row, 0);
    e.reputation_list_id  = f.GetInt32(row, 1);
    for (int i = 0; i < 4; ++i) {
      e.base_rep_race_mask[i]  = f.GetUInt32(row, 2 + i);
      e.base_rep_class_mask[i] = f.GetUInt32(row, 6 + i);
      e.base_rep_value[i]      = f.GetInt32(row, 10 + i);
      e.reputation_flags[i]    = f.GetUInt32(row, 14 + i);
    }
    e.parent_faction_id   = f.GetUInt32(row, 18);
    e.parent_faction_mod0 = f.GetFloat(row, 19);
    e.parent_faction_mod1 = f.GetFloat(row, 20);
    e.parent_faction_cap0 = f.GetUInt32(row, 21);
    e.parent_faction_cap1 = f.GetUInt32(row, 22);
    e.name                = f.GetLocalizedString(row, 23);
    e.description         = f.GetLocalizedString(row, 40);
)

OPENWOW_DBC_SCHEMA(FactionTemplateEntry,
    e.id            = f.GetUInt32(row, 0);
    e.faction       = f.GetUInt32(row, 1);
    e.flags         = f.GetUInt32(row, 2);
    e.faction_group = f.GetUInt32(row, 3);
    e.friend_group  = f.GetUInt32(row, 4);
    e.enemy_group   = f.GetUInt32(row, 5);
    for (int i = 0; i < 4; ++i) {
      e.enemies[i] = f.GetUInt32(row, 6 + i);
      e.friends[i] = f.GetUInt32(row, 10 + i);
    }
)

OPENWOW_DBC_SCHEMA(ChatChannelsEntry,
  DBC_U32(id, 0)
  DBC_U32(flags, 1)
  DBC_U32(faction_group, 2)
  DBC_LOCALIZED(pattern, 3)
  DBC_LOCALIZED(name, 20)
)

OPENWOW_DBC_SCHEMA(ChatProfanityEntry,
  DBC_U32(id, 0)
  DBC_STRING(text, 1)
  DBC_U32(language, 2)
)

OPENWOW_DBC_SCHEMA(QuestSortEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
)

OPENWOW_DBC_SCHEMA(CharTitlesEntry,
  DBC_U32(id, 0)
  DBC_U32(condition_id, 1)
  DBC_LOCALIZED(name_male, 2)
  DBC_LOCALIZED(name_female, 19)
  DBC_U32(mask_id, 36)
)

OPENWOW_DBC_SCHEMA(CurrencyTypesEntry,
  DBC_U32(id, 0)
  DBC_U32(item_id, 1)
  DBC_U32(category, 2)
  DBC_U32(bit_index, 3)
)

OPENWOW_DBC_SCHEMA(TotemCategoryEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
  DBC_U32(totem_category_type, 18)
  DBC_U32(totem_category_mask, 19)
)

OPENWOW_DBC_SCHEMA(GemPropertiesEntry,
  DBC_U32(id, 0)
  DBC_U32(enchant_id, 1)
  DBC_U32(maxcount_inv, 2)
  DBC_U32(maxcount_item, 3)
  DBC_U32(type, 4)
)

OPENWOW_DBC_SCHEMA(GlyphPropertiesEntry,
  DBC_U32(id, 0)
  DBC_U32(spell_id, 1)
  DBC_U32(type, 2)
  DBC_U32(spell_icon_id, 3)
)

OPENWOW_DBC_SCHEMA(GlyphSlotEntry,
  DBC_U32(id, 0)
  DBC_U32(type, 1)
  DBC_U32(order, 2)
)

OPENWOW_DBC_SCHEMA(LockEntry,
    e.id = f.GetUInt32(row, 0);
    for (int i = 0; i < 8; ++i) {
      e.type[i]   = f.GetUInt32(row, 1 + i);
      e.index[i]  = f.GetUInt32(row, 9 + i);
      e.skill[i]  = f.GetUInt32(row, 17 + i);
      e.action[i] = f.GetUInt32(row, 25 + i);
    }
)

OPENWOW_DBC_SCHEMA(OverrideSpellDataEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(spell, 1)
  DBC_U32(flags, 11)
)

OPENWOW_DBC_SCHEMA(SummonPropertiesEntry,
  DBC_U32(id, 0)
  DBC_U32(category, 1)
  DBC_U32(faction, 2)
  DBC_U32(type, 3)
  DBC_U32(slot, 4)
  DBC_U32(flags, 5)
)

OPENWOW_DBC_SCHEMA(ItemRandomPropertiesEntry,
  DBC_U32(id, 0)
  DBC_STRING(internal_name, 1)
  DBC_U32_ARRAY(enchantment, 2)
  DBC_LOCALIZED(name, 7)
)

OPENWOW_DBC_SCHEMA(ItemRandomSuffixEntry,
    e.id            = f.GetUInt32(row, 0);
    e.name          = f.GetLocalizedString(row, 1);
    e.internal_name = f.GetString(row, 18);
    for (int i = 0; i < 5; ++i) {
      e.enchantment[i]    = f.GetUInt32(row, 19 + i);
      e.allocation_pct[i] = f.GetUInt32(row, 24 + i);
    }
)

OPENWOW_DBC_SCHEMA(ItemExtendedCostEntry,
    e.id           = f.GetUInt32(row, 0);
    e.honor_points = f.GetUInt32(row, 1);
    e.arena_points = f.GetUInt32(row, 2);
    e.arena_slot   = f.GetUInt32(row, 3);
    for (int i = 0; i < 5; ++i) {
      e.item_id[i]    = f.GetUInt32(row, 4 + i);
      e.item_count[i] = f.GetUInt32(row, 9 + i);
    }
    e.personal_arena_rating = f.GetUInt32(row, 14);
    e.item_purchase_group   = f.GetUInt32(row, 15);
)

OPENWOW_DBC_SCHEMA(CreatureFamilyEntry,
  DBC_U32(id, 0)
  DBC_F32(min_scale, 1)
  DBC_U32(min_scale_level, 2)
  DBC_F32(max_scale, 3)
  DBC_U32(max_scale_level, 4)
  DBC_U32(skill_line_0, 5)
  DBC_U32(skill_line_1, 6)
  DBC_U32(pet_food_mask, 7)
  DBC_I32(pet_talent_type, 8)
  DBC_U32(category, 9)
  DBC_LOCALIZED(name, 10)
  DBC_STRING(icon_file, 27)
)

OPENWOW_DBC_SCHEMA(CreatureTypeEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
  DBC_U32(flags, 18)
)

#define OPENWOW_IMPL_GT_LOAD(Name)                                \
Name##Entry Name##Entry::Load(const DbcFile& f, std::uint32_t row) { \
  Name##Entry e{};                                                 \
  e.id    = row;                                                   \
  e.value = f.GetFloat(row, 0);                                    \
  return e;                                                        \
}

OPENWOW_IMPL_GT_LOAD(GtBarberShopCostBase)
OPENWOW_IMPL_GT_LOAD(GtChanceToMeleeCrit)
OPENWOW_IMPL_GT_LOAD(GtChanceToMeleeCritBase)
OPENWOW_IMPL_GT_LOAD(GtChanceToSpellCrit)
OPENWOW_IMPL_GT_LOAD(GtChanceToSpellCritBase)
OPENWOW_IMPL_GT_LOAD(GtOCTRegenHP)
OPENWOW_IMPL_GT_LOAD(GtOCTRegenMP)
OPENWOW_IMPL_GT_LOAD(GtRegenHPPerSpt)
OPENWOW_IMPL_GT_LOAD(GtRegenMPPerSpt)
OPENWOW_IMPL_GT_LOAD(GtNPCManaCostScaler)

#undef OPENWOW_IMPL_GT_LOAD

OPENWOW_DBC_SCHEMA(GtOCTClassCombatRatingScalarEntry,
  DBC_U32(id, 0)
  DBC_F32(value, 1)
)

OPENWOW_DBC_SCHEMA(SpellCategoryEntry,
  DBC_U32(id, 0)
  DBC_U32(flags, 1)
)

OPENWOW_DBC_SCHEMA(SpellDifficultyEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(spell_id, 1)
)

OPENWOW_DBC_SCHEMA(SpellDispelTypeEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
  DBC_U32(mask, 18)
  DBC_U32(immunity_possible, 19)
  DBC_STRING(internal_name, 20)
)

OPENWOW_DBC_SCHEMA(SpellMechanicEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
)

OPENWOW_DBC_SCHEMA(SpellFocusObjectEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
)

SpellItemEnchantmentConditionEntry SpellItemEnchantmentConditionEntry::Load(
    const DbcFile& f, std::uint32_t row) {

  constexpr std::uint32_t kIdOffset = 0u;
  constexpr std::uint32_t kLeftOperandTypeOffset = 4u;
  constexpr std::uint32_t kLeftOperandOffset = 9u;
  constexpr std::uint32_t kOperatorTypeOffset = 29u;
  constexpr std::uint32_t kRightOperandTypeOffset = 34u;
  constexpr std::uint32_t kRightOperandOffset = 39u;
  constexpr std::uint32_t kLogicOffset = 59u;

  SpellItemEnchantmentConditionEntry e{};
  e.id = f.GetUInt32AtOffset(row, kIdOffset);
  for (std::size_t i = 0; i < kConditionCount; ++i) {
    const auto byte_index = static_cast<std::uint32_t>(i);
    const auto word_offset =
        byte_index * static_cast<std::uint32_t>(DbcHeader::kWordSize);
    e.lt_operand_type[i] =
        f.GetByte(row, kLeftOperandTypeOffset + byte_index);
    e.lt_operand[i] =
        f.GetUInt32AtOffset(row, kLeftOperandOffset + word_offset);
    e.operator_type[i] =
        f.GetByte(row, kOperatorTypeOffset + byte_index);
    e.rt_operand_type[i] =
        f.GetByte(row, kRightOperandTypeOffset + byte_index);
    e.rt_operand[i] =
        f.GetUInt32AtOffset(row, kRightOperandOffset + word_offset);
    e.logic[i] = f.GetByte(row, kLogicOffset + byte_index);
  }
  return e;
}

OPENWOW_DBC_SCHEMA(SpellDescriptionVariablesEntry,
  DBC_U32(id, 0)
  DBC_STRING(variables, 1)
)

SpellChainEffectsEntry SpellChainEffectsEntry::Load(const DbcFile& f, std::uint32_t row) {

  constexpr std::uint32_t kAlphaOffset = 156u;
  constexpr std::uint32_t kRedOffset = 157u;
  constexpr std::uint32_t kGreenOffset = 158u;
  constexpr std::uint32_t kBlueOffset = 159u;
  constexpr std::uint32_t kBlendModeOffset = 160u;
  constexpr std::uint32_t kComboOffset = 161u;
  constexpr std::uint32_t kRenderLayerOffset = 165u;
  constexpr std::uint32_t kTextureLengthOffset = 169u;
  constexpr std::uint32_t kWavePhaseOffset = 173u;

  SpellChainEffectsEntry e{};

  e.id                              = f.GetUInt32(row, 0);
  e.avg_seg_len                     = f.GetFloat(row, 1);
  e.width                           = f.GetFloat(row, 2);
  e.noise_scale                     = f.GetFloat(row, 3);
  e.tex_coord_scale                 = f.GetFloat(row, 4);
  e.seg_duration                    = f.GetUInt32(row, 5);
  e.seg_delay                       = f.GetUInt32(row, 6);
  e.texture                         = f.GetString(row, 7);
  e.flags                           = f.GetUInt32(row, 8);
  e.joint_count                     = f.GetUInt32(row, 9);
  e.joint_offset_radius             = f.GetFloat(row, 10);
  e.joints_per_minor_joint          = f.GetUInt32(row, 11);
  e.minor_joints_per_major_joint    = f.GetUInt32(row, 12);
  e.minor_joint_scale               = f.GetFloat(row, 13);
  e.major_joint_scale               = f.GetFloat(row, 14);
  e.joint_move_speed                = f.GetFloat(row, 15);
  e.joint_smoothness                = f.GetFloat(row, 16);
  e.min_duration_between_joint_jumps = f.GetFloat(row, 17);
  e.max_duration_between_joint_jumps = f.GetFloat(row, 18);
  e.wave_height                     = f.GetFloat(row, 19);
  e.wave_freq                       = f.GetFloat(row, 20);
  e.wave_speed                      = f.GetFloat(row, 21);
  e.min_wave_angle                  = f.GetFloat(row, 22);
  e.max_wave_angle                  = f.GetFloat(row, 23);
  e.min_wave_spin                   = f.GetFloat(row, 24);
  e.max_wave_spin                   = f.GetFloat(row, 25);
  e.arc_height                      = f.GetFloat(row, 26);
  e.min_arc_angle                   = f.GetFloat(row, 27);
  e.max_arc_angle                   = f.GetFloat(row, 28);
  e.min_arc_spin                    = f.GetFloat(row, 29);
  e.max_arc_spin                    = f.GetFloat(row, 30);
  e.delay_between_effects           = f.GetFloat(row, 31);
  e.min_flicker_on_duration         = f.GetFloat(row, 32);
  e.max_flicker_on_duration         = f.GetFloat(row, 33);
  e.min_flicker_off_duration        = f.GetFloat(row, 34);
  e.max_flicker_off_duration        = f.GetFloat(row, 35);
  e.pulse_speed                     = f.GetFloat(row, 36);
  e.pulse_on_length                 = f.GetFloat(row, 37);
  e.pulse_fade_length               = f.GetFloat(row, 38);

  e.alpha = f.GetByte(row, kAlphaOffset);
  e.red = f.GetByte(row, kRedOffset);
  e.green = f.GetByte(row, kGreenOffset);
  e.blue = f.GetByte(row, kBlueOffset);
  e.blend_mode = f.GetByte(row, kBlendModeOffset);

  e.combo = f.GetStringAtOffset(row, kComboOffset);
  e.render_layer = f.GetUInt32AtOffset(row, kRenderLayerOffset);
  e.texture_length = f.GetFloatAtOffset(row, kTextureLengthOffset);
  e.wave_phase = f.GetFloatAtOffset(row, kWavePhaseOffset);

  return e;
}

OPENWOW_DBC_SCHEMA(SpellMissileEntry,
  DBC_U32(id, 0)
  DBC_U32(flags, 1)
  DBC_F32(default_pitch_min, 2)
  DBC_F32(default_pitch_max, 3)
  DBC_F32(default_speed_min, 4)
  DBC_F32(default_speed_max, 5)
  DBC_F32(randomize_facing_min, 6)
  DBC_F32(randomize_facing_max, 7)
  DBC_F32(randomize_pitch_min, 8)
  DBC_F32(randomize_pitch_max, 9)
  DBC_F32(randomize_speed_min, 10)
  DBC_F32(randomize_speed_max, 11)
  DBC_F32(gravity, 12)
  DBC_F32(max_duration, 13)
  DBC_F32(collision_radius, 14)
)

OPENWOW_DBC_SCHEMA(SpellMissileMotionEntry,
  DBC_U32(id, 0)
  DBC_STRING(script_name, 1)
  DBC_STRING(script_body, 2)
  DBC_U32(field_3, 3)
  DBC_U32(instance_count, 4)
)

OPENWOW_DBC_SCHEMA(SpellEffectCameraShakesEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(camera_shake, 1)
)

OPENWOW_DBC_SCHEMA(SpellVisualKitAreaModelEntry,
  DBC_U32(id, 0)
  DBC_STRING(model_path, 1)
  DBC_U32(aux_u32, 2)
)

OPENWOW_DBC_SCHEMA(SpellVisualKitModelAttachEntry,
  DBC_U32(id, 0)
  DBC_U32(parent_spell_visual_kit_id, 1)
  DBC_U32(spell_visual_effect_name_id, 2)
  DBC_U32(attachment_id, 3)
  DBC_F32(offset_x, 4)
  DBC_F32(offset_y, 5)
  DBC_F32(offset_z, 6)
  DBC_F32(yaw, 7)
  DBC_F32(pitch, 8)
  DBC_F32(roll, 9)
)

OPENWOW_DBC_SCHEMA(ItemEntry,
  DBC_U32(id, 0)
  DBC_U32(class_id, 1)
  DBC_U32(subclass_id, 2)
  DBC_I32(sound_override_subclass, 3)
  DBC_U32(material, 4)
  DBC_U32(display_info_id, 5)
  DBC_U32(inventory_type, 6)
  DBC_U32(sheathe_type, 7)
)

OPENWOW_DBC_SCHEMA(ItemBagFamilyEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
)

OPENWOW_DBC_SCHEMA(ItemClassEntry,
  DBC_U32(id, 0)
  DBC_U32(subclass_map, 1)
  DBC_U32(flags, 2)
  DBC_LOCALIZED(name, 3)
)

OPENWOW_DBC_SCHEMA(ItemSubClassEntry,
    e.class_id             = f.GetUInt32(row, 0);
    e.subclass_id          = f.GetUInt32(row, 1);
    e.id = ItemSubClassEntry::ComposeKey(e.class_id, e.subclass_id);
    e.prereq_proficiency   = f.GetUInt32(row, 2);
    e.postreq_proficiency  = f.GetUInt32(row, 3);
    e.flags                = f.GetUInt32(row, 4);
    e.display_flags        = f.GetUInt32(row, 5);
    e.weapon_parry_seq     = f.GetUInt32(row, 6);
    e.weapon_ready_seq     = f.GetUInt32(row, 7);
    e.weapon_attack_seq    = f.GetUInt32(row, 8);
    e.weapon_swing_size    = f.GetUInt32(row, 9);
    e.display_name         = f.GetLocalizedString(row, 10);
    e.verbose_name         = f.GetLocalizedString(row, 27);
)

OPENWOW_DBC_SCHEMA(ItemSubClassMaskEntry,
  DBC_U32(class_id, 0)
  DBC_ROW_ID()
  DBC_U32(mask, 1)
  DBC_LOCALIZED(name, 2)
)

OPENWOW_DBC_SCHEMA(ItemCondExtCostsEntry,
  DBC_U32(id, 0)
  DBC_U32(cond_extended_cost, 1)
  DBC_U32(item_extended_cost_entry, 2)
  DBC_U32(unknown_3, 3)
)

OPENWOW_DBC_SCHEMA(ItemGroupSoundsEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(sound, 1)
)

OPENWOW_DBC_SCHEMA(ItemLimitCategoryEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
  DBC_U32(quantity, 18)
  DBC_U32(flags, 19)
)

OPENWOW_DBC_SCHEMA(ItemPetFoodEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
)

OPENWOW_DBC_SCHEMA(ItemPurchaseGroupEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(item_id, 1)
  DBC_LOCALIZED(name, 9)
)

OPENWOW_DBC_SCHEMA(ItemVisualEffectsEntry,
  DBC_U32(id, 0)
  DBC_STRING(model, 1)
)

OPENWOW_DBC_SCHEMA(ItemVisualsEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(slot, 1)
)

OPENWOW_DBC_SCHEMA(SkillRaceClassInfoEntry,
  DBC_U32(id, 0)
  DBC_U32(skill_id, 1)
  DBC_U32(race_mask, 2)
  DBC_U32(class_mask, 3)
  DBC_U32(flags, 4)
  DBC_U32(min_level, 5)
  DBC_U32(skill_tier_id, 6)
  DBC_U32(skill_cost_index, 7)
)

OPENWOW_DBC_SCHEMA(SkillTiersEntry,
    e.id = f.GetUInt32(row, 0);
    for (int i = 0; i < 16; ++i) {
      e.cost[i]  = f.GetUInt32(row, 1 + i);
      e.value[i] = f.GetUInt32(row, 17 + i);
    }
)

OPENWOW_DBC_SCHEMA(SkillCostsDataEntry,
  DBC_U32(id, 0)
  DBC_U32(skill_costs_id, 1)
  DBC_U32(cost0, 2)
  DBC_U32(cost1, 3)
  DBC_U32(cost2, 4)
)

OPENWOW_DBC_SCHEMA(SkillLineCategoryEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
  DBC_U32(sort_index, 18)
)

OPENWOW_DBC_SCHEMA(QuestFactionRewardEntry,
  DBC_U32(id, 0)
  DBC_I32_ARRAY(difficulty, 1)
)

OPENWOW_DBC_SCHEMA(QuestInfoEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
)

OPENWOW_DBC_SCHEMA(QuestXPEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(difficulty, 1)
)

OPENWOW_DBC_SCHEMA(RandPropPointsEntry,
    e.id = f.GetUInt32(row, 0);
    for (int i = 0; i < 5; ++i) {
      e.epic[i]     = f.GetUInt32(row, 1 + i);
      e.rare[i]     = f.GetUInt32(row, 6 + i);
      e.uncommon[i] = f.GetUInt32(row, 11 + i);
    }
)

OPENWOW_DBC_SCHEMA(ScalingStatDistributionEntry,
    e.id = f.GetUInt32(row, 0);
    for (int i = 0; i < 10; ++i) {
      e.stat_id[i] = f.GetInt32(row, 1 + i);
      e.bonus[i]   = f.GetUInt32(row, 11 + i);
    }
    e.max_level = f.GetUInt32(row, 21);
)

OPENWOW_DBC_SCHEMA(ScalingStatValuesEntry,
    e.id = f.GetUInt32(row, 0);
    for (std::uint32_t i = 0; i < e.values.size(); ++i)
      e.values[i] = f.GetUInt32(row, i + 1);
)

std::uint32_t ScalingStatValuesEntry::GetField(const std::uint32_t field) const {
  if (field == 0u) {
    return id;
  }
  return field <= values.size() ? values[field - 1u] : 0u;
}

OPENWOW_DBC_SCHEMA(ResistancesEntry,
  DBC_U32(id, 0)
  DBC_U32(flags, 1)
  DBC_U32(fizzle_sound_id, 2)
  DBC_LOCALIZED(name, 3)
)

OPENWOW_DBC_SCHEMA(MapDifficultyEntry,
  DBC_U32(id, 0)
  DBC_U32(map_id, 1)
  DBC_U32(difficulty, 2)
  DBC_LOCALIZED(message, 3)
  DBC_U32(raid_duration, 20)
  DBC_U32(max_players, 21)
  DBC_STRING(difficulty_string, 22)
)

OPENWOW_DBC_SCHEMA(PvpDifficultyEntry,
  DBC_U32(id, 0)
  DBC_U32(map_id, 1)
  DBC_U32(range_index, 2)
  DBC_U32(min_level, 3)
  DBC_U32(max_level, 4)
  DBC_U32(unknown_14, 5)
)

OPENWOW_DBC_SCHEMA(AuctionHouseEntry,
  DBC_U32(id, 0)
  DBC_U32(faction_id, 1)
  DBC_U32(deposit_rate, 2)
  DBC_U32(consignment_rate, 3)
  DBC_LOCALIZED(name, 4)
)

OPENWOW_DBC_SCHEMA(BankBagSlotPricesEntry,
  DBC_U32(id, 0)
  DBC_U32(cost, 1)
)

OPENWOW_DBC_SCHEMA(LfgDungeonsEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
  DBC_U32(min_level, 18)
  DBC_U32(max_level, 19)
  DBC_U32(rec_level, 20)
  DBC_U32(rec_min_level, 21)
  DBC_U32(rec_max_level, 22)
  DBC_U32(map_id, 23)
  DBC_U32(difficulty, 24)
  DBC_U32(flags, 25)
  DBC_U32(type_id, 26)
  DBC_I32(faction, 27)
  DBC_STRING(texture_filename, 28)
  DBC_U32(expansion_level, 29)
  DBC_U32(order_index, 30)
  DBC_U32(group_id, 31)
  DBC_LOCALIZED(description, 32)
)

OPENWOW_DBC_SCHEMA(LfgDungeonGroupEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
  DBC_U32(order_index, 18)
  DBC_U32(parent_group_id, 19)
  DBC_U32(type_id, 20)
)

OPENWOW_DBC_SCHEMA(LfgDungeonExpansionEntry,
  DBC_U32(id, 0)
  DBC_U32(lfg_id, 1)
  DBC_U32(expansion, 2)
  DBC_U32(random_id, 3)
  DBC_U32(hard_level_min, 4)
  DBC_U32(hard_level_max, 5)
  DBC_U32(target_level_min, 6)
  DBC_U32(target_level_max, 7)
)

OPENWOW_DBC_SCHEMA(MailTemplateEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(subject, 1)
  DBC_LOCALIZED(body, 18)
)

PowerDisplayEntry PowerDisplayEntry::Load(const DbcFile& f, std::uint32_t row) {

  constexpr std::uint32_t kRedOffset = 12u;
  constexpr std::uint32_t kGreenOffset = 13u;
  constexpr std::uint32_t kBlueOffset = 14u;

  PowerDisplayEntry e{};
  e.id          = f.GetUInt32(row, 0);
  e.actual_type = f.GetUInt32(row, 1);
  e.tag         = f.GetString(row, 2);
  e.red         = f.GetByte(row, kRedOffset);
  e.green       = f.GetByte(row, kGreenOffset);
  e.blue        = f.GetByte(row, kBlueOffset);
  return e;
}

OPENWOW_DBC_SCHEMA(TeamContributionPointsEntry,
  DBC_U32(id, 0)
  DBC_F32(data, 1)
)

OPENWOW_DBC_SCHEMA(CreatureSpellDataEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(raw_fields, 1)
)

OPENWOW_DBC_SCHEMA(DungeonEncounterEntry,
  DBC_U32(id, 0)
  DBC_U32(map_id, 1)
  DBC_U32(difficulty, 2)
  DBC_U32(order_index, 3)
  DBC_U32(bit, 4)
  DBC_LOCALIZED(name, 5)
  DBC_U32(spell_icon_id, 22)
)

OPENWOW_DBC_SCHEMA(AchievementCategoryEntry,
  DBC_U32(id, 0)
  DBC_U32(parent_category, 1)
  DBC_LOCALIZED(name, 2)
  DBC_U32(sort_order, 19)
)

OPENWOW_DBC_SCHEMA(CurrencyCategoryEntry,
  DBC_U32(id, 0)
  DBC_U32(flags, 1)
  DBC_LOCALIZED(name, 2)
)

}

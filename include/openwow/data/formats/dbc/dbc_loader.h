#pragma once

#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::data::dbc {

class DbcLoader {
 public:
  int LoadAll(const openwow::vfs::VirtualFileSystem& vfs,
              const std::string& dbc_root_path = "DBFilesClient");
  bool LoadAreaTableRetailStrict(const openwow::vfs::VirtualFileSystem& vfs,
                                 const std::string& path = "DBFilesClient/AreaTable.dbc");
  [[nodiscard]] const openwow::vfs::VirtualFileSystem* vfs() const {
    return vfs_;
  }

  [[nodiscard]] const DbcStore<AreaTableEntry>&              area_table() const { return core_.area_table_; }
  [[nodiscard]] const DbcStore<CharStartOutfitEntry>&        char_start_outfit() const { return core_.char_start_outfit_; }
  [[nodiscard]] const DbcStore<ChrClassesEntry>&             chr_classes() const { return core_.chr_classes_; }
  [[nodiscard]] const DbcStore<ChrRacesEntry>&               chr_races() const { return core_.chr_races_; }
  [[nodiscard]] const DbcStore<CinematicSequencesEntry>&     cinematic_sequences() const { return core_.cinematic_sequences_; }
  [[nodiscard]] const DbcStore<CinematicCameraEntry>&        cinematic_camera() const { return core_.cinematic_camera_; }
  [[nodiscard]] const DbcStore<CreatureDisplayInfoEntry>&    creature_display_info() const { return core_.creature_display_info_; }
  [[nodiscard]] const DbcStore<CreatureModelDataEntry>&      creature_model_data() const { return core_.creature_model_data_; }
  [[nodiscard]] const DbcStore<GameObjectDisplayInfoEntry>&  gameobject_display_info() const { return core_.gameobject_display_info_; }
  [[nodiscard]] const DbcStore<ItemDisplayInfoEntry>&        item_display_info() const { return core_.item_display_info_; }
  [[nodiscard]] const DbcStore<LightEntry>&                  light() const { return core_.light_; }
  [[nodiscard]] const DbcStore<LightIntBandEntry>&           light_int_band() const { return core_.light_int_band_; }
  [[nodiscard]] const DbcStore<LightFloatBandEntry>&         light_float_band() const { return core_.light_float_band_; }
  [[nodiscard]] const DbcStore<LoadingScreensEntry>&         loading_screens() const { return core_.loading_screens_; }
  [[nodiscard]] const DbcStore<MapEntry>&                    map() const { return core_.map_; }
  [[nodiscard]] const DbcStore<SoundEntriesEntry>&           sound_entries() const { return core_.sound_entries_; }
  [[nodiscard]] const DbcStore<SpellEntry>&                  spell() const { return core_.spell_; }
  [[nodiscard]] const DbcStore<SpellCastTimesEntry>&         spell_cast_times() const { return core_.spell_cast_times_; }
  [[nodiscard]] const DbcStore<SpellIconEntry>&              spell_icon() const { return core_.spell_icon_; }
  [[nodiscard]] const DbcStore<SpellRangeEntry>&             spell_range() const { return core_.spell_range_; }
  [[nodiscard]] const DbcStore<SpellVisualEntry>&            spell_visual() const { return core_.spell_visual_; }
  [[nodiscard]] const DbcStore<SpellVisualKitEntry>&         spell_visual_kit() const { return core_.spell_visual_kit_; }
  [[nodiscard]] const DbcStore<SpellVisualEffectNameEntry>&  spell_visual_effect_name() const { return core_.spell_visual_effect_name_; }
  [[nodiscard]] const DbcStore<AnimationDataEntry>&          animation_data() const { return core_.animation_data_; }
  [[nodiscard]] const DbcStore<TalentEntry>&                talent() const { return core_.talent_; }
  [[nodiscard]] const DbcStore<TalentTabEntry>&             talent_tab() const { return core_.talent_tab_; }
  [[nodiscard]] const DbcStore<TaxiNodesEntry>&             taxi_nodes() const { return core_.taxi_nodes_; }
  [[nodiscard]] const DbcStore<TaxiPathEntry>&              taxi_path() const { return core_.taxi_path_; }
  [[nodiscard]] const DbcStore<TaxiPathNodeEntry>&          taxi_path_node() const { return core_.taxi_path_node_; }
  [[nodiscard]] const DbcStore<SkillLineEntry>&             skill_line() const { return core_.skill_line_; }
  [[nodiscard]] const DbcStore<SkillLineAbilityEntry>&      skill_line_ability() const { return core_.skill_line_ability_; }
  [[nodiscard]] const DbcStore<GtCombatRatingsEntry>&       gt_combat_ratings() const { return core_.gt_combat_ratings_; }

  [[nodiscard]] const DbcStore<EmotesEntry>&                emotes() const { return extended_.emotes_; }
  [[nodiscard]] const DbcStore<EmotesTextEntry>&            emotes_text() const { return extended_.emotes_text_; }
  [[nodiscard]] const DbcStore<LightParamsEntry>&           light_params() const { return extended_.light_params_; }
  [[nodiscard]] const DbcStore<AchievementEntry>&           achievement() const { return extended_.achievement_; }
  [[nodiscard]] const DbcStore<AchievementCriteriaEntry>&   achievement_criteria() const { return extended_.achievement_criteria_; }
  [[nodiscard]] const DbcStore<WorldMapAreaEntry>&          world_map_area() const { return extended_.world_map_area_; }
  [[nodiscard]] const DbcStore<LanguagesEntry>&             languages() const { return extended_.languages_; }
  [[nodiscard]] const DbcStore<LanguageWordsEntry>&         language_words() const { return extended_.language_words_; }
  [[nodiscard]] const DbcStore<NameGenEntry>&               name_gen() const { return extended_.name_gen_; }
  [[nodiscard]] const DbcStore<NamesProfanityEntry>&        names_profanity() const { return extended_.names_profanity_; }
  [[nodiscard]] const DbcStore<NamesReservedEntry>&         names_reserved() const { return extended_.names_reserved_; }
  [[nodiscard]] const DbcStore<CfgCategoriesEntry>&         cfg_categories() const { return extended_.cfg_categories_; }
  [[nodiscard]] const DbcStore<CfgConfigsEntry>&            cfg_configs() const { return extended_.cfg_configs_; }
  [[nodiscard]] const DbcStore<BannedAddOnsEntry>&          banned_addons() const { return extended_.banned_addons_; }
  [[nodiscard]] const DbcStore<GameTablesEntry>&            game_tables() const { return extended_.game_tables_; }
  [[nodiscard]] const DbcStore<GameTipsEntry>&              game_tips() const { return extended_.game_tips_; }
  [[nodiscard]] const DbcStore<StationeryEntry>&            stationery() const { return extended_.stationery_; }
  [[nodiscard]] const DbcStore<ServerMessagesEntry>&        server_messages() const { return extended_.server_messages_; }
  [[nodiscard]] const DbcStore<PackageEntry>&               package() const { return extended_.package_; }
  [[nodiscard]] const DbcStore<SpamMessagesEntry>&          spam_messages() const { return extended_.spam_messages_; }
  [[nodiscard]] const DbcStore<DanceMovesEntry>&            dance_moves() const { return extended_.dance_moves_; }
  [[nodiscard]] const DbcStore<ExhaustionEntry>&            exhaustion() const { return extended_.exhaustion_; }
  [[nodiscard]] const DbcStore<StartupStringsEntry>&        startup_strings() const { return extended_.startup_strings_; }
  [[nodiscard]] const DbcStore<StringLookupsEntry>&         string_lookups() const { return extended_.string_lookups_; }
  [[nodiscard]] const DbcStore<FileDataEntry>&              file_data() const { return extended_.file_data_; }
  [[nodiscard]] const DbcStore<VideoHardwareEntry>&         video_hardware() const { return extended_.video_hardware_; }
  [[nodiscard]] const DbcStore<HolidaysEntry>&              holidays() const { return extended_.holidays_; }
  [[nodiscard]] const DbcStore<HolidayNamesEntry>&          holiday_names() const { return extended_.holiday_names_; }
  [[nodiscard]] const DbcStore<HolidayDescriptionsEntry>&   holiday_descriptions() const { return extended_.holiday_descriptions_; }
  [[nodiscard]] const DbcStore<GMSurveyAnswersEntry>&       gm_survey_answers() const { return extended_.gm_survey_answers_; }
  [[nodiscard]] const DbcStore<GMSurveyCurrentSurveyEntry>& gm_survey_current_survey() const { return extended_.gm_survey_current_survey_; }
  [[nodiscard]] const DbcStore<GMSurveyQuestionsEntry>&     gm_survey_questions() const { return extended_.gm_survey_questions_; }
  [[nodiscard]] const DbcStore<GMSurveySurveysEntry>&       gm_survey_surveys() const { return extended_.gm_survey_surveys_; }
  [[nodiscard]] const DbcStore<GMTicketCategoryEntry>&      gm_ticket_category() const { return extended_.gm_ticket_category_; }
  [[nodiscard]] const DbcStore<DeclinedWordEntry>&          declined_word() const { return extended_.declined_word_; }
  [[nodiscard]] const DbcStore<DeclinedWordCasesEntry>&     declined_word_cases() const { return extended_.declined_word_cases_; }
  [[nodiscard]] const DbcStore<EmotesTextDataEntry>&        emotes_text_data() const { return extended_.emotes_text_data_; }
  [[nodiscard]] const DbcStore<EmotesTextSoundEntry>&       emotes_text_sound() const { return extended_.emotes_text_sound_; }
  [[nodiscard]] const DbcStore<AreaGroupEntry>&             area_group() const { return extended_.area_group_; }
  [[nodiscard]] const DbcStore<AreaPOIEntry>&               area_poi() const { return extended_.area_poi_; }
  [[nodiscard]] const DbcStore<FactionGroupEntry>&          faction_group() const { return extended_.faction_group_; }
  [[nodiscard]] const DbcStore<EnvironmentalDamageEntry>&   environmental_damage() const { return extended_.environmental_damage_; }
  [[nodiscard]] const DbcStore<LockTypeEntry>&              lock_type() const { return extended_.lock_type_; }
  [[nodiscard]] const DbcStore<PetPersonalityEntry>&        pet_personality() const { return extended_.pet_personality_; }

  [[nodiscard]] const DbcStore<SpellDurationEntry>&          spell_duration() const { return gameplay_.spell_duration_; }
  [[nodiscard]] const DbcStore<SpellRadiusEntry>&            spell_radius() const { return gameplay_.spell_radius_; }
  [[nodiscard]] const DbcStore<SpellRuneCostEntry>&          spell_rune_cost() const { return gameplay_.spell_rune_cost_; }
  [[nodiscard]] const DbcStore<SpellShapeshiftFormEntry>&    spell_shapeshift_form() const { return gameplay_.spell_shapeshift_form_; }
  [[nodiscard]] const DbcStore<SpellItemEnchantmentEntry>&   spell_item_enchantment() const { return gameplay_.spell_item_enchantment_; }
  [[nodiscard]] const DbcStore<FactionEntry>&                faction() const { return gameplay_.faction_; }
  [[nodiscard]] const DbcStore<FactionTemplateEntry>&        faction_template() const { return gameplay_.faction_template_; }
  [[nodiscard]] const DbcStore<ChatChannelsEntry>&           chat_channels() const { return gameplay_.chat_channels_; }
  [[nodiscard]] const DbcStore<ChatProfanityEntry>&          chat_profanity() const { return gameplay_.chat_profanity_; }
  [[nodiscard]] const DbcStore<QuestSortEntry>&              quest_sort() const { return gameplay_.quest_sort_; }
  [[nodiscard]] const DbcStore<CharTitlesEntry>&             char_titles() const { return gameplay_.char_titles_; }
  [[nodiscard]] const DbcStore<CurrencyTypesEntry>&          currency_types() const { return gameplay_.currency_types_; }
  [[nodiscard]] const DbcStore<TotemCategoryEntry>&          totem_category() const { return gameplay_.totem_category_; }
  [[nodiscard]] const DbcStore<GemPropertiesEntry>&          gem_properties() const { return gameplay_.gem_properties_; }
  [[nodiscard]] const DbcStore<GlyphPropertiesEntry>&        glyph_properties() const { return gameplay_.glyph_properties_; }
  [[nodiscard]] const DbcStore<GlyphSlotEntry>&              glyph_slot() const { return gameplay_.glyph_slot_; }
  [[nodiscard]] const DbcStore<LockEntry>&                   lock() const { return gameplay_.lock_; }
  [[nodiscard]] const DbcStore<OverrideSpellDataEntry>&      override_spell_data() const { return gameplay_.override_spell_data_; }
  [[nodiscard]] const DbcStore<SummonPropertiesEntry>&       summon_properties() const { return gameplay_.summon_properties_; }
  [[nodiscard]] const DbcStore<ItemRandomPropertiesEntry>&   item_random_properties() const { return gameplay_.item_random_properties_; }
  [[nodiscard]] const DbcStore<ItemRandomSuffixEntry>&       item_random_suffix() const { return gameplay_.item_random_suffix_; }
  [[nodiscard]] const DbcStore<ItemExtendedCostEntry>&       item_extended_cost() const { return gameplay_.item_extended_cost_; }
  [[nodiscard]] const DbcStore<CreatureFamilyEntry>&         creature_family() const { return gameplay_.creature_family_; }
  [[nodiscard]] const DbcStore<CreatureTypeEntry>&           creature_type() const { return gameplay_.creature_type_; }
  [[nodiscard]] const DbcStore<SpellCategoryEntry>&          spell_category() const { return gameplay_.spell_category_; }
  [[nodiscard]] const DbcStore<SpellDifficultyEntry>&        spell_difficulty() const { return gameplay_.spell_difficulty_; }
  [[nodiscard]] const DbcStore<SpellDispelTypeEntry>&        spell_dispel_type() const { return gameplay_.spell_dispel_type_; }
  [[nodiscard]] const DbcStore<SpellMechanicEntry>&          spell_mechanic() const { return gameplay_.spell_mechanic_; }
  [[nodiscard]] const DbcStore<SpellFocusObjectEntry>&       spell_focus_object() const { return gameplay_.spell_focus_object_; }
  [[nodiscard]] const DbcStore<SpellItemEnchantmentConditionEntry>& spell_item_enchantment_condition() const { return gameplay_.spell_item_enchantment_condition_; }
  [[nodiscard]] const DbcStore<SpellDescriptionVariablesEntry>& spell_description_variables() const { return gameplay_.spell_description_variables_; }
  [[nodiscard]] const DbcStore<SpellChainEffectsEntry>&      spell_chain_effects() const { return gameplay_.spell_chain_effects_; }
  [[nodiscard]] const DbcStore<SpellMissileEntry>&           spell_missile() const { return gameplay_.spell_missile_; }
  [[nodiscard]] const DbcStore<SpellMissileMotionEntry>&     spell_missile_motion() const { return gameplay_.spell_missile_motion_; }
  [[nodiscard]] const DbcStore<SpellEffectCameraShakesEntry>& spell_effect_camera_shakes() const { return gameplay_.spell_effect_camera_shakes_; }
  [[nodiscard]] const DbcStore<SpellVisualKitAreaModelEntry>& spell_visual_kit_area_model() const { return gameplay_.spell_visual_kit_area_model_; }
  [[nodiscard]] const DbcStore<SpellVisualKitModelAttachEntry>& spell_visual_kit_model_attach() const { return gameplay_.spell_visual_kit_model_attach_; }
  [[nodiscard]] const DbcStore<ItemEntry>&                   item() const { return gameplay_.item_; }
  [[nodiscard]] const DbcStore<ItemBagFamilyEntry>&          item_bag_family() const { return gameplay_.item_bag_family_; }
  [[nodiscard]] const DbcStore<ItemClassEntry>&              item_class() const { return gameplay_.item_class_; }
  [[nodiscard]] const DbcStore<ItemSubClassEntry>&           item_sub_class() const { return gameplay_.item_sub_class_; }
  [[nodiscard]] const DbcStore<ItemSubClassMaskEntry>&       item_sub_class_mask() const { return gameplay_.item_sub_class_mask_; }
  [[nodiscard]] const DbcStore<ItemCondExtCostsEntry>&       item_cond_ext_costs() const { return gameplay_.item_cond_ext_costs_; }
  [[nodiscard]] const DbcStore<ItemGroupSoundsEntry>&        item_group_sounds() const { return gameplay_.item_group_sounds_; }
  [[nodiscard]] const DbcStore<ItemLimitCategoryEntry>&      item_limit_category() const { return gameplay_.item_limit_category_; }
  [[nodiscard]] const DbcStore<ItemPetFoodEntry>&            item_pet_food() const { return gameplay_.item_pet_food_; }
  [[nodiscard]] const DbcStore<ItemPurchaseGroupEntry>&      item_purchase_group() const { return gameplay_.item_purchase_group_; }
  [[nodiscard]] const DbcStore<ItemVisualEffectsEntry>&      item_visual_effects() const { return gameplay_.item_visual_effects_; }
  [[nodiscard]] const DbcStore<ItemVisualsEntry>&            item_visuals() const { return gameplay_.item_visuals_; }
  [[nodiscard]] const DbcStore<SkillRaceClassInfoEntry>&     skill_race_class_info() const { return gameplay_.skill_race_class_info_; }
  [[nodiscard]] const DbcStore<SkillTiersEntry>&             skill_tiers() const { return gameplay_.skill_tiers_; }
  [[nodiscard]] const DbcStore<SkillCostsDataEntry>&         skill_costs_data() const { return gameplay_.skill_costs_data_; }
  [[nodiscard]] const DbcStore<SkillLineCategoryEntry>&      skill_line_category() const { return gameplay_.skill_line_category_; }
  [[nodiscard]] const DbcStore<QuestFactionRewardEntry>&     quest_faction_reward() const { return gameplay_.quest_faction_reward_; }
  [[nodiscard]] const DbcStore<QuestInfoEntry>&              quest_info() const { return gameplay_.quest_info_; }
  [[nodiscard]] const DbcStore<QuestXPEntry>&                quest_xp() const { return gameplay_.quest_xp_; }
  [[nodiscard]] const DbcStore<RandPropPointsEntry>&         rand_prop_points() const { return gameplay_.rand_prop_points_; }
  [[nodiscard]] const DbcStore<ScalingStatDistributionEntry>& scaling_stat_distribution() const { return gameplay_.scaling_stat_distribution_; }
  [[nodiscard]] const DbcStore<ScalingStatValuesEntry>&      scaling_stat_values() const { return gameplay_.scaling_stat_values_; }
  [[nodiscard]] const DbcStore<ResistancesEntry>&            resistances() const { return gameplay_.resistances_; }
  [[nodiscard]] const DbcStore<MapDifficultyEntry>&          map_difficulty() const { return gameplay_.map_difficulty_; }
  [[nodiscard]] const DbcStore<PvpDifficultyEntry>&          pvp_difficulty() const { return gameplay_.pvp_difficulty_; }
  [[nodiscard]] const DbcStore<AuctionHouseEntry>&           auction_house() const { return gameplay_.auction_house_; }
  [[nodiscard]] const DbcStore<BankBagSlotPricesEntry>&      bank_bag_slot_prices() const { return gameplay_.bank_bag_slot_prices_; }
  [[nodiscard]] const DbcStore<LfgDungeonsEntry>&            lfg_dungeons() const { return gameplay_.lfg_dungeons_; }
  [[nodiscard]] const DbcStore<LfgDungeonGroupEntry>&        lfg_dungeon_group() const { return gameplay_.lfg_dungeon_group_; }
  [[nodiscard]] const DbcStore<LfgDungeonExpansionEntry>&    lfg_dungeon_expansion() const { return gameplay_.lfg_dungeon_expansion_; }
  [[nodiscard]] const DbcStore<MailTemplateEntry>&           mail_template() const { return gameplay_.mail_template_; }
  [[nodiscard]] const DbcStore<PowerDisplayEntry>&           power_display() const { return gameplay_.power_display_; }
  [[nodiscard]] const DbcStore<TeamContributionPointsEntry>& team_contribution_points() const { return gameplay_.team_contribution_points_; }
  [[nodiscard]] const DbcStore<CreatureSpellDataEntry>&      creature_spell_data() const { return gameplay_.creature_spell_data_; }
  [[nodiscard]] const DbcStore<DungeonEncounterEntry>&       dungeon_encounter() const { return gameplay_.dungeon_encounter_; }
  [[nodiscard]] const DbcStore<AchievementCategoryEntry>&    achievement_category() const { return gameplay_.achievement_category_; }
  [[nodiscard]] const DbcStore<CurrencyCategoryEntry>&       currency_category() const { return gameplay_.currency_category_; }

  [[nodiscard]] const DbcStore<GtBarberShopCostBaseEntry>&          gt_barber_shop_cost_base() const { return gameplay_.gt_barber_shop_cost_base_; }
  [[nodiscard]] const DbcStore<GtChanceToMeleeCritEntry>&           gt_chance_to_melee_crit() const { return gameplay_.gt_chance_to_melee_crit_; }
  [[nodiscard]] const DbcStore<GtChanceToMeleeCritBaseEntry>&       gt_chance_to_melee_crit_base() const { return gameplay_.gt_chance_to_melee_crit_base_; }
  [[nodiscard]] const DbcStore<GtChanceToSpellCritEntry>&           gt_chance_to_spell_crit() const { return gameplay_.gt_chance_to_spell_crit_; }
  [[nodiscard]] const DbcStore<GtChanceToSpellCritBaseEntry>&       gt_chance_to_spell_crit_base() const { return gameplay_.gt_chance_to_spell_crit_base_; }
  [[nodiscard]] const DbcStore<GtOCTClassCombatRatingScalarEntry>&  gt_oct_class_combat_rating_scalar() const { return gameplay_.gt_oct_class_combat_rating_scalar_; }
  [[nodiscard]] const DbcStore<GtOCTRegenHPEntry>&                  gt_oct_regen_hp() const { return gameplay_.gt_oct_regen_hp_; }
  [[nodiscard]] const DbcStore<GtRegenHPPerSptEntry>&               gt_regen_hp_per_spt() const { return gameplay_.gt_regen_hp_per_spt_; }
  [[nodiscard]] const DbcStore<GtRegenMPPerSptEntry>&               gt_regen_mp_per_spt() const { return gameplay_.gt_regen_mp_per_spt_; }
  [[nodiscard]] const DbcStore<GtNPCManaCostScalerEntry>&           gt_npc_mana_cost_scaler() const { return gameplay_.gt_npc_mana_cost_scaler_; }
  [[nodiscard]] const DbcStore<GtOCTRegenMPEntry>&                 gt_oct_regen_mp() const { return gameplay_.gt_oct_regen_mp_; }

  [[nodiscard]] const DbcStore<BarberShopStyleEntry>&        barber_shop_style() const { return world_.barber_shop_style_; }
  [[nodiscard]] const DbcStore<WorldMapOverlayEntry>&        world_map_overlay() const { return world_.world_map_overlay_; }
  [[nodiscard]] const DbcStore<WorldSafeLocsEntry>&          world_safe_locs() const { return world_.world_safe_locs_; }
  [[nodiscard]] const DbcStore<LightSkyboxEntry>&            light_skybox() const { return world_.light_skybox_; }
  [[nodiscard]] const DbcStore<GroundEffectTextureEntry>&    ground_effect_texture() const { return world_.ground_effect_texture_; }
  [[nodiscard]] const DbcStore<GroundEffectDoodadEntry>&     ground_effect_doodad() const { return world_.ground_effect_doodad_; }
  [[nodiscard]] const DbcStore<WMOAreaTableEntry>&           wmo_area_table() const { return world_.wmo_area_table_; }
  [[nodiscard]] const DbcStore<AreaTriggerEntry>&            area_trigger() const { return world_.area_trigger_; }
  [[nodiscard]] const DbcStore<BattlemasterListEntry>&       battlemaster_list() const { return world_.battlemaster_list_; }
  [[nodiscard]] const DbcStore<DurabilityCostsEntry>&        durability_costs() const { return world_.durability_costs_; }
  [[nodiscard]] const DbcStore<DurabilityQualityEntry>&      durability_quality() const { return world_.durability_quality_; }
  [[nodiscard]] const DbcStore<LiquidTypeEntry>&             liquid_type() const { return world_.liquid_type_; }
  [[nodiscard]] const DbcStore<MovieEntry>&                  movie() const { return world_.movie_; }
  [[nodiscard]] const DbcStore<StableSlotPricesEntry>&       stable_slot_prices() const { return world_.stable_slot_prices_; }
  [[nodiscard]] const DbcStore<VehicleEntry>&                vehicle() const { return world_.vehicle_; }
  [[nodiscard]] const DbcStore<VehicleSeatEntry>&            vehicle_seat() const { return world_.vehicle_seat_; }
  [[nodiscard]] const DbcStore<ItemSetEntry>&                item_set() const { return world_.item_set_; }
  [[nodiscard]] const DbcStore<TransportAnimationEntry>&     transport_animation() const { return world_.transport_animation_; }
  [[nodiscard]] const DbcStore<CharSectionsEntry>&           char_sections() const { return world_.char_sections_; }
  [[nodiscard]] const DbcStore<CharHairGeosetsEntry>&        char_hair_geosets() const { return world_.char_hair_geosets_; }
  [[nodiscard]] const DbcStore<CharBaseInfoEntry>&           char_base_info() const { return world_.char_base_info_; }
  [[nodiscard]] const DbcStore<CharacterFacialHairStylesEntry>& character_facial_hair_styles() const { return world_.character_facial_hair_styles_; }
  [[nodiscard]] const DbcStore<CreatureDisplayInfoExtraEntry>& creature_display_info_extra() const { return world_.creature_display_info_extra_; }
  [[nodiscard]] const DbcStore<CreatureMovementInfoEntry>&   creature_movement_info() const { return world_.creature_movement_info_; }
  [[nodiscard]] const DbcStore<CreatureSoundDataEntry>&      creature_sound_data() const { return world_.creature_sound_data_; }
  [[nodiscard]] const DbcStore<SoundAmbienceEntry>&          sound_ambience() const { return world_.sound_ambience_; }
  [[nodiscard]] const DbcStore<ZoneMusicEntry>&              zone_music() const { return world_.zone_music_; }
  [[nodiscard]] const DbcStore<ZoneIntroMusicTableEntry>&    zone_intro_music_table() const { return world_.zone_intro_music_table_; }
  [[nodiscard]] const DbcStore<WeatherEntry>&                weather() const { return world_.weather_; }
  [[nodiscard]] const DbcStore<WorldMapContinentEntry>&      world_map_continent() const { return world_.world_map_continent_; }
  [[nodiscard]] const DbcStore<WorldMapTransformsEntry>&     world_map_transforms() const { return world_.world_map_transforms_; }
  [[nodiscard]] const DbcStore<WorldStateUIEntry>&           world_state_ui() const { return world_.world_state_ui_; }
  [[nodiscard]] const DbcStore<WorldStateZoneSoundsEntry>&   world_state_zone_sounds() const { return world_.world_state_zone_sounds_; }
  [[nodiscard]] const DbcStore<SoundEntriesAdvancedEntry>&   sound_entries_advanced() const { return world_.sound_entries_advanced_; }
  [[nodiscard]] const DbcStore<SoundFilterEntry>&            sound_filter() const { return world_.sound_filter_; }
  [[nodiscard]] const DbcStore<SoundFilterElemEntry>&        sound_filter_elem() const { return world_.sound_filter_elem_; }
  [[nodiscard]] const DbcStore<SoundProviderPreferencesEntry>& sound_provider_preferences() const { return world_.sound_provider_preferences_; }
  [[nodiscard]] const DbcStore<SoundWaterTypeEntry>&         sound_water_type() const { return world_.sound_water_type_; }
  [[nodiscard]] const DbcStore<SoundEmittersEntry>&          sound_emitters() const { return world_.sound_emitters_; }
  [[nodiscard]] const DbcStore<LiquidMaterialEntry>&         liquid_material() const { return world_.liquid_material_; }
  [[nodiscard]] const DbcStore<LoadingScreenTaxiSplinesEntry>& loading_screen_taxi_splines() const { return world_.loading_screen_taxi_splines_; }
  [[nodiscard]] const DbcStore<TerrainTypeEntry>&            terrain_type() const { return world_.terrain_type_; }
  [[nodiscard]] const DbcStore<TerrainTypeSoundsEntry>&      terrain_type_sounds() const { return world_.terrain_type_sounds_; }
  [[nodiscard]] const DbcStore<FootprintTexturesEntry>&      footprint_textures() const { return world_.footprint_textures_; }
  [[nodiscard]] const DbcStore<FootstepTerrainLookupEntry>&  footstep_terrain_lookup() const { return world_.footstep_terrain_lookup_; }
  [[nodiscard]] const DbcStore<CameraShakesEntry>&           camera_shakes() const { return world_.camera_shakes_; }
  [[nodiscard]] const DbcStore<ScreenEffectEntry>&           screen_effect() const { return world_.screen_effect_; }
  [[nodiscard]] const DbcStore<DestructibleModelDataEntry>&  destructible_model_data() const { return world_.destructible_model_data_; }
  [[nodiscard]] const DbcStore<HelmetGeosetVisDataEntry>&    helmet_geoset_vis_data() const { return world_.helmet_geoset_vis_data_; }
  [[nodiscard]] const DbcStore<UnitBloodEntry>&              unit_blood() const { return world_.unit_blood_; }
  [[nodiscard]] const DbcStore<UnitBloodLevelsEntry>&        unit_blood_levels() const { return world_.unit_blood_levels_; }
  [[nodiscard]] const DbcStore<VocalUISoundsEntry>&          vocal_ui_sounds() const { return world_.vocal_ui_sounds_; }
  [[nodiscard]] const DbcStore<UISoundLookupsEntry>&         ui_sound_lookups() const { return world_.ui_sound_lookups_; }
  [[nodiscard]] const DbcStore<WeaponImpactSoundsEntry>&     weapon_impact_sounds() const { return world_.weapon_impact_sounds_; }
  [[nodiscard]] const DbcStore<SheatheSoundLookupsEntry>&    sheathe_sound_lookups() const { return world_.sheathe_sound_lookups_; }
  [[nodiscard]] const DbcStore<NPCSoundsEntry>&              npc_sounds() const { return world_.npc_sounds_; }
  [[nodiscard]] const DbcStore<DeathThudLookupsEntry>&       death_thud_lookups() const { return world_.death_thud_lookups_; }
  [[nodiscard]] const DbcStore<ParticleColorEntry>&          particle_color() const { return world_.particle_color_; }
  [[nodiscard]] const DbcStore<PaperDollItemFrameEntry>&     paper_doll_item_frame() const { return world_.paper_doll_item_frame_; }
  [[nodiscard]] const DbcStore<PageTextMaterialEntry>&       page_text_material() const { return world_.page_text_material_; }
  [[nodiscard]] const DbcStore<MaterialEntry>&               material() const { return world_.material_; }
  [[nodiscard]] const DbcStore<ObjectEffectEntry>&           object_effect() const { return world_.object_effect_; }
  [[nodiscard]] const DbcStore<ObjectEffectGroupEntry>&      object_effect_group() const { return world_.object_effect_group_; }
  [[nodiscard]] const DbcStore<ObjectEffectModifierEntry>&   object_effect_modifier() const { return world_.object_effect_modifier_; }
  [[nodiscard]] const DbcStore<ObjectEffectPackageEntry>&    object_effect_package() const { return world_.object_effect_package_; }
  [[nodiscard]] const DbcStore<ObjectEffectPackageElemEntry>& object_effect_package_elem() const { return world_.object_effect_package_elem_; }
  [[nodiscard]] const DbcStore<TransportPhysicsEntry>&       transport_physics() const { return world_.transport_physics_; }
  [[nodiscard]] const DbcStore<TransportRotationEntry>&      transport_rotation() const { return world_.transport_rotation_; }
  [[nodiscard]] const DbcStore<VehicleUIIndicatorEntry>&     vehicle_ui_indicator() const { return world_.vehicle_ui_indicator_; }
  [[nodiscard]] const DbcStore<VehicleUIIndSeatEntry>&       vehicle_ui_ind_seat() const { return world_.vehicle_ui_ind_seat_; }
  [[nodiscard]] const DbcStore<MovieFileDataEntry>&          movie_file_data() const { return world_.movie_file_data_; }
  [[nodiscard]] const DbcStore<MovieVariationEntry>&         movie_variation() const { return world_.movie_variation_; }
  [[nodiscard]] const DbcStore<AttackAnimKitsEntry>&         attack_anim_kits() const { return world_.attack_anim_kits_; }
  [[nodiscard]] const DbcStore<AttackAnimTypesEntry>&        attack_anim_types() const { return world_.attack_anim_types_; }
  [[nodiscard]] const DbcStore<DungeonMapEntry>&             dungeon_map() const { return world_.dungeon_map_; }
  [[nodiscard]] const DbcStore<DungeonMapChunkEntry>&        dungeon_map_chunk() const { return world_.dungeon_map_chunk_; }
  [[nodiscard]] const DbcStore<WorldChunkSoundsEntry>&       world_chunk_sounds() const { return world_.world_chunk_sounds_; }
  [[nodiscard]] const DbcStore<GameObjectArtKitEntry>&       gameobject_art_kit() const { return world_.gameobject_art_kit_; }
  [[nodiscard]] const DbcStore<SoundSamplePreferencesEntry>& sound_sample_preferences() const { return world_.sound_sample_preferences_; }
  [[nodiscard]] const DbcStore<WeaponSwingSounds2Entry>&     weapon_swing_sounds2() const { return world_.weapon_swing_sounds2_; }

 private:
  template <typename T>
  bool LoadOne(DbcStore<T>& store,
               const openwow::vfs::VirtualFileSystem& vfs,
               const std::string& path,
               const RetailDbcDescriptor& descriptor);

  const openwow::vfs::VirtualFileSystem* vfs_ = nullptr;

  struct CoreStores {
    DbcStore<AreaTableEntry>             area_table_;
    DbcStore<CharStartOutfitEntry>       char_start_outfit_;
    DbcStore<ChrClassesEntry>            chr_classes_;
    DbcStore<ChrRacesEntry>              chr_races_;
    DbcStore<CinematicSequencesEntry>    cinematic_sequences_;
    DbcStore<CinematicCameraEntry>       cinematic_camera_;
    DbcStore<CreatureDisplayInfoEntry>   creature_display_info_;
    DbcStore<CreatureModelDataEntry>     creature_model_data_;
    DbcStore<GameObjectDisplayInfoEntry> gameobject_display_info_;
    DbcStore<ItemDisplayInfoEntry>       item_display_info_;
    DbcStore<LightEntry>                 light_;
    DbcStore<LightIntBandEntry>          light_int_band_;
    DbcStore<LightFloatBandEntry>        light_float_band_;
    DbcStore<LoadingScreensEntry>        loading_screens_;
    DbcStore<MapEntry>                   map_;
    DbcStore<SoundEntriesEntry>          sound_entries_;
    DbcStore<SpellEntry>                 spell_;
    DbcStore<SpellCastTimesEntry>        spell_cast_times_;
    DbcStore<SpellIconEntry>             spell_icon_;
    DbcStore<SpellRangeEntry>            spell_range_;
    DbcStore<SpellVisualEntry>           spell_visual_;
    DbcStore<SpellVisualKitEntry>        spell_visual_kit_;
    DbcStore<SpellVisualEffectNameEntry> spell_visual_effect_name_;
    DbcStore<AnimationDataEntry>         animation_data_;
    DbcStore<TalentEntry>                talent_;
    DbcStore<TalentTabEntry>             talent_tab_;
    DbcStore<TaxiNodesEntry>             taxi_nodes_;
    DbcStore<TaxiPathEntry>              taxi_path_;
    DbcStore<TaxiPathNodeEntry>          taxi_path_node_;
    DbcStore<SkillLineEntry>             skill_line_;
    DbcStore<SkillLineAbilityEntry>      skill_line_ability_;
    DbcStore<GtCombatRatingsEntry>       gt_combat_ratings_;
  };

  struct ExtendedStores {
    DbcStore<EmotesEntry>                emotes_;
    DbcStore<EmotesTextEntry>            emotes_text_;
    DbcStore<LightParamsEntry>           light_params_;
    DbcStore<AchievementEntry>           achievement_;
    DbcStore<AchievementCriteriaEntry>   achievement_criteria_;
    DbcStore<WorldMapAreaEntry>          world_map_area_;
    DbcStore<LanguagesEntry>             languages_;
    DbcStore<LanguageWordsEntry>         language_words_;
    DbcStore<NameGenEntry>               name_gen_;
    DbcStore<NamesProfanityEntry>        names_profanity_;
    DbcStore<NamesReservedEntry>         names_reserved_;
    DbcStore<CfgCategoriesEntry>         cfg_categories_;
    DbcStore<CfgConfigsEntry>            cfg_configs_;
    DbcStore<BannedAddOnsEntry>          banned_addons_;
    DbcStore<GameTablesEntry>            game_tables_;
    DbcStore<GameTipsEntry>              game_tips_;
    DbcStore<StationeryEntry>            stationery_;
    DbcStore<ServerMessagesEntry>        server_messages_;
    DbcStore<PackageEntry>               package_;
    DbcStore<SpamMessagesEntry>          spam_messages_;
    DbcStore<DanceMovesEntry>            dance_moves_;
    DbcStore<ExhaustionEntry>            exhaustion_;
    DbcStore<StartupStringsEntry>        startup_strings_;
    DbcStore<StringLookupsEntry>         string_lookups_;
    DbcStore<FileDataEntry>              file_data_;
    DbcStore<VideoHardwareEntry>         video_hardware_;
    DbcStore<HolidaysEntry>              holidays_;
    DbcStore<HolidayNamesEntry>          holiday_names_;
    DbcStore<HolidayDescriptionsEntry>   holiday_descriptions_;
    DbcStore<GMSurveyAnswersEntry>       gm_survey_answers_;
    DbcStore<GMSurveyCurrentSurveyEntry> gm_survey_current_survey_;
    DbcStore<GMSurveyQuestionsEntry>     gm_survey_questions_;
    DbcStore<GMSurveySurveysEntry>       gm_survey_surveys_;
    DbcStore<GMTicketCategoryEntry>      gm_ticket_category_;
    DbcStore<DeclinedWordEntry>          declined_word_;
    DbcStore<DeclinedWordCasesEntry>     declined_word_cases_;
    DbcStore<EmotesTextDataEntry>        emotes_text_data_;
    DbcStore<EmotesTextSoundEntry>       emotes_text_sound_;
    DbcStore<AreaGroupEntry>             area_group_;
    DbcStore<AreaPOIEntry>               area_poi_;
    DbcStore<FactionGroupEntry>          faction_group_;
    DbcStore<EnvironmentalDamageEntry>   environmental_damage_;
    DbcStore<LockTypeEntry>              lock_type_;
    DbcStore<PetPersonalityEntry>        pet_personality_;
  };

  struct GameplayStores {
    DbcStore<SpellDurationEntry>         spell_duration_;
    DbcStore<SpellRadiusEntry>           spell_radius_;
    DbcStore<SpellRuneCostEntry>         spell_rune_cost_;
    DbcStore<SpellShapeshiftFormEntry>   spell_shapeshift_form_;
    DbcStore<SpellItemEnchantmentEntry>  spell_item_enchantment_;
    DbcStore<FactionEntry>               faction_;
    DbcStore<FactionTemplateEntry>       faction_template_;
    DbcStore<ChatChannelsEntry>          chat_channels_;
    DbcStore<ChatProfanityEntry>         chat_profanity_;
    DbcStore<QuestSortEntry>             quest_sort_;
    DbcStore<CharTitlesEntry>            char_titles_;
    DbcStore<CurrencyTypesEntry>         currency_types_;
    DbcStore<TotemCategoryEntry>         totem_category_;
    DbcStore<GemPropertiesEntry>         gem_properties_;
    DbcStore<GlyphPropertiesEntry>       glyph_properties_;
    DbcStore<GlyphSlotEntry>             glyph_slot_;
    DbcStore<LockEntry>                  lock_;
    DbcStore<OverrideSpellDataEntry>     override_spell_data_;
    DbcStore<SummonPropertiesEntry>      summon_properties_;
    DbcStore<ItemRandomPropertiesEntry>  item_random_properties_;
    DbcStore<ItemRandomSuffixEntry>      item_random_suffix_;
    DbcStore<ItemExtendedCostEntry>      item_extended_cost_;
    DbcStore<CreatureFamilyEntry>        creature_family_;
    DbcStore<CreatureTypeEntry>          creature_type_;
    DbcStore<SpellCategoryEntry>         spell_category_;
    DbcStore<SpellDifficultyEntry>       spell_difficulty_;
    DbcStore<SpellDispelTypeEntry>       spell_dispel_type_;
    DbcStore<SpellMechanicEntry>         spell_mechanic_;
    DbcStore<SpellFocusObjectEntry>      spell_focus_object_;
    DbcStore<SpellItemEnchantmentConditionEntry> spell_item_enchantment_condition_;
    DbcStore<SpellDescriptionVariablesEntry> spell_description_variables_;
    DbcStore<SpellChainEffectsEntry>     spell_chain_effects_;
    DbcStore<SpellMissileEntry>          spell_missile_;
    DbcStore<SpellMissileMotionEntry>    spell_missile_motion_;
    DbcStore<SpellEffectCameraShakesEntry> spell_effect_camera_shakes_;
    DbcStore<SpellVisualKitAreaModelEntry> spell_visual_kit_area_model_;
    DbcStore<SpellVisualKitModelAttachEntry> spell_visual_kit_model_attach_;
    DbcStore<ItemEntry>                  item_;
    DbcStore<ItemBagFamilyEntry>         item_bag_family_;
    DbcStore<ItemClassEntry>             item_class_;
    DbcStore<ItemSubClassEntry>          item_sub_class_;
    DbcStore<ItemSubClassMaskEntry>      item_sub_class_mask_;
    DbcStore<ItemCondExtCostsEntry>      item_cond_ext_costs_;
    DbcStore<ItemGroupSoundsEntry>       item_group_sounds_;
    DbcStore<ItemLimitCategoryEntry>     item_limit_category_;
    DbcStore<ItemPetFoodEntry>           item_pet_food_;
    DbcStore<ItemPurchaseGroupEntry>     item_purchase_group_;
    DbcStore<ItemVisualEffectsEntry>     item_visual_effects_;
    DbcStore<ItemVisualsEntry>           item_visuals_;
    DbcStore<SkillRaceClassInfoEntry>    skill_race_class_info_;
    DbcStore<SkillTiersEntry>            skill_tiers_;
    DbcStore<SkillCostsDataEntry>        skill_costs_data_;
    DbcStore<SkillLineCategoryEntry>     skill_line_category_;
    DbcStore<QuestFactionRewardEntry>    quest_faction_reward_;
    DbcStore<QuestInfoEntry>             quest_info_;
    DbcStore<QuestXPEntry>               quest_xp_;
    DbcStore<RandPropPointsEntry>        rand_prop_points_;
    DbcStore<ScalingStatDistributionEntry> scaling_stat_distribution_;
    DbcStore<ScalingStatValuesEntry>     scaling_stat_values_;
    DbcStore<ResistancesEntry>           resistances_;
    DbcStore<MapDifficultyEntry>         map_difficulty_;
    DbcStore<PvpDifficultyEntry>         pvp_difficulty_;
    DbcStore<AuctionHouseEntry>          auction_house_;
    DbcStore<BankBagSlotPricesEntry>     bank_bag_slot_prices_;
    DbcStore<LfgDungeonsEntry>           lfg_dungeons_;
    DbcStore<LfgDungeonGroupEntry>       lfg_dungeon_group_;
    DbcStore<LfgDungeonExpansionEntry>   lfg_dungeon_expansion_;
    DbcStore<MailTemplateEntry>          mail_template_;
    DbcStore<PowerDisplayEntry>          power_display_;
    DbcStore<TeamContributionPointsEntry> team_contribution_points_;
    DbcStore<CreatureSpellDataEntry>     creature_spell_data_;
    DbcStore<DungeonEncounterEntry>      dungeon_encounter_;
    DbcStore<AchievementCategoryEntry>   achievement_category_;
    DbcStore<CurrencyCategoryEntry>      currency_category_;
    DbcStore<GtBarberShopCostBaseEntry>          gt_barber_shop_cost_base_;
    DbcStore<GtChanceToMeleeCritEntry>           gt_chance_to_melee_crit_;
    DbcStore<GtChanceToMeleeCritBaseEntry>       gt_chance_to_melee_crit_base_;
    DbcStore<GtChanceToSpellCritEntry>           gt_chance_to_spell_crit_;
    DbcStore<GtChanceToSpellCritBaseEntry>       gt_chance_to_spell_crit_base_;
    DbcStore<GtOCTClassCombatRatingScalarEntry>  gt_oct_class_combat_rating_scalar_;
    DbcStore<GtOCTRegenHPEntry>                  gt_oct_regen_hp_;
    DbcStore<GtRegenHPPerSptEntry>               gt_regen_hp_per_spt_;
    DbcStore<GtRegenMPPerSptEntry>               gt_regen_mp_per_spt_;
    DbcStore<GtNPCManaCostScalerEntry>           gt_npc_mana_cost_scaler_;
    DbcStore<GtOCTRegenMPEntry>                  gt_oct_regen_mp_;
  };

  struct WorldStores {
    DbcStore<BarberShopStyleEntry>       barber_shop_style_;
    DbcStore<WorldMapOverlayEntry>       world_map_overlay_;
    DbcStore<WorldSafeLocsEntry>         world_safe_locs_;
    DbcStore<LightSkyboxEntry>           light_skybox_;
    DbcStore<GroundEffectTextureEntry>   ground_effect_texture_;
    DbcStore<GroundEffectDoodadEntry>    ground_effect_doodad_;
    DbcStore<WMOAreaTableEntry>          wmo_area_table_;
    DbcStore<AreaTriggerEntry>           area_trigger_;
    DbcStore<BattlemasterListEntry>      battlemaster_list_;
    DbcStore<DurabilityCostsEntry>       durability_costs_;
    DbcStore<DurabilityQualityEntry>     durability_quality_;
    DbcStore<LiquidTypeEntry>            liquid_type_;
    DbcStore<MovieEntry>                 movie_;
    DbcStore<StableSlotPricesEntry>      stable_slot_prices_;
    DbcStore<VehicleEntry>               vehicle_;
    DbcStore<VehicleSeatEntry>           vehicle_seat_;
    DbcStore<ItemSetEntry>               item_set_;
    DbcStore<TransportAnimationEntry>    transport_animation_;
    DbcStore<CharSectionsEntry>          char_sections_;
    DbcStore<CharHairGeosetsEntry>       char_hair_geosets_;
    DbcStore<CharBaseInfoEntry>          char_base_info_;
    DbcStore<CharacterFacialHairStylesEntry> character_facial_hair_styles_;
    DbcStore<CreatureDisplayInfoExtraEntry> creature_display_info_extra_;
    DbcStore<CreatureMovementInfoEntry>  creature_movement_info_;
    DbcStore<CreatureSoundDataEntry>     creature_sound_data_;
    DbcStore<SoundAmbienceEntry>         sound_ambience_;
    DbcStore<ZoneMusicEntry>             zone_music_;
    DbcStore<ZoneIntroMusicTableEntry>   zone_intro_music_table_;
    DbcStore<WeatherEntry>               weather_;
    DbcStore<WorldMapContinentEntry>     world_map_continent_;
    DbcStore<WorldMapTransformsEntry>    world_map_transforms_;
    DbcStore<WorldStateUIEntry>          world_state_ui_;
    DbcStore<WorldStateZoneSoundsEntry>  world_state_zone_sounds_;
    DbcStore<SoundEntriesAdvancedEntry>  sound_entries_advanced_;
    DbcStore<SoundFilterEntry>           sound_filter_;
    DbcStore<SoundFilterElemEntry>       sound_filter_elem_;
    DbcStore<SoundProviderPreferencesEntry> sound_provider_preferences_;
    DbcStore<SoundWaterTypeEntry>        sound_water_type_;
    DbcStore<SoundEmittersEntry>         sound_emitters_;
    DbcStore<LiquidMaterialEntry>        liquid_material_;
    DbcStore<LoadingScreenTaxiSplinesEntry> loading_screen_taxi_splines_;
    DbcStore<TerrainTypeEntry>           terrain_type_;
    DbcStore<TerrainTypeSoundsEntry>     terrain_type_sounds_;
    DbcStore<FootprintTexturesEntry>     footprint_textures_;
    DbcStore<FootstepTerrainLookupEntry> footstep_terrain_lookup_;
    DbcStore<CameraShakesEntry>          camera_shakes_;
    DbcStore<ScreenEffectEntry>          screen_effect_;
    DbcStore<DestructibleModelDataEntry> destructible_model_data_;
    DbcStore<HelmetGeosetVisDataEntry>   helmet_geoset_vis_data_;
    DbcStore<UnitBloodEntry>             unit_blood_;
    DbcStore<UnitBloodLevelsEntry>       unit_blood_levels_;
    DbcStore<VocalUISoundsEntry>         vocal_ui_sounds_;
    DbcStore<UISoundLookupsEntry>        ui_sound_lookups_;
    DbcStore<WeaponImpactSoundsEntry>    weapon_impact_sounds_;
    DbcStore<SheatheSoundLookupsEntry>   sheathe_sound_lookups_;
    DbcStore<NPCSoundsEntry>             npc_sounds_;
    DbcStore<DeathThudLookupsEntry>      death_thud_lookups_;
    DbcStore<ParticleColorEntry>         particle_color_;
    DbcStore<PaperDollItemFrameEntry>    paper_doll_item_frame_;
    DbcStore<PageTextMaterialEntry>      page_text_material_;
    DbcStore<MaterialEntry>              material_;
    DbcStore<ObjectEffectEntry>          object_effect_;
    DbcStore<ObjectEffectGroupEntry>     object_effect_group_;
    DbcStore<ObjectEffectModifierEntry>  object_effect_modifier_;
    DbcStore<ObjectEffectPackageEntry>   object_effect_package_;
    DbcStore<ObjectEffectPackageElemEntry> object_effect_package_elem_;
    DbcStore<TransportPhysicsEntry>      transport_physics_;
    DbcStore<TransportRotationEntry>     transport_rotation_;
    DbcStore<VehicleUIIndicatorEntry>    vehicle_ui_indicator_;
    DbcStore<VehicleUIIndSeatEntry>      vehicle_ui_ind_seat_;
    DbcStore<MovieFileDataEntry>         movie_file_data_;
    DbcStore<MovieVariationEntry>        movie_variation_;
    DbcStore<AttackAnimKitsEntry>        attack_anim_kits_;
    DbcStore<AttackAnimTypesEntry>       attack_anim_types_;
    DbcStore<DungeonMapEntry>            dungeon_map_;
    DbcStore<DungeonMapChunkEntry>       dungeon_map_chunk_;
    DbcStore<WorldChunkSoundsEntry>      world_chunk_sounds_;
    DbcStore<GameObjectArtKitEntry>      gameobject_art_kit_;
    DbcStore<SoundSamplePreferencesEntry> sound_sample_preferences_;
    DbcStore<WeaponSwingSounds2Entry>     weapon_swing_sounds2_;
  };

  CoreStores core_;
  ExtendedStores extended_;
  GameplayStores gameplay_;
  WorldStores world_;
};

}

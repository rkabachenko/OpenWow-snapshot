
#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "dbc_schema_decode.h"

namespace openwow::data::dbc {

namespace {

std::array<std::string_view, kMaxLocales> LoadLocalizedStringFields(
    const DbcFile& f, const std::uint32_t row, const std::uint32_t first_field) {
  std::array<std::string_view, kMaxLocales> values{};
  for (std::size_t locale_index = 0; locale_index < values.size(); ++locale_index) {
    values[locale_index] = f.GetString(row, first_field + static_cast<std::uint32_t>(locale_index));
  }
  return values;
}

}

OPENWOW_DBC_SCHEMA(EmotesEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
  DBC_U32(anim_id, 2)
  DBC_U32(flags, 3)
  DBC_U32(spec, 4)
  DBC_U32(spec_param, 5)
  DBC_U32(event_sound_id, 6)
)

OPENWOW_DBC_SCHEMA(EmotesTextEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
  DBC_U32(emote_id, 2)
  DBC_U32_ARRAY(text_data_ids, 3)
)

OPENWOW_DBC_SCHEMA(LightParamsEntry,
  DBC_U32(id, 0)
  DBC_U32(highlight_sky, 1)
  DBC_U32(skybox_id, 2)
  DBC_U32(cloud_type_id, 3)
  DBC_F32(glow, 4)
  DBC_F32(water_shallow_alpha, 5)
  DBC_F32(water_deep_alpha, 6)
  DBC_F32(ocean_shallow_alpha, 7)
  DBC_F32(ocean_deep_alpha, 8)
)

OPENWOW_DBC_SCHEMA(AchievementEntry,
  DBC_U32(id, 0)
  DBC_I32(faction, 1)
  DBC_I32(map_id, 2)
  DBC_U32(parent_achievement, 3)
  DBC_LOCALIZED(name, 4)
  DBC_LOCALIZED(description, 21)
  DBC_U32(category, 38)
  DBC_U32(points, 39)
  DBC_U32(order_in_group, 40)
  DBC_U32(flags, 41)
  DBC_U32(icon, 42)
  DBC_LOCALIZED(reward_text, 43)
  DBC_U32(count, 60)
  DBC_U32(ref_achievement, 61)
)

OPENWOW_DBC_SCHEMA(AchievementCriteriaEntry,
  DBC_U32(id, 0)
  DBC_U32(achievement_id, 1)
  DBC_U32(type, 2)
  DBC_U32(asset, 3)
  DBC_U32(quantity, 4)
  DBC_U32(start_event, 5)
  DBC_U32(start_asset, 6)
  DBC_U32(fail_event, 7)
  DBC_U32(fail_asset, 8)
  DBC_LOCALIZED(description, 9)
  DBC_U32(flags, 26)
  DBC_U32(timer_start_event, 27)
  DBC_U32(timer_asset, 28)
  DBC_U32(timer_time, 29)
  DBC_U32(order, 30)
)

OPENWOW_DBC_SCHEMA(WorldMapAreaEntry,
  DBC_U32(id, 0)
  DBC_U32(map_id, 1)
  DBC_U32(area_id, 2)
  DBC_STRING(name, 3)
  DBC_F32(loc_left, 4)
  DBC_F32(loc_right, 5)
  DBC_F32(loc_top, 6)
  DBC_F32(loc_bottom, 7)
  DBC_I32(display_map_id, 8)
  DBC_I32(default_dungeon_map_id, 9)
  DBC_U32(parent_world_map_id, 10)
)

OPENWOW_DBC_SCHEMA(LanguagesEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
)

OPENWOW_DBC_SCHEMA(LanguageWordsEntry,
  DBC_U32(id, 0)
  DBC_U32(language_id, 1)
  DBC_STRING(word, 2)
)

OPENWOW_DBC_SCHEMA(NameGenEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
  DBC_U32(race_id, 2)
  DBC_U32(sex, 3)
)

OPENWOW_DBC_SCHEMA(NamesProfanityEntry,
  DBC_U32(id, 0)
  DBC_STRING(pattern, 1)
  DBC_U32(language, 2)
)

OPENWOW_DBC_SCHEMA(NamesReservedEntry,
  DBC_U32(id, 0)
  DBC_STRING(pattern, 1)
  DBC_U32(language, 2)
)

OPENWOW_DBC_SCHEMA(CfgCategoriesEntry,
  DBC_U32(id, 0)
  DBC_U32(locale_mask, 1)
  DBC_U32(create_charset_mask, 2)
  DBC_U32(flags, 3)
  DBC_LOCALIZED(name, 4)
)

OPENWOW_DBC_SCHEMA(CfgConfigsEntry,
  DBC_U32(id, 0)
  DBC_U32(realm_type, 1)
  DBC_U32(player_killing_allowed, 2)
  DBC_U32(roleplaying, 3)
)

OPENWOW_DBC_SCHEMA(BannedAddOnsEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(name_md5, 1)
  DBC_U32_ARRAY(version_md5, 5)
  DBC_U32(last_modified, 9)
  DBC_U32(flags, 10)
)

OPENWOW_DBC_SCHEMA(GameTablesEntry,
  DBC_ROW_ID()
  DBC_STRING(name, 0)
  DBC_U32(num_rows, 1)
  DBC_U32(num_columns, 2)
)

OPENWOW_DBC_SCHEMA(GameTipsEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(text, 1)
)

OPENWOW_DBC_SCHEMA(StationeryEntry,
  DBC_U32(id, 0)
  DBC_U32(item_id, 1)
  DBC_STRING(texture, 2)
  DBC_U32(flags, 3)
)

OPENWOW_DBC_SCHEMA(ServerMessagesEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(text, 1)
)

OPENWOW_DBC_SCHEMA(PackageEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
  DBC_U32(icon, 2)
  DBC_LOCALIZED(localized_name, 3)
)

OPENWOW_DBC_SCHEMA(SpamMessagesEntry,
  DBC_U32(id, 0)
  DBC_STRING(pattern, 1)
)

OPENWOW_DBC_SCHEMA(DanceMovesEntry,
  DBC_U32(id, 0)
  DBC_U32(type, 1)
  DBC_U32(action_parameter, 2)
  DBC_U32(fallback_step_id, 3)
  DBC_U32(required_class_mask, 4)
  DBC_STRING(name, 5)
  DBC_LOCALIZED(name_lang, 6)
  DBC_U32(required_learned_move_index, 23)
)

OPENWOW_DBC_SCHEMA(ExhaustionEntry,
  DBC_U32(id, 0)
  DBC_U32(xp, 1)
  DBC_F32(factor, 2)
  DBC_F32(outdoor_hours, 3)
  DBC_F32(inn_hours, 4)
  DBC_LOCALIZED(name, 5)
  DBC_F32(threshold, 22)
)

OPENWOW_DBC_SCHEMA(StartupStringsEntry,
  DBC_U32(id, 0)
  DBC_STRING(name, 1)
  DBC_LOCALIZED(text, 2)
)

OPENWOW_DBC_SCHEMA(StringLookupsEntry,
  DBC_U32(id, 0)
  DBC_STRING(value, 1)
)

OPENWOW_DBC_SCHEMA(FileDataEntry,
  DBC_U32(id, 0)
  DBC_STRING(filename, 1)
  DBC_STRING(filepath, 2)
)

OPENWOW_DBC_SCHEMA(VideoHardwareEntry,
  DBC_U32(id, 0)
  DBC_U32(vendor_id, 1)
  DBC_U32(device_id, 2)
  DBC_U32(farclip_idx, 3)
  DBC_U32(terrain_lod, 4)
  DBC_U32(terrain_shadow, 5)
  DBC_U32(detail_doodad, 6)
  DBC_U32(detail_doodad_density, 7)
  DBC_U32(anim, 8)
  DBC_U32(trilinear, 9)
  DBC_U32(max_lights, 10)
  DBC_U32(specular, 11)
  DBC_U32(water_lod_idx, 12)
  DBC_U32(particle_density_idx, 13)
  DBC_U32(unit_draw_dist_idx, 14)
  DBC_U32(small_cull_dist_idx, 15)
  DBC_U32(resolution_idx, 16)
  DBC_U32(base_mip, 17)
  DBC_STRING(oemdevice, 18)
  DBC_STRING(oemvendor, 19)
  DBC_U32(settings_20, 20)
  DBC_U32(settings_21, 21)
  DBC_U32(settings_22, 22)
)

OPENWOW_DBC_SCHEMA(HolidaysEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(sequence_duration_hours, 1)
  DBC_U32_ARRAY(occurrence_packed_times, 11)
  DBC_U32(selection_mask, 37)
  DBC_U32(loop_mode, 38)
  DBC_U32_ARRAY(sequence_team_masks, 39)
  DBC_U32(holiday_name_id, 49)
  DBC_U32(holiday_description_id, 50)
  DBC_STRING(texture_filename, 51)
  DBC_U32(priority, 52)
  DBC_U32(calendar_filter_type, 53)
  DBC_U32(flags, 54)
)

OPENWOW_DBC_SCHEMA(HolidayNamesEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
)

OPENWOW_DBC_SCHEMA(HolidayDescriptionsEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(description, 1)
)

OPENWOW_DBC_SCHEMA(GMSurveyAnswersEntry,
    e.id           = f.GetUInt32(row, 0);
    e.answer_index = f.GetUInt32(row, 1);
    e.question_id  = f.GetUInt32(row, 2);
    e.answer       = LoadLocalizedStringFields(f, row, 3);
)

OPENWOW_DBC_SCHEMA(GMSurveyCurrentSurveyEntry,
  DBC_U32(id, 0)
  DBC_U32(gm_survey_id, 1)
)

OPENWOW_DBC_SCHEMA(GMSurveyQuestionsEntry,
    e.id = f.GetUInt32(row, 0);
    e.question = LoadLocalizedStringFields(f, row, 1);
)

OPENWOW_DBC_SCHEMA(GMSurveySurveysEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(questions, 1)
)

OPENWOW_DBC_SCHEMA(GMTicketCategoryEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
)

OPENWOW_DBC_SCHEMA(DeclinedWordEntry,
  DBC_U32(id, 0)
  DBC_STRING(word, 1)
)

OPENWOW_DBC_SCHEMA(DeclinedWordCasesEntry,
  DBC_U32(id, 0)
  DBC_U32(declined_word_id, 1)
  DBC_U32(case_index, 2)
  DBC_STRING(declined_word, 3)
)

OPENWOW_DBC_SCHEMA(EmotesTextDataEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(text, 1)
)

OPENWOW_DBC_SCHEMA(EmotesTextSoundEntry,
  DBC_U32(id, 0)
  DBC_U32(emotes_text_id, 1)
  DBC_U32(race_id, 2)
  DBC_U32(sex_id, 3)
  DBC_U32(sound_id, 4)
)

OPENWOW_DBC_SCHEMA(AreaGroupEntry,
  DBC_U32(id, 0)
  DBC_U32_ARRAY(area_id, 1)
  DBC_U32(next_group, 7)
)

OPENWOW_DBC_SCHEMA(AreaPOIEntry,
    e.id         = f.GetUInt32(row, 0);
    e.importance = f.GetUInt32(row, 1);

    for (std::size_t index = 0; index < e.texture_indices.size(); ++index) {
      e.texture_indices[index] = f.GetUInt32(row, static_cast<std::uint32_t>(index + 2));
    }
    e.faction_id     = f.GetUInt32(row, 11);
    e.x              = f.GetFloat(row, 12);
    e.y              = f.GetFloat(row, 13);
    e.z              = f.GetFloat(row, 14);
    e.map_id         = f.GetUInt32(row, 15);
    e.flags          = f.GetUInt32(row, 16);
    e.area_id        = f.GetUInt32(row, 17);
    e.name           = f.GetLocalizedString(row, 18);
    e.description    = f.GetLocalizedString(row, 35);
    e.world_state_id = f.GetUInt32(row, 52);
    e.map_link_id    = f.GetUInt32(row, 53);
)

OPENWOW_DBC_SCHEMA(FactionGroupEntry,
  DBC_U32(id, 0)
  DBC_U32(mask_id, 1)
  DBC_STRING(internal_name, 2)
  DBC_LOCALIZED(name, 3)
)

OPENWOW_DBC_SCHEMA(EnvironmentalDamageEntry,
  DBC_U32(id, 0)
  DBC_U32(enum_id, 1)
  DBC_U32(visualization_kit_id, 2)
)

OPENWOW_DBC_SCHEMA(LockTypeEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
  DBC_LOCALIZED(resource, 18)
  DBC_LOCALIZED(verb, 35)
  DBC_STRING(cursor, 52)
)

OPENWOW_DBC_SCHEMA(PetPersonalityEntry,
  DBC_U32(id, 0)
  DBC_LOCALIZED(name, 1)
  DBC_U32_ARRAY(threshold, 18)
  DBC_F32_ARRAY(damage_modifier, 21)
)

}

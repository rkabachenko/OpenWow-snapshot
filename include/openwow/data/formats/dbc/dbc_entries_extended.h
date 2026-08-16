#pragma once

#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_file.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::data::dbc {

struct EmotesEntry {

  std::uint32_t id;
  std::string_view name;
  std::uint32_t anim_id;
  std::uint32_t flags;
  std::uint32_t spec;
  std::uint32_t spec_param;
  std::uint32_t event_sound_id;

  static EmotesEntry Load(const DbcFile& f, std::uint32_t row);
};

struct EmotesTextEntry {
  static constexpr std::size_t kTextDataSlotCount = 16;

  std::uint32_t id;
  std::string_view name;
  std::uint32_t emote_id;
  std::array<std::uint32_t, kTextDataSlotCount> text_data_ids{};

  [[nodiscard]] std::uint32_t TextDataId(std::size_t index) const {
    return index < text_data_ids.size() ? text_data_ids[index] : 0;
  }

  static EmotesTextEntry Load(const DbcFile& f, std::uint32_t row);
};

struct LightParamsEntry {

  std::uint32_t id;
  std::uint32_t highlight_sky;
  std::uint32_t skybox_id;
  std::uint32_t cloud_type_id;
  float glow;
  float water_shallow_alpha;
  float water_deep_alpha;
  float ocean_shallow_alpha;
  float ocean_deep_alpha;

  static LightParamsEntry Load(const DbcFile& f, std::uint32_t row);
};

struct AchievementEntry {

  std::uint32_t id;
  std::int32_t  faction;

  std::int32_t  map_id;
  std::uint32_t parent_achievement;
  std::string_view name;
  std::string_view description;
  std::uint32_t category;
  std::uint32_t points;
  std::uint32_t order_in_group;
  std::uint32_t flags;
  std::uint32_t icon;
  std::string_view reward_text;
  std::uint32_t count;
  std::uint32_t ref_achievement;

  static AchievementEntry Load(const DbcFile& f, std::uint32_t row);
};

struct AchievementCriteriaEntry {

  std::uint32_t id;
  std::uint32_t achievement_id;
  std::uint32_t type;
  std::uint32_t asset;
  std::uint32_t quantity;
  std::uint32_t start_event;
  std::uint32_t start_asset;
  std::uint32_t fail_event;
  std::uint32_t fail_asset;
  std::string_view description;
  std::uint32_t flags;
  std::uint32_t timer_start_event;
  std::uint32_t timer_asset;
  std::uint32_t timer_time;
  std::uint32_t order;

  static AchievementCriteriaEntry Load(const DbcFile& f, std::uint32_t row);
};

struct WorldMapAreaEntry {
  std::uint32_t id;
  std::uint32_t map_id;
  std::uint32_t area_id;
  std::string_view name;
  float loc_left;
  float loc_right;
  float loc_top;
  float loc_bottom;
  std::int32_t  display_map_id;
  std::int32_t  default_dungeon_map_id;
  std::uint32_t parent_world_map_id;

  static WorldMapAreaEntry Load(const DbcFile& f, std::uint32_t row);
};

struct LanguagesEntry {

  std::uint32_t id;
  std::string_view name;

  static LanguagesEntry Load(const DbcFile& f, std::uint32_t row);
};

struct LanguageWordsEntry {
  std::uint32_t id;
  std::uint32_t language_id;
  std::string_view word;

  static LanguageWordsEntry Load(const DbcFile& f, std::uint32_t row);
};

struct NameGenEntry {

  std::uint32_t id;
  std::string_view name;
  std::uint32_t race_id;
  std::uint32_t sex;

  static NameGenEntry Load(const DbcFile& f, std::uint32_t row);
};

struct NamesProfanityEntry {
  std::uint32_t id;
  std::string_view pattern;
  std::uint32_t language;

  static NamesProfanityEntry Load(const DbcFile& f, std::uint32_t row);
};

struct NamesReservedEntry {
  std::uint32_t id;
  std::string_view pattern;
  std::uint32_t language;

  static NamesReservedEntry Load(const DbcFile& f, std::uint32_t row);
};

struct CfgCategoriesEntry {

  std::uint32_t id;
  std::uint32_t locale_mask;
  std::uint32_t create_charset_mask;
  std::uint32_t flags;
  std::string_view name;

  static CfgCategoriesEntry Load(const DbcFile& f, std::uint32_t row);
};

struct CfgConfigsEntry {

  std::uint32_t id;
  std::uint32_t realm_type;
  std::uint32_t player_killing_allowed;
  std::uint32_t roleplaying;

  static CfgConfigsEntry Load(const DbcFile& f, std::uint32_t row);
};

struct BannedAddOnsEntry {

  std::uint32_t id;
  std::uint32_t name_md5[4];
  std::uint32_t version_md5[4];
  std::uint32_t last_modified;
  std::uint32_t flags;

  static BannedAddOnsEntry Load(const DbcFile& f, std::uint32_t row);
};

struct GameTablesEntry {
  std::uint32_t id;
  std::string_view name;
  std::uint32_t num_rows;
  std::uint32_t num_columns;

  static GameTablesEntry Load(const DbcFile& f, std::uint32_t row);
};

struct GameTipsEntry {

  std::uint32_t id;
  std::string_view text;

  static GameTipsEntry Load(const DbcFile& f, std::uint32_t row);
};

struct StationeryEntry {

  std::uint32_t id;
  std::uint32_t item_id;
  std::string_view texture;
  std::uint32_t flags;

  static StationeryEntry Load(const DbcFile& f, std::uint32_t row);
};

struct ServerMessagesEntry {

  std::uint32_t id;
  std::string_view text;

  static ServerMessagesEntry Load(const DbcFile& f, std::uint32_t row);
};

struct PackageEntry {
  std::uint32_t id;
  std::string_view name;
  std::uint32_t icon;
  std::string_view localized_name;

  static PackageEntry Load(const DbcFile& f, std::uint32_t row);
};

struct SpamMessagesEntry {
  std::uint32_t id;
  std::string_view pattern;

  static SpamMessagesEntry Load(const DbcFile& f, std::uint32_t row);
};

struct DanceMovesEntry {

  std::uint32_t id;
  std::uint32_t type;
  std::uint32_t action_parameter;
  std::uint32_t fallback_step_id;
  std::uint32_t required_class_mask;
  std::string_view name;
  std::string_view name_lang;
  std::uint32_t required_learned_move_index;

  static DanceMovesEntry Load(const DbcFile& f, std::uint32_t row);
};

struct ExhaustionEntry {

  std::uint32_t id;
  std::uint32_t xp;
  float factor;
  float outdoor_hours;
  float inn_hours;
  std::string_view name;
  float threshold;

  static ExhaustionEntry Load(const DbcFile& f, std::uint32_t row);
};

struct StartupStringsEntry {
  std::uint32_t id;
  std::string_view name;
  std::string_view text;

  static StartupStringsEntry Load(const DbcFile& f, std::uint32_t row);
};

struct StringLookupsEntry {
  std::uint32_t id;
  std::string_view value;

  static StringLookupsEntry Load(const DbcFile& f, std::uint32_t row);
};

struct FileDataEntry {

  std::uint32_t id;
  std::string_view filename;
  std::string_view filepath;

  static FileDataEntry Load(const DbcFile& f, std::uint32_t row);
};

struct VideoHardwareEntry {

  std::uint32_t id;
  std::uint32_t vendor_id;
  std::uint32_t device_id;
  std::uint32_t farclip_idx;
  std::uint32_t terrain_lod;
  std::uint32_t terrain_shadow;
  std::uint32_t detail_doodad;
  std::uint32_t detail_doodad_density;
  std::uint32_t anim;
  std::uint32_t trilinear;
  std::uint32_t max_lights;
  std::uint32_t specular;
  std::uint32_t water_lod_idx;
  std::uint32_t particle_density_idx;
  std::uint32_t unit_draw_dist_idx;
  std::uint32_t small_cull_dist_idx;
  std::uint32_t resolution_idx;
  std::uint32_t base_mip;
  std::string_view oemdevice;
  std::string_view oemvendor;
  std::uint32_t settings_20;
  std::uint32_t settings_21;
  std::uint32_t settings_22;

  static VideoHardwareEntry Load(const DbcFile& f, std::uint32_t row);
};

struct HolidaysEntry {

  std::uint32_t id;
  std::array<std::uint32_t, 10> sequence_duration_hours{};
  std::array<std::uint32_t, 26> occurrence_packed_times{};
  std::uint32_t selection_mask = 0;
  std::uint32_t loop_mode = 0;
  std::array<std::uint32_t, 10> sequence_team_masks{};
  std::uint32_t holiday_name_id = 0;
  std::uint32_t holiday_description_id = 0;
  std::string_view texture_filename;
  std::uint32_t priority = 0;
  std::uint32_t calendar_filter_type = 0;
  std::uint32_t flags = 0;

  static HolidaysEntry Load(const DbcFile& f, std::uint32_t row);
};

struct HolidayNamesEntry {

  std::uint32_t id;
  std::string_view name;

  static HolidayNamesEntry Load(const DbcFile& f, std::uint32_t row);
};

struct HolidayDescriptionsEntry {

  std::uint32_t id;
  std::string_view description;

  static HolidayDescriptionsEntry Load(const DbcFile& f, std::uint32_t row);
};

struct GMSurveyAnswersEntry {

  std::uint32_t id;
  std::uint32_t answer_index;
  std::uint32_t question_id;
  std::array<std::string_view, kMaxLocales> answer{};

  [[nodiscard]] std::string_view Answer(const std::uint8_t locale_index) const {
    return locale_index < answer.size() ? answer[locale_index] : std::string_view{};
  }

  static GMSurveyAnswersEntry Load(const DbcFile& f, std::uint32_t row);
};

struct GMSurveyCurrentSurveyEntry {
  std::uint32_t id;
  std::uint32_t gm_survey_id;

  static GMSurveyCurrentSurveyEntry Load(const DbcFile& f, std::uint32_t row);
};

struct GMSurveyQuestionsEntry {
  std::uint32_t id;
  std::array<std::string_view, kMaxLocales> question{};

  [[nodiscard]] std::string_view Question(const std::uint8_t locale_index) const {
    return locale_index < question.size() ? question[locale_index] : std::string_view{};
  }

  static GMSurveyQuestionsEntry Load(const DbcFile& f, std::uint32_t row);
};

struct GMSurveySurveysEntry {

  std::uint32_t id;
  std::uint32_t questions[10];

  static GMSurveySurveysEntry Load(const DbcFile& f, std::uint32_t row);
};

struct GMTicketCategoryEntry {

  std::uint32_t id;
  std::string_view name;

  static GMTicketCategoryEntry Load(const DbcFile& f, std::uint32_t row);
};

struct DeclinedWordEntry {
  std::uint32_t id;
  std::string_view word;

  static DeclinedWordEntry Load(const DbcFile& f, std::uint32_t row);
};

struct DeclinedWordCasesEntry {
  std::uint32_t id;
  std::uint32_t declined_word_id;
  std::uint32_t case_index;
  std::string_view declined_word;

  static DeclinedWordCasesEntry Load(const DbcFile& f, std::uint32_t row);
};

struct EmotesTextDataEntry {

  std::uint32_t id;
  std::string_view text;

  static EmotesTextDataEntry Load(const DbcFile& f, std::uint32_t row);
};

struct EmotesTextSoundEntry {
  std::uint32_t id;
  std::uint32_t emotes_text_id;
  std::uint32_t race_id;
  std::uint32_t sex_id;
  std::uint32_t sound_id;

  static EmotesTextSoundEntry Load(const DbcFile& f, std::uint32_t row);
};

struct AreaGroupEntry {
  std::uint32_t id;
  std::uint32_t area_id[6];
  std::uint32_t next_group;

  static AreaGroupEntry Load(const DbcFile& f, std::uint32_t row);
};

struct AreaPOIEntry {

  std::uint32_t id;
  std::uint32_t importance;
  std::array<std::uint32_t, 9> texture_indices{};
  std::uint32_t faction_id;
  float x;
  float y;
  float z;
  std::uint32_t map_id;
  std::uint32_t flags;
  std::uint32_t area_id;
  std::string_view name;
  std::string_view description;
  std::uint32_t world_state_id;
  std::uint32_t map_link_id = 0;

  static AreaPOIEntry Load(const DbcFile& f, std::uint32_t row);
};

struct FactionGroupEntry {
  std::uint32_t id;
  std::uint32_t mask_id;
  std::string_view internal_name;
  std::string_view name;

  static FactionGroupEntry Load(const DbcFile& f, std::uint32_t row);
};

struct EnvironmentalDamageEntry {
  std::uint32_t id;
  std::uint32_t enum_id;
  std::uint32_t visualization_kit_id;

  static EnvironmentalDamageEntry Load(const DbcFile& f, std::uint32_t row);
};

struct LockTypeEntry {

  std::uint32_t id;
  std::string_view name;
  std::string_view resource;
  std::string_view verb;
  std::string_view cursor;

  static LockTypeEntry Load(const DbcFile& f, std::uint32_t row);
};

struct PetPersonalityEntry {

  std::uint32_t id;
  std::string_view name;
  std::uint32_t threshold[3];
  float damage_modifier[3];

  static PetPersonalityEntry Load(const DbcFile& f, std::uint32_t row);
};

}

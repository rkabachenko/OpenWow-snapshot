
#pragma once
namespace openwow::audio { class SoundRuntime; }

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class WorldSession;

class SpellTextFormatter {
 public:
  using WorldStateValueResolver = std::function<std::int32_t(std::int32_t)>;

  static bool ExpandTextVariables(
      const void* spell_data,
      char* output,
      std::uint32_t output_size,
      std::int32_t effect_index,
      std::int32_t combo_points,
      std::int32_t stack_count,
      std::int32_t is_periodic,
      std::int32_t is_pet,
      const char** format_string_ptr,
      bool expand_inner);

  static bool ExpandTextVariablesWithNamedTagDefinitions(
      const void* spell_data,
      char* output,
      std::uint32_t output_size,
      std::int32_t effect_index,
      std::int32_t combo_points,
      std::int32_t stack_count,
      std::int32_t is_periodic,
      std::int32_t is_pet,
      const char* named_tag_definitions,
      const char** format_string_ptr,
      bool expand_inner);

  static bool ExpandObjectTextVariables(
      const char* format_text,
      char* output,
      std::uint32_t output_size,
      std::uint64_t guid,
      char* name_buf,
      std::int32_t name_size,
      const WorldStateValueResolver& resolve_world_state = {},
      std::int32_t current_time_seconds = 0,
      std::int32_t achievement_id = 0);

  static bool ExpandSimpleIntegerVariable(
      const void* data,
      char* output,
      std::uint32_t output_size,
      std::int32_t int_value);

  static std::uint8_t LookupTooltipVariableOpcode(std::string_view token);

  static double EvaluateTooltipExpression(
      void* parser_state,
      const void* spell_data,
      std::int32_t effect_index,
      std::int32_t combo_points,
      std::int32_t stack_count,
      std::int32_t is_periodic,
      std::int32_t is_pet);

  static void* EvaluateSpellTooltipVariable(
      void* parser_state,
      std::uint32_t var_id,
      void* eval_stack,
      const void* spell_data,
      std::int32_t effect_index,
      std::int32_t stack_count,
      std::int32_t combo_points,
      std::int32_t is_pet,
      std::int32_t is_periodic);

  static int LookupColorTag(
      const char* end_pos,
      char* output,
      std::uint32_t output_size,
      const char** pos_ptr,
      std::uint32_t* color_out);

  static int ApplyDrunkSpeechFilter(
      openwow::audio::SoundRuntime& sound_runtime,
      const char* input,
      char* output,
      std::uint32_t output_size,
      float drunk_level);
};

std::string ExpandSpellDescription(
    std::uint32_t spell_id,
    std::int32_t effect_index = 0,
    std::int32_t combo_points = 0);

[[nodiscard]] std::string ResolveSpellDescriptionForDisplay(
    std::uint32_t spell_id, std::string_view authored_description,
    std::int32_t effect_index = 0, std::int32_t combo_points = 0);

void BindSpellTextFormatterDbcLoader(
    const openwow::data::dbc::DbcLoader* dbc);

void BindSpellTextFormatterWorldSession(const WorldSession* session);

std::string ExpandQuestText(
    const std::string& format_text,
    std::uint64_t quest_giver_guid);

}

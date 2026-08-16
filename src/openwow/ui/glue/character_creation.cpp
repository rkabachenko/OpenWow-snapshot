#include "openwow/ui/glue/character_creation.h"

#include "openwow/core/storm_string.h"
#include "openwow/game/name_validation.h"
#include "openwow/net/client_services.h"
#include "openwow/ui/glue/cgluemgr.h"
#include "openwow/ui/glue/character_customization_randomizer.h"

#include <array>
#include <cstdint>

namespace openwow::ui::glue {
namespace {

struct RaceClassCombination {
  std::uint8_t race;
  std::uint8_t class_id;
};

constexpr auto kWotlkRaceClassCombinations = std::to_array<RaceClassCombination>({
    RaceClassCombination{1, 1},  {1, 2},  {1, 4},  {1, 5},  {1, 6},  {1, 8},  {1, 9},
    {2, 1},  {2, 3},  {2, 4},  {2, 6},  {2, 7},  {2, 9},
    {3, 1},  {3, 2},  {3, 3},  {3, 4},  {3, 5},  {3, 6},
    {4, 1},  {4, 3},  {4, 4},  {4, 5},  {4, 6},  {4, 11},
    {5, 1},  {5, 4},  {5, 5},  {5, 6},  {5, 8},  {5, 9},
    {6, 1},  {6, 3},  {6, 6},  {6, 7},  {6, 11},
    {7, 1},  {7, 4},  {7, 6},  {7, 8},  {7, 9},
    {8, 1},  {8, 3},  {8, 4},  {8, 5},  {8, 6},  {8, 7},  {8, 8},
    {10, 2}, {10, 3}, {10, 4}, {10, 5}, {10, 6}, {10, 8}, {10, 9},
    {11, 1}, {11, 2}, {11, 3}, {11, 5}, {11, 6}, {11, 7}, {11, 8},
});

}

bool IsWotlkRaceClassCombination(const int race_id, const int class_id) {
  for (const auto combination : kWotlkRaceClassCombinations) {
    if (race_id == combination.race && class_id == combination.class_id) {
      return true;
    }
  }
  return false;
}

std::optional<int> PickRandomAllowedClass(const std::span<const int> allowed_class_ids,
                                          detail::LegacyAdlerRandom &random) {
  if (allowed_class_ids.empty()) {
    return std::nullopt;
  }
  return allowed_class_ids[random.SelectOrdinal(allowed_class_ids.size())];
}

void SubmitCharacterCreation(GlueGameState &state, const char *name) {
  const bool has_customize_source_index = state.customize_source_character_index >= 0;
  const auto *source = GetCustomizationSourceCharacter(state);
  if (has_customize_source_index && source == nullptr) {
    return;
  }

  const char *effective_name = name;
  if (source != nullptr && effective_name == nullptr) {
    effective_name = source->name.c_str();
  }

  const bool unchanged_source_name =
      source != nullptr && name != nullptr &&
      openwow::core::SStrCmpNoCase(source->name.c_str(), name, 0x7FFFFFFFu) == 0;
  if (!unchanged_source_name) {
    const std::int32_t result = effective_name != nullptr
                                    ? openwow::game::ValidateGlueCharacterNameResultCode(
                                          effective_name)
                                    : 89;
    if (result != 87) {
      if (state.fire_event) {
        state.fire_event("OPEN_STATUS_DIALOG",
                         {MakeLuaString("OKAY"),
                          MakeLuaString(openwow::net::ClientServices::GetResultString(result))});
      }
      state.status_dialog_type = StatusDialogType::kOkay;
      return;
    }
  }

  const auto gender = static_cast<std::uint8_t>(state.create_sex);
  const auto skin = static_cast<std::uint8_t>(state.create_skin);
  const auto face = static_cast<std::uint8_t>(state.create_face);
  const auto hair_style = static_cast<std::uint8_t>(state.create_hair_style);
  const auto hair_color = static_cast<std::uint8_t>(state.create_hair_color);
  const auto facial_hair = static_cast<std::uint8_t>(state.create_facial_hair);

  if (source != nullptr) {
    if ((source->char_flags & 0x100000u) != 0) {
      CGlueMgr_SendRaceChange(state, source->id, effective_name, gender, skin, hair_style,
                              hair_color, facial_hair, face,
                              static_cast<std::uint8_t>(state.create_race));
    } else if ((source->char_flags & 0x10000u) != 0) {
      CGlueMgr_SendFactionChange(state, source->id, effective_name, gender, skin, hair_style,
                                 hair_color, facial_hair, face,
                                 static_cast<std::uint8_t>(state.create_race));
    } else {
      CGlueMgr_SendCharCustomize(state, source->id, effective_name, gender, skin, hair_style,
                                 hair_color, facial_hair, face);
    }
    return;
  }

  state.char_create_request.pending = true;
  state.char_create_request.name = effective_name;
  state.char_create_request.race = static_cast<std::uint8_t>(state.create_race);
  state.char_create_request.cls = static_cast<std::uint8_t>(state.create_class);
  state.char_create_request.gender = gender;
  state.char_create_request.skin = skin;
  state.char_create_request.face = face;
  state.char_create_request.hair_style = hair_style;
  state.char_create_request.hair_color = hair_color;
  state.char_create_request.facial_hair = facial_hair;
  CGlueMgr_SendCharCreate(state);
}

}

#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/game/api/game_lua_api_misc.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/runtime/scheduling/burst_throttle.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/barber_shop.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/comsat_client.h"
#include "openwow/game/currency_system.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/game/gm_survey.h"
#include "openwow/game/instance_handler.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/localization.h"
#include "openwow/game/minigame_system.h"
#include "openwow/game/name_declension.h"
#include "openwow/game/tracking_system.h"
#include "openwow/game/vehicle_system.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/name_declension_lua.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/update_fields.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/ui/game/camera_lua_bindings.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/autocomplete.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_misc_ui.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/lua_cpu_profiler.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/ui/game/script_addon_lua.h"
#include "openwow/ui/addons_data.h"
#include "openwow/ui/glue/character_customization_randomizer.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::game::detail {

namespace {

openwow::core::IdaBurstThrottle &GetSetCurrentTitleThrottle() {
  static openwow::core::IdaBurstThrottle throttle;
  return throttle;
}

bool CanSendSetCurrentTitle() {
  return GetSetCurrentTitleThrottle().TryConsume(
      openwow::core::GameClock::GetTickCountSeconds(), 10, 60.0);
}

}

using openwow::ui::glue::detail::CharacterCustomizationState;

static bool ResetBarberPreviewStylesFromActivePlayer(openwow::game::WorldSession *session) {
  if (!session || !openwow::game::BarberShop::Get().IsOpen()) {
    return false;
  }

  const auto *player = session->objects().GetActivePlayer();
  if (!player) {
    return false;
  }

  openwow::game::BarberShop::Get().ResetPreviewStylesFromActivePlayerAppearance({
      .hair_style = player->GetHairStyle(),
      .hair_color = player->GetHairColor(),
      .facial_hair = player->GetFacialHair(),
      .skin_color = player->GetSkinColor(),
      .face = player->GetFace(),
  });
  return true;
}

namespace {

constexpr int kBarberShopCustomizationSelector = 1;
constexpr int kGmTicketNoTextMessage = 365;
constexpr int kGmTicketTextTooLongMessage = 366;
constexpr int kGmTicketMaxTextCodepoints = 500;
constexpr std::string_view kGmTicketWhitespace = " \t\r\n";
constexpr std::string_view kBarberShopFallbackToken = "NORMAL";
constexpr int kDefaultCoinTextureFontHeight = 14;
constexpr std::array<std::string_view, 3> kCoinTextGlobalKeys{
    "GOLD_AMOUNT",
    "SILVER_AMOUNT",
    "COPPER_AMOUNT",
};
constexpr std::array<std::string_view, 3> kCoinTextureGlobalKeys{
    "GOLD_AMOUNT_TEXTURE",
    "SILVER_AMOUNT_TEXTURE",
    "COPPER_AMOUNT_TEXTURE",
};

struct CoinAmountParts {
  int gold = 0;
  int silver = 0;
  int copper = 0;
};

bool ValidateGmTicketTextOrReport(std::string_view text) {
  if (openwow::core::CountLegacyUtf8Codepoints(text) > kGmTicketMaxTextCodepoints) {
    DisplaySystemMessage(kGmTicketTextTooLongMessage);
    return false;
  }

  if (text.find_first_not_of(kGmTicketWhitespace) == std::string_view::npos) {
    DisplaySystemMessage(kGmTicketNoTextMessage);
    return false;
  }

  return true;
}

bool CanPerformGmTicketProtectedAction() {
  return GameUI_CanPerformProtectedAction(protected_action_kind::kGmTicket) != 0;
}

enum class GmTicketCreateRequestMode {
  NewTicket,
  FollowUp,
};

openwow::game::WorldSession *
ResolveGmTicketCreateRequestSession(lua_State *L, const GmTicketCreateRequestMode mode) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return nullptr;
  }

  const auto &gm_ticket = session->gm_ticket();
  if (mode == GmTicketCreateRequestMode::NewTicket) {
    if (gm_ticket.active_ticket_id() != 0) {
      return nullptr;
    }
  } else if (gm_ticket.active_response_id() == 0) {
    return nullptr;
  }

  if (session->objects().GetLocalPlayerTyped() == nullptr) {
    return nullptr;
  }

  return session;
}

enum class BarberShopStyleCategory : int {
  HairStyle = 0,
  HairColor = 1,
  FacialHair = 2,
  SkinColor = 3,
};

void PushStringView(lua_State *L, std::string_view value) {
  lua_pushlstring(L, value.data(), value.size());
}

int LuaNumberArgumentOrZero(lua_State *L, const int index) {
  if (lua_isnumber(L, index) == 0) {
    return 0;
  }

  return static_cast<int>(lua_tonumber(L, index));
}

std::string GetLuaOrLocalizedGlobalString(lua_State *L, std::string_view key) {
  const std::string key_string(key);
  lua_getglobal(L, key_string.c_str());

  std::string value;
  if (lua_isstring(L, -1) != 0) {
    value = lua_tostring(L, -1);
  }
  lua_pop(L, 1);

  if (!value.empty()) {
    return value;
  }
  return ::openwow::game::Localization::Get().GetString(key_string, key_string);
}

template <std::size_t N>
std::string RenderCoinGlobalString(lua_State *L, std::string_view key,
                                   const std::array<int, N> &args) {
  std::vector<std::string> format_args;
  format_args.reserve(N);
  for (const int arg : args) {
    format_args.emplace_back(std::to_string(arg));
  }

  return ::openwow::game::Localization::Get().FormatString(GetLuaOrLocalizedGlobalString(L, key),
                                                           format_args);
}

CoinAmountParts SplitCoinAmount(int amount) {
  CoinAmountParts parts;
  int remaining = amount;

  if (remaining >= 10000) {
    parts.gold = remaining / 10000;
    remaining %= 10000;
  }
  if (remaining >= 100) {
    parts.silver = remaining / 100;
    remaining %= 100;
  }

  parts.copper = remaining;
  return parts;
}

std::string BuildCoinTextString(lua_State *L, const int amount, std::string_view separator) {
  const CoinAmountParts parts = SplitCoinAmount(amount);
  const std::array<int, 3> values{parts.gold, parts.silver, parts.copper};

  std::string result;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (values[i] == 0) {
      continue;
    }

    if (!result.empty()) {
      result += separator;
    }
    result += RenderCoinGlobalString(L, kCoinTextGlobalKeys[i], std::array<int, 1>{values[i]});
  }

  return result;
}

std::string BuildCoinTextureString(lua_State *L, const int amount, const int font_height) {
  if (amount == 0) {
    return RenderCoinGlobalString(L, kCoinTextureGlobalKeys[2],
                                  std::array<int, 3>{0, font_height, font_height});
  }

  const CoinAmountParts parts = SplitCoinAmount(amount);
  const std::array<int, 3> values{parts.gold, parts.silver, parts.copper};

  std::string result;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (values[i] == 0) {
      continue;
    }

    if (!result.empty()) {
      result += ' ';
    }
    result += RenderCoinGlobalString(L, kCoinTextureGlobalKeys[i],
                                     std::array<int, 3>{values[i], font_height, font_height});
  }

  return result;
}

std::string ResolveDungeonNameWithDifficulty(lua_State *L,
                                             const openwow::game::WorldSession &session,
                                             const std::uint32_t map_id,
                                             const std::uint32_t difficulty) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    dbc = GetDbcLoader(L);
  }
  if (dbc == nullptr) {
    return {};
  }

  return ::openwow::game::FormatDungeonNameWithDifficulty(dbc, map_id, difficulty);
}

void DisplaySystemChatMessage(const openwow::game::ObjectManager& objects,
                              const std::string &message) {
  ::openwow::game::ChatFrame_DisplayMessage(objects, message.c_str(),
                                            ::openwow::game::ChatDisplayType::kSystem, nullptr, 0,
                                            nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
}

const openwow::game::CGPlayer_C *GetBarberShopActivePlayer(openwow::game::WorldSession *session) {
  if (session == nullptr) {
    return nullptr;
  }
  return session->objects().GetActivePlayer();
}

bool HasOpenBarberShopSession(const openwow::game::WorldSession *session) {
  return session != nullptr && openwow::game::BarberShop::Get().IsOpen();
}

std::uint8_t GetPlayerBarberShopValue(const openwow::game::CGPlayer_C &player,
                                      const BarberShopStyleCategory category) {
  switch (category) {
  case BarberShopStyleCategory::HairStyle:
    return player.GetHairStyle();
  case BarberShopStyleCategory::HairColor:
    return player.GetHairColor();
  case BarberShopStyleCategory::FacialHair:
    return player.GetFacialHair();
  case BarberShopStyleCategory::SkinColor:
    return player.GetSkinColor();
  }
  return 0;
}

std::uint8_t GetPreviewBarberShopValue(const openwow::game::BarberAppearance &appearance,
                                       const BarberShopStyleCategory category) {
  switch (category) {
  case BarberShopStyleCategory::HairStyle:
    return appearance.hair_style;
  case BarberShopStyleCategory::HairColor:
    return appearance.hair_color;
  case BarberShopStyleCategory::FacialHair:
    return appearance.facial_hair;
  case BarberShopStyleCategory::SkinColor:
    return appearance.skin_color;
  }
  return 0;
}

std::uint8_t GetSelectedBarberShopValue(const openwow::game::BarberShop &barber,
                                        const openwow::game::BarberAppearance &appearance,
                                        const BarberShopStyleCategory category) {
  switch (category) {
  case BarberShopStyleCategory::HairStyle:
    return barber.GetSelectedHairStyle();
  case BarberShopStyleCategory::HairColor:
    return appearance.hair_color;
  case BarberShopStyleCategory::FacialHair:
    return barber.GetSelectedFacialHair();
  case BarberShopStyleCategory::SkinColor:
    return barber.GetSelectedSkinColor();
  }
  return 0;
}

std::uint32_t GetBarberShopStyleDbcType(const BarberShopStyleCategory category) {
  switch (category) {
  case BarberShopStyleCategory::HairStyle:
    return 0;
  case BarberShopStyleCategory::FacialHair:
    return 2;
  case BarberShopStyleCategory::SkinColor:
    return 3;
  case BarberShopStyleCategory::HairColor:
    break;
  }
  return 0;
}

bool BarberShopStyleMatchesSex(const std::uint32_t entry_sex, const std::uint8_t player_gender) {
  return entry_sex == static_cast<std::uint32_t>(player_gender) || entry_sex == 0xFFFFFFFFu;
}

bool BarberShopStyleMatchesSelection(const openwow::data::dbc::BarberShopStyleEntry &entry,
                                     const openwow::game::CGPlayer_C &player,
                                     const BarberShopStyleCategory category,
                                     const std::uint8_t style_value) {
  return entry.type == GetBarberShopStyleDbcType(category) && entry.race == player.State().GetRace() &&
         BarberShopStyleMatchesSex(entry.sex, player.State().GetGender()) && entry.data == style_value;
}

const openwow::data::dbc::BarberShopStyleEntry *
FindBarberShopStyleEntry(const openwow::data::dbc::DbcLoader &dbc,
                         const openwow::game::CGPlayer_C &player,
                         const BarberShopStyleCategory category, const std::uint8_t style_value) {
  if (category == BarberShopStyleCategory::HairColor) {
    return nullptr;
  }

  const auto &entries = dbc.barber_shop_style().entries();

  for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
    if (BarberShopStyleMatchesSelection(*it, player, category, style_value)) {
      return &*it;
    }
  }
  return nullptr;
}

std::uint32_t RoundBarberShopCost(const float value) {
  if (value <= 0.0f) {
    return 0;
  }
  const auto rounded = std::nearbyint(static_cast<double>(value));
  if (rounded >= static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(rounded);
}

std::uint32_t GetBarberShopRoundedBaseCost(const openwow::data::dbc::DbcLoader &dbc,
                                           const openwow::game::CGPlayer_C &player) {
  const std::uint32_t level = player.State().GetLevel();
  if (level == 0) {
    return 0;
  }

  const auto *cost_entry = dbc.gt_barber_shop_cost_base().LookupEntry(level - 1);
  if (cost_entry == nullptr) {
    return 0;
  }
  return RoundBarberShopCost(cost_entry->value);
}

bool IsCurrentBarberShopStyleSelection(const openwow::game::CGPlayer_C &player,
                                       const openwow::game::BarberAppearance &appearance,
                                       const BarberShopStyleCategory category) {
  return GetPlayerBarberShopValue(player, category) ==
         GetPreviewBarberShopValue(appearance, category);
}

const openwow::data::dbc::BarberShopStyleEntry *GetSelectedBarberShopStyleEntry(
    const openwow::data::dbc::DbcLoader &dbc, const openwow::game::CGPlayer_C &player,
    const openwow::game::BarberAppearance &appearance, const BarberShopStyleCategory category) {
  return FindBarberShopStyleEntry(
      dbc, player, category,
      GetSelectedBarberShopValue(openwow::game::BarberShop::Get(), appearance, category));
}

std::uint32_t CalculateBarberShopTotalCost(const openwow::data::dbc::DbcLoader &dbc,
                                           const openwow::game::CGPlayer_C &player,
                                           const openwow::game::BarberAppearance &appearance) {
  const std::uint32_t base_cost = GetBarberShopRoundedBaseCost(dbc, player);
  if (base_cost == 0) {
    return 0;
  }

  std::uint32_t total_cost = 0;

  const auto *hair_style_entry =
      GetSelectedBarberShopStyleEntry(dbc, player, appearance, BarberShopStyleCategory::HairStyle);
  if (player.GetHairStyle() != appearance.hair_style) {
    if (hair_style_entry != nullptr) {
      total_cost += RoundBarberShopCost(base_cost * hair_style_entry->cost_modifier);
    }
  } else if (player.GetHairColor() != appearance.hair_color) {
    total_cost += RoundBarberShopCost(base_cost * 0.5f);
  }

  const auto *facial_hair_entry =
      GetSelectedBarberShopStyleEntry(dbc, player, appearance, BarberShopStyleCategory::FacialHair);
  if (facial_hair_entry != nullptr && player.GetFacialHair() != appearance.facial_hair) {
    total_cost += RoundBarberShopCost(base_cost * facial_hair_entry->cost_modifier);
  }

  const auto *skin_color_entry =
      GetSelectedBarberShopStyleEntry(dbc, player, appearance, BarberShopStyleCategory::SkinColor);
  if (skin_color_entry != nullptr && player.GetSkinColor() != appearance.skin_color) {
    total_cost += RoundBarberShopCost(base_cost * skin_color_entry->cost_modifier);
  }

  return total_cost;
}

bool HasSelectedBarberShopSkinStyle(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  const auto *player = GetBarberShopActivePlayer(session);
  if (dbc == nullptr || player == nullptr) {
    return false;
  }

  return GetSelectedBarberShopStyleEntry(*dbc, *player,
                                         openwow::game::BarberShop::Get().GetPreviewAppearance(),
                                         BarberShopStyleCategory::SkinColor) != nullptr;
}

const openwow::data::dbc::ChrRacesEntry *
LookupBarberShopRaceEntry(lua_State *L, const openwow::game::CGPlayer_C *player) {
  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr || player == nullptr) {
    return nullptr;
  }

  return dbc->chr_races().LookupEntry(player->State().GetRace());
}

std::string_view LookupBarberShopHairCustomizationToken(lua_State *L) {
  const auto *player = GetBarberShopActivePlayer(GetWorldSession(L));
  const auto *race_entry = LookupBarberShopRaceEntry(L, player);
  if (race_entry == nullptr) {
    return kBarberShopFallbackToken;
  }

  return race_entry->hair_customization;
}

std::string_view LookupBarberShopFacialHairCustomizationToken(lua_State *L) {
  const auto *player = GetBarberShopActivePlayer(GetWorldSession(L));
  const auto *race_entry = LookupBarberShopRaceEntry(L, player);
  if (race_entry == nullptr) {
    return kBarberShopFallbackToken;
  }

  switch (player->State().GetGender()) {
  case 0:
    return race_entry->facial_hair_male;
  case 1:
    return race_entry->facial_hair_female;
  case 2:

    return race_entry->hair_customization;
  default:
    return kBarberShopFallbackToken;
  }
}

CharacterCustomizationState
MakeBarberCustomizationState(const openwow::game::CGPlayer_C &player,
                             const openwow::game::BarberAppearance &appearance) {
  return CharacterCustomizationState{
      .race_id = player.State().GetRace(),
      .sex_id = player.State().GetGender(),
      .class_id = player.State().GetClass(),
      .skin = appearance.skin_color,
      .face = appearance.face,
      .hair_style = appearance.hair_style,
      .hair_color = appearance.hair_color,
      .facial_hair = appearance.facial_hair,
  };
}

void StoreBarberCustomizationState(openwow::game::BarberShop &barber,
                                   const CharacterCustomizationState &customization) {
  barber.SetPreviewAppearance({
      .hair_style = static_cast<std::uint8_t>(customization.hair_style),
      .hair_color = static_cast<std::uint8_t>(customization.hair_color),
      .facial_hair = static_cast<std::uint8_t>(customization.facial_hair),
      .skin_color = static_cast<std::uint8_t>(customization.skin),
      .face = static_cast<std::uint8_t>(customization.face),
  });
}

bool CycleBarberShopStyle(lua_State *state, const int category_index, const int delta) {
  auto *session = GetWorldSession(state);
  const auto *player = GetBarberShopActivePlayer(session);
  const auto *dbc = GetDbcLoader(state);
  if (player == nullptr || !HasOpenBarberShopSession(session) || dbc == nullptr ||
      dbc->char_sections().empty()) {
    return false;
  }

  auto &barber = openwow::game::BarberShop::Get();
  auto customization = MakeBarberCustomizationState(*player, barber.GetPreviewAppearance());

  bool changed = false;
  switch (static_cast<BarberShopStyleCategory>(category_index)) {
  case BarberShopStyleCategory::HairStyle:
    changed = openwow::ui::glue::detail::CycleHairStyleCustomizationSelection(
        customization, dbc->char_sections().entries(),
        dbc->character_facial_hair_styles().entries(), delta, kBarberShopCustomizationSelector);
    break;
  case BarberShopStyleCategory::HairColor:
    changed = openwow::ui::glue::detail::CycleHairColorCustomizationSelection(
        customization, dbc->char_sections().entries(), delta, kBarberShopCustomizationSelector);
    break;
  case BarberShopStyleCategory::FacialHair:
    changed = openwow::ui::glue::detail::CycleFacialHairCustomizationSelection(
        customization, dbc->char_sections().entries(),
        dbc->character_facial_hair_styles().entries(), delta, kBarberShopCustomizationSelector);
    break;
  case BarberShopStyleCategory::SkinColor:
    changed = openwow::ui::glue::detail::CycleSkinCustomizationSelection(
        customization, dbc->char_sections().entries(), delta, kBarberShopCustomizationSelector);
    break;
  default:
    return false;
  }

  StoreBarberCustomizationState(barber, customization);
  return changed;
}

}

int LuaGetNumTrackingTypes(lua_State *L) {
  auto &tracking = ::openwow::game::TrackingSystem::Get();
  const auto count = tracking.GetAvailableTracking().size() + tracking.GetLuaTrackingTypeCount();
  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaGetTrackingInfo(lua_State *L) {
  const auto index = static_cast<std::size_t>(luaL_checkinteger(L, 1));
  auto &tracking = ::openwow::game::TrackingSystem::Get();
  const auto spellTracking = tracking.GetAvailableTracking();
  if (index >= 1 && index <= spellTracking.size()) {
    const auto &entry = spellTracking[index - 1];
    lua_pushstring(L, entry.name.c_str());
    lua_pushstring(L, entry.iconPath.c_str());
    lua_pushwowbool(L, entry.isActive);
    lua_pushstring(L, "spell");
    return 4;
  }

  const auto info = tracking.GetLuaTrackingInfo(index - spellTracking.size());
  if (!info.has_value()) {
    return 0;
  }

  lua_pushstring(L, info->name.c_str());
  lua_pushstring(L, info->texturePath.c_str());
  lua_pushwowbool(L, info->active);
  lua_pushstring(L, "other");
  return 4;
}

int LuaSetTracking(lua_State *L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }
  auto &tracking = ::openwow::game::TrackingSystem::Get();
  if (lua_isnoneornil(L, 1) != 0) {
    tracking.ClearActive();
    tracking.ClearLuaTrackingSelection(session->objects());
    return 0;
  }

  const auto index = static_cast<std::size_t>(luaL_checkinteger(L, 1));
  const auto spellTracking = tracking.GetAvailableTracking();
  if (index >= 1 && index <= spellTracking.size()) {
    tracking.ClearLuaTrackingSelection(session->objects());
    static_cast<void>(tracking.SelectSpellTrackingSpell(
        spellTracking[index - 1].spellId));

    session->interaction().SendCastSpell(spellTracking[index - 1].spellId, 0,
                                         0);
    return 0;
  }

  tracking.ClearActive();
  static_cast<void>(tracking.SetLuaTrackingSelection(
      session->objects(), index - spellTracking.size()));
  return 0;
}

int LuaGetTrackingTexture(lua_State *L) {
  auto &tracking = openwow::game::TrackingSystem::Get();
  const std::string path = tracking.GetCurrentTrackingTexturePath();
  lua_pushstring(L, path.c_str());
  return 1;
}

int LuaApplyBarberShopStyle(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  const auto *player = GetBarberShopActivePlayer(session);
  if (session == nullptr || dbc == nullptr || player == nullptr ||
      !HasOpenBarberShopSession(session)) {
    return 0;
  }

  const auto appearance = openwow::game::BarberShop::Get().GetPreviewAppearance();
  const auto *hair_style_entry = GetSelectedBarberShopStyleEntry(
      *dbc, *player, appearance, BarberShopStyleCategory::HairStyle);
  const auto *facial_hair_entry = GetSelectedBarberShopStyleEntry(
      *dbc, *player, appearance, BarberShopStyleCategory::FacialHair);
  const auto *skin_color_entry = GetSelectedBarberShopStyleEntry(
      *dbc, *player, appearance, BarberShopStyleCategory::SkinColor);

  const bool has_changes =
      (hair_style_entry != nullptr && player->GetHairStyle() != hair_style_entry->data) ||
      player->GetHairColor() != appearance.hair_color ||
      (facial_hair_entry != nullptr && player->GetFacialHair() != facial_hair_entry->data) ||
      (skin_color_entry != nullptr && player->GetSkinColor() != skin_color_entry->data);
  if (!has_changes || hair_style_entry == nullptr || facial_hair_entry == nullptr) {
    return 0;
  }

  const std::uint32_t total_cost = CalculateBarberShopTotalCost(*dbc, *player, appearance);
  if (total_cost > player->GetMoney()) {
    DisplaySystemMessage(40);
    return 0;
  }

  session->interaction().SendAlterAppearance(
      hair_style_entry->id, appearance.hair_color, facial_hair_entry->id,
      skin_color_entry != nullptr ? skin_color_entry->id : 0u);
  return 0;
}

int LuaGetHairCustomization(lua_State *L) {
  PushStringView(L, LookupBarberShopHairCustomizationToken(L));
  return 1;
}

int LuaGetFacialHairCustomization(lua_State *L) {
  PushStringView(L, LookupBarberShopFacialHairCustomizationToken(L));
  return 1;
}

int LuaBarberShopReset(lua_State *L) {
  ResetBarberPreviewStylesFromActivePlayer(GetWorldSession(L));
  return 1;
}

int LuaCancelBarberShop(lua_State *L) {
  if (auto *session = GetWorldSession(L)) {
    session->inspect().CloseBarberShop();
    (void)openwow::game::BarberShop::Get().Cancel(*session);
  }
  return 0;
}

int LuaSetNextBarberShopStyle(lua_State *L) {
  if (lua_gettop(L) < 1 || lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: SetNextBarberShopStyle(type[, backward])");
  }

  const std::uint32_t category = openwow::ui::ClampLuaNumberToU32(lua_tonumber(L, 1));
  const bool backward = ScriptReadBoolArgOrDefault(L, 2, false);
  const std::uint32_t category_index = category - 1u;
  if (category_index < 4u &&
      (category_index != 3u || HasSelectedBarberShopSkinStyle(L))) {
    CycleBarberShopStyle(L, static_cast<int>(category_index), backward ? -1 : 1);
  }
  return 0;
}

int LuaCanAlterSkin(lua_State *L) {
  lua_pushboolean(L, HasSelectedBarberShopSkinStyle(L));
  return 1;
}

int LuaGetBarberShopInfo(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  const auto *player = GetBarberShopActivePlayer(session);
  if (!HasOpenBarberShopSession(session) || dbc == nullptr || player == nullptr) {
    return 0;
  }

  if (lua_gettop(L) < 1 || lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetBarberShopInfo(type)");
  }

  const std::uint32_t type_index =
      openwow::ui::ClampLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  if (type_index >= 4u) {
    return 0;
  }

  const auto category = static_cast<BarberShopStyleCategory>(type_index);
  const auto appearance = openwow::game::BarberShop::Get().GetPreviewAppearance();
  const auto *style_entry = GetSelectedBarberShopStyleEntry(*dbc, *player, appearance, category);

  if (category != BarberShopStyleCategory::HairColor && style_entry == nullptr) {
    return 0;
  }

  if (category == BarberShopStyleCategory::HairColor) {
    lua_pushnil(L);
    lua_pushnil(L);
  } else {
    PushStringView(L, style_entry->name);
    PushStringView(L, style_entry->description);
  }

  const std::uint32_t base_cost = GetBarberShopRoundedBaseCost(*dbc, *player);
  const float modifier =
      category == BarberShopStyleCategory::HairColor ? 0.5f : style_entry->cost_modifier;
  lua_pushinteger(L, static_cast<lua_Integer>(
                         RoundBarberShopCost(base_cost * modifier)));

  if (IsCurrentBarberShopStyleSelection(*player, appearance, category)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 4;
}

int LuaGetBarberShopTotalCost(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  const auto *player = GetBarberShopActivePlayer(session);
  if (!HasOpenBarberShopSession(session) || dbc == nullptr || player == nullptr) {
    FrameScript_PushNumberFromInt(L, 0);
    return 1;
  }

  const auto total_cost = CalculateBarberShopTotalCost(
      *dbc, *player, openwow::game::BarberShop::Get().GetPreviewAppearance());
  FrameScript_PushNumberFromInt(L, static_cast<int>(total_cost));
  return 1;
}

int LuaGetCurrentTitle(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session) {
    const auto *player = session->objects().GetLocalPlayer();
    if (player) {
      lua_pushnumber(L, static_cast<lua_Number>(player->GetUInt32(PLAYER_CHOSEN_TITLE)));
      return 1;
    }
  }
  lua_pushnumber(L, 0.0);
  return 1;
}

int LuaGetNumTitles(lua_State *L) {
  std::uint32_t title_count = 0;
  if (const auto *dbc = GetDbcLoader(L); dbc != nullptr) {
    title_count = dbc->char_titles().size();
  }

  lua_pushnumber(L, static_cast<lua_Number>(title_count + 1));
  return 1;
}

int LuaGetTitleName(lua_State *L) {
  if (!lua_isnumber(L, 1))
    return luaL_error(L, "Usage: GetTitleName(titleMaskID)");

  const auto title_mask_id = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *entry = FindCharTitleEntryByMaskId(L, title_mask_id);
  if (entry == nullptr) {
    return 0;
  }

  const auto selected_name =
      SelectTitleNameForActiveGender(*entry, ActivePlayerUsesFemaleTitles(L));
  const auto cleaned_name = StripLuaTitleFormatTokens(selected_name);
  lua_pushlstring(L, cleaned_name.data(), cleaned_name.size());
  return 1;
}

int LuaIsTitleKnown(lua_State *L) {
  if (!lua_isnumber(L, 1))
    return luaL_error(L, "Usage: IsTitleKnown(titleMaskID)");

  const auto title_mask_id = TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 1));
  auto *session = GetWorldSession(L);
  if (!session || title_mask_id == 0u) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  const auto *player = session->objects().GetActivePlayer();
  if (!player) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  lua_pushnumber(L, player->HasTitle(title_mask_id) ? 1.0 : 0.0);
  return 1;
}

int LuaSetCurrentTitle(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: SetCurrentTitle(titleMaskID)");
  }

  if (session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  if (!GameUI_CanPerformHardwareEventAction()) {
    return 0;
  }

  if (!CanSendSetCurrentTitle()) {
    DisplaySystemMessage(136);
    return 0;
  }

  const auto title_id = TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 1));
  session->interaction().SendSetTitle(title_id);
  return 0;
}

void ResetSetCurrentTitleThrottleForTests() {
  GetSetCurrentTitleThrottle().Reset();
}

int LuaGetCoinText(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetCoinText(amount [,separator])");
  }

  const int amount = static_cast<int>(lua_tonumber(L, 1));
  const char *separator = luaL_optstring(L, 2, ", ");
  const std::string result = BuildCoinTextString(L, amount, separator);

  lua_pushlstring(L, result.data(), result.size());
  return 1;
}

int LuaGetCoinTextureString(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetCoinText(amount, fontHeight)");
  }

  int font_height = kDefaultCoinTextureFontHeight;
  if (lua_isnumber(L, 2) != 0) {
    font_height = static_cast<int>(lua_tonumber(L, 2));
  }

  const int amount = static_cast<int>(lua_tonumber(L, 1));
  const std::string result = BuildCoinTextureString(L, amount, font_height);

  lua_pushlstring(L, result.data(), result.size());
  return 1;
}

int LuaExpandCurrencyList(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: ExpandCurrencyList(index,expand)");
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  if (const auto *dbc = GetDbcLoader(L)) {
    ::openwow::game::CurrencySystem::Get().CacheDbcData(*dbc);
  }

  auto &currency_system = ::openwow::game::CurrencySystem::Get();
  currency_system.RebuildCurrencyList(session->objects());
  currency_system.SetCategoryExpanded(static_cast<int>(lua_tonumber(L, 1)) - 1,
                                      TruncateLuaNumberToSseI32(lua_tonumber(L, 2)) != 0);
  currency_system.RebuildCurrencyList(session->objects());
  return 0;
}

int LuaSetCurrencyUnused(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: SetCurrencyUnused(index,unused)");
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  if (const auto *dbc = GetDbcLoader(L)) {
    ::openwow::game::CurrencySystem::Get().CacheDbcData(*dbc);
  }

  auto &currency_system = ::openwow::game::CurrencySystem::Get();
  currency_system.RebuildCurrencyList(session->objects());
  currency_system.SetCurrencyUnusedFlag(static_cast<int>(lua_tonumber(L, 1)) - 1,
                                        TruncateLuaNumberToSseI32(lua_tonumber(L, 2)) != 0);
  currency_system.RebuildCurrencyList(session->objects());
  return 0;
}

int LuaGMReportLag(lua_State *L) {
  if (lua_gettop(L) < 1 || !lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GMReportLag(number)");
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  const auto lag_type_argument = static_cast<std::int32_t>(lua_tonumber(L, 1));
  session->interaction().SendGMReportLag(lag_type_argument);
  return 0;
}

int LuaGMRequestPlayerInfo(lua_State *L) {
  return luaL_error(L, "Access Denied");
}

int LuaGMResponseResolve(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr || session->gm_ticket().active_response_id() == 0) {
    return 0;
  }

  session->interaction().SendGMResponseResolve();
  return 0;
}

int LuaDeleteGMTicket(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto &gm_ticket = session->gm_ticket();
  if (gm_ticket.active_ticket_id() == 0 || gm_ticket.active_response_id() != 0) {
    return 0;
  }

  session->interaction().SendGMTicketDelete();
  return 0;
}

int LuaGMSurveyAnswer(lua_State *L) {
  if (lua_isnumber(L, 1) == 0 && lua_isnumber(L, 2) == 0) {
    return luaL_error(L, "Usage: GMSurveyGetAnswer(questionIndex, answerIndex)");
  }

  const int question_index = LuaNumberArgumentOrZero(L, 1);
  const int answer_index = LuaNumberArgumentOrZero(L, 2);
  const auto *session = GetWorldSession(L);
  const auto *dbc = session != nullptr ? session->GetDbcLoader() : GetDbcLoader(L);
  if (dbc != nullptr && question_index >= 1 &&
      question_index <= static_cast<int>(openwow::game::kGMSurveyMaxQuestions)) {
    const auto answer_text = openwow::game::ResolveCurrentGMSurveyAnswerText(
        *dbc, Localization::Get().GetLocaleIndex(), question_index, answer_index);
    if (answer_text.has_value()) {
      PushStringView(L, *answer_text);
      return 1;
    }
  }

  lua_pushnil(L);
  return 1;
}

int LuaGMSurveyCommentSubmit(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GMSurveyCommentSubmit(comment)");
  }

  if (auto *session = GetWorldSession(L)) {
    session->gm_survey().SetOverallComment(SafeLuaString(L, 1));
  }
  return 0;
}

int LuaGMSurveyQuestion(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GMSurveyGetQuestion(index)");
  }

  const auto index = static_cast<int>(lua_tonumber(L, 1));
  const auto *session = GetWorldSession(L);
  const auto *dbc = session != nullptr ? session->GetDbcLoader() : GetDbcLoader(L);
  if (dbc != nullptr) {
    const auto question_text = openwow::game::ResolveCurrentGMSurveyQuestionText(
        *dbc, Localization::Get().GetLocaleIndex(), index);
    if (question_text.has_value()) {
      PushStringView(L, *question_text);
      return 1;
    }
  }

  lua_pushnil(L);
  return 1;
}

int LuaGMSurveySubmit(lua_State *L) {
  if (auto *session = GetWorldSession(L)) {
    session->interaction().SendGMSurveySubmit();
  }
  return 0;
}

int LuaGetGMStatus(lua_State *L) {
  if (auto *session = GetWorldSession(L)) {
    session->interaction().SendGMTicketSystemStatus();
  }
  return 0;
}

int LuaGetGMTicketCategories(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const auto *dbc = session != nullptr ? session->GetDbcLoader() : nullptr;
  if (dbc == nullptr) {
    dbc = GetDbcLoader(L);
  }
  if (dbc == nullptr) {
    return 0;
  }

  const auto &categories = dbc->gm_ticket_category();
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, static_cast<std::size_t>(categories.size()), 2u,
      "GM ticket category values");
  for (const auto &entry : categories) {
    FrameScript_PushNumber(L, static_cast<lua_Number>(entry.id));
    lua_pushstring(L, entry.name.empty() ? "" : entry.name.data());
  }

  return result_count;
}

int LuaGetGMTicket(lua_State *L) {
  if (auto *session = GetWorldSession(L)) {
    session->interaction().SendGMTicketGetTicket();
  }
  return 0;
}

int LuaUpdateGMTicket(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: UpdateGMTicket(ticketDescription)");
  }

  if (!CanPerformGmTicketProtectedAction()) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  if (session->gm_ticket().active_ticket_id() == 0 ||
      session->gm_ticket().active_response_id() != 0) {
    return 0;
  }

  const auto text = SafeLuaString(L, 1);
  if (!ValidateGmTicketTextOrReport(text)) {
    return 0;
  }

  session->interaction().SendGMTicketUpdateText(text);
  return 0;
}

int LuaNewGMTicket(lua_State *L) {
  if (!lua_isstring(L, 1) || lua_type(L, 2) != LUA_TBOOLEAN) {
    return luaL_error(L, "Usage: NewGMTicket(ticketDescription, needResponse)");
  }

  if (!CanPerformGmTicketProtectedAction()) {
    return 0;
  }

  auto *session = ResolveGmTicketCreateRequestSession(L, GmTicketCreateRequestMode::NewTicket);
  if (session == nullptr) {
    return 0;
  }

  const auto description = SafeLuaString(L, 1);
  if (!ValidateGmTicketTextOrReport(description)) {
    return 0;
  }

  session->interaction().SendGMTicketCreate(description,
                                            static_cast<std::uint8_t>(lua_toboolean(L, 2) != 0));
  return 0;
}

int LuaSaveView(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: SaveView(viewModeIndex)");
  }
  const int view_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  if (view_index < 1 || view_index > 5)
    return 0;
  if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    SaveCameraViewPreset(manager->world_camera(), view_index);
  }
  return 0;
}

int LuaSetView(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: SetView(viewModeIndex)");
  }
  const int view_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  if (view_index < 1 || view_index > 5)
    return 0;
  if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    SetCameraViewPreset(manager->world_camera(), view_index);
  }
  return 0;
}

int LuaPrevView(lua_State *L) {
  if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    StepCameraViewPreset(manager->world_camera(), -1);
  }
  return 0;
}

int LuaPlayDance(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: PlayDance(\"dance name\")");
  }

  const char *dance_name = lua_tostring(L, 1);
  if (auto *session = GetWorldSession(L);
      session != nullptr && dance_name != nullptr) {
    (void)session->dance_studio().SendPlayDance(dance_name);
  }
  return 0;
}

int LuaMakeMinigameMove(lua_State *L) {
  if (lua_isnumber(L, 1) == 0 || lua_isnumber(L, 2) == 0) {
    return luaL_error(L, "Usage: MakeMinigameMove(moveType, param)");
  }

  const auto move_type = static_cast<int>(lua_tonumber(L, 1) - 1.0);
  const auto param = static_cast<int>(lua_tonumber(L, 2)) - 1;
  (void)::openwow::game::MinigameSystem::Get().TrySendMove(move_type, param);
  return 0;
}

int LuaGMResponseNeedMoreHelp(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GMResponseNeedMoreHelp(newTicketDescription)");
  }

  if (!CanPerformGmTicketProtectedAction()) {
    return 0;
  }

  auto *session = ResolveGmTicketCreateRequestSession(L, GmTicketCreateRequestMode::FollowUp);
  if (session == nullptr) {
    return 0;
  }

  const auto description = SafeLuaString(L, 1);
  if (!ValidateGmTicketTextOrReport(description)) {
    return 0;
  }

  session->interaction().SendGMResponseNeedMoreHelp(description);
  return 0;
}

int LuaGMSurveyAnswerSubmit(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isstring(L, 3)) {
    return luaL_error(L, "Usage: GMSurveyAnswerSubmit(question, rank, comment)");
  }

  const auto question_index = static_cast<std::uint32_t>(static_cast<int>(lua_tonumber(L, 1)) - 1);
  if (question_index >= kGMSurveyMaxQuestions) {
    return luaL_error(L, "GMSurveyAnswerSubmit: Questions limited from %d to %d", 1,
                      static_cast<int>(kGMSurveyMaxQuestions));
  }

  if (auto *session = GetWorldSession(L)) {
    session->gm_survey().SetQuestionData(
        question_index, static_cast<std::uint8_t>(static_cast<int>(lua_tonumber(L, 2))),
        SafeLuaString(L, 3));
  }
  return 0;
}

int LuaGMSurveyNumAnswers(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GMSurveyGetNumAnswers(questionIndex)");
  }

  int answer_count = 0;
  const int question_index = LuaNumberArgumentOrZero(L, 1);
  if (question_index >= 1 &&
      question_index <= static_cast<int>(openwow::game::kGMSurveyMaxQuestions)) {
    const auto *session = GetWorldSession(L);
    const auto *dbc = session != nullptr ? session->GetDbcLoader() : GetDbcLoader(L);
    if (dbc != nullptr) {
      answer_count = openwow::game::CountCurrentGMSurveyAnswers(
          *dbc, Localization::Get().GetLocaleIndex(), question_index);
    }
  }

  lua_pushnumber(L, answer_count);
  return 1;
}

int LuaReportBug(lua_State *L) {
  if (lua_isstring(L, 1) == 0) {
    return luaL_error(L, "Usage: ReportBug(\"description\")");
  }

  if (auto *session = GetWorldSession(L); session != nullptr) {
    session->interaction().SendBugReport(lua_tostring(L, 1));
  }
  return 0;
}

int LuaReportSuggestion(lua_State *L) {
  if (lua_isstring(L, 1) == 0) {
    return luaL_error(L, "Usage: ReportSuggestion(\"description\")");
  }

  if (auto *session = GetWorldSession(L); session != nullptr) {
    session->interaction().SendSuggestionReport(lua_tostring(L, 1));
  }
  return 0;
}

int LuaClearTutorials(lua_State *L) {
  (void)L;
  openwow::game::TutorialSystem::Instance().ClearTutorials();
  return 0;
}

int LuaResetTutorials(lua_State *L) {
  (void)L;
  openwow::game::TutorialSystem::Instance().ResetTutorials();
  return 0;
}

int LuaTriggerTutorial(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: Trigger(\"tutorial\")");
  }

  const int tutorial_id = static_cast<int>(lua_tonumber(L, 1));
  if (tutorial_id >= 1 && tutorial_id <= openwow::game::TutorialSystem::kMaxFlaggedTutorials) {
    openwow::game::TutorialSystem::Instance().TriggerTutorial(
        static_cast<std::uint32_t>(tutorial_id - 1));
  }
  return 0;
}

int LuaFlagTutorial(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: FlagTutorial(\"tutorial\")");
  }

  const int tutorial_id = static_cast<int>(lua_tonumber(L, 1));
  if (tutorial_id >= 1 && tutorial_id <= openwow::game::TutorialSystem::kMaxFlaggedTutorials) {
    openwow::game::TutorialSystem::Instance().FlagTutorial(
        static_cast<std::uint32_t>(tutorial_id - 1));
  }
  return 0;
}

int LuaIsTutorialFlagged(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: Trigger(\"tutorial\")");
  }

  const int tutorial_id = static_cast<int>(lua_tonumber(L, 1));
  if (tutorial_id < 1 || tutorial_id > openwow::game::TutorialSystem::kMaxFlaggedTutorials) {
    return 0;
  }

  if (openwow::game::TutorialSystem::Instance().IsTutorialCompleted(
          static_cast<std::uint32_t>(tutorial_id - 1))) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

int LuaGetNextCompleatedTutorial(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetNextCompleatedTutorial(\"tutorial\")");
  }

  const int tutorial = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const int result = openwow::game::TutorialSystem::Instance()
                         .GetNextCompletedTutorial(tutorial);
  if (result == openwow::game::TutorialSystem::kMaxFlaggedTutorials) {
    return 0;
  }
  lua_pushnumber(L, result);
  return 1;
}

int LuaGetPrevCompleatedTutorial(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetPrevCompleatedTutorial(\"tutorial\")");
  }

  const int tutorial = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const int result = openwow::game::TutorialSystem::Instance()
                         .GetPrevCompletedTutorial(tutorial);
  if (result == 0) {
    return 0;
  }
  lua_pushnumber(L, result);
  return 1;
}

int LuaGetAutoCompletePresenceID(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GetAutoCompletePresenceID(\"name\")");
  }

  const char *name = lua_tostring(L, 1);
  const auto presence_id = AutoComplete::Get().GetRecentPresenceIdForName(name);
  if (!presence_id.has_value()) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(*presence_id));
  return 1;
}

int LuaGetBackpackCurrencyInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetBackpackCurrencyInfo(index)");
  }
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    for (int i = 0; i < 5; ++i) {
      lua_pushnil(L);
    }
    return 5;
  }
  const int index = static_cast<int>(lua_tonumber(L, 1)) - 1;
  if (const auto *dbc = GetDbcLoader(L)) {
    ::openwow::game::CurrencySystem::Get().CacheDbcData(*dbc);
  }

  auto &currency_system = ::openwow::game::CurrencySystem::Get();
  currency_system.RebuildCurrencyList(session->objects());

  const auto backpack = currency_system.GetBackpackCurrencies();
  if (index < 0 || index >= static_cast<int>(backpack.size())) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 5;
  }

  const auto item_id = backpack[static_cast<std::size_t>(index)];
  const auto *item = RequireItemDefinitions(L).GetItem(item_id);
  if (!item) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 5;
  }

  if (!item->name.empty()) {
    lua_pushstring(L, item->name.c_str());
  } else {
    lua_pushnil(L);
  }

  std::uint32_t count = 0;
  int extra_currency_type = 0;
  if (const auto *player = session->objects().GetLocalPlayer()) {
    if (item_id == 43307) {
      count = player->GetUInt32(PLAYER_FIELD_ARENA_CURRENCY);
      extra_currency_type = 1;
    } else if (item_id == 43308) {
      count = player->GetUInt32(PLAYER_FIELD_HONOR_CURRENCY);
      extra_currency_type = 2;
    } else {
      count = currency_system.GetAmount(item_id);
    }
  }
  lua_pushnumber(L, static_cast<lua_Number>(count));
  lua_pushinteger(L, extra_currency_type);

  if (const auto *dbc = GetDbcLoader(L)) {
    const auto icon_path =
        ::openwow::game::ResolveItemInventoryIconTexturePath(
            dbc, item->display_id);
    lua_pushstring(L, icon_path.c_str());
  } else {
    lua_pushnil(L);
  }
  lua_pushinteger(L, static_cast<lua_Integer>(item_id));
  return 5;
}

int LuaGetFrameCPUUsage(lua_State *L) {
  if (lua_type(L, 1) != LUA_TTABLE) {
    return luaL_error(L, "Usage: GetFrameCPUUsage(frame[, includeChildren])");
  }

  if (!HasLuaScriptObjectThis(L, 1)) {
    return luaL_error(L, "GetFrameCPUUsage(): Couldn't find 'this' in frame object");
  }

  if (!PushLuaScriptObjectByThis(L, 1)) {
    return luaL_error(L, "GetFrameCPUUsage(): Wrong object type, expected frame");
  }

  const int resolved_frame_index = lua_absindex(L, -1);
  const auto frame_type = GetAttachedLuaCanonicalScriptObjectType(L, resolved_frame_index);
  if (!IsFrameLikeLookupObjectType(frame_type)) {
    lua_pop(L, 1);
    return luaL_error(L, "GetFrameCPUUsage(): Wrong object type, expected frame");
  }

  const bool include_children = ScriptReadBoolArgOrDefault(L, 2, true);
  const auto usage = GetFrameCpuUsage(L, resolved_frame_index, include_children);
  lua_pop(L, 1);

  lua_pushnumber(L, usage.total_seconds * 1000.0);
  lua_pushnumber(L, static_cast<lua_Number>(usage.call_count));
  return 2;
}

int LuaGetEventCPUUsage(lua_State *L) {
  (void)lua_tostring(L, 1);

  const auto usage = GetEventCpuUsage(L);
  lua_pushnumber(L, usage.total_seconds * 1000.0);
  lua_pushnumber(L, static_cast<lua_Number>(usage.call_count));
  return 2;
}

int LuaGetFunctionCPUUsage(lua_State *L) {
  if (lua_type(L, 1) != LUA_TFUNCTION) {
    return luaL_error(L, "Usage: GetFunctionCPUUsage(function[, includeSubroutines])");
  }

  const bool include_subroutines = ScriptReadBoolArgOrDefault(L, 2, true);
  lua_settop(L, 1);

  const auto usage = GetFunctionCpuUsage(L, 1);
  const double cpu_usage_milliseconds =
      (include_subroutines ? usage.total_seconds : usage.self_seconds) * 1000.0;
  lua_pushnumber(L, cpu_usage_milliseconds);
  lua_pushnumber(L, static_cast<lua_Number>(usage.call_count));
  return 2;
}

int LuaGetNumDeclensionSets(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GetNumDeclensionSets(\"name\", gender)");
  }

  const char *name = lua_tostring(L, 1);
  const int gender_index = openwow::ui::ReadLuaDeclensionGenderIndex(L, 2);

  lua_pushnumber(L, static_cast<lua_Number>(openwow::game::declension::GetNumSets(
                        name != nullptr ? name : "", gender_index)));
  return 1;
}

void FirePetitionVendorUpdateOnResolvedItemTemplate(const bool success) {
  if (!success) {
    return;
  }

  ScriptEventDispatch::Get().FireEvent(events::PETITION_VENDOR_UPDATE);
}

openwow::game::AsyncQueryChannel::CallbackKey
BuildPetitionVendorItemUpdateCallbackKey(const std::uint32_t item_entry) {
  return openwow::game::AsyncQueryChannel::CallbackKey(
      reinterpret_cast<std::uintptr_t>(&FirePetitionVendorUpdateOnResolvedItemTemplate),
      item_entry);
}

int LuaGetPetitionItemInfo(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetPetitionItemInfo(index)");
  }

  auto *session = GetWorldSession(L);
  const int zero_based_index = static_cast<int>(lua_tonumber(L, 1)) - 1;
  const auto *offer =
      session != nullptr && zero_based_index >= 0
          ? session->petition().GetPetitionVendorOffer(
                static_cast<std::size_t>(zero_based_index))
          : nullptr;
  if (session == nullptr || offer == nullptr || offer->charter_entry == 0) {
    FrameScript_PushNil(L);
    FrameScript_PushNil(L);
    FrameScript_PushNumber(L, 0.0);
    return 3;
  }

  openwow::game::QueryCache::QueryRequestOptions request_options{};
  request_options.callback_key =
      BuildPetitionVendorItemUpdateCallbackKey(offer->charter_entry);
  request_options.callback = FirePetitionVendorUpdateOnResolvedItemTemplate;
  const auto *item_template = session->query_cache().GetOrRequestItemTemplate(
      offer->charter_entry, std::move(request_options));
  if (item_template == nullptr) {
    FrameScript_PushNil(L);
    FrameScript_PushNil(L);
    FrameScript_PushNumber(L, 0.0);
    return 3;
  }

  lua_pushstring(L, item_template->name.c_str());

  const std::string texture_path =
      ResolveItemDisplayIdIconTexturePathOrFallback(L, offer->charter_display_id);
  lua_pushstring(L, texture_path.c_str());
  FrameScript_PushNumber(L, static_cast<lua_Number>(offer->cost));
  return 3;
}

int LuaKeyRingButtonIDToInvSlotID(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: KeyRingButtonIDToInvSlotID(buttonID)");
  }

  constexpr std::uint32_t kKeyRingAbsoluteSlotBase = 0x56u;
  const auto button_id = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto absolute_slot = openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(button_id) + kKeyRingAbsoluteSlotBase);
  lua_pushnumber(L, static_cast<lua_Number>(absolute_slot));
  return 1;
}

int LuaSetCurrencyBackpack(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: SetCurrencyBackpack(index,backpack)");
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  if (const auto *dbc = GetDbcLoader(L)) {
    ::openwow::game::CurrencySystem::Get().CacheDbcData(*dbc);
  }

  auto &currency_system = ::openwow::game::CurrencySystem::Get();
  currency_system.RebuildCurrencyList(session->objects());
  currency_system.SetCurrencyBackpackFlag(static_cast<int>(lua_tonumber(L, 1)) - 1,
                                          TruncateLuaNumberToSseI32(lua_tonumber(L, 2)) != 0);
  currency_system.RebuildCurrencyList(session->objects());
  return 0;
}

int LuaSetSavedInstanceExtend(lua_State *L) {
  if (!lua_isnumber(L, 1) || lua_type(L, 2) != LUA_TBOOLEAN) {
    return luaL_error(L, "Usage: SetSavedInstanceExtend(index, extend)");
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto index = static_cast<std::size_t>(
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u);

  const bool extend = lua_toboolean(L, 2) != 0;
  const auto updated_lockout =
      session->instance().SetRaidLockoutExtended(index, extend);
  if (!updated_lockout.has_value()) {
    return 0;
  }

  const auto dungeon_name = ResolveDungeonNameWithDifficulty(L, *session, updated_lockout->map_id,
                                                             updated_lockout->difficulty);
  const auto message_key =
      extend ? "RAID_INSTANCE_LOCK_EXTENDED" : "RAID_INSTANCE_LOCK_NOT_EXTENDED";
  const std::string message_format =
      ::openwow::game::Localization::Get().GetString(message_key, message_key);
  DisplaySystemChatMessage(session->objects(),
      ::openwow::game::Localization::Get().FormatString(message_format, {dungeon_name}));

  session->interaction().SendSetSavedInstanceExtend(updated_lockout->map_id,
                                                    updated_lockout->difficulty, extend);
  return 0;
}

}

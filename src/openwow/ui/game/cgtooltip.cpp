#include "openwow/ui/game/tooltip_runtime.h"
#include "openwow/ui/game/tooltip_internal.h"

#include "openwow/core/storm_containers.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/client_config.h"
#include "openwow/game/container_slot_mapping.h"
#include "openwow/game/currency_system.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/inventory/loot/loot_interaction.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/commerce/mail/mail_compose_state.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/quest_dialog_text.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_query_bridge.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/script_event_helpers.h"
#include "openwow/game/shapeshift_form_resolver.h"
#include "openwow/game/spell_cooldown_state.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/talent_info.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/commerce/trade/trade_item_location.h"
#include "openwow/game/unit_level_display.h"
#include "openwow/game/world_session.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_craft.h"
#include "openwow/ui/game/api/game_lua_api_quest.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_tradeskill_state.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/ui/game/merchant_repair_cost.h"
#include "openwow/ui/game/quest_special_item.h"
#include "openwow/ui/game/quest_leaderboard_builder.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/game/trade_cursor_utils.h"
#include "openwow/ui/widgets/script_object.h"
#include "openwow/game/aura_lua_bridge.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
namespace openwow::ui::game {

namespace {

constexpr int ScriptObjectTypeTag(
    const openwow::ui::widgets::ScriptObjectType type) noexcept {
  return static_cast<int>(type);
}

bool MatchesTooltipTypeTag(const int type_id) noexcept {
  return type_id == ScriptObjectTypeTag(openwow::ui::widgets::ScriptObjectType::GameTooltip) ||
         type_id == ScriptObjectTypeTag(openwow::ui::widgets::ScriptObjectType::Frame) ||
         type_id == ScriptObjectTypeTag(openwow::ui::widgets::ScriptObjectType::Region) ||
         type_id == ScriptObjectTypeTag(openwow::ui::widgets::ScriptObjectType::Object);
}

}

void CGTooltip_InitFadeTimer([[maybe_unused]] void *tooltip) {
  TooltipSystem::Get().FadeOut();
}

void CGTooltip_OnUpdate([[maybe_unused]] void *tooltip, [[maybe_unused]] float elapsed) {
}

void CGTooltip_OnLogout() {
}

int CGTooltip_RegisterEventHandler() {
  return 0;
}

void *CGTooltip_Constructor([[maybe_unused]] void *mem, [[maybe_unused]] int parent) {
  return mem;
}

void CGTooltip_Destructor([[maybe_unused]] void *tooltip) {
  TooltipSystem::Get().Reset();
}

void CGTooltip_DestructorAndFree([[maybe_unused]] void *tooltip, [[maybe_unused]] uint8_t flags) {
  CGTooltip_Destructor(tooltip);
}

const char *CGTooltip_GetTypeName() {
  return "GameTooltip";
}

int CGTooltip_GetTypeId() {
  return ScriptObjectTypeTag(openwow::ui::widgets::ScriptObjectType::GameTooltip);
}

bool CGTooltip_IsTypeCompatible(const int typeId) {
  return MatchesTooltipTypeTag(typeId);
}

bool CGTooltip_IsTypeNameMatch(const char *name) {
  return openwow::text::EqualsIgnoreCaseAscii(name, "GameTooltip")
      || openwow::text::EqualsIgnoreCaseAscii(name, "Frame")
      || openwow::text::EqualsIgnoreCaseAscii(name, "Region")
      || openwow::text::EqualsIgnoreCaseAscii(name, "Object");
}

int CGTooltip_OnXmlLoad([[maybe_unused]] void *tooltip, [[maybe_unused]] unsigned int *arg2,
                        [[maybe_unused]] int arg3) {
  return 0;
}

void CGTooltip_ClearLines([[maybe_unused]] void *tooltip) {
  TooltipSystem::Get().ClearLines();
}

void CGTooltip_FinalizeTooltip([[maybe_unused]] void *tooltip) {
  auto &ts = TooltipSystem::Get();

  if (ts.GetLines().empty() && !ts.IsShown()) {
    return;
  }

  ts.Show();
}

void CGTooltip_Show([[maybe_unused]] void *tooltip) {
  TooltipSystem::Get().Show();
}

void CGTooltip_Hide([[maybe_unused]] void *tooltip) {
  TooltipSystem::Get().Hide();
}

void CGTooltip_SetOwnerInternal([[maybe_unused]] void *tooltip, [[maybe_unused]] int ownerFrame,
                                [[maybe_unused]] int anchorType, [[maybe_unused]] float xOff,
                                [[maybe_unused]] float yOff) {
  static const char *anchorNames[] = {
      "ANCHOR_LEFT",    "ANCHOR_RIGHT",       "ANCHOR_BOTTOMLEFT", "ANCHOR_BOTTOM",
      "ANCHOR_BOTTOMRIGHT", "ANCHOR_TOPLEFT", "ANCHOR_TOP",        "ANCHOR_TOPRIGHT",
      "ANCHOR_CURSOR",  "ANCHOR_NONE",        "ANCHOR_PRESERVE",   "ANCHOR_CURSOR_RIGHT"};

  const char *anchorStr = "ANCHOR_LEFT";
  if (anchorType >= 0 && anchorType <= 11)
    anchorStr = anchorNames[anchorType];

  TooltipSystem::Get().SetOwner("", anchorStr);
}

static void ExtractBgraColor(const void *color, float &r, float &g, float &b) {
  if (color) {
    const auto *bytes = static_cast<const std::uint8_t *>(color);
    r = bytes[2] / 255.0f;
    g = bytes[1] / 255.0f;
    b = bytes[0] / 255.0f;
  } else {
    r = g = b = 1.0f;
  }
}

void CGTooltip_AddTextLine([[maybe_unused]] void *tooltip, const char *leftText,
                           const char *rightText, const void *leftColor,
                           const void *rightColor, int wrapFlag) {
  float lr, lg, lb;
  ExtractBgraColor(leftColor, lr, lg, lb);

  auto &ts = TooltipSystem::Get();
  if (rightText && *rightText) {
    float rr, rg, rb;
    ExtractBgraColor(rightColor, rr, rg, rb);
    ts.AddDoubleLine(leftText ? leftText : "", rightText, lr, lg, lb, rr, rg, rb);
  } else if (leftText && *leftText) {
    ts.AddLine(leftText, lr, lg, lb, wrapFlag != 0);
  }
}

void CGTooltip_AddLineDefaultColor([[maybe_unused]] void *tooltip,
                                   const char *leftText,
                                   const char *rightText, int wrapFlag) {
  static constexpr std::uint8_t kDefaultTooltipColor[4] = {0x00, 0xD2, 0xFF, 0xFF};
  CGTooltip_AddTextLine(tooltip, leftText, rightText,
                        kDefaultTooltipColor, kDefaultTooltipColor, wrapFlag);
}

void CGTooltip_AppendToFirstLine([[maybe_unused]] void *tooltip,
                                 const char *text) {
  if (text == nullptr) {
    return;
  }

  TooltipSystem::Get().AppendToFirstLine(text);
}

}

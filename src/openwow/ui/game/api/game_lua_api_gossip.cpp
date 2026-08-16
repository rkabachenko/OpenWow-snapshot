
#include "openwow/ui/game/api/game_lua_api_gossip.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"

#include "openwow/game/chat_display.h"
#include "openwow/game/gossip_manager.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/diagnostics/logging.h"

extern "C" {
#include <lua.hpp>
}

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::ui::game::detail {

bool PrepareCurrentGossipText(openwow::game::WorldSession& session) {
  auto& gossip = session.gossip();
  if (!gossip.has_gossip()) {
    return false;
  }

  const auto& dialog = gossip.gossip();
  const auto* npc_text = session.query_cache().GetNpcText(dialog.title_text_id);
  const auto* active_player = session.objects().GetActivePlayer();
  const auto* npc = session.objects().GetUnit(dialog.npc_guid);
  if (npc_text == nullptr || active_player == nullptr || npc == nullptr) {
    return false;
  }

  const bool use_female_text = npc->State().GetGender() == 1u;
  float total_probability = 0.0f;
  for (const auto& block : npc_text->blocks) {
    const auto& text = use_female_text ? block.text_female : block.text_male;
    if (!text.empty()) {
      total_probability += block.probability;
    }
  }

  const openwow::game::NpcTextBlock* selected = nullptr;
  const float unit_roll =
      static_cast<float>(session.client_random().Next() + 1u) / 32768.0f;
  const float target = unit_roll * total_probability;
  float cumulative = 0.0f;
  for (const auto& block : npc_text->blocks) {
    const auto& text = use_female_text ? block.text_female : block.text_male;
    if (text.empty()) {
      continue;
    }
    cumulative += block.probability;
    if (target <= cumulative) {
      selected = &block;
      break;
    }
  }

  if (selected == nullptr) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Missing gossip text!");

    CloseGossipInteraction(session);
    return false;
  }

  const auto& raw_text = use_female_text ? selected->text_female : selected->text_male;
  constexpr std::size_t kPreparedGossipTextBytes = 3000u;
  std::array<char, kPreparedGossipTextBytes> expanded{};
  BindSpellTextFormatterDbcLoader(session.GetDbcLoader());
  BindSpellTextFormatterWorldSession(&session);
  SpellTextFormatter::ExpandObjectTextVariables(
      raw_text.c_str(), expanded.data(), static_cast<std::uint32_t>(expanded.size()),
      active_player->GetGuid().GetRawValue(), nullptr, 0);

  std::uint32_t comprehension_value = 0;
  if (const auto* dbc = session.GetDbcLoader(); dbc != nullptr) {
    BindChatDisplayDbcLoader(dbc);
    comprehension_value =
        GetChatLanguageComprehensionValue(*active_player, *dbc, selected->language);
  }

  gossip.SetDisplayText(ChatFrame_FormatMessage(
      session.objects(), selected->language, comprehension_value,
      expanded[0] != '\0' ? std::string_view(expanded.data()) : std::string_view(raw_text),
      {.output_limit = kPreparedGossipTextBytes, .preserve_separators = true}));

  std::array<std::uint32_t, 6> emote_pairs{};
  std::int32_t emote_count = 0;
  for (std::size_t index = 0; index < selected->emotes.size(); index += 2u) {
    if (selected->emotes[index + 1u] == 0u) {
      continue;
    }
    emote_pairs[static_cast<std::size_t>(emote_count) * 2u] = selected->emotes[index];
    emote_pairs[static_cast<std::size_t>(emote_count) * 2u + 1u] =
        selected->emotes[index + 1u];
    ++emote_count;
  }
  if (emote_count != 0) {
    if (auto* mutable_npc = session.objects().GetMutableUnit(dialog.npc_guid);
        mutable_npc != nullptr) {
      mutable_npc->Animation().EmoteQueueHandler(emote_pairs.data(), emote_count);
    }
  }

  return true;
}

int LuaCheckBinderDist(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *player = session ? session->objects().GetLocalPlayerTyped() : nullptr;
  lua_pushwowbool(L, player != nullptr && session != nullptr &&
                          IsWithinNpcInteractionDistance(
                              *player, session->objects(),
                              session->misc().binder_confirm_guid()));
  return 1;
}

int LuaGetGossipText(lua_State *L) {
  auto *session = GetWorldSession(L);
  lua_pushstring(L, session != nullptr ? session->gossip().display_text().c_str() : "");
  return 1;
}

int LuaGetNumGossipAvailableQuests(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !session->gossip().has_gossip()) {
    lua_pushnumber(L, 0);
    return 1;
  }
  const auto &dialog = session->gossip().gossip();

  int count = 0;
  for (const auto &q : dialog.quests) {
    if (q.quest_icon != 3 && q.quest_icon != 4)
      ++count;
  }
  lua_pushnumber(L, count);
  return 1;
}

int LuaGetNumGossipActiveQuests(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !session->gossip().has_gossip()) {
    lua_pushnumber(L, 0);
    return 1;
  }
  const auto &dialog = session->gossip().gossip();
  int count = 0;
  for (const auto &q : dialog.quests) {
    if (q.quest_icon == 3 || q.quest_icon == 4)
      ++count;
  }
  lua_pushnumber(L, count);
  return 1;
}

int LuaGetNumGossipOptions(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !session->gossip().has_gossip()) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Integer>(session->gossip().gossip().items.size()));
  return 1;
}

static const char *GossipIconToType(std::uint8_t icon) {

  switch (icon) {
  case 0:
    return "gossip";
  case 1:
    return "vendor";
  case 2:
    return "taxi";
  case 3:
    return "trainer";
  case 4:
    return "healer";
  case 5:
    return "binder";
  case 6:
    return "banker";
  case 7:
    return "petition";
  case 8:
    return "tabard";
  case 9:
    return "battlemaster";
  case 10:
    return "auctioneer";
  default:
    return "gossip";
  }
}

int LuaGetGossipOptions(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !session->gossip().has_gossip()) {
    return 0;
  }
  const auto &items = session->gossip().gossip().items;
  int n = 0;
  for (const auto &item : items) {
    lua_pushstring(L, item.message.c_str());
    lua_pushstring(L, GossipIconToType(item.icon));
    n += 2;
  }
  return n;
}

int LuaGetGossipAvailableQuests(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !session->gossip().has_gossip()) {
    return 0;
  }

  const auto &quests = session->gossip().gossip().quests;
  int n = 0;
  for (const auto &q : quests) {
    if (q.quest_icon == 3 || q.quest_icon == 4)
      continue;
    lua_pushstring(L, q.title.c_str());
    lua_pushnumber(L, q.quest_level);

    lua_pushwowbool(L, IsQuestLevelTrivial(*session, q.quest_level));

    lua_pushwowbool(L, (q.quest_flags & 0x1000) != 0);

    lua_pushwowbool(L, q.is_repeatable);
    n += 5;
  }
  return n;
}

int LuaGetGossipActiveQuests(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !session->gossip().has_gossip()) {
    return 0;
  }

  const auto &quests = session->gossip().gossip().quests;
  int n = 0;
  for (const auto &q : quests) {
    if (q.quest_icon != 3 && q.quest_icon != 4)
      continue;
    lua_pushstring(L, q.title.c_str());
    lua_pushnumber(L, q.quest_level);

    lua_pushwowbool(L, IsQuestLevelTrivial(*session, q.quest_level));
    lua_pushwowbool(L, IsQuestTurnInReady(*session, q.quest_id));
    n += 4;
  }
  return n;
}

int LuaSelectGossipOption(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SelectGossipOption(index)");
  }

  auto *session = GetWorldSession(L);
  if (!session || !session->gossip().has_gossip())
    return 0;

  const auto &dialog = session->gossip().gossip();

  const auto one_based_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  if (one_based_index <= 0)
    return 0;

  const auto option_index = static_cast<std::size_t>(one_based_index - 1);
  if (option_index >= dialog.items.size())
    return 0;

  const auto &item = dialog.items[option_index];
  const bool skip_confirmation = ScriptReadBoolArgOrDefault(L, 3, false);
  const char *code_text = lua_tostring(L, 2);
  const bool has_code_text = code_text != nullptr && code_text[0] != '\0';

  if (!item.box_message.empty() && !skip_confirmation) {
    ScriptEventDispatch::Get().FireEventArgs(events::GOSSIP_CONFIRM,
                                             {one_based_index, item.box_message,
                                              static_cast<int>(item.box_money)});
    return 0;
  }

  if (item.is_coded && !has_code_text) {
    ScriptEventDispatch::Get().FireEventArgs(events::GOSSIP_ENTER_CODE, {one_based_index});
    return 0;
  }

  if (const auto *active_player = session->objects().GetActivePlayer();
      active_player != nullptr && item.box_money > active_player->GetMoney()) {
    DisplaySystemMessage(40);
    return 0;
  }

  session->interaction().SendGossipSelectOption(
      dialog.npc_guid.GetRawValue(), dialog.menu_id, item.menu_item_id,
      item.is_coded && code_text != nullptr ? std::string(code_text) : std::string());
  session->gossip().ClearGossipDialog();
  ScriptEventDispatch::Get().FireEvent(events::GOSSIP_CONFIRM_CANCEL);
  return 0;
}

int LuaSelectGossipAvailableQuest(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SelectGossipAvailableQuest(index)");
  }

  auto *session = GetWorldSession(L);
  if (!session || !session->gossip().has_gossip() || !session->objects().GetLocalPlayer())
    return 0;

  const auto zero_based_index = SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;

  const auto selection = session->gossip().GetGossipAvailableQuestSelection(
      zero_based_index);
  if (!selection.has_value())
    return 0;

  const auto npc_guid = session->gossip().gossip().npc_guid.GetRawValue();
  if (selection->action == openwow::game::GossipQuestSelectionAction::kCompleteQuest) {
    session->interaction().SendQuestGiverCompleteQuest(npc_guid, selection->quest->quest_id);
    return 0;
  }

  session->interaction().SendQuestGiverQueryQuest(npc_guid, selection->quest->quest_id);
  return 0;
}

int LuaSelectGossipActiveQuest(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SelectGossipActiveQuest(index)");
  }

  auto *session = GetWorldSession(L);
  if (!session || !session->gossip().has_gossip() || !session->objects().GetLocalPlayer())
    return 0;

  const auto *quest =
      session->gossip().GetGossipActiveQuest(SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u);
  if (quest == nullptr)
    return 0;

  session->interaction().SendQuestGiverCompleteQuest(
      session->gossip().gossip().npc_guid.GetRawValue(), quest->quest_id);
  return 0;
}

int LuaCloseGossip(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session) {

    CloseGossipInteraction(*session);
  }
  return 0;
}

int LuaForceGossip(lua_State *L) {
  constexpr std::uint32_t kCreatureTypeFlagForceGossip = 0x08000000;

  bool force = false;
  auto *session = GetWorldSession(L);
  if (session && session->gossip().has_gossip()) {
    const auto &dialog = session->gossip().gossip();
    const auto *npc = session->objects().GetUnit(dialog.npc_guid);
    if (npc) {
      const auto *info =
          session->query_cache().GetCreatureTemplate(npc->GetEntry());
      if (info) {
        force = (info->type_flags & kCreatureTypeFlagForceGossip) != 0;
      }
    }
  }
  lua_pushboolean(L, force ? 1 : 0);
  return 1;
}

}

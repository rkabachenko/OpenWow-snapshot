
#include "openwow/game/world_session.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/console.h"
#include "openwow/game/commerce/auctions/auction_state.h"
#include "openwow/game/commerce/auctions/adapters/protocol/auction_packet_codec.h"
#include "openwow/game/commerce/trade/adapters/protocol/trade_packet_codec.h"
#include "openwow/game/commerce/mail/adapters/protocol/mail_packet_codec.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/combat_log_internal.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/cooldown_tracker.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/lcd_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/money_display.h"
#include "openwow/game/object_types.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/player_bag_family.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/readable_text.h"
#include "openwow/game/tabard_frame.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/trainer_frame.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_gossip.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/game/trade_cursor_utils.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/foundation/diagnostics/logging.h"

#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace openwow::game {

namespace {

constexpr std::uint32_t kGuildRegistrarNpcFlagsMask = 0x000C0000u;
constexpr int kOfferPetitionErrorSystemMessageId = 523;

void HandleOfferPetitionErrorNameLookup(WorldSession &session, const std::uint64_t guid) {
  if (guid == 0) {
    return;
  }

  if (const auto *cached_name = session.query_cache().GetPlayerName(guid)) {
    ui::game::DisplaySystemMessage(kOfferPetitionErrorSystemMessageId,
                                   cached_name->name.c_str());
    return;
  }

  (void)session.query_cache().RequestNameQuery(guid);
}

void FireActivePlayerPortraitAndModelEvents(WorldSession &session) {
  if (session.objects().GetActivePlayer() == nullptr) {
    return;
  }

  const auto active_player_guid = session.objects().GetActivePlayerGuid().GetRawValue();
  if (active_player_guid == 0) {
    return;
  }

  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireUnitPortrait(active_player_guid);
  dispatch.FireUnitModel(active_player_guid);
}

}

void WorldSession::ClosePetitionVendorInteraction() {
  if (petition_.petition_vendor_guid() == 0) {
    return;
  }

  petition_.ClearPetitionVendorInteraction();
  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::PETITION_VENDOR_CLOSED);
}

void WorldSession::CloseGuildRegistrarInteraction() {
  const auto vendor_guid = petition_.guild_registrar_guid();
  if (vendor_guid == 0) {
    return;
  }

  ui::game::ApplyNpcInteractionCloseFeedback(
      *this, ObjectGuid(vendor_guid),
      ui::game::NpcInteractionClosureCause::UnitUnavailable);
  petition_.ClearGuildRegistrarGuid();
  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::GUILD_REGISTRAR_CLOSED);
}

void WorldSession::ClosePetitionSignatureDisplay() {
  ApplyPetitionUiTransition(petition_.ClosePetitionSignatureDisplay(
      objects().GetActivePlayerGuid().GetRawValue(), send_fn_));
}

void WorldSession::CloseTabardVendorInteraction() {
  const auto vendor_guid = petition_.tabard_vendor_guid();
  if (vendor_guid == 0) {
    return;
  }

  FireActivePlayerPortraitAndModelEvents(*this);
  ui::game::ApplyNpcInteractionCloseFeedback(
      *this, ObjectGuid(vendor_guid),
      ui::game::NpcInteractionClosureCause::UnitUnavailable);
  petition_.ClearTabardVendorInteraction();
  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::CLOSE_TABARD_FRAME);
}

void WorldSession::HandlePetitionShowList(const net::wotlk::WorldPacket &pkt) {
  PetitionShowList list{};
  if (!petition_.ParsePetitionShowList(pkt.payload.data(), pkt.payload.size(), &list)) {
    return;
  }

  const auto* vendor = objects().GetUnit(ObjectGuid(list.npc_guid));
  if (vendor == nullptr) {
    return;
  }

  ui::game::SetNpcInteractionTarget(ObjectGuid(list.npc_guid));

  if ((vendor->State().GetNpcFlags() & kGuildRegistrarNpcFlagsMask) ==
      kGuildRegistrarNpcFlagsMask) {
    const PetitionType charter_offer =
        list.types.empty() ? PetitionType{} : list.types.front();
    petition_.OpenGuildRegistrar(list.npc_guid, charter_offer);
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::GUILD_REGISTRAR_SHOW);
    return;
  }

  petition_.SetPetitionShowList(std::move(list));

  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::PETITION_VENDOR_UPDATE);
  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::PETITION_VENDOR_SHOW);
}

void WorldSession::HandlePetitionShowSignatures(const net::wotlk::WorldPacket &pkt) {
  if (!petition_.HandlePetitionShowSignatures(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  ApplyPetitionUiTransition(
      petition_.BeginPetitionSignatureDisplay(query_cache_, objects(), send_fn_));
}

void WorldSession::HandlePetitionSignResults(const net::wotlk::WorldPacket &pkt) {
  if (!petition_.HandlePetitionSignResults(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  ApplyPetitionUiTransition(petition_.ApplyPetitionSignResult(
      objects().GetActivePlayerGuid().GetRawValue(), query_cache_, objects(), send_fn_));
}

void WorldSession::HandlePetitionQueryResponse(const net::wotlk::WorldPacket &pkt) {
  if (!petition_.HandlePetitionQueryResponse(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (petition_.last_petition_query_was_update()) {
    ApplyPetitionUiTransition(petition_.OnPetitionQueryResponseUpdated());
  }
}

void WorldSession::HandleTurnInPetitionResults(const net::wotlk::WorldPacket &pkt) {
  petition_.HandleTurnInPetitionResults(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::ApplyPetitionUiTransition(const PetitionUiTransition &transition) {
  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  if (transition.system_message_id.has_value()) {
    if (transition.system_message_arg.empty()) {
      ui::game::DisplaySystemMessage(*transition.system_message_id);
    } else {
      ui::game::DisplaySystemMessage(*transition.system_message_id,
                                     transition.system_message_arg.c_str());
    }
  }
  if (!transition.console_line.empty()) {
    openwow::core::ida::ConsoleAddLine(transition.console_line,
                                       openwow::core::ida::COLOR_DEFAULT);
  }
  if (transition.fire_closed) {
    dispatch.FireEvent(ui::game::events::PETITION_CLOSED);
  }
  if (transition.fire_show) {
    dispatch.FireEvent(ui::game::events::PETITION_SHOW);
  }
}

void WorldSession::HandleSaveGuildEmblem(const net::wotlk::WorldPacket &pkt) {
  if (!petition_.HandleSaveGuildEmblem(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  TabardFrame_HandleSaveResult(*this, petition_.guild_emblem_result());
}

void WorldSession::HandleTabardVendorActivate(const net::wotlk::WorldPacket &pkt) {
  if (!petition_.HandleTabardVendorActivate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  auto *player = objects().GetActivePlayer();
  if (player != nullptr) {
    player->RefreshCharacterModelAndQueuePortraitEvents();
  }
  ui::game::SetNpcInteractionTarget(
      ObjectGuid(petition_.tabard_vendor_guid()));
  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::OPEN_TABARD_FRAME);
}

void WorldSession::HandlePetitionDecline(const net::wotlk::WorldPacket &pkt) {
  petition_.HandlePetitionDecline(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandlePetitionRename(const net::wotlk::WorldPacket &pkt) {
  if (!petition_.HandlePetitionRename(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &rename = petition_.last_petition_rename();
  if (!rename.has_value() || rename->name.empty()) {
    return;
  }

  const auto *item = inventory_replica_.FindItemByGuid(rename->guid);
  if (item == nullptr) {
    return;
  }

  const auto *item_template = item_definitions_.GetItem(item->entry);
  if (item_template == nullptr ||
      (item_template->flags & kItemTemplateFlagPetition) == 0) {
    return;
  }

  const std::uint32_t petition_id = item->enchantments[0].id;
  if (petition_.UpdateCachedPetitionName(petition_id, rename->name)) {
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::PETITION_SHOW);
  }
}

void WorldSession::HandleOfferPetitionError(const net::wotlk::WorldPacket &pkt) {
  if (!petition_.HandleOfferPetitionError(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  HandleOfferPetitionErrorNameLookup(*this, petition_.petition_error_guid());
}

}

#include "openwow/game/session/handlers/inventory/loot_roll_packets.h"

#include "openwow/game/chat_display.h"
#include "openwow/game/inventory/loot/adapters/protocol/loot_packet_codec.h"
#include "openwow/game/inventory/loot/adapters/protocol/loot_roll_packet_codec.h"
#include "openwow/game/inventory/loot/adapters/ui/loot_roll_result_presenter.h"
#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/query_cache.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <utility>

namespace openwow::game {
namespace {

void DisplayLootMessage(const ObjectManager& objects, std::string message) {
  ChatFrame_DisplayMessage(
      objects, message.c_str(), ChatDisplayType::kLoot, nullptr, 0,
      nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
}

void PresentStarted(const PendingRollStartEvent& event) {
  ui::game::ScriptEventDispatch::Get().FireStartLootRoll(
      static_cast<int>(event.roll_id),
      static_cast<int>(event.countdown_ms));
}

void PresentCompletedRoll(const std::optional<PendingRollRemoval>& removal) {
  if (!removal.has_value()) {
    return;
  }
  if (removal->fire_cancel_event) {
    ui::game::ScriptEventDispatch::Get().FireCancelLootRoll(
        static_cast<int>(removal->roll_id));
  }
  for (const auto& event : removal->start_events) {
    PresentStarted(event);
  }
}

void ResolveAllPassed(
    LootInteraction& loot, ObjectManager& objects, QueryCache& queries,
    Localization& localization, ui::game::CVarSystem& cvars,
    const data::dbc::DbcLoader* dbc,
    const GroupLootAllPassed& all_passed) {
  loot.SetSlotAllowLootForActiveSource(
      all_passed.source_guid, all_passed.item_slot);
  const auto roll = loot.state().FindPendingRollBySourceAndSlot(
      all_passed.source_guid, all_passed.item_slot);
  ResolveAndDisplayLootAllPassed(
      objects, queries, localization, cvars,
      [&objects](std::string message) {
        DisplayLootMessage(objects, std::move(message));
      },
      dbc, all_passed,
      [&loot, roll](const bool resolved) {
        if (!roll.has_value()) {
          return;
        }
        if (!resolved) {
          loot.state().DiscardPendingRollAfterCacheFailure(*roll);
          return;
        }
        PresentCompletedRoll(loot.state().CompletePendingRoll(*roll));
      });
}

}

void HandleLootStartRollPacket(
    LootInteraction& loot, ItemDefinitions& items, QueryCache& queries,
    const std::uint32_t active_map_id, const std::uint32_t now,
    const net::wotlk::WorldPacket& packet) {
  const auto message = loot_protocol::DecodeStartRoll(
      packet.payload.data(), packet.payload.size());
  if (!message.has_value()) {
    return;
  }

  if (message->map_id != active_map_id) {
    return;
  }

  PendingRollEntry roll{
      .loot_guid = message->source_guid,
      .loot_slot = message->item_slot,
      .item_id = message->item_id,
      .item_count = message->item_count,
      .random_suffix = message->random_suffix,
      .random_property_id =
          static_cast<std::int32_t>(message->random_prop_id),
      .countdown_ms = message->countdown_ms,
      .roll_vote_mask = message->roll_vote_mask,
      .deadline_tick_ms = now + message->countdown_ms,
      .item_template_ready = items.HasItem(message->item_id),
  };
  if (!roll.item_template_ready) {
    (void)queries.GetOrRequestItemTemplate(message->item_id);
  }
  if (const auto event = loot.state().AddPendingRoll(std::move(roll))) {
    PresentStarted(*event);
  }
}

void HandleLootRollPacket(
    ObjectManager& objects, QueryCache& queries, Localization& localization,
    ui::game::CVarSystem& cvars, const data::dbc::DbcLoader* dbc,
    const net::wotlk::WorldPacket& packet) {
  const auto result = loot_protocol::DecodeRoll(
      packet.payload.data(), packet.payload.size());
  if (!result.has_value()) {
    return;
  }
  ResolveAndDisplayLootRollResult(
      objects, queries, localization, cvars,
      [&objects](std::string message) {
        DisplayLootMessage(objects, std::move(message));
      },
      dbc, *result);
}

void HandleLootAllPassedPacket(
    LootInteraction& loot, ObjectManager& objects, QueryCache& queries,
    Localization& localization, ui::game::CVarSystem& cvars,
    const data::dbc::DbcLoader* dbc,
    const net::wotlk::WorldPacket& packet) {
  const auto all_passed = loot_protocol::DecodeAllPassed(
      packet.payload.data(), packet.payload.size());
  if (all_passed.has_value()) {
    ResolveAllPassed(loot, objects, queries, localization, cvars, dbc,
                     *all_passed);
  }
}

void HandleLootRollWonPacket(
    LootInteraction& loot, ObjectManager& objects, QueryCache& queries,
    Localization& localization, ui::game::CVarSystem& cvars,
    const data::dbc::DbcLoader* dbc,
    const net::wotlk::WorldPacket& packet) {
  auto decoded = loot_protocol::DecodeRollWon(
      packet.payload.data(), packet.payload.size());
  if (!decoded.has_value()) {
    return;
  }
  loot.HandleLootRollWon(std::move(*decoded));
  if (!loot.last_roll_won().has_value()) {
    return;
  }

  const auto& roll_won = *loot.last_roll_won();
  loot.SetSlotAllowLootForActiveSource(roll_won.source_guid, roll_won.slot);
  const auto matched_roll = loot.state().FindPendingRollBySourceAndSlot(
      roll_won.source_guid, roll_won.slot);
  ResolveAndDisplayLootRollWinner(
      objects, queries, localization, cvars,
      [&objects](std::string message) {
        DisplayLootMessage(objects, std::move(message));
      },
      dbc, roll_won,
      [&loot, matched_roll](const bool resolved) {
        if (!matched_roll.has_value()) {
          return;
        }
        if (!resolved) {
          loot.state().DiscardPendingRollAfterCacheFailure(*matched_roll);
          return;
        }
        PresentCompletedRoll(loot.state().CompletePendingRoll(*matched_roll));
      });
}

}

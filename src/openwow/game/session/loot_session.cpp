#include "openwow/game/inventory/equipment/adapters/protocol/equipment_set_packet_codec.h"

#include "openwow/game/world_session.h"
#include "openwow/ui/frame_script_events.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/game/session/handlers/commerce/mail_packets.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/client_misc.h"
#include "openwow/core/console.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/achievements/adapters/protocol/achievement_protocol.h"
#include "openwow/game/achievements/rules/achievement_category_resolver.h"
#include "openwow/game/barber_shop.h"
#include "openwow/game/calendar/adapters/protocol/calendar_date_fields_packed.h"
#include "openwow/game/calendar/calendar_time.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/chat_message_formatters.h"
#include "openwow/game/combat/application/client_control_transition.h"
#include "openwow/game/combat/adapters/ui/auto_attack_activity_presenter.h"
#include "openwow/game/comsat_client.h"
#include "openwow/game/activities/dance/adapters/protocol/dance_protocol.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/world/environment/day_night.h"
#include "openwow/game/emote_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/faction_system.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/group_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/knowledge_base.h"
#include "openwow/game/localization.h"
#include "openwow/game/inventory/loot/adapters/protocol/loot_packet_codec.h"
#include "openwow/game/inventory/loot/adapters/ui/loot_roll_result_presenter.h"
#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/game/minimap_ping.h"
#include "openwow/game/money_display.h"
#include "openwow/game/object_types.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/quest_dialog_close.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_poi.h"
#include "openwow/game/readable_text.h"
#include "openwow/game/reputation_info.h"

#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_failure_names.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/taxi_map_frame.h"
#include "openwow/game/taxi_runtime_slice.h"
#include "openwow/game/taxi_system.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/voice_chat.h"
#include "openwow/game/world_scene_state.h"
#include "openwow/net/client_services.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_guild_roster_view.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/game/combat/adapters/ui/combo_point_presentation.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/ui/game/quest_log_interleaved.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/foundation/diagnostics/logging.h"

#include "openwow/game/account_data.h"
#include "openwow/game/account_data_runtime_sync.h"
#include "openwow/game/achievements/application/tracked_achievement_state.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/title_system.h"
#include "openwow/game/talent_info.h"
#include "openwow/core/init_subsystems.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace openwow::game {

namespace {

std::string BuildLootMoneyNotifyMessage(const LootMoneyNotify &notify) {
  if (notify.copper == 0) {
    return {};
  }

  const std::string money_text =
      MoneyDisplay::FormatLocalizedCoinText(notify.copper, ", ");
  if (money_text.empty()) {
    return {};
  }

  auto &localization = Localization::Get();
  const char *const format_key = notify.is_solo ? "YOU_LOOT_MONEY" : "LOOT_MONEY_SPLIT";
  const std::string format = localization.GetString(format_key, "");
  if (format.empty()) {
    return {};
  }

  return localization.FormatString(format, {money_text});
}
std::optional<std::uint8_t> FindRaidRosterSubGroup(const GroupSystem &group,
                                                   const std::uint64_t guid) {
  const auto member_count = group.GetNumGroupMembers();
  for (std::size_t index = 0; index < member_count; ++index) {
    const auto member = group.GetMemberSnapshot(index);
    if (!member.has_value() || member->guid != guid) {
      continue;
    }

    return member->group_index;
  }

  return std::nullopt;
}

std::vector<std::uint64_t>
BuildMasterLootCandidateSlots(const GroupSystem &group,
                              const std::vector<std::uint64_t> &packet_guids) {
  std::vector<std::uint64_t> slots(LootState::kMaxMasterLootCandidateSlots, 0);
  const auto limit = std::min(packet_guids.size(), slots.size());

  if (group.GetNumGroupMembers() == 0) {
    std::copy_n(packet_guids.begin(), limit, slots.begin());
    return slots;
  }

  for (const auto guid : packet_guids) {
    if (guid == 0) {
      continue;
    }

    const auto sub_group = FindRaidRosterSubGroup(group, guid);
    if (!sub_group.has_value() || *sub_group >= 8) {
      continue;
    }

    const std::size_t block_start = static_cast<std::size_t>(*sub_group) * 5;
    const std::size_t block_end = std::min(block_start + 5, slots.size());
    for (std::size_t slot_index = block_start; slot_index < block_end; ++slot_index) {
      if (slots[slot_index] == 0) {
        slots[slot_index] = guid;
        break;
      }
    }
  }

  return slots;
}

bool HasResolvedLootItemTemplate(WorldSession &session, const std::uint32_t item_id) {
  return session.item_definitions().HasItem(item_id);
}

void CloseExistingLootWindowForIncomingLoot(WorldSession &session) {
  auto &loot = session.loot();
  if (!loot.is_looting()) {
    return;
  }

  const LootWindow previous_window = loot.loot_window();
  CleanupClosingLootSourceState(session, previous_window);
  ClearActivePlayerLootInteractionState(session);

  loot.CloseLootWindow();
  loot.state().ClearPendingConfirmSlot();
  ui::game::ScriptEventDispatch::Get().FireLootClosed();
}

void DispatchLootOpenedOrApplyAutoLoot(WorldSession &session) {
  if (session.loot().pending_auto_loot()) {
    const bool should_open_window = ApplyPendingAutoLoot(session);
    if (should_open_window) {
      ui::game::ScriptEventDispatch::Get().FireLootOpened(true);
    }
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireLootOpened(false);
}

void RegisterLootItemQuery(WorldSession &session, const LootInteraction::ItemQueryRequest &request) {
  const auto owner = session.lifetime_token();
  QueryCache::QueryRequestOptions options;
  options.callback_key =
      AsyncQueryChannel::CallbackKey(0x4FDu, static_cast<std::uint32_t>(request.request_id));
  options.callback = [owner, &session, request](const bool success) {
    if (owner.expired()) {
      return;
    }
    const auto resolution = session.loot().ResolveItemQuery(request.request_id, success);
    for (const int ui_slot : resolution.changed_ui_slots) {
      ui::game::ScriptEventDispatch::Get().FireLootSlotChanged(ui_slot);
    }
    if (resolution.fire_loot_opened) {
      DispatchLootOpenedOrApplyAutoLoot(session);
    }
  };

  (void)session.query_cache().GetOrRequestItemTemplate(request.item_id, std::move(options));
}

}

void WorldSession::HandleLootResponse(const net::wotlk::WorldPacket &pkt) {
  using openwow::ui::game::DisplaySystemMessage;

  auto loot_window =
      loot_protocol::DecodeLootResponse(pkt.payload.data(), pkt.payload.size());
  if (!loot_window.has_value()) {
    return;
  }
  if (!loot_.ConsumeLootResponse(loot_window->source_guid)) {
    return;
  }

  if (loot_window->loot_type == LootType::kNone) {
    const auto mapping = MapLootResponseError(loot_window->error_sub_type);
    DisplaySystemMessage(mapping.system_message_id);
    if (mapping.clears_active_loot) {
      ClearActivePlayerLootInteractionState(*this);
      ui::game::GameUI_OnMouseoverUnitLeave(loot_window->source_guid.GetRawValue());
    }
    return;
  }

  const ObjectGuid source_guid = loot_window->source_guid;
  const auto *source_object = map_runtime_.objects().Get(source_guid);

  if (source_object == nullptr) {
    DisplaySystemMessage(140);

    (void)loot_.TakePendingReleaseGuid();
    interaction_.SendLootRelease(source_guid.GetRawValue());
    return;
  }

  const bool source_is_unit = source_object->IsUnit();
  if (source_is_unit) {
    if (loot_source_target_selection_callback_) {
      loot_source_target_selection_callback_(source_guid.GetRawValue());
    }
  }

  CloseExistingLootWindowForIncomingLoot(*this);
  if (source_is_unit) {
    if (auto *active_player = map_runtime_.objects().GetActivePlayer(); active_player != nullptr) {

      active_player->Animation().SetLootTargetAndPlayLootAnimation(
          source_guid.GetRawValue());
    }
  }
  loot_.SetLootWindow(std::move(*loot_window));
  loot_.state().ClearPendingConfirmSlot();
  const auto pending_queries = loot_.BeginLootWindowItemQueries(
      [this](const std::uint32_t item_id) { return HasResolvedLootItemTemplate(*this, item_id); });
  for (const auto &query : pending_queries) {
    RegisterLootItemQuery(*this, query);
  }

  if (!loot_.HasPendingItemQueries()) {
    DispatchLootOpenedOrApplyAutoLoot(*this);
  }
}

void WorldSession::HandleLootReleaseResponse(const net::wotlk::WorldPacket &pkt) {
  const auto response = loot_protocol::DecodeLootReleaseResponse(
      pkt.payload.data(), pkt.payload.size());
  if (!response) {
    return;
  }
  const auto result =
      loot_.HandleLootReleaseResponse(response->first, response->second);
  if (!result.should_close) {
    return;
  }

  CloseActiveLootWindow(*this, CloseLootWindowOptions{
                                   .send_release = false,
                                   .skip_item_check = false,
                                   .show_interrupted = false,
                                   .clear_dead_target = true,
                               });
}

void WorldSession::HandleLootRemoved(const net::wotlk::WorldPacket &pkt) {
  const auto wire_slot =
      loot_protocol::DecodeRemovedSlot(pkt.payload.data(), pkt.payload.size());
  if (!wire_slot) {
    return;
  }
  const bool was_looting = loot_.is_looting();
  std::optional<int> ui_slot;
  if (was_looting) {
    if (const auto display_index =
            LootInteraction::FindDisplayIndexForWireSlot(loot_.loot_window(),
                                                         *wire_slot)) {
      ui_slot = static_cast<int>(*display_index) + 1 +
                (loot_.loot_window().gold_slot_reserved ? 1 : 0);
    }
  }
  loot_.HandleLootRemoved(*wire_slot);
  if (ui_slot.has_value()) {
    ui::game::ScriptEventDispatch::Get().FireLootSlotCleared(*ui_slot);
  }
  if (was_looting && !loot_.is_looting()) {
    loot_.state().ClearPendingConfirmSlot();
    ui::game::ScriptEventDispatch::Get().FireLootClosed();
  }
}

void WorldSession::HandleLootMoneyNotify(const net::wotlk::WorldPacket &pkt) {
  auto decoded_notify =
      loot_protocol::DecodeMoneyNotify(pkt.payload.data(), pkt.payload.size());
  if (!decoded_notify) {
    return;
  }
  loot_.HandleLootMoneyNotify(std::move(*decoded_notify));

  const auto &state_notify = loot_.last_money_notify();
  if (!state_notify.has_value()) {
    return;
  }

  const std::string message =
      BuildLootMoneyNotifyMessage(*state_notify);
  if (message.empty()) {
    return;
  }

  ChatFrame_DisplayMessage(objects(), message.c_str(), ChatDisplayType::kMoney, nullptr, 0, nullptr, nullptr,
                           nullptr, 0, 0, 0, 0, 0, nullptr);
}

void WorldSession::HandleLootClearMoney(const net::wotlk::WorldPacket & ) {
  const auto result = loot_.HandleLootClearMoney();
  if (!result.cleared_gold) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireLootSlotCleared(1);
  if (!result.should_release_and_close) {
    return;
  }

  CloseActiveLootWindow(*this, CloseLootWindowOptions{
                                   .send_release = true,
                                   .skip_item_check = false,
                                   .show_interrupted = false,
                                   .clear_dead_target = true,
                               });
}

void WorldSession::HandleLootItemNotify(const net::wotlk::WorldPacket &pkt) {
  if (auto notify =
          loot_protocol::DecodeItemNotify(pkt.payload.data(), pkt.payload.size())) {
    loot_.HandleLootItemNotify(std::move(*notify));
  }
}

void WorldSession::HandleLootList(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  ObjectGuid creature_guid;
  if (!reader.ReadGuid(creature_guid)) {
    return;
  }

  if (auto *const unit = map_runtime_.objects().GetMutableUnit(creature_guid);
      unit != nullptr) {
    unit->Loot().StoreLootListGuids(reader);
  }
}

void WorldSession::HandleLootMasterList(const net::wotlk::WorldPacket &pkt) {
  auto list =
      loot_protocol::DecodeMasterList(pkt.payload.data(), pkt.payload.size());
  if (!list) {
    return;
  }
  loot_.HandleLootMasterList(std::move(*list));
  loot_.state().SetMasterLootCandidates(
      BuildMasterLootCandidateSlots(GroupSystem::Get(),
                                    loot_.last_master_list()->player_guids));
}

void WorldSession::HandleLootSlotChanged(const net::wotlk::WorldPacket &pkt) {
  auto changed =
      loot_protocol::DecodeSlotChanged(pkt.payload.data(), pkt.payload.size());
  if (!changed) {
    return;
  }
  loot_.HandleLootSlotChanged(std::move(*changed));

  const auto result = loot_.ApplyLootSlotChanged(
      *loot_.last_slot_changed(),
      HasResolvedLootItemTemplate(*this, loot_.last_slot_changed()->item_id));
  if (!result.applied) {
    return;
  }

  if (result.immediate_changed_ui_slot.has_value()) {
    ui::game::ScriptEventDispatch::Get().FireLootSlotChanged(*result.immediate_changed_ui_slot);
  }
  if (result.item_query.has_value()) {
    RegisterLootItemQuery(*this, *result.item_query);
  }
}

}

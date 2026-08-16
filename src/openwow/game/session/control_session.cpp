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
#include "openwow/game/quest_dialog_close.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_poi.h"
#include "openwow/game/readable_text.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_c_internals.h"
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

void WorldSession::HandleForceMoveRoot(const net::wotlk::WorldPacket &pkt) {
  if (!session_.HandleForceMoveRoot(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &root = session_.last_root();
  auto *const mover = objects().GetMutableUnit(root.guid);
  if (mover == nullptr) {
    return;
  }
  const auto timestamp = CurrentClientTimeMs();
  mover->Movement().Data().QueueDeferredMoveEvent(
      timestamp, static_cast<std::uint32_t>(movement::MoveEventType::kRoot),
      true, root.counter, 0.0f, false, timestamp);
}

void WorldSession::HandleForceMoveUnroot(const net::wotlk::WorldPacket &pkt) {
  if (!session_.HandleForceMoveUnroot(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &unroot = session_.last_unroot();
  auto *const mover = objects().GetMutableUnit(unroot.guid);
  if (mover == nullptr) {
    return;
  }
  const auto timestamp = CurrentClientTimeMs();
  mover->Movement().Data().QueueDeferredMoveEvent(
      timestamp, static_cast<std::uint32_t>(movement::MoveEventType::kUnroot),
      true, unroot.counter, 0.0f, false, timestamp);
}

void WorldSession::HandleMoveKnockBack(const net::wotlk::WorldPacket &pkt) {
  if (!session_.HandleMoveKnockBack(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &kb = session_.last_knockback();
  auto *const mover = objects().GetMutableUnit(kb.guid);
  if (mover == nullptr) {
    return;
  }
  mover->Movement().Data().QueueKnockBack(
      CurrentClientTimeMs(), kb.counter, kb.cos_angle, kb.sin_angle,
      kb.speed_xy, kb.speed_z);
}

void WorldSession::HandleMoveSetCanFly(const net::wotlk::WorldPacket &pkt) {
  if (!session_.HandleMoveSetCanFly(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &can_fly = session_.last_can_fly();
  auto *const mover = objects().GetMutableUnit(can_fly.guid);
  if (mover == nullptr) {
    return;
  }
  const auto timestamp = CurrentClientTimeMs();
  mover->Movement().Data().QueueDeferredMoveEvent(
      timestamp,
      static_cast<std::uint32_t>(movement::MoveEventType::kCanFlyEnable),
      true, can_fly.counter, 0.0f, false, timestamp);
}

void WorldSession::HandleMoveUnsetCanFly(const net::wotlk::WorldPacket &pkt) {
  if (!session_.HandleMoveUnsetCanFly(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto &can_fly = session_.last_unset_fly();
  auto *const mover = objects().GetMutableUnit(can_fly.guid);
  if (mover == nullptr) {
    return;
  }
  const auto timestamp = CurrentClientTimeMs();
  mover->Movement().Data().QueueDeferredMoveEvent(
      timestamp,
      static_cast<std::uint32_t>(movement::MoveEventType::kCanFlyDisable),
      true, can_fly.counter, 0.0f, false, timestamp);
}

void WorldSession::HandleClientControlUpdate(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  if (!reader.ReadPackedGuid(client_control_.guid))
    return;
  std::uint8_t enabled_wire = 0;
  if (!reader.ReadU8(enabled_wire))
    return;

  client_control_.enabled = enabled_wire != 0;
  ApplyStoredClientControl();
}

void WorldSession::ApplyStoredClientControl() {
  const auto &ctrl = client_control_;
  if (ctrl.guid.IsEmpty()) {
    return;
  }
  const auto guid_raw = ctrl.guid.GetRawValue();
  const bool enabled = ctrl.enabled;

  auto *unit = objects().GetMutableUnit(ctrl.guid);

  if (unit != nullptr) {

    const auto auto_attack_change = combat::ApplyClientControlPermission(
        *this, *unit, enabled ? combat::ClientControlPermission::Granted
                             : combat::ClientControlPermission::Revoked);
    if (unit->IsActivePlayer()) {
      combat::ui::PresentAutoAttackActivityChange(*this, auto_attack_change);
    }

    const auto camera_target =
        world_camera_ != nullptr
            ? ObjectGuid(world_camera_->bound_object())
            : ObjectGuid{};

    if (camera_target.GetRawValue() == guid_raw) {

      if (unit->Movement().CanControlCharacter()) {
        player_control_runtime_.SetActiveMover(
            *this, objects(), missile_trajectory(), guid_raw);
      } else if (unit->IsActiveMover()) {
        player_control_runtime_.SetActiveMover(
            *this, objects(), missile_trajectory(), 0);
      }
    } else if (unit->IsActiveMover() &&
               !unit->Movement().CanControlCharacter()) {

      auto *cam_unit = objects().GetMutableUnit(camera_target);
      if (cam_unit != nullptr && cam_unit->Movement().CanControlCharacter()) {
        player_control_runtime_.SetActiveMover(
            *this, objects(), missile_trajectory(), camera_target.GetRawValue());
      } else {
        player_control_runtime_.SetActiveMover(
            *this, objects(), missile_trajectory(), 0);
      }
    }
  } else {
    player_control_runtime_.SetCombatFocusGuid(guid_raw, enabled);
  }
}

void WorldSession::HandleCancelAutoRepeat(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  if (!reader.ReadPackedGuid(cancel_auto_repeat_info_.target_guid))
    return;

  const auto &info = cancel_auto_repeat_info_;

  auto *unit = objects().GetMutableUnit(info.target_guid);
  if (unit == nullptr)
    return;

  if (info.target_guid == objects().GetActivePlayerGuid()) {
    spell_cast_runtime_.StopAutoRepeat(*this);
  }

  unit->Animation().SetAutoRepeatActive(false);

  unit->Animation().ResetAuraAnimationVisualState(*this);
}

void WorldSession::HandleDismount(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  if (!reader.ReadPackedGuid(dismount_guid_))
    return;

  const ObjectGuid dismount_guid = dismount_guid_;
  auto *unit = objects().GetMutableUnit(dismount_guid);
  if (unit == nullptr)
    return;

  unit->Mount().HandleDismountPacket(*unit);

  unit->Animation().RefreshSelectedStandAnimation(*this, 0u, ~0u);
}

void WorldSession::HandleDismountResult(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleDismountResult(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  constexpr std::array<int, 3> kDismountResultMessages{221, 222, 223};
  const auto result = misc_.dismount_result();
  if (result < kDismountResultMessages.size()) {
    ui::game::DisplaySystemMessage(kDismountResultMessages[result]);
  }
}

}

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

void WorldSession::InvalidateDecodedCaches(
    const std::uint32_t version,
    const openwow::data::DBCacheVersionChanges& changes) {
    if (changes.Changed(data::WDBCacheType::Creature)) {
      query_cache_.ClearCreatureEntriesForClientCacheVersion();
    }
    if (changes.Changed(data::WDBCacheType::GameObject)) {
      query_cache_.ClearGameObjectEntriesForClientCacheVersion();
    }
    if (changes.Changed(data::WDBCacheType::Item)) {
      query_cache_.ClearItemEntriesForClientCacheVersion();
      item_definitions_.ClearItems();
    }
    if (changes.Changed(data::WDBCacheType::ItemName)) {
      item_definitions_.ClearItemNames();
    }
    if (changes.Changed(data::WDBCacheType::NpcText)) {
      query_cache_.ClearNpcTextEntriesForClientCacheVersion();
    }
    if (changes.Changed(data::WDBCacheType::Name)) {
      query_cache_.ApplyPlayerNameCacheVersion(version);
    }
    if (changes.Changed(data::WDBCacheType::Guild)) {
      guild_.ClearGuildQueryCacheForClientCacheVersion();
    }
    if (changes.Changed(data::WDBCacheType::Quest)) {
      quests_.HandleClientCacheVersionInvalidation();
    }
    if (changes.Changed(data::WDBCacheType::PageText)) {
      misc_.ClearPageTextCacheForClientCacheVersion();
    }
    if (changes.Changed(data::WDBCacheType::PetName)) {
      pet_.ClearNameCacheForClientCacheVersion();
    }
    if (changes.Changed(data::WDBCacheType::Petition)) {
      petition_.HandleClientCacheVersionInvalidation();
    }
    if (changes.item_text) {
      item_interactions_.clear_text_cache();
    }
    if (changes.Changed(data::WDBCacheType::ArenaTeam)) {
      arena_.ClearArenaTeamQueryMirror();
    }
}

void WorldSession::HandleInvalidatePlayer(const net::wotlk::WorldPacket &pkt) {
  if (!character_.HandleInvalidatePlayer(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto invalidated_guid = ObjectGuid(character_.invalidated_player_guid());
  if (invalidated_guid.IsEmpty()) {
    return;
  }

  if (invalidated_guid.IsGuildCacheKey()) {
    guild_.InvalidateCachedGuildInfo(invalidated_guid.GetLowPart());
    return;
  }

  (void)query_cache_.InvalidatePlayerName(invalidated_guid.GetRawValue());
  (void)map_runtime_.objects().InvalidatePlayerName(invalidated_guid);
  if (ui::game::detail::GuildRosterDisplayContainsGuid(*this, invalidated_guid.GetRawValue())) {
    interaction().SendGuildRosterRefresh();
  }
  if (!map_runtime_.objects().GetActivePlayerGuid().IsEmpty() &&
      CalendarSystem::Get().HasIndexedDayEventNameReference(invalidated_guid.GetRawValue())) {
    interaction().SendCalendarGetCalendar();
  }

  (void)query_cache_.RequestNameQuery(invalidated_guid.GetRawValue());

  (void)party_stats_.ClearCachedReferAFriendFlag(invalidated_guid.GetRawValue());
  if (social_.ClearFriendReferAFriendFlag(invalidated_guid)) {
    ui::game::ScriptEventDispatch::Get().FireFriendListUpdate();
  }
}

}

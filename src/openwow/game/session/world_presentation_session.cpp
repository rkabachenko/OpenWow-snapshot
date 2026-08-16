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
#include "openwow/core/localized_format.h"
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
#include "openwow/game/death_manager.h"
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
#include "openwow/game/minimap_terrain.h"
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

namespace {

constexpr int kFeignDeathResistedSystemMessage = 0x1C6;

constexpr int kCorpseIsNotInInstanceSystemMessage = 0x1AB;

constexpr int kInvalidPromotionCodeSystemMessage = 0x1E5;

constexpr int kDeathbindSuccessSystemMessage = 0x14B;
constexpr std::int32_t kDeathbindSoundKitId = 0x475;

constexpr int kMountResultSuccessValue = 10;
constexpr std::array<int, 10> kMountResultMessages{
    212, 213, 214, 215, 216, 217, 218, 219, 220, 312};

int ResolveOpenContainerBagIndex(const WorldSession& session,
                                 const std::uint64_t container_guid) {
  if (container_guid == 0) return -1;
  if (container_guid == session.objects().GetActivePlayerGuid().GetRawValue()) return 0;
  const auto slot = session.inventory_replica().FindSlotByGuid(container_guid);
  if (slot >= InventorySlots::kBagSlotsStart && slot < InventorySlots::kBagSlotsEnd) {
    return slot - InventorySlots::kBagSlotsStart + 1;
  }
  if (slot >= InventorySlots::kBankBagStart && slot < InventorySlots::kBankBagEnd) {
    return slot - InventorySlots::kBankBagStart + 5;
  }
  return -1;
}

[[nodiscard]] std::string_view StripWhoRealmSuffix(const std::string &player_name_filter) {
  const auto separator = player_name_filter.find('-');
  if (separator == std::string::npos) {
    return player_name_filter;
  }

  return std::string_view(player_name_filter.data(), separator);
}

[[nodiscard]] bool WhoUtf8ContainsNoCase(const std::string_view candidate,
                                         const std::string_view query) {
  if (query.empty()) {
    return true;
  }

  const auto candidate_codepoints = core::CountLegacyUtf8Codepoints(candidate);
  const auto query_codepoints = core::CountLegacyUtf8Codepoints(query);
  if (query_codepoints == 0) {
    return true;
  }
  if (candidate_codepoints < query_codepoints) {
    return false;
  }

  const char *cursor = candidate.empty() ? "" : candidate.data();
  for (std::size_t offset = 0; offset + query_codepoints <= candidate_codepoints; ++offset) {
    if (core::SStrCmpUTF8NoCase(cursor, query.data(), query_codepoints) == 0) {
      return true;
    }

    cursor = core::AdvanceLegacyUtf8Codepoints(cursor, 1);
  }

  return false;
}

[[nodiscard]] bool WhoMaskMatches(const std::uint32_t mask, const std::uint8_t value) {
  return value < 32u && (mask & (1u << value)) != 0u;
}

[[nodiscard]] std::string LookupWhoAreaName(const data::dbc::DbcLoader *dbc,
                                            const std::uint32_t area_id) {
  if (dbc == nullptr) {
    return {};
  }

  const auto *entry = dbc->area_table().LookupEntry(area_id);
  return entry != nullptr ? std::string(entry->name) : std::string();
}

[[nodiscard]] std::string LookupWhoRaceName(const data::dbc::DbcLoader *dbc,
                                            const std::uint8_t race_id) {
  if (dbc == nullptr) {
    return {};
  }

  const auto *entry = dbc->chr_races().LookupEntry(race_id);
  return entry != nullptr ? std::string(entry->name) : std::string();
}

[[nodiscard]] std::string LookupWhoClassName(const data::dbc::DbcLoader *dbc,
                                             const std::uint8_t class_id) {
  if (dbc == nullptr) {
    return {};
  }

  const auto *entry = dbc->chr_classes().LookupEntry(class_id);
  return entry != nullptr ? std::string(entry->name) : std::string();
}

[[nodiscard]] std::string UnknownWhoField() {
  return Localization::Get().GetString("UNKNOWN", "UNKNOWN");
}

[[nodiscard]] std::string LookupWhoDisplayAreaName(const data::dbc::DbcLoader *dbc,
                                                   const std::uint32_t area_id) {
  const std::string name = LookupWhoAreaName(dbc, area_id);
  return name.empty() ? UnknownWhoField() : name;
}

[[nodiscard]] std::string LookupWhoDisplayRaceName(const data::dbc::DbcLoader *dbc,
                                                   const WhoEntry &entry) {
  if (dbc != nullptr) {
    if (const auto *race = dbc->chr_races().LookupEntry(entry.race_id); race != nullptr) {
      const auto name = race->DisplayNameForSex(entry.gender);
      if (!name.empty()) {
        return std::string(name);
      }
    }
  }
  return UnknownWhoField();
}

[[nodiscard]] std::string LookupWhoDisplayClassName(const data::dbc::DbcLoader *dbc,
                                                    const WhoEntry &entry) {
  if (dbc != nullptr) {
    if (const auto *player_class = dbc->chr_classes().LookupEntry(entry.class_id);
        player_class != nullptr) {
      const auto name = player_class->DisplayNameForSex(entry.gender);
      if (!name.empty()) {
        return std::string(name);
      }
    }
  }
  return UnknownWhoField();
}

[[nodiscard]] std::string FormatWhoChatEntry(const WhoEntry &entry,
                                             const data::dbc::DbcLoader *dbc) {
  auto &localization = Localization::Get();
  const bool has_guild = !entry.guild_name.empty();
  const std::string format = localization.GetString(
      has_guild ? "WHO_LIST_GUILD_FORMAT" : "WHO_LIST_FORMAT",
      has_guild ? "|Hplayer:%s|h[%s]|h: Level %d %s %s <%s> - %s"
                : "|Hplayer:%s|h[%s]|h: Level %d %s %s - %s");
  std::vector<std::string> args{
      entry.name,
      entry.name,
      std::to_string(entry.level),
      LookupWhoDisplayRaceName(dbc, entry),
      LookupWhoDisplayClassName(dbc, entry),
  };
  if (has_guild) {
    args.push_back(entry.guild_name);
  }
  args.push_back(LookupWhoDisplayAreaName(dbc, entry.zone_id));
  return localization.FormatString(format, args);
}

void DisplayWhoChatEntry(const ObjectManager& objects, const WhoEntry &entry,
                         const data::dbc::DbcLoader *dbc) {
  const std::string message = FormatWhoChatEntry(entry, dbc);
  ChatFrame_DisplayMessage(objects, message.c_str(), ChatDisplayType::kSystem, nullptr, 0, nullptr,
                           nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
}

void DisplayWhoChatSummary(const ObjectManager& objects, const std::uint32_t count) {
  auto &localization = Localization::Get();
  const std::string format =
      localization.GetString("WHO_NUM_RESULTS", "%d |4player:players; total");
  const std::string message = localization.FormatString(format, {std::to_string(count)});
  ChatFrame_DisplayMessage(objects, message.c_str(), ChatDisplayType::kSystem, nullptr, 0, nullptr,
                           nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
}

[[nodiscard]] std::string LookupWhoGuildName(const WorldSession &session,
                                             const CGPlayer_C &player) {
  const auto guild_id = player.GetGuildID();
  if (guild_id == 0) {
    return {};
  }

  const auto *guild_info = session.guild().FindCachedGuildInfo(guild_id);
  if (guild_info == nullptr) {
    return {};
  }

  return guild_info->name;
}

[[nodiscard]] bool WhoGenericTermMatchesRaidMember(const WorldSession &session,
                                                   const CGPlayer_C &member,
                                                   const PlayerNameInfo &name_info,
                                                   const std::string_view term,
                                                   const std::uint32_t current_zone_id) {
  if (WhoUtf8ContainsNoCase(name_info.name, term)) {
    return true;
  }

  const auto *dbc = session.GetDbcLoader();
  if (WhoUtf8ContainsNoCase(LookupWhoRaceName(dbc, member.State().GetRace()), term)) {
    return true;
  }
  if (WhoUtf8ContainsNoCase(LookupWhoClassName(dbc, member.State().GetClass()), term)) {
    return true;
  }
  if (WhoUtf8ContainsNoCase(LookupWhoAreaName(dbc, current_zone_id), term)) {
    return true;
  }

  return WhoUtf8ContainsNoCase(LookupWhoGuildName(session, member), term);
}

[[nodiscard]] bool WhoAllGenericTermsMatchRaidMember(const WorldSession &session,
                                                     const CGPlayer_C &member,
                                                     const PlayerNameInfo &name_info,
                                                     const WhoClientFilterInfo &filter,
                                                     const std::uint32_t current_zone_id) {
  return std::all_of(
      filter.search_terms.begin(), filter.search_terms.end(), [&](const std::string &term) {
        return WhoGenericTermMatchesRaidMember(session, member, name_info, term, current_zone_id);
      });
}

[[nodiscard]] std::string BuildCrossRealmWhoDisplayName(const PlayerNameInfo &name_info) {
  std::string result = name_info.name;
  result.push_back('-');
  for (const char ch : name_info.realm_name) {
    if (ch != ' ') {
      result.push_back(ch);
    }
  }
  return result;
}

std::uint32_t AppendCrossRealmRaidWhoMatches(WorldSession &session) {
  auto &misc = session.misc();
  if (!misc.has_who_client_filter()) {
    return 0;
  }

  const auto *active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr) {
    return 0;
  }

  const auto *active_name_info =
      session.query_cache().GetPlayerName(active_player->GetGuid().GetRawValue());
  if (active_name_info == nullptr) {
    return 0;
  }

  auto &group_system = GroupSystem::Get();
  if (!group_system.IsInRaid()) {
    return 0;
  }

  const auto &filter = misc.who_client_filter();
  const auto name_filter = StripWhoRealmSuffix(filter.player_name);
  const auto current_zone_id = session.objects().GetZoneId();

  std::uint32_t appended_count = 0;
  auto &who_list = misc.mutable_who_list();
  for (const auto &member_snapshot : group_system.GetMembers()) {
    if (who_list.display_count >= 50u) {
      break;
    }
    if (member_snapshot.guid == active_player->GetGuid()) {
      continue;
    }

    const auto *member = session.objects().GetPlayer(member_snapshot.guid);
    if (member == nullptr) {
      continue;
    }
    if (!WhoMaskMatches(filter.race_mask, member->State().GetRace()) ||
        !WhoMaskMatches(filter.class_mask, member->State().GetClass())) {
      continue;
    }

    const auto *member_name_info =
        session.query_cache().GetPlayerName(member_snapshot.guid.GetRawValue());
    if (member_name_info == nullptr) {
      continue;
    }
    if (active_name_info->realm_name == member_name_info->realm_name) {
      continue;
    }
    if (!WhoUtf8ContainsNoCase(member_name_info->name, name_filter)) {
      continue;
    }

    const auto guild_name = LookupWhoGuildName(session, *member);
    if (!filter.guild_name.empty() &&
        (guild_name.empty() || !WhoUtf8ContainsNoCase(guild_name, filter.guild_name))) {
      continue;
    }
    if (member->State().GetLevel() < filter.min_level || member->State().GetLevel() > filter.max_level) {
      continue;
    }
    if (!WhoAllGenericTermsMatchRaidMember(session, *member, *member_name_info, filter,
                                           current_zone_id)) {
      continue;
    }

    WhoEntry entry;
    entry.name = BuildCrossRealmWhoDisplayName(*member_name_info);
    entry.guild_name = guild_name;
    entry.level = member->State().GetLevel();
    entry.class_id = member->State().GetClass();
    entry.race_id = member->State().GetRace();
    entry.gender = member->State().GetGender();
    entry.zone_id = current_zone_id;
    who_list.entries.push_back(std::move(entry));
    ++who_list.display_count;
    ++who_list.match_count;
    ++appended_count;
  }

  return appended_count;
}
struct NotificationRouting {
  std::string_view text;
  int console_color_class = 3;
  int chat_type = 0;
  bool fire_ui_message = true;
  bool route_to_chat_frame = false;
};

NotificationRouting DecodeNotificationRouting(std::string_view raw_text) {
  constexpr std::string_view kDebugActionPrefix = "DEBUGACTION: ";

  if (raw_text.starts_with(kDebugActionPrefix)) {
    raw_text.remove_prefix(kDebugActionPrefix.size());
  }

  NotificationRouting routing;
  routing.text = raw_text;
  if (raw_text.empty() || raw_text.front() != '~') {
    return routing;
  }

  raw_text.remove_prefix(1);
  routing.text = raw_text;

  if (!raw_text.empty() && raw_text.front() == '>') {
    routing.fire_ui_message = false;
    raw_text.remove_prefix(1);
  }

  if (!raw_text.empty() && raw_text.front() == '#') {
    routing.fire_ui_message = false;
    routing.route_to_chat_frame = true;
    raw_text.remove_prefix(1);
  }

  if (raw_text.size() >= 2 && raw_text.front() == '^') {
    routing.chat_type = static_cast<signed char>(raw_text[1]) - '0';
    routing.console_color_class = routing.chat_type;
    raw_text.remove_prefix(2);
  }

  routing.text = raw_text;
  return routing;
}

void DispatchNotificationUiMessage(const NotificationRouting &routing) {
  if (!routing.fire_ui_message || routing.text.empty()) {
    return;
  }

  const std::string message(routing.text);
  if (routing.console_color_class == 3) {
    ui::UIErrorManager::Get().AddErrorMessage(message);
    ui::game::ScriptEventDispatch::Get().FireUiErrorMessage(message);
  } else {
    ui::UIErrorManager::Get().AddInfoMessage(message);
    ui::game::ScriptEventDispatch::Get().FireUiInfoMessage(message);
  }
}

void DisplayDrunkMessage(WorldSession &session, const InebriationThreshold &threshold,
                         const ItemTemplate *item_template) {
  const ObjectGuid actor_guid(threshold.guid);
  const auto *actor = session.objects().GetUnit(actor_guid);
  if (actor == nullptr) {
    return;
  }

  const bool is_self = actor_guid == session.objects().GetLocalPlayerGuid();
  const std::string suffix = std::to_string(threshold.threshold + 1u);
  std::string key;
  std::vector<std::string> args;
  if (item_template == nullptr) {
    key = std::string(is_self ? "DRUNK_MESSAGE_SELF" : "DRUNK_MESSAGE_OTHER") + suffix;
    args.push_back(actor->ResolveRetailName(session));
  } else {
    key = std::string(is_self ? "DRUNK_MESSAGE_ITEM_SELF" : "DRUNK_MESSAGE_ITEM_OTHER") + suffix;
    const std::string item_link = HyperlinkParser::BuildItemLink(
        threshold.item_id, item_template->name,
        static_cast<std::uint32_t>(item_template->quality), 0, 0, 0, 0, 0, 0, 0, 0);
    if (!is_self) {
      args.push_back(actor->ResolveRetailName(session));
    }
    args.push_back(item_link);
  }

  auto &localization = Localization::Get();
  const std::string message = localization.FormatString(localization.GetString(key), args);
  ChatFrame_DisplayMessage(session.objects(), message.c_str(), ChatDisplayType::kSystem, nullptr, 0,
                           nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
}

}

void WorldSession::HandleWeather(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleWeather(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &weather = misc_.weather();
  const auto *const row =
      dbc_ != nullptr ? dbc_->weather().LookupEntry(weather.type) : nullptr;
  const std::int32_t sound_kit_id =
      row != nullptr && row->ambience_id > 0u
          ? static_cast<std::int32_t>(row->ambience_id)
          : kNoWeatherSoundKitId;

  misc_.SetWeatherSoundKitId(sound_kit_id);
  sound_runtime_.SetWeatherSoundKit(sound_kit_id);

  const std::uint32_t effect_type = row != nullptr ? row->effect_type : 0u;
  scene_state_.SetWeather(
      effect_type <= static_cast<std::uint32_t>(WeatherType::Storm)
          ? static_cast<WeatherType>(effect_type)
          : WeatherType::None,
      weather.grade);
  if (weather_presentation_callback_) {
    weather_presentation_callback_(weather.type, weather.grade,
                                   weather.instant_transition == 0u);
  }
}

void WorldSession::HandleBindPointUpdate(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleBindPointUpdate(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandlePlayerBound(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandlePlayerBound(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  (void)sound_runtime_.PlaySoundKit(kDeathbindSoundKitId);

  const auto *const dbc = GetDbcLoader();
  if (dbc == nullptr) {
    return;
  }

  const auto *const area = dbc->area_table().LookupEntry(misc_.player_bound().area_id);
  if (area == nullptr) {
    return;
  }

  const std::string area_name(area->name);
  ui::game::DisplaySystemMessage(kDeathbindSuccessSystemMessage, area_name.c_str());
}

void WorldSession::HandlePlayedTime(const net::wotlk::WorldPacket &pkt) {
  misc_.HandlePlayedTime(pkt.payload.data(), pkt.payload.size());

  const auto &pt = misc_.played_time();
  if (pt.show_in_chat) {
    ui::game::ScriptEventDispatch::Get().FireTimePlayedMsg(pt.total_time, pt.level_time);
  }
}

void WorldSession::HandleWho(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleWho(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  constexpr std::uint32_t kDefaultChatResultLimit = 3;
  const auto server_entry_count = misc_.who_list().entries.size();
  const auto wire_display_count = misc_.who_list().wire_display_count;
  const auto wire_match_count = misc_.who_list().match_count;
  const bool display_server_results_in_chat =
      !social_.WhoResultsToUi() && wire_display_count <= kDefaultChatResultLimit;
  const bool display_cross_realm_results_in_chat =
      !social_.WhoResultsToUi() && wire_match_count <= kDefaultChatResultLimit;

  const auto appended_count = AppendCrossRealmRaidWhoMatches(*this);
  const auto &entries = misc_.who_list().entries;
  if (display_server_results_in_chat) {
    for (std::size_t index = 0; index < server_entry_count; ++index) {
      DisplayWhoChatEntry(objects(), entries[index], GetDbcLoader());
    }
  }
  if (display_cross_realm_results_in_chat) {
    for (std::size_t index = server_entry_count; index < entries.size(); ++index) {
      DisplayWhoChatEntry(objects(), entries[index], GetDbcLoader());
    }
  }
  if (display_server_results_in_chat) {
    DisplayWhoChatSummary(objects(), wire_display_count + appended_count);
  }

  misc_.SortWhoResults(GetDbcLoader());
  if (!display_server_results_in_chat) {
    ui::game::ScriptEventDispatch::Get().FireWhoListUpdate();
  }
}

void WorldSession::HandleMotd(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleMotd(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  auto &knowledge_base = KnowledgeBase::Get();
  knowledge_base.SetSystemMotdLines(misc_.motd().lines);

  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  for (const auto &line : misc_.motd().lines) {

    ChatFrame_DisplayMessage(
        objects(), line.c_str(), ChatDisplayType::kSystem, nullptr,
        static_cast<int>(Language::kUniversal), nullptr, nullptr, nullptr,
        0, 0, 0, 0, 0, nullptr);
  }
  dispatch.FireGlobalEventWithArgs(ui::game::events::KNOWLEDGE_BASE_SYSTEM_MOTD_UPDATED, {});
}

void WorldSession::HandleTutorialFlags(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleTutorialFlags(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleEmote(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleEmote(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &emote = misc_.last_emote();
  if (emote.guid == 0) {
    return;
  }

  auto *unit = map_runtime_.objects().GetMutableUnit(ObjectGuid(emote.guid));
  if (unit == nullptr) {
    return;
  }

  constexpr std::uint8_t kStandStateSleep = 3u;
  if (unit->Animation().GetStandState() == kStandStateSleep) {
    return;
  }

  if (unit->Movement().IsSwimming()) {
    return;
  }

  unit->Animation().PlayEmoteOnUnit(static_cast<std::int32_t>(emote.emote_id));
}

bool WorldSession::TryResolveIncomingTextEmote(PendingIncomingTextEmote &emote,
                                               const bool request_resolution) {
  if (!emote.source_name.empty() || emote.source_guid.IsEmpty()) {
    return true;
  }

  emote.source_name = ResolveImmediateChatParticipantName(emote.source_guid);
  if (!emote.source_name.empty()) {
    if (const auto *unit = map_runtime_.objects().GetUnit(emote.source_guid)) {
      emote.use_alternate_gender_variant = unit->State().GetGender() == 1;
      return true;
    }

    if (emote.source_guid.IsPlayer()) {
      if (const auto *cached_name = query_cache_.GetPlayerName(emote.source_guid.GetRawValue())) {
        emote.use_alternate_gender_variant = cached_name->sex == 1;
        return true;
      }

      if (const auto *name_entry = map_runtime_.objects().GetNameEntry(emote.source_guid)) {
        emote.use_alternate_gender_variant = name_entry->gender == 1;
      }
    }
    return true;
  }

  if (!request_resolution) {
    return false;
  }

  if (emote.source_guid.IsPlayer()) {
    if (pending_chat_name_queries_.insert(emote.source_guid.GetRawValue()).second) {
      (void)query_cache_.RequestNameQuery(emote.source_guid.GetRawValue());
    }
    return false;
  }

  if (emote.source_guid.IsCreatureOrPetOrVehicle() && emote.source_guid.GetEntry() != 0) {
    (void)query_cache_.GetOrRequestCreatureTemplate(emote.source_guid.GetEntry(),
                                                    emote.source_guid.GetRawValue());
    return false;
  }

  return true;
}

void WorldSession::DispatchIncomingTextEmote(const PendingIncomingTextEmote &emote) {
  const auto play_sound = [this, &emote]() {
    if (emote.sound_id != 0) {
      openwow::audio::PlayEmoteSoundForUnit(sound_runtime_, emote.source_guid.GetRawValue(), emote.sound_id, 1.0f);
    }
  };

  if (dbc_ == nullptr) {
    play_sound();
    return;
  }

  const auto *entry = dbc_->emotes_text().LookupEntry(emote.text_emote_id);
  if (entry == nullptr) {
    play_sound();
    return;
  }

  TextEmoteFormatContext context;
  context.source_is_active_player = emote.source_guid == map_runtime_.objects().GetLocalPlayerGuid();
  context.has_target = !emote.target_name.empty();
  context.use_alternate_gender_variant = emote.use_alternate_gender_variant;

  if (context.has_target) {
    const ObjectGuid active_player_guid = map_runtime_.objects().GetLocalPlayerGuid();
    if (!active_player_guid.IsEmpty()) {
      const std::string active_player_name =
          ResolveImmediateChatParticipantName(active_player_guid);
      if (!active_player_name.empty()) {
        context.target_is_active_player =
            openwow::core::SStrCmpI(emote.target_name.c_str(), active_player_name.c_str(),
                                    0x7FFFFFFFu) == 0;
      }
    }
  }

  auto resolve_text_data = [this](std::uint32_t text_data_id) -> std::string_view {
    if (dbc_ == nullptr) return {};
    const auto* td = dbc_->emotes_text_data().LookupEntry(text_data_id);
    return td ? td->text : std::string_view{};
  };

  const std::string message_text =
      FormatTextEmoteText(*entry, context, emote.source_name, emote.target_name,
                          resolve_text_data);
  if (message_text.empty()) {
    play_sound();
    return;
  }

  ChatMessage message;
  message.type = ChatMsg::kTextEmote;
  message.language = Language::kUniversal;
  message.sender_guid = emote.source_guid;
  message.sender_name = emote.source_name;
  message.channel_name = emote.target_name;
  message.message = message_text;
  DispatchIncomingChatMessage(std::move(message));
  play_sound();
}

void WorldSession::HandleTextEmote(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleTextEmote(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &text_emote = misc_.last_text_emote();
  const ObjectGuid source_guid(text_emote.source_guid);
  if (!source_guid.IsEmpty() && social_.IsIgnored(source_guid)) {
    return;
  }

  PendingIncomingTextEmote pending;
  pending.source_guid = source_guid;
  pending.text_emote_id = text_emote.text_emote_id;
  pending.sound_id = text_emote.emote_num;
  pending.target_name = text_emote.target_name;

  if (!TryResolveIncomingTextEmote(pending, true)) {
    pending_text_emotes_.push_back(std::move(pending));
    return;
  }

  if (incoming_chat_delivery_suspended_) {
    pending_text_emotes_.push_back(std::move(pending));
    return;
  }

  DispatchIncomingTextEmote(pending);
}

void WorldSession::HandleNotification(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleNotification(pkt.payload.data(), pkt.payload.size());

  const NotificationRouting routing = DecodeNotificationRouting(misc_.last_notification());
  DispatchNotificationUiMessage(routing);

  std::string notification_text(routing.text);
  core::ida::ConsoleLogColored("%s", routing.console_color_class, notification_text.c_str());

  if (routing.route_to_chat_frame) {
    ChatFrame_DisplayMessage(objects(), notification_text.c_str(), routing.chat_type, nullptr, 0, nullptr,
                             nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
  }
}

constexpr int kZoneExploredMessage = 0x14d;
constexpr int kZoneExploredXpMessage = 0x14e;

void WorldSession::HandleExplorationExperience(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleExplorationExperience(pkt.payload.data(), pkt.payload.size()))
    return;

  const auto &exploration = misc_.last_exploration();
  const auto *dbc = GetDbcLoader();
  if (dbc == nullptr)
    return;

  const auto *entry = dbc->area_table().LookupEntry(exploration.area_id);
  if (entry == nullptr)
    return;

  const std::string_view area_name = entry->name;
  if (area_name.empty())
    return;

  const std::string area_name_str(area_name);
  ui::game::DisplaySystemMessage(kZoneExploredMessage, area_name_str.c_str());
  if (exploration.experience > 0) {
    ui::game::DisplaySystemMessage(kZoneExploredXpMessage, area_name_str.c_str(),
                                   exploration.experience);
  }
}

void WorldSession::HandleEquipmentSetList(const net::wotlk::WorldPacket &pkt) {
  auto sets = equipment_protocol::decode_list(pkt.payload);
  if (!sets.has_value()) {
    return;
  }

  equipment_.apply_list(std::move(*sets));
}

namespace {

constexpr std::uint32_t kCorpsePlayerFlagsGhost = 0x10u;

}

void WorldSession::HandleDeathReleaseLoc(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleDeathReleaseLoc(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto *const player = objects().GetLocalPlayerTyped();
  const bool store_cleared_position =
      player == nullptr ||
      (player->GetUInt32(PLAYER_FLAGS) & kCorpsePlayerFlagsGhost) != 0u;

  const auto &loc = misc_.death_release_loc();
  SetDeathReleasePosition(
      objects(),
      store_cleared_position
          ? DeathReleasePosition{kNoDeathReleaseMapId, 0.0f, 0.0f, 0.0f}
          : DeathReleasePosition{static_cast<int>(loc.map_id), loc.x, loc.y,
                                 loc.z});

  if (death_callbacks_.on_death_release_loc) {
    death_callbacks_.on_death_release_loc();
  }
}

void WorldSession::HandleCorpseReclaimDelay(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleCorpseReclaimDelay(pkt.payload.data(), pkt.payload.size());
  if (death_callbacks_.on_corpse_reclaim_delay) {
    death_callbacks_.on_corpse_reclaim_delay();
  }
}

void WorldSession::HandleSetProficiency(const net::wotlk::WorldPacket &pkt) {
  session_.HandleSetProficiency(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleStandStateUpdate(const net::wotlk::WorldPacket &pkt) {
  if (!session_.HandleStandStateUpdate(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  auto *const player = map_runtime_.objects().GetMutablePlayer(map_runtime_.objects().GetLocalPlayerGuid());
  if (player == nullptr) {
    return;
  }

  player->Animation().ApplyRequestedStandState(*this, session_.stand_state().state);
}

void WorldSession::HandleUpdateComboPoints(const net::wotlk::WorldPacket &pkt) {
  if (!session_.HandleUpdateComboPoints(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  auto *const player = map_runtime_.objects().GetMutablePlayer(map_runtime_.objects().GetLocalPlayerGuid());
  if (player == nullptr) {
    return;
  }

  const auto &combo = session_.combo_points();
  player->SetComboPoints(combo.points);
  player->Casts().SetComboTarget(combo.target);

  ui::game::GameUI_UpdateComboPoints(*this, combo.target.GetRawValue(), combo.points);
}

void WorldSession::HandlePlaySound(const net::wotlk::WorldPacket &pkt) {
  if (!session_.HandlePlaySound(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  (void)sound_runtime_.PlaySoundKit(session_.last_sound().sound_id);
}

void WorldSession::HandlePhaseShift(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  if (!reader.ReadU32(phase_shift_.phase_mask)) {
    return;
  }

  if (world_states_.SetWorldStateUiFilterMask(phase_shift_.phase_mask)) {
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::UPDATE_WORLD_STATES);
  }
}

void WorldSession::HandleGossipComplete(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;

  ui::game::CloseGossipInteraction(*this);
}

void WorldSession::HandleGossipPoi(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  if (!reader.ReadU32(last_poi_.flags) || !reader.ReadFloat(last_poi_.x) ||
      !reader.ReadFloat(last_poi_.y) || !reader.ReadU32(last_poi_.icon) ||
      !reader.ReadU32(last_poi_.importance) || !reader.ReadCString(last_poi_.name)) {
    return;
  }

  if (minimap_ != nullptr) {
    minimap_->SetGossipPointOfInterest(last_poi_.x, last_poi_.y,
                                       last_poi_.icon, last_poi_.name);
  }
}

void WorldSession::ResetAndRequeryCorpsePosition() {

  corpse_query_ = CorpseQueryResult{};
  RefreshActivePlayerCorpseMinimapMarker();
  NotifyCorpsePositionCleared();

  const auto *const player = objects().GetLocalPlayerTyped();
  if (player == nullptr ||
      (player->GetUInt32(PLAYER_FLAGS) & kCorpsePlayerFlagsGhost) == 0u ||
      IsActiveArenaBattlefield()) {
    return;
  }

  net::wotlk::WorldPacket packet(net::wotlk::Opcode::MSG_CORPSE_QUERY);
  Send(packet);
}

void WorldSession::RefreshActivePlayerCorpseMinimapMarker() {

  const bool on_current_map =
      corpse_query_.found &&
      corpse_query_.map_id == static_cast<std::int32_t>(objects().GetMapId());
  if (on_current_map) {
    Minimap_SetCorpseMarker(corpse_query_.x, corpse_query_.y);
  } else {
    Minimap_SetCorpseMarker(0.0f, 0.0f);
  }
}

void WorldSession::NotifyCorpsePositionCleared() {
  if (death_callbacks_.on_corpse_position_cleared) {
    death_callbacks_.on_corpse_position_cleared();
  }
}

void WorldSession::HandleCorpseQuery(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  std::uint8_t found = 0;
  if (!reader.ReadU8(found)) {
    return;
  }

  if (found == 0) {

    corpse_query_ = CorpseQueryResult{};
    RefreshActivePlayerCorpseMinimapMarker();

    NotifyCorpsePositionCleared();
    return;
  }

  CorpseQueryResult reply{};
  reply.found = true;
  if (!reader.ReadI32(reply.map_id) || !reader.ReadFloat(reply.x) ||
      !reader.ReadFloat(reply.y) || !reader.ReadFloat(reply.z) ||
      !reader.ReadI32(reply.corpse_map_id) ||
      !reader.ReadU32(reply.transport_counter)) {
    return;
  }

  const auto *const ghost_player = objects().GetLocalPlayerTyped();
  if (ghost_player == nullptr ||
      (ghost_player->GetUInt32(PLAYER_FLAGS) & kCorpsePlayerFlagsGhost) == 0u) {
    return;
  }

  corpse_query_ = reply;
  RefreshActivePlayerCorpseMinimapMarker();
}

void WorldSession::HandleRandomRoll(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  if (!reader.ReadU32(last_roll_.min_val) || !reader.ReadU32(last_roll_.max_val) ||
      !reader.ReadU32(last_roll_.result) || !reader.ReadU64(last_roll_.roller_guid)) {
    return;
  }

  const auto &roll = last_roll_;
  QueryCache *const cache = &query_cache_;
  const auto owner = lifetime_token();
  const auto *cached_name = query_cache_.GetOrRequestPlayerName(
      roll.roller_guid,
      QueryCache::QueryRequestOptions{
          .dedupe_callbacks = false,
       .callback = [owner, this, cache, roller_guid = roll.roller_guid,
                        min_val = roll.min_val, max_val = roll.max_val,
                        result = roll.result](const bool success) {
            if (owner.expired()) {
              return;
            }
            if (success) {
              FormatRandomRollResult(this->objects(), *cache, roller_guid, min_val, max_val, result);
            }
          },
      });
  if (cached_name != nullptr) {
    FormatRandomRollResult(objects(), query_cache_, roll.roller_guid, roll.min_val,
                           roll.max_val, roll.result);
  }
}

void WorldSession::HandleResurrectRequest(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  if (!reader.ReadU64(resurrect_request_.caster_guid)) {
    return;
  }
  std::uint32_t name_length = 0;
  if (!reader.ReadU32(name_length)) {
    return;
  }
  resurrect_request_.caster_name.clear();
  resurrect_request_.caster_name.resize(name_length);
  if (name_length > 0 && !reader.ReadBytes(
                             reinterpret_cast<std::uint8_t *>(
                                 resurrect_request_.caster_name.data()),
                             name_length)) {
    return;
  }
  std::uint8_t has_sickness = 0;
  std::uint8_t has_timer = 0;
  if (!reader.ReadU8(has_sickness) || !reader.ReadU8(has_timer)) {
    return;
  }
  resurrect_request_.has_sickness = has_sickness != 0;
  resurrect_request_.has_timer = has_timer != 0;
  if (death_callbacks_.on_resurrect_request) {
    death_callbacks_.on_resurrect_request();
  }
}

void WorldSession::HandleShowBank(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  if (!reader.ReadU64(bank_npc_guid_)) {
    return;
  }

  ui::game::SetBankInteractionTarget(*this, ObjectGuid(bank_npc_guid_));
}

void WorldSession::HandleRealmSplit(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  if (!reader.ReadU32(realm_split_.state) ||
      !reader.ReadCString(realm_split_.split_date)) {
    return;
  }
}

void WorldSession::HandlePlayerVehicleData(const net::wotlk::WorldPacket &pkt) {
  vehicle_.HandlePlayerVehicleData(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleForceSetVehicleRecId(const net::wotlk::WorldPacket &pkt) {
  vehicle_.HandleForceSetVehicleRecId(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleCancelExpectedRideVehicleAura(const net::wotlk::WorldPacket &pkt) {
  vehicle_.HandleCancelExpectedRideVehicleAura(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleDurabilityDamageDeath(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  misc_.HandleDurabilityDamageDeath();
  FormatDurabilityDamageDeath(objects());
}

void WorldSession::HandlePlayMusic(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandlePlayMusic(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  sound_runtime_.InitializePlayMusicRuntime(
      static_cast<std::int32_t>(misc_.last_play_music().sound_kit_id));
}

void WorldSession::HandlePlayObjectSound(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandlePlayObjectSound(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &request = misc_.last_play_object_sound();
  const ObjectGuid source_guid(request.source_guid);
  const auto *const source = map_runtime_.objects().Get(source_guid);
  if (source == nullptr) {
    return;
  }

  if (const auto *const unit = map_runtime_.objects().GetUnit(source_guid);
      unit != nullptr) {
    (void)unit->Sound().PlayServerObjectSound(*unit, request.sound_kit_id);
    return;
  }

  const Position position = source->GetPosition();
  const float emitter[3] = {position.x, position.y, position.z};

  (void)sound_runtime_.PlaySoundKit(request.sound_kit_id, emitter);
}

void WorldSession::HandleGameObjectCustomAnim(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleGameObjectCustomAnim(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &anim = misc_.last_go_custom_anim();
  if (!anim.has_value()) {
    return;
  }

  auto *game_object = map_runtime_.objects().GetMutableGameObject(ObjectGuid(anim->guid));
  if (game_object == nullptr) {
    return;
  }

  game_object->HandleServerCustomAnimation(anim->anim_id);
}

void WorldSession::HandleGameObjectDespawnAnim(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleGameObjectDespawnAnim(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const ObjectGuid guid(misc_.last_go_despawn_guid());
  if (guid.IsEmpty()) {
    return;
  }

  auto *game_object = map_runtime_.objects().GetMutableGameObject(guid);
  if (game_object == nullptr) {
    return;
  }

  game_object->HandleServerDespawnAnimation();
}

void WorldSession::HandleGameObjectResetState(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleGameObjectResetState(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const ObjectGuid guid(misc_.last_go_reset_guid());
  if (guid.IsEmpty()) {
    return;
  }

  auto *game_object = map_runtime_.objects().GetMutableGameObject(guid);
  if (game_object == nullptr) {
    return;
  }

  game_object->HandleResetStatePacket();
}

void WorldSession::HandleGameObjectPageText(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleGameObjectPageText(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  auto guid = misc_.last_go_page_text_guid();
  if (guid == 0) {
    return;
  }

  if (!ToggleOrBeginReadableObjectInteraction(*this, guid)) {
    return;
  }

  LoadCurrentReadableTextPage(*this, true);
}

void WorldSession::HandleAreaTriggerMessage(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleAreaTriggerMessage(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &area_trigger_msg = misc_.last_area_trigger_msg();
  if (!area_trigger_msg.has_value()) {
    return;
  }

  if (pkt.payload.size() < sizeof(std::uint32_t) + area_trigger_msg->length) {
    return;
  }

  if (area_trigger_msg->message.empty()) {
    return;
  }

  ui::UIErrorManager::Get().AddInfoMessage(area_trigger_msg->message);
  ui::game::ScriptEventDispatch::Get().FireUiInfoMessage(
      area_trigger_msg->message);
}

void WorldSession::HandleZoneUnderAttack(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleZoneUnderAttack(pkt.payload.data(), pkt.payload.size()))
    return;

  const std::uint32_t zone_id = misc_.last_zone_under_attack();
  if (zone_id == 0)
    return;

  const auto *dbc = GetDbcLoader();
  if (!dbc)
    return;

  const auto *entry = dbc->area_table().LookupEntry(zone_id);
  if (!entry)
    return;

  const std::string_view zone_name = entry->name;
  if (zone_name.empty())
    return;

  const std::string format = Localization::Get().GetString("ZONE_UNDER_ATTACK");
  char buf[3000];
  core::FormatLocalized(buf, sizeof(buf), format.c_str(),
                        std::string(zone_name).c_str());

  ChatFrame_DisplayMessage(objects(), buf, ChatDisplayType::kZoneUnderAttack,
                           nullptr, 0, nullptr, nullptr, nullptr,
                           0, 0, 0, 0, 0, nullptr);
}

void WorldSession::HandleForcedDeathUpdate(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  misc_.HandleForcedDeathUpdate();

  RefreshActivePlayerReleaseTimerMode();
  EvaluateActivePlayerLifeLevel(true);
}

void WorldSession::HandlePreResurrect(const net::wotlk::WorldPacket &pkt) {
  misc_.HandlePreResurrect(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleFeignDeathResisted(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  ui::game::DisplaySystemMessage(kFeignDeathResistedSystemMessage);
}

void WorldSession::HandleCameraShake(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleCameraShake(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &shake = misc_.last_camera_shake();
  const auto *active_player = map_runtime_.objects().GetActivePlayer();
  if (!shake.has_value() || active_player == nullptr) {
    return;
  }

  const Position origin_position = active_player->GetPosition();
  const float origin[3] = {origin_position.x, origin_position.y, origin_position.z};
  if (world_camera_ != nullptr) {
    world_camera_->TriggerSpellEffectCameraShakes(
        shake->effect_id, {origin[0], origin[1], origin[2]});
  }

  if (shake->sound_id != 0u) {
    (void)sound_runtime_.PlaySoundKit(shake->sound_id, origin);
  }
}

void WorldSession::HandleOverrideLight(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleOverrideLight(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &override_light = misc_.last_override_light();
  if (!override_light.has_value()) {
    return;
  }

  DayNight_BeginOverrideFadeOut(override_light->env_light,
                                override_light->transition_ms);
  if (override_light->override_light != 0u) {
    (void)DayNight_TransitionLight(
        dbc_, override_light->env_light, override_light->override_light,
        override_light->transition_ms);
  }
}

void WorldSession::HandleSetForcedReactions(const net::wotlk::WorldPacket &pkt) {
  auto &reputation_info = ReputationInfo::Get();
  PrimeReputationInfo(reputation_info, dbc_, map_runtime_.objects());
  if (!reputation_info.HandleSetForcedReactions(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  RefreshActivePlayerFactionDependentState();
}

void WorldSession::HandleMirrorImageData(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleMirrorImageData(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleMountResult(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleMountResult(pkt.payload.data(), pkt.payload.size())) {
    return;
  }
  const auto result = misc_.mount_result();
  if (result != kMountResultSuccessValue &&
      result < std::size(kMountResultMessages)) {
    ui::game::DisplaySystemMessage(kMountResultMessages[result]);
  }
}

void WorldSession::HandleMountSpecialAnim(const net::wotlk::WorldPacket &pkt) {

  if (!misc_.HandleMountSpecialAnim(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto guid = misc_.mount_special_guid();
  if (guid == 0 ||
      guid == map_runtime_.objects().GetLocalPlayerGuid().GetRawValue()) {
    return;
  }
  auto *const unit = map_runtime_.objects().GetMutableUnit(ObjectGuid(guid));
  if (unit == nullptr) {
    return;
  }
  unit->Animation().PlayEmoteAnimation(94, false);
}

void WorldSession::HandleFishEscaped(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  misc_.HandleFishEscaped();
}

void WorldSession::HandleFishNotHooked(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  misc_.HandleFishNotHooked();
}

void WorldSession::HandleBinderConfirm(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleBinderConfirm(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto binder_guid = misc_.binder_confirm_guid();
  const auto *npc = objects().GetUnit(ObjectGuid(binder_guid));
  if (npc == nullptr) {
    return;
  }

  const auto *player = objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return;
  }
  const float ddx = npc->GetX() - player->GetX();
  const float ddy = npc->GetY() - player->GetY();
  const float ddz = npc->GetZ() - player->GetZ();
  const float distance_sq = ddx * ddx + ddy * ddy + ddz * ddz;
  const float threshold = npc->State().GetBoundingRadius() + 4.0f;
  if (distance_sq > threshold * threshold) {
    return;
  }

  std::string inn_name;
  if (const auto *dbc = GetDbcLoader(); dbc != nullptr) {
    const auto area_id = misc_.bind_point().area_id;
    if (const auto *area = dbc->area_table().LookupEntry(area_id);
        area != nullptr) {
      inn_name = area->name;
    }
  }
  if (inn_name.empty()) {
    inn_name = Localization::Get().GetString("HOME_INN");
  }

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::CONFIRM_BINDER, {ui::game::EventArg{inn_name}});
}

void WorldSession::HandleBindZoneReply(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleBindZoneReply(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandlePlayerBindError(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  misc_.HandlePlayerBindError();
}

void WorldSession::HandleCrossedInebriationThreshold(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleCrossedInebriationThreshold(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& threshold = misc_.last_inebriation();
  if (!threshold.has_value()) {
    return;
  }

  const InebriationThreshold info = *threshold;
  if (info.item_id == 0) {
    DisplayDrunkMessage(*this, info, nullptr);
    return;
  }

  const auto owner = lifetime_token();
  const auto* item_template = query_cache_.GetOrRequestItemTemplate(
      info.item_id,
      QueryCache::QueryRequestOptions{
          .dedupe_callbacks = false,
          .callback = [owner, this, info](const bool success) {
            if (owner.expired()) {
              return;
            }
            const auto* resolved = success
                                       ? query_cache_.GetItemTemplate(info.item_id)
                                       : nullptr;

            DisplayDrunkMessage(*this, info, resolved);
          },
      });
  if (item_template != nullptr) {
    DisplayDrunkMessage(*this, info, item_template);
  }
}

void WorldSession::HandlePlayerSkinned(const net::wotlk::WorldPacket &pkt) {
  misc_.HandlePlayerSkinned(pkt.payload.data(), pkt.payload.size());

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::PLAYER_SKINNED,
      {misc_.player_skinned() != 0 ? 1 : 0});
}

void WorldSession::HandleTalentsInvoluntarilyReset(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleTalentsInvoluntarilyReset(pkt.payload.data(),
                                             pkt.payload.size())) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::TALENTS_INVOLUNTARILY_RESET,
      {misc_.talents_reset_is_pet() != 0});
}

void WorldSession::HandleToggleXpGain(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  misc_.HandleToggleXpGain();
}

void WorldSession::HandleQueryQuestsCompleted(const net::wotlk::WorldPacket &pkt) {
  if (!quests_.HandleQueryQuestsCompleted(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  ui::game::ScriptEventDispatch::Get().FireQuestQueryComplete();
}

void WorldSession::HandleDynamicDropRollResult(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  loot_.HandleDynamicDropRollResult();
}

void WorldSession::HandleNpcWontTalk(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleNpcWontTalk(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleDelayGhostTeleport(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleDelayGhostTeleport(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleClearFarSightImmediate(const net::wotlk::WorldPacket & ) {
  misc_.HandleClearFarSightImmediate();
  if (auto *const local_player = map_runtime_.objects().GetMutablePlayer(map_runtime_.objects().GetLocalPlayerGuid());
      local_player != nullptr) {
    local_player->ClearFarSightFocus(*this);
  }
}

void WorldSession::HandleCorpseMapPositionResponse(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleCorpseMapPositionResponse(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleCorpseNotInInstance(const net::wotlk::WorldPacket & ) {
  misc_.HandleCorpseNotInInstance();

  ui::game::DisplaySystemMessage(kCorpseIsNotInInstanceSystemMessage);
}

void WorldSession::HandleGhosteeGone(const net::wotlk::WorldPacket &pkt) {
  misc_.HandleGhosteeGone(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleOpenContainer(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandleOpenContainer(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto bag_index = ResolveOpenContainerBagIndex(*this, misc_.open_container_guid());
  if (bag_index >= 0) {
    ui::game::ScriptEventDispatch::Get().FireBagOpen(bag_index);
  }
}

void WorldSession::HandlePlayTimeWarning(const net::wotlk::WorldPacket &pkt) {
  if (!misc_.HandlePlayTimeWarning(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto display = ResolvePlayTimeWarningDisplay(*misc_.last_play_time_warning());
  if (!display.has_value()) {
    return;
  }

  if (display->remaining_time.empty()) {
    ui::game::DisplaySystemMessage(display->system_message_id);
  } else {
    ui::game::DisplaySystemMessage(display->system_message_id,
                                   display->remaining_time.c_str());
  }
}

void WorldSession::HandleInvalidPromotionCode(const net::wotlk::WorldPacket & ) {
  misc_.HandleInvalidPromotionCode();

  ui::game::DisplaySystemMessage(kInvalidPromotionCodeSystemMessage);
}

void WorldSession::HandleChangeDifficultyResult(const net::wotlk::WorldPacket &pkt) {
  const auto now = static_cast<std::uint32_t>(std::time(nullptr));
  if (!instance_.HandleChangePlayerDifficultyResult(pkt.payload.data(), pkt.payload.size(), now)) {
    return;
  }

  const auto &result = instance_.change_player_difficulty_result();
  switch (static_cast<PlayerDifficultyChangeResultCode>(result.code)) {
  case PlayerDifficultyChangeResultCode::kCurrentDifficulty:
    if (auto &group_system = GroupSystem::Get();
        group_system.GetPlayerDifficultyIndex() != instance_.player_difficulty()) {
      group_system.SetPlayerDifficultyIndex(instance_.player_difficulty());
      RefreshGameObjectDifficultyVisibility();
    }
    break;
  case PlayerDifficultyChangeResultCode::kCooldownMessage: {
    const auto duration = FormatDifficultyChangeCooldownText(result.value * 1000u);
    ui::game::DisplaySystemMessage(714, duration.c_str());
    break;
  }
  case PlayerDifficultyChangeResultCode::kWorldState:
    ui::game::DisplaySystemMessage(715);
    break;
  case PlayerDifficultyChangeResultCode::kEncounter:
    ui::game::DisplaySystemMessage(716);
    break;
  case PlayerDifficultyChangeResultCode::kCombat:
    ui::game::DisplaySystemMessage(717);
    break;
  case PlayerDifficultyChangeResultCode::kPlayerBusy:
    ui::game::DisplaySystemMessage(718);
    break;
  case PlayerDifficultyChangeResultCode::kCooldownStarted:
    openwow::core::LoadingScreen_InitFont(static_cast<int>(map_runtime_.objects().GetMapId()), false);
    break;
  case PlayerDifficultyChangeResultCode::kAlreadyStarted:
    ui::game::DisplaySystemMessage(719);
    break;
  case PlayerDifficultyChangeResultCode::kChanged:
    openwow::core::LoadingScreen_CleanupResources(sound_runtime_);
    openwow::core::ida::ConsoleAddLine("Changed difficulty successfully",
                                       openwow::core::ida::COLOR_DEFAULT);
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::PLAYER_DIFFICULTY_CHANGED);
    DisplayPlayerDifficultyChangedMessage(instance_.player_difficulty());
    break;
  default:
    break;
  }
}

}

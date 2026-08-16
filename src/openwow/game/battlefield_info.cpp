
#include "openwow/game/battlefield_info.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/barber_shop.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/group_system.h"
#include "openwow/game/lcd_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/world_map_system.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstring>
#include <limits>
#include <utility>

namespace openwow::game {

static BGQueueSlot s_empty_slot;
static ArenaOpponent s_empty_opponent;
static const BFMgrQueueEntry s_empty_bf_mgr_slot;
static ArenaBattlefieldTeamInfo s_empty_battlefield_team_info;

namespace {

constexpr std::size_t kMaxBattlefieldObjectiveStats = 8;
constexpr std::size_t kBattlefieldScoreSortStatOffset = 9;
constexpr std::uint32_t kStrandOfTheAncientsMapId = 566;
constexpr std::uint32_t kBattlefieldVehicleFlag = 0x10000000u;
constexpr std::size_t kMaxBattlefieldVehicles = 40;
constexpr int kBgPlayerJoinedSystemMessageId = 486;
constexpr int kBgPlayerLeftSystemMessageId = 487;
constexpr std::string_view kAllianceFlagToken = "AllianceFlag";
constexpr std::string_view kHordeFlagToken = "HordeFlag";
constexpr std::string_view kArenaClearedSuffix = "cleared";

constexpr std::int32_t SignedI32FromU32Bits(const std::uint32_t value) noexcept {
  if (value <= 0x7FFFFFFFu) {
    return static_cast<std::int32_t>(value);
  }
  return std::numeric_limits<std::int32_t>::min() +
         static_cast<std::int32_t>(value - 0x80000000u);
}

std::uint32_t GetWorldPvpNowSeconds(const WorldSession& session) {
  const auto local_now = std::time(nullptr);
  return static_cast<std::uint32_t>(
      session.world_states().world_state_ui_current_time_seconds(local_now));
}

std::uint32_t GetRemainingTickCount32(const std::uint32_t deadline_tick) {
  if (deadline_tick == 0) {
    return 0;
  }

  const auto now = core::GameClock::GetTickCount32();
  if (static_cast<std::int32_t>(now - deadline_tick) >= 0) {
    return 0;
  }

  return deadline_tick - now;
}

bool ShouldReserveEjectPendingSlot(const std::uint8_t reason) {
  switch (reason) {
  case 1:
  case 2:
  case 4:
  case 5:
  case 7:
  case 8:
  case 9:
  case 10:
  case 11:
    return true;
  default:
    return false;
  }
}

std::string FormatBattlegroundParticipantName(const PlayerNameInfo &name_info) {
  if (name_info.name.empty()) {
    return {};
  }

  if (name_info.realm_name.empty()) {
    return name_info.name;
  }

  return name_info.name + "-" + name_info.realm_name;
}

void DisplayBattlegroundParticipantStatusMessage(const int system_message_id,
                                                 const std::string &player_name) {
  if (player_name.empty()) {
    return;
  }

  switch (system_message_id) {
  case kBgPlayerJoinedSystemMessageId:
    ui::game::DisplaySystemMessage(system_message_id, player_name.c_str(), player_name.c_str());
    break;
  case kBgPlayerLeftSystemMessageId:
    ui::game::DisplaySystemMessage(system_message_id, player_name.c_str());
    break;
  default:
    break;
  }
}

std::int32_t RaceIdToBattlefieldFaction(const std::uint8_t race_id) {
  switch (race_id) {
  case 1:
  case 3:
  case 4:
  case 7:
  case 11:
    return -1;
  case 2:
  case 5:
  case 6:
  case 8:
  case 10:
    return 1;
  default:
    return 0;
  }
}

struct BattlefieldScoreIdentity {
  std::string name;
  std::uint8_t race_id = 0;
  std::uint8_t gender_id = 0;
  std::uint8_t class_id = 0;
  std::int32_t faction = 0;
};

int CompareAsciiNoCase(const std::string_view left, const std::string_view right) {
  const auto count = std::min(left.size(), right.size());
  for (std::size_t i = 0; i < count; ++i) {
    const auto lhs = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(left[i])));
    const auto rhs = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(right[i])));
    if (lhs < rhs) {
      return -1;
    }
    if (lhs > rhs) {
      return 1;
    }
  }

  if (left.size() < right.size()) {
    return -1;
  }
  if (left.size() > right.size()) {
    return 1;
  }
  return 0;
}

void FireArenaLifecycleEvent(const std::string_view token_prefix, const std::size_t slot_index,
                             const std::string_view suffix) {
  std::string unit_token(token_prefix);
  unit_token += std::to_string(slot_index + 1);
  ui::game::ScriptEventDispatch::Get().FireGlobalEventWithArgs(
      ui::game::events::ARENA_OPPONENT_UPDATE, {unit_token, std::string(suffix)});
}

bool IsArenaOpponentVisible(const ObjectManager& objects, const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return false;
  }

  return objects.Get(guid) != nullptr;
}

int CompareNumericDescending(const std::uint32_t left, const std::uint32_t right) {
  if (left == right) {
    return 0;
  }
  return left > right ? -1 : 1;
}

int CompareNumericAscending(const std::int32_t left, const std::int32_t right) {
  if (left == right) {
    return 0;
  }
  return left < right ? -1 : 1;
}

std::uint32_t GetBattlefieldScoreStatValue(const BGScoreEntry &entry, const std::size_t index) {
  if (index >= entry.bg_stats.size()) {
    return 0;
  }
  return entry.bg_stats[index];
}

bool HasBattlefieldScoreIdentity(const BattlefieldScoreIdentity &identity) {
  return !identity.name.empty() || identity.race_id != 0 || identity.gender_id != 0 ||
         identity.class_id != 0;
}

std::optional<BattlefieldScoreIdentity>
ResolveBattlefieldScoreIdentity(const BGScoreEntry &entry,
                                const ObjectManager &objects,
                                const QueryCache *query_cache = nullptr) {
  BattlefieldScoreIdentity identity;
  identity.name = entry.player_name;
  identity.race_id = entry.race_id;
  identity.gender_id = entry.gender_id;
  identity.class_id = entry.class_id;
  identity.faction = entry.faction;

  if (query_cache != nullptr) {
    if (const auto *player_name = query_cache->GetPlayerName(entry.player_guid.GetRawValue());
        player_name != nullptr) {
      if (identity.name.empty()) {
        identity.name = player_name->name;
      }
      if (identity.race_id == 0) {
        identity.race_id = player_name->race;
      }
      if (identity.gender_id == 0) {
        identity.gender_id = player_name->sex;
      }
      if (identity.class_id == 0) {
        identity.class_id = player_name->class_id;
      }
      if (identity.faction == 0) {
        identity.faction = RaceIdToBattlefieldFaction(player_name->race);
      }
    }
  }

  const auto *unit = objects.GetUnit(entry.player_guid);
  const auto *name_entry = objects.GetNameEntry(entry.player_guid);
  if (unit == nullptr && name_entry == nullptr) {
    return HasBattlefieldScoreIdentity(identity) ? std::optional<BattlefieldScoreIdentity>(identity)
                                                 : std::nullopt;
  }

  if (unit != nullptr) {

    const auto live_name = unit->GetName();
    if (!live_name.empty()) {
      identity.name = live_name;
    }
    identity.race_id = unit->State().GetRace();
    identity.gender_id = unit->State().GetGender();
    identity.class_id = unit->State().GetClass();
    identity.faction = RaceIdToBattlefieldFaction(unit->State().GetRace());
  }

  if (name_entry != nullptr) {
    if (identity.name.empty()) {
      identity.name = name_entry->name;
    }
    if (identity.race_id == 0) {
      identity.race_id = name_entry->race;
    }
    if (identity.gender_id == 0) {
      identity.gender_id = name_entry->gender;
    }
    if (identity.class_id == 0) {
      identity.class_id = name_entry->class_id;
    }
    if (identity.faction == 0) {
      identity.faction = RaceIdToBattlefieldFaction(name_entry->race);
    }
  }

  return identity;
}

void RefreshBattlefieldScoreIdentity(BGScoreEntry &entry, const QueryCache &query_cache,
                                     const ObjectManager &objects, const bool is_arena) {
  if (const auto resolved = ResolveBattlefieldScoreIdentity(entry, objects, &query_cache);
      resolved.has_value()) {
    entry.player_name = resolved->name;
    entry.race_id = resolved->race_id;
    entry.gender_id = resolved->gender_id;
    entry.class_id = resolved->class_id;
    if (!is_arena && entry.race_id != 0) {
      entry.faction = RaceIdToBattlefieldFaction(entry.race_id);
    }
  }
}

std::uint32_t GetBattlefieldFlagSlotCount(const std::array<BGFlagPosition, 2> &flag_positions) {
  if (!flag_positions[1].guid.IsEmpty()) {
    return 2;
  }

  return flag_positions[0].guid.IsEmpty() ? 0u : 1u;
}

std::optional<std::uint32_t> GetBattlefieldTeamIndex(const std::uint8_t race_id) {
  switch (RaceIdToBattlefieldFaction(race_id)) {
  case -1:
    return 0u;
  case 1:
    return 1u;
  default:
    return std::nullopt;
  }
}

bool HasBattlefieldPlayerGuid(const std::vector<BGPlayerPosition> &player_positions,
                              const ObjectGuid guid) {
  for (const auto &position : player_positions) {
    if (position.guid == guid) {
      return true;
    }
  }

  return false;
}

bool ProjectBattlefieldWorldPosition(
    const openwow::ui::WorldMapSystem* world_map,
    const std::uint32_t map_id, const float world_x,
    const float world_y, float &map_x, float &map_y) {
  map_x = 0.0f;
  map_y = 0.0f;
  if (map_id == 0 || world_map == nullptr) {
    return false;
  }

  const auto projection =
      world_map->LegacyWorldToMapCoords(map_id, world_x, world_y);
  map_x = projection.x;
  map_y = projection.y;
  return projection.has_projection;
}

bool IsBattlegroundStatEntry(const openwow::data::dbc::WorldStateUIEntry &entry,
                             const std::int32_t map_id) {
  return entry.type == 2 && (entry.map_id == -1 || entry.map_id == map_id);
}

bool HasDirectGroupMemberGuid(const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return false;
  }

  auto &group_system = GroupSystem::Get();
  if (group_system.GetMemberByGuid(guid.GetRawValue()) != nullptr) {
    return true;
  }

  return group_system.GetMember(guid).has_value();
}

bool IsHolidayBattlegroundEntry(
    const openwow::data::dbc::BattlemasterListEntry &entry,
    const std::function<bool(std::uint32_t)> &is_holiday_active) {
  return entry.holiday_world_state != 0 && is_holiday_active(entry.holiday_world_state);
}

bool IsHiddenBattlefieldPlayerGuid(const ObjectManager& objects,
                                   const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return false;
  }

  if (guid == objects.GetActivePlayerGuid()) {
    return true;
  }

  return HasDirectGroupMemberGuid(guid);
}

bool IsBattlefieldVehicleUnit(const openwow::data::dbc::DbcLoader *dbc, const CGUnit_C &unit) {
  if (dbc == nullptr) {
    return false;
  }

  const auto &movement = unit.GetMovementUpdate();
  if (!movement.HasUpdateFlag(kUpdateFlagVehicle) || movement.vehicle_id == 0) {
    return false;
  }

  const auto *vehicle_entry = dbc->vehicle().LookupEntry(movement.vehicle_id);
  return vehicle_entry != nullptr && (vehicle_entry->flags & kBattlefieldVehicleFlag) != 0;
}

}

BattlefieldInfo &BattlefieldInfo::Get() {
  static BattlefieldInfo instance;
  return instance;
}

bool BattlefieldInfo::HandleBattlefieldStatus(WorldSession &session,
                                              const std::uint8_t *data,
                                              std::size_t len) {
  const auto &objects = session.objects();
  PacketReader r(data, len);

  std::uint32_t slot_index;
  if (!r.ReadU32(slot_index)) {
    return false;
  }
  if (slot_index >= kMaxBGQueueSlots) {
    return true;
  }

  std::uint64_t queue_descriptor = 0;
  if (!r.ReadU64(queue_descriptor)) {
    return false;
  }

  auto decoded_slot = queue_slots_[slot_index];
  decoded_slot.bg_confirm_guid = ObjectGuid();
  decoded_slot.bg_instance_guid = ObjectGuid(queue_descriptor);

  if (queue_descriptor == 0) {
    decoded_slot.status = 0;
    decoded_slot.arena_type = 0;
    decoded_slot.is_rated = 0;
    decoded_slot.map_id = 0;
    decoded_slot.expire_time = 0;
    decoded_slot.avg_wait = 0;
    decoded_slot.time_in_queue = 0;
    queue_slots_[slot_index] = decoded_slot;

    if (active_slot_ == static_cast<std::int32_t>(slot_index)) {
      active_slot_ = -1;
      num_positions_ = 0;
      battlefield_instance_expire_tick_ = 0;
      battlefield_instance_start_tick_ = 0;
      bg_stats_columns_.clear();
      bg_stats_count_ = 0;
      active_bg_map_id_ = 0;
      active_bg_type_ = 0;
      LCD_OnBattlefieldStatusCleared();
    }

    ui::game::ScriptEventDispatch::Get().FireEventArgs(
        ui::game::events::UPDATE_BATTLEFIELD_STATUS,
        {static_cast<int>(slot_index + 1)});
    return true;
  }

  decoded_slot.client_instance = static_cast<std::uint32_t>(queue_descriptor >> 16);
  decoded_slot.bg_type_id = static_cast<std::uint32_t>(
      static_cast<std::uint8_t>(queue_descriptor) & 0x7Fu);

  std::uint8_t arena_type_raw, is_rated_raw;
  if (!r.ReadU8(arena_type_raw)) {
    return false;
  }
  if (!r.ReadU8(is_rated_raw)) {
    return false;
  }
  decoded_slot.arena_type = arena_type_raw;
  decoded_slot.is_rated = is_rated_raw;

  std::uint32_t client_instance_id = 0;
  if (!r.ReadU32(client_instance_id)) {
    return false;
  }
  decoded_slot.map_id = client_instance_id;

  std::uint8_t is_registered = 0;
  if (!r.ReadU8(is_registered)) {
    return false;
  }
  decoded_slot.is_registered = (is_registered != 0);

  std::uint32_t status = 0;
  if (!r.ReadU32(status)) {
    return false;
  }
  decoded_slot.status = status;

  const auto now_tick = core::GameClock::GetTickCount32();
  std::uint32_t decoded_active_map_id = active_bg_map_id_;
  std::uint32_t decoded_instance_expire_tick = 0;
  std::uint32_t decoded_instance_start_tick = 0;
  std::uint8_t decoded_arena_faction = battlefield_arena_faction_;

  if (status == 2) {
    if (!r.ReadU32(decoded_slot.confirm_time)) {
      return false;
    }
    std::uint64_t confirm_guid_raw;
    if (!r.ReadU64(confirm_guid_raw)) {
      return false;
    }
    decoded_slot.bg_confirm_guid = ObjectGuid(confirm_guid_raw);

    std::uint32_t expire_timeout = 0;
    if (!r.ReadU32(expire_timeout)) {
      return false;
    }
    decoded_slot.expire_time = expire_timeout != 0 ? now_tick + expire_timeout : 0;
  } else {
    decoded_slot.expire_time = 0;
  }

  if (status == 3) {
    if (!r.ReadU32(decoded_active_map_id)) {
      return false;
    }
    std::uint64_t active_guid_raw;
    if (!r.ReadU64(active_guid_raw)) {
      return false;
    }
    decoded_slot.bg_confirm_guid = ObjectGuid(active_guid_raw);

    std::uint32_t end_timeout = 0;
    if (!r.ReadU32(end_timeout)) {
      return false;
    }
    decoded_instance_expire_tick = end_timeout != 0 ? now_tick + end_timeout : 0;

    std::uint32_t elapsed_runtime = 0;
    if (!r.ReadU32(elapsed_runtime)) {
      return false;
    }
    decoded_instance_start_tick = elapsed_runtime != 0 ? now_tick - elapsed_runtime : 0;

    std::uint8_t arena_flag = 0;
    if (!r.ReadU8(arena_flag)) {
      return false;
    }
    decoded_arena_faction = arena_flag;
  }

  if (status == 1) {
    if (!r.ReadU32(decoded_slot.avg_wait)) {
      return false;
    }
    std::uint32_t time_raw;
    if (!r.ReadU32(time_raw)) {
      return false;
    }
    decoded_slot.time_in_queue = now_tick - time_raw;
  } else {
    decoded_slot.avg_wait = 0;
    decoded_slot.time_in_queue = 0;
  }

  queue_slots_[slot_index] = decoded_slot;
  battlefield_instance_expire_tick_ = decoded_instance_expire_tick;
  battlefield_instance_start_tick_ = decoded_instance_start_tick;

  if (status == 3) {
    active_slot_ = static_cast<std::int32_t>(slot_index);
    active_bg_map_id_ = decoded_active_map_id;
    battlefield_arena_faction_ = decoded_arena_faction;
    active_bg_type_ = 0;
    if (dbc_loader_ != nullptr) {
      if (const auto *map_entry = dbc_loader_->map().LookupEntry(active_bg_map_id_);
          map_entry != nullptr) {
        active_bg_type_ = map_entry->map_type;
      }
      RefreshBattlegroundStatLayout(*dbc_loader_);
    } else {
      bg_stats_columns_.clear();
      bg_stats_count_ = 0;
    }
    faction_filter_ = -1;
    UpdateScoreUI(objects);
    (void)BarberShop::Get().Cancel(session);
    LCD_OnBattlefieldStatus(objects);
  } else if (active_slot_ == static_cast<std::int32_t>(slot_index)) {
    active_slot_ = -1;
  }

  if (status == 2) {
    TutorialSystem::Instance().TriggerTutorial(0x30u);
  }
  if (status == 1) {
    TutorialSystem::Instance().TriggerTutorial(0x2Fu);
  }

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::UPDATE_BATTLEFIELD_STATUS,
      {static_cast<int>(slot_index + 1)});
  return true;
}

bool BattlefieldInfo::HandleGroupJoinedBattleground(
    const std::uint8_t *data, std::size_t len, QueryCache &query_cache,
    const std::function<void(std::uint64_t)> &send_name_query) {
  PacketReader r(data, len);
  std::int32_t result = 0;
  if (!r.ReadI32(result)) {
    return false;
  }

  std::uint64_t raw_guid = 0;
  const bool has_guid = result == -12 || result == -11;
  if (has_guid && !r.ReadU64(raw_guid)) {
    return false;
  }
  return ApplyGroupJoinedBattleground(result, raw_guid, has_guid, query_cache,
                                      send_name_query);
}

bool BattlefieldInfo::ApplyGroupJoinedBattleground(
    const std::int32_t result, const std::uint64_t raw_guid, const bool has_guid,
    QueryCache &query_cache,
    const std::function<void(std::uint64_t)> &send_name_query) {

  switch (result) {
  case -15:
    ui::game::DisplaySystemMessage(723);
    return true;
  case -14:
    ui::game::DisplaySystemMessage(722);
    return true;
  case -13:
    ui::game::DisplaySystemMessage(709);
    return true;
  case -12:
  case -11: {
    if (!has_guid) {
      return false;
    }

    if (const auto *cached_name = query_cache.GetPlayerName(raw_guid); cached_name != nullptr) {
      const auto player_name = FormatBattlegroundParticipantName(*cached_name);
      ui::game::DisplaySystemMessage(677, player_name.c_str());
      return true;
    }

    if (query_cache.RequestNameQuery(raw_guid) &&
        !query_cache.HasNameQueryDispatcher() &&
        static_cast<bool>(send_name_query)) {
      send_name_query(raw_guid);
    }
    ui::game::DisplaySystemMessage(676);
    return true;
  }
  case -10:
    ui::game::DisplaySystemMessage(674);
    return true;
  case -9:
    ui::game::DisplaySystemMessage(668);
    return true;
  case -8:
    ui::game::DisplaySystemMessage(484);
    return true;
  case -7:
    ui::game::DisplaySystemMessage(483);
    return true;
  case -6:
    ui::game::DisplaySystemMessage(482);
    return true;
  case -5:
    ui::game::DisplaySystemMessage(481);
    return true;
  case -4:
    ui::game::DisplaySystemMessage(480);
    return true;
  case -3:
    ui::game::DisplaySystemMessage(720);
    return true;
  case -2:
    ui::game::DisplaySystemMessage(476);
    return true;
  case -1:
    return true;
  default:
    break;
  }

  if (result >= 0 && dbc_loader_ != nullptr) {
    if (const auto *entry = dbc_loader_->battlemaster_list().LookupEntry(
            static_cast<std::uint32_t>(result));
        entry != nullptr) {
      const std::string battleground_name(entry->name);
      ui::game::DisplaySystemMessage(477, battleground_name.c_str());
      return true;
    }
  }

  ui::game::DisplaySystemMessage(478);
  return true;
}

bool BattlefieldInfo::HandleBGPlayerPositions(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);

  std::uint32_t ally_count;
  if (!r.ReadU32(ally_count))
    return false;
  constexpr std::size_t kPositionWireSize = sizeof(std::uint64_t) + sizeof(float) * 2u;
  if (ally_count > r.Remaining() / kPositionWireSize) {
    return false;
  }

  std::vector<BGPlayerPosition> decoded_players(ally_count);
  for (std::uint32_t i = 0; i < ally_count; ++i) {
    std::uint64_t guid_raw;
    float x, y;
    if (!r.ReadU64(guid_raw))
      return false;
    if (!r.ReadFloat(x))
      return false;
    if (!r.ReadFloat(y))
      return false;
    decoded_players[i].guid = ObjectGuid(guid_raw);
    decoded_players[i].x = x;
    decoded_players[i].y = y;
  }

  std::uint32_t flag_count;
  if (!r.ReadU32(flag_count))
    return false;
  if (flag_count > r.Remaining() / kPositionWireSize) {
    return false;
  }

  std::array<BGFlagPosition, 2> decoded_flags{};
  if (flag_count != 0) {

    for (std::uint32_t i = flag_count; i > 0; --i) {
      std::uint32_t idx = i - 1;
      std::uint64_t guid_raw;
      float x, y;
      if (!r.ReadU64(guid_raw))
        return false;
      if (!r.ReadFloat(x))
        return false;
      if (!r.ReadFloat(y))
        return false;
      if (idx < 2) {
        decoded_flags[idx].guid = ObjectGuid(guid_raw);
        decoded_flags[idx].x = x;
        decoded_flags[idx].y = y;
      }
    }
  }

  num_positions_ = ally_count;
  player_positions_ = std::move(decoded_players);
  flag_positions_ = decoded_flags;
  return true;
}

std::uint32_t BattlefieldInfo::GetVisiblePlayerPositionCount(
    const ObjectManager &objects) const {
  std::uint32_t visible_count = 0;
  for (const auto &position : player_positions_) {
    if (!IsHiddenBattlefieldPlayerGuid(objects, position.guid)) {
      ++visible_count;
    }
  }
  return visible_count;
}

std::optional<BattlefieldPlayerMapPosition>
BattlefieldInfo::GetVisiblePlayerMapPosition(const ObjectManager &objects,
                                             const std::uint32_t idx) const {
  std::uint32_t visible_index = 0;
  for (const auto &position : player_positions_) {
    if (IsHiddenBattlefieldPlayerGuid(objects, position.guid)) {
      continue;
    }

    if (visible_index == idx) {
      BattlefieldPlayerMapPosition resolved_position;
      resolved_position.guid = position.guid;

      float world_x = position.x;
      float world_y = position.y;
      if (const auto *player = objects.GetPlayer(position.guid)) {
        world_x = player->GetX();
        world_y = player->GetY();
      }

      ProjectBattlefieldWorldPosition(world_map_, active_bg_map_id_, world_x,
                                      world_y, resolved_position.x,
                                      resolved_position.y);
      return resolved_position;
    }

    ++visible_index;
  }

  return std::nullopt;
}

const PlayerNameInfo *BattlefieldInfo::GetBattlefieldPositionName(
    const std::uint64_t raw_guid, QueryCache &query_cache,
    const std::function<void(std::uint64_t)> &send_name_query) {
  if (raw_guid == 0) {
    return nullptr;
  }

  if (const auto *cached_name = query_cache.GetPlayerName(raw_guid); cached_name != nullptr) {
    return cached_name;
  }

  ++pending_player_position_name_callbacks_[raw_guid];
  if (query_cache.RequestNameQuery(raw_guid) &&
      !query_cache.HasNameQueryDispatcher() &&
      static_cast<bool>(send_name_query)) {
    send_name_query(raw_guid);
  }

  return nullptr;
}

std::uint32_t BattlefieldInfo::ConsumePendingBattlefieldPositionNameCallbacks(
    const std::uint64_t raw_guid) {
  if (raw_guid == 0) {
    return 0;
  }

  const auto it = pending_player_position_name_callbacks_.find(raw_guid);
  if (it == pending_player_position_name_callbacks_.end()) {
    return 0;
  }

  const auto callback_count = it->second;
  pending_player_position_name_callbacks_.erase(it);
  return callback_count;
}

void BattlefieldInfo::ClearPlayerPositions() {
  num_positions_ = 0;
  player_positions_.clear();
}

bool BattlefieldInfo::HasActiveBattlefieldInstance() const {
  if (active_slot_ < 0 || static_cast<std::size_t>(active_slot_) >= queue_slots_.size()) {
    return false;
  }

  return queue_slots_[static_cast<std::size_t>(active_slot_)].client_instance != 0;
}

ObjectGuid BattlefieldInfo::GetActiveBattlefieldGuid() const {
  if (active_slot_ < 0 || static_cast<std::size_t>(active_slot_) >= queue_slots_.size()) {
    return {};
  }

  return queue_slots_[static_cast<std::size_t>(active_slot_)].bg_instance_guid;
}

bool BattlefieldInfo::CanRequestPlayerPositions(const std::uint32_t now_tick) const {
  if (next_player_positions_request_tick_ == 0) {
    return true;
  }

  return static_cast<std::int32_t>(now_tick - next_player_positions_request_tick_) >= 0;
}

void BattlefieldInfo::MarkPlayerPositionsRequested(const std::uint32_t now_tick) {
  next_player_positions_request_tick_ = now_tick + 5000;
}

bool BattlefieldInfo::CanRequestScoreData(const std::uint32_t now_tick) const {

  if (next_score_data_request_tick_ == 0) {
    return true;
  }
  if (battlefield_winner_valid_ ||
      active_bg_type_ ==
          static_cast<std::uint32_t>(openwow::data::dbc::MapType::kArena)) {
    return false;
  }
  return static_cast<std::int32_t>(now_tick - next_score_data_request_tick_) >= 0;
}

void BattlefieldInfo::MarkScoreDataRequested(const std::uint32_t now_tick) {
  next_score_data_request_tick_ = now_tick + 5000;
}

bool BattlefieldInfo::HandleBGPlayerJoinLeave(
    const std::uint8_t *data, std::size_t len, const int system_message_id, QueryCache &query_cache,
    const std::function<void(std::uint64_t)> &send_name_query) {
  PacketReader r(data, len);
  std::uint64_t guid_raw = 0;
  if (!r.ReadU64(guid_raw))
    return false;

  if (guid_raw == 0) {
    return true;
  }

  if (const auto *cached_name = query_cache.GetPlayerName(guid_raw)) {
    DisplayBattlegroundParticipantStatusMessage(system_message_id,
                                                FormatBattlegroundParticipantName(*cached_name));
    return true;
  }

  pending_bg_player_status_messages_.push_back({guid_raw, system_message_id});
  if (query_cache.RequestNameQuery(guid_raw) &&
      !query_cache.HasNameQueryDispatcher() &&
      static_cast<bool>(send_name_query)) {
    send_name_query(guid_raw);
  }

  return true;
}

bool BattlefieldInfo::OnBGPlayerStatusNameResolved(const std::uint64_t raw_guid,
                                                   const QueryCache &query_cache) {
  if (raw_guid == 0 || pending_bg_player_status_messages_.empty()) {
    return false;
  }

  std::string player_name;
  if (const auto *cached_name = query_cache.GetPlayerName(raw_guid)) {
    player_name = FormatBattlegroundParticipantName(*cached_name);
  }

  bool resolved_any = false;
  auto it = pending_bg_player_status_messages_.begin();
  while (it != pending_bg_player_status_messages_.end()) {
    if (it->guid != raw_guid) {
      ++it;
      continue;
    }

    if (!player_name.empty()) {
      DisplayBattlegroundParticipantStatusMessage(it->system_message_id, player_name);
      resolved_any = true;
    }

    it = pending_bg_player_status_messages_.erase(it);
  }

  return resolved_any;
}

bool BattlefieldInfo::HandleBfMgrQueueInvite(const ObjectManager &objects,
                                             const std::uint8_t *data,
                                             std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t queue_id = 0;
  std::uint8_t invite_flag = 0;
  std::uint8_t warmup = 0;
  std::uint8_t cleared_afk = 0;
  if (!r.ReadU32(queue_id))
    return false;
  if (!r.ReadU8(invite_flag))
    return false;
  if (!r.ReadU8(warmup))
    return false;
  if (!r.ReadU8(cleared_afk))
    return false;

  ApplyBfMgrQueueInvite(objects, queue_id, invite_flag, warmup, cleared_afk);
  return true;
}

void BattlefieldInfo::ApplyBfMgrQueueInvite(
    const ObjectManager &objects,
    const std::uint32_t queue_id, const std::uint8_t invite_flag,
    const std::uint8_t warmup, const std::uint8_t cleared_afk) {
  if (BFMgrQueueEntry *slot = FindOrReserveBfMgrQueueEntry(queue_id)) {
    slot->queue_id = queue_id;
    slot->state = WorldPvpQueueState::kNone;
  }

  if (cleared_afk != 0) {
    const std::string message = Localization::Get().GetString("CLEARED_AFK", "CLEARED_AFK");
    ChatFrame_DisplayMessage(objects, message.c_str(), ChatDisplayType::kSystem, nullptr, 0, nullptr,
                             nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
  }

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::BATTLEFIELD_MGR_QUEUE_INVITE,
      {static_cast<int>(queue_id), invite_flag != 0, warmup != 0});
}

bool BattlefieldInfo::HandleBfMgrStateChange(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t queue_id = 0;
  std::uint32_t area_id = 0;
  std::uint32_t expiry_time = 0;
  if (!r.ReadU32(queue_id))
    return false;
  if (!r.ReadU32(area_id))
    return false;
  if (!r.ReadU32(expiry_time))
    return false;

  ApplyBfMgrStateChange(queue_id, area_id, expiry_time);
  return true;
}

void BattlefieldInfo::ApplyBfMgrStateChange(
    const std::uint32_t queue_id, const std::uint32_t area_id,
    const std::uint32_t expiry_time) {
  UpsertBfMgrQueueEntry(queue_id, WorldPvpQueueState::kConfirm, area_id, expiry_time);
  ui::game::ScriptEventDispatch::Get().FireEventArgs(ui::game::events::BATTLEFIELD_MGR_STATE_CHANGE,
                                                     {static_cast<int>(queue_id)});
}

bool BattlefieldInfo::HandleBfMgrEntryInvite(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t battle_id = 0;
  std::uint8_t accepted = 0;
  if (!r.ReadU32(battle_id) || !r.ReadU8(accepted)) {
    return false;
  }

  ApplyBfMgrEntryInvite(battle_id, accepted);
  return true;
}

void BattlefieldInfo::ApplyBfMgrEntryInvite(const std::uint32_t battle_id,
                                            const std::uint8_t accepted) {
  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::BATTLEFIELD_MGR_ENTRY_INVITE,
      {static_cast<int>(battle_id), accepted == 1});
}

bool BattlefieldInfo::HandleBfMgrEntered(const ObjectManager &objects,
                                         const std::uint8_t *data,
                                         std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t battlefield_id = 0;
  std::uint32_t area_id = 0;
  std::uint8_t status_flag = 0;
  std::uint8_t secondary_flag = 0;
  std::uint8_t cleared_afk = 0;
  if (!r.ReadU32(battlefield_id)) {
    return false;
  }
  if (!r.ReadU32(area_id)) {
    return false;
  }
  if (!r.ReadU8(status_flag)) {
    return false;
  }
  if (!r.ReadU8(secondary_flag)) {
    return false;
  }
  if (!r.ReadU8(cleared_afk)) {
    return false;
  }

  ApplyBfMgrEntered(objects, battlefield_id, area_id, status_flag, secondary_flag,
                    cleared_afk);
  return true;
}

void BattlefieldInfo::ApplyBfMgrEntered(
    const ObjectManager &objects,
    const std::uint32_t battlefield_id, const std::uint32_t area_id,
    const std::uint8_t status_flag, const std::uint8_t secondary_flag,
    const std::uint8_t cleared_afk) {
  const bool is_current_area = IsCurrentWorldPvpArea(objects, area_id);
  UpsertBfMgrQueueEntry(battlefield_id,
                        status_flag != 0 ? WorldPvpQueueState::kQueued
                                         : WorldPvpQueueState::kNone,
                        area_id, 0);

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::BATTLEFIELD_MGR_ENTERED,
      {static_cast<int>(battlefield_id), status_flag != 0, cleared_afk == 1, is_current_area,
       secondary_flag != 0});
}

bool BattlefieldInfo::HandleBfMgrQueueResponse(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t queue_id = 0;
  std::uint8_t accepted = 0;
  if (!r.ReadU32(queue_id) || !r.ReadU8(accepted)) {
    return false;
  }

  ApplyBfMgrQueueResponse(queue_id, accepted);
  return true;
}

void BattlefieldInfo::ApplyBfMgrQueueResponse(const std::uint32_t queue_id,
                                              const std::uint8_t accepted) {
  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::BATTLEFIELD_MGR_QUEUE_REQUEST_RESPONSE,
      {static_cast<int>(queue_id), accepted != 0});
}

bool BattlefieldInfo::HandleBfMgrEjectPending(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t queue_id = 0;
  std::uint8_t reason = 0;
  std::uint8_t relocate_flag = 0;
  std::uint8_t battleground_flag = 0;
  if (!r.ReadU32(queue_id))
    return false;
  if (!r.ReadU8(reason))
    return false;
  if (!r.ReadU8(relocate_flag))
    return false;
  if (!r.ReadU8(battleground_flag))
    return false;

  ApplyBfMgrEjectPending(queue_id, reason, relocate_flag, battleground_flag);
  return true;
}

void BattlefieldInfo::ApplyBfMgrEjectPending(
    const std::uint32_t queue_id, const std::uint8_t reason,
    const std::uint8_t relocate_flag, const std::uint8_t battleground_flag) {
  if (ShouldReserveEjectPendingSlot(reason)) {
    if (BFMgrQueueEntry *slot = FindOrReserveBfMgrQueueEntry(queue_id)) {
      slot->queue_id = queue_id;
      slot->state = WorldPvpQueueState::kNone;
    }
  }

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::BATTLEFIELD_MGR_EJECT_PENDING,
      {static_cast<int>(queue_id), reason == 8, battleground_flag != 0, relocate_flag == 2,
       reason == 10});
}

bool BattlefieldInfo::HandleBfMgrEjected(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t queue_id = 0;
  std::uint32_t reason = 0;
  if (!r.ReadU32(queue_id) || !r.ReadU32(reason)) {
    return false;
  }

  ApplyBfMgrEjected(queue_id, reason);
  return true;
}

void BattlefieldInfo::ApplyBfMgrEjected(const std::uint32_t queue_id,
                                        const std::uint32_t reason) {
  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::BATTLEFIELD_MGR_EJECTED,
      {static_cast<int>(queue_id), static_cast<int>(reason)});
}

void BattlefieldInfo::UpdateScoreUI(const ObjectManager &objects) {
  if (pending_score_name_queries_ != 0)
    return;

  filtered_score_count_ = 0;
  for (auto &entry : score_entries_) {
    const auto identity = ResolveBattlefieldScoreIdentity(entry, objects);
    if (!score_entries_are_arena_) {
      entry.faction = identity ? identity->faction : 0;
    }

    if (faction_filter_ == -1 || entry.faction == faction_filter_) {
      ++filtered_score_count_;
    }
  }

  std::sort(score_entries_.begin(), score_entries_.end(),
            [this, &objects](const BGScoreEntry &left, const BGScoreEntry &right) {
              return CompareScoreEntries(objects, left, right) < 0;
            });

  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::UPDATE_BATTLEFIELD_SCORE);
}

void BattlefieldInfo::PromoteScoreSortColumn(const ObjectManager &objects,
                                             const std::uint32_t column) {
  if (pending_score_name_queries_ != 0) {
    return;
  }

  auto column_it = std::find(score_sort_columns_.begin(), score_sort_columns_.end(), column);
  if (column_it == score_sort_columns_.end()) {
    UpdateScoreUI(objects);
    return;
  }

  const auto index =
      static_cast<std::size_t>(std::distance(score_sort_columns_.begin(), column_it));
  bool reverse = score_sort_reverse_[index];
  if (index == 0) {
    reverse = !reverse;
  }

  for (std::size_t i = index; i > 0; --i) {
    score_sort_columns_[i] = score_sort_columns_[i - 1];
    score_sort_reverse_[i] = score_sort_reverse_[i - 1];
  }

  score_sort_columns_[0] = column;
  score_sort_reverse_[0] = reverse;
  UpdateScoreUI(objects);
}

void BattlefieldInfo::SetScoreEntries(const ObjectManager &objects,
                                      std::vector<BGScoreEntry> entries,
                                      const bool is_arena) {
  score_entries_ = std::move(entries);
  score_entries_are_arena_ = is_arena;
  pending_score_name_query_guids_.clear();
  pending_score_name_queries_ = 0;
  UpdateScoreUI(objects);
}

void BattlefieldInfo::SetScoreEntries(std::vector<BGScoreEntry> entries, const bool is_arena,
                                      QueryCache &query_cache, const ObjectManager &objects,
                                      const std::function<void(std::uint64_t)> &send_name_query) {
  score_entries_ = std::move(entries);
  score_entries_are_arena_ = is_arena;
  filtered_score_count_ = 0;
  pending_score_name_query_guids_.clear();

  for (auto &entry : score_entries_) {
    RefreshBattlefieldScoreIdentity(entry, query_cache, objects, score_entries_are_arena_);
    if (!entry.player_name.empty() || entry.class_id != 0 || entry.race_id != 0 ||
        entry.gender_id != 0) {
      continue;
    }

    const auto raw_guid = entry.player_guid.GetRawValue();
    if (raw_guid == 0 || !pending_score_name_query_guids_.insert(raw_guid).second) {
      continue;
    }

    if (query_cache.RequestNameQuery(raw_guid) &&
        !query_cache.HasNameQueryDispatcher() &&
        static_cast<bool>(send_name_query)) {
      send_name_query(raw_guid);
    }
  }

  pending_score_name_queries_ =
      static_cast<std::uint32_t>(pending_score_name_query_guids_.size());
  if (pending_score_name_queries_ == 0) {
    UpdateScoreUI(objects);
  }
}

bool BattlefieldInfo::OnScoreNameResolved(const std::uint64_t raw_guid, const QueryCache &query_cache,
                                          const ObjectManager &objects) {
  if (raw_guid == 0) {
    return false;
  }

  const auto it = pending_score_name_query_guids_.find(raw_guid);
  if (it == pending_score_name_query_guids_.end()) {
    return false;
  }

  pending_score_name_query_guids_.erase(it);
  pending_score_name_queries_ =
      static_cast<std::uint32_t>(pending_score_name_query_guids_.size());

  for (auto &entry : score_entries_) {
    if (entry.player_guid.GetRawValue() == raw_guid) {
      RefreshBattlefieldScoreIdentity(entry, query_cache, objects, score_entries_are_arena_);
    }
  }

  if (pending_score_name_queries_ == 0) {
    UpdateScoreUI(objects);
  }

  return true;
}

const BGScoreEntry *BattlefieldInfo::GetDisplayedScoreEntry(const std::size_t index) const {

  return index < score_entries_.size() ? &score_entries_[index] : nullptr;
}

void BattlefieldInfo::RefreshBattlegroundStatLayout(const openwow::data::dbc::DbcLoader &dbc) {
  bg_stats_columns_.clear();
  bg_stats_count_ = 0;

  if (active_bg_map_id_ == 0) {
    return;
  }

  const auto map_id = static_cast<std::int32_t>(active_bg_map_id_);
  bool found_block = false;
  for (const auto &entry : dbc.world_state_ui().entries()) {
    const bool matches = IsBattlegroundStatEntry(entry, map_id);
    if (!found_block) {
      if (!matches) {
        continue;
      }
      found_block = true;
    } else if (!matches) {
      break;
    }

    bg_stats_columns_.push_back(entry.id);
  }

  bg_stats_count_ = static_cast<std::uint32_t>(bg_stats_columns_.size());
}

const openwow::data::dbc::WorldStateUIEntry *
BattlefieldInfo::GetBattlegroundStatInfo(const openwow::data::dbc::DbcLoader &dbc,
                                         const std::size_t index) const {
  if (index >= bg_stats_columns_.size()) {
    return nullptr;
  }

  return dbc.world_state_ui().LookupEntry(bg_stats_columns_[index]);
}

std::uint32_t BattlefieldInfo::GetBattlegroundStatData(const std::size_t player_index,
                                                       const std::size_t stat_index) const {
  if (player_index >= score_entries_.size() || stat_index >= kMaxBattlefieldObjectiveStats) {
    return 0;
  }

  const auto &stats = score_entries_[player_index].bg_stats;
  if (stat_index >= stats.size()) {
    return 0;
  }

  return stats[stat_index];
}

void BattlefieldInfo::SetArenaBattlefieldTeamInfo(const std::size_t index, std::string name,
                                                  std::array<std::uint32_t, 3> raw_values) {
  if (index >= arena_team_info_.size()) {
    return;
  }

  arena_team_info_[index].name = std::move(name);
  arena_team_info_[index].raw_values = raw_values;
}

const ArenaBattlefieldTeamInfo &
BattlefieldInfo::GetArenaBattlefieldTeamInfo(const std::size_t index) const {
  if (index < arena_team_info_.size()) {
    return arena_team_info_[index];
  }

  return s_empty_battlefield_team_info;
}

void BattlefieldInfo::ResetScoreSortOrder() {
  for (std::size_t i = 0; i < score_sort_columns_.size(); ++i) {
    score_sort_columns_[i] = static_cast<std::uint32_t>(i);
    score_sort_reverse_[i] = false;
  }
}

int BattlefieldInfo::CompareScoreEntries(const ObjectManager &objects,
                                         const BGScoreEntry &left,
                                         const BGScoreEntry &right) const {
  const auto left_identity = ResolveBattlefieldScoreIdentity(left, objects);
  const auto right_identity = ResolveBattlefieldScoreIdentity(right, objects);
  if (!left_identity || !right_identity) {
    return 0;
  }

  const auto left_faction = left_identity->faction;
  const auto right_faction = right_identity->faction;
  if (faction_filter_ != -1 && left_faction != right_faction) {
    return left_faction != faction_filter_ ? 1 : -1;
  }

  for (std::size_t i = 0; i < score_sort_columns_.size(); ++i) {
    const auto column = score_sort_columns_[i];
    int result = 0;

    switch (column) {
    case 0:
      result = CompareNumericDescending(left.killing_blows, right.killing_blows);
      break;
    case 1:
      result = CompareNumericDescending(left.deaths, right.deaths);
      break;
    case 2:
      result = CompareNumericDescending(left.bonus_honor, right.bonus_honor);
      break;
    case 3:
      result = CompareAsciiNoCase(left_identity->name, right_identity->name);
      break;
    case 4:
      result = CompareNumericDescending(left_identity->class_id, right_identity->class_id);
      break;
    case 5:
      result = CompareNumericDescending(left.honorable_kills, right.honorable_kills);
      break;
    case 6:
      result = CompareNumericDescending(left.damage_done, right.damage_done);
      break;
    case 7:
      result = CompareNumericDescending(left.healing_done, right.healing_done);
      break;
    case 8:
      result = CompareNumericAscending(left_faction, right_faction);
      break;
    default:
      if (column < kBattlefieldScoreSortStatOffset) {
        continue;
      }

      {
        const auto stat_index = static_cast<std::size_t>(column - kBattlefieldScoreSortStatOffset);
        if (stat_index >= bg_stats_count_) {
          continue;
        }

        result = CompareNumericDescending(GetBattlefieldScoreStatValue(left, stat_index),
                                          GetBattlefieldScoreStatValue(right, stat_index));
      }
      break;
    }

    if (result == 0) {
      continue;
    }

    return score_sort_reverse_[i] ? -result : result;
  }

  return 0;
}

std::size_t BattlefieldInfo::GetArenaOpponentSlotCount() const {
  std::size_t slot = 0;
  for (; slot < kMaxArenaOpponents; ++slot) {
    const auto &opponent = arena_opponents_[slot];
    if (opponent.guid.IsEmpty() && opponent.pet_guid.IsEmpty()) {
      break;
    }
  }
  return slot;
}

void BattlefieldInfo::OnArenaUnitUnseen(const ObjectGuid &guid, const ObjectManager &objects) {
  for (std::size_t i = 0; i < kMaxArenaOpponents; ++i) {
    if (arena_opponents_[i].guid == guid) {
      FireArenaLifecycleEvent("arena", i, "unseen");
      return;
    }
    if (!arena_opponents_[i].guid.IsEmpty()) {
      if (const auto *obj = objects.GetUnit(arena_opponents_[i].guid); obj != nullptr &&
          obj->State().GetPrimaryControlledUnitGUID() == guid) {
        FireArenaLifecycleEvent("arenapet", i, "unseen");
        return;
      }
    }
  }

  for (std::size_t i = 0; i < kMaxArenaOpponents; ++i) {
    if (arena_opponents_[i].pet_guid == guid) {
      FireArenaLifecycleEvent("arenapet", i, "unseen");
      return;
    }
  }
}

void BattlefieldInfo::OnArenaUnitDestroyed(const ObjectGuid &guid, const ObjectManager &objects) {
  for (std::size_t i = 0; i < kMaxArenaOpponents; ++i) {
    if (arena_opponents_[i].guid == guid) {
      FireArenaLifecycleEvent("arena", i, "destroyed");
      return;
    }
    if (!arena_opponents_[i].guid.IsEmpty()) {
      if (const auto *obj = objects.GetUnit(arena_opponents_[i].guid); obj != nullptr &&
          obj->State().GetPrimaryControlledUnitGUID() == guid) {
        FireArenaLifecycleEvent("arenapet", i, "destroyed");
        return;
      }
    }
  }

  for (std::size_t i = 0; i < kMaxArenaOpponents; ++i) {
    if (arena_opponents_[i].pet_guid != guid) {
      continue;
    }

    arena_opponents_[i].pet_guid = ObjectGuid();
    arena_opponents_[i].pet_state.reset();
    FireArenaLifecycleEvent("arenapet", i, "destroyed");
    return;
  }
}

void BattlefieldInfo::SetArenaOpponent(const ObjectManager &objects,
                                       std::size_t slot,
                                       const ArenaOpponent &opp) {
  if (slot >= kMaxArenaOpponents) {
    return;
  }

  const bool guid_changed = arena_opponents_[slot].guid != opp.guid;
  const auto previous_pet_guid = guid_changed ? ObjectGuid() : arena_opponents_[slot].pet_guid;
  if (guid_changed) {
    arena_opponents_[slot] = {};
  }

  auto &tracked = arena_opponents_[slot];
  tracked.guid = opp.guid;
  if (guid_changed || !opp.pet_guid.IsEmpty()) {
    if (tracked.pet_guid != opp.pet_guid) {
      tracked.pet_state.reset();
    }
    tracked.pet_guid = opp.pet_guid;
  }
  if (guid_changed || opp.pet_state.has_value()) {
    tracked.pet_state = opp.pet_state;
  }
  if (guid_changed || opp.vehicle_seat != 0) {
    tracked.vehicle_seat = opp.vehicle_seat;
  }
  if (guid_changed || !opp.aura_spell_ids.empty()) {
    tracked.aura_spell_ids = opp.aura_spell_ids;
  }
  if (guid_changed || opp.pvp_enabled) {
    tracked.pvp_enabled = opp.pvp_enabled;
  }

  FireArenaLifecycleEvent("arena", slot, "seen");
  if (!IsArenaOpponentVisible(objects, opp.guid)) {
    FireArenaLifecycleEvent("arena", slot, "unseen");
  }

  if (!opp.pet_guid.IsEmpty()) {
    if (previous_pet_guid != opp.pet_guid) {
      SetArenaOpponentPet(objects, slot, opp.pet_guid);
    }
  }
}

void BattlefieldInfo::SetArenaOpponentPet(const ObjectManager &objects,
                                          const std::size_t slot,
                                          const ObjectGuid pet_guid) {
  if (slot >= kMaxArenaOpponents || pet_guid.IsEmpty()) {
    return;
  }

  auto& opponent = arena_opponents_[slot];
  if (opponent.pet_guid != pet_guid) {
    opponent.pet_state.reset();
  }
  opponent.pet_guid = pet_guid;
  FireArenaLifecycleEvent("arenapet", slot, "seen");
  if (!IsArenaOpponentVisible(objects, pet_guid)) {
    FireArenaLifecycleEvent("arenapet", slot, "unseen");
  }
}

void BattlefieldInfo::SetArenaOpponentPetState(
    const std::size_t slot, TrackedControlledUnitStateSlice state) {
  if (slot >= kMaxArenaOpponents || state.controlled_unit_guid.IsEmpty()) {
    return;
  }

  auto& opponent = arena_opponents_[slot];
  if (opponent.guid.IsEmpty() || opponent.pet_guid != state.controlled_unit_guid) {
    return;
  }
  state.owner_guid = opponent.guid;
  opponent.pet_state = std::move(state);
}

const ArenaOpponent &BattlefieldInfo::GetArenaOpponent(std::size_t slot) const {
  if (slot < kMaxArenaOpponents)
    return arena_opponents_[slot];
  return s_empty_opponent;
}

std::optional<std::size_t> BattlefieldInfo::FindArenaOpponentSlot(
    const std::uint64_t guid) const {
  if (guid == 0) {
    return std::nullopt;
  }
  for (std::size_t slot = 0; slot < arena_opponents_.size(); ++slot) {
    if (arena_opponents_[slot].guid.GetRawValue() == guid) {
      return slot;
    }
  }
  return std::nullopt;
}

bool BattlefieldInfo::ArenaOpponentHasAura(const std::uint64_t guid,
                                           const std::uint32_t spell_id) const {
  const auto slot = FindArenaOpponentSlot(guid);
  if (!slot.has_value() || spell_id == 0) {
    return false;
  }
  const auto &auras = arena_opponents_[*slot].aura_spell_ids;
  return std::find(auras.begin(), auras.end(), spell_id) != auras.end();
}

bool BattlefieldInfo::GetArenaOpponentPvpFlag(const std::uint64_t guid) const {
  const auto slot = FindArenaOpponentSlot(guid);
  return slot.has_value() && arena_opponents_[*slot].pvp_enabled;
}

std::uint32_t BattlefieldInfo::GetArenaOpponentVehicleSeat(const std::uint64_t guid) const {
  const auto slot = FindArenaOpponentSlot(guid);
  return slot.has_value() ? arena_opponents_[*slot].vehicle_seat : 0;
}

void BattlefieldInfo::SetArenaOpponentAuraSnapshot(
    const std::uint64_t guid, std::vector<std::uint32_t> aura_spell_ids) {
  if (const auto slot = FindArenaOpponentSlot(guid); slot.has_value()) {
    arena_opponents_[*slot].aura_spell_ids = std::move(aura_spell_ids);
  }
}

void BattlefieldInfo::SetArenaOpponentPvpFlag(const std::uint64_t guid,
                                              const bool pvp_enabled) {
  if (const auto slot = FindArenaOpponentSlot(guid); slot.has_value()) {
    arena_opponents_[*slot].pvp_enabled = pvp_enabled;
  }
}

void BattlefieldInfo::SetArenaOpponentVehicleSeat(const std::uint64_t guid,
                                                  const std::uint32_t vehicle_seat) {
  if (const auto slot = FindArenaOpponentSlot(guid); slot.has_value()) {
    arena_opponents_[*slot].vehicle_seat = vehicle_seat;
  }
}

const BGQueueSlot &BattlefieldInfo::GetQueueSlot(std::size_t index) const {
  if (index < kMaxBGQueueSlots)
    return queue_slots_[index];
  return s_empty_slot;
}

std::uint32_t BattlefieldInfo::GetQueueSlotBattlemasterListId(const std::size_t index) const {
  if (index >= kMaxBGQueueSlots) {
    return 0;
  }

  return queue_slots_[index].client_instance;
}

std::uint32_t BattlefieldInfo::GetQueueSlotInstanceId(const std::size_t index) const {
  if (index >= kMaxBGQueueSlots) {
    return 0;
  }

  return queue_slots_[index].map_id;
}

const BFMgrQueueEntry &BattlefieldInfo::GetBfMgrQueueEntry(std::size_t index) const {
  if (index == 0) {
    return bf_mgr_queue_entry_;
  }
  return s_empty_bf_mgr_slot;
}

std::optional<WorldPvpQueueStatus>
BattlefieldInfo::GetWorldPvpQueueStatus(const WorldSession &session,
                                        const std::size_t lua_index) const {
  if (lua_index != 1) {
    return std::nullopt;
  }

  const auto &slot = bf_mgr_queue_entry_;
  WorldPvpQueueStatus status;
  status.state = slot.state;
  status.area_id = slot.area_id;
  status.queue_id = slot.queue_id;
  status.time_left_seconds =
      slot.state == WorldPvpQueueState::kConfirm
          ? static_cast<double>(SignedI32FromU32Bits(
                slot.expiry_time - GetWorldPvpNowSeconds(session)))
          : 0.0;
  return status;
}

bool BattlefieldInfo::GetBattlefieldPosition(std::uint32_t index, float &x, float &y) const {
  if (index >= num_positions_)
    return false;
  x = player_positions_[index].x;
  y = player_positions_[index].y;
  return true;
}

bool BattlefieldInfo::GetBattlefieldFlagPosition(std::uint32_t index, float &x, float &y) const {
  const auto count = GetNumFlags();
  if (index >= count)
    return false;
  x = flag_positions_[index].x;
  y = flag_positions_[index].y;
  return true;
}

bool BattlefieldInfo::GetBattlefieldFlagMapPosition(const ObjectManager &objects,
                                                    std::uint32_t index,
                                                    float &x, float &y) const {
  x = 0.0f;
  y = 0.0f;

  const auto guid = GetFlagGuid(index);
  if (!guid.IsEmpty()) {
    if (const auto *unit = objects.GetUnit(guid)) {
      float unit_map_x = 0.0f;
      float unit_map_y = 0.0f;

      const auto carrier_position = unit->GetPosition();
      if (ProjectBattlefieldWorldPosition(world_map_, active_bg_map_id_,
                                          carrier_position.x, carrier_position.y,
                                          unit_map_x, unit_map_y) &&
          (unit_map_x != 0.0f || unit_map_y != 0.0f)) {
        x = unit_map_x;
        y = unit_map_y;
        return true;
      }
    }
  }

  float world_x = 0.0f;
  float world_y = 0.0f;
  if (!GetBattlefieldFlagPosition(index, world_x, world_y)) {
    return false;
  }

  return ProjectBattlefieldWorldPosition(world_map_, active_bg_map_id_,
                                         world_x, world_y, x, y);
}

std::string_view BattlefieldInfo::GetBattlefieldFlagToken(const ObjectManager &objects,
                                                          std::uint32_t index) const {
  const auto guid = GetFlagGuid(index);
  if (guid.IsEmpty()) {
    return {};
  }

  std::optional<std::uint32_t> team_index;
  if (const auto *unit = objects.GetUnit(guid)) {
    team_index = GetBattlefieldTeamIndex(unit->State().GetRace());
  }

  if (!team_index) {
    const auto *active_player = objects.GetActivePlayer();
    if (!active_player) {
      return {};
    }

    team_index = GetBattlefieldTeamIndex(active_player->State().GetRace());
    if (!team_index) {
      return {};
    }

    if (!HasBattlefieldPlayerGuid(player_positions_, guid)) {
      *team_index = *team_index == 0 ? 1u : 0u;
    }
  }

  if (active_bg_map_id_ == kStrandOfTheAncientsMapId) {
    *team_index = *team_index == 0 ? 1u : 0u;
  }

  return *team_index == 0 ? kAllianceFlagToken : kHordeFlagToken;
}

ObjectGuid BattlefieldInfo::GetFlagGuid(std::uint32_t index) const {
  const auto count = GetNumFlags();
  if (index >= count)
    return ObjectGuid();
  return flag_positions_[index].guid;
}

std::uint32_t BattlefieldInfo::GetNumFlags() const {
  return GetBattlefieldFlagSlotCount(flag_positions_);
}

const BGPlayerPosition *BattlefieldInfo::GetPlayerPosition(std::uint32_t idx) const {
  if (idx >= num_positions_ || idx >= player_positions_.size())
    return nullptr;
  return &player_positions_[idx];
}

const BGFlagPosition *BattlefieldInfo::GetFlagPosition(std::uint32_t idx) const {
  if (idx >= GetNumFlags())
    return nullptr;
  return &flag_positions_[idx];
}

std::uint32_t BattlefieldInfo::GetBattlegroundInfoEntry(std::int32_t index) const {
  if (index < 0 || index >= static_cast<std::int32_t>(bg_info_entries_.size()))
    return 0;
  return bg_info_entries_[index];
}

void BattlefieldInfo::RefreshBattlegroundInfoEntries(
    const openwow::data::dbc::DbcLoader &dbc,
    const std::function<bool(std::uint32_t)> &is_holiday_active,
    const bool force_sort) {
  const bool needs_population = bg_info_entries_.empty();
  if (needs_population) {
    bg_info_entries_.reserve(dbc.battlemaster_list().size());
    for (const auto &entry : dbc.battlemaster_list().entries()) {
      if (entry.instance_type == 3) {
        bg_info_entries_.push_back(entry.id);
      }
    }
  }

  if (!needs_population && !force_sort) {
    return;
  }

  std::stable_sort(bg_info_entries_.begin(), bg_info_entries_.end(),
                   [&dbc, &is_holiday_active](const std::uint32_t left_id,
                                              const std::uint32_t right_id) {
                     if (left_id == 32) {
                       return right_id != 32;
                     }
                     if (right_id == 32) {
                       return false;
                     }

                     const auto *left = dbc.battlemaster_list().LookupEntry(left_id);
                     const auto *right = dbc.battlemaster_list().LookupEntry(right_id);
                     if (left == nullptr || right == nullptr) {
                       return left_id < right_id;
                     }

                     const bool left_holiday =
                         IsHolidayBattlegroundEntry(*left, is_holiday_active);
                     const bool right_holiday =
                         IsHolidayBattlegroundEntry(*right, is_holiday_active);
                     if (left_holiday != right_holiday) {
                       return left_holiday;
                     }

                     if (left->min_level != right->min_level) {
                       return left->min_level < right->min_level;
                     }

                     return left_id < right_id;
                   });
}

void BattlefieldInfo::ObserveBattlefieldVehicle(const WorldObject &object,
                                                const openwow::data::dbc::DbcLoader *dbc) {
  if (!object.IsUnit()) {
    return;
  }

  const auto &unit = static_cast<const CGUnit_C &>(object);
  RemoveBattlefieldVehicle(unit.GetGuid());

  if (!IsBattlefieldVehicleUnit(dbc, unit) ||
      battlefield_vehicle_guids_.size() >= kMaxBattlefieldVehicles) {
    return;
  }

  battlefield_vehicle_guids_.push_back(unit.GetGuid());
}

void BattlefieldInfo::RemoveBattlefieldVehicle(const ObjectGuid guid) {
  battlefield_vehicle_guids_.erase(
      std::remove(battlefield_vehicle_guids_.begin(), battlefield_vehicle_guids_.end(), guid),
      battlefield_vehicle_guids_.end());
}

std::uint32_t BattlefieldInfo::GetBattlefieldVehicleCount() const {
  return static_cast<std::uint32_t>(battlefield_vehicle_guids_.size());
}

ObjectGuid BattlefieldInfo::GetBattlefieldVehicleGuid(const std::uint32_t index) const {
  if (index >= battlefield_vehicle_guids_.size()) {
    return ObjectGuid();
  }

  return battlefield_vehicle_guids_[index];
}

bool BattlefieldInfo::IsControllerRepresentedByBattlefieldVehicle(
    const ObjectManager &object_manager, const ObjectGuid controller_guid) const {
  if (controller_guid.IsEmpty()) {
    return false;
  }

  for (std::uint32_t index = 0; index < GetBattlefieldVehicleCount(); ++index) {
    const auto vehicle_guid = GetBattlefieldVehicleGuid(index);
    const auto *vehicle = object_manager.GetUnit(vehicle_guid);
    if (vehicle != nullptr &&
        vehicle->Interaction().MatchesImmediateControllerGuid(controller_guid)) {
      return true;
    }
  }

  return false;
}

std::uint32_t BattlefieldInfo::GetBattlefieldEstimatedWaitTime(const std::size_t index) const {
  if (index >= kMaxBGQueueSlots) {
    return 0;
  }

  return queue_slots_[index].avg_wait;
}

std::uint32_t BattlefieldInfo::GetBattlefieldTimeWaited(const std::size_t index) const {
  if (index >= kMaxBGQueueSlots) {
    return 0;
  }

  const auto queue_enter_tick = queue_slots_[index].time_in_queue;
  if (queue_enter_tick == 0) {
    return 0;
  }

  return core::GameClock::GetTickCount32() - queue_enter_tick;
}

std::uint32_t BattlefieldInfo::GetBattlefieldPortExpiration(const std::size_t index) const {
  if (index >= kMaxBGQueueSlots) {
    return 0;
  }

  return GetRemainingTickCount32(queue_slots_[index].expire_time) / 1000u;
}

std::uint32_t BattlefieldInfo::GetBattlefieldInstanceExpiration() const {
  return GetRemainingTickCount32(battlefield_instance_expire_tick_);
}

std::uint32_t BattlefieldInfo::GetBattlefieldInstanceRunTime() const {
  if (battlefield_instance_start_tick_ == 0) {
    return 0;
  }

  return core::GameClock::GetTickCount32() - battlefield_instance_start_tick_;
}

void BattlefieldInfo::ResetForPlayerEnterWorld() {
  Reset();

  for (std::size_t slot_index = 0; slot_index < kMaxArenaOpponents; ++slot_index) {
    FireArenaLifecycleEvent("arena", slot_index, kArenaClearedSuffix);
    FireArenaLifecycleEvent("arenapet", slot_index, kArenaClearedSuffix);
  }
}

void BattlefieldInfo::Reset() {
  for (auto &s : queue_slots_)
    s = BGQueueSlot{};
  bf_mgr_queue_entry_ = BFMgrQueueEntry{};
  active_slot_ = -1;
  num_positions_ = 0;
  player_positions_.clear();
  flag_positions_ = {};
  for (auto &o : arena_opponents_)
    o = ArenaOpponent{};
  score_entries_.clear();
  filtered_score_count_ = 0;
  faction_filter_ = -1;
  score_entries_are_arena_ = false;
  pending_score_name_queries_ = 0;
  pending_score_name_query_guids_.clear();
  pending_bg_player_status_messages_.clear();
  ResetScoreSortOrder();
  arena_team_info_ = {};
  battlefield_arena_faction_ = 0;
  battlefield_winner_valid_ = false;
  battlefield_winner_ = 0;
  active_bg_map_id_ = 0;
  active_bg_type_ = 0;
  battlefield_instance_expire_tick_ = 0;
  battlefield_instance_start_tick_ = 0;
  next_score_data_request_tick_ = 0;
  next_player_positions_request_tick_ = 0;
  bg_stats_columns_.clear();
  bg_stats_count_ = 0;
  bg_info_entries_.clear();
  battlefield_vehicle_guids_.clear();
}

BFMgrQueueEntry *BattlefieldInfo::FindBfMgrQueueEntry(const std::uint32_t queue_id) {
  if (bf_mgr_queue_entry_.queue_id == queue_id) {
    return &bf_mgr_queue_entry_;
  }
  return nullptr;
}

const BFMgrQueueEntry *BattlefieldInfo::FindBfMgrQueueEntry(const std::uint32_t queue_id) const {
  if (bf_mgr_queue_entry_.queue_id == queue_id) {
    return &bf_mgr_queue_entry_;
  }
  return nullptr;
}

BFMgrQueueEntry *BattlefieldInfo::FindOrReserveBfMgrQueueEntry(const std::uint32_t queue_id) {
  if (BFMgrQueueEntry *existing = FindBfMgrQueueEntry(queue_id)) {
    return existing;
  }

  if (bf_mgr_queue_entry_.area_id == 0 || bf_mgr_queue_entry_.state == WorldPvpQueueState::kNone) {
    return &bf_mgr_queue_entry_;
  }
  return nullptr;
}

bool BattlefieldInfo::IsCurrentWorldPvpArea(const ObjectManager &objects,
                                            const std::uint32_t area_id) const {
  if (area_id == 0) {
    return false;
  }

  const auto current_area_id = objects.GetAreaId();
  if (current_area_id == area_id) {
    return true;
  }

  if (current_area_id == 0 || dbc_loader_ == nullptr) {
    return false;
  }

  const auto *current_area = dbc_loader_->area_table().LookupEntry(current_area_id);
  return current_area != nullptr && current_area->parent_area == area_id;
}

void BattlefieldInfo::UpsertBfMgrQueueEntry(const std::uint32_t queue_id,
                                            const WorldPvpQueueState state,
                                            const std::uint32_t area_id,
                                            const std::uint32_t expiry_time) {
  BFMgrQueueEntry *slot = FindOrReserveBfMgrQueueEntry(queue_id);
  if (!slot) {
    return;
  }

  if (area_id != 0) {
    slot->area_id = area_id;
  }
  if (expiry_time != 0) {
    slot->expiry_time = expiry_time;
  }
  slot->queue_id = queue_id;
  slot->state = state;
}

}

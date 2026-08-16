
#include "openwow/game/battleground_manager.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/arena_system.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/update_fields.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

namespace openwow::game {

using openwow::diagnostics::Log;
using openwow::diagnostics::LogLevel;

namespace {

constexpr std::size_t kArenaRosterNameFieldSize = 0x30;
constexpr std::size_t kArenaCommandPlayerNameSize = 0x60;
constexpr std::size_t kArenaCommandTeamNameSize = 0x30;
constexpr std::size_t kStormCompareMax = 0x7FFFFFFF;

bool ReadFixedCString(
    PacketReader& reader, const std::size_t width, std::string& value) {
  value.clear();
  if (!reader.HasBytes(width)) {
    return false;
  }

  const auto* bytes = reader.PeekBytes(width);
  const auto* terminator = static_cast<const std::uint8_t*>(
      std::memchr(bytes, '\0', width));
  const auto length = terminator != nullptr ? static_cast<std::size_t>(terminator - bytes) : width;
  value.assign(reinterpret_cast<const char*>(bytes), length);
  reader.Skip(width);
  return true;
}

[[nodiscard]] const char* ArenaClassNameForSort(
    const openwow::data::dbc::DbcLoader* dbc, const std::uint8_t class_id) {
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto* entry = dbc->chr_classes().LookupEntry(class_id);
  if (entry == nullptr || entry->name.empty()) {
    return nullptr;
  }

  return entry->name.data();
}

[[nodiscard]] int CompareArenaRosterMembersByField(
    const openwow::data::dbc::DbcLoader* dbc,
    const ArenaTeamMember& left, const ArenaTeamMember& right,
    const ArenaRosterSortField field) {
  switch (field) {
    case ArenaRosterSortField::kName:
      return openwow::core::SStrCmpI(
          left.name.c_str(), right.name.c_str(), kStormCompareMax);
    case ArenaRosterSortField::kClass: {
      const auto* left_name = ArenaClassNameForSort(dbc, left.class_id);
      const auto* right_name = ArenaClassNameForSort(dbc, right.class_id);
      if (left_name == nullptr || right_name == nullptr) {
        return 0;
      }
      return openwow::core::SStrCmpNoCaseCollate(
          left_name, right_name, kStormCompareMax);
    }
    case ArenaRosterSortField::kPlayed:
      return left.week_games == right.week_games
                 ? 0
                 : (left.week_games > right.week_games ? -1 : 1);
    case ArenaRosterSortField::kWins:
      return left.week_wins == right.week_wins
                 ? 0
                 : (left.week_wins > right.week_wins ? -1 : 1);
    case ArenaRosterSortField::kSeasonPlayed:
      return left.season_games == right.season_games
                 ? 0
                 : (left.season_games > right.season_games ? -1 : 1);
    case ArenaRosterSortField::kSeasonWon:
      return left.season_wins == right.season_wins
                 ? 0
                 : (left.season_wins > right.season_wins ? -1 : 1);
    case ArenaRosterSortField::kRating:
      return left.personal_rating == right.personal_rating
                 ? 0
                 : (left.personal_rating > right.personal_rating ? -1 : 1);
  }

  return 0;
}

}

BattlegroundManager::BattlegroundManager() {
  ResetArenaRosterViewState();
}

void BattlegroundManager::ResetArenaRosterViewState() {
  arena_roster_selection_guid_ = 0;
  arena_roster_show_offline_ = true;
  arena_roster_sort_order_ = {{
      {ArenaRosterSortField::kName, false},
      {ArenaRosterSortField::kClass, false},
      {ArenaRosterSortField::kPlayed, false},
      {ArenaRosterSortField::kWins, false},
      {ArenaRosterSortField::kSeasonPlayed, false},
      {ArenaRosterSortField::kSeasonWon, false},
      {ArenaRosterSortField::kRating, false},
  }};
  arena_roster_request_deadlines_ms_.fill(0);
}

int BattlegroundManager::FindArenaRosterSlotByTeamId(const std::uint32_t team_id) const {
  return ArenaSystem::Get().FindTeamSlotById(team_id);
}

void BattlegroundManager::SortArenaRoster(const std::uint8_t slot) {
  if (slot >= arena_rosters_.size()) {
    return;
  }

  auto& roster = arena_rosters_[slot];
  std::stable_sort(
      roster.members.begin(), roster.members.end(),
      [this](const ArenaTeamMember& left, const ArenaTeamMember& right) {
        if (!arena_roster_show_offline_ && left.online != right.online) {
          return left.online;
        }

        for (const auto& spec : arena_roster_sort_order_) {
          int cmp = CompareArenaRosterMembersByField(dbc_, left, right, spec.field);
          if (cmp == 0) {
            continue;
          }
          if (spec.descending) {
            cmp = -cmp;
          }
          return cmp < 0;
        }

        return false;
      });
}

bool BattlegroundManager::HandleBattlefieldList(const std::uint8_t* data,
                                                  std::size_t len) {
  PacketReader r(data, len);
  BattlefieldListInfo decoded;

  if (!r.ReadU64(decoded.battlemaster_guid)) return false;

  std::uint8_t from_where;
  if (!r.ReadU8(from_where)) return false;
  decoded.from_where = from_where;
  if (!r.ReadU32(decoded.bg_type_id)) return false;

  std::uint8_t unk1, unk2, has_win;
  if (!r.ReadU8(unk1)) return false;
  if (!r.ReadU8(unk2)) return false;
  if (!r.ReadU8(has_win)) return false;
  decoded.holiday_has_win = (has_win != 0);
  if (!r.ReadU32(decoded.holiday_winner_honor)) return false;
  if (!r.ReadU32(decoded.holiday_winner_arena)) return false;
  if (!r.ReadU32(decoded.holiday_loser_honor)) return false;

  std::uint8_t is_random;
  if (!r.ReadU8(is_random)) return false;
  decoded.is_random = (is_random != 0);

  if (decoded.is_random) {
    std::uint8_t has_win_r;
    if (!r.ReadU8(has_win_r)) return false;
    decoded.random_has_win = (has_win_r != 0);
    if (!r.ReadU32(decoded.random_winner_honor)) return false;
    if (!r.ReadU32(decoded.random_winner_arena)) return false;
    if (!r.ReadU32(decoded.random_loser_honor)) return false;
  }

  std::uint32_t instance_count;
  if (!r.ReadU32(instance_count)) return false;
  if (instance_count > r.Remaining() / sizeof(std::uint32_t)) return false;

  decoded.instance_ids.resize(instance_count);
  for (std::uint32_t i = 0; i < instance_count; ++i) {
    if (!r.ReadU32(decoded.instance_ids[i])) return false;
  }

  bf_list_ = std::move(decoded);
  return true;
}

bool BattlegroundManager::HandlePvpLogData(const std::uint8_t* data,
                                             std::size_t len) {
  PacketReader r(data, len);
  PvpLogData decoded;

  std::uint8_t is_arena;
  if (!r.ReadU8(is_arena)) return false;
  decoded.is_arena = (is_arena != 0);

  if (decoded.is_arena) {
    for (int t = 0; t < 2; ++t) {
      for (auto &value : decoded.arena_teams[t].raw_values) {
        if (!r.ReadU32(value)) return false;
      }
    }
    for (int t = 0; t < 2; ++t) {
      if (!r.ReadCString(decoded.arena_teams[t].name, 0x60u)) return false;
    }
  }

  std::uint8_t bg_ended;
  if (!r.ReadU8(bg_ended)) return false;
  decoded.is_ended = (bg_ended != 0);

  if (decoded.is_arena || decoded.is_ended) {
    if (!r.ReadU8(decoded.winner)) return false;
  }

  std::uint32_t score_count;
  if (!r.ReadU32(score_count)) return false;
  constexpr std::size_t kMinimumArenaScoreWireSize = 8u + 4u + 1u + 4u + 4u + 4u;
  constexpr std::size_t kMinimumBattlegroundScoreWireSize =
      8u + 4u + 4u + 4u + 4u + 4u + 4u + 4u;
  const auto minimum_score_size = decoded.is_arena ? kMinimumArenaScoreWireSize
                                                    : kMinimumBattlegroundScoreWireSize;
  if (score_count > r.Remaining() / minimum_score_size) return false;

  decoded.scores.resize(score_count);
  for (std::uint32_t i = 0; i < score_count; ++i) {
    auto& s = decoded.scores[i];
    if (!r.ReadU64(s.player_guid)) return false;
    if (!r.ReadU32(s.killing_blows)) return false;

    if (decoded.is_arena) {
      if (!r.ReadU8(s.pvp_team_id)) return false;
    } else {
      if (!r.ReadU32(s.honorable_kills)) return false;
      if (!r.ReadU32(s.deaths)) return false;
      if (!r.ReadU32(s.bonus_honor)) return false;
    }

    if (!r.ReadU32(s.damage_done)) return false;
    if (!r.ReadU32(s.healing_done)) return false;

    std::uint32_t obj_count;
    if (!r.ReadU32(obj_count)) return false;
    if (obj_count > r.Remaining() / sizeof(std::uint32_t)) return false;
    constexpr std::size_t kStoredObjectiveCount = 8u;
    s.bg_objectives.resize(
        std::min<std::size_t>(obj_count, kStoredObjectiveCount));
    for (std::uint32_t j = 0; j < obj_count; ++j) {
      std::uint32_t value = 0;
      if (!r.ReadU32(value)) return false;
      if (j < s.bg_objectives.size()) {
        s.bg_objectives[j] = value;
      }
    }
  }

  pvp_log_ = std::move(decoded);
  return true;
}

bool BattlegroundManager::HandlePvpCredit(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(last_pvp_credit_.honor)) return false;
  if (!r.ReadU64(last_pvp_credit_.victim_guid)) return false;
  if (!r.ReadU32(last_pvp_credit_.victim_rank)) return false;
  return true;
}

bool BattlegroundManager::HandleArenaTeamRoster(const std::uint8_t* data,
                                                  std::size_t len) {
  PacketReader r(data, len);

  std::uint32_t team_id = 0;
  if (!r.ReadU32(team_id)) return false;

  const auto slot = FindArenaRosterSlotByTeamId(team_id);
  if (slot < 0) {
    return true;
  }

  auto& roster = arena_rosters_[static_cast<std::size_t>(slot)];
  roster = ArenaTeamRoster{};
  roster.team_id = team_id;

  std::uint8_t unk308;
  if (!r.ReadU8(unk308)) return false;

  std::uint32_t member_count;
  if (!r.ReadU32(member_count)) return false;
  if (!r.ReadU32(roster.team_type)) return false;

  roster.members.clear();
  roster.members.resize(member_count);
  for (std::uint32_t i = 0; i < member_count; ++i) {
    auto& m = roster.members[i];
    if (!r.ReadU64(m.guid)) return false;

    std::uint8_t online;
    if (!r.ReadU8(online)) return false;
    m.online = (online != 0);

    if (!ReadFixedCString(r, kArenaRosterNameFieldSize, m.name)) return false;

    if (!r.ReadU32(m.rank)) return false;

    if (!r.ReadU8(m.level)) return false;
    if (!r.ReadU8(m.class_id)) return false;
    if (!r.ReadU32(m.week_games)) return false;
    if (!r.ReadU32(m.week_wins)) return false;
    if (!r.ReadU32(m.season_games)) return false;
    if (!r.ReadU32(m.season_wins)) return false;
    if (!r.ReadU32(m.personal_rating)) return false;

    if (unk308 != 0) {
      if (!r.ReadFloat(m.mmr_change)) return false;
      if (!r.ReadFloat(m.mmr_value)) return false;
    } else {
      m.mmr_change = 0.0f;
      m.mmr_value = 0.0f;
    }
  }

  SortArenaRoster(static_cast<std::uint8_t>(slot));

  std::vector<ArenaSystemMember> members;
  members.reserve(roster.members.size());
  for (const auto& member : roster.members) {
    members.push_back(ArenaSystemMember{
        .guid = member.guid,
        .name = member.name,
        .rank = member.rank,
        .played_week = member.week_games,
        .won_week = member.week_wins,
        .played_season = member.season_games,
        .won_season = member.season_wins,
        .personal_rating = member.personal_rating,
        .class_id = member.class_id,
        .is_online = member.online,
    });
  }

  ArenaSystem::Get().UpdateTeamRosterById(team_id, static_cast<std::uint8_t>(roster.team_type),
                                          members);
  return true;
}

bool BattlegroundManager::HandleArenaTeamCommandResult(
    const std::uint8_t* data, std::size_t len) {
  PacketReader r(data, len);
  arena_command_ = ArenaCommandResult{};

  if (!r.ReadU32(arena_command_.action)) return false;
  if (!ReadFixedCString(r, kArenaCommandPlayerNameSize, arena_command_.player_name)) return false;
  if (!ReadFixedCString(r, kArenaCommandTeamNameSize, arena_command_.team_name)) return false;
  if (!r.ReadU32(arena_command_.error_type)) return false;

  return true;
}

bool BattlegroundManager::HandleArenaTeamStats(const std::uint8_t* data,
                                                 std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(arena_stats_.team_id)) return false;
  if (!r.ReadU32(arena_stats_.rating)) return false;
  if (!r.ReadU32(arena_stats_.week_games)) return false;
  if (!r.ReadU32(arena_stats_.week_wins)) return false;
  if (!r.ReadU32(arena_stats_.season_games)) return false;
  if (!r.ReadU32(arena_stats_.season_wins)) return false;
  if (!r.ReadU32(arena_stats_.rank)) return false;
  return true;
}

bool BattlegroundManager::HandleArenaTeamInvite(const std::uint8_t* data,
                                                  std::size_t len) {
  PacketReader r(data, len);
  ArenaTeamInvite invite;
  if (!r.ReadCString(invite.inviter_name, 0x30u)) return false;
  if (!r.ReadCString(invite.team_name, 0x60u)) return false;
  arena_invite_ = std::move(invite);
  return true;
}

const ArenaTeamRoster& BattlegroundManager::GetArenaRoster(const std::uint8_t slot) const {
  static const ArenaTeamRoster kEmptyRoster{};
  return slot < arena_rosters_.size() ? arena_rosters_[slot] : kEmptyRoster;
}

std::size_t BattlegroundManager::GetArenaRosterMemberCount(
    const std::uint8_t slot, const bool include_offline) const {
  if (slot >= arena_rosters_.size()) {
    return 0;
  }

  const auto& roster = arena_rosters_[slot];
  if (include_offline || arena_roster_show_offline_) {
    return roster.members.size();
  }

  return roster.OnlineMemberCount();
}

const ArenaTeamMember* BattlegroundManager::GetArenaRosterMember(
    const std::uint8_t slot, const std::size_t member_index) const {
  if (slot >= arena_rosters_.size()) {
    return nullptr;
  }

  const auto& roster = arena_rosters_[slot];
  if (member_index >= roster.members.size()) {
    return nullptr;
  }

  return &roster.members[member_index];
}

void BattlegroundManager::SetSelectedBattlefieldListIndex(const std::uint32_t index) {
  if (index == 0) {
    selected_battlefield_instance_id_ = 0;
    return;
  }

  const auto zero_based_index = static_cast<std::size_t>(index - 1);
  if (zero_based_index >= bf_list_.instance_ids.size()) {
    selected_battlefield_instance_id_ = 0;
    return;
  }

  selected_battlefield_instance_id_ = bf_list_.instance_ids[zero_based_index];
}

std::uint32_t BattlegroundManager::GetSelectedBattlefieldListIndex() const {
  if (selected_battlefield_instance_id_ == 0) {
    return 0;
  }

  const auto selected_it = std::find(
      bf_list_.instance_ids.begin(), bf_list_.instance_ids.end(),
      selected_battlefield_instance_id_);
  if (selected_it == bf_list_.instance_ids.end()) {
    return 0;
  }

  return static_cast<std::uint32_t>(
      std::distance(bf_list_.instance_ids.begin(), selected_it) + 1);
}

void BattlegroundManager::SetArenaRosterSelection(
    const std::uint8_t slot, const std::size_t member_index) {
  if (slot >= arena_rosters_.size() ||
      member_index >= arena_rosters_[slot].members.size()) {
    arena_roster_selection_guid_ = 0;
    return;
  }

  arena_roster_selection_guid_ = arena_rosters_[slot].members[member_index].guid;
}

int BattlegroundManager::GetArenaRosterSelection(const std::uint8_t slot) const {
  if (slot >= arena_rosters_.size() || arena_roster_selection_guid_ == 0) {
    return -1;
  }

  const auto& members = arena_rosters_[slot].members;
  for (std::size_t index = 0; index < members.size(); ++index) {
    if (members[index].guid == arena_roster_selection_guid_) {
      return static_cast<int>(index);
    }
  }

  return -1;
}

bool BattlegroundManager::SetArenaRosterShowOffline(const bool show_offline) {
  if (arena_roster_show_offline_ == show_offline) {
    return false;
  }

  arena_roster_show_offline_ = show_offline;
  for (std::uint8_t slot = 0; slot < arena_rosters_.size(); ++slot) {
    if (!arena_rosters_[slot].members.empty()) {
      SortArenaRoster(slot);
    }
  }

  return true;
}

void BattlegroundManager::SortArenaRosters(const ArenaRosterSortField field) {
  std::size_t field_index = arena_roster_sort_order_.size();
  for (std::size_t index = 0; index < arena_roster_sort_order_.size(); ++index) {
    if (arena_roster_sort_order_[index].field == field) {
      field_index = index;
      break;
    }
  }

  if (field_index >= arena_roster_sort_order_.size()) {
    return;
  }

  auto leading_spec = arena_roster_sort_order_[field_index];
  if (field_index == 0) {
    leading_spec.descending = !leading_spec.descending;
  }

  while (field_index > 0) {
    arena_roster_sort_order_[field_index] = arena_roster_sort_order_[field_index - 1];
    --field_index;
  }
  arena_roster_sort_order_[0] = leading_spec;

  for (std::uint8_t slot = 0; slot < arena_rosters_.size(); ++slot) {
    if (!arena_rosters_[slot].members.empty()) {
      SortArenaRoster(slot);
    }
  }
}

bool BattlegroundManager::BeginArenaRosterRequest(
    const std::uint8_t slot, const std::uint32_t now_tick_ms) {
  if (slot >= arena_roster_request_deadlines_ms_.size()) {
    return false;
  }

  const auto next_allowed_tick = arena_roster_request_deadlines_ms_[slot];
  if (next_allowed_tick != 0 &&
      static_cast<std::int32_t>(now_tick_ms - next_allowed_tick) < 0) {
    return false;
  }

  arena_roster_request_deadlines_ms_[slot] = now_tick_ms + 10000u;
  return true;
}

void BattlegroundManager::ResetArenaRosterRequest(const std::uint8_t slot) {
  if (slot < arena_roster_request_deadlines_ms_.size()) {
    arena_roster_request_deadlines_ms_[slot] = 0;
  }
}

void BattlegroundManager::ResetAllArenaRosterRequests() {
  arena_roster_request_deadlines_ms_.fill(0);
}

ArenaOpponentSlot BattlegroundManager::GetArenaOpponent(std::size_t slot) const {
  return BattlefieldInfo::Get().GetArenaOpponent(slot);
}

std::size_t BattlegroundManager::GetArenaOpponentSlotCount() const {
  return BattlefieldInfo::Get().GetArenaOpponentSlotCount();
}

std::optional<std::size_t> BattlegroundManager::FindArenaOpponentSlot(
    const std::uint64_t guid) const {
  return BattlefieldInfo::Get().FindArenaOpponentSlot(guid);
}

bool BattlegroundManager::HasArenaOpponentGuid(const std::uint64_t guid) const {
  return FindArenaOpponentSlot(guid).has_value();
}

bool BattlegroundManager::ArenaOpponentHasAura(const std::uint64_t guid,
                                               const std::uint32_t spell_id) const {
  return BattlefieldInfo::Get().ArenaOpponentHasAura(guid, spell_id);
}

bool BattlegroundManager::GetArenaOpponentPvpFlag(const std::uint64_t guid) const {
  return BattlefieldInfo::Get().GetArenaOpponentPvpFlag(guid);
}

std::uint32_t BattlegroundManager::GetArenaOpponentVehicleSeat(const std::uint64_t guid) const {
  return BattlefieldInfo::Get().GetArenaOpponentVehicleSeat(guid);
}

void BattlegroundManager::NotifyArenaUnitUnseen(const ObjectGuid& guid,
                                                const ObjectManager& objects) {
  BattlefieldInfo::Get().OnArenaUnitUnseen(guid, objects);
}

void BattlegroundManager::NotifyArenaUnitDestroyed(const ObjectGuid& guid,
                                                   const ObjectManager& objects) {
  BattlefieldInfo::Get().OnArenaUnitDestroyed(guid, objects);
}

void BattlegroundManager::SetArenaOpponent(
    const ObjectManager& objects, std::size_t slot,
    const ArenaOpponentSlot& opponent) {
  BattlefieldInfo::Get().SetArenaOpponent(objects, slot, opponent);
}

void BattlegroundManager::SetArenaOpponentPet(const ObjectManager& objects,
                                              const std::size_t slot,
                                              const ObjectGuid& pet_guid) {
  BattlefieldInfo::Get().SetArenaOpponentPet(objects, slot, pet_guid);
}

void BattlegroundManager::SetArenaOpponentPetState(
    const std::size_t slot, TrackedControlledUnitStateSlice state) {
  BattlefieldInfo::Get().SetArenaOpponentPetState(slot, std::move(state));
}

void BattlegroundManager::SetArenaOpponentAuraSnapshot(
    const std::uint64_t guid,
    std::vector<std::uint32_t> aura_spell_ids) {
  BattlefieldInfo::Get().SetArenaOpponentAuraSnapshot(guid, std::move(aura_spell_ids));
}

void BattlegroundManager::SetArenaOpponentPvpFlag(const std::uint64_t guid,
                                                  const bool pvp_enabled) {
  BattlefieldInfo::Get().SetArenaOpponentPvpFlag(guid, pvp_enabled);
}

void BattlegroundManager::SetArenaOpponentVehicleSeat(
    const std::uint64_t guid,
    const std::uint32_t vehicle_seat) {
  BattlefieldInfo::Get().SetArenaOpponentVehicleSeat(guid, vehicle_seat);
}

void BattlegroundManager::BeginBattlefieldListRequest(const std::uint32_t bg_type_id) {
  bf_list_.bg_type_id = bg_type_id;
  bf_list_.instance_ids.clear();
}

void BattlegroundManager::Clear() {
  bf_list_ = BattlefieldListInfo{};
  active_battlemaster_guid_ = 0;
  pvp_log_ = PvpLogData{};
  last_pvp_credit_ = PvpCreditInfo{};
  arena_rosters_ = {};
  arena_stats_ = ArenaTeamStats{};
  arena_command_ = ArenaCommandResult{};
  arena_invite_ = ArenaTeamInvite{};
  selected_battlefield_instance_id_ = 0;
  ResetArenaRosterViewState();
}

}

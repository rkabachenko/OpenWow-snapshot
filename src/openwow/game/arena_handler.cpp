
#include "openwow/game/arena_handler.h"

#include "openwow/data/db_cache_instances.h"
#include "openwow/data/wdb_cache.h"
#include "openwow/data/wdb_persistence.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <array>
#include <cstring>
#include <string_view>

namespace openwow::game {

namespace {

constexpr std::size_t kArenaTeamQueryNameStorageBytes = 0x60;

struct ArenaTeamCacheStringField {
  std::array<char, kArenaTeamQueryNameStorageBytes> bytes{};
};

class ArenaTeamCacheReader {
 public:
  explicit ArenaTeamCacheReader(std::span<const std::uint8_t> bytes)
      : bytes_(bytes) {}

  [[nodiscard]] std::uint32_t ReadU32() {
    if (failed_ || pos_ + sizeof(std::uint32_t) > bytes_.size()) {
      failed_ = true;
      return 0;
    }

    std::uint32_t value = 0;
    std::memcpy(&value, bytes_.data() + pos_, sizeof(value));
    pos_ += sizeof(value);
    return value;
  }

  [[nodiscard]] ArenaTeamCacheStringField ReadCStringField() {
    ArenaTeamCacheStringField field;
    if (failed_) {
      return field;
    }

    for (std::size_t index = 0; index < field.bytes.size(); ++index) {
      if (pos_ >= bytes_.size()) {
        failed_ = true;
        field.bytes[0] = '\0';
        return field;
      }

      const auto byte = bytes_[pos_++];
      field.bytes[index] = static_cast<char>(byte);
      if (byte == 0) {
        return field;
      }
    }

    failed_ = true;
    field.bytes[0] = '\0';
    return field;
  }

 private:
  std::span<const std::uint8_t> bytes_{};
  std::size_t pos_{0};
  bool failed_{false};
};

[[nodiscard]] std::string ClampArenaTeamCacheName(std::string_view name) {
  constexpr std::size_t kMaxNameBytes =
      kArenaTeamQueryNameStorageBytes - 1;
  return std::string(name.substr(0, kMaxNameBytes));
}

void AppendU32(std::vector<std::uint8_t> &bytes, const std::uint32_t value) {
  for (int shift = 0; shift < 4; ++shift) {
    bytes.push_back(
        static_cast<std::uint8_t>((value >> (shift * 8)) & 0xFF));
  }
}

[[nodiscard]] ArenaTeamQueryResponse DecodeArenaTeamQueryRecord(
    std::span<const std::uint8_t> bytes) {
  ArenaTeamCacheReader reader(bytes);
  ArenaTeamQueryResponse response{};
  response.team_id = reader.ReadU32();
  response.team_name = reader.ReadCStringField().bytes.data();
  response.team_type = reader.ReadU32();
  response.background_color = reader.ReadU32();
  response.emblem_style = reader.ReadU32();
  response.emblem_color = reader.ReadU32();
  response.border_style = reader.ReadU32();
  response.border_color = reader.ReadU32();
  response.team_name = ClampArenaTeamCacheName(response.team_name);
  return response;
}

[[nodiscard]] std::vector<std::uint8_t> EncodeArenaTeamQueryRecord(
    const ArenaTeamQueryResponse &response) {
  const auto clamped_name = ClampArenaTeamCacheName(response.team_name);

  std::vector<std::uint8_t> bytes;
  bytes.reserve(sizeof(std::uint32_t) * 7 + clamped_name.size() + 1);
  AppendU32(bytes, response.team_id);
  bytes.insert(bytes.end(), clamped_name.begin(), clamped_name.end());
  bytes.push_back(0);
  AppendU32(bytes, response.team_type);
  AppendU32(bytes, response.background_color);
  AppendU32(bytes, response.emblem_style);
  AppendU32(bytes, response.emblem_color);
  AppendU32(bytes, response.border_style);
  AppendU32(bytes, response.border_color);
  return bytes;
}

[[nodiscard]] std::optional<ArenaTeamQueryResponse> LoadPersistedArenaTeamQuery(
    openwow::data::DBCacheRuntime& runtime,
    const std::uint32_t team_id) {
  const auto persisted = runtime.cache().Get(
      openwow::data::WDBCacheType::ArenaTeam, team_id);
  if (!persisted.has_value()) {
    return std::nullopt;
  }

  return DecodeArenaTeamQueryRecord(persisted->data);
}

void PersistArenaTeamQuery(openwow::data::DBCacheRuntime& runtime,
                           const ArenaTeamQueryResponse &response) {
  auto &cache = runtime.cache();
  auto &persistence = runtime.persistence();
  cache.UpdateEntry(openwow::data::WDBCacheType::ArenaTeam, response.team_id,
                    EncodeArenaTeamQueryRecord(response),
                    openwow::data::wdb_format::kVersion_ArenaTeam);
  persistence.SetDirty(openwow::data::WDBCacheType::ArenaTeam);
}

void InvalidatePersistedArenaTeamQuery(openwow::data::DBCacheRuntime& runtime,
                                       const std::uint32_t team_id) {
  auto &cache = runtime.cache();
  auto &persistence = runtime.persistence();
  if (cache.InvalidateEntry(openwow::data::WDBCacheType::ArenaTeam, team_id)) {
    persistence.SetDirty(openwow::data::WDBCacheType::ArenaTeam);
  }
}

}

void ArenaHandler::ResetInspectSnapshot() {
  inspect_honor_data_ready_ = false;
  inspect_honor_request_in_flight_ = false;
  inspect_arena_teams_.fill({});
  last_arena_inspect_.reset();
}

void ArenaHandler::BeginInspect(const std::uint64_t guid) {
  if (guid == inspect_target_guid_) {
    return;
  }

  inspect_target_guid_ = guid;
  ResetInspectSnapshot();
}

void ArenaHandler::ClearInspectState() {
  if (inspect_target_guid_ == 0 && !inspect_honor_data_ready_ &&
      !inspect_honor_request_in_flight_) {
    return;
  }

  inspect_target_guid_ = 0;
  ResetInspectSnapshot();
}

void ArenaHandler::EraseArenaTeamQuery(const std::uint32_t team_id) {
  if (team_id == 0) {
    return;
  }

  InvalidatePersistedArenaTeamQuery(db_cache_runtime_, team_id);
  arena_team_query_cache_.erase(team_id);
  pending_arena_team_query_callbacks_.erase(team_id);
  inflight_arena_team_queries_.erase(team_id);
}

void ArenaHandler::ClearArenaTeamQueryMirror() {
  arena_team_query_cache_.clear();
}

void ArenaHandler::ClearPendingQueriesOnLogout() {
  pending_arena_team_query_callbacks_.clear();
  inflight_arena_team_queries_.clear();
}

bool ArenaHandler::QueueArenaTeamUpdateQuery(const std::uint32_t team_id) {
  return QueueArenaTeamQuery(team_id,
                             ArenaTeamQueryCallbackKind::kArenaTeamUpdate);
}

bool ArenaHandler::StoreInspectHonorStats(const InspectHonorStats& stats) {
  if (stats.player_guid == 0 || stats.player_guid != inspect_target_guid_) {
    return false;
  }

  inspect_honor_cache_.UpdateFromStats(stats);
  inspect_honor_data_ready_ = true;
  inspect_honor_request_in_flight_ = false;
  return true;
}

bool ArenaHandler::QueueInspectArenaTeamQuery(const std::uint32_t team_id) {
  return QueueArenaTeamQuery(team_id,
                             ArenaTeamQueryCallbackKind::kInspectHonorUpdate);
}

bool ArenaHandler::QueueArenaTeamQuery(
    const std::uint32_t team_id,
    const ArenaTeamQueryCallbackKind callback_kind) {
  if (team_id == 0 || FindArenaTeamQuery(team_id) != nullptr) {
    return false;
  }

  auto& callbacks = pending_arena_team_query_callbacks_[team_id];
  if (callback_kind == ArenaTeamQueryCallbackKind::kArenaTeamUpdate) {
    ++callbacks.arena_team_update_count;
  } else {
    ++callbacks.inspect_honor_update_count;
  }
  return inflight_arena_team_queries_.insert(team_id).second;
}

const ArenaTeamInspect::Team* ArenaHandler::GetInspectArenaTeam(
    const std::size_t index) const {
  if (index >= inspect_arena_teams_.size()) {
    return nullptr;
  }

  const auto& team = inspect_arena_teams_[index];
  return team.team_id != 0 ? &team : nullptr;
}

const ArenaTeamQueryResponse* ArenaHandler::FindArenaTeamQuery(
    const std::uint32_t team_id) const {
  if (team_id == 0) {
    return nullptr;
  }

  const auto persisted =
      LoadPersistedArenaTeamQuery(db_cache_runtime_, team_id);
  if (!persisted.has_value()) {
    arena_team_query_cache_.erase(team_id);
    return nullptr;
  }

  auto [it, _] = arena_team_query_cache_.insert_or_assign(
      team_id, std::move(*persisted));
  return &it->second;
}

bool ArenaHandler::HandleArenaTeamQueryResponse(const std::uint8_t* data,
                                                std::size_t len) {
  if (len < sizeof(std::uint32_t)) {
    return false;
  }

  last_team_query_ = DecodeArenaTeamQueryRecord(
      std::span<const std::uint8_t>(data, len));

  const auto team_id = last_team_query_.team_id;
  inflight_arena_team_queries_.erase(team_id);
  PendingArenaTeamQueryCallbacks callback_counts{};
  if (const auto callback_it =
          pending_arena_team_query_callbacks_.find(team_id);
      callback_it != pending_arena_team_query_callbacks_.end()) {
    callback_counts = callback_it->second;
    pending_arena_team_query_callbacks_.erase(callback_it);
  }

  if (last_team_query_.team_name.empty()) {
    InvalidatePersistedArenaTeamQuery(db_cache_runtime_, team_id);
    arena_team_query_cache_.erase(team_id);
    return true;
  }

  PersistArenaTeamQuery(db_cache_runtime_, last_team_query_);
  arena_team_query_cache_[team_id] = last_team_query_;
  for (std::size_t i = 0; i < callback_counts.arena_team_update_count; ++i) {
    ui::game::ScriptEventDispatch::Get().FireGlobalEvent(
        ui::game::events::ARENA_TEAM_UPDATE);
  }
  for (std::size_t i = 0; i < callback_counts.inspect_honor_update_count; ++i) {
    ui::game::ScriptEventDispatch::Get().FireInspectHonorUpdate();
  }
  return true;
}

bool ArenaHandler::HandleArenaTeamEvent(const std::uint8_t* data,
                                         std::size_t len) {
  PacketReader r(data, len);
  ArenaTeamEvent evt{};
  if (!r.ReadU8(evt.event_type)) return false;
  std::uint8_t str_count = 0;
  if (!r.ReadU8(str_count)) return false;
  constexpr std::uint8_t kMaxArenaEventStrings = 3;
  if (str_count > kMaxArenaEventStrings) return false;
  evt.strings.resize(str_count);
  for (auto& s : evt.strings) {
    if (!r.ReadCString(s, 0x100u)) return false;
  }
  if (evt.event_type == 3 || evt.event_type == 4) {
    if (!r.ReadU64(evt.guid)) return false;
    evt.has_guid = true;
  }
  last_team_event_ = std::move(evt);
  return true;
}

void ArenaHandler::Clear() {
  ClearInspectState();
  last_team_query_ = {};
  last_team_event_ = {};
  last_arena_inspect_.reset();
  last_arena_error_.reset();
  arena_team_query_cache_.clear();
  pending_arena_team_query_callbacks_.clear();
  inflight_arena_team_queries_.clear();
  arena_change_failed_queued_ = 0;
  arena_unit_destroyed_guid_ = 0;
  last_joined_bg_queue_.reset();
  battlefield_port_denied_ = false;
  battleground_info_throttled_ = false;
  removed_from_pvp_queue_ = false;
  last_pvp_afk_result_.reset();
}

bool ArenaHandler::HandleInspectArenaTeams(const std::uint8_t* data,
                                           std::size_t len) {
  PacketReader r(data, len);
  ArenaTeamInspect info{};
  if (!r.ReadU64(info.guid)) return false;
  auto& team = info.team;
  if (!r.ReadU8(team.bracket_index)) return false;
  if (!r.ReadU32(team.team_id)) return false;
  if (!r.ReadU32(team.rating)) return false;
  if (!r.ReadU32(team.games_played)) return false;
  if (!r.ReadU32(team.games_won)) return false;
  if (!r.ReadU32(team.season_games_played)) return false;
  if (!r.ReadU32(team.season_games_won)) return false;

  if (team.bracket_index > 3) {
    return true;
  }

  if (info.guid != inspect_target_guid_) {
    return true;
  }

  if (team.bracket_index < inspect_arena_teams_.size()) {
    inspect_arena_teams_[team.bracket_index] = team;
  }
  last_arena_inspect_ = std::move(info);
  ui::game::ScriptEventDispatch::Get().FireInspectHonorUpdate();
  return true;
}

bool ArenaHandler::HandleArenaError(const std::uint8_t* data,
                                    std::size_t len) {
  PacketReader r(data, len);
  ArenaError err{};
  if (!r.ReadU32(err.error_type)) return false;
  if (err.error_type == 0 && !r.ReadU8(err.unk)) return false;
  last_arena_error_ = err;
  return true;
}

bool ArenaHandler::HandleArenaTeamChangeFailedQueued(const std::uint8_t* data,
                                                     std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(arena_change_failed_queued_)) return false;
  return true;
}

bool ArenaHandler::HandleArenaUnitDestroyed(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(arena_unit_destroyed_guid_)) return false;
  return true;
}

bool ArenaHandler::HandleJoinedBattlegroundQueue(const std::uint8_t* data,
                                                 std::size_t len) {
  PacketReader r(data, len);
  JoinedBGQueue q{};
  if (!r.ReadU32(q.bg_type)) return false;
  if (!r.ReadU8(q.unk)) return false;
  if (!r.ReadU32(q.unk2)) return false;
  last_joined_bg_queue_ = q;
  return true;
}

bool ArenaHandler::HandleBattlefieldPortDenied(const std::uint8_t* ,
                                               std::size_t ) {
  battlefield_port_denied_ = true;
  return true;
}

bool ArenaHandler::HandleBattlegroundInfoThrottled(const std::uint8_t* ,
                                                   std::size_t ) {
  battleground_info_throttled_ = true;
  return true;
}

bool ArenaHandler::HandleRemovedFromPvpQueue(const std::uint8_t* ,
                                             std::size_t ) {
  removed_from_pvp_queue_ = true;
  return true;
}

bool ArenaHandler::HandleReportPvpAfkResult(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  PvpAfkResult res{};
  if (!r.ReadU8(res.offender_count)) return false;
  if (!r.ReadU8(res.num_reported)) return false;
  if (!r.ReadU8(res.num_needed)) return false;
  last_pvp_afk_result_ = res;
  return true;
}

}

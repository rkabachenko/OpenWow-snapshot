
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "openwow/game/inspect_handler.h"
#include "openwow/game/packet_reader.h"

namespace openwow::data {
class DBCacheRuntime;
}

namespace openwow::game {

struct ArenaTeamQueryResponse {
  std::uint32_t team_id = 0;
  std::string team_name;
  std::uint32_t team_type = 0;
  std::uint32_t background_color = 0;
  std::uint32_t emblem_style = 0;
  std::uint32_t emblem_color = 0;
  std::uint32_t border_style = 0;
  std::uint32_t border_color = 0;
};

struct ArenaTeamEvent {
  std::uint8_t event_type = 0;
  std::vector<std::string> strings;
  std::uint64_t guid = 0;
  bool has_guid = false;
};

struct ArenaTeamInspect {
  std::uint64_t guid = 0;
  struct Team {
    std::uint8_t bracket_index = 0;
    std::uint32_t team_id = 0;
    std::uint32_t rating = 0;
    std::uint32_t games_played = 0;
    std::uint32_t games_won = 0;
    std::uint32_t season_games_played = 0;
    std::uint32_t season_games_won = 0;
  };
  Team team{};
};

struct ArenaError {
  std::uint32_t error_type = 0;
  std::uint8_t unk = 0;
};

struct JoinedBGQueue {
  std::uint32_t bg_type = 0;
  std::uint8_t unk = 0;
  std::uint32_t unk2 = 0;
};

struct PvpAfkResult {
  std::uint8_t offender_count = 0;
  std::uint8_t num_reported = 0;
  std::uint8_t num_needed = 0;
};

struct InspectHonorCache {
  std::uint16_t today_honorable_kills =
      std::numeric_limits<std::uint16_t>::max();
  std::uint32_t today_contribution =
      std::numeric_limits<std::uint32_t>::max();
  std::uint16_t yesterday_honorable_kills =
      std::numeric_limits<std::uint16_t>::max();
  std::uint32_t yesterday_contribution =
      std::numeric_limits<std::uint32_t>::max();
  std::uint32_t lifetime_honorable_kills =
      std::numeric_limits<std::uint32_t>::max();
  std::uint8_t lifetime_rank =
      std::numeric_limits<std::uint8_t>::max();

  void UpdateFromStats(const InspectHonorStats& stats) {
    today_honorable_kills = stats.today_honorable_kills;
    today_contribution = stats.today_contribution;
    yesterday_honorable_kills = stats.yesterday_honorable_kills;
    yesterday_contribution = stats.yesterday_contribution;
    lifetime_honorable_kills = stats.lifetime_honorable_kills;
    lifetime_rank = stats.lifetime_rank;
  }
};

class ArenaHandler {
 public:
  static constexpr std::size_t kInspectArenaTeamSlots = 3;

  explicit ArenaHandler(openwow::data::DBCacheRuntime& db_cache_runtime)
      : db_cache_runtime_(db_cache_runtime) {}

  void BeginInspect(std::uint64_t guid);
  void ClearInspectState();
  void EraseArenaTeamQuery(std::uint32_t team_id);
  void ClearArenaTeamQueryMirror();
  void ClearPendingQueriesOnLogout();
  [[nodiscard]] bool QueueArenaTeamUpdateQuery(std::uint32_t team_id);
  bool StoreInspectHonorStats(const InspectHonorStats& stats);
  [[nodiscard]] bool QueueInspectArenaTeamQuery(std::uint32_t team_id);

  bool HandleArenaTeamQueryResponse(const std::uint8_t* data, std::size_t len);
  bool HandleArenaTeamEvent(const std::uint8_t* data, std::size_t len);
  bool HandleInspectArenaTeams(const std::uint8_t* data, std::size_t len);
  bool HandleArenaError(const std::uint8_t* data, std::size_t len);
  bool HandleArenaTeamChangeFailedQueued(const std::uint8_t* data, std::size_t len);
  bool HandleArenaUnitDestroyed(const std::uint8_t* data, std::size_t len);
  bool HandleJoinedBattlegroundQueue(const std::uint8_t* data, std::size_t len);
  bool HandleBattlefieldPortDenied(const std::uint8_t* data, std::size_t len);
  bool HandleBattlegroundInfoThrottled(const std::uint8_t* data, std::size_t len);
  bool HandleRemovedFromPvpQueue(const std::uint8_t* data, std::size_t len);
  bool HandleReportPvpAfkResult(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] const ArenaTeamQueryResponse& last_team_query() const { return last_team_query_; }
  [[nodiscard]] const ArenaTeamEvent& last_team_event() const { return last_team_event_; }
  [[nodiscard]] const std::optional<ArenaTeamInspect>& last_arena_inspect() const { return last_arena_inspect_; }
  [[nodiscard]] const std::optional<ArenaError>& last_arena_error() const { return last_arena_error_; }
  [[nodiscard]] std::uint64_t inspect_target_guid() const { return inspect_target_guid_; }
  [[nodiscard]] bool HasInspectHonorData() const { return inspect_honor_data_ready_; }
  [[nodiscard]] bool IsInspectHonorRequestInFlight() const { return inspect_honor_request_in_flight_; }
  [[nodiscard]] const InspectHonorCache& inspect_honor_cache() const { return inspect_honor_cache_; }
  [[nodiscard]] const ArenaTeamInspect::Team* GetInspectArenaTeam(
      std::size_t index) const;
  [[nodiscard]] const ArenaTeamQueryResponse* FindArenaTeamQuery(std::uint32_t team_id) const;
  void MarkInspectHonorRequestSent() { inspect_honor_request_in_flight_ = true; }
  [[nodiscard]] std::uint32_t arena_change_failed_queued() const { return arena_change_failed_queued_; }
  [[nodiscard]] std::uint64_t arena_unit_destroyed_guid() const { return arena_unit_destroyed_guid_; }
  [[nodiscard]] const std::optional<JoinedBGQueue>& last_joined_bg_queue() const { return last_joined_bg_queue_; }
  [[nodiscard]] bool battlefield_port_denied() const { return battlefield_port_denied_; }
  [[nodiscard]] bool battleground_info_throttled() const { return battleground_info_throttled_; }
  [[nodiscard]] bool removed_from_pvp_queue() const { return removed_from_pvp_queue_; }
  [[nodiscard]] const std::optional<PvpAfkResult>& last_pvp_afk_result() const { return last_pvp_afk_result_; }

  void Clear();

 private:
  openwow::data::DBCacheRuntime& db_cache_runtime_;

  enum class ArenaTeamQueryCallbackKind {
    kArenaTeamUpdate,
    kInspectHonorUpdate,
  };

  struct PendingArenaTeamQueryCallbacks {
    std::size_t arena_team_update_count = 0;
    std::size_t inspect_honor_update_count = 0;
  };

  using Team = ArenaTeamInspect::Team;

  [[nodiscard]] bool QueueArenaTeamQuery(std::uint32_t team_id,
                                         ArenaTeamQueryCallbackKind callback_kind);
  void ResetInspectSnapshot();

  ArenaTeamQueryResponse last_team_query_{};
  ArenaTeamEvent last_team_event_{};
  std::optional<ArenaTeamInspect> last_arena_inspect_;
  std::optional<ArenaError> last_arena_error_;
  std::uint64_t inspect_target_guid_{0};
  InspectHonorCache inspect_honor_cache_{};
  bool inspect_honor_data_ready_{false};
  bool inspect_honor_request_in_flight_{false};
  std::array<Team, kInspectArenaTeamSlots> inspect_arena_teams_{};
  mutable std::unordered_map<std::uint32_t, ArenaTeamQueryResponse>
      arena_team_query_cache_;
  std::unordered_map<std::uint32_t, PendingArenaTeamQueryCallbacks>
      pending_arena_team_query_callbacks_;
  std::unordered_set<std::uint32_t> inflight_arena_team_queries_;
  std::uint32_t arena_change_failed_queued_{0};
  std::uint64_t arena_unit_destroyed_guid_{0};
  std::optional<JoinedBGQueue> last_joined_bg_queue_;
  bool battlefield_port_denied_{false};
  bool battleground_info_throttled_{false};
  bool removed_from_pvp_queue_{false};
  std::optional<PvpAfkResult> last_pvp_afk_result_;
};

}

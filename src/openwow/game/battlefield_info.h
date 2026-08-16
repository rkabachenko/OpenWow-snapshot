
#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/tracked_unit_state_slice.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::ui {
class WorldMapSystem;
}

namespace openwow::data::dbc {
class DbcLoader;
struct WorldStateUIEntry;
}

namespace openwow::game {
class CGObject_C;
class ObjectManager;
class WorldSession;
class QueryCache;
struct PlayerNameInfo;
using WorldObject = CGObject_C;

struct BGQueueSlot {
  ObjectGuid bg_instance_guid;
  ObjectGuid bg_confirm_guid;
  std::uint32_t bg_type_id = 0;
  std::uint32_t confirm_time = 0;
  std::uint32_t status = 0;
  std::uint8_t arena_type = 0;
  std::uint8_t is_rated = 0;
  std::uint32_t map_id = 0;
  std::uint32_t expire_time = 0;
  std::uint32_t avg_wait = 0;
  std::uint32_t time_in_queue = 0;
  std::uint32_t client_instance = 0;
  bool is_registered = false;
};

static constexpr std::size_t kMaxBGQueueSlots = 2;

struct BGPlayerPosition {
  ObjectGuid guid;
  float x = 0.0f;
  float y = 0.0f;
};

struct BattlefieldPlayerMapPosition {
  ObjectGuid guid;
  float x = 0.0f;
  float y = 0.0f;
};

struct BGFlagPosition {
  ObjectGuid guid;
  float x = 0.0f;
  float y = 0.0f;
};

struct ArenaOpponent {
  ObjectGuid guid;
  ObjectGuid pet_guid;
  std::uint32_t vehicle_seat = 0;
  bool pvp_enabled = false;
  std::vector<std::uint32_t> aura_spell_ids;
  std::optional<TrackedControlledUnitStateSlice> pet_state;
};

static constexpr std::size_t kMaxArenaOpponents = 5;

struct BGScoreEntry {
  ObjectGuid player_guid;
  std::string player_name;
  std::uint32_t killing_blows = 0;
  std::uint32_t deaths = 0;
  std::uint32_t honorable_kills = 0;
  std::uint32_t bonus_honor = 0;
  std::uint32_t damage_done = 0;
  std::uint32_t healing_done = 0;
  std::uint8_t race_id = 0;
  std::uint8_t gender_id = 0;
  std::uint8_t class_id = 0;
  std::int32_t faction = 0;

  std::vector<std::uint32_t> bg_stats;
};

struct ArenaBattlefieldTeamInfo {
  std::string name;
  std::array<std::uint32_t, 3> raw_values{};
};

static constexpr std::size_t kBattlefieldArenaTeamCount = 2;

enum class WorldPvpQueueState : std::uint32_t {
  kNone = 0,
  kQueued = 1,
  kConfirm = 2,
  kActive = 3,
};

struct BFMgrQueueEntry {
  std::uint32_t queue_id = 0;
  std::uint32_t area_id = 0;
  WorldPvpQueueState state = WorldPvpQueueState::kNone;
  std::uint32_t expiry_time = 0;
};

struct WorldPvpQueueStatus {
  WorldPvpQueueState state = WorldPvpQueueState::kNone;
  std::uint32_t area_id = 0;
  std::uint32_t queue_id = 0;
  double time_left_seconds = 0.0;
};

static constexpr std::size_t kMaxBFMgrQueueSlots = 1;

class BattlefieldInfo {
public:
  static BattlefieldInfo &Get();
  void BindWorldMapSystem(openwow::ui::WorldMapSystem* world_map) noexcept {
    world_map_ = world_map;
  }

  bool HandleBattlefieldStatus(WorldSession &session,
                               const std::uint8_t *data, std::size_t len);
  [[nodiscard]] const BGQueueSlot &GetQueueSlot(std::size_t index) const;
  [[nodiscard]] std::uint32_t GetQueueSlotBattlemasterListId(std::size_t index) const;
  [[nodiscard]] std::uint32_t GetQueueSlotInstanceId(std::size_t index) const;
  [[nodiscard]] std::int32_t GetActiveSlotIndex() const {
    return active_slot_;
  }

  bool HandleGroupJoinedBattleground(const std::uint8_t *data, std::size_t len,
                                     QueryCache &query_cache,
                                     const std::function<void(std::uint64_t)> &send_name_query);
  bool ApplyGroupJoinedBattleground(std::int32_t result, std::uint64_t raw_guid,
                                    bool has_guid, QueryCache &query_cache,
                                    const std::function<void(std::uint64_t)> &send_name_query);

  bool HandleBGPlayerPositions(const std::uint8_t *data, std::size_t len);
  [[nodiscard]] std::uint32_t GetNumPlayerPositions() const {
    return num_positions_;
  }
  [[nodiscard]] std::uint32_t GetVisiblePlayerPositionCount(
      const ObjectManager &objects) const;
  [[nodiscard]] const BGPlayerPosition *GetPlayerPosition(std::uint32_t idx) const;
  [[nodiscard]] std::optional<BattlefieldPlayerMapPosition>
  GetVisiblePlayerMapPosition(const ObjectManager &objects,
                              std::uint32_t idx) const;
  [[nodiscard]] const PlayerNameInfo *
  GetBattlefieldPositionName(std::uint64_t raw_guid, QueryCache &query_cache,
                             const std::function<void(std::uint64_t)> &send_name_query);
  [[nodiscard]] std::uint32_t ConsumePendingBattlefieldPositionNameCallbacks(std::uint64_t raw_guid);
  void ClearPlayerPositions();
  [[nodiscard]] bool HasActiveBattlefieldInstance() const;
  [[nodiscard]] ObjectGuid GetActiveBattlefieldGuid() const;
  [[nodiscard]] bool CanRequestPlayerPositions(std::uint32_t now_tick) const;
  void MarkPlayerPositionsRequested(std::uint32_t now_tick);
  [[nodiscard]] bool CanRequestScoreData(std::uint32_t now_tick) const;
  void MarkScoreDataRequested(std::uint32_t now_tick);

  bool HandleBGPlayerJoinLeave(const std::uint8_t *data, std::size_t len, int system_message_id,
                               QueryCache &query_cache,
                               const std::function<void(std::uint64_t)> &send_name_query);
  bool OnBGPlayerStatusNameResolved(std::uint64_t raw_guid, const QueryCache &query_cache);
  [[nodiscard]] std::uint32_t GetNumFlags() const;
  [[nodiscard]] const BGFlagPosition *GetFlagPosition(std::uint32_t idx) const;

  bool HandleBfMgrQueueInvite(const ObjectManager &objects,
                              const std::uint8_t *data, std::size_t len);
  void ApplyBfMgrQueueInvite(const ObjectManager &objects,
                             std::uint32_t queue_id, std::uint8_t invite_flag,
                             std::uint8_t warmup, std::uint8_t cleared_afk);

  bool HandleBfMgrStateChange(const std::uint8_t *data, std::size_t len);
  void ApplyBfMgrStateChange(std::uint32_t queue_id, std::uint32_t area_id,
                             std::uint32_t expiry_time);

  bool HandleBfMgrEntryInvite(const std::uint8_t *data, std::size_t len);
  void ApplyBfMgrEntryInvite(std::uint32_t battle_id, std::uint8_t accepted);

  bool HandleBfMgrEntered(const ObjectManager &objects,
                          const std::uint8_t *data, std::size_t len);
  void ApplyBfMgrEntered(const ObjectManager &objects,
                         std::uint32_t battlefield_id, std::uint32_t area_id,
                         std::uint8_t status_flag, std::uint8_t secondary_flag,
                         std::uint8_t cleared_afk);

  bool HandleBfMgrQueueResponse(const std::uint8_t *data, std::size_t len);
  void ApplyBfMgrQueueResponse(std::uint32_t queue_id, std::uint8_t accepted);

  bool HandleBfMgrEjectPending(const std::uint8_t *data, std::size_t len);
  void ApplyBfMgrEjectPending(std::uint32_t queue_id, std::uint8_t reason,
                              std::uint8_t relocate_flag,
                              std::uint8_t battleground_flag);

  bool HandleBfMgrEjected(const std::uint8_t *data, std::size_t len);
  void ApplyBfMgrEjected(std::uint32_t queue_id, std::uint32_t reason);

  [[nodiscard]] const BFMgrQueueEntry &GetBfMgrQueueEntry(std::size_t index) const;
  [[nodiscard]] std::optional<WorldPvpQueueStatus>
  GetWorldPvpQueueStatus(const WorldSession &session,
                         std::size_t lua_index) const;

  void UpdateScoreUI(const ObjectManager &objects);
  void PromoteScoreSortColumn(const ObjectManager &objects, std::uint32_t column);
  void SetScoreEntries(const ObjectManager &objects,
                       std::vector<BGScoreEntry> entries, bool is_arena);
  void SetScoreEntries(std::vector<BGScoreEntry> entries, bool is_arena, QueryCache &query_cache,
                       const ObjectManager &objects,
                       const std::function<void(std::uint64_t)> &send_name_query);
  bool OnScoreNameResolved(std::uint64_t raw_guid, const QueryCache &query_cache,
                           const ObjectManager &objects);
  [[nodiscard]] const std::vector<BGScoreEntry> &GetScoreEntries() const {
    return score_entries_;
  }
  [[nodiscard]] const BGScoreEntry *GetDisplayedScoreEntry(std::size_t index) const;
  [[nodiscard]] std::uint32_t GetFilteredScoreCount() const {
    return filtered_score_count_;
  }
  void RefreshBattlegroundStatLayout(const openwow::data::dbc::DbcLoader &dbc);
  [[nodiscard]] std::uint32_t GetBattlegroundStatCount() const {
    return bg_stats_count_;
  }
  [[nodiscard]] const openwow::data::dbc::WorldStateUIEntry *
  GetBattlegroundStatInfo(const openwow::data::dbc::DbcLoader &dbc, std::size_t index) const;
  [[nodiscard]] std::uint32_t GetBattlegroundStatData(std::size_t player_index,
                                                      std::size_t stat_index) const;
  void SetArenaBattlefieldTeamInfo(std::size_t index, std::string name,
                                   std::array<std::uint32_t, 3> raw_values);
  [[nodiscard]] const ArenaBattlefieldTeamInfo &
  GetArenaBattlefieldTeamInfo(std::size_t index) const;
  void SetBattlefieldArenaFactionRaw(std::uint8_t faction) {
    battlefield_arena_faction_ = faction;
  }
  [[nodiscard]] std::uint8_t GetBattlefieldArenaFactionRaw() const {
    return battlefield_arena_faction_;
  }
  void SetBattlefieldWinnerValid(bool valid) {
    battlefield_winner_valid_ = valid;
  }
  void SetBattlefieldWinnerRaw(std::uint8_t winner) {
    battlefield_winner_ = winner;
  }
  [[nodiscard]] std::optional<std::uint8_t> GetBattlefieldWinner() const {
    if (!battlefield_winner_valid_) {
      return std::nullopt;
    }
    return battlefield_winner_;
  }

  [[nodiscard]] std::size_t GetArenaOpponentSlotCount() const;
  void OnArenaUnitUnseen(const ObjectGuid &guid, const ObjectManager &objects);
  void OnArenaUnitDestroyed(const ObjectGuid &guid, const ObjectManager &objects);
  void SetArenaOpponent(const ObjectManager &objects,
                        std::size_t slot, const ArenaOpponent &opp);
  void SetArenaOpponentPet(const ObjectManager &objects,
                           std::size_t slot, ObjectGuid pet_guid);
  void SetArenaOpponentPetState(
      std::size_t slot, TrackedControlledUnitStateSlice state);
  [[nodiscard]] const ArenaOpponent &GetArenaOpponent(std::size_t slot) const;
  [[nodiscard]] std::optional<std::size_t> FindArenaOpponentSlot(std::uint64_t guid) const;
  [[nodiscard]] bool ArenaOpponentHasAura(std::uint64_t guid,
                                          std::uint32_t spell_id) const;
  [[nodiscard]] bool GetArenaOpponentPvpFlag(std::uint64_t guid) const;
  [[nodiscard]] std::uint32_t GetArenaOpponentVehicleSeat(std::uint64_t guid) const;
  void SetArenaOpponentAuraSnapshot(std::uint64_t guid,
                                    std::vector<std::uint32_t> aura_spell_ids);
  void SetArenaOpponentPvpFlag(std::uint64_t guid, bool pvp_enabled);
  void SetArenaOpponentVehicleSeat(std::uint64_t guid, std::uint32_t vehicle_seat);

  bool GetBattlefieldPosition(std::uint32_t index, float &x, float &y) const;

  bool GetBattlefieldFlagPosition(std::uint32_t index, float &x, float &y) const;

  bool GetBattlefieldFlagMapPosition(const ObjectManager &objects,
                                     std::uint32_t index, float &x, float &y) const;

  [[nodiscard]] std::string_view GetBattlefieldFlagToken(
      const ObjectManager &objects, std::uint32_t index) const;

  [[nodiscard]] ObjectGuid GetFlagGuid(std::uint32_t index) const;

  [[nodiscard]] std::uint32_t GetBattlegroundInfoEntry(std::int32_t index) const;
  [[nodiscard]] std::uint32_t GetBattlegroundInfoCount() const {
    return static_cast<std::uint32_t>(bg_info_entries_.size());
  }
  void RefreshBattlegroundInfoEntries(
      const openwow::data::dbc::DbcLoader &dbc,
      const std::function<bool(std::uint32_t)> &is_holiday_active,
      bool force_sort);

  void ObserveBattlefieldVehicle(const WorldObject &object,
                                 const openwow::data::dbc::DbcLoader *dbc);
  void RemoveBattlefieldVehicle(ObjectGuid guid);
  [[nodiscard]] std::uint32_t GetBattlefieldVehicleCount() const;

  [[nodiscard]] ObjectGuid GetBattlefieldVehicleGuid(std::uint32_t index) const;

  [[nodiscard]] bool IsControllerRepresentedByBattlefieldVehicle(
      const ObjectManager &object_manager, ObjectGuid controller_guid) const;

  [[nodiscard]] std::uint32_t GetBattlefieldEstimatedWaitTime(std::size_t index) const;

  [[nodiscard]] std::uint32_t GetBattlefieldTimeWaited(std::size_t index) const;

  [[nodiscard]] std::uint32_t GetBattlefieldPortExpiration(std::size_t index) const;

  [[nodiscard]] std::uint32_t GetBattlefieldInstanceExpiration() const;

  [[nodiscard]] std::uint32_t GetBattlefieldInstanceRunTime() const;

  [[nodiscard]] std::uint32_t GetActiveBGMapId() const {
    return active_bg_map_id_;
  }
  [[nodiscard]] std::uint32_t GetActiveBGType() const {
    return active_bg_type_;
  }
  [[nodiscard]] std::uint32_t GetBattlefieldInstanceExpireTick() const {
    return battlefield_instance_expire_tick_;
  }
  [[nodiscard]] std::uint32_t GetBattlefieldInstanceStartTick() const {
    return battlefield_instance_start_tick_;
  }

  void SetFactionFilter(std::int32_t faction) {
    faction_filter_ = faction;
  }
  [[nodiscard]] std::int32_t GetFactionFilter() const {
    return faction_filter_;
  }

  void ResetForPlayerEnterWorld();
  void Reset();
  void SetDbcLoader(const openwow::data::dbc::DbcLoader *dbc) {
    dbc_loader_ = dbc;
  }

private:
  static constexpr std::size_t kBattlefieldScoreSortColumnCount = 17;
  struct PendingBGPlayerStatusMessage {
    std::uint64_t guid = 0;
    int system_message_id = 0;
  };

  BattlefieldInfo() = default;

  std::array<BGQueueSlot, kMaxBGQueueSlots> queue_slots_{};
  std::int32_t active_slot_ = -1;

  std::uint32_t num_positions_ = 0;
  std::vector<BGPlayerPosition> player_positions_;

  std::array<BGFlagPosition, 2> flag_positions_{};

  std::array<ArenaOpponent, kMaxArenaOpponents> arena_opponents_{};

  std::vector<BGScoreEntry> score_entries_;
  std::uint32_t filtered_score_count_ = 0;
  std::int32_t faction_filter_ = -1;

  bool score_entries_are_arena_ = false;
  std::uint32_t pending_score_name_queries_ = 0;
  std::unordered_set<std::uint64_t> pending_score_name_query_guids_;
  std::vector<PendingBGPlayerStatusMessage> pending_bg_player_status_messages_;
  std::unordered_map<std::uint64_t, std::uint32_t> pending_player_position_name_callbacks_;
  std::array<std::uint32_t, kBattlefieldScoreSortColumnCount> score_sort_columns_{
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  std::array<bool, kBattlefieldScoreSortColumnCount> score_sort_reverse_{};
  std::array<ArenaBattlefieldTeamInfo, kBattlefieldArenaTeamCount> arena_team_info_{};
  std::uint8_t battlefield_arena_faction_ = 0;

  bool battlefield_winner_valid_ = false;

  std::uint8_t battlefield_winner_ = 0;

  std::uint32_t active_bg_map_id_ = 0;
  std::uint32_t active_bg_type_ = 0;
  std::uint32_t battlefield_instance_expire_tick_ = 0;
  std::uint32_t battlefield_instance_start_tick_ = 0;
  std::uint32_t next_score_data_request_tick_ = 0;
  std::uint32_t next_player_positions_request_tick_ = 0;

  std::vector<std::uint32_t> bg_stats_columns_;

  std::uint32_t bg_stats_count_ = 0;

  const openwow::data::dbc::DbcLoader *dbc_loader_ = nullptr;
  openwow::ui::WorldMapSystem* world_map_ = nullptr;

  std::vector<std::uint32_t> bg_info_entries_;

  std::vector<ObjectGuid> battlefield_vehicle_guids_;
  BFMgrQueueEntry bf_mgr_queue_entry_{};

  [[nodiscard]] BFMgrQueueEntry *FindBfMgrQueueEntry(std::uint32_t queue_id);
  [[nodiscard]] const BFMgrQueueEntry *FindBfMgrQueueEntry(std::uint32_t queue_id) const;
  [[nodiscard]] BFMgrQueueEntry *FindOrReserveBfMgrQueueEntry(std::uint32_t queue_id);
  [[nodiscard]] bool IsCurrentWorldPvpArea(const ObjectManager &objects,
                                           std::uint32_t area_id) const;
  void ResetScoreSortOrder();
  [[nodiscard]] int CompareScoreEntries(const ObjectManager &objects,
                                        const BGScoreEntry &left,
                                        const BGScoreEntry &right) const;
  void UpsertBfMgrQueueEntry(std::uint32_t queue_id, WorldPvpQueueState state,
                             std::uint32_t area_id, std::uint32_t expiry_time);
};

}

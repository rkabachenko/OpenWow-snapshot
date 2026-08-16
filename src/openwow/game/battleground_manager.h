
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "openwow/game/battlefield_info.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class ObjectManager;

struct BattlefieldListInfo {
  std::uint64_t battlemaster_guid{0};
  std::uint8_t from_where{0};
  std::uint32_t bg_type_id{0};
  bool          is_random{false};
  bool          holiday_has_win{false};
  std::uint32_t holiday_winner_honor{0};
  std::uint32_t holiday_winner_arena{0};
  std::uint32_t holiday_loser_honor{0};
  bool          random_has_win{false};
  std::uint32_t random_winner_honor{0};
  std::uint32_t random_winner_arena{0};
  std::uint32_t random_loser_honor{0};
  std::vector<std::uint32_t> instance_ids;
};

struct PvpScoreEntry {
  std::uint64_t player_guid{0};
  std::uint32_t killing_blows{0};
  std::uint32_t honorable_kills{0};
  std::uint32_t deaths{0};
  std::uint32_t bonus_honor{0};
  std::uint32_t damage_done{0};
  std::uint32_t healing_done{0};
  std::vector<std::uint32_t> bg_objectives;

  std::uint8_t pvp_team_id{0};
};

struct PvpLogData {
  bool is_arena{false};
  bool is_ended{false};
  std::uint8_t winner{0};

  std::array<ArenaBattlefieldTeamInfo, kBattlefieldArenaTeamCount> arena_teams{};

  std::vector<PvpScoreEntry> scores;
};

struct PvpCreditInfo {
  std::uint32_t honor{0};
  std::uint64_t victim_guid{0};
  std::uint32_t victim_rank{0};
};

struct ArenaTeamMember {
  std::uint64_t guid{0};
  bool          online{false};
  std::string   name;
  std::uint32_t rank{0};
  std::uint8_t  level{0};
  std::uint8_t  class_id{0};
  std::uint32_t week_games{0};
  std::uint32_t week_wins{0};
  std::uint32_t season_games{0};
  std::uint32_t season_wins{0};
  std::uint32_t personal_rating{0};
  float         mmr_change{0.0f};

  float         mmr_value{0.0f};

};

struct ArenaTeamRoster {
  std::uint32_t team_id{0};
  std::uint32_t team_type{0};
  std::vector<ArenaTeamMember> members;

  [[nodiscard]] std::size_t OnlineMemberCount() const {
    std::size_t count = 0;
    for (const auto& member : members) {
      if (member.online) {
        ++count;
      }
    }
    return count;
  }
};

struct ArenaTeamStats {
  std::uint32_t team_id{0};
  std::uint32_t rating{0};
  std::uint32_t week_games{0};
  std::uint32_t week_wins{0};
  std::uint32_t season_games{0};
  std::uint32_t season_wins{0};
  std::uint32_t rank{0};
};

struct ArenaCommandResult {
  std::uint32_t action{0};
  std::string player_name;
  std::string team_name;
  std::uint32_t error_type{0};
};

struct ArenaTeamInvite {
  std::string inviter_name;
  std::string team_name;
};

using ArenaOpponentSlot = ArenaOpponent;
inline constexpr std::size_t kMaxArenaOpponentSlots = kMaxArenaOpponents;

enum class ArenaRosterSortField : std::uint8_t {
  kName = 0,
  kClass = 1,
  kPlayed = 2,
  kWins = 3,
  kSeasonPlayed = 4,
  kSeasonWon = 5,
  kRating = 6,
};

class BattlegroundManager {
 public:
  BattlegroundManager();

  bool HandleBattlefieldList(const std::uint8_t* data, std::size_t len);
  bool HandlePvpLogData(const std::uint8_t* data, std::size_t len);
  bool HandlePvpCredit(const std::uint8_t* data, std::size_t len);
  bool HandleArenaTeamRoster(const std::uint8_t* data, std::size_t len);
  bool HandleArenaTeamCommandResult(const std::uint8_t* data, std::size_t len);
  bool HandleArenaTeamStats(const std::uint8_t* data, std::size_t len);
  bool HandleArenaTeamInvite(const std::uint8_t* data, std::size_t len);

  const BattlefieldListInfo& battlefield_list() const { return bf_list_; }
  [[nodiscard]] std::uint64_t GetBattlefieldListBattlemasterGuid() const {
    return active_battlemaster_guid_;
  }
  void SetBattlefieldListBattlemasterGuid(const std::uint64_t guid) {
    active_battlemaster_guid_ = guid;
  }
  const PvpLogData& pvp_log() const { return pvp_log_; }
  const PvpCreditInfo& last_pvp_credit() const { return last_pvp_credit_; }
  const ArenaTeamRoster& GetArenaRoster(std::uint8_t slot) const;
  const ArenaTeamStats& arena_stats() const { return arena_stats_; }
  const ArenaCommandResult& arena_command() const { return arena_command_; }
  const ArenaTeamInvite& arena_invite() const { return arena_invite_; }
  [[nodiscard]] std::size_t GetArenaRosterMemberCount(
      std::uint8_t slot, bool include_offline) const;
  [[nodiscard]] const ArenaTeamMember* GetArenaRosterMember(
      std::uint8_t slot, std::size_t member_index) const;
  void SetSelectedBattlefieldListIndex(std::uint32_t index);
  [[nodiscard]] std::uint32_t GetSelectedBattlefieldListIndex() const;
  void SetArenaRosterSelection(std::uint8_t slot, std::size_t member_index);
  [[nodiscard]] int GetArenaRosterSelection(std::uint8_t slot) const;
  bool SetArenaRosterShowOffline(bool show_offline);
  [[nodiscard]] bool GetArenaRosterShowOffline() const {
    return arena_roster_show_offline_;
  }
  void SetDbcLoader(const data::dbc::DbcLoader* dbc) { dbc_ = dbc; }
  void SortArenaRosters(ArenaRosterSortField field);
  bool BeginArenaRosterRequest(std::uint8_t slot, std::uint32_t now_tick_ms);
  void ResetArenaRosterRequest(std::uint8_t slot);
  void ResetAllArenaRosterRequests();
  [[nodiscard]] ArenaOpponentSlot GetArenaOpponent(std::size_t slot) const;
  [[nodiscard]] std::size_t GetArenaOpponentSlotCount() const;
  [[nodiscard]] std::optional<std::size_t> FindArenaOpponentSlot(std::uint64_t guid) const;
  [[nodiscard]] bool HasArenaOpponentGuid(std::uint64_t guid) const;
  [[nodiscard]] bool ArenaOpponentHasAura(std::uint64_t guid, std::uint32_t spell_id) const;
  [[nodiscard]] bool GetArenaOpponentPvpFlag(std::uint64_t guid) const;
  [[nodiscard]] std::uint32_t GetArenaOpponentVehicleSeat(std::uint64_t guid) const;
  void NotifyArenaUnitUnseen(const ObjectGuid& guid, const ObjectManager& objects);
  void NotifyArenaUnitDestroyed(const ObjectGuid& guid, const ObjectManager& objects);
  void SetArenaOpponent(const ObjectManager& objects, std::size_t slot,
                        const ArenaOpponentSlot& opponent);
  void SetArenaOpponentPet(const ObjectManager& objects, std::size_t slot,
                           const ObjectGuid& pet_guid);
  void SetArenaOpponentPetState(
      std::size_t slot, TrackedControlledUnitStateSlice state);
  void SetArenaOpponentAuraSnapshot(std::uint64_t guid,
                                    std::vector<std::uint32_t> aura_spell_ids);
  void SetArenaOpponentPvpFlag(std::uint64_t guid, bool pvp_enabled);
  void SetArenaOpponentVehicleSeat(std::uint64_t guid, std::uint32_t vehicle_seat);

  void BeginBattlefieldListRequest(std::uint32_t bg_type_id);
  void Clear();

 private:
  struct ArenaRosterSortSpec {
    ArenaRosterSortField field{ArenaRosterSortField::kName};
    bool descending{false};
  };

  void ResetArenaRosterViewState();
  [[nodiscard]] int FindArenaRosterSlotByTeamId(std::uint32_t team_id) const;
  void SortArenaRoster(std::uint8_t slot);

  BattlefieldListInfo   bf_list_;
  std::uint64_t active_battlemaster_guid_{0};
  PvpLogData            pvp_log_;
  PvpCreditInfo         last_pvp_credit_;
  std::array<ArenaTeamRoster, 3> arena_rosters_{};
  ArenaTeamStats        arena_stats_;
  ArenaCommandResult    arena_command_;
  ArenaTeamInvite       arena_invite_;
  std::uint32_t selected_battlefield_instance_id_{0};
  std::uint64_t arena_roster_selection_guid_{0};
  bool arena_roster_show_offline_{true};
  std::array<ArenaRosterSortSpec, 7> arena_roster_sort_order_{};
  std::array<std::uint32_t, 3> arena_roster_request_deadlines_ms_{};
  const data::dbc::DbcLoader* dbc_{nullptr};
};

}

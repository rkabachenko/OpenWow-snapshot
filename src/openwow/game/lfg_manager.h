
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "openwow/network/protocol/wotlk/world_packet.h"

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class WorldSession;

inline constexpr std::uint8_t kLfgRoleLeader = 1;
inline constexpr std::uint8_t kLfgRoleTank = 2;
inline constexpr std::uint8_t kLfgRoleHealer = 4;
inline constexpr std::uint8_t kLfgRoleDps = 8;

enum class LfgUpdateType : std::uint8_t {
  kDefault = 0,
  kLeaderUnk1 = 1,
  kRolecheckAborted = 3,
  kJoinQueue = 4,
  kRolecheckFailed = 5,
  kRemovedFromQueue = 6,
  kProposalFailed = 7,
  kProposalDeclined = 8,
  kGroupFound = 9,
  kAddedToQueue = 10,
  kProposalBegin = 12,
  kUpdateStatus = 13,
  kGroupMemberOffline = 14,
  kGroupDisbandUnk16 = 15,
  kUnknown16 = 16,
};

struct LfgLockEntry {
  std::uint32_t dungeon_entry = 0;
  std::uint32_t lock_status = 0;
};

struct LfgPlayerLock {
  std::uint64_t player_guid = 0;
  std::vector<LfgLockEntry> locks;
};

struct LfgJoinResult {
  std::uint32_t result = 0;
  std::uint32_t state = 0;
  std::vector<LfgPlayerLock> player_locks;
};

struct LfgQueueStatus {
  std::uint32_t dungeon_id = 0;
  std::int32_t wait_time_avg = -1;
  std::int32_t wait_time = -1;
  std::int32_t wait_time_tank = -1;
  std::int32_t wait_time_healer = -1;
  std::int32_t wait_time_dps = -1;
  std::uint8_t tanks_needed = 0;
  std::uint8_t healers_needed = 0;
  std::uint8_t dps_needed = 0;
  std::uint32_t queued_time = 0;
};

struct LfgUpdateInfo {
  LfgUpdateType update_type = LfgUpdateType::kDefault;
  bool has_extra = false;
  bool joined = false;
  bool queued = false;
  bool raw_flag_4 = false;
  bool raw_flag_5 = false;
  std::array<std::uint8_t, 3> raw_tail_bytes{};
  std::vector<std::uint32_t> dungeons;
  std::string comment;

  void AddDungeonSelection(std::uint32_t packed_dungeon_id);
  [[nodiscard]] bool ContainsDungeonSelection(std::uint32_t packed_dungeon_id) const;
  [[nodiscard]] bool MatchesServerSnapshot(const LfgUpdateInfo& other) const;

  void SyncFrom(const LfgUpdateInfo& src);
};

struct LfgProposalPlayer {
  std::uint32_t role = 0;
  bool is_current_player = false;
  bool in_dungeon = false;
  bool same_group = false;
  bool has_answered = false;
  bool has_accepted = false;
};

struct LfgProposal {
  std::uint32_t dungeon_entry = 0;
  std::uint8_t state = 0;
  std::uint32_t proposal_id = 0;
  std::uint32_t encounter_mask = 0;
  bool silent = false;
  std::vector<LfgProposalPlayer> players;
};

struct LfgRoleCheckPlayer {
  std::uint64_t guid = 0;
  bool ready = false;
  std::uint32_t roles = 0;
  std::uint8_t level = 0;
};

struct LfgRoleCheckUpdate {
  std::uint32_t state = 0;
  bool is_beginning = false;
  std::vector<std::uint32_t> dungeons;
  std::vector<LfgRoleCheckPlayer> players;
};

struct LfgBootProposal {
  bool in_progress = false;
  bool did_vote = false;
  bool agree = false;
  std::uint64_t victim_guid = 0;
  std::uint32_t total_votes = 0;
  std::uint32_t agree_count = 0;
  std::uint32_t time_left = 0;
  std::uint32_t needed_votes = 0;
  std::string reason;
};

struct LfgRewardItem {
  std::uint32_t item_id = 0;
  std::uint32_t display_info_id = 0;
  std::uint32_t item_count = 0;
};

struct LfgPlayerReward {
  std::uint32_t random_dungeon_entry = 0;
  std::uint32_t completed_dungeon_entry = 0;
  bool is_first_reward = false;
  std::uint32_t strangers_count = 0;
  std::uint32_t base_money_reward = 0;
  std::uint32_t base_xp_reward = 0;
  std::uint32_t variable_money_reward = 0;
  std::uint32_t variable_xp_reward = 0;
  std::vector<LfgRewardItem> items;
};

enum class LfgSearchSortKey : std::uint8_t {
  kZone = 0,
  kLevel = 1,
  kClass = 2,
  kName = 3,
  kTank = 4,
  kHealer = 5,
  kDamage = 6,
};

struct LfgSearchPlayerResult {
  std::uint64_t guid = 0;

  std::uint64_t resolved_group_guid = 0;
  std::uint64_t secondary_guid = 0;
  std::uint32_t secondary_mask = 0;
  std::string comment;
  std::uint8_t level = 0;
  std::uint8_t raw_u8_45 = 0;
  std::uint8_t raw_u8_46 = 0;
  std::array<std::uint8_t, 3> raw_u8_47_49{};
  std::array<std::uint32_t, 6> raw_u32_52_72{};
  std::array<float, 2> raw_f32_76_80{};
  std::array<std::uint32_t, 5> raw_u32_84_100{};
  float raw_f32_104 = 0.0f;
  std::array<std::uint32_t, 6> raw_u32_108_128{};
  std::uint32_t area_id = 0;
  std::uint8_t role_byte = 0;
  bool joined_group = false;
  std::uint8_t search_flags = 0;
  std::uint64_t update_sequence = 0;
};

struct LfgSearchGroupResult {
  std::uint64_t guid = 0;
  std::string comment;
  std::array<std::uint8_t, 3> raw_u8_292_294{};
  std::uint64_t encounter_guid = 0;
  std::uint32_t encounter_mask = 0;
  std::vector<std::uint64_t> member_guids;
};

struct LfgRoleChosen {
  std::uint64_t guid = 0;
  std::uint32_t roles = 0;
  std::uint8_t ready = 0;
};

struct LfgPlayerDungeonState {
  std::uint32_t packed_dungeon_id = 0;
  bool reward_done = false;
  std::uint32_t reward_money = 0;
  std::uint32_t reward_xp = 0;
  std::uint32_t reward_money_var = 0;
  std::uint32_t reward_xp_var = 0;
  std::vector<LfgRewardItem> rewards;
  bool locked = false;
  std::uint32_t lock_reason = 0;
};

struct LfgPartyLockInfo {
  std::uint64_t guid = 0;
  std::vector<LfgLockEntry> locks;
};

class LfgManager {
public:

  bool HandleLfgJoinResult(const std::uint8_t *data, std::size_t len);
  bool HandleLfgQueueStatus(const std::uint8_t *data, std::size_t len);
  bool HandleLfgUpdatePlayer(const std::uint8_t *data, std::size_t len);
  bool HandleLfgUpdateParty(const std::uint8_t *data, std::size_t len);
  bool HandleLfgProposalUpdate(const std::uint8_t *data, std::size_t len);
  bool HandleLfgRoleCheckUpdate(const std::uint8_t *data, std::size_t len);
  bool HandleLfgBootProposalUpdate(const std::uint8_t *data, std::size_t len);
  bool HandleLfgPlayerReward(const std::uint8_t *data, std::size_t len);
  bool HandleLfgTeleportDenied(const std::uint8_t *data, std::size_t len);
  bool HandleLfgOfferContinue(const std::uint8_t *data, std::size_t len);
  bool HandleLfgPlayerInfo(const std::uint8_t *data, std::size_t len);
  bool HandleLfgPartyInfo(const std::uint8_t *data, std::size_t len);
  bool HandleLfgRoleChosen(const std::uint8_t *data, std::size_t len);
  bool HandleLfgUpdateSearch(const std::uint8_t *data, std::size_t len);
  bool HandleLfgDisabled(const std::uint8_t *data, std::size_t len);
  bool HandleOpenLfgDungeonFinder(const std::uint8_t *data, std::size_t len);
  bool HandleUpdateLfgList(const std::uint8_t *data, std::size_t len);

  static net::wotlk::WorldPacket BuildLfgJoin(std::uint32_t roles, bool no_partial,
                                              bool achievements,
                                              const std::vector<std::uint32_t> &dungeons,
                                              const std::string &comment);
  static net::wotlk::WorldPacket BuildLfgLeave();
  static net::wotlk::WorldPacket BuildLfgProposalResult(std::uint32_t proposal_id, bool accept);
  static net::wotlk::WorldPacket BuildLfgSetRoles(std::uint8_t roles);
  static net::wotlk::WorldPacket BuildLfgSetComment(const std::string &comment);
  static net::wotlk::WorldPacket BuildLfgSetBootVote(bool agree);
  static net::wotlk::WorldPacket BuildLfgTeleport(bool out);

  [[nodiscard]] const std::optional<LfgJoinResult> &join_result() const {
    return join_result_;
  }
  [[nodiscard]] const std::optional<LfgQueueStatus> &queue_status() const {
    return queue_status_;
  }
  [[nodiscard]] const std::optional<LfgUpdateInfo> &player_update() const {
    return player_update_;
  }
  [[nodiscard]] const std::optional<LfgUpdateInfo> &party_update() const {
    return party_update_;
  }
  [[nodiscard]] const std::optional<LfgProposal> &proposal() const {
    return proposal_;
  }
  [[nodiscard]] const std::optional<LfgRoleCheckUpdate> &role_check() const {
    return role_check_;
  }
  [[nodiscard]] const std::optional<LfgBootProposal> &boot_proposal() const {
    return boot_proposal_;
  }
  [[nodiscard]] const std::optional<LfgPlayerReward> &player_reward() const {
    return player_reward_;
  }
  [[nodiscard]] const LfgRewardItem *FindCompletionRewardItemByIndex(
      std::size_t reward_index) const;
  [[nodiscard]] std::uint32_t teleport_error() const {
    return teleport_error_;
  }
  [[nodiscard]] std::uint32_t offer_continue_dungeon() const {
    return offer_continue_dungeon_;
  }
  [[nodiscard]] const std::vector<std::uint8_t> &lfg_player_info_blob() const {
    return lfg_player_info_blob_;
  }
  [[nodiscard]] const std::vector<std::uint8_t> &lfg_party_info_blob() const {
    return lfg_party_info_blob_;
  }
  [[nodiscard]] const std::optional<LfgRoleChosen> &last_role_chosen() const {
    return last_role_chosen_;
  }
  [[nodiscard]] bool has_player_dungeon_info() const {
    return has_player_dungeon_info_;
  }
  [[nodiscard]] const LfgPlayerDungeonState *FindPlayerDungeonState(
      std::uint32_t packed_dungeon_id) const;
  [[nodiscard]] const LfgRewardItem *FindPlayerDungeonRewardItemByIndex(
      std::uint32_t packed_dungeon_id, std::size_t reward_index) const;
  [[nodiscard]] bool has_party_lock_info() const {
    return has_party_lock_info_;
  }
  [[nodiscard]] std::size_t GetLfdLockPlayerCount() const;
  [[nodiscard]] std::optional<std::uint32_t> FindPlayerLockReason(
      std::uint32_t dungeon_id) const;
  [[nodiscard]] const LfgPartyLockInfo *GetPartyLockInfo(std::size_t member_index) const;
  [[nodiscard]] const LfgPartyLockInfo *FindPartyLockByGuid(std::uint64_t guid) const;
  [[nodiscard]] std::optional<std::uint32_t> FindPartyLockReason(
      std::size_t member_index, std::uint32_t dungeon_id) const;
  [[nodiscard]] std::vector<LfgLockEntry> GetChoiceLockEntries() const;
  [[nodiscard]] bool HasUnlockedPlayerDungeon(std::uint32_t packed_dungeon_id) const;
  [[nodiscard]] bool IsDungeonJoinable(std::uint32_t packed_dungeon_id) const;
  [[nodiscard]] std::vector<std::uint32_t> GetAvailableRandomDungeonIds(
      const openwow::data::dbc::DbcLoader &dbc) const;
  [[nodiscard]] std::optional<std::uint32_t> GetBestRandomDungeonId(
      const openwow::data::dbc::DbcLoader &dbc, std::uint8_t expansion_level) const;
  [[nodiscard]] std::uint8_t lfg_update_search() const {
    return lfg_update_search_;
  }
  [[nodiscard]] bool lfg_disabled() const {
    return lfg_disabled_;
  }
  [[nodiscard]] std::uint32_t open_lfg_dungeon_id() const {
    return open_lfg_dungeon_id_;
  }
  [[nodiscard]] const std::vector<std::uint8_t> &update_lfg_list_blob() const {
    return update_lfg_list_blob_;
  }
  [[nodiscard]] bool has_search_results() const {
    return has_search_results_;
  }
  [[nodiscard]] std::uint32_t joined_search_id() const {
    return joined_search_id_;
  }
  [[nodiscard]] std::uint32_t active_search_id() const {
    return active_search_id_;
  }
  [[nodiscard]] std::uint32_t search_result_count() const;
  [[nodiscard]] std::uint32_t search_result_total_count() const;
  [[nodiscard]] bool search_players_first() const {
    return search_players_first_;
  }
  [[nodiscard]] const std::array<std::pair<LfgSearchSortKey, bool>, 7> &search_sort_order() const {
    return search_sort_order_;
  }
  [[nodiscard]] std::vector<const LfgSearchGroupResult *> SearchGroups() const;
  [[nodiscard]] std::vector<const LfgSearchPlayerResult *> StandaloneSearchPlayers() const;
  [[nodiscard]] const LfgSearchGroupResult *FindSearchGroup(std::uint64_t guid) const;
  [[nodiscard]] const LfgSearchPlayerResult *FindSearchPlayer(std::uint64_t guid) const;
  [[nodiscard]] const LfgSearchPlayerResult *GetSearchPrimaryPlayer(std::uint64_t group_guid) const;
  [[nodiscard]] const LfgSearchPlayerResult *GetSearchGroupMember(std::uint64_t group_guid,
                                                                  std::size_t member_index) const;
  void StartSearchBrowse(std::uint32_t packed_search_id);
  void StopSearchBrowse();
  void RefreshPublishedSearchResults();
  void ToggleSearchGroupOrdering();
  void PromoteSearchSortKey(LfgSearchSortKey key);
  void ResortSearchResults(WorldSession *session, const openwow::data::dbc::DbcLoader *dbc);
  void QueueMissingSearchPlayerNameQueries(
      WorldSession *session, const std::function<void(std::uint64_t)> &send_name_query);
  void ApplyProposalResponse(bool accept);
  void ClearProposal();
  void ResetProposalEventGateForPlayerEnterWorld();
  [[nodiscard]] bool ConsumeProposalShowPending();
  [[nodiscard]] bool ResolvePendingSearchPlayerNameQuery(std::uint64_t guid);
  [[nodiscard]] std::uint64_t published_search_generation() const {
    return published_search_generation_;
  }

  void ClearServerInfoSnapshots();
  void ClearPlayerReward();
  void Clear();
  void ClearQueueStatus();

private:
  void RemoveWorkingSearchResult(std::uint64_t guid);
  void RemoveWorkingGroup(std::uint64_t guid);
  void RemoveWorkingPlayer(std::uint64_t guid);
  void RemoveWorkingStandalonePlayer(std::uint64_t guid);
  void AppendWorkingStandalonePlayer(std::uint64_t guid);
  void RemoveWorkingGroupMember(std::uint64_t group_guid, std::uint64_t player_guid);
  void InsertWorkingGroupMember(std::uint64_t group_guid, std::uint64_t player_guid,
                                bool joined_group);
  void PublishSearchResults();
  void ClearPendingSearchPlayerNameQueries();
  void ResetWorkingSearchResults();
  void ResetPublishedSearchResults();
  void ResetSearchResultData();
  void ResetSearchOrdering();
  void ResetSearchResults();

  std::optional<LfgJoinResult> join_result_;
  std::optional<LfgQueueStatus> queue_status_;
  std::optional<LfgUpdateInfo> player_update_;
  std::optional<LfgUpdateInfo> party_update_;
  std::optional<LfgProposal> proposal_;
  std::uint32_t last_proposal_event_id_ = 0;
  bool proposal_show_pending_ = false;
  std::optional<LfgRoleCheckUpdate> role_check_;
  std::optional<LfgBootProposal> boot_proposal_;
  std::optional<LfgPlayerReward> player_reward_;
  std::uint32_t teleport_error_ = 0;
  std::uint32_t offer_continue_dungeon_ = 0;
  std::vector<std::uint8_t> lfg_player_info_blob_;
  std::vector<std::uint8_t> lfg_party_info_blob_;
  std::optional<LfgRoleChosen> last_role_chosen_;
  bool has_player_dungeon_info_{false};
  bool has_party_lock_info_{false};
  std::vector<LfgPlayerDungeonState> player_dungeon_states_;
  std::vector<LfgPartyLockInfo> party_lock_info_;
  std::uint8_t lfg_update_search_{0};
  bool lfg_disabled_{false};
  std::uint32_t open_lfg_dungeon_id_{0};
  std::vector<std::uint8_t> update_lfg_list_blob_;
  std::uint32_t working_search_id_{0};
  std::uint32_t working_reported_group_total_{0};
  std::uint32_t working_reported_player_total_{0};
  std::vector<std::uint64_t> pending_search_player_name_query_requests_;
  std::unordered_set<std::uint64_t> pending_search_player_name_guids_;
  std::unordered_map<std::uint64_t, LfgSearchGroupResult> working_search_groups_;
  std::unordered_map<std::uint64_t, LfgSearchPlayerResult> working_search_players_;
  std::vector<std::uint64_t> working_group_order_;
  std::vector<std::uint64_t> working_standalone_player_order_;
  bool has_search_results_{false};
  std::uint32_t joined_search_id_{0};
  std::uint32_t active_search_id_{0};
  std::uint32_t reported_group_total_{0};
  std::uint32_t reported_player_total_{0};
  std::unordered_map<std::uint64_t, LfgSearchGroupResult> search_groups_;
  std::unordered_map<std::uint64_t, LfgSearchPlayerResult> search_players_;
  std::vector<std::uint64_t> published_group_order_;
  std::vector<std::uint64_t> published_standalone_player_order_;
  std::uint64_t published_search_generation_{0};
  bool search_players_first_{true};
  std::array<std::pair<LfgSearchSortKey, bool>, 7> search_sort_order_{{
      {LfgSearchSortKey::kZone, false},
      {LfgSearchSortKey::kLevel, false},
      {LfgSearchSortKey::kClass, false},
      {LfgSearchSortKey::kName, false},
      {LfgSearchSortKey::kTank, false},
      {LfgSearchSortKey::kHealer, false},
      {LfgSearchSortKey::kDamage, false},
  }};
};

}

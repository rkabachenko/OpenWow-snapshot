
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "openwow/game/async_query_channel.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/quest_types.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

namespace openwow::data {
class DBCacheRuntime;
class WDBCache;
}
namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class CGPlayer_C;
class ObjectManager;

struct QuestRewardItem {
  std::uint32_t item_id = 0;
  std::uint32_t count = 0;
  std::uint32_t display_info_id = 0;
};

struct QuestRewardFactionPreviewEntry {
  std::int32_t faction_id = 0;
  std::int32_t amount = 0;
};

struct QuestObjective {
  std::int32_t creature_or_go = 0;
  std::uint32_t required_count = 0;
  std::uint32_t current_count = 0;
  std::string text;
};

struct QuestItemObjective {
  std::uint32_t item_id = 0;
  std::uint32_t required_count = 0;
};

struct QuestDialogEmoteEntry {
  std::uint32_t delay = 0;
  std::uint32_t emote_id = 0;
};

struct QuestTemplate {
  std::uint32_t quest_id = 0;
  std::uint32_t quest_method = 0;
  std::int32_t quest_level = 0;
  std::uint32_t min_level = 0;
  std::uint32_t zone_or_sort = 0;
  std::uint32_t type = 0;
  std::uint32_t suggested_players = 0;
  std::uint32_t required_reputation_faction = 0;
  std::int32_t required_reputation_value = 0;
  std::uint32_t required_reputation_faction_max = 0;
  std::int32_t required_reputation_value_max = 0;
  std::uint32_t next_quest_in_chain = 0;
  std::uint32_t xp_id = 0;
  std::uint32_t reward_money = 0;
  std::uint32_t rew_money_max_level = 0;
  std::uint32_t rew_spell = 0;
  std::int32_t rew_spell_cast = 0;
  std::uint32_t rew_honor_addition = 0;
  float rew_honor_multiplier = 0.0f;
  std::uint32_t src_item_id = 0;
  QuestFlags flags = QuestFlags::kNone;
  std::uint32_t char_title_id = 0;
  std::uint32_t required_player_kills = 0;
  std::uint32_t bonus_talents = 0;
  std::uint32_t rew_arena_points = 0;

  QuestRewardItem reward_items[kQuestRewardsCount] = {};
  QuestRewardItem reward_choice_items[kQuestRewardChoicesCount] = {};
  std::uint32_t reward_faction_id[kQuestReputationsCount] = {};
  std::int32_t reward_faction_value[kQuestReputationsCount] = {};
  std::int32_t reward_faction_value_override[kQuestReputationsCount] = {};
  std::uint32_t reward_faction_control = 0;

  float poi_x = 0.0f, poi_y = 0.0f;
  std::uint32_t poi_continent = 0;
  std::uint32_t poi_option = 0;

  std::string title;
  std::string objectives;
  std::string details;
  std::string area_description;
  std::string completed_text;

  QuestObjective npc_or_go_objectives[kQuestObjectivesCount] = {};
  QuestItemObjective item_objectives[kQuestItemObjectivesCount] = {};
  QuestItemObjective item_drop_objectives[kQuestRewardsCount] = {};
};

struct QuestLogEntry {
  std::uint32_t quest_id = 0;
  std::uint8_t slot = 0;
  QuestStatus status = QuestStatus::kIncomplete;

  std::uint32_t kill_counts[kQuestObjectivesCount] = {};
  bool is_timed = false;
  std::uint32_t timer_ms = 0;
  bool timer_expiration_reported = false;
};

struct QuestDetailsDialog {
  ObjectGuid npc_guid;
  ObjectGuid sharer_guid;
  std::uint32_t quest_id = 0;
  std::string title;
  std::string details;
  std::string objectives;
  bool auto_accept = false;
  QuestFlags quest_flags = QuestFlags::kNone;
  std::uint32_t suggested_players = 0;
  std::vector<QuestRewardItem> reward_choice_items;
  std::vector<QuestRewardItem> reward_items;
  std::uint32_t reward_money = 0;
  std::uint32_t reward_xp = 0;
  std::uint32_t reward_honor = 0;
  std::uint32_t rew_spell = 0;
  std::int32_t rew_spell_cast = 0;
  std::uint32_t char_title_id = 0;
  std::uint32_t bonus_talents = 0;
  std::uint32_t rew_arena_points = 0;
  std::uint32_t accept_packet_value = 0;

  std::uint32_t reward_faction_id[kQuestReputationsCount] = {};
  std::int32_t reward_faction_value[kQuestReputationsCount] = {};
  std::int32_t reward_faction_value_override[kQuestReputationsCount] = {};
  std::uint32_t reward_faction_control = 0;

  std::vector<QuestDialogEmoteEntry> emotes;
};

struct QuestOfferRewardDialog {
  ObjectGuid npc_guid;
  std::uint32_t quest_id = 0;
  std::string title;
  std::string reward_text;
  bool close_on_decline = false;
  bool enable_next = false;
  QuestFlags quest_flags = QuestFlags::kNone;
  std::uint32_t suggested_players = 0;
  std::vector<QuestDialogEmoteEntry> emotes;
  std::vector<QuestRewardItem> reward_choice_items;
  std::vector<QuestRewardItem> reward_items;
  std::uint32_t reward_money = 0;
  std::uint32_t required_money = 0;
  std::uint32_t reward_xp = 0;
  std::uint32_t reward_honor = 0;
  std::uint32_t rew_spell = 0;
  std::int32_t rew_spell_cast = 0;
  std::uint32_t char_title_id = 0;
  std::uint32_t bonus_talents = 0;
  std::uint32_t rew_arena_points = 0;

  std::uint32_t reward_faction_id[kQuestReputationsCount] = {};
  std::int32_t reward_faction_value[kQuestReputationsCount] = {};
  std::int32_t reward_faction_value_override[kQuestReputationsCount] = {};
  std::uint32_t reward_faction_control = 0;
};

struct QuestRequestItemsDialog {
  ObjectGuid npc_guid;
  std::uint32_t quest_id = 0;
  std::string title;
  std::string request_text;
  bool close_on_decline = false;
  QuestFlags quest_flags = QuestFlags::kNone;
  std::uint32_t suggested_players = 0;
  std::vector<QuestDialogEmoteEntry> emotes;
  std::uint32_t required_money = 0;
  std::vector<QuestRewardItem> required_items;
  bool is_completable = false;
};

struct QuestConfirmAcceptPrompt {
  std::uint32_t quest_id = 0;
  std::string title;
  ObjectGuid sharer_guid;
};

struct QuestFrameInteractionState {
  ObjectGuid interaction_guid;
  ObjectGuid secondary_guid;
  std::uint32_t quest_id = 0;
};

struct PendingQuestRewardSelection {
  std::uint32_t quest_id = 0;
  std::uint32_t item_id = 0;
};

struct QuestDialogTextState {
  std::string title_text;
  bool has_title_text = false;
  std::string greeting_text;
  bool has_greeting_text = false;
  std::string quest_text;
  bool has_quest_text = false;
  std::string objective_text;
  bool has_objective_text = false;
  std::string progress_text;
  bool has_progress_text = false;
  std::string reward_text;
  bool has_reward_text = false;

  void Clear() {
    title_text.clear();
    has_title_text = false;
    greeting_text.clear();
    has_greeting_text = false;
    quest_text.clear();
    has_quest_text = false;
    objective_text.clear();
    has_objective_text = false;
    progress_text.clear();
    has_progress_text = false;
    reward_text.clear();
    has_reward_text = false;
  }
};

struct QuestGiverStatusEntry {
  ObjectGuid guid;
  QuestGiverStatus status = QuestGiverStatus::kNone;
};

struct QuestPoiPoint {
  std::int32_t x = 0;
  std::int32_t y = 0;
};

struct QuestPoiEntry {
  std::uint32_t poi_id = 0;
  std::int32_t objective_index = 0;
  std::uint32_t map_id = 0;
  std::uint32_t area_id = 0;
  std::uint32_t floor_id = 0;
  std::uint32_t unk3 = 0;
  std::uint32_t unk4 = 0;
  std::vector<QuestPoiPoint> points;
};

struct QuestPoiData {
  std::uint32_t quest_id = 0;
  std::vector<QuestPoiEntry> pois;
};

struct QuestPushResultInfo {
  std::uint64_t player_guid = 0;
  std::uint8_t msg = 0;
};

struct QuestFailedInfo {
  std::uint32_t quest_id = 0;
  std::uint32_t reason = 0;
};

struct QuestPvpKillInfo {
  std::uint32_t quest_id = 0;
  std::uint32_t current_count = 0;
  std::uint32_t required_count = 0;
};

struct QuestAddItem {
  std::uint32_t item_entry = 0;
  std::uint32_t count = 0;
};

class QuestManager {
public:
  static constexpr std::size_t kMaxRewardFactionPreviewEntries = 10;

  using QuestQueryDispatchFn = std::function<void(std::uint32_t quest_id)>;
  using QueryCallback = AsyncQueryChannel::Callback;
  using QueryRequestOptions = AsyncQueryChannel::RequestOptions;
  using DialogTextExpansionFn =
      std::function<std::string(std::string_view, bool)>;

  QuestManager(openwow::data::DBCacheRuntime& db_cache_runtime,
               const data::dbc::DbcLoader& dbc_loader,
               DialogTextExpansionFn expand_dialog_text);

  bool HandleQuestQueryResponse(const std::uint8_t *data, std::size_t len);
  bool HandleQuestGiverQuestDetails(const std::uint8_t *data, std::size_t len);
  bool HandleQuestGiverOfferReward(const std::uint8_t *data, std::size_t len);
  bool HandleQuestGiverRequestItems(const std::uint8_t *data, std::size_t len);
  bool HandleQuestGiverStatus(const std::uint8_t *data, std::size_t len);
  bool HandleQuestGiverStatusMultiple(const std::uint8_t *data, std::size_t len);
  bool HandleQuestUpdateComplete(const std::uint8_t *data, std::size_t len);
  bool HandleQuestUpdateAddKill(const std::uint8_t *data, std::size_t len);
  bool HandleQuestConfirmAccept(const std::uint8_t *data, std::size_t len);
  bool HandleQuestLogFull();
  bool HandleQuestPoiQueryResponse(const std::uint8_t *data, std::size_t len);
  bool HandleQuestPushResult(const std::uint8_t *data, std::size_t len);
  bool HandleQuestGiverQuestFailed(const std::uint8_t *data, std::size_t len);
  bool HandleQuestUpdateFailed(const std::uint8_t *data, std::size_t len);
  bool HandleQuestUpdateAddPvpKill(const std::uint8_t *data, std::size_t len);

  bool HandleQuestForceRemove(const std::uint8_t *data, std::size_t len);
  bool HandleQuestgiverQuestInvalid(const std::uint8_t *data, std::size_t len);
  bool HandleQuestUpdateAddItem(const std::uint8_t *data, std::size_t len);
  bool HandleQuestUpdateFailedTimer(const std::uint8_t *data, std::size_t len);
  bool HandleQueryQuestsCompleted(const std::uint8_t *data, std::size_t len);

  [[nodiscard]] static net::wotlk::WorldPacket BuildQuestQuery(std::uint32_t quest_id);
  [[nodiscard]] static net::wotlk::WorldPacket BuildQuestGiverHello(const ObjectGuid &npc);
  [[nodiscard]] static net::wotlk::WorldPacket BuildCompleteQuest(const ObjectGuid &npc,
                                                                  std::uint32_t quest_id);
  [[nodiscard]] static net::wotlk::WorldPacket
  BuildChooseReward(const ObjectGuid &npc, std::uint32_t quest_id, std::uint32_t choice);

  void AddQuestToLog(std::uint32_t quest_id);
  void RemoveQuestFromLog(std::uint32_t quest_id);
  void SyncQuestLogFromPlayer(const ObjectManager& objects, const CGPlayer_C &player);
  [[nodiscard]] bool IsQuestInLog(std::uint32_t quest_id) const;
  [[nodiscard]] std::size_t quest_log_count() const {
    return quest_log_.size();
  }
  [[nodiscard]] const std::vector<QuestLogEntry> &quest_log() const {
    return quest_log_;
  }
  [[nodiscard]] QuestLogEntry *FindQuestLogEntry(std::uint32_t quest_id);
  [[nodiscard]] const QuestLogEntry *FindQuestLogEntry(std::uint32_t quest_id) const;

  [[nodiscard]] const QuestTemplate *GetTemplate(std::uint32_t quest_id) const;
  [[nodiscard]] const QuestTemplate *GetOrRequestTemplate(std::uint32_t quest_id);
  [[nodiscard]] const QuestTemplate *GetOrRequestTemplate(std::uint32_t quest_id,
                                                          QueryRequestOptions options);

  [[nodiscard]] bool HydrateRetailQuestCache(openwow::data::WDBCache &cache);

  [[nodiscard]] const char *GetQuestCacheTitle(
      std::uint32_t quest_id,
      AsyncQueryChannel::Callback on_miss = nullptr);
  [[nodiscard]] std::size_t template_cache_size() const {
    return template_cache_.size();
  }
  void SetQuestQueryDispatcher(QuestQueryDispatchFn dispatcher);
  void SetTickCountProvider(std::function<std::uint32_t()> provider);
  void PumpDispatchQueues(std::uint32_t current_tick_ms);
  void SetQuestQueryMaxInFlight(std::uint32_t max_in_flight);
  void MarkQuestQueryPending(std::uint32_t quest_id);
  [[nodiscard]] bool IsQuestQueryPending(std::uint32_t quest_id) const;
  void ClearPendingQueriesOnLogout();
  void HandleClientCacheVersionInvalidation();

  [[nodiscard]] bool has_active_details() const {
    return active_details_.has_value();
  }
  [[nodiscard]] const QuestDetailsDialog &active_details() const {
    return active_details_.value();
  }
  [[nodiscard]] bool has_active_reward() const {
    return active_reward_.has_value();
  }
  [[nodiscard]] const QuestOfferRewardDialog &active_reward() const {
    return active_reward_.value();
  }
  [[nodiscard]] bool has_active_request() const {
    return active_request_.has_value();
  }
  [[nodiscard]] const QuestRequestItemsDialog &active_request() const {
    return active_request_.value();
  }
  [[nodiscard]] bool has_pending_confirm_accept() const {
    return pending_confirm_accept_.has_value();
  }
  [[nodiscard]] const QuestConfirmAcceptPrompt &pending_confirm_accept() const {
    return pending_confirm_accept_.value();
  }
  [[nodiscard]] const QuestDialogTextState &dialog_text() const {
    return dialog_text_;
  }
  [[nodiscard]] const QuestFrameInteractionState &quest_frame_interaction_state() const {
    return quest_frame_interaction_state_;
  }
  [[nodiscard]] bool is_dialog_action_pending() const {
    return dialog_action_pending_;
  }
  [[nodiscard]] std::size_t reward_faction_preview_count() const {
    return reward_faction_preview_count_;
  }
  [[nodiscard]] const QuestRewardFactionPreviewEntry *
  reward_faction_preview_entry(std::size_t index) const {
    return index < reward_faction_preview_count_ ? &reward_faction_preview_[index] : nullptr;
  }
  void MarkDialogActionPending() {
    dialog_action_pending_ = true;
  }

  void MarkQuestDetailsAcceptSubmitted() {
    if (active_details_.has_value()) {
      active_details_->accept_packet_value = 0;
    }
    dialog_action_pending_ = true;
  }
  void RecordPendingRewardSelection(std::uint32_t quest_id, std::uint32_t reward_index);
  [[nodiscard]] std::uint32_t TakePendingRewardSelectionItem(std::uint32_t quest_id);
  void ShowQuestGreeting(const std::string &greeting);
  void BuildQuestLogRewardFactionPreview(std::uint32_t quest_id);

  void CloseQuestFrameInteraction() {
    quest_frame_interaction_state_ = {};
  }

  [[nodiscard]] const std::vector<QuestGiverStatusEntry> &quest_giver_statuses() const {
    return giver_statuses_;
  }
  [[nodiscard]] std::optional<QuestGiverStatus> FindQuestGiverStatus(
      const ObjectGuid &guid) const;
  void EraseQuestGiverStatus(const ObjectGuid &guid);

  [[nodiscard]] bool is_quest_log_full() const {
    return quest_log_full_;
  }
  void ClearQuestLogFullFlag() {
    quest_log_full_ = false;
  }

  [[nodiscard]] const std::vector<QuestPoiData> &last_poi_response() const {
    return last_poi_response_;
  }
  [[nodiscard]] const std::optional<QuestPushResultInfo> &last_push_result() const {
    return last_push_result_;
  }
  [[nodiscard]] const std::optional<QuestFailedInfo> &last_quest_failed() const {
    return last_quest_failed_;
  }
  [[nodiscard]] std::uint32_t last_update_failed_quest() const {
    return last_update_failed_quest_;
  }
  [[nodiscard]] const std::optional<QuestPvpKillInfo> &last_pvp_kill() const {
    return last_pvp_kill_;
  }

  [[nodiscard]] std::uint32_t force_remove_quest() const {
    return force_remove_quest_;
  }
  [[nodiscard]] std::uint32_t quest_invalid_reason() const {
    return quest_invalid_reason_;
  }
  [[nodiscard]] const std::optional<QuestAddItem> &last_quest_add_item() const {
    return last_quest_add_item_;
  }
  [[nodiscard]] std::uint32_t failed_timer_quest() const {
    return failed_timer_quest_;
  }
  [[nodiscard]] const std::vector<std::uint32_t> &completed_quests() const {
    return completed_quests_;
  }

  void Clear();

  void CommitQuestTemplateUpdate(QuestTemplate quest_template);

private:
  openwow::data::DBCacheRuntime& db_cache_runtime_;
  const data::dbc::DbcLoader& dbc_;

  void InvalidateQuestTemplate(std::uint32_t quest_id);
  bool MarkQuestFailedInLog(std::uint32_t quest_id, bool timer_expiration_reported);
  void ClearRewardFactionPreview();
  void BeginDialogResponse();
  void RebuildRewardFactionPreview(const std::uint32_t *faction_ids,
                                   const std::int32_t *reward_values,
                                   const std::int32_t *reward_value_overrides,
                                   std::uint32_t reward_faction_control);
  void SetQuestFrameInteractionState(const ObjectGuid &interaction_guid,
                                     const ObjectGuid &secondary_guid, std::uint32_t quest_id);
  std::int32_t AccumulateRewardFactionPreview(std::int32_t faction_id, std::int32_t delta,
                                              std::int32_t positive_cap, std::int32_t negative_cap,
                                              std::int32_t source_faction_id,
                                              bool suppress_spillover);
  void ClearActiveDialog();
  void ResetDialogState();
  [[nodiscard]] std::string ExpandDialogText(
      std::string_view text, bool empty_as_space) const;

  std::vector<QuestLogEntry> quest_log_;
  std::unordered_map<std::uint32_t, QuestTemplate> template_cache_;
  AsyncQueryChannel quest_queries_;
  std::function<std::uint32_t()> tick_count_provider_;
  std::optional<QuestDetailsDialog> active_details_;
  std::optional<QuestOfferRewardDialog> active_reward_;
  std::optional<QuestRequestItemsDialog> active_request_;
  std::optional<QuestConfirmAcceptPrompt> pending_confirm_accept_;
  QuestDialogTextState dialog_text_;
  QuestFrameInteractionState quest_frame_interaction_state_;
  bool dialog_action_pending_ = false;
  PendingQuestRewardSelection pending_reward_selection_{};
  std::array<QuestRewardFactionPreviewEntry, kMaxRewardFactionPreviewEntries>
      reward_faction_preview_{};
  std::size_t reward_faction_preview_count_ = 0;
  std::vector<QuestGiverStatusEntry> giver_statuses_;
  bool quest_log_full_ = false;
  std::vector<QuestPoiData> last_poi_response_;
  std::optional<QuestPushResultInfo> last_push_result_;
  std::optional<QuestFailedInfo> last_quest_failed_;
  std::uint32_t last_update_failed_quest_ = 0;
  std::optional<QuestPvpKillInfo> last_pvp_kill_;
  DialogTextExpansionFn expand_dialog_text_;

  std::uint32_t force_remove_quest_ = 0;
  std::uint32_t quest_invalid_reason_ = 0;
  std::optional<QuestAddItem> last_quest_add_item_;
  std::uint32_t failed_timer_quest_ = 0;
  std::vector<std::uint32_t> completed_quests_;
};

}

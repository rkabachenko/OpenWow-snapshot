
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::data {
class DBCacheRuntime;
class WDBCache;
}

namespace openwow::game {

inline constexpr std::int32_t kNoWeatherSoundKitId = -1;

struct WeatherInfo {
  std::uint32_t type = 0;
  float grade = 0.0f;
  std::uint8_t instant_transition = 0;

  std::int32_t sound_kit_id = kNoWeatherSoundKitId;
};

struct BindPointInfo {
  float x = 0.0f, y = 0.0f, z = 0.0f;
  std::uint32_t map_id = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t area_id = std::numeric_limits<std::uint32_t>::max();
};

struct PlayerBoundInfo {
  std::uint64_t binder_guid = 0;
  std::uint32_t area_id = 0;
};

struct PlayedTimeInfo {
  std::uint32_t total_time = 0;
  std::uint32_t level_time = 0;
  std::uint8_t show_in_chat = 0;
};

struct WhoEntry {
  std::string name;
  std::string guild_name;
  std::uint32_t level = 0;
  std::uint32_t class_id = 0;
  std::uint32_t race_id = 0;
  std::uint8_t gender = 0;
  std::uint32_t zone_id = 0;
};

struct WhoListInfo {
  static constexpr std::uint32_t kMaxDisplayed = 50;

  std::uint32_t wire_display_count = 0;
  std::uint32_t display_count = 0;
  std::uint32_t match_count = 0;
  std::vector<WhoEntry> entries;
};

struct WhoClientFilterInfo {
  std::uint32_t min_level = 0;
  std::uint32_t max_level = 100;
  std::string player_name;
  std::string guild_name;
  std::uint32_t race_mask = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t class_mask = std::numeric_limits<std::uint32_t>::max();
  std::vector<std::string> zone_terms;
  std::vector<std::string> search_terms;
};

enum class WhoSortField : std::uint32_t {
  Zone = 0,
  Level = 1,
  Class = 2,
  Group = 3,
  Name = 4,
  Race = 5,
  Guild = 6,
};

struct WhoSortCriterion {
  WhoSortField field = WhoSortField::Zone;
  bool descending = false;
};

struct MotdInfo {
  std::vector<std::string> lines;
};

struct DuelRequestInfo {
  std::uint64_t flag_guid = 0;
  std::uint64_t challenger_guid = 0;
};

struct DuelWinnerInfo {
  std::uint8_t result = 0;
  std::uint8_t win_type = 0;
  std::string winner_name;
  std::string loser_name;
};

struct EmoteInfo {
  std::uint32_t emote_id = 0;
  std::uint64_t guid = 0;
};

struct TextEmoteInfo {
  std::uint64_t source_guid = 0;
  std::uint32_t text_emote_id = 0;
  std::uint32_t emote_num = 0;
  std::uint32_t name_len = 0;
  std::string target_name;
};

struct ExplorationExpInfo {
  std::uint32_t area_id = 0;
  std::uint32_t experience = 0;
};

struct DeathReleaseLoc {
  std::uint32_t map_id = 0;
  float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct CorpseReclaimDelay {
  std::uint32_t delay_ms = 0;
};

struct DuelCountdown {
  std::uint32_t countdown_ms = 0;
};

struct PlayMusicInfo {
  std::uint32_t sound_kit_id = 0;
};

struct PlayObjectSoundInfo {
  std::uint32_t sound_kit_id = 0;
  std::uint64_t source_guid = 0;
};

struct GameObjectCustomAnim {
  std::uint64_t guid = 0;
  std::uint32_t anim_id = 0;
};

struct AreaTriggerMessage {
  std::uint32_t length = 0;
  std::string message;
};

struct CameraShake {
  std::uint32_t effect_id = 0;
  std::uint32_t sound_id = 0;
};

struct PageTextResponse {
  std::uint32_t page_id = 0;
  std::string text;
  std::uint32_t next_page = 0;
};

struct PauseMirrorTimer {
  std::uint32_t timer_type = 0;
  bool paused = false;
};

struct OverrideLight {
  std::uint32_t env_light = 0;
  std::uint32_t override_light = 0;
  std::uint32_t transition_ms = 0;
};

struct InebriationThreshold {
  std::uint64_t guid = 0;
  std::uint32_t threshold = 0;
  std::uint32_t item_id = 0;
};

struct FactionAtWar {
  std::uint32_t faction_index = 0;
  std::uint8_t flags = 0;
};

struct MirrorImageData {
  std::uint64_t guid = 0;
  std::uint32_t display_id = 0;
  std::uint8_t race = 0;
  std::uint8_t gender = 0;
  std::uint8_t class_id = 0;
  std::uint8_t skin = 0;
  std::uint8_t face = 0;
  std::uint8_t hair_style = 0;
  std::uint8_t hair_color = 0;
  std::uint8_t facial_hair = 0;
  std::uint32_t guild_id = 0;
  std::uint32_t item_display[11] = {};
  bool is_creature = false;
};

struct KickReason {
  std::uint8_t reason = 0;
  std::string text;
};

struct CorpseMapPosition {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float orientation = 0.0f;
};

struct PlayTimeWarning {
  std::uint32_t flags = 0;
  std::uint32_t remaining_seconds = 0;
};

struct PlayTimeWarningDisplay {
  int system_message_id = 0;
  std::string remaining_time;
};

[[nodiscard]] std::optional<PlayTimeWarningDisplay>
ResolvePlayTimeWarningDisplay(const PlayTimeWarning &warning);

struct RafFailure {
  std::uint32_t reason = 0;
  std::string name;
};

class MiscHandler {
public:
  using PlayedTimeClock = std::chrono::steady_clock;

  explicit MiscHandler(openwow::data::DBCacheRuntime& db_cache_runtime)
      : db_cache_runtime_(db_cache_runtime) {}

  bool HandleWeather(const std::uint8_t *data, std::size_t len);
  bool HandleBindPointUpdate(const std::uint8_t *data, std::size_t len);
  bool HandlePlayerBound(const std::uint8_t *data, std::size_t len);
  bool HandlePlayedTime(const std::uint8_t *data, std::size_t len);
  bool HandleWho(const std::uint8_t *data, std::size_t len);
  bool HandleMotd(const std::uint8_t *data, std::size_t len);
  bool HandleTutorialFlags(const std::uint8_t *data, std::size_t len);
  bool HandleDuelRequested(const std::uint8_t *data, std::size_t len);
  bool HandleDuelWinner(const std::uint8_t *data, std::size_t len);
  bool HandleDuelComplete(const std::uint8_t *data, std::size_t len);
  bool HandleEmote(const std::uint8_t *data, std::size_t len);
  bool HandleTextEmote(const std::uint8_t *data, std::size_t len);
  bool HandleNotification(const std::uint8_t *data, std::size_t len);
  bool HandleExplorationExperience(const std::uint8_t *data, std::size_t len);
  bool HandleDeathReleaseLoc(const std::uint8_t *data, std::size_t len);
  bool HandleCorpseReclaimDelay(const std::uint8_t *data, std::size_t len);
  bool HandleDuelCountdown(const std::uint8_t *data, std::size_t len);
  bool HandleDuelOutOfBounds();
  bool HandleDuelInBounds();
  bool HandleDurabilityDamageDeath();
  bool HandlePlayMusic(const std::uint8_t *data, std::size_t len);
  bool HandlePlayObjectSound(const std::uint8_t *data, std::size_t len);

  bool HandleGameObjectCustomAnim(const std::uint8_t *data, std::size_t len);
  bool HandleGameObjectDespawnAnim(const std::uint8_t *data, std::size_t len);
  bool HandleGameObjectResetState(const std::uint8_t *data, std::size_t len);
  bool HandleGameObjectPageText(const std::uint8_t *data, std::size_t len);
  bool HandleAreaTriggerMessage(const std::uint8_t *data, std::size_t len);
  bool HandleZoneUnderAttack(const std::uint8_t *data, std::size_t len);
  bool HandleForcedDeathUpdate();
  bool HandlePreResurrect(const std::uint8_t *data, std::size_t len);
  bool HandleCameraShake(const std::uint8_t *data, std::size_t len);

  bool HandlePageTextQueryResponse(const std::uint8_t *data, std::size_t len);
  [[nodiscard]] bool HydrateRetailPageTextCache(
      openwow::data::WDBCache &cache);
  void ClearPageTextCacheForClientCacheVersion();
  bool HandlePauseMirrorTimer(const std::uint8_t *data, std::size_t len);
  bool HandleOverrideLight(const std::uint8_t *data, std::size_t len);
  bool HandleMirrorImageData(const std::uint8_t *data, std::size_t len);

  bool HandleMountResult(const std::uint8_t *data, std::size_t len);
  bool HandleDismountResult(const std::uint8_t *data, std::size_t len);
  bool HandleMountSpecialAnim(const std::uint8_t *data, std::size_t len);
  bool HandleFishEscaped();
  bool HandleFishNotHooked();
  bool HandleBinderConfirm(const std::uint8_t *data, std::size_t len);
  bool HandleBindZoneReply(const std::uint8_t *data, std::size_t len);
  bool HandlePlayerBindError();
  bool HandleCrossedInebriationThreshold(const std::uint8_t *data, std::size_t len);
  bool HandleSetFactionAtWar(const std::uint8_t *data, std::size_t len);
  bool HandlePlayerSkinned(const std::uint8_t *data, std::size_t len);
  bool HandleTalentsInvoluntarilyReset(const std::uint8_t *data, std::size_t len);
  bool HandleToggleXpGain();
  bool HandleKickReason(const std::uint8_t *data, std::size_t len);

  bool HandleNpcWontTalk(const std::uint8_t *data, std::size_t len);
  bool HandleDelayGhostTeleport(const std::uint8_t *data, std::size_t len);
  bool HandleClearFarSightImmediate();
  bool HandleCorpseMapPositionResponse(const std::uint8_t *data, std::size_t len);
  bool HandleCorpseNotInInstance();
  bool HandleGhosteeGone(const std::uint8_t *data, std::size_t len);
  bool HandleOpenContainer(const std::uint8_t *data, std::size_t len);
  bool HandlePlayTimeWarning(const std::uint8_t *data, std::size_t len);
  bool HandleProposeLevelGrant(const std::uint8_t *data, std::size_t len);
  bool HandleReferAFriendExpired();
  bool HandleReferAFriendFailure(const std::uint8_t *data, std::size_t len);
  bool HandleInvalidPromotionCode();
  bool HandleWorldStateTimerUpdate(const std::uint8_t *data, std::size_t len);

  static net::wotlk::WorldPacket BuildPlayedTimeRequest(bool show_in_chat);
  static net::wotlk::WorldPacket BuildTutorialFlag(std::uint32_t flag_index);
  static net::wotlk::WorldPacket BuildTutorialClear();
  static net::wotlk::WorldPacket BuildTutorialReset();

  [[nodiscard]] const WeatherInfo &weather() const {
    return weather_;
  }

  void SetWeatherSoundKitId(std::int32_t id) { weather_.sound_kit_id = id; }
  [[nodiscard]] const BindPointInfo &bind_point() const {
    return bind_;
  }
  [[nodiscard]] const PlayerBoundInfo &player_bound() const {
    return player_bound_;
  }
  [[nodiscard]] const PlayedTimeInfo &played_time() const {
    return played_time_;
  }
  [[nodiscard]] std::uint32_t current_total_played_time() const;
  void SetPlayedTimeSnapshotTimeForTesting(
      PlayedTimeClock::time_point captured_at);
  [[nodiscard]] const WhoListInfo &who_list() const {
    return who_list_;
  }
  [[nodiscard]] WhoListInfo &mutable_who_list() {
    return who_list_;
  }
  void SetWhoClientFilter(WhoClientFilterInfo filter) {
    who_client_filter_ = std::move(filter);
    has_who_client_filter_ = true;
  }
  [[nodiscard]] const WhoClientFilterInfo &who_client_filter() const {
    return who_client_filter_;
  }
  [[nodiscard]] bool has_who_client_filter() const {
    return has_who_client_filter_;
  }
  void SortWhoResults(const openwow::data::dbc::DbcLoader *dbc);
  void UpdateWhoSortOrder(std::string_view sort_type);
  [[nodiscard]] const std::array<WhoSortCriterion, 7> &who_sort_order() const {
    return who_sort_order_;
  }
  [[nodiscard]] const MotdInfo &motd() const {
    return motd_;
  }
  [[nodiscard]] const std::array<std::uint32_t, 8> &tutorial_flags() const {
    return tutorials_;
  }
  [[nodiscard]] bool has_tutorial_flags() const {
    return tutorial_flags_initialized_;
  }

  [[nodiscard]] const DuelRequestInfo &duel_request() const {
    return duel_req_;
  }
  [[nodiscard]] const DuelWinnerInfo &duel_winner() const {
    return duel_winner_;
  }
  [[nodiscard]] bool duel_complete() const {
    return duel_complete_;
  }

  [[nodiscard]] const EmoteInfo &last_emote() const {
    return last_emote_;
  }
  [[nodiscard]] const TextEmoteInfo &last_text_emote() const {
    return last_text_emote_;
  }
  [[nodiscard]] const std::string &last_notification() const {
    return notification_;
  }
  [[nodiscard]] const ExplorationExpInfo &last_exploration() const {
    return exploration_;
  }
  [[nodiscard]] const DeathReleaseLoc &death_release_loc() const {
    return death_loc_;
  }
  [[nodiscard]] const CorpseReclaimDelay &corpse_reclaim_delay() const {
    return corpse_delay_;
  }
  [[nodiscard]] const DuelCountdown &duel_countdown() const {
    return duel_countdown_;
  }
  [[nodiscard]] bool duel_out_of_bounds() const {
    return duel_oob_;
  }
  [[nodiscard]] bool durability_damage_death() const {
    return durability_death_;
  }
  [[nodiscard]] const PlayMusicInfo &last_play_music() const {
    return last_play_music_;
  }
  [[nodiscard]] const PlayObjectSoundInfo &last_play_object_sound() const {
    return last_play_object_sound_;
  }

  [[nodiscard]] const std::optional<GameObjectCustomAnim> &last_go_custom_anim() const {
    return last_go_custom_anim_;
  }
  [[nodiscard]] std::uint64_t last_go_despawn_guid() const {
    return last_go_despawn_guid_;
  }
  [[nodiscard]] std::uint64_t last_go_reset_guid() const {
    return last_go_reset_guid_;
  }
  [[nodiscard]] std::uint64_t last_go_page_text_guid() const {
    return last_go_page_text_guid_;
  }
  [[nodiscard]] const std::optional<AreaTriggerMessage> &last_area_trigger_msg() const {
    return last_area_trigger_msg_;
  }
  [[nodiscard]] std::uint32_t last_zone_under_attack() const {
    return last_zone_under_attack_;
  }
  [[nodiscard]] bool forced_death() const {
    return forced_death_;
  }
  [[nodiscard]] const ObjectGuid &last_pre_resurrect_guid() const {
    return last_pre_resurrect_guid_;
  }
  [[nodiscard]] const std::optional<CameraShake> &last_camera_shake() const {
    return last_camera_shake_;
  }

  [[nodiscard]] const std::optional<PageTextResponse> &last_page_text() const {
    return last_page_text_;
  }
  [[nodiscard]] const PageTextResponse *FindCachedPageText(std::uint32_t page_id) const;
  [[nodiscard]] bool IsPageTextQueryPending(std::uint32_t page_id) const {
    return pending_page_text_queries_.find(page_id) != pending_page_text_queries_.end();
  }
  void MarkPageTextQueryPending(std::uint32_t page_id);
  void ClearPendingPageTextQueriesOnLogout();
  [[nodiscard]] const std::optional<PauseMirrorTimer> &last_pause_mirror_timer() const {
    return last_pause_mirror_timer_;
  }
  [[nodiscard]] const std::optional<OverrideLight> &last_override_light() const {
    return last_override_light_;
  }
  [[nodiscard]] const std::optional<MirrorImageData> &last_mirror_image() const {
    return last_mirror_image_;
  }

  [[nodiscard]] std::uint32_t mount_result() const {
    return mount_result_;
  }
  [[nodiscard]] std::uint32_t dismount_result() const {
    return dismount_result_;
  }
  [[nodiscard]] std::uint64_t mount_special_guid() const {
    return mount_special_guid_;
  }
  [[nodiscard]] bool fish_escaped() const {
    return fish_escaped_;
  }
  [[nodiscard]] bool fish_not_hooked() const {
    return fish_not_hooked_;
  }
  [[nodiscard]] std::uint64_t binder_confirm_guid() const {
    return binder_confirm_guid_;
  }
  [[nodiscard]] std::uint32_t bind_zone_id() const {
    return bind_zone_id_;
  }
  [[nodiscard]] bool bind_error() const {
    return bind_error_;
  }
  [[nodiscard]] const std::optional<InebriationThreshold> &last_inebriation() const {
    return last_inebriation_;
  }
  [[nodiscard]] const std::optional<FactionAtWar> &last_faction_at_war() const {
    return last_faction_at_war_;
  }
  [[nodiscard]] std::uint8_t player_skinned() const {
    return player_skinned_;
  }
  [[nodiscard]] std::uint8_t talents_reset_is_pet() const {
    return talents_reset_is_pet_;
  }
  [[nodiscard]] bool xp_gain_toggled() const {
    return xp_gain_toggled_;
  }
  [[nodiscard]] const std::optional<KickReason> &last_kick_reason() const {
    return last_kick_reason_;
  }

  [[nodiscard]] std::uint64_t npc_wont_talk_guid() const {
    return npc_wont_talk_guid_;
  }
  [[nodiscard]] std::uint8_t delay_ghost_teleport() const {
    return delay_ghost_teleport_;
  }
  [[nodiscard]] bool clear_far_sight() const {
    return clear_far_sight_;
  }
  [[nodiscard]] const std::optional<CorpseMapPosition> &last_corpse_map_pos() const {
    return last_corpse_map_pos_;
  }
  [[nodiscard]] bool BeginCorpseTransportPositionQuery() {
    const auto now = std::chrono::steady_clock::now();
    if (now < next_corpse_transport_query_) {
      return false;
    }
    next_corpse_transport_query_ = now + std::chrono::seconds(30);
    return true;
  }
  void ResetCorpseTransportPositionQueryThrottle() noexcept {
    next_corpse_transport_query_ = {};
  }
  [[nodiscard]] bool corpse_not_in_instance() const {
    return corpse_not_in_instance_;
  }
  [[nodiscard]] std::uint8_t ghostee_gone() const {
    return ghostee_gone_;
  }
  [[nodiscard]] std::uint64_t open_container_guid() const {
    return open_container_guid_;
  }
  [[nodiscard]] const std::optional<PlayTimeWarning> &last_play_time_warning() const {
    return last_play_time_warning_;
  }
  [[nodiscard]] std::uint64_t propose_level_grant_guid() const {
    return propose_level_grant_guid_;
  }
  [[nodiscard]] bool refer_a_friend_expired() const {
    return raf_expired_;
  }
  [[nodiscard]] const std::optional<RafFailure> &last_raf_failure() const {
    return last_raf_failure_;
  }
  [[nodiscard]] bool invalid_promotion_code() const {
    return invalid_promo_code_;
  }
  [[nodiscard]] std::uint32_t world_state_timer() const {
    return world_state_timer_;
  }

  void Clear();

private:
  openwow::data::DBCacheRuntime& db_cache_runtime_;

  WeatherInfo weather_{};
  BindPointInfo bind_{};
  PlayerBoundInfo player_bound_{};
  PlayedTimeInfo played_time_{};
  PlayedTimeClock::time_point played_time_snapshot_at_{};
  WhoListInfo who_list_{};
  WhoClientFilterInfo who_client_filter_{};
  bool has_who_client_filter_ = false;
  std::array<WhoSortCriterion, 7> who_sort_order_{{
      {WhoSortField::Zone, false},
      {WhoSortField::Level, false},
      {WhoSortField::Class, false},
      {WhoSortField::Group, false},
      {WhoSortField::Name, false},
      {WhoSortField::Race, false},
      {WhoSortField::Guild, false},
  }};
  MotdInfo motd_{};
  std::array<std::uint32_t, 8> tutorials_{};
  bool tutorial_flags_initialized_ = false;
  DuelRequestInfo duel_req_{};
  DuelWinnerInfo duel_winner_{};
  bool duel_complete_ = false;
  EmoteInfo last_emote_{};
  TextEmoteInfo last_text_emote_{};
  std::string notification_;
  ExplorationExpInfo exploration_{};
  DeathReleaseLoc death_loc_{};
  CorpseReclaimDelay corpse_delay_{};
  DuelCountdown duel_countdown_{};
  bool duel_oob_ = false;
  bool durability_death_ = false;
  PlayMusicInfo last_play_music_{};
  PlayObjectSoundInfo last_play_object_sound_{};

  std::optional<GameObjectCustomAnim> last_go_custom_anim_;
  std::uint64_t last_go_despawn_guid_ = 0;
  std::uint64_t last_go_reset_guid_ = 0;
  std::uint64_t last_go_page_text_guid_ = 0;
  std::optional<AreaTriggerMessage> last_area_trigger_msg_;
  std::uint32_t last_zone_under_attack_ = 0;
  bool forced_death_ = false;
  ObjectGuid last_pre_resurrect_guid_{0};
  std::optional<CameraShake> last_camera_shake_;

  std::optional<PageTextResponse> last_page_text_;
  std::unordered_map<std::uint32_t, PageTextResponse> page_text_cache_;
  std::unordered_set<std::uint32_t> pending_page_text_queries_;
  std::optional<PauseMirrorTimer> last_pause_mirror_timer_;
  std::optional<OverrideLight> last_override_light_;
  std::optional<MirrorImageData> last_mirror_image_;

  std::uint32_t mount_result_ = 0;
  std::uint32_t dismount_result_ = 0;
  std::uint64_t mount_special_guid_ = 0;
  bool fish_escaped_ = false;
  bool fish_not_hooked_ = false;
  std::uint64_t binder_confirm_guid_ = 0;
  std::uint32_t bind_zone_id_ = 0;
  bool bind_error_ = false;
  std::optional<InebriationThreshold> last_inebriation_;
  std::optional<FactionAtWar> last_faction_at_war_;
  std::uint8_t player_skinned_ = 0;
  std::uint8_t talents_reset_is_pet_ = 0;
  bool xp_gain_toggled_ = false;
  std::optional<KickReason> last_kick_reason_;

  std::uint64_t npc_wont_talk_guid_ = 0;
  std::uint8_t delay_ghost_teleport_ = 0;
  bool clear_far_sight_ = false;
  std::optional<CorpseMapPosition> last_corpse_map_pos_;
  std::chrono::steady_clock::time_point next_corpse_transport_query_{};
  bool corpse_not_in_instance_ = false;
  std::uint8_t ghostee_gone_ = 0;
  std::uint64_t open_container_guid_ = 0;
  std::optional<PlayTimeWarning> last_play_time_warning_;
  std::uint64_t propose_level_grant_guid_ = 0;
  bool raf_expired_ = false;
  std::optional<RafFailure> last_raf_failure_;
  bool invalid_promo_code_ = false;
  std::uint32_t world_state_timer_ = 0;
};

}

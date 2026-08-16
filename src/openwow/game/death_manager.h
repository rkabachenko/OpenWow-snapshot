#pragma once

#include "openwow/game/misc_handler.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace openwow::game {

class QueryCache;
struct ResurrectRequest;

inline constexpr std::int32_t kNoDeathReleaseMapId = -1;

enum class DeathState : std::uint8_t {
  kAlive = 0,
  kDead,
  kGhost,
};

struct ReleaseTimerContext {

  bool is_ghost = false;

  bool has_release_timer = false;

  bool no_release_window = false;

  bool is_out_of_bounds = false;
};

using DeathSendFn = std::function<void(const net::wotlk::WorldPacket&)>;

class DeathManager {
 public:
  DeathManager() = default;
  ~DeathManager() = default;

  DeathManager(const DeathManager&) = delete;
  DeathManager& operator=(const DeathManager&) = delete;

  void Initialize(DeathSendFn send_fn);

  void Reset();

  void ResetForPlayerEnterWorld();

  void HandleCorpseReclaimDelay(const MiscHandler& misc);

  void RefreshReleaseTimeCountdownMode(const ReleaseTimerContext& timer_ctx);

  [[nodiscard]] bool HandlePlayerDeath(
      const ReleaseTimerContext& timer_ctx = {});

  void HandleDeathReleaseLoc(const MiscHandler& misc,
                             const ReleaseTimerContext& timer_ctx = {});

  void HandleResurrectRequest(const ResurrectRequest& request);

  bool HandleAlive(bool is_ghost);

  void AcceptResurrect();

  void DeclineResurrect();

  [[nodiscard]] DeathState state() const { return state_; }
  [[nodiscard]] bool IsDead() const { return state_ == DeathState::kDead; }
  [[nodiscard]] bool IsGhost() const { return state_ == DeathState::kGhost; }
  [[nodiscard]] bool IsDeadOrGhost() const {
    return state_ == DeathState::kDead || state_ == DeathState::kGhost;
  }
  [[nodiscard]] bool IsResurrectPending() const {
    return resurrect_pending_;
  }
  [[nodiscard]] bool IsAlive() const { return state_ == DeathState::kAlive; }

  [[nodiscard]] float graveyard_x() const { return graveyard_x_; }
  [[nodiscard]] float graveyard_y() const { return graveyard_y_; }
  [[nodiscard]] float graveyard_z() const { return graveyard_z_; }
  [[nodiscard]] std::int32_t graveyard_map() const { return graveyard_map_; }

  [[nodiscard]] float corpse_x() const { return corpse_x_; }
  [[nodiscard]] float corpse_y() const { return corpse_y_; }
  [[nodiscard]] float corpse_z() const { return corpse_z_; }
  [[nodiscard]] std::uint32_t corpse_map() const { return corpse_map_; }

  void ArmReleaseTimeCountdown(std::uint32_t delay_ms,
                               std::uint32_t current_tick_ms);

  void DisableReleaseTimeCountdown();

  [[nodiscard]] std::int32_t GetReleaseTimeRemainingSeconds(
      std::uint32_t current_tick_ms) const;

  void ArmCorpseReclaimDelay(std::uint32_t delay_ms,
                             std::uint32_t current_tick_ms);

  [[nodiscard]] std::int32_t GetCorpseRecoveryDelaySeconds(
      std::uint32_t current_tick_ms) const;

  [[nodiscard]] bool can_reclaim_corpse(std::uint32_t current_tick_ms) const {
    return state_ == DeathState::kGhost
        && GetCorpseRecoveryDelaySeconds(current_tick_ms) == 0;
  }

  [[nodiscard]] std::uint64_t resurrecter_guid() const {
    return resurrecter_guid_;
  }
  [[nodiscard]] bool resurrect_has_sickness() const {
    return resurrect_has_sickness_;
  }
  [[nodiscard]] bool resurrect_has_timer() const {
    return resurrect_has_timer_;
  }

  [[nodiscard]] std::optional<std::string> ResolveResurrectOffererForLua(
      const QueryCache& query_cache,
      const std::function<void(std::uint64_t)>& request_name_query);

  [[nodiscard]] std::optional<std::string> ResolveResurrectRequestEventOfferer(
      const QueryCache& query_cache,
      const std::function<void(std::uint64_t)>& request_name_query);

  struct PendingResurrectRequestEventResolution {
    std::string offerer_name;
    std::uint32_t fire_count = 0;
  };

  [[nodiscard]] PendingResurrectRequestEventResolution
  HandleResurrectOffererNameQueryResult(
      std::uint64_t guid, bool name_unknown, const QueryCache& query_cache);

 private:
  DeathState state_{DeathState::kAlive};

  float graveyard_x_{0.0f};
  float graveyard_y_{0.0f};
  float graveyard_z_{0.0f};
  std::int32_t graveyard_map_{kNoDeathReleaseMapId};

  float corpse_x_{0.0f};
  float corpse_y_{0.0f};
  float corpse_z_{0.0f};
  std::uint32_t corpse_map_{0};

  std::uint32_t corpse_reclaim_deadline_ms_{0};

  std::uint64_t resurrecter_guid_{0};
  std::string resurrect_offerer_packet_name_;
  bool resurrect_has_sickness_{false};
  bool resurrect_has_timer_{false};
  bool resurrect_pending_{false};
  bool resurrect_request_waiting_for_name_query_{false};
  std::uint32_t resurrect_get_offerer_refire_count_{0};

  bool release_time_countdown_disabled_{false};
  std::uint32_t release_time_countdown_deadline_ms_{0};

  DeathSendFn send_fn_;

  void ClearPendingResurrectOffer();
  void ClearResurrectRequestState();

  std::uint64_t local_player_guid_{0};

 public:

  void SetLocalPlayerGuid(std::uint64_t guid) { local_player_guid_ = guid; }

  void SetCorpsePosition(float x, float y, float z, std::uint32_t map_id) {
    corpse_x_ = x;
    corpse_y_ = y;
    corpse_z_ = z;
    corpse_map_ = map_id;
  }
};

}

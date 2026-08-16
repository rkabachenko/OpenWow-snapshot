#include "openwow/game/death_manager.h"

#include "openwow/game/world_session.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/query_cache.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>

namespace openwow::game {

using net::wotlk::Opcode;
using net::wotlk::WorldPacket;

namespace {

constexpr std::uint32_t kAutoSpiritReleaseDelayMs = 360000u;

std::uint32_t ArmDeadlineFromNow(std::uint32_t delay_ms,
                                 std::uint32_t current_tick_ms) {
  auto deadline = current_tick_ms + delay_ms;
  if (deadline == 0) {
    deadline = 1;
  }
  return deadline;
}

std::int32_t GetRemainingWholeSeconds(std::uint32_t deadline_ms,
                                      std::uint32_t current_tick_ms) {
  if (deadline_ms == 0) {
    return 0;
  }

  const auto remaining_ms =
      static_cast<std::int32_t>(deadline_ms - current_tick_ms);
  if (remaining_ms <= 0) {
    return 0;
  }

  return remaining_ms / 1000;
}

}

void DeathManager::Initialize(DeathSendFn send_fn) {
  send_fn_ = std::move(send_fn);
  Reset();
}

void DeathManager::ResetForPlayerEnterWorld() { ClearResurrectRequestState(); }

void DeathManager::ClearPendingResurrectOffer() {

  resurrecter_guid_ = 0;
  resurrect_offerer_packet_name_.clear();
  resurrect_pending_ = false;
  resurrect_request_waiting_for_name_query_ = false;
  resurrect_get_offerer_refire_count_ = 0;
}

void DeathManager::ClearResurrectRequestState() {
  ClearPendingResurrectOffer();
  resurrect_has_sickness_ = false;
  resurrect_has_timer_ = false;
}

void DeathManager::Reset() {
  state_ = DeathState::kAlive;
  ResetDeathReleasePosition();
  graveyard_x_ = graveyard_y_ = graveyard_z_ = 0.0f;
  graveyard_map_ = kNoDeathReleaseMapId;
  corpse_x_ = corpse_y_ = corpse_z_ = 0.0f;
  corpse_map_ = 0;
  corpse_reclaim_deadline_ms_ = 0;
  ClearResurrectRequestState();
  release_time_countdown_disabled_ = false;
  release_time_countdown_deadline_ms_ = 0;
}

void DeathManager::HandleCorpseReclaimDelay(const MiscHandler& misc) {
  const auto& delay = misc.corpse_reclaim_delay();
  ArmCorpseReclaimDelay(delay.delay_ms, core::GameClock::GetTickCount32());

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "DeathManager: corpse reclaim delay = " +
                         std::to_string(delay.delay_ms) + "ms");
}

void DeathManager::RefreshReleaseTimeCountdownMode(
    const ReleaseTimerContext& timer_ctx) {
  if (timer_ctx.no_release_window) {

    ArmReleaseTimeCountdown(0, core::GameClock::GetTickCount32());
  } else if (!timer_ctx.has_release_timer || timer_ctx.is_out_of_bounds) {

    DisableReleaseTimeCountdown();
  } else {

    ArmReleaseTimeCountdown(kAutoSpiritReleaseDelayMs,
                            core::GameClock::GetTickCount32());
  }
}

bool DeathManager::HandlePlayerDeath(const ReleaseTimerContext& timer_ctx) {

  const bool became_dead = state_ == DeathState::kAlive;
  const DeathState next_state = timer_ctx.is_ghost
                                    ? DeathState::kGhost
                                    : DeathState::kDead;
  if (state_ != next_state) {
    state_ = next_state;
  }

  return became_dead;
}

void DeathManager::HandleDeathReleaseLoc(
    const MiscHandler& misc, const ReleaseTimerContext& timer_ctx) {

  const auto& loc = misc.death_release_loc();
  const bool store_cleared_position = timer_ctx.is_ghost;
  graveyard_map_ = store_cleared_position
                       ? kNoDeathReleaseMapId
                       : static_cast<std::int32_t>(loc.map_id);
  graveyard_x_ = store_cleared_position ? 0.0f : loc.x;
  graveyard_y_ = store_cleared_position ? 0.0f : loc.y;
  graveyard_z_ = store_cleared_position ? 0.0f : loc.z;
}

void DeathManager::HandleResurrectRequest(const ResurrectRequest& req) {
  resurrecter_guid_ = req.caster_guid;
  resurrect_offerer_packet_name_ = req.caster_name;
  resurrect_has_sickness_ = req.has_sickness;
  resurrect_has_timer_ = req.has_timer;
  resurrect_pending_ = true;
  resurrect_request_waiting_for_name_query_ = false;
  resurrect_get_offerer_refire_count_ = 0;

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "DeathManager: resurrect request from " +
                         resurrect_offerer_packet_name_ + " (guid=" +
                         std::to_string(resurrecter_guid_) + ")");
}

std::optional<std::string> DeathManager::ResolveResurrectOffererForLua(
    const QueryCache& query_cache,
    const std::function<void(std::uint64_t)>& request_name_query) {
  if (!resurrect_pending_ || resurrecter_guid_ == 0) {
    return std::nullopt;
  }

  if (const auto* cached_name = query_cache.GetPlayerName(resurrecter_guid_);
      cached_name != nullptr && !cached_name->name.empty()) {
    return cached_name->name;
  }

  ++resurrect_get_offerer_refire_count_;
  if (request_name_query) {
    request_name_query(resurrecter_guid_);
  }

  return std::nullopt;
}

std::optional<std::string> DeathManager::ResolveResurrectRequestEventOfferer(
    const QueryCache& query_cache,
    const std::function<void(std::uint64_t)>& request_name_query) {
  if (!resurrect_pending_ || resurrecter_guid_ == 0) {
    return std::nullopt;
  }

  if (!resurrect_offerer_packet_name_.empty()) {
    return resurrect_offerer_packet_name_;
  }

  if (const auto* cached_name = query_cache.GetPlayerName(resurrecter_guid_);
      cached_name != nullptr && !cached_name->name.empty()) {
    return cached_name->name;
  }

  resurrect_request_waiting_for_name_query_ = true;
  if (request_name_query) {
    request_name_query(resurrecter_guid_);
  }

  return std::nullopt;
}

DeathManager::PendingResurrectRequestEventResolution
DeathManager::HandleResurrectOffererNameQueryResult(
    const std::uint64_t guid, const bool name_unknown,
    const QueryCache& query_cache) {
  PendingResurrectRequestEventResolution resolution;
  if (guid == 0 || guid != resurrecter_guid_) {
    return resolution;
  }

  const auto* cached_name = query_cache.GetPlayerName(guid);
  const bool has_cached_name =
      cached_name != nullptr && !cached_name->name.empty();

  if (resurrect_request_waiting_for_name_query_) {
    resurrect_request_waiting_for_name_query_ = false;
    if (name_unknown || !has_cached_name) {
      ClearResurrectRequestState();
      return resolution;
    }

    resolution.offerer_name = cached_name->name;
    resolution.fire_count = 1;
  }

  if (resurrect_get_offerer_refire_count_ != 0) {
    const auto refire_count = resurrect_get_offerer_refire_count_;
    resurrect_get_offerer_refire_count_ = 0;
    if (!name_unknown && has_cached_name) {
      if (resolution.offerer_name.empty()) {
        resolution.offerer_name = cached_name->name;
      }
      resolution.fire_count += refire_count;
    }
  }

  return resolution;
}

bool DeathManager::HandleAlive(const bool is_ghost) {

  const DeathState next_state =
      is_ghost ? DeathState::kGhost : DeathState::kAlive;

  if (state_ == next_state && !resurrect_pending_) return false;

  const bool was_alive = state_ == DeathState::kAlive;
  state_ = next_state;
  ClearPendingResurrectOffer();

  if (next_state == DeathState::kAlive) {

    ResetDeathReleasePosition();
  } else if (was_alive) {

  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     next_state == DeathState::kAlive
                         ? "DeathManager: player is alive"
                         : "DeathManager: player is alive in ghost form");
  return true;
}

void DeathManager::AcceptResurrect() {

  if (resurrecter_guid_ == 0) return;

  if (send_fn_) {
    WorldPacket pkt(Opcode::CMSG_RESURRECT_RESPONSE);
    pkt.AppendU64(resurrecter_guid_);
    pkt.AppendU8(1);
    send_fn_(pkt);
  }

  ClearPendingResurrectOffer();

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "DeathManager: accepted resurrection");
}

void DeathManager::DeclineResurrect() {
  if (!resurrect_pending_ || resurrecter_guid_ == 0) return;

  if (send_fn_) {
    WorldPacket pkt(Opcode::CMSG_RESURRECT_RESPONSE);
    pkt.AppendU64(resurrecter_guid_);
    pkt.AppendU8(0);
    send_fn_(pkt);
  }

  ClearPendingResurrectOffer();

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "DeathManager: declined resurrection");
}

void DeathManager::ArmReleaseTimeCountdown(std::uint32_t delay_ms,
                                           std::uint32_t current_tick_ms) {
  release_time_countdown_disabled_ = false;
  release_time_countdown_deadline_ms_ =
      ArmDeadlineFromNow(delay_ms, current_tick_ms);
}

void DeathManager::DisableReleaseTimeCountdown() {
  release_time_countdown_disabled_ = true;
  release_time_countdown_deadline_ms_ = 0;
}

void DeathManager::ArmCorpseReclaimDelay(std::uint32_t delay_ms,
                                         std::uint32_t current_tick_ms) {
  corpse_reclaim_deadline_ms_ = ArmDeadlineFromNow(delay_ms, current_tick_ms);
}

std::int32_t DeathManager::GetReleaseTimeRemainingSeconds(
    std::uint32_t current_tick_ms) const {
  if (release_time_countdown_disabled_) {
    return -1;
  }

  return GetRemainingWholeSeconds(release_time_countdown_deadline_ms_,
                                  current_tick_ms);
}

std::int32_t DeathManager::GetCorpseRecoveryDelaySeconds(
    std::uint32_t current_tick_ms) const {
  return GetRemainingWholeSeconds(corpse_reclaim_deadline_ms_,
                                  current_tick_ms);
}

}

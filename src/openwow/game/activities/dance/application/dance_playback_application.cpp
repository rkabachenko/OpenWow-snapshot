#include "openwow/game/activities/dance/application/dance_playback_application.h"

#include "openwow/game/activities/dance/rules/dance_playback_rules.h"

#include <utility>

namespace openwow::game {

void DancePlaybackApplication::ResetSessionState() {
  active_player_dance_state_provider_ = {};
  play_dance_sender_ = {};
  learned_dance_move_mask_ = {};
  active_player_class_.reset();
  dance_move_catalog_ = nullptr;
}

KnownDanceSendResult DancePlaybackApplication::CheckKnownDanceSend() const {
  if (!active_player_dance_state_provider_) {
    return KnownDanceSendResult::kActivePlayerUnavailable;
  }
  const std::optional<DancePlaybackState> active_player_state =
      active_player_dance_state_provider_();
  if (!active_player_state) {
    return KnownDanceSendResult::kActivePlayerUnavailable;
  }
  if (*active_player_state == DancePlaybackState::kActive) {
    return KnownDanceSendResult::kAlreadyDancing;
  }
  return KnownDanceSendResult::kReady;
}

bool DancePlaybackApplication::SendKnownDance(
    const DanceId dance_id, const DanceSequenceId sequence_id) const {
  if (!play_dance_sender_) {
    return false;
  }
  play_dance_sender_(dance_id, sequence_id);
  return true;
}

DancePlaybackStartResult DancePlaybackApplication::StartResolvedDance(
    const DanceUnitGuid unit_guid, const DanceCacheRecord& dance,
    const DancePlaybackStep start_step,
    const DancePlaybackSeed seed) const {
  if (!unit_dance_state_provider_) {
    return DancePlaybackStartResult::kUnitUnavailable;
  }
  const std::optional<DancePlaybackState> unit_state =
      unit_dance_state_provider_(unit_guid);
  if (!unit_state) {
    return DancePlaybackStartResult::kUnitUnavailable;
  }
  if (*unit_state == DancePlaybackState::kActive) {
    return DancePlaybackStartResult::kAlreadyDancing;
  }
  if (dance_move_catalog_ == nullptr || !unit_dance_starter_) {
    return DancePlaybackStartResult::kNotConfigured;
  }

  unit_dance_starter_(
      unit_guid,
      BuildDancePlaybackSequence(dance, start_step, seed,
                                 *dance_move_catalog_),
      *dance_move_catalog_);
  return DancePlaybackStartResult::kStarted;
}

void DancePlaybackApplication::CancelDance(
    const DanceUnitGuid unit_guid) const {
  if (unit_dance_canceller_) {
    unit_dance_canceller_(unit_guid);
  }
}

void DancePlaybackApplication::SetActivePlayerClass(
    std::optional<DancePlayerClass> player_class) {
  active_player_class_ = player_class;
}

void DancePlaybackApplication::SetLearnedDanceMoveMask(
    const LearnedDanceMoveMask learned_move_mask) {
  learned_dance_move_mask_ = learned_move_mask;
}

void DancePlaybackApplication::SetActivePlayerDanceStateProvider(
    ActivePlayerDanceStateProvider provider) {
  active_player_dance_state_provider_ = std::move(provider);
}

void DancePlaybackApplication::SetPlayDanceSender(PlayDanceSender sender) {
  play_dance_sender_ = std::move(sender);
}

void DancePlaybackApplication::SetUnitDanceStateProvider(
    UnitDanceStateProvider provider) {
  unit_dance_state_provider_ = std::move(provider);
}

void DancePlaybackApplication::SetUnitDanceCanceller(
    UnitDanceCanceller canceller) {
  unit_dance_canceller_ = std::move(canceller);
}

void DancePlaybackApplication::SetUnitDanceStarter(
    UnitDanceStarter starter) {
  unit_dance_starter_ = std::move(starter);
}

void DancePlaybackApplication::BindDanceMoveCatalog(
    const DanceMoveCatalog& catalog) {
  dance_move_catalog_ = &catalog;
}

void DancePlaybackApplication::UnbindDanceMoveCatalog() {
  dance_move_catalog_ = nullptr;
}

std::optional<DancePlayerClass>
DancePlaybackApplication::ActivePlayerClass() const {
  return active_player_class_;
}

LearnedDanceMoveMask DancePlaybackApplication::LearnedMoveMask() const {
  return learned_dance_move_mask_;
}

const DanceMoveCatalog* DancePlaybackApplication::MoveCatalog() const {
  return dance_move_catalog_;
}

}

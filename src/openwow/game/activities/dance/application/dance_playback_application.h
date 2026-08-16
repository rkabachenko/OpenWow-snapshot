#pragma once

#include "openwow/game/activities/dance/model/dance_move_catalog.h"
#include "openwow/game/activities/dance/model/dance_sequence.h"
#include "openwow/game/activities/dance/model/dance_studio_messages.h"

#include <functional>
#include <optional>

namespace openwow::game {

enum class DancePlaybackStartResult {
  kStarted,
  kUnitUnavailable,
  kAlreadyDancing,
  kNotConfigured,
};

enum class KnownDanceSendResult {
  kReady,
  kActivePlayerUnavailable,
  kAlreadyDancing,
};

class DancePlaybackApplication final {
 public:
  using UnitDanceStateProvider =
      std::function<std::optional<DancePlaybackState>(DanceUnitGuid)>;
  using UnitDanceCanceller = std::function<void(DanceUnitGuid)>;
  using UnitDanceStarter =
      std::function<void(DanceUnitGuid, DanceSequence,
                         const DanceMoveCatalog&)>;
  using ActivePlayerDanceStateProvider =
      std::function<std::optional<DancePlaybackState>()>;
  using PlayDanceSender =
      std::function<void(DanceId, DanceSequenceId)>;

  void ResetSessionState();

  [[nodiscard]] KnownDanceSendResult CheckKnownDanceSend() const;
  [[nodiscard]] bool SendKnownDance(
      DanceId dance_id, DanceSequenceId sequence_id) const;
  [[nodiscard]] DancePlaybackStartResult StartResolvedDance(
      DanceUnitGuid unit_guid, const DanceCacheRecord& dance,
      DancePlaybackStep start_step, DancePlaybackSeed seed) const;
  void CancelDance(DanceUnitGuid unit_guid) const;

  void SetActivePlayerClass(std::optional<DancePlayerClass> player_class);
  void SetLearnedDanceMoveMask(LearnedDanceMoveMask learned_move_mask);
  void SetActivePlayerDanceStateProvider(
      ActivePlayerDanceStateProvider provider);
  void SetPlayDanceSender(PlayDanceSender sender);
  void SetUnitDanceStateProvider(UnitDanceStateProvider provider);
  void SetUnitDanceCanceller(UnitDanceCanceller canceller);
  void SetUnitDanceStarter(UnitDanceStarter starter);
  void BindDanceMoveCatalog(const DanceMoveCatalog& catalog);
  void UnbindDanceMoveCatalog();

  [[nodiscard]] std::optional<DancePlayerClass> ActivePlayerClass() const;
  [[nodiscard]] LearnedDanceMoveMask LearnedMoveMask() const;
  [[nodiscard]] const DanceMoveCatalog* MoveCatalog() const;

 private:
  UnitDanceStateProvider unit_dance_state_provider_;
  UnitDanceCanceller unit_dance_canceller_;
  UnitDanceStarter unit_dance_starter_;
  ActivePlayerDanceStateProvider active_player_dance_state_provider_;
  PlayDanceSender play_dance_sender_;
  LearnedDanceMoveMask learned_dance_move_mask_;
  std::optional<DancePlayerClass> active_player_class_;
  const DanceMoveCatalog* dance_move_catalog_ = nullptr;
};

}

#pragma once

#include "openwow/game/activities/dance/model/dance_sequence.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

namespace openwow::game {

class DanceMoveCatalog;
struct DanceMoveRecord;

enum class DanceSynchronizationRole : std::uint8_t {
  kRemoteUnit,
  kActivePlayer,
};

struct DanceStateCallbacks final {
  std::function<void(DanceEmoteAnimationId)> play_emote_animation;
  std::function<void(DanceAnimationDataId)> play_animation;
  std::function<void(DanceSoundKitId)> play_sound;
  std::function<void(std::chrono::milliseconds, std::function<void()>)>
      schedule_advance;
  std::function<void()> request_sync;
  std::function<void()> request_stop;

  std::function<void()> refresh_stand_animation;
};

class UnitDanceState final {
 public:
  void CancelDance();
  void SetDanceData(DanceSequence sequence,
                    const DanceMoveCatalog& catalog);
  void AdvanceDanceStep();
  void ExecuteStep();
  void DispatchAction(const DanceMoveRecord& record);
  void ContinuationCheck();

  [[nodiscard]] bool IsActive() const;
  [[nodiscard]] DanceSequencePosition StepPosition() const;

  void SetCallbacks(DanceStateCallbacks callbacks);
  void SetSynchronizationRole(DanceSynchronizationRole role);

 private:
  std::optional<DanceSequence> sequence_;
  std::optional<std::reference_wrapper<const DanceMoveCatalog>> catalog_;
  DanceSequencePosition step_position_;
  DanceStateCallbacks callbacks_;
  DanceSynchronizationRole synchronization_role_ =
      DanceSynchronizationRole::kRemoteUnit;
};

}

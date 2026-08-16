#include "openwow/game/activities/dance/application/unit_dance_state.h"

#include "openwow/game/activities/dance/model/dance_move_catalog.h"

#include <utility>

namespace openwow::game {

void UnitDanceState::CancelDance() {
  sequence_.reset();
  catalog_.reset();
  step_position_ = {};
}

void UnitDanceState::SetDanceData(DanceSequence sequence,
                                  const DanceMoveCatalog& catalog) {
  if (sequence_) {
    CancelDance();
  }
  sequence_.emplace(std::move(sequence));
  catalog_ = catalog;
  step_position_ = sequence_->start_position;
  ExecuteStep();
}

void UnitDanceState::AdvanceDanceStep() {
  if (synchronization_role_ == DanceSynchronizationRole::kActivePlayer &&
      callbacks_.request_sync) {
    callbacks_.request_sync();
  }
  ++step_position_.value;
  ExecuteStep();
}

void UnitDanceState::ExecuteStep() {
  if (!sequence_) {
    return;
  }

  if (static_cast<std::size_t>(step_position_.value) >=
          sequence_->StepCount()) {
    if (callbacks_.request_stop) {
      callbacks_.request_stop();
    }
    if (callbacks_.refresh_stand_animation) {
      callbacks_.refresh_stand_animation();
    }
    CancelDance();
    return;
  }

  const DanceMoveId step_id =
      sequence_->steps[static_cast<std::size_t>(step_position_.value)];
  if (const DanceMoveRecord* record = catalog_->get().Lookup(step_id)) {
    DispatchAction(*record);
  }
}

void UnitDanceState::DispatchAction(const DanceMoveRecord& record) {
  if (const auto* action =
          std::get_if<DanceEmoteAnimationAction>(&record.action)) {
    if (callbacks_.play_emote_animation) {
      callbacks_.play_emote_animation(action->animation_id);
    }
    return;
  }
  if (const auto* action =
          std::get_if<DanceAnimationDataAction>(&record.action)) {
    if (callbacks_.play_animation) {
      callbacks_.play_animation(action->animation_id);
    }
    return;
  }
  if (const auto* action = std::get_if<DanceSoundAction>(&record.action)) {
    if (callbacks_.play_sound) {
      callbacks_.play_sound(action->sound_kit_id);
    }
    AdvanceDanceStep();
    return;
  }
  if (const auto* action = std::get_if<DanceDelayAction>(&record.action)) {
    if (callbacks_.schedule_advance) {
      callbacks_.schedule_advance(action->duration.value,
                                  [this]() { AdvanceDanceStep(); });
    }
    return;
  }
  if (const auto* action =
          std::get_if<DanceRepeatPreviousAction>(&record.action)) {
    if (callbacks_.schedule_advance) {
      callbacks_.schedule_advance(action->duration.value,
                                  [this]() { AdvanceDanceStep(); });
    }
    if (step_position_.value > 0 && sequence_) {
      const DanceMoveId previous_step_id =
          sequence_->steps[
              static_cast<std::size_t>(step_position_.value - 1)];
      const DanceMoveRecord* previous_record =
          catalog_->get().Lookup(previous_step_id);
      if (previous_record && !IsDanceDelayAction(previous_record->action)) {
        DispatchAction(*previous_record);
      }
    }
  }
}

void UnitDanceState::ContinuationCheck() {
  if (!sequence_ ||
      static_cast<std::size_t>(step_position_.value) >=
          sequence_->StepCount()) {
    return;
  }

  const DanceMoveId step_id =
      sequence_->steps[static_cast<std::size_t>(step_position_.value)];
  const DanceMoveRecord* record = catalog_->get().Lookup(step_id);
  if (!record ||
      !std::holds_alternative<DanceRepeatPreviousAction>(record->action) ||
      step_position_.value == 0) {
    AdvanceDanceStep();
    return;
  }

  const DanceMoveId previous_step_id =
      sequence_->steps[
          static_cast<std::size_t>(step_position_.value - 1)];
  if (const DanceMoveRecord* previous_record =
          catalog_->get().Lookup(previous_step_id)) {
    DispatchAction(*previous_record);
  }
}

bool UnitDanceState::IsActive() const {
  return sequence_.has_value();
}

DanceSequencePosition UnitDanceState::StepPosition() const {
  return step_position_;
}

void UnitDanceState::SetCallbacks(DanceStateCallbacks callbacks) {
  callbacks_ = std::move(callbacks);
}

void UnitDanceState::SetSynchronizationRole(
    const DanceSynchronizationRole role) {
  synchronization_role_ = role;
}

}

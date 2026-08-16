#include "openwow/game/spell_visual_using_animation.h"

namespace openwow::game {

void UsingAnimationSequencer::Start(std::uint16_t initial_anim,
                                     std::uint32_t threshold_tick,
                                     bool model_has_omni) {
  phase_ = UsingAnimPhase::kInitialAnimation;
  initial_anim_id_ = initial_anim;
  next_anim_id_ = initial_anim;
  threshold_tick_ = threshold_tick;
  model_has_omni_ = model_has_omni;
}

UsingAnimationSequencer::Action
UsingAnimationSequencer::Advance(std::uint32_t current_tick,
                                  bool anim_completed) {
  if (!anim_completed) {
    return Action::kNone;
  }

  switch (phase_) {
    case UsingAnimPhase::kInitialAnimation: {

      const bool threshold_elapsed =
          (threshold_tick_ == 0) ||
          (static_cast<std::int32_t>(current_tick - threshold_tick_) > 0);

      if (!threshold_elapsed) {

        next_anim_id_ = initial_anim_id_;
        return Action::kPlayAnimation;
      }

      if (model_has_omni_) {
        phase_ = UsingAnimPhase::kReadySpellOmni;
        next_anim_id_ = UsingAnimId::kReadySpellOmni;
        return Action::kPlayAnimation;
      }

      phase_ = UsingAnimPhase::kDone;
      return Action::kDestroyEffectNode;
    }

    case UsingAnimPhase::kReadySpellOmni:
      phase_ = UsingAnimPhase::kDone;
      return Action::kDestroyEffectNode;

    case UsingAnimPhase::kIdle:
    case UsingAnimPhase::kDone:
      return Action::kNone;
  }

  return Action::kNone;
}

}

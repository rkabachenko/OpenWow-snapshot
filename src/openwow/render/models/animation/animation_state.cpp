
#include "openwow/render/models/animation/animation_state.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace openwow::render {

void AnimationState::SetAnimation(std::uint16_t anim_id, bool looping) {
  if (anim_id == current_anim_ && !is_one_shot_) {

    is_looping_ = looping;
    return;
  }

  previous_anim_ = current_anim_;
  previous_time_ms_ = current_time_ms_;

  current_anim_ = anim_id;
  current_time_ms_ = 0.0;
  is_looping_ = looping;
  is_one_shot_ = false;
  blend_factor_ = 0.0f;
}

bool AnimationState::SetAnimationAtPhase(const std::uint16_t anim_id,
                                         const bool looping,
                                         const std::uint32_t start_time_ms) {
  if (anim_id == current_anim_ && !is_one_shot_) {
    is_looping_ = looping;
    return false;
  }
  SetAnimation(anim_id, looping);
  current_time_ms_ = static_cast<double>(start_time_ms);
  return true;
}

void AnimationState::Restart(const std::uint16_t anim_id, const bool looping) {
  previous_anim_ = current_anim_;
  previous_time_ms_ = current_time_ms_;
  current_anim_ = anim_id;
  current_time_ms_ = 0.0;

  is_looping_ = looping;
  is_one_shot_ = false;
  did_complete_ = false;
  blend_factor_ = 0.0f;
}

void AnimationState::PlayOneShot(std::uint16_t anim_id) {
  if (anim_id == current_anim_ && is_one_shot_) return;

  fallback_anim_ = current_anim_;

  previous_anim_ = current_anim_;
  previous_time_ms_ = current_time_ms_;

  current_anim_ = anim_id;
  current_time_ms_ = 0.0;
  is_looping_ = false;
  is_one_shot_ = true;
  blend_factor_ = 0.0f;
}

void AnimationState::Update(float dt, std::uint32_t duration_ms) {
  did_complete_ = false;
  const double advance_ms =
      static_cast<double>(std::max(dt, 0.0f)) * 1000.0;

  if (blend_factor_ < 1.0f) {
    if (blend_duration_ > 0.0f) {
      blend_factor_ += std::max(dt, 0.0f) / blend_duration_;
    } else {
      blend_factor_ = 1.0f;
    }
    blend_factor_ = std::min(blend_factor_, 1.0f);
  }

  current_time_ms_ += advance_ms;
  previous_time_ms_ += advance_ms;

  if (duration_ms > 0) {
    if (is_looping_) {

      if (current_time_ms_ >= duration_ms) {
        did_complete_ = true;
      }
      current_time_ms_ =
          std::fmod(current_time_ms_, static_cast<double>(duration_ms));
    } else if (current_time_ms_ >= duration_ms) {
      did_complete_ = true;

      if (completion_callback_) {
        auto cb = std::move(completion_callback_);
        completion_callback_ = nullptr;
        cb(current_anim_);
      }

      if (is_one_shot_) {

        SetAnimation(fallback_anim_, true);
      } else {

        current_time_ms_ = static_cast<double>(duration_ms);
      }
    }
  }
}

void AnimationState::Reset() {
  current_anim_ = AnimId::kStand;
  previous_anim_ = AnimId::kStand;
  fallback_anim_ = AnimId::kStand;
  current_time_ms_ = 0.0;
  previous_time_ms_ = 0.0;
  blend_factor_ = 1.0f;
  is_looping_ = true;
  is_one_shot_ = false;
  did_complete_ = false;
  completion_callback_ = nullptr;
}

float AnimationState::GetProgress(std::uint32_t duration_ms) const {
  if (duration_ms == 0) return 0.0f;
  if (!is_looping_ && !is_one_shot_ && current_time_ms_ >= duration_ms) {
    return 1.0f;
  }
  return static_cast<float>(
             std::fmod(current_time_ms_, static_cast<double>(duration_ms))) /
         static_cast<float>(duration_ms);
}

namespace {

[[nodiscard]] std::uint32_t SaturatingMilliseconds(
    const double milliseconds) noexcept {
  if (!(milliseconds > 0.0)) {
    return 0u;
  }
  constexpr double kMaximum =
      static_cast<double>(std::numeric_limits<std::uint32_t>::max());
  return milliseconds >= kMaximum
             ? std::numeric_limits<std::uint32_t>::max()
             : static_cast<std::uint32_t>(milliseconds);
}

}

std::uint32_t AnimationState::current_time_ms() const {
  return SaturatingMilliseconds(current_time_ms_);
}

std::uint32_t AnimationState::previous_time_ms() const {
  return SaturatingMilliseconds(previous_time_ms_);
}

std::string AnimationState::GetAnimationName(std::uint16_t anim_id) {
  switch (anim_id) {
    case AnimId::kStand:         return "Stand";
    case AnimId::kDeath:         return "Death";
    case AnimId::kSpell:         return "Spell";
    case AnimId::kStop:          return "Stop";
    case AnimId::kWalk:          return "Walk";
    case AnimId::kRun:           return "Run";
    case AnimId::kDead:          return "Dead";
    case AnimId::kRise:          return "Rise";
    case AnimId::kStandWound:    return "StandWound";
    case AnimId::kCombatWound:   return "CombatWound";
    case AnimId::kCombatCritical:return "CombatCritical";
    case AnimId::kShuffleLeft:   return "ShuffleLeft";
    case AnimId::kShuffleRight:  return "ShuffleRight";
    case AnimId::kWalkBackwards: return "WalkBackwards";
    case AnimId::kStun:          return "Stun";
    case AnimId::kHandsClosed:   return "HandsClosed";
    case AnimId::kAttackUnarmed: return "AttackUnarmed";
    case AnimId::kAttack1H:      return "Attack1H";
    case AnimId::kAttack2H:      return "Attack2H";
    case AnimId::kAttack2HL:     return "Attack2HL";
    case AnimId::kParryUnarmed:  return "ParryUnarmed";
    case AnimId::kParry1H:       return "Parry1H";
    case AnimId::kParry2H:       return "Parry2H";
    case AnimId::kParry2HL:      return "Parry2HL";
    case AnimId::kShieldBlock:   return "ShieldBlock";
    case AnimId::kReadyUnarmed:  return "ReadyUnarmed";
    case AnimId::kReady1H:       return "Ready1H";
    case AnimId::kReady2H:       return "Ready2H";
    case AnimId::kReady2HL:      return "Ready2HL";
    case AnimId::kReadyBow:      return "ReadyBow";
    case AnimId::kDodge:         return "Dodge";
    case AnimId::kSpellPrecast:  return "SpellPrecast";
    case AnimId::kSpellCast:     return "SpellCast";
    case AnimId::kSpellCastArea: return "SpellCastArea";
    case AnimId::kJumpStart:     return "JumpStart";
    case AnimId::kJump:          return "Jump";
    case AnimId::kJumpEnd:       return "JumpEnd";
    case AnimId::kFall:          return "Fall";
    case AnimId::kSwimIdle:      return "SwimIdle";
    case AnimId::kSwim:          return "Swim";
    case AnimId::kSwimLeft:      return "SwimLeft";
    case AnimId::kSwimRight:     return "SwimRight";
    case AnimId::kSwimBackwards: return "SwimBackwards";
    case AnimId::kAttackBow:     return "AttackBow";
    case AnimId::kMount:         return "Mount";
    case AnimId::kStealthWalk:   return "StealthWalk";
    case AnimId::kFly:           return "Fly";
    case AnimId::kSprint:        return "Sprint";
    case AnimId::kHover:         return "Hover";
    case AnimId::kEmoteTalk:     return "EmoteTalk";
    case AnimId::kEmoteEat:      return "EmoteEat";
    case AnimId::kEmoteWork:     return "EmoteWork";
    default:                     return "Anim_" + std::to_string(anim_id);
  }
}

}

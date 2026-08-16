
#include "openwow/game/fishing_system.h"

namespace openwow::game {

void FishingSystem::StartCasting(float castTime) {
    if (castTime <= 0.0f) return;
    state_         = FishingState::Casting;
    castTime_      = castTime;
    castRemaining_ = castTime;
    bobRemaining_  = 0.0f;
    ++attemptsCount_;
}

void FishingSystem::SetBobbing() {
    if (state_ != FishingState::Waiting) return;
    state_        = FishingState::Bobbing;
    bobRemaining_ = bobDuration_;
}

void FishingSystem::Loot() {
    if (state_ != FishingState::Bobbing) return;
    state_ = FishingState::Looting;
}

void FishingSystem::Cancel() {
    state_         = FishingState::Idle;
    castRemaining_ = 0.0f;
    bobRemaining_  = 0.0f;
}

bool FishingSystem::IsFishing() const {
    return state_ != FishingState::Idle && state_ != FishingState::Failed;
}

bool FishingSystem::IsBobbing() const {
    return state_ == FishingState::Bobbing;
}

float FishingSystem::GetCatchChance() const {
    if (requiredSkill_ == 0) return 1.0f;
    uint32_t eff = GetEffectiveSkill();
    if (eff >= requiredSkill_) return 1.0f;
    return std::clamp(static_cast<float>(eff) / static_cast<float>(requiredSkill_),
                      0.0f, 1.0f);
}

void FishingSystem::IncrementCatchCount() {
    ++catchCount_;
}

bool FishingSystem::Update(float dt) {
    FishingState prev = state_;

    switch (state_) {
        case FishingState::Casting:
            castRemaining_ -= dt;
            if (castRemaining_ <= 0.0f) {
                castRemaining_ = 0.0f;
                state_ = FishingState::Waiting;
            }
            break;

        case FishingState::Bobbing:
            if (bobDuration_ > 0.0f) {
                bobRemaining_ -= dt;
                if (bobRemaining_ <= 0.0f) {
                    bobRemaining_ = 0.0f;
                    state_ = FishingState::Failed;
                }
            }
            break;

        case FishingState::Looting:

            state_ = FishingState::Idle;
            break;

        default:
            break;
    }

    return state_ != prev;
}

void FishingSystem::Reset() {
    state_         = FishingState::Idle;
    castTime_      = 0.0f;
    castRemaining_ = 0.0f;
    bobDuration_   = 0.0f;
    bobRemaining_  = 0.0f;
    skillLevel_    = 0;
    requiredSkill_ = 0;
    lureBonus_     = 0;
    catchCount_    = 0;
    attemptsCount_ = 0;
}

}

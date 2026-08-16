
#pragma once

#include <algorithm>
#include <cstdint>

namespace openwow::game {

enum class FishingState : uint32_t {
    Idle    = 0,
    Casting = 1,
    Waiting = 2,
    Bobbing = 3,
    Looting = 4,
    Failed  = 5,
};

class FishingSystem {
public:

    void StartCasting(float castTime);

    void SetBobbing();

    void Loot();

    void Cancel();

    [[nodiscard]] FishingState GetState() const { return state_; }
    [[nodiscard]] bool IsFishing() const;
    [[nodiscard]] bool IsBobbing() const;

    [[nodiscard]] float GetCastTime() const { return castTime_; }
    [[nodiscard]] float GetCastTimeRemaining() const { return castRemaining_; }

    [[nodiscard]] float GetBobDuration() const { return bobDuration_; }
    void SetBobDuration(float d) { bobDuration_ = d; }

    [[nodiscard]] float GetBobTimeRemaining() const { return bobRemaining_; }

    void     SetSkillLevel(uint32_t lvl) { skillLevel_ = lvl; }
    [[nodiscard]] uint32_t GetSkillLevel() const { return skillLevel_; }

    void     SetRequiredSkill(uint32_t lvl) { requiredSkill_ = lvl; }
    [[nodiscard]] uint32_t GetRequiredSkill() const { return requiredSkill_; }

    [[nodiscard]] float GetCatchChance() const;

    void     SetLureBonus(uint32_t bonus) { lureBonus_ = bonus; }
    [[nodiscard]] uint32_t GetEffectiveSkill() const { return skillLevel_ + lureBonus_; }

    void     IncrementCatchCount();
    [[nodiscard]] uint32_t GetCatchCount() const { return catchCount_; }
    [[nodiscard]] uint32_t GetAttemptsCount() const { return attemptsCount_; }

    bool Update(float dt);

    void Reset();

private:
    FishingState state_         = FishingState::Idle;
    float        castTime_      = 0.0f;
    float        castRemaining_ = 0.0f;
    float        bobDuration_   = 0.0f;
    float        bobRemaining_  = 0.0f;
    uint32_t     skillLevel_    = 0;
    uint32_t     requiredSkill_ = 0;
    uint32_t     lureBonus_     = 0;
    uint32_t     catchCount_    = 0;
    uint32_t     attemptsCount_ = 0;
};

}

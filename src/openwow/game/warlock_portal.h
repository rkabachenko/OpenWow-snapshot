
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class RitualState : uint8_t {
    Inactive          = 0,
    WaitingForClickers = 1,
    Complete          = 2,
    Failed            = 3,
};

class RitualOfSummoning {
public:

    void StartRitual(ObjectGuid caster, ObjectGuid target,
                     const std::string& targetName);

    [[nodiscard]] RitualState GetState() const;

    bool AddClicker(ObjectGuid clicker, const std::string& name);

    [[nodiscard]] const std::vector<std::pair<ObjectGuid, std::string>>& GetClickers() const;

    [[nodiscard]] std::size_t GetClickerCount() const;

    [[nodiscard]] std::size_t GetRequiredClickers() const;

    [[nodiscard]] bool IsComplete() const;

    [[nodiscard]] const std::string& GetTargetName() const;

    [[nodiscard]] ObjectGuid GetCasterGuid() const;

    void Update(float dt);

    [[nodiscard]] float GetTimeRemaining() const;

    void Reset();

private:
    static constexpr std::size_t kRequiredClickers = 2;
    static constexpr float       kMaxDuration      = 120.0f;

    RitualState state_ = RitualState::Inactive;
    ObjectGuid  caster_;
    ObjectGuid  target_;
    std::string target_name_;
    std::vector<std::pair<ObjectGuid, std::string>> clickers_;
    float       elapsed_ = 0.0f;
};

}

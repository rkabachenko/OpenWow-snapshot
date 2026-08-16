
#pragma once

#include <cstdint>
#include <string>

#include "openwow/game/object_guid.h"

namespace openwow::game {

class AssistTargetSystem {
public:

    void SetMainAssist(ObjectGuid guid);
    [[nodiscard]] ObjectGuid GetMainAssist() const;
    [[nodiscard]] bool HasMainAssist() const;
    void ClearMainAssist();

    void SetMainTank(ObjectGuid guid);
    [[nodiscard]] ObjectGuid GetMainTank() const;
    [[nodiscard]] bool HasMainTank() const;
    void ClearMainTank();

    void SetTargetOfTarget(ObjectGuid guid);
    [[nodiscard]] ObjectGuid GetTargetOfTarget() const;
    [[nodiscard]] bool HasTargetOfTarget() const;
    void ClearTargetOfTarget();

    void SetTargetOfTargetName(const std::string& name);
    [[nodiscard]] const std::string& GetTargetOfTargetName() const;

    void SetTargetOfTargetHealth(std::uint32_t current, std::uint32_t max);
    [[nodiscard]] float GetTargetOfTargetHealthPercent() const;

    void SetMainAssistTarget(ObjectGuid guid);
    [[nodiscard]] ObjectGuid GetMainAssistTarget() const;

    void SetMainTankTarget(ObjectGuid guid);
    [[nodiscard]] ObjectGuid GetMainTankTarget() const;

    void Reset();

private:
    ObjectGuid    mainAssist_;
    ObjectGuid    mainTank_;
    ObjectGuid    targetOfTarget_;
    std::string   totName_;
    std::uint32_t totHealthCurrent_ = 0;
    std::uint32_t totHealthMax_     = 0;
    ObjectGuid    mainAssistTarget_;
    ObjectGuid    mainTankTarget_;
};

}


#include "openwow/game/assist_target.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

void AssistTargetSystem::SetMainAssist(ObjectGuid guid) {

    if (mainAssist_.GetRawValue() != guid.GetRawValue()) {
        mainAssistTarget_ = ObjectGuid{};
    }
    mainAssist_ = guid;
}

ObjectGuid AssistTargetSystem::GetMainAssist() const {
    return mainAssist_;
}

bool AssistTargetSystem::HasMainAssist() const {
    return !mainAssist_.IsEmpty();
}

void AssistTargetSystem::ClearMainAssist() {
    mainAssist_       = ObjectGuid{};
    mainAssistTarget_ = ObjectGuid{};
}

void AssistTargetSystem::SetMainTank(ObjectGuid guid) {

    if (mainTank_.GetRawValue() != guid.GetRawValue()) {
        mainTankTarget_ = ObjectGuid{};
    }
    mainTank_ = guid;
}

ObjectGuid AssistTargetSystem::GetMainTank() const {
    return mainTank_;
}

bool AssistTargetSystem::HasMainTank() const {
    return !mainTank_.IsEmpty();
}

void AssistTargetSystem::ClearMainTank() {
    mainTank_       = ObjectGuid{};
    mainTankTarget_ = ObjectGuid{};
}

void AssistTargetSystem::SetTargetOfTarget(ObjectGuid guid) {

    if (targetOfTarget_.GetRawValue() != guid.GetRawValue()) {
        totHealthCurrent_ = 0;
        totHealthMax_     = 0;
        totName_.clear();
    }
    targetOfTarget_ = guid;
}

ObjectGuid AssistTargetSystem::GetTargetOfTarget() const {
    return targetOfTarget_;
}

bool AssistTargetSystem::HasTargetOfTarget() const {
    return !targetOfTarget_.IsEmpty();
}

void AssistTargetSystem::ClearTargetOfTarget() {
    targetOfTarget_   = ObjectGuid{};
    totName_.clear();
    totHealthCurrent_ = 0;
    totHealthMax_     = 0;
}

void AssistTargetSystem::SetTargetOfTargetName(const std::string& name) {
    totName_ = name;
}

const std::string& AssistTargetSystem::GetTargetOfTargetName() const {
    return totName_;
}

void AssistTargetSystem::SetTargetOfTargetHealth(std::uint32_t current,
                                                 std::uint32_t max) {

    totHealthMax_     = max;
    totHealthCurrent_ = (current > max) ? max : current;
}

float AssistTargetSystem::GetTargetOfTargetHealthPercent() const {
    if (totHealthMax_ == 0) return 0.0f;
    const float pct = static_cast<float>(totHealthCurrent_) /
                      static_cast<float>(totHealthMax_) * 100.0f;

    return std::clamp(pct, 0.0f, 100.0f);
}

void AssistTargetSystem::SetMainAssistTarget(ObjectGuid guid) {
    mainAssistTarget_ = guid;
}

ObjectGuid AssistTargetSystem::GetMainAssistTarget() const {
    return mainAssistTarget_;
}

void AssistTargetSystem::SetMainTankTarget(ObjectGuid guid) {
    mainTankTarget_ = guid;
}

ObjectGuid AssistTargetSystem::GetMainTankTarget() const {
    return mainTankTarget_;
}

void AssistTargetSystem::Reset() {
    mainAssist_       = ObjectGuid{};
    mainTank_         = ObjectGuid{};
    targetOfTarget_   = ObjectGuid{};
    totName_.clear();
    totHealthCurrent_ = 0;
    totHealthMax_     = 0;
    mainAssistTarget_ = ObjectGuid{};
    mainTankTarget_   = ObjectGuid{};
}

}

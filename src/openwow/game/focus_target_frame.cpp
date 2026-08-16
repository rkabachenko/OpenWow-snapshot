
#include "openwow/game/focus_target_frame.h"

#include <algorithm>
#include <iterator>

namespace openwow::game {

void FocusTargetFrame::SetFocus(const FocusFrameData& data) {
    focus_    = data;
    hasFocus_ = true;

    castInfo_.reset();
    auras_.clear();
}

void FocusTargetFrame::ClearFocus() {
    hasFocus_ = false;
    focus_    = {};
    castInfo_.reset();
    auras_.clear();
}

bool FocusTargetFrame::HasFocus() const { return hasFocus_; }

std::optional<FocusFrameData> FocusTargetFrame::GetFocus() const {
    if (!hasFocus_) return std::nullopt;
    return focus_;
}

std::uint64_t FocusTargetFrame::GetFocusGuid() const {
    return hasFocus_ ? focus_.guid : 0;
}

void FocusTargetFrame::UpdateHealth(std::uint32_t current, std::uint32_t max) {
    if (!hasFocus_) return;
    focus_.healthCurrent = current;
    focus_.healthMax     = max;
}

void FocusTargetFrame::UpdatePower(std::uint32_t current, std::uint32_t max) {
    if (!hasFocus_) return;
    focus_.powerCurrent = current;
    focus_.powerMax     = max;
}

float FocusTargetFrame::GetHealthPercent() const {
    if (!hasFocus_ || focus_.healthMax == 0) return 0.0f;
    return static_cast<float>(focus_.healthCurrent) /
           static_cast<float>(focus_.healthMax) * 100.0f;
}

float FocusTargetFrame::GetPowerPercent() const {
    if (!hasFocus_ || focus_.powerMax == 0) return 0.0f;
    return static_cast<float>(focus_.powerCurrent) /
           static_cast<float>(focus_.powerMax) * 100.0f;
}

void FocusTargetFrame::SetCastInfo(const FocusTargetCastInfo& info) {
    castInfo_ = info;
}

void FocusTargetFrame::ClearCast() {
    castInfo_.reset();
}

std::optional<FocusTargetCastInfo> FocusTargetFrame::GetCastInfo() const {
    return castInfo_;
}

bool FocusTargetFrame::IsCasting() const {
    return castInfo_.has_value();
}

void FocusTargetFrame::SetAuras(const std::vector<UnitFrameAuraIcon>& auras) {
    auras_ = auras;
}

const std::vector<UnitFrameAuraIcon>& FocusTargetFrame::GetAuras() const {
    return auras_;
}

std::vector<UnitFrameAuraIcon> FocusTargetFrame::GetDebuffs() const {
    std::vector<UnitFrameAuraIcon> debuffs;
    std::copy_if(auras_.begin(), auras_.end(), std::back_inserter(debuffs),
                 [](const UnitFrameAuraIcon& a) { return a.isDebuff; });
    return debuffs;
}

bool FocusTargetFrame::IsHostile() const {
    return hasFocus_ && focus_.reaction == 0;
}

bool FocusTargetFrame::IsFriendly() const {
    return hasFocus_ && focus_.reaction == 2;
}

}

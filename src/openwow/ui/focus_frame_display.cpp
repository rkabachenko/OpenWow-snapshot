
#include "openwow/ui/focus_frame_display.h"

namespace openwow::ui {

void FocusFrameDisplay::SetFocusData(const FocusFrameDisplayData& data) {
    data_ = data;
}

void FocusFrameDisplay::ClearFocus() {
    data_.reset();
}

std::optional<FocusFrameDisplayData> FocusFrameDisplay::GetFocusData() const {
    return data_;
}

bool FocusFrameDisplay::HasFocus() const {
    return data_.has_value();
}

openwow::game::ObjectGuid FocusFrameDisplay::GetFocusGuid() const {
    if (data_) return data_->guid;
    return openwow::game::ObjectGuid{};
}

void FocusFrameDisplay::UpdateHealth(std::int32_t current, std::int32_t max) {
    if (!data_) return;
    data_->health    = current;
    data_->healthMax = max;
}

void FocusFrameDisplay::UpdatePower(std::int32_t current, std::int32_t max,
                                    std::uint8_t type) {
    if (!data_) return;
    data_->power     = current;
    data_->powerMax  = max;
    data_->powerType = type;
}

void FocusFrameDisplay::UpdateCast(const std::string& spellName,
                                   float progress) {
    if (!data_) return;
    data_->castingSpellName = spellName;
    data_->castProgress     = progress;
    data_->isCasting        = true;
}

void FocusFrameDisplay::ClearCast() {
    if (!data_) return;
    data_->castingSpellName.clear();
    data_->castProgress = 0.0f;
    data_->isCasting    = false;
}

void FocusFrameDisplay::AddAura(std::uint32_t auraId) {
    if (!data_) return;

    auto& auras = data_->auraIds;
    if (std::find(auras.begin(), auras.end(), auraId) == auras.end()) {
        auras.push_back(auraId);
    }
}

void FocusFrameDisplay::RemoveAura(std::uint32_t auraId) {
    if (!data_) return;
    auto& auras = data_->auraIds;
    auras.erase(std::remove(auras.begin(), auras.end(), auraId), auras.end());
}

std::size_t FocusFrameDisplay::GetAuraCount() const {
    if (!data_) return 0;
    return data_->auraIds.size();
}

float FocusFrameDisplay::GetHealthPercent() const {
    if (!data_ || data_->healthMax <= 0) return 0.0f;
    return static_cast<float>(data_->health) /
           static_cast<float>(data_->healthMax) * 100.0f;
}

float FocusFrameDisplay::GetPowerPercent() const {
    if (!data_ || data_->powerMax <= 0) return 0.0f;
    return static_cast<float>(data_->power) /
           static_cast<float>(data_->powerMax) * 100.0f;
}

void FocusFrameDisplay::Reset() {
    data_.reset();
}

}


#include "openwow/game/inspect_system.h"

#include <numeric>

namespace openwow::game {

void InspectSystem::RequestInspect(ObjectGuid target) {
    std::lock_guard lock(mutex_);
    data_.reset();
    target_           = target;
    pending_          = true;
    timeSinceRequest_ = 0.0f;
}

bool InspectSystem::IsInspecting() const {
    std::lock_guard lock(mutex_);
    return !target_.IsEmpty();
}

ObjectGuid InspectSystem::GetInspectTarget() const {
    std::lock_guard lock(mutex_);
    return target_;
}

bool InspectSystem::IsRequestPending() const {
    std::lock_guard lock(mutex_);
    return pending_;
}

void InspectSystem::SetRequestPending(bool pending) {
    std::lock_guard lock(mutex_);
    pending_ = pending;
}

void InspectSystem::SetInspectData(const InspectData& data) {
    std::lock_guard lock(mutex_);
    data_    = data;
    pending_ = false;
}

std::optional<InspectData> InspectSystem::GetInspectData() const {
    std::lock_guard lock(mutex_);
    return data_;
}

bool InspectSystem::HasInspectData() const {
    std::lock_guard lock(mutex_);
    return data_.has_value();
}

std::optional<InspectSlot> InspectSystem::GetInspectSlot(uint32_t slot) const {
    std::lock_guard lock(mutex_);
    if (!data_ || slot >= InspectData::kMaxEquipSlots) return std::nullopt;
    const auto& s = data_->slots[slot];
    if (s.itemId == 0) return std::nullopt;
    return s;
}

std::vector<InspectTalent> InspectSystem::GetInspectTalents() const {
    std::lock_guard lock(mutex_);
    if (!data_) return {};
    return data_->talents;
}

float InspectSystem::GetInspectItemLevel() const {
    std::lock_guard lock(mutex_);
    if (!data_) return 0.0f;

    uint32_t count = 0;
    uint32_t total = 0;
    for (const auto& s : data_->slots) {
        if (s.itemId != 0) {
            total += s.itemLevel;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(total) / static_cast<float>(count) : 0.0f;
}

float InspectSystem::GetTimeSinceRequest() const {
    std::lock_guard lock(mutex_);
    return timeSinceRequest_;
}

void InspectSystem::Update(float dt) {
    std::lock_guard lock(mutex_);
    if (pending_) {
        timeSinceRequest_ += dt;

        if (timeSinceRequest_ >= 10.0f) {
            pending_ = false;
        }
    }
}

void InspectSystem::ClearInspect() {
    std::lock_guard lock(mutex_);
    data_.reset();
    target_           = ObjectGuid{};
    pending_          = false;
    timeSinceRequest_ = 0.0f;
}

void InspectSystem::Reset() {
    ClearInspect();
}

}


#include "openwow/game/zone_text_system.h"

#include <algorithm>

namespace openwow::game {

ZoneTextSystem& ZoneTextSystem::Get() {
    static ZoneTextSystem instance;
    return instance;
}

void ZoneTextSystem::SetCurrentZone(std::uint32_t zoneId,
                                     const std::string& zoneName,
                                     PvPStatus status) {
    std::lock_guard lock(mutex_);
    zone_id_ = zoneId;
    zone_name_ = zoneName;
    pvp_status_ = status;

    state_ = ZoneDisplayState::FadingIn;
    timer_ = 0.0f;
}

void ZoneTextSystem::SetCurrentSubZone(std::uint32_t subZoneId,
                                        const std::string& subZoneName) {
    std::lock_guard lock(mutex_);
    subzone_id_ = subZoneId;
    subzone_name_ = subZoneName;

    state_ = ZoneDisplayState::FadingIn;
    timer_ = 0.0f;
}

std::string ZoneTextSystem::GetCurrentZoneName() const {
    std::lock_guard lock(mutex_);
    return zone_name_;
}

std::string ZoneTextSystem::GetCurrentSubZoneName() const {
    std::lock_guard lock(mutex_);
    return subzone_name_;
}

std::uint32_t ZoneTextSystem::GetCurrentZoneId() const {
    std::lock_guard lock(mutex_);
    return zone_id_;
}

std::uint32_t ZoneTextSystem::GetCurrentSubZoneId() const {
    std::lock_guard lock(mutex_);
    return subzone_id_;
}

PvPStatus ZoneTextSystem::GetPvPStatus() const {
    std::lock_guard lock(mutex_);
    return pvp_status_;
}

std::uint32_t ZoneTextSystem::GetPvPStatusColor() const {
    std::lock_guard lock(mutex_);
    switch (pvp_status_) {
        case PvPStatus::Friendly:   return 0xFF00FF00;
        case PvPStatus::Hostile:    return 0xFFFF0000;
        case PvPStatus::Contested:  return 0xFFFF8800;
        case PvPStatus::Sanctuary:  return 0xFF6688FF;
        case PvPStatus::FreeForAll: return 0xFFFF0000;
        case PvPStatus::Combat:     return 0xFFFF0000;
    }
    return 0xFFFFFFFF;
}

ZoneDisplayState ZoneTextSystem::GetDisplayState() const {
    std::lock_guard lock(mutex_);
    return state_;
}

float ZoneTextSystem::GetDisplayAlpha() const {
    std::lock_guard lock(mutex_);
    switch (state_) {
        case ZoneDisplayState::Hidden:
            return 0.0f;
        case ZoneDisplayState::FadingIn:
            return std::clamp(timer_ / kFadeInDuration, 0.0f, 1.0f);
        case ZoneDisplayState::Visible:
            return 1.0f;
        case ZoneDisplayState::FadingOut:
            return std::clamp(1.0f - timer_ / kFadeOutDuration, 0.0f, 1.0f);
    }
    return 0.0f;
}

bool ZoneTextSystem::IsDisplayVisible() const {
    std::lock_guard lock(mutex_);
    return state_ != ZoneDisplayState::Hidden;
}

void ZoneTextSystem::SetInInstance(bool in_instance) {
    std::lock_guard lock(mutex_);
    in_instance_ = in_instance;
}

bool ZoneTextSystem::IsInInstance() const {
    std::lock_guard lock(mutex_);
    return in_instance_;
}

void ZoneTextSystem::SetInstanceName(const std::string& name) {
    std::lock_guard lock(mutex_);
    instance_name_ = name;
}

std::string ZoneTextSystem::GetInstanceName() const {
    std::lock_guard lock(mutex_);
    return instance_name_;
}

void ZoneTextSystem::SetZoneLevel(const std::string& level) {
    std::lock_guard lock(mutex_);
    zone_level_ = level;
}

std::string ZoneTextSystem::GetZoneLevel() const {
    std::lock_guard lock(mutex_);
    return zone_level_;
}

void ZoneTextSystem::Update(float dt) {
    std::lock_guard lock(mutex_);
    if (state_ == ZoneDisplayState::Hidden) return;

    timer_ += dt;

    switch (state_) {
        case ZoneDisplayState::FadingIn:
            if (timer_ >= kFadeInDuration) {
                state_ = ZoneDisplayState::Visible;
                timer_ = 0.0f;
            }
            break;
        case ZoneDisplayState::Visible:
            if (timer_ >= kVisibleDuration) {
                state_ = ZoneDisplayState::FadingOut;
                timer_ = 0.0f;
            }
            break;
        case ZoneDisplayState::FadingOut:
            if (timer_ >= kFadeOutDuration) {
                state_ = ZoneDisplayState::Hidden;
                timer_ = 0.0f;
            }
            break;
        case ZoneDisplayState::Hidden:
            break;
    }
}

void ZoneTextSystem::Reset() {
    std::lock_guard lock(mutex_);
    zone_id_ = 0;
    zone_name_.clear();
    subzone_id_ = 0;
    subzone_name_.clear();
    pvp_status_ = PvPStatus::Friendly;
    in_instance_ = false;
    instance_name_.clear();
    zone_level_.clear();
    state_ = ZoneDisplayState::Hidden;
    timer_ = 0.0f;
}

}

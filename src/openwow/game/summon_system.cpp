
#include "openwow/game/summon_system.h"

#include <cstdio>

namespace openwow::game {

SummonSystem& SummonSystem::Get() {
    static SummonSystem instance;
    return instance;
}

void SummonSystem::SetPendingSummon(ObjectGuid summoner,
                                     const std::string& summonerName,
                                     uint32_t mapId,
                                     SummonType type,
                                     float timeout) {
    std::lock_guard lock(mutex_);
    state_ = SummonState::Pending;
    summoner_ = summoner;
    summoner_name_ = summonerName;
    map_id_ = mapId;
    type_ = type;
    timeout_ = timeout;
    elapsed_ = 0.0f;
}

bool SummonSystem::HasPendingSummon() const {
    std::lock_guard lock(mutex_);
    return state_ == SummonState::Pending;
}

void SummonSystem::AcceptSummon() {
    std::lock_guard lock(mutex_);
    if (state_ == SummonState::Pending) {
        state_ = SummonState::Accepted;
    }
}

void SummonSystem::DeclineSummon() {
    std::lock_guard lock(mutex_);
    if (state_ == SummonState::Pending) {
        state_ = SummonState::Declined;
    }
}

SummonState SummonSystem::GetSummonState() const {
    std::lock_guard lock(mutex_);
    return state_;
}

ObjectGuid SummonSystem::GetSummoner() const {
    std::lock_guard lock(mutex_);
    return summoner_;
}

const std::string& SummonSystem::GetSummonerName() const {
    std::lock_guard lock(mutex_);
    return summoner_name_;
}

SummonType SummonSystem::GetSummonType() const {
    std::lock_guard lock(mutex_);
    return type_;
}

uint32_t SummonSystem::GetMapId() const {
    std::lock_guard lock(mutex_);
    return map_id_;
}

float SummonSystem::GetTimeRemaining() const {
    std::lock_guard lock(mutex_);
    if (state_ != SummonState::Pending) return 0.0f;
    float remaining = timeout_ - elapsed_;
    return remaining > 0.0f ? remaining : 0.0f;
}

void SummonSystem::Update(float dt) {
    std::lock_guard lock(mutex_);
    if (state_ != SummonState::Pending) return;
    elapsed_ += dt;
    if (elapsed_ >= timeout_) {
        state_ = SummonState::Expired;
    }
}

void SummonSystem::Reset() {
    std::lock_guard lock(mutex_);
    state_ = SummonState::None;
    summoner_ = ObjectGuid{};
    summoner_name_.clear();
    type_ = SummonType::MeetingStone;
    map_id_ = 0;
    timeout_ = 0.0f;
    elapsed_ = 0.0f;
}

std::string SummonSystem::GetSummonTypeName(SummonType type) {
    switch (type) {
        case SummonType::MeetingStone: return "Meeting Stone";
        case SummonType::Warlock:      return "Warlock";
        case SummonType::ReferAFriend:  return "Refer-a-Friend";
    }
    return "Unknown";
}

std::string SummonSystem::GetStatusText() const {
    std::lock_guard lock(mutex_);
    switch (state_) {
        case SummonState::None:     return "No summon pending";
        case SummonState::Accepted: return "Summon accepted";
        case SummonState::Declined: return "Summon declined";
        case SummonState::Expired:  return "Summon expired";
        case SummonState::Pending: {
            float remaining = timeout_ - elapsed_;
            if (remaining < 0.0f) remaining = 0.0f;
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "Pending summon from %s (%s, %.0fs remaining)",
                          summoner_name_.c_str(),
                          GetSummonTypeName(type_).c_str(),
                          remaining);
            return buf;
        }
    }
    return "Unknown state";
}

std::string SummonSystem::GetFormattedTimeRemaining() const {
    std::lock_guard lock(mutex_);
    if (state_ != SummonState::Pending) return "0:00";

    float remaining = timeout_ - elapsed_;
    if (remaining < 0.0f) remaining = 0.0f;

    int total_sec = static_cast<int>(remaining);
    int minutes = total_sec / 60;
    int seconds = total_sec % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", minutes, seconds);
    return buf;
}

std::string SummonSystem::GetDescription() const {
    std::lock_guard lock(mutex_);
    if (state_ != SummonState::Pending) return {};

    return summoner_name_ + " has requested to summon you via " +
           GetSummonTypeName(type_) + ".";
}

float SummonSystem::GetElapsed() const {
    std::lock_guard lock(mutex_);
    return elapsed_;
}

float SummonSystem::GetTimeout() const {
    std::lock_guard lock(mutex_);
    return timeout_;
}

}


#include "openwow/game/summon_request.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace openwow::game {

void SummonRequest::SetRequest(SummonRequestType type,
                               ObjectGuid summoner,
                               const std::string& summonerName,
                               const std::string& location,
                               float duration) {
    state_          = RequestState::Pending;
    type_           = type;
    summoner_guid_  = summoner;
    summoner_name_  = summonerName;
    location_       = location;
    duration_       = (duration > 0.0f) ? duration : 0.0f;
    elapsed_        = 0.0f;
    map_id_         = 0;
    zone_id_        = 0;
}

bool SummonRequest::HasPending() const {
    return state_ == RequestState::Pending;
}

SummonRequestType SummonRequest::GetType() const {
    return type_;
}

const std::string& SummonRequest::GetSummonerName() const {
    return summoner_name_;
}

const std::string& SummonRequest::GetLocation() const {
    return location_;
}

ObjectGuid SummonRequest::GetSummonerGuid() const {
    return summoner_guid_;
}

float SummonRequest::GetTimeRemaining() const {
    if (state_ != RequestState::Pending) return 0.0f;
    float remaining = duration_ - elapsed_;
    return remaining > 0.0f ? remaining : 0.0f;
}

void SummonRequest::Update(float dt) {
    if (state_ != RequestState::Pending) return;
    if (dt <= 0.0f) return;
    elapsed_ += dt;
    if (elapsed_ >= duration_) {
        elapsed_ = duration_;
        state_   = RequestState::Expired;
    }
}

bool SummonRequest::IsExpired() const {
    return state_ == RequestState::Expired;
}

float SummonRequest::GetProgress() const {
    if (duration_ <= 0.0f) return 1.0f;
    float p = elapsed_ / duration_;
    if (p < 0.0f) return 0.0f;
    if (p > 1.0f) return 1.0f;
    return p;
}

void SummonRequest::AcceptSummon() {
    if (state_ == RequestState::Pending) {
        state_ = RequestState::Accepted;
    }
}

void SummonRequest::DeclineSummon() {
    if (state_ == RequestState::Pending) {
        state_ = RequestState::Declined;
    }
}

bool SummonRequest::HasBeenAnswered() const {
    return state_ == RequestState::Accepted || state_ == RequestState::Declined;
}

bool SummonRequest::WasAccepted() const {
    return state_ == RequestState::Accepted;
}

bool SummonRequest::WasDeclined() const {
    return state_ == RequestState::Declined;
}

void SummonRequest::SetMapZone(uint32_t mapId, uint32_t zoneId) {
    map_id_  = mapId;
    zone_id_ = zoneId;
}

uint32_t SummonRequest::GetMapId() const {
    return map_id_;
}

uint32_t SummonRequest::GetZoneId() const {
    return zone_id_;
}

std::string SummonRequest::GetTypeString() const {
    switch (type_) {
        case SummonRequestType::MeetingStone:       return "Meeting Stone";
        case SummonRequestType::WarlockSummon:      return "Warlock Summon";
        case SummonRequestType::SpellSummon:         return "Spell Summon";
        case SummonRequestType::RandomDungeon:       return "Random Dungeon";
        default:                                     return "Unknown";
    }
}

std::string SummonRequest::GetFormattedTimeRemaining() const {
    float rem = GetTimeRemaining();
    if (rem <= 0.0f) return "0:00";

    int totalSeconds = static_cast<int>(std::ceil(rem));
    int minutes      = totalSeconds / 60;
    int seconds      = totalSeconds % 60;

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", minutes, seconds);
    return std::string(buf);
}

std::string SummonRequest::GetDisplayText() const {
    if (summoner_name_.empty() && location_.empty()) {
        return {};
    }

    return summoner_name_ + " wants to summon you to " + location_ + ".";
}

void SummonRequest::Reset() {
    state_          = RequestState::None;
    type_           = SummonRequestType::MeetingStone;
    summoner_guid_  = ObjectGuid{};
    summoner_name_.clear();
    location_.clear();
    duration_       = 0.0f;
    elapsed_        = 0.0f;
    map_id_         = 0;
    zone_id_        = 0;
}

}


#include "openwow/game/ready_check.h"

#include <algorithm>

namespace openwow::game {

ReadyCheckSystem& ReadyCheckSystem::Get() {
    static ReadyCheckSystem instance;
    return instance;
}

void ReadyCheckSystem::StartReadyCheck(ObjectGuid initiator, float timeout) {
    std::lock_guard lock(mutex_);
    state_ = ReadyCheckState::InProgress;
    initiator_ = initiator;
    timeout_ = timeout;
    elapsed_ = 0.0f;
    responses_.clear();
}

void ReadyCheckSystem::RespondReady(ObjectGuid member, ReadyResponse response) {
    std::lock_guard lock(mutex_);
    if (state_ != ReadyCheckState::InProgress) return;
    responses_[member.GetRawValue()] = response;
}

ReadyCheckState ReadyCheckSystem::GetState() const {
    std::lock_guard lock(mutex_);
    return state_;
}

ObjectGuid ReadyCheckSystem::GetInitiator() const {
    std::lock_guard lock(mutex_);
    return initiator_;
}

ReadyResponse ReadyCheckSystem::GetResponse(ObjectGuid member) const {
    std::lock_guard lock(mutex_);
    auto it = responses_.find(member.GetRawValue());
    if (it != responses_.end()) return it->second;
    return ReadyResponse::None;
}

std::map<uint64_t, ReadyResponse> ReadyCheckSystem::GetAllResponses() const {
    std::lock_guard lock(mutex_);
    return responses_;
}

uint32_t ReadyCheckSystem::GetReadyCount() const {
    std::lock_guard lock(mutex_);
    uint32_t count = 0;
    for (const auto& [_, r] : responses_) {
        if (r == ReadyResponse::Ready) ++count;
    }
    return count;
}

uint32_t ReadyCheckSystem::GetNotReadyCount() const {
    std::lock_guard lock(mutex_);
    uint32_t count = 0;
    for (const auto& [_, r] : responses_) {
        if (r == ReadyResponse::NotReady || r == ReadyResponse::AFK) ++count;
    }
    return count;
}

uint32_t ReadyCheckSystem::GetPendingCount() const {
    std::lock_guard lock(mutex_);
    uint32_t none_count = 0;
    for (const auto& [_, r] : responses_) {
        if (r == ReadyResponse::None) ++none_count;
    }
    return none_count;
}

bool ReadyCheckSystem::IsAllReady() const {
    std::lock_guard lock(mutex_);
    if (responses_.empty()) return false;
    return std::all_of(responses_.begin(), responses_.end(),
                       [](const auto& p) {
                           return p.second == ReadyResponse::Ready;
                       });
}

float ReadyCheckSystem::GetTimeRemaining() const {
    std::lock_guard lock(mutex_);
    if (state_ != ReadyCheckState::InProgress) return 0.0f;
    float remaining = timeout_ - elapsed_;
    return remaining > 0.0f ? remaining : 0.0f;
}

void ReadyCheckSystem::Update(float dt) {
    std::lock_guard lock(mutex_);
    if (state_ != ReadyCheckState::InProgress) return;
    elapsed_ += dt;
    if (elapsed_ >= timeout_) {
        state_ = ReadyCheckState::Complete;
    }
}

void ReadyCheckSystem::EndReadyCheck() {
    std::lock_guard lock(mutex_);
    if (state_ == ReadyCheckState::InProgress) {
        state_ = ReadyCheckState::Complete;
    }
}

void ReadyCheckSystem::Reset() {
    std::lock_guard lock(mutex_);
    state_ = ReadyCheckState::Inactive;
    initiator_ = ObjectGuid{};
    timeout_ = 0.0f;
    elapsed_ = 0.0f;
    responses_.clear();
}

}

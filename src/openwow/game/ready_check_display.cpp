
#include "openwow/game/ready_check_display.h"

#include <algorithm>

namespace openwow::game {

void ReadyCheckDisplay::StartReadyCheck(ObjectGuid initiator,
                                         std::string initiatorName,
                                         float duration) {
    std::lock_guard lock(mutex_);
    active_ = true;
    initiatorGuid_ = initiator;
    initiatorName_ = std::move(initiatorName);
    duration_ = duration;
    elapsed_ = 0.0f;
    myResponse_ = ReadyCheckResponseType::NoResponse;
    responses_.clear();
}

bool ReadyCheckDisplay::IsActive() const {
    std::lock_guard lock(mutex_);
    return active_;
}

std::string ReadyCheckDisplay::GetInitiator() const {
    std::lock_guard lock(mutex_);
    return initiatorName_;
}

void ReadyCheckDisplay::SetMyResponse(ReadyCheckResponseType response) {
    std::lock_guard lock(mutex_);
    myResponse_ = response;
}

ReadyCheckResponseType ReadyCheckDisplay::GetMyResponse() const {
    std::lock_guard lock(mutex_);
    return myResponse_;
}

void ReadyCheckDisplay::AddMemberResponse(ReadyCheckMemberResponse response) {
    std::lock_guard lock(mutex_);

    auto it = std::find_if(responses_.begin(), responses_.end(),
                           [&](const auto& r) {
                               return r.guid == response.guid;
                           });
    if (it != responses_.end()) {
        it->response = response.response;
        it->name = std::move(response.name);
    } else {
        responses_.push_back(std::move(response));
    }
}

std::vector<ReadyCheckMemberResponse> ReadyCheckDisplay::GetResponses() const {
    std::lock_guard lock(mutex_);
    return responses_;
}

std::size_t ReadyCheckDisplay::GetReadyCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(std::count_if(
        responses_.begin(), responses_.end(),
        [](const auto& r) {
            return r.response == ReadyCheckResponseType::Ready;
        }));
}

std::size_t ReadyCheckDisplay::GetNotReadyCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(std::count_if(
        responses_.begin(), responses_.end(),
        [](const auto& r) {
            return r.response == ReadyCheckResponseType::NotReady;
        }));
}

std::size_t ReadyCheckDisplay::GetNoResponseCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(std::count_if(
        responses_.begin(), responses_.end(),
        [](const auto& r) {
            return r.response == ReadyCheckResponseType::NoResponse;
        }));
}

std::size_t ReadyCheckDisplay::GetTotalCount() const {
    std::lock_guard lock(mutex_);
    return responses_.size();
}

float ReadyCheckDisplay::GetReadyPercent() const {
    std::lock_guard lock(mutex_);
    if (responses_.empty()) return 0.0f;
    auto readyCount = std::count_if(
        responses_.begin(), responses_.end(),
        [](const auto& r) {
            return r.response == ReadyCheckResponseType::Ready;
        });
    return static_cast<float>(readyCount) /
           static_cast<float>(responses_.size()) * 100.0f;
}

void ReadyCheckDisplay::Update(float dt) {
    std::lock_guard lock(mutex_);
    if (!active_) return;
    elapsed_ += dt;
    if (elapsed_ >= duration_) {
        elapsed_ = duration_;
    }
}

float ReadyCheckDisplay::GetTimeRemaining() const {
    std::lock_guard lock(mutex_);
    if (!active_) return 0.0f;
    float remaining = duration_ - elapsed_;
    return remaining > 0.0f ? remaining : 0.0f;
}

bool ReadyCheckDisplay::IsExpired() const {
    std::lock_guard lock(mutex_);
    if (!active_) return false;
    return elapsed_ >= duration_;
}

void ReadyCheckDisplay::Reset() {
    std::lock_guard lock(mutex_);
    active_ = false;
    initiatorGuid_ = ObjectGuid{};
    initiatorName_.clear();
    duration_ = 0.0f;
    elapsed_ = 0.0f;
    myResponse_ = ReadyCheckResponseType::NoResponse;
    responses_.clear();
}

}

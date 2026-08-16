
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class ReadyCheckResponseType : std::uint8_t {
    NotReady   = 0,
    Ready      = 1,
    AFK        = 2,
    NoResponse = 3,
};

struct ReadyCheckMemberResponse {
    ObjectGuid guid;
    std::string name;
    ReadyCheckResponseType response = ReadyCheckResponseType::NoResponse;
};

class ReadyCheckDisplay {
 public:
    void StartReadyCheck(ObjectGuid initiator, std::string initiatorName,
                         float duration);
    [[nodiscard]] bool IsActive() const;
    [[nodiscard]] std::string GetInitiator() const;

    void SetMyResponse(ReadyCheckResponseType response);
    [[nodiscard]] ReadyCheckResponseType GetMyResponse() const;

    void AddMemberResponse(ReadyCheckMemberResponse response);
    [[nodiscard]] std::vector<ReadyCheckMemberResponse> GetResponses() const;

    [[nodiscard]] std::size_t GetReadyCount() const;
    [[nodiscard]] std::size_t GetNotReadyCount() const;
    [[nodiscard]] std::size_t GetNoResponseCount() const;
    [[nodiscard]] std::size_t GetTotalCount() const;
    [[nodiscard]] float GetReadyPercent() const;

    void Update(float dt);
    [[nodiscard]] float GetTimeRemaining() const;
    [[nodiscard]] bool IsExpired() const;

    void Reset();

 private:
    mutable std::mutex mutex_;
    bool active_ = false;
    ObjectGuid initiatorGuid_;
    std::string initiatorName_;
    float duration_ = 0.0f;
    float elapsed_ = 0.0f;
    ReadyCheckResponseType myResponse_ = ReadyCheckResponseType::NoResponse;
    std::vector<ReadyCheckMemberResponse> responses_;
};

}

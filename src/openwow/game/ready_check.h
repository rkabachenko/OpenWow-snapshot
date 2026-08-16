
#pragma once

#include <cstdint>
#include <map>
#include <mutex>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class ReadyResponse : uint8_t {
    None     = 0,
    Ready    = 1,
    NotReady = 2,
    AFK      = 3,
};

enum class ReadyCheckState : uint8_t {
    Inactive   = 0,
    InProgress = 1,
    Complete   = 2,
};

class ReadyCheckSystem {
 public:
    static ReadyCheckSystem& Get();

    void StartReadyCheck(ObjectGuid initiator, float timeout = 35.0f);

    void RespondReady(ObjectGuid member, ReadyResponse response);

    [[nodiscard]] ReadyCheckState GetState() const;

    [[nodiscard]] ObjectGuid GetInitiator() const;

    [[nodiscard]] ReadyResponse GetResponse(ObjectGuid member) const;

    [[nodiscard]] std::map<uint64_t, ReadyResponse> GetAllResponses() const;

    [[nodiscard]] uint32_t GetReadyCount() const;
    [[nodiscard]] uint32_t GetNotReadyCount() const;
    [[nodiscard]] uint32_t GetPendingCount() const;

    [[nodiscard]] bool IsAllReady() const;

    [[nodiscard]] float GetTimeRemaining() const;

    void Update(float dt);

    void EndReadyCheck();

    void Reset();

 private:
    ReadyCheckSystem() = default;

    ReadyCheckState state_ = ReadyCheckState::Inactive;
    ObjectGuid initiator_;
    float timeout_ = 0.0f;
    float elapsed_ = 0.0f;
    std::map<uint64_t, ReadyResponse> responses_;
    mutable std::mutex mutex_;
};

}

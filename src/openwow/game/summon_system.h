
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class SummonType : uint8_t {
    MeetingStone  = 0,
    Warlock       = 1,
    ReferAFriend  = 2,
};

enum class SummonState : uint8_t {
    None     = 0,
    Pending  = 1,
    Accepted = 2,
    Declined = 3,
    Expired  = 4,
};

class SummonSystem {
 public:
    static SummonSystem& Get();

    void SetPendingSummon(ObjectGuid summoner,
                          const std::string& summonerName,
                          uint32_t mapId,
                          SummonType type,
                          float timeout = 120.0f);

    [[nodiscard]] bool HasPendingSummon() const;

    void AcceptSummon();

    void DeclineSummon();

    [[nodiscard]] SummonState GetSummonState() const;

    [[nodiscard]] ObjectGuid GetSummoner() const;
    [[nodiscard]] const std::string& GetSummonerName() const;

    [[nodiscard]] SummonType GetSummonType() const;

    [[nodiscard]] uint32_t GetMapId() const;

    [[nodiscard]] float GetTimeRemaining() const;

    void Update(float dt);

    void Reset();

    [[nodiscard]] static std::string GetSummonTypeName(SummonType type);

    [[nodiscard]] std::string GetStatusText() const;

    [[nodiscard]] std::string GetFormattedTimeRemaining() const;

    [[nodiscard]] std::string GetDescription() const;

    [[nodiscard]] float GetElapsed() const;

    [[nodiscard]] float GetTimeout() const;

 private:
    SummonSystem() = default;

    SummonState state_ = SummonState::None;
    ObjectGuid summoner_;
    std::string summoner_name_;
    SummonType type_ = SummonType::MeetingStone;
    uint32_t map_id_ = 0;
    float timeout_ = 0.0f;
    float elapsed_ = 0.0f;
    mutable std::mutex mutex_;
};

}

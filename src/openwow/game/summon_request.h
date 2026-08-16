
#pragma once

#include <cstdint>
#include <string>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class SummonRequestType : uint8_t {
    MeetingStone  = 0,
    WarlockSummon = 1,
    SpellSummon   = 2,
    RandomDungeon = 3,
};

class SummonRequest {
public:

    void SetRequest(SummonRequestType type,
                    ObjectGuid summoner,
                    const std::string& summonerName,
                    const std::string& location,
                    float duration);

    [[nodiscard]] bool HasPending() const;

    [[nodiscard]] SummonRequestType GetType() const;

    [[nodiscard]] const std::string& GetSummonerName() const;

    [[nodiscard]] const std::string& GetLocation() const;

    [[nodiscard]] ObjectGuid GetSummonerGuid() const;

    [[nodiscard]] float GetTimeRemaining() const;

    void Update(float dt);

    [[nodiscard]] bool IsExpired() const;

    void AcceptSummon();

    void DeclineSummon();

    [[nodiscard]] std::string GetDisplayText() const;

    [[nodiscard]] std::string GetTypeString() const;

    [[nodiscard]] std::string GetFormattedTimeRemaining() const;

    [[nodiscard]] bool HasBeenAnswered() const;

    [[nodiscard]] bool WasAccepted() const;

    [[nodiscard]] bool WasDeclined() const;

    void SetMapZone(uint32_t mapId, uint32_t zoneId);
    [[nodiscard]] uint32_t GetMapId() const;
    [[nodiscard]] uint32_t GetZoneId() const;

    [[nodiscard]] float GetProgress() const;

    void Reset();

private:
    enum class RequestState : uint8_t {
        None = 0,
        Pending,
        Accepted,
        Declined,
        Expired,
    };

    RequestState        state_          = RequestState::None;
    SummonRequestType   type_           = SummonRequestType::MeetingStone;
    ObjectGuid          summoner_guid_;
    std::string         summoner_name_;
    std::string         location_;
    float               duration_       = 0.0f;
    float               elapsed_        = 0.0f;
    uint32_t            map_id_         = 0;
    uint32_t            zone_id_        = 0;
};

}

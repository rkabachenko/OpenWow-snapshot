
#pragma once

#include "openwow/game/object_guid.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct InspectSlot {
    uint32_t slotId    = 0;
    uint32_t itemId    = 0;
    uint32_t itemLevel = 0;
    uint32_t enchantId = 0;
    std::array<uint32_t, 3> gems = {};
    uint32_t displayId = 0;
};

struct InspectTalent {
    uint32_t talentId = 0;
    uint8_t rank      = 0;
};

struct InspectData {
    ObjectGuid guid;
    std::string name;
    uint32_t classId  = 0;
    uint32_t race     = 0;
    uint32_t level    = 0;
    std::string guildName;

    static constexpr uint8_t kMaxEquipSlots = 19;
    std::array<InspectSlot, kMaxEquipSlots> slots = {};

    std::vector<InspectTalent> talents;

    uint32_t achievementPoints = 0;
    uint32_t honorKills        = 0;
    uint32_t arenaRating2v2    = 0;
    uint32_t arenaRating3v3    = 0;
    uint32_t arenaRating5v5    = 0;
};

class InspectSystem {
public:
    InspectSystem() = default;

    void RequestInspect(ObjectGuid target);
    [[nodiscard]] bool IsInspecting() const;
    [[nodiscard]] ObjectGuid GetInspectTarget() const;
    [[nodiscard]] bool IsRequestPending() const;
    void SetRequestPending(bool pending);

    void SetInspectData(const InspectData& data);
    [[nodiscard]] std::optional<InspectData> GetInspectData() const;
    [[nodiscard]] bool HasInspectData() const;

    [[nodiscard]] std::optional<InspectSlot> GetInspectSlot(uint32_t slot) const;

    [[nodiscard]] std::vector<InspectTalent> GetInspectTalents() const;

    [[nodiscard]] float GetInspectItemLevel() const;

    [[nodiscard]] float GetTimeSinceRequest() const;
    void Update(float dt);

    void ClearInspect();
    void Reset();

private:
    std::optional<InspectData> data_;
    ObjectGuid target_;
    bool pending_           = false;
    float timeSinceRequest_ = 0.0f;
    mutable std::mutex mutex_;
};

}

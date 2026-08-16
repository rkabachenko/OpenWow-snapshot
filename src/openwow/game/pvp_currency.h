
#pragma once

#include <cstdint>

namespace openwow::game {

class PvPCurrency {
public:

    void     SetHonorPoints(uint32_t points);
    [[nodiscard]] uint32_t GetHonorPoints() const;

    void     AddHonor(uint32_t amount);
    bool     SpendHonor(uint32_t amount);

    [[nodiscard]] static constexpr uint32_t GetHonorCap() { return kHonorCap; }
    [[nodiscard]] bool IsHonorCapped() const;

    void     SetArenaPoints(uint32_t points);
    [[nodiscard]] uint32_t GetArenaPoints() const;

    void     AddArenaPoints(uint32_t amount);
    bool     SpendArenaPoints(uint32_t amount);

    [[nodiscard]] static constexpr uint32_t GetArenaPointsCap() { return kArenaPointsCap; }
    [[nodiscard]] bool IsArenaPointsCapped() const;

    void     SetWeeklyHonorEarned(uint32_t amount);
    [[nodiscard]] uint32_t GetWeeklyHonorEarned() const;

    void     SetWeeklyArenaPoints(uint32_t amount);
    [[nodiscard]] uint32_t GetWeeklyArenaPoints() const;

    void     ResetWeekly();

    [[nodiscard]] uint32_t GetSessionHonorGain() const;
    void     AddSessionHonor(uint32_t amount);

    void Reset();

private:
    static constexpr uint32_t kHonorCap      = 75000;
    static constexpr uint32_t kArenaPointsCap = 10000;

    uint32_t honorPoints_       = 0;
    uint32_t arenaPoints_       = 0;
    uint32_t weeklyHonor_       = 0;
    uint32_t weeklyArenaPoints_ = 0;
    uint32_t sessionHonor_      = 0;
};

}

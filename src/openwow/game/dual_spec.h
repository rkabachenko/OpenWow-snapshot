
#pragma once

#include <cstdint>
#include <string>

namespace openwow::game {

struct PointsSpent {
    uint32_t tab0 = 0;
    uint32_t tab1 = 0;
    uint32_t tab2 = 0;
};

class DualSpecManager {
 public:
    void SetActiveSpec(uint32_t index);
    [[nodiscard]] uint32_t GetActiveSpec() const;

    [[nodiscard]] uint32_t GetSpecCount() const;
    [[nodiscard]] bool HasDualSpec() const;
    void SetDualSpecUnlocked(bool unlocked);
    [[nodiscard]] bool IsDualSpecUnlocked() const;

    [[nodiscard]] uint32_t GetDualSpecCost() const;

    void SetSpecName(uint32_t specIndex, std::string name);
    [[nodiscard]] std::string GetSpecName(uint32_t specIndex) const;

    void SetPointsSpent(uint32_t specIndex, uint32_t tab0, uint32_t tab1,
                         uint32_t tab2);
    [[nodiscard]] PointsSpent GetPointsSpent(uint32_t specIndex) const;
    [[nodiscard]] uint32_t GetPrimaryTree(uint32_t specIndex) const;
    [[nodiscard]] uint32_t GetTotalPoints(uint32_t specIndex) const;

    [[nodiscard]] bool CanSwapSpec() const;
    void SetCanSwap(bool can_swap);
    [[nodiscard]] float GetSwapCooldown() const;
    void SetSwapCooldown(float seconds);
    void Update(float dt);

    void Reset();

 private:
    static constexpr uint32_t kDualSpecCost = 10000000;

    uint32_t    active_spec_       = 0;
    bool        dual_spec_unlocked_ = false;
    std::string spec_names_[2]     = {"Primary", "Secondary"};
    PointsSpent points_spent_[2]   = {};
    bool        can_swap_          = true;
    float       swap_cooldown_     = 0.0f;
};

}

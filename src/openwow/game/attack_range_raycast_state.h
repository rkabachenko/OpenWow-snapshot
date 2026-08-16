#pragma once

#include <cstdint>
#include <vector>

namespace openwow::game {

struct RaycastFacet {
    float a = 0.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 0.0f;
};

class AttackRangeRaycastState {
public:
    AttackRangeRaycastState() = default;
    ~AttackRangeRaycastState() = default;

    AttackRangeRaycastState(const AttackRangeRaycastState&) = delete;
    AttackRangeRaycastState& operator=(const AttackRangeRaycastState&) = delete;

    void EnsureInitialised();

    void Cleanup();

    void ResetKArrayCount();

    [[nodiscard]] bool IsInitialised() const { return initialised_; }
    [[nodiscard]] std::vector<RaycastFacet>& FacetArray() { return facet_array_; }
    [[nodiscard]] const std::vector<RaycastFacet>& FacetArray() const { return facet_array_; }
    [[nodiscard]] std::vector<float>& KArray() { return k_array_; }
    [[nodiscard]] const std::vector<float>& KArray() const { return k_array_; }

private:
    bool initialised_ = false;
    std::vector<RaycastFacet> facet_array_;
    std::vector<float> k_array_;
};

AttackRangeRaycastState& GetAttackRangeRaycastState();

}

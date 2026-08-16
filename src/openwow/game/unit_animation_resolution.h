#pragma once

#include <cstdint>
#include <functional>

namespace openwow::data::dbc {
struct AnimationDataEntry;
template <typename T> class DbcStore;
}

namespace openwow::game::unit_animation_resolution {

inline constexpr std::uint32_t kMaxAnimationId = 506u;

inline constexpr std::uint32_t kAnimationTierCount = 5u;

inline constexpr std::uint32_t kAnimationTierParent[kAnimationTierCount] = {
    0, 0, 3, 0, 0};

[[nodiscard]] bool AnimationDataDB_FindByBehaviorAndTier(
    const data::dbc::DbcStore<data::dbc::AnimationDataEntry>& animation_db,
    std::uint32_t behavior_id,
    std::uint32_t variation_tier,
    std::uint32_t& out_sequence_id);

[[nodiscard]] bool AnimationDataDB_FindDefaultForVariation(
    const data::dbc::DbcStore<data::dbc::AnimationDataEntry>& animation_db,
    const std::function<bool(std::uint32_t)>& model_supports_animation,
    std::uint32_t variation_tier,
    std::uint32_t& out_sequence_id);

[[nodiscard]] std::uint32_t AnimationData_GetBehaviorId(
    const data::dbc::DbcStore<data::dbc::AnimationDataEntry>& animation_db,
    std::uint32_t animation_id);

}

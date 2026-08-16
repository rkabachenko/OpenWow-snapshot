#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

namespace openwow::game {

class GatherInteraction {
public:
  using Position = std::array<float, 3>;

  using GetGatherSpellIdFn  = std::function<std::uint32_t(std::uint64_t target_guid)>;
  using GetRangeWindowFn    = std::function<bool(std::uint32_t spell_id,
                                                 std::uint64_t target_guid,
                                                 float* out_min_range,
                                                 float* out_max_range)>;
  using GetPositionFn       = std::function<Position(std::uint64_t guid)>;
  using InitiateActionFn    = std::function<bool(std::uint64_t target_guid,
                                                 std::uint32_t action_type)>;
  using CastGatherSpellFn   = std::function<void(std::uint32_t spell_id,
                                                  std::uint64_t target_guid)>;
  using IsAutoLootToggledFn = std::function<bool()>;
  using SetPendingAutoLootFn = std::function<void(bool enabled)>;

  struct Delegates {
    GetGatherSpellIdFn   get_gather_spell_id;
    GetRangeWindowFn     get_range_window;
    GetPositionFn        get_player_position;
    GetPositionFn        get_target_position;
    InitiateActionFn     initiate_action;
    CastGatherSpellFn    cast_gather_spell;
    IsAutoLootToggledFn  is_auto_loot_toggled;
    SetPendingAutoLootFn set_pending_auto_loot;
  };

  explicit GatherInteraction(Delegates delegates)
      : delegates_(std::move(delegates)) {}

  void TryGatherOrApproach(std::uint64_t player_guid,
                           std::uint64_t target_guid,
                           bool approach_if_out_of_range,
                           bool set_auto_loot);

private:
  Delegates delegates_;
};

inline constexpr std::uint32_t kActionTypeObjectInteract = 9;

[[nodiscard]] inline bool IsOutOfGatherRange(float dist_sq,
                                              float max_range) {
  return max_range * max_range <= dist_sq;
}

}

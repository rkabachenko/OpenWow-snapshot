#pragma once

#include <cstdint>
#include <vector>

namespace openwow::math {
struct C3Vector;
}

namespace openwow::game {

struct DelayedSpellVisualKit {

  static constexpr std::uint32_t kFlagHasPosition = 0x01u;
  static constexpr std::uint32_t kFlagParamA      = 0x02u;
  static constexpr std::uint32_t kFlagParamB      = 0x04u;
  static constexpr std::uint32_t kFlagActive       = 0x08u;

  std::uint32_t spell_id{0};
  std::uint32_t spell_visual_rec_ptr{0};
  std::uint32_t effect_index{0};
  float         position_x{0.0f};
  float         position_y{0.0f};
  float         position_z{0.0f};
  std::uint32_t position_param_a{0};
  std::uint32_t position_param_b{0};
  std::uint32_t timestamp{0};
  std::uint32_t flags{0};
  std::uint32_t visual_type{0};
  std::uint32_t expire_time{0};

  [[nodiscard]] bool IsActive() const {
    return (flags & kFlagActive) != 0;
  }
};

static_assert(sizeof(DelayedSpellVisualKit) == 48,
              "DelayedSpellVisualKit must be 48 bytes to match binary layout");

struct DelayedVisualKitPosition {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

DelayedSpellVisualKit& AddDelayedSpellVisualKit(
    std::vector<DelayedSpellVisualKit>& kits,
    std::uint32_t spell_id,
    std::uint32_t visual_rec,
    std::uint32_t effect_index,
    const DelayedVisualKitPosition* position,
    std::uint32_t pos_param_a,
    std::uint32_t pos_param_b,
    bool flag_a,
    bool flag_b,
    std::uint32_t timestamp,
    std::uint32_t visual_type,
    std::uint32_t expire_time);

}

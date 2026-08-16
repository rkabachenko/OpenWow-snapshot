#pragma once

#include <cstdint>

namespace openwow::game {

namespace UsingAnimId {
inline constexpr std::uint16_t kStand                = 0x00;
inline constexpr std::uint16_t kReadySpellDirected   = 0x9E;
inline constexpr std::uint16_t kReadySpellOmni       = 0x9F;
}

enum class UsingAnimPhase : std::uint8_t {
  kIdle,
  kInitialAnimation,
  kReadySpellOmni,
  kDone,
};

class UsingAnimationSequencer {
 public:

  enum class Action : std::uint8_t {
    kNone,
    kPlayAnimation,
    kDestroyEffectNode,
  };

  UsingAnimationSequencer() = default;

  void Start(std::uint16_t initial_anim,
             std::uint32_t threshold_tick,
             bool model_has_omni);

  Action Advance(std::uint32_t current_tick, bool anim_completed);

  [[nodiscard]] std::uint16_t next_anim_id() const { return next_anim_id_; }

  [[nodiscard]] UsingAnimPhase phase() const { return phase_; }

  [[nodiscard]] bool IsActive() const {
    return phase_ != UsingAnimPhase::kIdle &&
           phase_ != UsingAnimPhase::kDone;
  }

 private:
  UsingAnimPhase phase_{UsingAnimPhase::kIdle};
  std::uint16_t initial_anim_id_{0};
  std::uint16_t next_anim_id_{0};
  std::uint32_t threshold_tick_{0};
  bool model_has_omni_{false};
};

}

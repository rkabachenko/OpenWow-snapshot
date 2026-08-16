#pragma once

#include <cstdint>

namespace openwow::render::api {

enum class FinalCompositorStage : std::uint8_t {
  kPostProcess = 1u << 0u,
  kWorldUi = 1u << 1u,
  kDebugOverlay = 1u << 2u,
  kLoadingScreen = 1u << 3u,
};

using FinalCompositorStageFlags = std::uint8_t;

[[nodiscard]] inline constexpr FinalCompositorStageFlags FinalCompositorStageBit(
    const FinalCompositorStage stage) noexcept {
  return static_cast<FinalCompositorStageFlags>(stage);
}

inline constexpr FinalCompositorStageFlags kFinalWorldCompositorStages =
    FinalCompositorStageBit(FinalCompositorStage::kPostProcess) |
    FinalCompositorStageBit(FinalCompositorStage::kWorldUi) |
    FinalCompositorStageBit(FinalCompositorStage::kDebugOverlay);

inline constexpr FinalCompositorStageFlags kFinalLoadingCompositorStages =
    FinalCompositorStageBit(FinalCompositorStage::kLoadingScreen);

enum class FinalCompositorTarget : std::uint8_t {
  kNone,
  kLdrBackbuffer,
};

struct FinalCompositorState {
  std::uint64_t active_generation{0};
  std::uint64_t completed_generation{0};
  FinalCompositorStageFlags submitted_stages{0};
  FinalCompositorTarget target{FinalCompositorTarget::kNone};

  [[nodiscard]] bool IsCurrentFrameComplete() const noexcept {
    return active_generation != 0u && completed_generation == active_generation &&
           target == FinalCompositorTarget::kLdrBackbuffer;
  }

  [[nodiscard]] bool IsCurrentWorldFrameComplete() const noexcept {
    return IsCurrentFrameComplete() &&
           (submitted_stages & kFinalWorldCompositorStages) ==
               kFinalWorldCompositorStages;
  }

  [[nodiscard]] bool IsCurrentLoadingFrameComplete() const noexcept {
    return IsCurrentFrameComplete() &&
           (submitted_stages & kFinalLoadingCompositorStages) ==
               kFinalLoadingCompositorStages;
  }
};

class FinalCompositorTracker {
 public:
  void BeginFrame() noexcept {
    ++state_.active_generation;
    if (state_.active_generation == 0u) {
      ++state_.active_generation;
    }
    state_.submitted_stages = 0u;
    state_.target = FinalCompositorTarget::kNone;
  }

  void MarkStage(const FinalCompositorStage stage) noexcept {
    if (state_.active_generation == 0u || state_.IsCurrentFrameComplete()) {
      return;
    }
    state_.submitted_stages |= FinalCompositorStageBit(stage);
  }

  void Complete(const FinalCompositorTarget target) noexcept {
    if (state_.active_generation == 0u || target == FinalCompositorTarget::kNone) {
      return;
    }
    state_.target = target;
    state_.completed_generation = state_.active_generation;
  }

  void Reset() noexcept { state_ = {}; }

  [[nodiscard]] const FinalCompositorState& state() const noexcept { return state_; }

 private:
  FinalCompositorState state_{};
};

}

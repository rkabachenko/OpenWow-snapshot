
#pragma once

#include "openwow/ui/frame_script_type_info.h"

#include <cstdint>

namespace openwow::ui::widgets {

class CSimpleAnim {
 public:
  CSimpleAnim() = default;
  ~CSimpleAnim() = default;

  [[nodiscard]] void* GetAnimationGroup() const noexcept {
    return animationGroup_;
  }
  void SetAnimationGroup(void* group) noexcept { animationGroup_ = group; }

  void SetAnimType(uint32_t type) noexcept { animType_ = type; }
  [[nodiscard]] uint32_t GetAnimType() const noexcept { return animType_; }

  void SetDuration(float duration) noexcept { duration_ = duration; }
  [[nodiscard]] float GetDuration() const noexcept { return duration_; }

  void SetElapsed(float elapsed) noexcept { elapsed_ = elapsed; }
  [[nodiscard]] float GetElapsed() const noexcept { return elapsed_; }

  [[nodiscard]] float GetProgress() const noexcept {
    if (duration_ <= 0.0f) return 1.0f;
    float p = elapsed_ / duration_;
    return p > 1.0f ? 1.0f : (p < 0.0f ? 0.0f : p);
  }

  void SetLooping(bool loop) noexcept { looping_ = loop; }
  [[nodiscard]] bool IsLooping() const noexcept { return looping_; }

  void SetSmoothingType(uint32_t type) noexcept { smoothingType_ = type; }
  [[nodiscard]] uint32_t GetSmoothingType() const noexcept {
    return smoothingType_;
  }

  void SetOrder(uint32_t order) noexcept { order_ = order; }
  [[nodiscard]] uint32_t GetOrder() const noexcept { return order_; }

  enum class State : uint8_t { Stopped, Playing, Paused };

  void SetState(State s) noexcept { state_ = s; }
  [[nodiscard]] State GetState() const noexcept { return state_; }
  [[nodiscard]] bool IsPlaying() const noexcept {
    return state_ == State::Playing;
  }

  void Play() noexcept { state_ = State::Playing; }
  void Pause() noexcept { state_ = State::Paused; }
  void Stop() noexcept {
    state_ = State::Stopped;
    elapsed_ = 0.0f;
  }

  struct ScriptSlotInfo {
    int offset;
    const char* format;
  };

  [[nodiscard]] ScriptSlotInfo GetScriptSlot(const char* handlerName) const {
    if (handlerName == nullptr) {
      return {0, nullptr};
    }
    const auto* slot = openwow::ui::LookupUiScriptHandlerVariant(
        openwow::ui::UiScriptHandlerOwner::Animation, handlerName);
    if (slot == nullptr) {
      return {0, nullptr};
    }
    const char* format_override =
        slot->wrapper_format == openwow::ui::kDefaultFrameScriptWrapperFormat
            ? nullptr
            : slot->wrapper_format;
    return {static_cast<int>(slot->retail_slot_offset), format_override};
  }

  void SetSmoothProgress(float progress) noexcept {
    smoothProgress_ = progress;
  }
  [[nodiscard]] float GetSmoothProgress() const noexcept {
    return smoothProgress_;
  }

 private:
  uint32_t animType_{0};
  State state_{State::Stopped};
  float startTime_{0.0f};
  float duration_{0.0f};
  float elapsed_{0.0f};
  bool looping_{false};
  uint32_t smoothingType_{0};
  uint32_t order_{0};
  void* animationGroup_{nullptr};
  float smoothProgress_{0.0f};
};

}

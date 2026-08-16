#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::render {

enum class AnimationType : std::uint8_t {
  Alpha = 0,
  Translation = 1,
  Scale = 2,
  Rotation = 3,
  Path = 4,
  TextureCoords = 5,
};

enum class EasingType : std::uint8_t {
  None = 0,
  Linear = 1,
  InQuad = 2,
  OutQuad = 3,
  InOutQuad = 4,
  InCubic = 5,
  OutCubic = 6,
  InOutCubic = 7,
  InSine = 8,
  OutSine = 9,
};

enum class LoopType : std::uint8_t {
  None = 0,
  Repeat = 1,
  Bounce = 2,
};

struct AnimationKeyframe {
  float time{0.0f};
  float value{0.0f};
  float valueX{0.0f};
  float valueY{0.0f};
  EasingType easing{EasingType::Linear};
};

struct UIAnimation {
  std::uint32_t id{0};
  AnimationType type{AnimationType::Alpha};
  float duration{1.0f};
  float delay{0.0f};
  std::int32_t order{0};
  EasingType smoothing{EasingType::Linear};
  LoopType looping{LoopType::None};

  bool playing{false};
  bool paused{false};
  float progress{0.0f};
  float elapsed{0.0f};

  std::vector<AnimationKeyframe> keyframes;

  float fromValue{0.0f};
  float toValue{1.0f};
  float fromX{0.0f};
  float fromY{0.0f};
  float toX{0.0f};
  float toY{0.0f};
};

struct AnimationGroup {
  std::uint32_t id{0};
  std::uint32_t parentFrameId{0};
  std::vector<std::uint32_t> animationIds;
  LoopType looping{LoopType::None};

  bool playing{false};
  bool paused{false};
  std::int32_t currentOrder{0};
  std::uint32_t loopCount{0};

  std::unordered_map<std::string, std::function<void()>> scripts;
};

class UIAnimationSystem {
 public:
  UIAnimationSystem() = default;
  ~UIAnimationSystem() = default;

  UIAnimationSystem(const UIAnimationSystem&) = delete;
  UIAnimationSystem& operator=(const UIAnimationSystem&) = delete;

  std::uint32_t CreateGroup(std::uint32_t parentFrameId);

  std::uint32_t AddAnimation(std::uint32_t groupId, const UIAnimation& anim);

  void DestroyGroup(std::uint32_t groupId);

  void Play(std::uint32_t groupId);
  void Stop(std::uint32_t groupId);
  void Pause(std::uint32_t groupId);

  [[nodiscard]] bool IsPlaying(std::uint32_t groupId) const;

  void Update(float dt);

  [[nodiscard]] float GetAnimationValue(std::uint32_t animId) const;

  void SetScript(std::uint32_t groupId, const std::string& event,
                 std::function<void()> callback);

  [[nodiscard]] std::uint32_t GetActiveAnimationCount() const;

  void Reset();

  static float Ease(EasingType type, float t);

 private:

  void UpdateAnimation(UIAnimation& anim, float dt);

  [[nodiscard]] bool IsOrderTierComplete(const AnimationGroup& group) const;

  [[nodiscard]] std::int32_t GetMaxOrder(const AnimationGroup& group) const;

  void FireScript(AnimationGroup& group, const std::string& event);

  std::unordered_map<std::uint32_t, AnimationGroup> groups_;
  std::unordered_map<std::uint32_t, UIAnimation> animations_;

  std::uint32_t nextGroupId_{1};
  std::uint32_t nextAnimId_{1};
};

}


#include "openwow/render/ui/ui_animation.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openwow::render {

float UIAnimationSystem::Ease(EasingType type, float t) {
  t = std::clamp(t, 0.0f, 1.0f);

  switch (type) {
    case EasingType::None:
      return t < 1.0f ? 0.0f : 1.0f;

    case EasingType::Linear:
      return t;

    case EasingType::InQuad:
      return t * t;

    case EasingType::OutQuad:
      return t * (2.0f - t);

    case EasingType::InOutQuad:
      return (t < 0.5f) ? (2.0f * t * t)
                         : (-1.0f + (4.0f - 2.0f * t) * t);

    case EasingType::InCubic:
      return t * t * t;

    case EasingType::OutCubic: {
      const float f = t - 1.0f;
      return f * f * f + 1.0f;
    }

    case EasingType::InOutCubic:
      return (t < 0.5f) ? (4.0f * t * t * t)
                         : ((t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f);

    case EasingType::InSine:
      return 1.0f - std::cos(t * static_cast<float>(M_PI) * 0.5f);

    case EasingType::OutSine:
      return std::sin(t * static_cast<float>(M_PI) * 0.5f);

    default:
      return t;
  }
}

std::uint32_t UIAnimationSystem::CreateGroup(std::uint32_t parentFrameId) {
  const std::uint32_t id = nextGroupId_++;
  AnimationGroup group;
  group.id = id;
  group.parentFrameId = parentFrameId;
  groups_[id] = std::move(group);
  return id;
}

std::uint32_t UIAnimationSystem::AddAnimation(std::uint32_t groupId,
                                              const UIAnimation& anim) {
  auto it = groups_.find(groupId);
  if (it == groups_.end()) return 0;

  const std::uint32_t id = nextAnimId_++;
  UIAnimation newAnim = anim;
  newAnim.id = id;
  animations_[id] = std::move(newAnim);
  it->second.animationIds.push_back(id);
  return id;
}

void UIAnimationSystem::DestroyGroup(std::uint32_t groupId) {
  auto it = groups_.find(groupId);
  if (it == groups_.end()) return;

  for (auto animId : it->second.animationIds) {
    animations_.erase(animId);
  }
  groups_.erase(it);
}

void UIAnimationSystem::Play(std::uint32_t groupId) {
  auto it = groups_.find(groupId);
  if (it == groups_.end()) return;

  auto& group = it->second;
  group.playing = true;
  group.paused = false;
  group.currentOrder = 0;
  group.loopCount = 0;

  for (auto animId : group.animationIds) {
    auto ait = animations_.find(animId);
    if (ait != animations_.end()) {
      ait->second.progress = 0.0f;
      ait->second.elapsed = 0.0f;
      ait->second.playing = false;
      ait->second.paused = false;
    }
  }

  for (auto animId : group.animationIds) {
    auto ait = animations_.find(animId);
    if (ait != animations_.end() && ait->second.order == 0) {
      ait->second.playing = true;
    }
  }

  FireScript(group, "OnPlay");
}

void UIAnimationSystem::Stop(std::uint32_t groupId) {
  auto it = groups_.find(groupId);
  if (it == groups_.end()) return;

  auto& group = it->second;
  group.playing = false;
  group.paused = false;

  for (auto animId : group.animationIds) {
    auto ait = animations_.find(animId);
    if (ait != animations_.end()) {
      ait->second.playing = false;
      ait->second.paused = false;
    }
  }
}

void UIAnimationSystem::Pause(std::uint32_t groupId) {
  auto it = groups_.find(groupId);
  if (it == groups_.end()) return;

  auto& group = it->second;
  if (!group.playing) return;

  group.paused = !group.paused;

  for (auto animId : group.animationIds) {
    auto ait = animations_.find(animId);
    if (ait != animations_.end() && ait->second.playing) {
      ait->second.paused = group.paused;
    }
  }
}

bool UIAnimationSystem::IsPlaying(std::uint32_t groupId) const {
  auto it = groups_.find(groupId);
  if (it == groups_.end()) return false;
  return it->second.playing && !it->second.paused;
}

void UIAnimationSystem::Update(float dt) {
  for (auto& [gid, group] : groups_) {
    if (!group.playing || group.paused) continue;

    for (auto animId : group.animationIds) {
      auto ait = animations_.find(animId);
      if (ait == animations_.end()) continue;
      if (!ait->second.playing || ait->second.paused) continue;
      UpdateAnimation(ait->second, dt);
    }

    if (IsOrderTierComplete(group)) {
      const std::int32_t maxOrder = GetMaxOrder(group);
      if (group.currentOrder < maxOrder) {

        group.currentOrder++;
        for (auto animId : group.animationIds) {
          auto ait = animations_.find(animId);
          if (ait != animations_.end() &&
              ait->second.order == group.currentOrder) {
            ait->second.playing = true;
            ait->second.elapsed = 0.0f;
            ait->second.progress = 0.0f;
          }
        }
      } else {

        if (group.looping == LoopType::Repeat) {
          group.loopCount++;
          FireScript(group, "OnLoop");

          group.currentOrder = 0;
          for (auto animId : group.animationIds) {
            auto ait = animations_.find(animId);
            if (ait != animations_.end()) {
              ait->second.progress = 0.0f;
              ait->second.elapsed = 0.0f;
              ait->second.playing = (ait->second.order == 0);
            }
          }
        } else if (group.looping == LoopType::Bounce) {
          group.loopCount++;
          FireScript(group, "OnLoop");

          for (auto animId : group.animationIds) {
            auto ait = animations_.find(animId);
            if (ait != animations_.end()) {
              std::swap(ait->second.fromValue, ait->second.toValue);
              std::swap(ait->second.fromX, ait->second.toX);
              std::swap(ait->second.fromY, ait->second.toY);
              ait->second.progress = 0.0f;
              ait->second.elapsed = 0.0f;
              ait->second.playing = (ait->second.order == 0);
            }
          }
          group.currentOrder = 0;
        } else {

          group.playing = false;
          FireScript(group, "OnFinished");
        }
      }
    }
  }
}

void UIAnimationSystem::UpdateAnimation(UIAnimation& anim, float dt) {
  if (!anim.playing || anim.paused) return;

  anim.elapsed += dt;

  if (anim.elapsed < anim.delay) return;

  const float activeTime = anim.elapsed - anim.delay;
  if (anim.duration <= 0.0f) {
    anim.progress = 1.0f;
    anim.playing = false;
    return;
  }

  anim.progress = std::clamp(activeTime / anim.duration, 0.0f, 1.0f);

  if (anim.progress >= 1.0f) {
    anim.progress = 1.0f;
    anim.playing = false;
  }
}

float UIAnimationSystem::GetAnimationValue(std::uint32_t animId) const {
  auto it = animations_.find(animId);
  if (it == animations_.end()) return 0.0f;

  const auto& anim = it->second;
  const float easedT = Ease(anim.smoothing, anim.progress);

  if (!anim.keyframes.empty()) {

    if (anim.keyframes.size() == 1) return anim.keyframes[0].value;

    for (std::size_t i = 0; i + 1 < anim.keyframes.size(); ++i) {
      const auto& k0 = anim.keyframes[i];
      const auto& k1 = anim.keyframes[i + 1];
      if (easedT >= k0.time && easedT <= k1.time) {
        const float segT = (k1.time - k0.time > 0.0f)
                               ? (easedT - k0.time) / (k1.time - k0.time)
                               : 1.0f;
        const float segEased = Ease(k0.easing, segT);
        return k0.value + (k1.value - k0.value) * segEased;
      }
    }
    return anim.keyframes.back().value;
  }

  return anim.fromValue + (anim.toValue - anim.fromValue) * easedT;
}

void UIAnimationSystem::SetScript(std::uint32_t groupId,
                                  const std::string& event,
                                  std::function<void()> callback) {
  auto it = groups_.find(groupId);
  if (it == groups_.end()) return;
  it->second.scripts[event] = std::move(callback);
}

std::uint32_t UIAnimationSystem::GetActiveAnimationCount() const {
  std::uint32_t count = 0;
  for (const auto& [id, anim] : animations_) {
    if (anim.playing && !anim.paused) ++count;
  }
  return count;
}

void UIAnimationSystem::Reset() {
  groups_.clear();
  animations_.clear();
  nextGroupId_ = 1;
  nextAnimId_ = 1;
}

bool UIAnimationSystem::IsOrderTierComplete(
    const AnimationGroup& group) const {
  for (auto animId : group.animationIds) {
    auto it = animations_.find(animId);
    if (it == animations_.end()) continue;
    if (it->second.order == group.currentOrder && it->second.playing) {
      return false;
    }
  }
  return true;
}

std::int32_t UIAnimationSystem::GetMaxOrder(
    const AnimationGroup& group) const {
  std::int32_t maxOrd = 0;
  for (auto animId : group.animationIds) {
    auto it = animations_.find(animId);
    if (it != animations_.end()) {
      maxOrd = std::max(maxOrd, it->second.order);
    }
  }
  return maxOrd;
}

void UIAnimationSystem::FireScript(AnimationGroup& group,
                                   const std::string& event) {
  auto it = group.scripts.find(event);
  if (it != group.scripts.end() && it->second) {
    it->second();
  }
}

}

#include "openwow/game/emote_visual.h"

#include <algorithm>

namespace openwow::game {

std::uint32_t EmoteVisualSystem::PlayEmote(ObjectGuid unitGuid,
                                            std::uint32_t emoteId,
                                            EmoteVisualType type,
                                            float duration) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::uint32_t id = nextId_++;

  EmoteVisual vis;
  vis.id = id;
  vis.unitGuid = unitGuid;
  vis.emoteId = emoteId;
  vis.visualType = type;
  vis.startTime = elapsed_;
  vis.duration = duration;
  vis.looping = (type == EmoteVisualType::Loop);

  auto animIt = emoteToAnim_.find(emoteId);
  vis.animationId = (animIt != emoteToAnim_.end()) ? animIt->second : emoteId;

  visuals_[id] = std::move(vis);
  return id;
}

void EmoteVisualSystem::StopEmote(std::uint32_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  visuals_.erase(id);
}

void EmoteVisualSystem::StopAllEmotes(ObjectGuid unitGuid) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = visuals_.begin(); it != visuals_.end();) {
    if (it->second.unitGuid.GetRawValue() == unitGuid.GetRawValue())
      it = visuals_.erase(it);
    else
      ++it;
  }
}

std::optional<EmoteVisual> EmoteVisualSystem::GetActiveEmote(
    ObjectGuid unitGuid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const EmoteVisual* latest = nullptr;
  for (const auto& [_, v] : visuals_) {
    if (v.unitGuid.GetRawValue() == unitGuid.GetRawValue()) {
      if (!latest || v.startTime > latest->startTime) latest = &v;
    }
  }
  if (latest) return *latest;
  return std::nullopt;
}

std::vector<EmoteVisual> EmoteVisualSystem::GetAllActiveEmotes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<EmoteVisual> result;
  result.reserve(visuals_.size());
  for (const auto& [_, v] : visuals_) result.push_back(v);
  return result;
}

std::uint32_t EmoteVisualSystem::GetActiveCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<std::uint32_t>(visuals_.size());
}

bool EmoteVisualSystem::IsPlayingEmote(ObjectGuid unitGuid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& [_, v] : visuals_) {
    if (v.unitGuid.GetRawValue() == unitGuid.GetRawValue()) return true;
  }
  return false;
}

std::uint32_t EmoteVisualSystem::GetEmoteAnimation(
    std::uint32_t emoteId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = emoteToAnim_.find(emoteId);
  if (it != emoteToAnim_.end()) return it->second;
  return emoteId;
}

void EmoteVisualSystem::SetEmoteAnimation(std::uint32_t emoteId,
                                           std::uint32_t animId) {
  std::lock_guard<std::mutex> lock(mutex_);
  emoteToAnim_[emoteId] = animId;
}

void EmoteVisualSystem::Update(float dt) {
  std::lock_guard<std::mutex> lock(mutex_);
  elapsed_ += dt;

  for (auto it = visuals_.begin(); it != visuals_.end();) {
    auto& v = it->second;

    if (v.visualType == EmoteVisualType::OneShot && v.duration > 0.0f) {
      float age = elapsed_ - v.startTime;
      if (age >= v.duration) {
        it = visuals_.erase(it);
        continue;
      }
    }

    ++it;
  }
}

void EmoteVisualSystem::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  visuals_.clear();
}

void EmoteVisualSystem::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  visuals_.clear();
  emoteToAnim_.clear();
  nextId_ = 1;
  elapsed_ = 0.0f;
}

}


#include "openwow/game/rune_system.h"

#include <algorithm>

namespace openwow::game {

RuneSystem::RuneSystem() { InitializeDefaultLayout(); }

void RuneSystem::SetRune(std::uint32_t slotIndex, RuneType baseType) {
  if (slotIndex >= static_cast<std::uint32_t>(kMaxRunes)) return;
  auto& r = runes_[slotIndex];
  r.baseType = baseType;
  r.type = baseType;
  r.isReady = true;
  r.cooldownRemaining = 0.0f;
  r.cooldownTotal = 0.0f;
}

std::optional<RuneSlotInfo> RuneSystem::GetRune(
    std::uint32_t slotIndex) const {
  if (slotIndex >= static_cast<std::uint32_t>(kMaxRunes)) return std::nullopt;
  return runes_[slotIndex];
}

std::vector<RuneSlotInfo> RuneSystem::GetAllRunes() const {
  return {runes_.begin(), runes_.end()};
}

void RuneSystem::SetRuneReady(std::uint32_t slotIndex, bool ready) {
  if (slotIndex >= static_cast<std::uint32_t>(kMaxRunes)) return;
  runes_[slotIndex].isReady = ready;
  if (ready) {
    runes_[slotIndex].cooldownRemaining = 0.0f;
  }
}

bool RuneSystem::IsRuneReady(std::uint32_t slotIndex) const {
  if (slotIndex >= static_cast<std::uint32_t>(kMaxRunes)) return false;
  return runes_[slotIndex].isReady;
}

void RuneSystem::StartCooldown(std::uint32_t slotIndex, float duration) {
  if (slotIndex >= static_cast<std::uint32_t>(kMaxRunes)) return;
  auto& r = runes_[slotIndex];
  r.isReady = false;
  r.cooldownTotal = duration;
  r.cooldownRemaining = duration;
}

float RuneSystem::GetCooldownRemaining(std::uint32_t slotIndex) const {
  if (slotIndex >= static_cast<std::uint32_t>(kMaxRunes)) return 0.0f;
  return runes_[slotIndex].cooldownRemaining;
}

float RuneSystem::GetCooldownProgress(std::uint32_t slotIndex) const {
  if (slotIndex >= static_cast<std::uint32_t>(kMaxRunes)) return 0.0f;
  const auto& r = runes_[slotIndex];
  if (r.cooldownTotal <= 0.0f) return r.isReady ? 1.0f : 0.0f;
  float elapsed = r.cooldownTotal - r.cooldownRemaining;
  return std::clamp(elapsed / r.cooldownTotal, 0.0f, 1.0f);
}

void RuneSystem::ConvertToDeath(std::uint32_t slotIndex) {
  if (slotIndex >= static_cast<std::uint32_t>(kMaxRunes)) return;
  runes_[slotIndex].type = RuneType::kDeath;
}

void RuneSystem::RevertFromDeath(std::uint32_t slotIndex) {
  if (slotIndex >= static_cast<std::uint32_t>(kMaxRunes)) return;
  runes_[slotIndex].type = runes_[slotIndex].baseType;
}

std::uint32_t RuneSystem::GetReadyRuneCount(RuneType type) const {
  std::uint32_t count = 0;
  for (const auto& r : runes_)
    if (r.type == type && r.isReady) ++count;
  return count;
}

std::uint32_t RuneSystem::GetTotalRuneCount(RuneType type) const {
  std::uint32_t count = 0;
  for (const auto& r : runes_)
    if (r.type == type) ++count;
  return count;
}

void RuneSystem::Update(float dt) {
  for (auto& r : runes_) {
    if (!r.isReady && r.cooldownRemaining > 0.0f) {
      r.cooldownRemaining -= dt;
      if (r.cooldownRemaining <= 0.0f) {
        r.cooldownRemaining = 0.0f;
        r.isReady = true;
      }
    }
  }
}

std::uint32_t RuneSystem::GetRuneColor(RuneType type) {
  switch (type) {
    case RuneType::kBlood:  return 0xFFFF0000;
    case RuneType::kUnholy: return 0xFF00FF00;
    case RuneType::kFrost:  return 0xFF00BFFF;
    case RuneType::kDeath:  return 0xFF8040FF;
  }
  return 0xFFFFFFFF;
}

std::string RuneSystem::GetRuneTypeName(RuneType type) {
  switch (type) {
    case RuneType::kBlood:  return "Blood";
    case RuneType::kUnholy: return "Unholy";
    case RuneType::kFrost:  return "Frost";
    case RuneType::kDeath:  return "Death";
  }
  return "Unknown";
}

void RuneSystem::InitializeDefaultLayout() {
  constexpr RuneType kLayout[kMaxRunes] = {
      RuneType::kBlood, RuneType::kBlood,
      RuneType::kUnholy, RuneType::kUnholy,
      RuneType::kFrost, RuneType::kFrost,
  };
  for (int i = 0; i < kMaxRunes; ++i) {
    runes_[i].type = kLayout[i];
    runes_[i].baseType = kLayout[i];
    runes_[i].isReady = true;
    runes_[i].cooldownRemaining = 0.0f;
    runes_[i].cooldownTotal = 0.0f;
  }
}

void RuneSystem::Reset() { InitializeDefaultLayout(); }

}

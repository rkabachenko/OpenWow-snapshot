
#include "openwow/game/boss_warning.h"

#include <algorithm>

namespace openwow::game {

namespace {

constexpr std::uint32_t kMaxWarnings = 20;

}

void BossWarningSystem::AddWarning(BossWarningEntry entry) {
  if (!enabled_) return;

  if (entry.remaining <= 0.0f) {
    entry.remaining = entry.duration;
  }

  warnings_.push_back(std::move(entry));

  while (warnings_.size() > kMaxWarnings) {
    warnings_.erase(warnings_.begin());
  }
}

std::vector<BossWarningEntry> BossWarningSystem::GetActiveWarnings() const {
  return warnings_;
}

std::optional<BossWarningEntry> BossWarningSystem::GetWarning(
    std::uint32_t index) const {
  if (index >= warnings_.size()) return std::nullopt;
  return warnings_[index];
}

bool BossWarningSystem::HasActiveWarning() const {
  return !warnings_.empty();
}

std::uint32_t BossWarningSystem::GetActiveCount() const {
  return static_cast<std::uint32_t>(warnings_.size());
}

void BossWarningSystem::ClearWarning(std::uint32_t index) {
  if (index < warnings_.size()) {
    warnings_.erase(warnings_.begin() +
                    static_cast<std::ptrdiff_t>(index));
  }
}

void BossWarningSystem::ClearAll() { warnings_.clear(); }

std::vector<BossWarningEntry> BossWarningSystem::GetWarningsByType(
    BossWarningType type) const {
  std::vector<BossWarningEntry> result;
  for (const auto& w : warnings_) {
    if (w.warningType == type) {
      result.push_back(w);
    }
  }
  return result;
}

std::vector<BossWarningEntry> BossWarningSystem::GetTimerWarnings() const {
  std::vector<BossWarningEntry> result;
  for (const auto& w : warnings_) {
    if (w.hasTimer) {
      result.push_back(w);
    }
  }
  return result;
}

void BossWarningSystem::Update(float dt) {
  if (!enabled_) return;

  for (auto& w : warnings_) {
    w.remaining -= dt;
  }

  std::erase_if(warnings_,
                [](const BossWarningEntry& w) { return w.remaining <= 0.0f; });
}

void BossWarningSystem::SetEnabled(bool enabled) { enabled_ = enabled; }

bool BossWarningSystem::IsEnabled() const { return enabled_; }

std::string BossWarningSystem::GetTypeName(BossWarningType type) {
  switch (type) {
    case BossWarningType::Emote: return "Emote";
    case BossWarningType::Yell:  return "Yell";
    case BossWarningType::Phase: return "Phase";
    case BossWarningType::Timer: return "Timer";
    case BossWarningType::Spell: return "Spell";
  }
  return "Unknown";
}

void BossWarningSystem::Reset() {
  warnings_.clear();
  enabled_ = true;
}

}

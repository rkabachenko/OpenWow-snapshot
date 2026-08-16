
#include "openwow/game/absorb_tracker.h"

#include <algorithm>
#include <numeric>

namespace openwow::game {

void AbsorbTracker::AddShield(ObjectGuid unitGuid, AbsorbShieldEntry shield) {
  shields_[unitGuid.GetRawValue()].push_back(std::move(shield));
}

void AbsorbTracker::RemoveShield(ObjectGuid unitGuid, std::uint32_t auraId) {
  auto it = shields_.find(unitGuid.GetRawValue());
  if (it == shields_.end()) return;
  auto& vec = it->second;
  vec.erase(std::remove_if(vec.begin(), vec.end(),
                            [auraId](const AbsorbShieldEntry& e) {
                              return e.auraId == auraId;
                            }),
            vec.end());
  if (vec.empty()) shields_.erase(it);
}

std::vector<AbsorbShieldEntry> AbsorbTracker::GetShields(
    ObjectGuid unitGuid) const {
  auto it = shields_.find(unitGuid.GetRawValue());
  if (it == shields_.end()) return {};
  return it->second;
}

std::uint32_t AbsorbTracker::GetTotalAbsorb(ObjectGuid unitGuid) const {
  auto it = shields_.find(unitGuid.GetRawValue());
  if (it == shields_.end()) return 0;
  return std::accumulate(
      it->second.begin(), it->second.end(), std::uint32_t{0},
      [](std::uint32_t sum, const AbsorbShieldEntry& e) {
        return sum + e.currentAmount;
      });
}

std::uint32_t AbsorbTracker::GetAbsorbForSchool(
    ObjectGuid unitGuid, std::uint32_t schoolMask) const {
  auto it = shields_.find(unitGuid.GetRawValue());
  if (it == shields_.end()) return 0;
  std::uint32_t total = 0;
  for (const auto& e : it->second) {
    if (e.schoolMask == 0 || (e.schoolMask & schoolMask) != 0)
      total += e.currentAmount;
  }
  return total;
}

std::uint32_t AbsorbTracker::ConsumeAbsorb(ObjectGuid unitGuid,
                                            std::uint32_t amount,
                                            std::uint32_t schoolMask) {
  auto it = shields_.find(unitGuid.GetRawValue());
  if (it == shields_.end()) return 0;
  auto& vec = it->second;

  std::sort(vec.begin(), vec.end(),
            [](const AbsorbShieldEntry& a, const AbsorbShieldEntry& b) {
              return a.priority > b.priority;
            });

  std::uint32_t absorbed = 0;
  for (auto sit = vec.begin(); sit != vec.end() && amount > 0;) {

    if (sit->schoolMask != 0 && (sit->schoolMask & schoolMask) == 0) {
      ++sit;
      continue;
    }
    std::uint32_t take = std::min(sit->currentAmount, amount);
    sit->currentAmount -= take;
    absorbed += take;
    amount -= take;
    if (sit->currentAmount == 0) {
      sit = vec.erase(sit);
    } else {
      ++sit;
    }
  }
  if (vec.empty()) shields_.erase(it);
  return absorbed;
}

void AbsorbTracker::UpdateShieldAmount(ObjectGuid unitGuid,
                                        std::uint32_t auraId,
                                        std::uint32_t newAmount) {
  auto it = shields_.find(unitGuid.GetRawValue());
  if (it == shields_.end()) return;
  for (auto& e : it->second) {
    if (e.auraId == auraId) {
      e.currentAmount = newAmount;
      return;
    }
  }
}

bool AbsorbTracker::HasShield(ObjectGuid unitGuid) const {
  auto it = shields_.find(unitGuid.GetRawValue());
  return it != shields_.end() && !it->second.empty();
}

std::uint32_t AbsorbTracker::GetShieldCount(ObjectGuid unitGuid) const {
  auto it = shields_.find(unitGuid.GetRawValue());
  if (it == shields_.end()) return 0;
  return static_cast<std::uint32_t>(it->second.size());
}

void AbsorbTracker::ClearUnit(ObjectGuid unitGuid) {
  shields_.erase(unitGuid.GetRawValue());
}

void AbsorbTracker::Reset() { shields_.clear(); }

}

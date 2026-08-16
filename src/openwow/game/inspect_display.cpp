
#include "openwow/game/inspect_display.h"

#include <algorithm>
#include <cstdio>

namespace openwow::game {

namespace {

constexpr int kIlvlSlotCount = 17;
}

void InspectDisplay::RequestInspect(ObjectGuid target) {
  pending_ = true;
  pendingTarget_ = target;
  data_.reset();
}

bool InspectDisplay::IsPending() const { return pending_; }

ObjectGuid InspectDisplay::GetPendingTarget() const { return pendingTarget_; }

void InspectDisplay::SetInspectData(InspectDisplayData data) {
  data_ = std::move(data);
  pending_ = false;
}

void InspectDisplay::ClearInspectData() {
  data_.reset();
  pending_ = false;
  pendingTarget_ = ObjectGuid(0);
}

std::optional<InspectDisplayData> InspectDisplay::GetInspectData() const {
  return data_;
}

bool InspectDisplay::HasInspectData() const { return data_.has_value(); }

size_t InspectDisplay::GetEquipmentCount() const {
  return data_ ? data_->equipment.size() : 0;
}

size_t InspectDisplay::GetTalentCount() const {
  return data_ ? data_->talentSpells.size() : 0;
}

float InspectDisplay::GetAverageItemLevel() const {
  if (!data_ || data_->equipment.empty()) return 0.0f;

  uint32_t ilvlSum = 0;
  int equippedCount = 0;
  bool hasAnyIlvl = false;
  for (const auto& slot : data_->equipment) {

    if (slot.slot == 3 || slot.slot == 18) continue;
    if (slot.itemId == 0) continue;
    ilvlSum += slot.itemLevel;
    if (slot.itemLevel > 0) hasAnyIlvl = true;
    ++equippedCount;
  }

  if (equippedCount == 0) return 0.0f;

  if (!hasAnyIlvl) return static_cast<float>(equippedCount);

  return static_cast<float>(ilvlSum) / static_cast<float>(kIlvlSlotCount);
}

std::string InspectDisplay::GetTalentSpecSummary() const {
  if (!data_ || data_->talentTrees.size() < 3) return "0/0/0";

  return std::to_string(data_->talentTrees[0].pointsSpent) + "/" +
         std::to_string(data_->talentTrees[1].pointsSpent) + "/" +
         std::to_string(data_->talentTrees[2].pointsSpent);
}

std::optional<InspectTalentTreeInfo> InspectDisplay::GetPrimaryTalentTree() const {
  if (!data_ || data_->talentTrees.empty()) return std::nullopt;

  const InspectTalentTreeInfo* best = nullptr;
  uint32_t maxPoints = 0;
  for (const auto& tree : data_->talentTrees) {
    if (tree.pointsSpent > maxPoints) {
      maxPoints = tree.pointsSpent;
      best = &tree;
    }
  }
  if (best) return *best;
  return std::nullopt;
}

bool InspectDisplay::IsTimedOut(float currentTime, float timeout) const {
  if (!data_) return true;
  return (currentTime - data_->timestamp) > timeout;
}

float InspectDisplay::GetTimeRemaining(float currentTime, float timeout) const {
  if (!data_) return 0.0f;
  float remaining = timeout - (currentTime - data_->timestamp);
  return remaining > 0.0f ? remaining : 0.0f;
}

std::string InspectDisplay::GetHonorSummary() const {
  if (!data_) return "";
  return "Honorable Kills: " + std::to_string(data_->honorKills);
}

std::string InspectDisplay::GetArenaSummary() const {
  if (!data_) return "";
  std::string s;
  if (data_->arenaRating2v2 > 0)
    s += "2v2: " + std::to_string(data_->arenaRating2v2);
  if (data_->arenaRating3v3 > 0) {
    if (!s.empty()) s += "  ";
    s += "3v3: " + std::to_string(data_->arenaRating3v3);
  }
  if (data_->arenaRating5v5 > 0) {
    if (!s.empty()) s += "  ";
    s += "5v5: " + std::to_string(data_->arenaRating5v5);
  }
  return s;
}

void InspectDisplay::Reset() {
  pending_ = false;
  pendingTarget_ = ObjectGuid(0);
  data_.reset();
}

}

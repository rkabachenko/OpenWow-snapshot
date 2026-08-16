
#include "openwow/game/faction_manager.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

const FactionEntry FactionManager::kEmpty{};

static constexpr std::uint8_t kFlagVisible = 0x01;
static constexpr std::uint8_t kFlagAtWar   = 0x02;

bool FactionManager::HandleInitializeFactions(const std::uint8_t* data,
                                                std::size_t len) {
  if (!data || len < 4) return false;

  PacketReader r(data, len);

  std::uint32_t count;
  if (!r.ReadU32(count)) return false;

  if (count > kMaxFactionSlots) count = kMaxFactionSlots;

  if (r.Remaining() < count * 5) return false;

  for (std::uint32_t i = 0; i < count; ++i) {
    if (!r.ReadU8(factions_[i].flags)) return false;
    if (!r.ReadI32(factions_[i].standing)) return false;
  }

  for (std::uint32_t i = count; i < kMaxFactionSlots; ++i) {
    factions_[i] = FactionEntry{};
  }

  return true;
}

bool FactionManager::HandleSetFactionStanding(const std::uint8_t* data,
                                                std::size_t len) {
  if (!data || len < 9) return false;

  PacketReader r(data, len);

  if (!r.ReadFloat(last_standing_.bonus_rep)) return false;

  std::uint8_t inc;
  if (!r.ReadU8(inc)) return false;
  last_standing_.increased = (inc != 0);

  std::uint32_t count;
  if (!r.ReadU32(count)) return false;

  if (count > kMaxFactionSlots) return false;

  if (r.Remaining() < count * 8) return false;

  last_standing_.updates.resize(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    auto& u = last_standing_.updates[i];
    if (!r.ReadU32(u.list_id)) return false;
    if (!r.ReadI32(u.standing)) return false;

    if (u.list_id < kMaxFactionSlots) {
      factions_[u.list_id].standing = u.standing;
    }
  }

  return true;
}

bool FactionManager::HandleSetFactionVisible(const std::uint8_t* data,
                                               std::size_t len) {
  if (!data || len < 4) return false;

  PacketReader r(data, len);
  if (!r.ReadU32(last_visible_)) return false;

  if (last_visible_ < kMaxFactionSlots) {
    factions_[last_visible_].flags |= kFlagVisible;
  }

  return true;
}

bool FactionManager::HandleSetFactionAtWar(const std::uint8_t* data,
                                           std::size_t len) {
  if (!data || len < 5) return false;

  PacketReader r(data, len);
  std::uint32_t slot = 0;
  std::uint8_t flags = 0;
  if (!r.ReadU32(slot)) return false;
  if (!r.ReadU8(flags)) return false;
  if (slot >= kMaxFactionSlots) return false;

  if ((flags & kFlagAtWar) != 0) {
    factions_[slot].flags |= kFlagAtWar;
  } else {
    factions_[slot].flags &= static_cast<std::uint8_t>(~kFlagAtWar);
  }

  return true;
}

const FactionEntry& FactionManager::GetFaction(std::uint32_t slot) const {
  return (slot < kMaxFactionSlots) ? factions_[slot] : kEmpty;
}

ReputationRank FactionManager::GetRankFromStanding(std::int32_t standing) {
  if (standing >= kRepThresholds[7]) return ReputationRank::Exalted;
  if (standing >= kRepThresholds[6]) return ReputationRank::Revered;
  if (standing >= kRepThresholds[5]) return ReputationRank::Honored;
  if (standing >= kRepThresholds[4]) return ReputationRank::Friendly;
  if (standing >= kRepThresholds[3]) return ReputationRank::Neutral;
  if (standing >= kRepThresholds[2]) return ReputationRank::Unfriendly;
  if (standing >= kRepThresholds[1]) return ReputationRank::Hostile;
  return ReputationRank::Hated;
}

std::string FactionManager::GetRankName(ReputationRank rank) {
  switch (rank) {
    case ReputationRank::Hated:      return "Hated";
    case ReputationRank::Hostile:    return "Hostile";
    case ReputationRank::Unfriendly: return "Unfriendly";
    case ReputationRank::Neutral:    return "Neutral";
    case ReputationRank::Friendly:   return "Friendly";
    case ReputationRank::Honored:    return "Honored";
    case ReputationRank::Revered:    return "Revered";
    case ReputationRank::Exalted:    return "Exalted";
  }
  return "Unknown";
}

std::int32_t FactionManager::GetBarMin(ReputationRank rank) {
  auto idx = static_cast<std::size_t>(rank);
  if (idx >= kRepThresholds.size()) return 0;
  return kRepThresholds[idx];
}

std::int32_t FactionManager::GetBarMax(ReputationRank rank) {
  auto idx = static_cast<std::size_t>(rank);
  if (idx + 1 >= kRepThresholds.size()) return 42999;
  return kRepThresholds[idx + 1] - 1;
}

float FactionManager::GetBarProgress(std::int32_t standing) {
  auto rank = GetRankFromStanding(standing);
  auto idx = static_cast<std::size_t>(rank);
  if (idx >= kRepBarWidths.size()) return 1.0f;

  std::int32_t barMin = kRepThresholds[idx];
  std::int32_t barWidth = kRepBarWidths[idx];
  if (barWidth <= 0) return 1.0f;

  float progress = static_cast<float>(standing - barMin) /
                   static_cast<float>(barWidth);
  return std::clamp(progress, 0.0f, 1.0f);
}

ReputationRank FactionManager::GetFactionRank(std::uint32_t slot) const {
  if (slot >= kMaxFactionSlots) return ReputationRank::Neutral;
  return GetRankFromStanding(factions_[slot].standing);
}

std::string FactionManager::GetFactionRankName(std::uint32_t slot) const {
  return GetRankName(GetFactionRank(slot));
}

float FactionManager::GetFactionBarProgress(std::uint32_t slot) const {
  if (slot >= kMaxFactionSlots) return 0.0f;
  return GetBarProgress(factions_[slot].standing);
}

void FactionManager::SetWatchedFaction(std::uint32_t slot) {
  watched_faction_ = slot;
}

std::uint32_t FactionManager::GetWatchedFaction() const {
  return watched_faction_;
}

bool FactionManager::IsAtWar(std::uint32_t slot) const {
  if (slot >= kMaxFactionSlots) return false;
  return (factions_[slot].flags & kFlagAtWar) != 0;
}

void FactionManager::SetAtWar(std::uint32_t slot, bool atWar) {
  if (slot >= kMaxFactionSlots) return;
  if (atWar) {
    factions_[slot].flags |= kFlagAtWar;
  } else {
    factions_[slot].flags &= static_cast<std::uint8_t>(~kFlagAtWar);
  }
}

bool FactionManager::IsVisible(std::uint32_t slot) const {
  if (slot >= kMaxFactionSlots) return false;
  return (factions_[slot].flags & kFlagVisible) != 0;
}

void FactionManager::Clear() {
  factions_.fill(FactionEntry{});
  last_standing_ = FactionStandingNotification{};
  last_visible_ = 0;
  watched_faction_ = 0;
}

}

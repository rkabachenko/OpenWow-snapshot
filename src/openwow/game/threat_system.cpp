
#include "openwow/game/threat_system.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace openwow::game {

const std::vector<ThreatInfo> ThreatSystem::kEmptyList;

ThreatSystem &ThreatSystem::Get() {
  static ThreatSystem instance;
  return instance;
}

ThreatInfo *ThreatSystem::FindEntry(ThreatTargetState &state, const ObjectGuid &unit) {
  for (auto &entry : state.entries) {
    if (entry.unit_guid == unit) {
      return &entry;
    }
  }
  return nullptr;
}

const ThreatInfo *ThreatSystem::FindEntry(const ThreatTargetState &state, const ObjectGuid &unit) {
  for (const auto &entry : state.entries) {
    if (entry.unit_guid == unit) {
      return &entry;
    }
  }
  return nullptr;
}

void ThreatSystem::InitializeEntry(ThreatInfo &entry, const ObjectGuid &unit) {
  entry = ThreatInfo{};
  entry.unit_guid = unit;
  entry.threat_percent = 255.0f;
  entry.raw_percent = 255;
}

ThreatInfo &ThreatSystem::FindOrCreateEntry(ThreatTargetState &state, const ObjectGuid &unit) {
  if (auto *const existing = FindEntry(state, unit)) {
    return *existing;
  }

  state.entries.emplace_back();
  InitializeEntry(state.entries.back(), unit);
  return state.entries.back();
}

std::uint8_t ThreatSystem::ComputeRawPercent(const std::uint32_t threat_value,
                                             const std::uint32_t highest_threat_value) {
  if (highest_threat_value == 0) {
    return 100;
  }
  if (threat_value == 0) {
    return 0;
  }

  const auto raw = (100ULL * static_cast<std::uint64_t>(threat_value)) /
                   static_cast<std::uint64_t>(highest_threat_value);
  return raw >= 250ULL ? 250 : static_cast<std::uint8_t>(raw);
}

bool ThreatSystem::RefreshRelativeThreatStatus(ThreatInfo &entry,
                                               const std::uint32_t highest_threat_value) {
  const auto previous_status = entry.threat_status;
  entry.raw_percent = ComputeRawPercent(entry.threat_value, highest_threat_value);
  entry.threat_percent = static_cast<float>(entry.raw_percent);
  entry.threat_status = entry.raw_percent < 100 ? ThreatStatus::kLow : ThreatStatus::kOvernuking;
  entry.is_tanking = false;
  return previous_status != entry.threat_status;
}

void ThreatSystem::RefreshHighestThreatStatus(ThreatTargetState &state) {
  if (state.highest_guid.IsEmpty()) {
    return;
  }

  auto *const highest_entry = FindEntry(state, state.highest_guid);
  if (highest_entry == nullptr) {
    return;
  }

  auto highest_status = ThreatStatus::kTanking;
  for (const auto &entry : state.entries) {
    if (entry.unit_guid == state.highest_guid) {
      continue;
    }
    if (entry.threat_status == ThreatStatus::kOvernuking) {
      highest_status = ThreatStatus::kPulling;
      break;
    }
  }

  highest_entry->threat_status = highest_status;
  highest_entry->is_tanking = true;
}

void ThreatSystem::RecomputeThreatTable(ThreatTargetState &state, const bool recompute_all,
                                        const std::vector<ObjectGuid> &touched_units) {
  if (state.highest_guid.IsEmpty()) {
    return;
  }

  const auto *const highest_entry = FindEntry(state, state.highest_guid);
  if (highest_entry == nullptr) {
    return;
  }

  bool any_status_changed = false;

  if (recompute_all) {
    for (auto &entry : state.entries) {
      if (entry.unit_guid == state.highest_guid) {
        continue;
      }
      any_status_changed |= RefreshRelativeThreatStatus(entry, highest_entry->threat_value);
    }
  } else {
    for (const auto &unit : touched_units) {
      if (unit == state.highest_guid) {
        continue;
      }
      if (auto *const entry = FindEntry(state, unit)) {
        any_status_changed |= RefreshRelativeThreatStatus(*entry, highest_entry->threat_value);
      }
    }
  }

  if (recompute_all || any_status_changed) {
    RefreshHighestThreatStatus(state);
  }
}

void ThreatSystem::LinkThreatTargetForUnit(const ObjectGuid &unit, const ObjectGuid &target) {
  if (unit.IsEmpty() || target.IsEmpty()) {
    return;
  }

  auto &reverse_targets = unit_targets_[unit.GetRawValue()].targets;
  std::size_t empty_slot = reverse_targets.size();
  for (std::size_t i = reverse_targets.size(); i > 0; --i) {
    const std::size_t slot = i - 1;
    if (reverse_targets[slot] == target) {
      return;
    }
    if (reverse_targets[slot].IsEmpty()) {
      empty_slot = slot;
    }
  }

  if (empty_slot != reverse_targets.size()) {
    reverse_targets[empty_slot] = target;
    return;
  }

  reverse_targets.push_back(target);
}

void ThreatSystem::UnlinkThreatTargetForUnit(const ObjectGuid &unit, const ObjectGuid &target) {
  if (unit.IsEmpty() || target.IsEmpty()) {
    return;
  }

  const auto it = unit_targets_.find(unit.GetRawValue());
  if (it == unit_targets_.end()) {
    return;
  }

  auto &reverse_targets = it->second.targets;
  for (std::size_t i = reverse_targets.size(); i > 0; --i) {
    auto &slot = reverse_targets[i - 1];
    if (slot == target) {
      slot = ObjectGuid{};
      break;
    }
  }

  const auto has_live_target = std::any_of(reverse_targets.begin(), reverse_targets.end(),
                                           [](const ObjectGuid &guid) { return !guid.IsEmpty(); });
  if (!has_live_target) {
    unit_targets_.erase(it);
  }
}

void ThreatSystem::SetThreatList(const ObjectGuid &target, const std::vector<ThreatInfo> &threats) {
  std::lock_guard lock(mutex_);
  auto &state = threat_lists_[target.GetRawValue()];
  for (const auto &entry : state.entries) {
    UnlinkThreatTargetForUnit(entry.unit_guid, target);
  }
  state.entries.clear();
  state.entries.reserve(threats.size());
  state.highest_guid = ObjectGuid{};

  for (const auto &threat : threats) {
    if (threat.unit_guid.IsEmpty()) {
      continue;
    }

    ThreatInfo entry;
    InitializeEntry(entry, threat.unit_guid);
    entry.threat_value = threat.threat_value;
    state.entries.push_back(entry);
    LinkThreatTargetForUnit(threat.unit_guid, target);
  }

  for (const auto &entry : state.entries) {
    if (state.highest_guid.IsEmpty()) {
      state.highest_guid = entry.unit_guid;
      continue;
    }

    const auto *const current_highest = FindEntry(state, state.highest_guid);
    if (current_highest == nullptr || entry.threat_value > current_highest->threat_value) {
      state.highest_guid = entry.unit_guid;
    }
  }

  RecomputeThreatTable(state, true, {});
}

void ThreatSystem::UpdateThreat(const ObjectGuid &target, const ObjectGuid &unit,
                                std::uint32_t threat) {
  std::lock_guard lock(mutex_);
  auto &state = threat_lists_[target.GetRawValue()];
  auto &entry = FindOrCreateEntry(state, unit);
  entry.threat_value = threat;
  LinkThreatTargetForUnit(unit, target);

  const auto *const current_highest = FindEntry(state, state.highest_guid);
  if (state.highest_guid.IsEmpty() || current_highest == nullptr ||
      threat > current_highest->threat_value) {
    state.highest_guid = unit;
  }

  RecomputeThreatTable(state, true, {});
}

void ThreatSystem::ApplyThreatPacketUpdate(const ObjectGuid &target,
                                           const std::vector<ThreatInfo> &threats) {
  std::lock_guard lock(mutex_);
  auto &state = threat_lists_[target.GetRawValue()];
  std::vector<ObjectGuid> touched_units;
  touched_units.reserve(threats.size());

  for (const auto &threat : threats) {
    auto &entry = FindOrCreateEntry(state, threat.unit_guid);
    entry.threat_value = threat.threat_value;
    touched_units.push_back(threat.unit_guid);
    LinkThreatTargetForUnit(threat.unit_guid, target);
  }

  RecomputeThreatTable(state, false, touched_units);
}

void ThreatSystem::ApplyHighestThreatPacketUpdate(const ObjectGuid &target,
                                                  const ObjectGuid &highest_guid,
                                                  const std::vector<ThreatInfo> &threats) {
  std::lock_guard lock(mutex_);
  auto &state = threat_lists_[target.GetRawValue()];
  state.highest_guid = highest_guid;
  if (!highest_guid.IsEmpty()) {
    FindOrCreateEntry(state, highest_guid);
    LinkThreatTargetForUnit(highest_guid, target);
  }

  std::vector<ObjectGuid> touched_units;
  touched_units.reserve(threats.size());
  for (const auto &threat : threats) {
    auto &entry = FindOrCreateEntry(state, threat.unit_guid);
    entry.threat_value = threat.threat_value;
    touched_units.push_back(threat.unit_guid);
    LinkThreatTargetForUnit(threat.unit_guid, target);
  }

  RecomputeThreatTable(state, true, touched_units);
}

void ThreatSystem::RemoveThreatEntry(const ObjectGuid &target, const ObjectGuid &unit) {
  std::lock_guard lock(mutex_);
  UnlinkThreatTargetForUnit(unit, target);
  auto it = threat_lists_.find(target.GetRawValue());
  if (it == threat_lists_.end()) {
    return;
  }

  auto &state = it->second;
  const auto entry_it =
      std::find_if(state.entries.begin(), state.entries.end(),
                   [&](const ThreatInfo &entry) { return entry.unit_guid == unit; });
  if (entry_it == state.entries.end()) {
    return;
  }

  const bool removed_was_highest = state.highest_guid == unit;
  const bool refresh_highest = !removed_was_highest && entry_it->threat_status > ThreatStatus::kLow;
  state.entries.erase(entry_it);

  if (removed_was_highest) {
    state.highest_guid = ObjectGuid{};
  } else if (refresh_highest) {
    RefreshHighestThreatStatus(state);
  }

  if (state.entries.empty() && state.highest_guid.IsEmpty()) {
    threat_lists_.erase(it);
  }
}

void ThreatSystem::RemoveThreatTargetForUnit(const ObjectGuid &unit, const ObjectGuid &target) {
  std::lock_guard lock(mutex_);
  UnlinkThreatTargetForUnit(unit, target);
}

void ThreatSystem::ClearThreatList(const ObjectGuid &target) {
  std::lock_guard lock(mutex_);
  const auto it = threat_lists_.find(target.GetRawValue());
  if (it == threat_lists_.end()) {
    return;
  }

  for (const auto &entry : it->second.entries) {
    UnlinkThreatTargetForUnit(entry.unit_guid, target);
  }
  threat_lists_.erase(it);
}

void ThreatSystem::ClearThreatLists() {
  std::lock_guard lock(mutex_);
  threat_lists_.clear();
  unit_targets_.clear();
}

const std::vector<ThreatInfo> &ThreatSystem::GetThreatList(const ObjectGuid &target) const {
  std::lock_guard lock(mutex_);
  auto it = threat_lists_.find(target.GetRawValue());
  if (it == threat_lists_.end())
    return kEmptyList;
  return it->second.entries;
}

float ThreatSystem::GetThreatPercent(const ObjectGuid &target, const ObjectGuid &unit) const {
  std::lock_guard lock(mutex_);
  auto it = threat_lists_.find(target.GetRawValue());
  if (it == threat_lists_.end() || it->second.highest_guid.IsEmpty()) {
    return 0.0f;
  }
  const auto *const entry = FindEntry(it->second, unit);
  return entry ? entry->threat_percent : 0.0f;
}

bool ThreatSystem::IsTanking(const ObjectGuid &target, const ObjectGuid &unit) const {
  std::lock_guard lock(mutex_);
  auto it = threat_lists_.find(target.GetRawValue());
  if (it == threat_lists_.end() || it->second.highest_guid.IsEmpty()) {
    return false;
  }
  const auto *const entry = FindEntry(it->second, unit);
  return entry ? entry->is_tanking : false;
}

std::uint8_t ThreatSystem::GetThreatStatus(const ObjectGuid &target, const ObjectGuid &unit) const {
  std::lock_guard lock(mutex_);
  auto it = threat_lists_.find(target.GetRawValue());
  if (it == threat_lists_.end() || it->second.highest_guid.IsEmpty()) {
    return ThreatStatus::kLow;
  }
  const auto *const entry = FindEntry(it->second, unit);
  return entry ? entry->threat_status : ThreatStatus::kLow;
}

float ThreatSystem::GetPlayerThreatPercent() const {
  std::lock_guard lock(mutex_);
  if (player_guid_.IsEmpty())
    return 0.0f;
  float max_pct = 0.0f;
  for (const auto &[_, state] : threat_lists_) {
    if (state.highest_guid.IsEmpty()) {
      continue;
    }
    for (const auto &entry : state.entries) {
      if (entry.unit_guid == player_guid_) {
        max_pct = std::max(max_pct, entry.threat_percent);
      }
    }
  }
  return max_pct;
}

std::uint8_t ThreatSystem::GetPlayerThreatStatus() const {
  std::lock_guard lock(mutex_);
  if (player_guid_.IsEmpty())
    return ThreatStatus::kLow;
  std::uint8_t worst = ThreatStatus::kLow;
  for (const auto &[_, state] : threat_lists_) {
    if (state.highest_guid.IsEmpty()) {
      continue;
    }
    for (const auto &entry : state.entries) {
      if (entry.unit_guid == player_guid_) {
        worst = std::max(worst, entry.threat_status);
      }
    }
  }
  return worst;
}

bool ThreatSystem::IsPlayerTanking() const {
  std::lock_guard lock(mutex_);
  if (player_guid_.IsEmpty())
    return false;
  for (const auto &[_, state] : threat_lists_) {
    if (state.highest_guid.IsEmpty()) {
      continue;
    }
    for (const auto &entry : state.entries) {
      if (entry.unit_guid == player_guid_ && entry.is_tanking) {
        return true;
      }
    }
  }
  return false;
}

void ThreatSystem::SetPlayerGuid(const ObjectGuid &guid) {
  std::lock_guard lock(mutex_);
  player_guid_ = guid;
}

void ThreatSystem::Reset() {
  ClearThreatLists();
  std::lock_guard lock(mutex_);
  player_guid_ = ObjectGuid{};
}

float ThreatSystem::GetMyThreat(const ObjectGuid &target) const {
  std::lock_guard lock(mutex_);
  if (player_guid_.IsEmpty())
    return 0.0f;
  auto it = threat_lists_.find(target.GetRawValue());
  if (it == threat_lists_.end()) {
    return 0.0f;
  }
  const auto *const entry = FindEntry(it->second, player_guid_);
  return entry ? static_cast<float>(entry->threat_value) : 0.0f;
}

float ThreatSystem::GetMyThreatPercent(const ObjectGuid &target) const {
  std::lock_guard lock(mutex_);
  if (player_guid_.IsEmpty())
    return 0.0f;
  auto it = threat_lists_.find(target.GetRawValue());
  if (it == threat_lists_.end() || it->second.highest_guid.IsEmpty()) {
    return 0.0f;
  }
  const auto *const entry = FindEntry(it->second, player_guid_);
  return entry ? entry->threat_percent : 0.0f;
}

bool ThreatSystem::AmITanking(const ObjectGuid &target) const {
  std::lock_guard lock(mutex_);
  if (player_guid_.IsEmpty())
    return false;
  auto it = threat_lists_.find(target.GetRawValue());
  if (it == threat_lists_.end() || it->second.highest_guid.IsEmpty()) {
    return false;
  }
  const auto *const entry = FindEntry(it->second, player_guid_);
  return entry ? entry->is_tanking : false;
}

std::optional<ThreatInfo> ThreatSystem::GetHighestThreat(const ObjectGuid &target) const {
  std::lock_guard lock(mutex_);
  auto it = threat_lists_.find(target.GetRawValue());
  if (it == threat_lists_.end() || it->second.entries.empty())
    return std::nullopt;
  const ThreatInfo *best = nullptr;
  for (const auto &e : it->second.entries) {
    if (!best || e.threat_value > best->threat_value) {
      best = &e;
    }
  }
  return best ? std::optional<ThreatInfo>(*best) : std::nullopt;
}

std::uint32_t ThreatSystem::GetTrackedTargetCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(threat_lists_.size());
}

bool ThreatSystem::HasThreatData(const ObjectGuid &target) const {
  std::lock_guard lock(mutex_);
  const auto it = threat_lists_.find(target.GetRawValue());
  return it != threat_lists_.end() && !it->second.entries.empty();
}

ObjectGuid ThreatSystem::GetHighestThreatGuid(const ObjectGuid &target) const {
  std::lock_guard lock(mutex_);
  const auto it = threat_lists_.find(target.GetRawValue());
  if (it == threat_lists_.end()) {
    return ObjectGuid{};
  }
  return it->second.highest_guid;
}

bool ThreatSystem::TryGetThreatQueryData(const ObjectGuid &target, const ObjectGuid &unit,
                                         ThreatQueryData *out) const {
  if (out == nullptr) {
    return false;
  }

  std::lock_guard lock(mutex_);
  *out = ThreatQueryData{};

  const auto it = threat_lists_.find(target.GetRawValue());
  if (it == threat_lists_.end()) {
    return false;
  }

  const auto *const entry = FindEntry(it->second, unit);
  out->has_entry = entry != nullptr;
  out->has_highest_guid = !it->second.highest_guid.IsEmpty();
  out->highest_guid = it->second.highest_guid;
  if (entry != nullptr) {
    out->entry = *entry;
  }

  return entry != nullptr && !it->second.highest_guid.IsEmpty();
}

std::vector<ObjectGuid> ThreatSystem::GetTargetsForUnit(const ObjectGuid &unit) const {
  std::lock_guard lock(mutex_);
  std::vector<ObjectGuid> targets;
  const auto it = unit_targets_.find(unit.GetRawValue());
  if (it == unit_targets_.end()) {
    return targets;
  }

  targets.reserve(it->second.targets.size());
  for (const auto &target : it->second.targets) {
    if (!target.IsEmpty()) {
      targets.push_back(target);
    }
  }
  return targets;
}

}

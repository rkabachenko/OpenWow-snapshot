
#include "openwow/game/world_state_manager.h"

#include <algorithm>
#include <ctime>
#include <sstream>

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::game {

namespace {

bool IsLuaVisibleWorldStateUiType(const std::uint32_t type) {
  return type == 0 || type == 1 || type == 3;
}

}

bool WorldStateManager::HandleInitWorldStates(const std::uint8_t* data,
                                             std::size_t len) {
  PacketReader r(data, len);

  std::int32_t map_id, zone_id, area_id;
  if (!r.ReadI32(map_id)) return false;
  if (!r.ReadI32(zone_id)) return false;
  if (!r.ReadI32(area_id)) return false;

  std::uint16_t count;
  if (!r.ReadU16(count)) return false;

  const bool location_changed =
      SetPacketWorldStateUiLocation(map_id, zone_id, area_id);
  bool state_changed = false;
  states_.reserve(states_.size() + count);

  for (std::uint16_t i = 0; i < count; ++i) {
    std::int32_t var_id, value;
    if (!r.ReadI32(var_id)) return false;
    if (!r.ReadI32(value)) return false;
    state_changed = ApplyWorldStateValue(var_id, value) || state_changed;
  }

  if (location_changed || state_changed) {
    BumpWorldStateUiRevision();
  }

  diagnostics::Log(diagnostics::LogLevel::kInfo,
      "WorldState: init map=" + std::to_string(ui_map_id_) +
      " zone=" + std::to_string(ui_zone_id_) +
      " area=" + std::to_string(ui_area_id_) +
      " packet_entries=" + std::to_string(count) +
      " tracked_states=" + std::to_string(states_.size()));
  return true;
}

bool WorldStateManager::HandleUpdateWorldState(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);

  std::int32_t var_id, value;
  if (!r.ReadI32(var_id)) return false;
  if (!r.ReadI32(value)) return false;

  if (ApplyWorldStateValue(var_id, value)) {
    BumpWorldStateUiRevision();
  }
  return true;
}

std::int32_t WorldStateManager::GetWorldState(std::int32_t variable_id) const {
  auto it = states_.find(variable_id);
  return (it != states_.end()) ? it->second : 0;
}

bool WorldStateManager::HasWorldState(std::int32_t variable_id) const {
  return states_.find(variable_id) != states_.end();
}

void WorldStateManager::SetWorldState(std::int32_t variable_id,
                                      std::int32_t value) {
  if (ApplyWorldStateValue(variable_id, value)) {
    BumpWorldStateUiRevision();
  }
}

bool WorldStateManager::RemoveWorldState(std::int32_t variable_id) {
  auto it = states_.find(variable_id);
  if (it == states_.end()) return false;
  std::int32_t old_val = it->second;
  states_.erase(it);
  BumpWorldStateUiRevision();
  if (old_val != 0) {
    FireCallbacks(variable_id, old_val, 0);
  }
  return true;
}

std::vector<WorldStateEntry> WorldStateManager::GetAllEntries() const {
  std::vector<WorldStateEntry> result;
  result.reserve(states_.size());
  for (const auto& [var_id, value] : states_) {
    result.push_back({var_id, value});
  }
  std::sort(result.begin(), result.end(),
            [](const WorldStateEntry& a, const WorldStateEntry& b) {
              return a.variable_id < b.variable_id;
            });
  return result;
}

void WorldStateManager::SetMapId(std::int32_t id) {
  map_id_ = id;
}

void WorldStateManager::SetZoneId(std::int32_t id) {
  zone_id_ = id;
}

void WorldStateManager::SetAreaId(std::int32_t id) {
  area_id_ = id;
}

bool WorldStateManager::SetWorldStateUiFilterMask(std::uint32_t mask) {
  if (world_state_ui_filter_mask_ == mask) {
    return false;
  }

  world_state_ui_filter_mask_ = mask;
  BumpWorldStateUiRevision();
  return true;
}

void WorldStateManager::SetWorldStateUiServerTime(
    std::uint32_t absolute_time_seconds) {
  const auto now = static_cast<std::int64_t>(std::time(nullptr));
  world_state_ui_time_offset_seconds_ = static_cast<std::int32_t>(
      static_cast<std::int64_t>(absolute_time_seconds) - now);
}

std::int32_t WorldStateManager::world_state_ui_current_time_seconds(
    std::time_t now_seconds) const {
  return static_cast<std::int32_t>(now_seconds) + world_state_ui_time_offset_seconds_;
}

bool WorldStateManager::IsVisibleWorldStateUiEntry(
    const openwow::data::dbc::WorldStateUIEntry& entry) const {
  if (!world_state_ui_location_active_) {
    return false;
  }

  if (!IsLuaVisibleWorldStateUiType(entry.type)) {
    return false;
  }

  if (entry.map_id != -1 && entry.map_id != ui_map_id_) {
    return false;
  }

  if (entry.area_id != 0 &&
      static_cast<std::int32_t>(entry.area_id) != ui_zone_id_ &&
      static_cast<std::int32_t>(entry.area_id) != ui_area_id_) {
    return false;
  }

  return entry.phase_shift == 0 ||
         (entry.phase_shift & world_state_ui_filter_mask_) != 0;
}

bool WorldStateManager::HasVisibleWorldStateUiType(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint32_t type) const {
  if (!world_state_ui_location_active_) {
    return false;
  }

  for (const auto& entry : dbc.world_state_ui().entries()) {
    if (entry.type == type && IsVisibleWorldStateUiEntry(entry)) {
      return true;
    }
  }

  return false;
}

std::size_t WorldStateManager::CountVisibleWorldStateUiEntries(
    const openwow::data::dbc::DbcLoader& dbc) const {
  if (!world_state_ui_location_active_) {
    return 0;
  }

  std::size_t count = 0;
  for (const auto& entry : dbc.world_state_ui().entries()) {
    if (IsVisibleWorldStateUiEntry(entry)) {
      ++count;
    }
  }
  return count;
}

const openwow::data::dbc::WorldStateUIEntry*
WorldStateManager::FindVisibleWorldStateUiEntry(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::size_t visible_index) const {
  if (!world_state_ui_location_active_) {
    return nullptr;
  }

  std::size_t current_index = 0;
  for (const auto& entry : dbc.world_state_ui().entries()) {
    if (!IsVisibleWorldStateUiEntry(entry)) {
      continue;
    }

    if (current_index == visible_index) {
      return &entry;
    }
    ++current_index;
  }

  return nullptr;
}

void WorldStateManager::ApplyBatch(
    const std::vector<WorldStateEntry>& entries) {
  for (const auto& entry : entries) {
    SetWorldState(entry.variable_id, entry.value);
  }
}

std::unordered_map<std::int32_t, std::int32_t>
WorldStateManager::Snapshot() const {
  return states_;
}

std::vector<WorldStateEntry> WorldStateManager::Diff(
    const std::unordered_map<std::int32_t, std::int32_t>& snapshot) const {
  std::vector<WorldStateEntry> result;

  for (const auto& [var_id, cur_val] : states_) {
    auto it = snapshot.find(var_id);
    std::int32_t snap_val = (it != snapshot.end()) ? it->second : 0;
    if (cur_val != snap_val) {
      result.push_back({var_id, cur_val});
    }
  }

  for (const auto& [var_id, snap_val] : snapshot) {
    if (states_.find(var_id) == states_.end() && snap_val != 0) {
      result.push_back({var_id, 0});
    }
  }

  std::sort(result.begin(), result.end(),
            [](const WorldStateEntry& a, const WorldStateEntry& b) {
              return a.variable_id < b.variable_id;
            });
  return result;
}

WorldStateManager::CallbackHandle WorldStateManager::RegisterCallback(
    WorldStateCallback cb) {
  CallbackHandle h = next_handle_++;
  callbacks_.push_back({h, std::move(cb)});
  return h;
}

void WorldStateManager::UnregisterCallback(CallbackHandle handle) {
  callbacks_.erase(
      std::remove_if(callbacks_.begin(), callbacks_.end(),
                     [handle](const CallbackEntry& e) {
                       return e.handle == handle;
                     }),
      callbacks_.end());
}

void WorldStateManager::FireCallbacks(std::int32_t var_id,
                                      std::int32_t old_val,
                                      std::int32_t new_val) {
  for (const auto& entry : callbacks_) {
    if (entry.cb) entry.cb(var_id, old_val, new_val);
  }
}

std::string WorldStateManager::DebugDump() const {
  std::ostringstream ss;
  ss << "WorldStateManager: local map=" << map_id_
     << " zone=" << zone_id_
     << " area=" << area_id_
     << " | packet-ui map=" << ui_map_id_
     << " zone=" << ui_zone_id_
     << " area=" << ui_area_id_
     << " count=" << states_.size() << "\n";

  auto entries = GetAllEntries();
  for (const auto& e : entries) {
    ss << "  [" << e.variable_id << "] = " << e.value << "\n";
  }
  return ss.str();
}

void WorldStateManager::Clear() {
  map_id_ = -1;
  zone_id_ = 0;
  area_id_ = 0;
  ui_map_id_ = -1;
  ui_zone_id_ = 0;
  ui_area_id_ = 0;
  world_state_ui_location_active_ = false;
  world_state_ui_filter_mask_ = 1;
  world_state_ui_time_offset_seconds_ = 0;
  world_state_ui_revision_ = 0;
  states_.clear();
  callbacks_.clear();
  next_handle_ = 1;
}

bool WorldStateManager::SetPacketWorldStateUiLocation(std::int32_t map_id,
                                                      std::int32_t zone_id,
                                                      std::int32_t area_id) {
  if (world_state_ui_location_active_ && ui_map_id_ == map_id &&
      ui_zone_id_ == zone_id && ui_area_id_ == area_id) {
    return false;
  }

  ui_map_id_ = map_id;
  ui_zone_id_ = zone_id;
  ui_area_id_ = area_id;
  world_state_ui_location_active_ = true;
  return true;
}

bool WorldStateManager::ApplyWorldStateValue(const std::int32_t variable_id,
                                             const std::int32_t value) {
  auto [it, inserted] = states_.try_emplace(variable_id, value);
  if (inserted) {
    if (value != 0) {
      FireCallbacks(variable_id, 0, value);
      return true;
    }
    return false;
  }

  if (it->second == value) {
    return false;
  }

  const std::int32_t old_value = it->second;
  it->second = value;
  FireCallbacks(variable_id, old_value, value);
  return true;
}

void WorldStateManager::BumpWorldStateUiRevision() {
  ++world_state_ui_revision_;
}

}

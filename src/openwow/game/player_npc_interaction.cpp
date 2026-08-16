
#include "openwow/game/player_npc_interaction.h"

#include "openwow/data/formats/dbc/dbc_entries_world.h"

#include <cmath>

namespace openwow::game {

namespace {

void RebuildAreaTriggerMapSlice(
    AreaTriggerSystem& system,
    const std::span<const openwow::data::dbc::AreaTriggerEntry> entries,
    const std::uint32_t map_id) {

  const std::size_t empty_slice_position = entries.size();

  system.current_map_id = map_id;
  system.map_slice_begin = empty_slice_position;
  system.map_slice_end = empty_slice_position;
  system.active_trigger_index.reset();

  for (std::size_t index = 0; index < entries.size(); ++index) {
    const auto entry_map_id = entries[index].map_id;
    if (entry_map_id < map_id) {
      continue;
    }

    if (entry_map_id == map_id) {

      if (system.map_slice_begin == empty_slice_position) {
        system.map_slice_begin = index;
      }
      continue;
    }

    if (system.map_slice_begin != empty_slice_position) {
      system.map_slice_end = index;
    }
    break;
  }
}

}

bool AreaTriggerSystem::ConsumeCheckDue(const std::uint32_t now_ms) {

  if (next_check_tick_ms != 0u &&
      static_cast<std::int32_t>(now_ms - next_check_tick_ms) < 0) {
    return false;
  }
  next_check_tick_ms = now_ms + kAreaTriggerCheckIntervalMs;
  return true;
}

void AreaTriggerSystem::Cleanup() {
  is_registered = false;
  current_map_id = 0;
  map_slice_begin = 0;
  map_slice_end = 0;
  active_trigger_index.reset();
  next_check_tick_ms = 0;
}

bool AreaTriggerSystem::ContainsPoint(
    const openwow::data::dbc::AreaTriggerEntry& entry,
    const std::array<float, 3>& player_position) {
  const float dx = player_position[0] - entry.x;
  const float dy = player_position[1] - entry.y;
  const float dz = player_position[2] - entry.z;

  if (entry.radius != 0.0f) {
    const float distance_sq = dx * dx + dy * dy + dz * dz;
    return distance_sq <= entry.radius * entry.radius;
  }

  const float cosine = std::cos(entry.box_yaw);
  const float sine = std::sin(entry.box_yaw);
  const float local_x = cosine * dx + sine * dy;
  const float local_y = -sine * dx + cosine * dy;
  const float local_z = dz;

  const float half_length = entry.box_length * 0.5f;
  const float half_width = entry.box_width * 0.5f;
  const float half_height = entry.box_height * 0.5f;
  return -half_length < local_x && local_x < half_length &&
         -half_width < local_y && local_y < half_width &&
         -half_height < local_z && local_z < half_height;
}

std::optional<std::uint32_t> AreaTriggerSystem::Update(
    const std::span<const openwow::data::dbc::AreaTriggerEntry> entries,
    const std::uint32_t map_id,
    const std::array<float, 3>& player_position) {
  if (entries.empty()) {
    is_registered = true;
    active_trigger_index.reset();
    map_slice_begin = 0;
    map_slice_end = 0;
    current_map_id = map_id;
    return std::nullopt;
  }

  if (!is_registered ||
      current_map_id != map_id ||
      map_slice_begin > entries.size() ||
      map_slice_end > entries.size() ||
      map_slice_begin > map_slice_end) {
    RebuildAreaTriggerMapSlice(*this, entries, map_id);
  }
  is_registered = true;

  if (active_trigger_index.has_value()) {
    const std::size_t index = *active_trigger_index;
    if (index < entries.size() &&
        index >= map_slice_begin &&
        index < map_slice_end &&
        ContainsPoint(entries[index], player_position)) {
      return std::nullopt;
    }
    active_trigger_index.reset();
  }

  if (map_slice_begin >= map_slice_end || map_slice_begin >= entries.size()) {
    return std::nullopt;
  }

  for (std::size_t index = map_slice_begin; index < map_slice_end; ++index) {
    if (!ContainsPoint(entries[index], player_position)) {
      continue;
    }

    active_trigger_index = index;
    return entries[index].id;
  }

  return std::nullopt;
}

}

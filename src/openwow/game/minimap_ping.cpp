
#include "openwow/game/minimap_ping.h"

#include "openwow/game/group_system.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <cmath>

namespace openwow::game {

void MinimapPingSystem::Reset() {
  std::lock_guard lock(mutex_);
  last_ping_world_x_ = 0.0f;
  last_ping_world_y_ = 0.0f;
}

void MinimapPingSystem::SetLastPingWorldPosition(const float world_x,
                                                 const float world_y) {
  std::lock_guard lock(mutex_);
  last_ping_world_x_ = world_x;
  last_ping_world_y_ = world_y;
}

std::pair<float, float> MinimapPingSystem::GetLastPingWorldPosition() const {
  std::lock_guard lock(mutex_);
  return {last_ping_world_x_, last_ping_world_y_};
}

MinimapPingPosition MinimapPingSystem::GetNormalizedPingPosition(
    const float player_x, const float player_y,
    const float player_facing) const {
  auto [world_x, world_y] = GetLastPingWorldPosition();
  float delta_x = world_x - player_x;
  float delta_y = world_y - player_y;
  if (ShouldRotateMinimap()) {
    const float minimap_direction =
        GetDisplayedMinimapDirection(player_facing);
    const float cos_angle = std::cos(-minimap_direction);
    const float sin_angle = std::sin(-minimap_direction);
    const float rotated_x = delta_x * cos_angle - delta_y * sin_angle;
    const float rotated_y = delta_x * sin_angle + delta_y * cos_angle;
    delta_x = rotated_x;
    delta_y = rotated_y;
  }

  const double diameter = static_cast<double>(GetVisibleRadius()) * 2.0;
  if (diameter == 0.0) {
    return {};
  }

  return {
      .x = -static_cast<double>(delta_y) / diameter,
      .y = static_cast<double>(delta_x) / diameter,
  };
}

std::pair<float, float> MinimapPingSystem::ResolveWorldPositionFromFrameOffset(
    const double frame_x, const double frame_y, const float minimap_width,
    const float minimap_height, const float player_x, const float player_y,
    const float player_facing) const {
  if (minimap_width == 0.0f || minimap_height == 0.0f) {
    return {player_x, player_y};
  }

  const float ui_script_width = openwow::ui::GetUiScriptScreenWidth();
  const float stored_x = openwow::ui::ApplyCachedUiHorizontalStretch(
      static_cast<float>(frame_x) / ui_script_width);
  const float stored_y = openwow::ui::ApplyCachedUiHorizontalStretch(
      static_cast<float>(frame_y) / ui_script_width);
  const float diameter = GetVisibleRadius() * 2.0f;
  float delta_x = (stored_y / minimap_width) * diameter;
  float delta_y = (-stored_x / minimap_height) * diameter;
  if (ShouldRotateMinimap()) {
    const float minimap_direction =
        GetDisplayedMinimapDirection(player_facing);
    const float cos_angle = std::cos(minimap_direction);
    const float sin_angle = std::sin(minimap_direction);
    const float rotated_x = delta_x * cos_angle - delta_y * sin_angle;
    const float rotated_y = delta_x * sin_angle + delta_y * cos_angle;
    delta_x = rotated_x;
    delta_y = rotated_y;
  }
  return {player_x + delta_x, player_y + delta_y};
}

std::pair<float, float> MinimapPingSystem::ResolveWorldPosition(
    const double normalized_x, const double normalized_y,
    const float player_x, const float player_y,
    const float player_facing) const {
  const float diameter = GetVisibleRadius() * 2.0f;
  float delta_x = static_cast<float>(normalized_y * diameter);
  float delta_y = static_cast<float>(-normalized_x * diameter);
  if (ShouldRotateMinimap()) {
    const float minimap_direction =
        GetDisplayedMinimapDirection(player_facing);
    const float cos_angle = std::cos(minimap_direction);
    const float sin_angle = std::sin(minimap_direction);
    const float rotated_x = delta_x * cos_angle - delta_y * sin_angle;
    const float rotated_y = delta_x * sin_angle + delta_y * cos_angle;
    delta_x = rotated_x;
    delta_y = rotated_y;
  }

  return {player_x + delta_x, player_y + delta_y};
}

void MinimapPingSystem::DispatchPingEvent(const std::uint64_t source_guid,
                                          const std::uint64_t active_player_guid,
                                          const float player_x,
                                          const float player_y,
                                          const float player_facing) const {
  const auto position =
      GetNormalizedPingPosition(player_x, player_y, player_facing);
  openwow::ui::game::ScriptEventDispatch::Get().FireEventArgs(
      openwow::ui::game::events::MINIMAP_PING,
      {ResolveSourceUnitToken(source_guid, active_player_guid), position.x,
       position.y});
}

std::string MinimapPingSystem::ResolveSourceUnitToken(
    const std::uint64_t source_guid,
    const std::uint64_t active_player_guid) const {
  std::string token = "player";
  if (source_guid == active_player_guid) {
    return token;
  }

  const auto& group_system = GroupSystem::Get();
  const auto tracked_party_count = group_system.GetTrackedPartyMemberCount();
  for (std::uint32_t slot = 0; slot < tracked_party_count; ++slot) {
    if (group_system.GetTrackedPartyMemberGuid(slot) == source_guid) {
      return "party" + std::to_string(slot + 1);
    }
  }

  const auto raid_count = group_system.GetNumGroupMembers();
  for (std::size_t index = 0; index < raid_count; ++index) {
    const auto* member = group_system.GetMember(index);
    if (member != nullptr && member->guid == source_guid) {
      return "raid" + std::to_string(index + 1);
    }
  }

  return token;
}

bool MinimapPingSystem::ShouldRotateMinimap() {
  return openwow::ui::game::CVarSystem::Instance().GetCVarBool(
      "rotateMinimap");
}

float MinimapPingSystem::GetDisplayedMinimapDirection(
    const float fallback_player_facing) const {
  if (!ShouldRotateMinimap()) {
    return fallback_player_facing;
  }

  return minimap_.GetPlayerOrientation();
}

float MinimapPingSystem::GetVisibleRadius() const {
  return minimap_.GetVisibleRadius();
}

}

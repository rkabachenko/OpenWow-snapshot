
#include "openwow/render/scene/world_frame.h"
#include "openwow/game/c_input_control.h"
#include "openwow/ui/game/nameplate_system.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/math/planar_facing_angle.h"
#include "openwow/game/unit_defines.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/scene/object_renderer.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/world/collision/collision.h"
#include "openwow/world/interaction/world_cursor_policy.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openwow::render {

namespace {

constexpr float kPickDistanceTieEpsilon = 1.0e-4f;

int BuildCursorIntersectionMask(const game::ObjectPresentationSnapshot *const objects,
                                openwow::world::CursorInfoContext context) {
  context.active_player_present = objects != nullptr && !objects->local_player.guid.IsEmpty();
  return openwow::world::BuildWorldCursorIntersectionMask(context);
}

PickResult::HitType ResolveModelPickHitType(const game::ObjectPresentationRecord &object) {
  if (object.type_id == game::TypeID::kGameObject) {
    return PickResult::HitType::kGameObject;
  }
  if (object.type_id == game::TypeID::kCorpse) {
    return PickResult::HitType::kCorpse;
  }
  if (object.type_id == game::TypeID::kUnit || object.type_id == game::TypeID::kPlayer) {
    return PickResult::HitType::kUnit;
  }
  return PickResult::HitType::kNone;
}

bool PassesUnitModelFilter(const game::ObjectPresentationRecord &unit, const std::uint32_t flags,
                           const game::ObjectPresentationSnapshot &objects) {
  if ((flags & 0x20u) == 0u && unit.controlled_by_local_player) {
    return false;
  }
  if ((flags & 0xF0000u) != 0u && (objects.local_player.guid.IsEmpty() ||
                                   (flags & unit.model_relation_mask & 0xF0000u) == 0u)) {
    return false;
  }
  if ((flags & 0x300000u) != 0u) {
    const bool alive = unit.health > 0u;
    if (alive ? (flags & 0x100000u) == 0u : (flags & 0x200000u) == 0u) {
      return false;
    }
  }
  if (!objects.has_active_targeting_spell) {
    return true;
  }
  if ((flags & 0x400000u) != 0u &&
      unit.creature_type == static_cast<std::uint32_t>(game::CreatureTypeId::kNonCombatPet)) {
    return true;
  }
  return (unit.unit_flags & 0x100u) == 0u || objects.active_spell_allows_flagged_units;
}

bool PassesModelFilter(const game::ObjectPresentationRecord &object, const std::uint32_t flags,
                       const game::ObjectPresentationSnapshot &objects) {

  if (object.handle == objects.local_player &&
      !world::Camera_IsActivePlayerBoundAlphaVisible()) {
    return false;
  }
  if (object.type_id == game::TypeID::kPlayer) {
    return (flags & 0x10u) != 0u && PassesUnitModelFilter(object, flags, objects);
  }
  if (object.type_id == game::TypeID::kUnit) {
    return (flags & 0x08u) != 0u && PassesUnitModelFilter(object, flags, objects);
  }
  if (object.type_id == game::TypeID::kGameObject) {
    return (flags & 0x04u) != 0u &&
           (!objects.has_active_targeting_spell || object.game_object_spell_focus_eligible) &&
           (object.game_object_requirements_met || objects.active_spell_allows_tapped);
  }
  if (object.type_id == game::TypeID::kCorpse) {
    return (flags & 0x40u) != 0u &&
           (!objects.has_active_targeting_spell || object.corpse_spell_target_eligible);
  }
  return false;
}

int ModelHitPriority(const game::ObjectPresentationRecord &object,
                     const game::ObjectPresentationSnapshot &objects) {
  if (object.handle.guid == objects.target) {
    return -1;
  }
  if (object.type_id == game::TypeID::kGameObject) {
    return object.game_object_highlighted ? 1 : 0;
  }
  if (object.type_id == game::TypeID::kCorpse) {
    return (object.dynamic_flags & 0x1u) != 0u ? 2 : 0;
  }
  if (object.type_id == game::TypeID::kPlayer || object.type_id == game::TypeID::kUnit) {
    return object.health > 0u ? 3 : ((object.dynamic_flags & 0x1u) != 0u ? 2 : 0);
  }
  return 0;
}

}

void WorldFrame::Initialize(std::uint32_t width, std::uint32_t height) {
  width_ = width;
  height_ = height;

  view_ = kRenderIdentityMatrix4x4;
  proj_ = kRenderIdentityMatrix4x4;
  vp_ = kRenderIdentityMatrix4x4;

  mouseover_guid_ = game::ObjectGuid{};
  mouseover_pick_ = {};
  hover_pick_ = {};
  lmb_down_ = false;
  rmb_down_ = false;

  highlight_guid_ = 0;
  soft_highlight_guid_ = 0;
  cursor_mode_ = CursorMode::kNone;

  viewport_left_ = 0.0f;
  viewport_top_ = 0.0f;
  viewport_right_ = 1.0f;
  viewport_bottom_ = 1.0f;

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "WorldFrame: initialized " +
                                                                       std::to_string(width) + "x" +
                                                                       std::to_string(height));
}

void WorldFrame::Resize(std::uint32_t width, std::uint32_t height) {
  width_ = width;
  height_ = height;
}

void WorldFrame::SetCamera(const RenderMatrix4x4View view_4x4, const RenderMatrix4x4View proj_4x4) {
  std::copy(view_4x4.begin(), view_4x4.end(), view_.begin());
  std::copy(proj_4x4.begin(), proj_4x4.end(), proj_.begin());

  vp_ = MatMul4x4(RenderMatrix4x4View{view_}, RenderMatrix4x4View{proj_});

  frustum_.ExtractFromViewProj(RenderMatrix4x4View{vp_});

  viewport_left_ = 0.0f;
  viewport_top_ = 0.0f;
  viewport_right_ = 1.0f;
  viewport_bottom_ = 1.0f;
}

RenderVec3 WorldFrame::GetCameraPosition() const {
  return ExtractCameraPositionFromRetailViewMatrix(RenderMatrix4x4View{view_});
}

RenderMatrix4x4
WorldFrame::BuildGroundFacingBillboardMatrix(const RenderVec3View world_position) const {
  const auto camera_position = GetCameraPosition();
  RenderMatrix4x4 matrix{kRenderIdentityMatrix4x4};
  matrix = PrependRotationMatrix4x4Z(
      matrix, -openwow::math::ComputeRetailPlanarFacingAngle(
                  openwow::math::PlanarPoint{camera_position[0], camera_position[1]},
                  openwow::math::PlanarPoint{world_position[0], world_position[1]}));
  return matrix;
}

WorldRay WorldFrame::ScreenToWorldRay(int screen_x, int screen_y) const {
  WorldRay ray{};

  if (width_ == 0 || height_ == 0)
    return ray;

  const float normalized_x = static_cast<float>(screen_x) / static_cast<float>(width_);
  const float normalized_y = 1.0f - (static_cast<float>(screen_y) / static_cast<float>(height_));

  const auto frustum_corners = ComputeFrustumCornersFromViewProjection(view_, proj_);
  const auto endpoints =
      ComputeNormalizedViewportRayEndpoints(normalized_x, normalized_y, frustum_corners);
  if (!endpoints) {
    return ray;
  }

  ray.origin_x = endpoints->near_point[0];
  ray.origin_y = endpoints->near_point[1];
  ray.origin_z = endpoints->near_point[2];

  float dx = endpoints->far_point[0] - endpoints->near_point[0];
  float dy = endpoints->far_point[1] - endpoints->near_point[1];
  float dz = endpoints->far_point[2] - endpoints->near_point[2];
  const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (len < 1e-8f)
    return ray;

  ray.dir_x = dx / len;
  ray.dir_y = dy / len;
  ray.dir_z = dz / len;

  return ray;
}

PickResult WorldFrame::Pick(int screen_x, int screen_y) const {
  const WorldRay ray = ScreenToWorldRay(screen_x, screen_y);
  if (std::fabs(ray.dir_x) < 1e-8f && std::fabs(ray.dir_y) < 1e-8f &&
      std::fabs(ray.dir_z) < 1e-8f) {
    return {};
  }

  const PickResult world_geometry = RayTestTerrain(ray);
  const float model_segment_length =
      world_geometry.hit ? world_geometry.distance : kMaxPickDistance;

  PickResult best{};
  best.distance = kMaxPickDistance;

  if (objects_ != nullptr && object_renderer_ != nullptr) {
    const RenderVec3 segment_start{ray.origin_x, ray.origin_y, ray.origin_z};
    const RenderVec3 segment_end{
        ray.origin_x + ray.dir_x * model_segment_length,
        ray.origin_y + ray.dir_y * model_segment_length,
        ray.origin_z + ray.dir_z * model_segment_length,
    };
    const std::uint32_t flags = BuildCursorIntersectionMask(objects_, cursor_context_);
    int best_priority = 0;
    const auto consider = [&](const game::ObjectPresentationRecord &object) {
      if (!PassesModelFilter(object, flags, *objects_)) {
        return;
      }
      const auto hit = object_renderer_->FindClosestSegmentIntersectionForGuid(
          object.handle.guid, segment_start, segment_end);
      if (!hit.has_value()) {
        return;
      }
      const int priority = ModelHitPriority(object, *objects_);

      if (best.hit &&
          (hit->distance > best.distance + kPickDistanceTieEpsilon ||
           (std::fabs(hit->distance - best.distance) <= kPickDistanceTieEpsilon &&
            priority <= best_priority))) {
        return;
      }
      best.hit = true;
      best.guid = object.handle.guid;
      best.distance = hit->distance;
      best.world_x = hit->point[0];
      best.world_y = hit->point[1];
      best.world_z = hit->point[2];
      best.type = ResolveModelPickHitType(object);
      best_priority = priority;
    };

    for (const auto &object : objects_->active) {
      if (!object.unit_click_deferred) {
        consider(object);
      }
    }
    if (!best.hit) {
      for (const auto &object : objects_->active) {
        if (object.unit_click_deferred) {
          consider(object);
        }
      }
    }
  }

  return SelectNearestPick(best, world_geometry);
}

PickResult WorldFrame::RayTestTerrain(const WorldRay &ray) const {
  PickResult result{};
  if (collision_ == nullptr) {
    return result;
  }
  const auto hit = collision_->Raycast(ray.origin_x, ray.origin_y, ray.origin_z, ray.dir_x,
                                       ray.dir_y, ray.dir_z, kMaxPickDistance);
  if (hit.has_value() && hit->distance > 0.0f) {
    result.hit = true;
    result.distance = hit->distance;
    result.world_x = hit->x;
    result.world_y = hit->y;
    result.world_z = hit->z;
    result.type = PickResult::HitType::kTerrain;
  }

  return result;
}

void WorldFrame::OnMouseDown(int button, int x, int y) {
  if (button == 0) {
    lmb_down_ = true;
    click_start_x_ = x;
    click_start_y_ = y;
  } else if (button == 1) {
    rmb_down_ = true;
    click_start_x_ = x;
    click_start_y_ = y;
  }
}

void WorldFrame::OnMouseUp(int button, int x, int y) {
  if (button == 0 && lmb_down_) {
    lmb_down_ = false;

    const int dx = x - click_start_x_;
    const int dy = y - click_start_y_;
    if (dx * dx + dy * dy <= kClickDragThreshold * kClickDragThreshold) {
      HandleLeftClick(x, y);
    }
  } else if (button == 1 && rmb_down_) {
    rmb_down_ = false;
    const int dx = x - click_start_x_;
    const int dy = y - click_start_y_;
    if (dx * dx + dy * dy <= kClickDragThreshold * kClickDragThreshold) {
      HandleRightClick(x, y);
    }
  }
}

void WorldFrame::OnMouseMove(int x, int y) {

  SetCursorPosition(x, y);

  if (rmb_down_ && mouse_delta_cb_) {
    float dx = static_cast<float>(x - click_start_x_);
    float dy = static_cast<float>(y - click_start_y_);
    mouse_delta_cb_(dx, dy);
    click_start_x_ = x;
    click_start_y_ = y;
  }
}

void WorldFrame::OnMouseWheel(float delta) {
  if (scroll_cb_) {
    scroll_cb_(delta);
  }
}

void WorldFrame::HandleLeftClick(int x, int y) {
  if (!target_selection_cb_)
    return;

  const bool has_ground_target_spell =
      cursor_context_.has_active_targeting_spell && cursor_context_.targets_terrain_and_liquid;
  if (has_ground_target_spell) {
    const PickResult result = Pick(x, y);
    const PickResult terrain_hit = RayTestTerrain(ScreenToWorldRay(x, y));
    hover_pick_ = (terrain_hit.hit && !result.guid.IsMoTransport()) ? terrain_hit : result;

    game::targeting::WorldTerrainClick click;
    if (terrain_click_cb_ && TryBuildHoverTerrainClickInput(&click)) {
      terrain_click_cb_(click);
    }
    cursor_mode_ = CursorMode::kNone;
    return;
  }

  if (!nameplate_hover_guid_.IsEmpty()) {
    PickResult plate_pick{};
    plate_pick.hit = true;
    plate_pick.guid = nameplate_hover_guid_;
    plate_pick.type = PickResult::HitType::kUnit;
    ApplyTargetSelection(plate_pick);
    cursor_mode_ = CursorMode::kNone;
    return;
  }

  const PickResult result = Pick(x, y);
  hover_pick_ = result;

  ApplyTargetSelection(result);

  cursor_mode_ = CursorMode::kNone;
}

void WorldFrame::ApplyTargetSelection(const PickResult &pick) {
  const auto selected_guid = (pick.hit && pick.guid) ? pick.guid : game::ObjectGuid{};

  if (target_selection_cb_) {
    target_selection_cb_(selected_guid);
  }

  highlight_guid_ = 0;
  if (selected_guid) {
    highlight_guid_ = selected_guid.GetRawValue();
  }
}

void WorldFrame::HandleRightClick(int x, int y) {

  PickResult result = Pick(x, y);
  if (result.hit && result.guid) {
    if (interact_cb_) {
      interact_cb_(result.guid);
    }
  }
}

bool WorldFrame::TryBuildHoverTerrainClickInput(game::targeting::WorldTerrainClick *out) const {
  return TryBuildTerrainClickInput(hover_pick_, out);
}

bool WorldFrame::TryBuildTerrainClickInput(const PickResult &pick,
                                           game::targeting::WorldTerrainClick *out) const {
  if (out == nullptr || !pick.hit) {
    return false;
  }

  out->reference_object = {};
  out->button = game::targeting::WorldClickButton::kPrimary;
  out->local_position = {
      .x = pick.world_x,
      .y = pick.world_y,
      .z = pick.world_z,
  };

  if (!pick.guid.IsMoTransport() || objects_ == nullptr) {
    return true;
  }

  const auto object = std::lower_bound(
      objects_->active.begin(), objects_->active.end(), pick.guid.GetRawValue(),
      [](const game::ObjectPresentationRecord &record, const std::uint64_t raw_guid) {
        return record.handle.guid.GetRawValue() < raw_guid;
      });
  if (object == objects_->active.end() || object->handle.guid != pick.guid) {
    return true;
  }

  const float dx = pick.world_x - object->x;
  const float dy = pick.world_y - object->y;
  const float orientation = object->facing;
  const float cos_orientation = std::cos(orientation);
  const float sin_orientation = std::sin(orientation);

  out->reference_object = pick.guid;
  out->local_position = {
      .x = dx * cos_orientation + dy * sin_orientation,
      .y = -dx * sin_orientation + dy * cos_orientation,
      .z = pick.world_z - object->z,
  };
  return true;
}

void WorldFrame::UpdateNameplateHover(const std::uint64_t target_guid) {

  if (const auto *const input = ::openwow::game::GetInputControlSingleton();
      input != nullptr &&
      (input->GetControlFlags() & ::openwow::game::kMaskAllMouseModes) != 0) {
    return;
  }

  const auto layout = openwow::ui::NameplateFrameChannel::Get().AcquireLayout();
  if (!layout) {
    nameplate_hover_guid_ = game::ObjectGuid{};
    return;
  }

  const openwow::ui::NameplateScreenPlacement *best = nullptr;
  for (const auto &plate : layout->plates) {

    if (target_guid != 0 && plate.info.guid == target_guid) {
      continue;
    }

    const float half_width = layout->geometry.frame_width * 0.5f;
    const float min_x = plate.screen_x - half_width;
    const float max_x = plate.screen_x + half_width;
    const float min_y = plate.screen_y;
    const float max_y = plate.screen_y + layout->geometry.frame_height;
    if (static_cast<float>(mouse_x_) < min_x || static_cast<float>(mouse_x_) > max_x ||
        static_cast<float>(mouse_y_) < min_y || static_cast<float>(mouse_y_) > max_y) {
      continue;
    }

    if (best == nullptr || plate.view_depth < best->view_depth) {
      best = &plate;
    }
  }

  nameplate_hover_guid_ =
      best != nullptr ? game::ObjectGuid{best->info.guid} : game::ObjectGuid{};
}

void WorldFrame::Update(float ) {
  PickResult model_pick = Pick(mouse_x_, mouse_y_);
  PickResult hover_pick = model_pick;
  const bool uses_terrain_and_liquid =
      cursor_context_.has_active_targeting_spell && cursor_context_.targets_terrain_and_liquid;
  if (uses_terrain_and_liquid) {
    const PickResult terrain_hit = RayTestTerrain(ScreenToWorldRay(mouse_x_, mouse_y_));
    if (terrain_hit.hit && !model_pick.guid.IsMoTransport()) {
      hover_pick = terrain_hit;
    }
  }
  hover_pick_ = hover_pick;

  game::ObjectGuid new_mouseover{};

  const bool suppress_mouseover_for_ground_only_spell =
      uses_terrain_and_liquid && !cursor_context_.has_spell_target_mask;

  if (!suppress_mouseover_for_ground_only_spell && !nameplate_hover_guid_.IsEmpty()) {
    mouseover_pick_ = PickResult{};
    SetMouseoverGuid(nameplate_hover_guid_);
    return;
  }

  if (!suppress_mouseover_for_ground_only_spell && model_pick.hit &&
      (model_pick.type == PickResult::HitType::kUnit ||
       model_pick.type == PickResult::HitType::kGameObject ||
       model_pick.type == PickResult::HitType::kCorpse)) {
    new_mouseover = model_pick.guid;
  }

  mouseover_pick_ = new_mouseover.IsEmpty() ? PickResult{} : model_pick;
  SetMouseoverGuid(new_mouseover);
}

bool WorldFrame::IsInFrustum(float cx, float cy, float cz, float radius) const {
  return frustum_.TestSphere(cx, cy, cz, radius);
}

RenderMatrix4x4 WorldFrame::MatMul4x4(const RenderMatrix4x4View a, const RenderMatrix4x4View b) {
  return MultiplyMatrix4x4(a, b);
}

RenderScreenProjection WorldFrame::WorldToScreen(const RenderVec3View world_pos) const {
  float viewport_width = 0.0f;
  float viewport_height = 0.0f;
  openwow::ui::ApplyCachedUiStretch(viewport_right_ - viewport_left_,
                                    viewport_bottom_ - viewport_top_, &viewport_width,
                                    &viewport_height);
  return ProjectWorldPointToViewport(world_pos, RenderMatrix4x4View{view_},
                                     RenderMatrix4x4View{proj_},
                                     RenderProjectionViewport{
                                         .right = viewport_width,
                                         .bottom = viewport_height,
                                     });
}

bool WorldFrame::SetMouseoverGuid(game::ObjectGuid guid) {
  if (guid == mouseover_guid_)
    return false;
  mouseover_guid_ = guid;
  if (guid.IsEmpty()) {
    mouseover_pick_ = {};
  }
  return true;
}

bool WorldFrame::SetExplicitMouseoverGuid(game::ObjectGuid guid) {
  const bool changed = guid != mouseover_guid_;
  mouseover_guid_ = guid;
  mouseover_pick_ = {};
  hover_pick_ = {};
  return changed;
}

void WorldFrame::ClearSelectionHighlights() {
  highlight_guid_ = 0;
  soft_highlight_guid_ = 0;
}

RenderVec3 WorldFrame::GetFacingDirection(const float yaw) {
  return RenderVec3{std::cos(yaw), std::sin(yaw), 0.0f};
}

}

#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/animation/path_anim.h"
#include "openwow/ui/animation/animation_coordinate_space.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace openwow::ui::anim {

namespace {

constexpr float kOffsetEpsilon = 0.00000023841858f;

struct StoredPathPoint {
  float x{0.0f};
  float y{0.0f};
  float adjusted_curve{0.0f};
};

StoredPathPoint ReadStoredPathPoint(const PathControlPoint& point) {
  return {
      .x = point.GetOffsetX(),
      .y = point.GetOffsetY(),
      .adjusted_curve = point.GetAdjustedCurve(),
  };
}

float EvaluateCatmullRomAxis(const float p0,
                             const float p1,
                             const float p2,
                             const float p3,
                             const float t) {
  const float t_squared = t * t;
  const float t_cubed = t_squared * t;
  return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                 (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t_squared +
                 (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t_cubed);
}

StoredPathPoint EvaluateCatmullRomPoint(const StoredPathPoint& p0,
                                        const StoredPathPoint& p1,
                                        const StoredPathPoint& p2,
                                        const StoredPathPoint& p3,
                                        const float t) {
  return {
      .x = EvaluateCatmullRomAxis(p0.x, p1.x, p2.x, p3.x, t),
      .y = EvaluateCatmullRomAxis(p0.y, p1.y, p2.y, p3.y, t),
      .adjusted_curve = 0.0f,
  };
}

void ResetLuaRegistryRef(lua_State** state, int* ref) {
  if (state == nullptr || ref == nullptr) {
    return;
  }

  if (*state != nullptr && *ref != LUA_NOREF) {
    luaL_unref(*state, LUA_REGISTRYINDEX, *ref);
  }

  *state = nullptr;
  *ref = LUA_NOREF;
}

void RemoveTableFromArrayField(lua_State* L, int owner_idx, const char* field, int value_idx) {
  owner_idx = lua_absindex(L, owner_idx);
  value_idx = lua_absindex(L, value_idx);

  lua_getfield(L, owner_idx, field);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }

  const int array_idx = lua_absindex(L, -1);
  const lua_Integer len = luaL_len(L, array_idx);
  lua_Integer remove_at = 0;
  for (lua_Integer index = 1; index <= len; ++index) {
    lua_geti(L, array_idx, index);
    const bool matches = lua_rawequal(L, -1, value_idx) != 0;
    lua_pop(L, 1);
    if (matches) {
      remove_at = index;
      break;
    }
  }

  if (remove_at != 0) {
    for (lua_Integer index = remove_at; index < len; ++index) {
      lua_geti(L, array_idx, index + 1);
      lua_seti(L, array_idx, index);
    }
    lua_pushnil(L);
    lua_seti(L, array_idx, len);
  }

  lua_pop(L, 1);
}

void InvalidateControlPointTable(lua_State* L, int table_idx) {
  table_idx = lua_absindex(L, table_idx);

  lua_getfield(L, table_idx, "__ow_path");
  if (lua_istable(L, -1)) {
    RemoveTableFromArrayField(L, -1, "__ow_control_points", table_idx);
  }
  lua_pop(L, 1);

  lua_pushnil(L);
  lua_setfield(L, table_idx, "__ow_control_point_ptr");
  lua_pushnil(L);
  lua_setfield(L, table_idx, "__ow_path");
}

}

PathControlPoint::PathControlPoint(PathAnim* parent)
    : parent_(parent) {}

PathControlPoint::~PathControlPoint() {
  if (object_lua_state_ != nullptr && object_lua_ref_ != LUA_NOREF) {
    lua_rawgeti(object_lua_state_, LUA_REGISTRYINDEX, object_lua_ref_);
    if (lua_istable(object_lua_state_, -1)) {
      InvalidateControlPointTable(object_lua_state_, -1);
    }
    lua_pop(object_lua_state_, 1);
  }

  ResetLuaRegistryRef(&object_lua_state_, &object_lua_ref_);
  parent_ = nullptr;
  name_.clear();
}

void PathControlPoint::SetOffset(float x, float y) {
  if (x * x + y * y <= kOffsetEpsilon) {
    offset_x_ = 0.0f;
    offset_y_ = 0.0f;
    return;
  }
  offset_x_ = x;
  offset_y_ = y;
}

void PathControlPoint::SetOrder(int order, bool notify_parent, bool validate) {
  const int old_order = order_;
  if (validate) {
    order_ = (order >= 0 && order <= 99) ? order : 0;
  } else {
    order_ = std::clamp(order, 0, 99);
  }

  if (parent_ && notify_parent && old_order != order_) {
    parent_->OnControlPointOrderChanged(*this);
  }
}

void PathControlPoint::SetLuaObjectRef(lua_State* L, int lua_ref) {
  ResetLuaRegistryRef(&object_lua_state_, &object_lua_ref_);
  object_lua_state_ = L;
  object_lua_ref_ = lua_ref;
}

PathControlPoint* PathAnim::CreateControlPoint(const std::string& name) {
  auto point = std::make_unique<PathControlPoint>(this);
  point->SetName(name);
  auto* raw = point.get();
  control_points_.push_back(std::move(point));
  return raw;
}

PathControlPoint* PathAnim::AddControlPoint(const PathControlPointData& cp) {
  auto* point = CreateControlPoint();
  point->SetOffset(cp.offset_x, cp.offset_y);
  point->SetAdjustedCurve(cp.adjusted_curve);
  if (cp.order >= 0) {
    point->SetOrder(cp.order, false, false);
  }
  OnControlPointOrderChanged(*point);
  return point;
}

std::unique_ptr<PathControlPoint> PathAnim::ExtractOwnedControlPoint(PathControlPoint& point) {
  auto it = std::find_if(control_points_.begin(), control_points_.end(),
                         [&point](const auto& owned) { return owned.get() == &point; });
  if (it == control_points_.end()) {
    return nullptr;
  }

  auto extracted = std::move(*it);
  control_points_.erase(it);
  RebuildControlPointCache();
  return extracted;
}

void PathAnim::AdoptControlPoint(std::unique_ptr<PathControlPoint> point) {
  if (!point) {
    return;
  }

  point->SetParentRaw(this);
  control_points_.push_back(std::move(point));
}

int PathAnim::FindRepresentativeIndexByOrder(int order) const {
  if (order == -1) {
    return -1;
  }

  int low = 0;
  int high = static_cast<int>(ordered_control_points_.size()) - 1;

  while (low <= high) {
    const int mid = static_cast<int>(static_cast<unsigned int>(low + high) >> 1);
    const int point_order = ordered_control_points_[static_cast<size_t>(mid)]->GetOrder();

    if (point_order < order) {
      low = mid + 1;
    } else if (point_order > order) {
      high = mid - 1;
    } else {
      return mid;
    }
  }

  return -1;
}

void PathAnim::MoveBeforeRepresentative(PathControlPoint& point) {
  size_t point_index = control_points_.size();
  size_t rep_index = control_points_.size();
  for (size_t index = 0; index < control_points_.size(); ++index) {
    auto* current = control_points_[index].get();
    if (current == &point) {
      point_index = index;
      continue;
    }
    if (rep_index == control_points_.size() && current->GetOrder() == point.GetOrder()) {
      rep_index = index;
    }
  }

  if (point_index == control_points_.size() || rep_index == control_points_.size()) {
    return;
  }

  auto moved = std::move(control_points_[point_index]);
  control_points_.erase(control_points_.begin() + static_cast<std::ptrdiff_t>(point_index));
  if (point_index < rep_index) {
    --rep_index;
  }
  control_points_.insert(control_points_.begin() + static_cast<std::ptrdiff_t>(rep_index),
                         std::move(moved));
}

void PathAnim::InsertRepresentative(PathControlPoint& point) {
  auto it = std::find_if(ordered_control_points_.begin(), ordered_control_points_.end(),
                         [&point](const PathControlPoint* existing) {
                           return existing->GetOrder() >= point.GetOrder();
                         });
  if (it == ordered_control_points_.end()) {
    ordered_control_points_.push_back(&point);
    return;
  }
  if ((*it)->GetOrder() != point.GetOrder()) {
    ordered_control_points_.insert(it, &point);
  }
}

void PathAnim::RebuildControlPointCache() {
  Stop(false);
  ResetEffect();
  ordered_control_points_.clear();
  if (control_points_.empty()) {
    return;
  }

  int next_order = 0;
  for (const auto& owned : control_points_) {
    auto* point = owned.get();
    int order = point->GetOrder();
    if (order == -1 || order < next_order) {
      order = std::clamp(next_order, 0, kMaxControlPointOrder);
      point->SetOrder(order, false, false);
      ++next_order;
    } else {
      next_order = order + 1;
    }

    if (next_order > kMaxControlPointOrder) {
      next_order = 0;
    }

    InsertRepresentative(*point);
  }

  const float inv_count = 1.0f / static_cast<float>(ordered_control_points_.size());
  for (size_t index = 0; index < ordered_control_points_.size(); ++index) {
    float adjusted_curve = static_cast<float>(index + 1) * inv_count;
    adjusted_curve = std::clamp(adjusted_curve, 0.0f, 1.0f);
    ordered_control_points_[index]->SetAdjustedCurve(adjusted_curve);
  }
}

bool PathAnim::ReparentControlPoint(PathControlPoint& point, PathAnim* new_parent) {
  if (!new_parent || point.GetParent() == new_parent) {
    return false;
  }

  PathAnim* old_parent = point.GetParent();
  std::unique_ptr<PathControlPoint> owned;
  if (old_parent) {
    owned = old_parent->ExtractOwnedControlPoint(point);
  }
  if (!owned) {
    return false;
  }

  new_parent->AdoptControlPoint(std::move(owned));
  new_parent->MoveBeforeRepresentative(point);
  new_parent->RebuildControlPointCache();
  return true;
}

void PathAnim::OnControlPointOrderChanged(PathControlPoint& point) {
  MoveBeforeRepresentative(point);
  RebuildControlPointCache();
}

PathControlPoint* PathAnim::GetControlPoint(size_t idx) {
  return idx < ordered_control_points_.size() ? ordered_control_points_[idx] : nullptr;
}

const PathControlPoint* PathAnim::GetControlPoint(size_t idx) const {
  return idx < ordered_control_points_.size() ? ordered_control_points_[idx] : nullptr;
}

int PathAnim::GetMaxOrder() const {
  if (ordered_control_points_.empty()) {
    return -1;
  }
  return ordered_control_points_.back()->GetOrder();
}

void PathAnim::ClearControlPoints() {
  Stop(false);
  ordered_control_points_.clear();
  control_points_.clear();
}

void PathAnim::Apply(float progress) {
  ApplySignedFactor(progress);
}

void PathAnim::ApplySignedFactor(const float factor) {
  const float progress = std::fabs(factor);

  if (ordered_control_points_.empty()) {
    current_x_ = 0.0f;
    current_y_ = 0.0f;
    return;
  }

  const size_t point_count = ordered_control_points_.size();
  const size_t segment_index =
      progress >= 1.0f ? point_count
                       : static_cast<size_t>(progress * static_cast<float>(point_count));

  StoredPathPoint stored_sample{};
  if (segment_index >= point_count) {
    stored_sample = ReadStoredPathPoint(*ordered_control_points_.back());
  } else if (curve_ != 0 && point_count > 1) {
    if (segment_index == 0) {
      const auto first = ReadStoredPathPoint(*ordered_control_points_.front());
      const auto second = ReadStoredPathPoint(*ordered_control_points_[1]);
      const StoredPathPoint reflected_start{-first.x, -first.y, 0.0f};
      const StoredPathPoint origin{};
      const float local_t = progress / first.adjusted_curve;
      stored_sample =
          EvaluateCatmullRomPoint(reflected_start, origin, first, second, local_t);
    } else {
      const auto previous = ReadStoredPathPoint(*ordered_control_points_[segment_index - 1]);
      const auto current = ReadStoredPathPoint(*ordered_control_points_[segment_index]);
      const StoredPathPoint before_previous =
          segment_index == 1
              ? StoredPathPoint{}
              : ReadStoredPathPoint(*ordered_control_points_[segment_index - 2]);
      const StoredPathPoint next =
          segment_index + 1 < point_count
              ? ReadStoredPathPoint(*ordered_control_points_[segment_index + 1])
              : StoredPathPoint{
                    current.x * 2.0f - previous.x,
                    current.y * 2.0f - previous.y,
                    0.0f,
                };
      const float local_t =
          (progress - previous.adjusted_curve) /
          (current.adjusted_curve - previous.adjusted_curve);
      stored_sample =
          EvaluateCatmullRomPoint(before_previous, previous, current, next, local_t);
    }
  } else if (segment_index == 0) {
    const auto first = ReadStoredPathPoint(*ordered_control_points_.front());
    const float local_t = progress / first.adjusted_curve;
    stored_sample = {
        .x = first.x * local_t,
        .y = first.y * local_t,
        .adjusted_curve = 0.0f,
    };
  } else {
    const auto previous = ReadStoredPathPoint(*ordered_control_points_[segment_index - 1]);
    const auto current = ReadStoredPathPoint(*ordered_control_points_[segment_index]);
    const float local_t =
        (progress - previous.adjusted_curve) /
        (current.adjusted_curve - previous.adjusted_curve);
    stored_sample = {
        .x = previous.x + (current.x - previous.x) * local_t,
        .y = previous.y + (current.y - previous.y) * local_t,
        .adjusted_curve = 0.0f,
    };
  }

  if (factor < 0.0f) {
    stored_sample.x = -stored_sample.x;
    stored_sample.y = -stored_sample.y;
  }

  current_x_ = StoredAnimationOffsetToPixels(stored_sample.x);
  current_y_ = StoredAnimationOffsetToPixels(stored_sample.y);
}

void PathAnim::ResetEffect() {
  current_x_ = 0.0f;
  current_y_ = 0.0f;
}

void PathAnim::FinalizeXmlLoad() {
  RebuildControlPointCache();
  Animation::FinalizeXmlLoad();
}

}

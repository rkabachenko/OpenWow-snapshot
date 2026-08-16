#include "openwow/debug/inspection/render_debug.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace openwow::debug {

RenderDebug& RenderDebug::Get() {
  static RenderDebug instance;
  return instance;
}

void RenderDebug::DrawBBox(const Vec3& min, const Vec3& max,
                           const DebugColor& color) {
  if (!IsEnabled() || !std::isfinite(min.x) || !std::isfinite(min.y) ||
      !std::isfinite(min.z) || !std::isfinite(max.x) ||
      !std::isfinite(max.y) || !std::isfinite(max.z) ||
      !std::isfinite(color.r) || !std::isfinite(color.g) ||
      !std::isfinite(color.b) || !std::isfinite(color.a)) return;
  std::lock_guard lock(mutex_);
  if (commands_.size() >= kMaxCommands) return;
  DebugDrawCommand cmd;
  cmd.type = DebugDrawType::BBox;
  cmd.p0 = {std::min(min.x, max.x), std::min(min.y, max.y),
            std::min(min.z, max.z)};
  cmd.p1 = {std::max(min.x, max.x), std::max(min.y, max.y),
            std::max(min.z, max.z)};
  cmd.color = {std::clamp(color.r, 0.0f, 1.0f),
               std::clamp(color.g, 0.0f, 1.0f),
               std::clamp(color.b, 0.0f, 1.0f),
               std::clamp(color.a, 0.0f, 1.0f)};
  commands_.push_back(cmd);
}

void RenderDebug::DrawSphere(const Vec3& center, float radius,
                             const DebugColor& color) {
  if (!IsEnabled() || !std::isfinite(center.x) ||
      !std::isfinite(center.y) || !std::isfinite(center.z) ||
      !std::isfinite(radius) || radius <= 0.0f ||
      !std::isfinite(color.r) || !std::isfinite(color.g) ||
      !std::isfinite(color.b) || !std::isfinite(color.a)) return;
  std::lock_guard lock(mutex_);
  if (commands_.size() >= kMaxCommands) return;
  DebugDrawCommand cmd;
  cmd.type = DebugDrawType::Sphere;
  cmd.p0 = center;
  cmd.radius = radius;
  cmd.color = {std::clamp(color.r, 0.0f, 1.0f),
               std::clamp(color.g, 0.0f, 1.0f),
               std::clamp(color.b, 0.0f, 1.0f),
               std::clamp(color.a, 0.0f, 1.0f)};
  commands_.push_back(cmd);
}

void RenderDebug::DrawLine3D(const Vec3& from, const Vec3& to,
                             const DebugColor& color, float thickness) {
  if (!IsEnabled() || !std::isfinite(from.x) || !std::isfinite(from.y) ||
      !std::isfinite(from.z) || !std::isfinite(to.x) ||
      !std::isfinite(to.y) || !std::isfinite(to.z) ||
      !std::isfinite(thickness) || thickness <= 0.0f ||
      !std::isfinite(color.r) || !std::isfinite(color.g) ||
      !std::isfinite(color.b) || !std::isfinite(color.a)) return;
  std::lock_guard lock(mutex_);
  if (commands_.size() >= kMaxCommands) return;
  DebugDrawCommand cmd;
  cmd.type = DebugDrawType::Line3D;
  cmd.p0 = from;
  cmd.p1 = to;
  cmd.color = {std::clamp(color.r, 0.0f, 1.0f),
               std::clamp(color.g, 0.0f, 1.0f),
               std::clamp(color.b, 0.0f, 1.0f),
               std::clamp(color.a, 0.0f, 1.0f)};
  cmd.size = thickness;
  commands_.push_back(cmd);
}

void RenderDebug::DrawCircle3D(const Vec3& center, float radius,
                               const Vec3& normal,
                               const DebugColor& color) {
  const float normal_length_squared = normal.x * normal.x +
                                      normal.y * normal.y +
                                      normal.z * normal.z;
  if (!IsEnabled() || !std::isfinite(center.x) ||
      !std::isfinite(center.y) || !std::isfinite(center.z) ||
      !std::isfinite(radius) || radius <= 0.0f ||
      !std::isfinite(normal_length_squared) ||
      normal_length_squared <= 1.0e-12f || !std::isfinite(color.r) ||
      !std::isfinite(color.g) || !std::isfinite(color.b) ||
      !std::isfinite(color.a)) return;
  std::lock_guard lock(mutex_);
  if (commands_.size() >= kMaxCommands) return;
  DebugDrawCommand cmd;
  cmd.type = DebugDrawType::Circle3D;
  cmd.p0 = center;
  cmd.radius = radius;
  const float inverse_normal_length = 1.0f / std::sqrt(normal_length_squared);
  cmd.normal = {normal.x * inverse_normal_length,
                normal.y * inverse_normal_length,
                normal.z * inverse_normal_length};
  cmd.color = {std::clamp(color.r, 0.0f, 1.0f),
               std::clamp(color.g, 0.0f, 1.0f),
               std::clamp(color.b, 0.0f, 1.0f),
               std::clamp(color.a, 0.0f, 1.0f)};
  commands_.push_back(cmd);
}

void RenderDebug::DrawText3D(const Vec3& position, const std::string& text,
                             const DebugColor& color, float size) {
  if (!IsEnabled() || !std::isfinite(position.x) ||
      !std::isfinite(position.y) || !std::isfinite(position.z) ||
      !std::isfinite(size) || size <= 0.0f || text.empty() ||
      !std::isfinite(color.r) || !std::isfinite(color.g) ||
      !std::isfinite(color.b) || !std::isfinite(color.a)) return;
  std::lock_guard lock(mutex_);
  if (commands_.size() >= kMaxCommands) return;
  DebugDrawCommand cmd;
  cmd.type = DebugDrawType::Text3D;
  cmd.p0 = position;
  cmd.text = text.substr(0, 1024);
  cmd.color = {std::clamp(color.r, 0.0f, 1.0f),
               std::clamp(color.g, 0.0f, 1.0f),
               std::clamp(color.b, 0.0f, 1.0f),
               std::clamp(color.a, 0.0f, 1.0f)};
  cmd.size = size;
  commands_.push_back(cmd);
}

void RenderDebug::DrawGrid(const Vec3& origin, float size, float spacing,
                           const DebugColor& color) {
  if (!IsEnabled() || !std::isfinite(origin.x) ||
      !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
      !std::isfinite(size) || size <= 0.0f || !std::isfinite(spacing) ||
      spacing <= 0.0f || !std::isfinite(color.r) ||
      !std::isfinite(color.g) || !std::isfinite(color.b) ||
      !std::isfinite(color.a)) return;
  std::lock_guard lock(mutex_);
  if (commands_.size() >= kMaxCommands) return;
  DebugDrawCommand cmd;
  cmd.type = DebugDrawType::Grid;
  cmd.p0 = origin;
  cmd.size = size;
  cmd.spacing = std::max(spacing, size / 512.0f);
  cmd.color = {std::clamp(color.r, 0.0f, 1.0f),
               std::clamp(color.g, 0.0f, 1.0f),
               std::clamp(color.b, 0.0f, 1.0f),
               std::clamp(color.a, 0.0f, 1.0f)};
  commands_.push_back(cmd);
}

void RenderDebug::DrawFrustum(const Mat4& view_proj,
                               const DebugColor& color) {
  if (!IsEnabled() || !std::all_of(std::begin(view_proj.m),
                                   std::end(view_proj.m),
                                   [](float value) {
                                     return std::isfinite(value);
                                   }) || !std::isfinite(color.r) ||
      !std::isfinite(color.g) || !std::isfinite(color.b) ||
      !std::isfinite(color.a)) return;
  float matrix[4][4];
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      matrix[row][column] = view_proj.m[row * 4 + column];
    }
  }
  for (std::size_t column = 0; column < 4; ++column) {
    std::size_t pivot = column;
    for (std::size_t row = column + 1; row < 4; ++row) {
      if (std::abs(matrix[row][column]) >
          std::abs(matrix[pivot][column])) {
        pivot = row;
      }
    }
    if (std::abs(matrix[pivot][column]) <= 1.0e-8f) return;
    if (pivot != column) {
      for (std::size_t entry = column; entry < 4; ++entry) {
        std::swap(matrix[column][entry], matrix[pivot][entry]);
      }
    }
    for (std::size_t row = column + 1; row < 4; ++row) {
      const float scale = matrix[row][column] / matrix[column][column];
      for (std::size_t entry = column + 1; entry < 4; ++entry) {
        matrix[row][entry] -= scale * matrix[column][entry];
      }
    }
  }
  std::lock_guard lock(mutex_);
  if (commands_.size() >= kMaxCommands) return;
  DebugDrawCommand cmd;
  cmd.type = DebugDrawType::Frustum;
  cmd.view_proj = view_proj;
  cmd.color = {std::clamp(color.r, 0.0f, 1.0f),
               std::clamp(color.g, 0.0f, 1.0f),
               std::clamp(color.b, 0.0f, 1.0f),
               std::clamp(color.a, 0.0f, 1.0f)};
  commands_.push_back(cmd);
}

std::uint32_t RenderDebug::GetDebugDrawCallCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(commands_.size());
}

void RenderDebug::Clear() {
  std::lock_guard lock(mutex_);
  if (!snapshot_pending_) {
    commands_.clear();
  } else {
    const std::size_t count = std::min(snapshot_size_, commands_.size());
    commands_.erase(commands_.begin(), commands_.begin() + count);
  }
  snapshot_size_ = 0;
  snapshot_pending_ = false;
}

void RenderDebug::Flush() {
  Clear();
}

std::vector<DebugDrawCommand> RenderDebug::GetCommands() const {
  std::lock_guard lock(mutex_);
  snapshot_size_ = commands_.size();
  snapshot_pending_ = true;
  return commands_;
}

void RenderDebug::Reset() {
  std::lock_guard lock(mutex_);
  commands_.clear();
  snapshot_size_ = 0;
  snapshot_pending_ = false;
  enabled_.store(true, std::memory_order_relaxed);
  show_bboxes_.store(false, std::memory_order_relaxed);
  show_collision_.store(false, std::memory_order_relaxed);
  show_navmesh_.store(false, std::memory_order_relaxed);
  show_wireframe_.store(false, std::memory_order_relaxed);
  wmo_debug_geometry_mode_.store(render::WmoDebugGeometryMode::Collision,
                                 std::memory_order_relaxed);
}

}

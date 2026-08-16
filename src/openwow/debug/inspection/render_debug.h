#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "openwow/render/world/wmo/wmo_mesh.h"

namespace openwow::debug {

struct DebugColor {
  float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

  static DebugColor White()  { return {1, 1, 1, 1}; }
  static DebugColor Red()    { return {1, 0, 0, 1}; }
  static DebugColor Green()  { return {0, 1, 0, 1}; }
  static DebugColor Blue()   { return {0, 0, 1, 1}; }
  static DebugColor Yellow() { return {1, 1, 0, 1}; }

  bool operator==(const DebugColor& o) const = default;
};

struct Vec3 {
  float x = 0, y = 0, z = 0;
  bool operator==(const Vec3& o) const = default;
};

struct Mat4 {
  float m[16] = {};
  bool operator==(const Mat4& o) const = default;
};

enum class DebugDrawType : std::uint8_t {
  BBox,
  Sphere,
  Line3D,
  Circle3D,
  Text3D,
  Grid,
  Frustum,
};

struct DebugDrawCommand {
  DebugDrawType type;
  Vec3 p0, p1;
  Vec3 normal;
  float radius  = 0.0f;
  float size    = 0.0f;
  float spacing = 0.0f;
  DebugColor color;
  std::string text;
  Mat4 view_proj;
};

class RenderDebug {
 public:
  static RenderDebug& Get();

  void DrawBBox(const Vec3& min, const Vec3& max, const DebugColor& color);
  void DrawSphere(const Vec3& center, float radius, const DebugColor& color);
  void DrawLine3D(const Vec3& from, const Vec3& to, const DebugColor& color,
                  float thickness = 1.0f);
  void DrawCircle3D(const Vec3& center, float radius, const Vec3& normal,
                    const DebugColor& color);
  void DrawText3D(const Vec3& position, const std::string& text,
                  const DebugColor& color, float size = 12.0f);
  void DrawGrid(const Vec3& origin, float size, float spacing,
                const DebugColor& color);
  void DrawFrustum(const Mat4& view_proj, const DebugColor& color);

  void SetEnabled(bool e) { enabled_.store(e, std::memory_order_relaxed); }
  [[nodiscard]] bool IsEnabled() const {
    return enabled_.load(std::memory_order_relaxed);
  }

  void ShowBoundingBoxes(bool v) {
    show_bboxes_.store(v, std::memory_order_relaxed);
  }
  [[nodiscard]] bool AreBoundingBoxesShown() const {
    return show_bboxes_.load(std::memory_order_relaxed);
  }

  void ShowCollision(bool v) {
    show_collision_.store(v, std::memory_order_relaxed);
  }
  [[nodiscard]] bool IsCollisionShown() const {
    return show_collision_.load(std::memory_order_relaxed);
  }

  void SetWmoDebugGeometryMode(render::WmoDebugGeometryMode mode) {
    wmo_debug_geometry_mode_.store(mode, std::memory_order_relaxed);
  }
  [[nodiscard]] render::WmoDebugGeometryMode GetWmoDebugGeometryMode() const {
    return wmo_debug_geometry_mode_.load(std::memory_order_relaxed);
  }

  void ShowNavMesh(bool v) {
    show_navmesh_.store(v, std::memory_order_relaxed);
  }
  [[nodiscard]] bool IsNavMeshShown() const {
    return show_navmesh_.load(std::memory_order_relaxed);
  }

  void ShowWireframe(bool v) {
    show_wireframe_.store(v, std::memory_order_relaxed);
  }
  [[nodiscard]] bool IsWireframeShown() const {
    return show_wireframe_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint32_t GetDebugDrawCallCount() const;

  void Clear();
  void Flush();

  [[nodiscard]] std::vector<DebugDrawCommand> GetCommands() const;

  void Reset();

 private:
  RenderDebug() = default;

  mutable std::mutex mutex_;
  std::vector<DebugDrawCommand> commands_;
  mutable std::size_t snapshot_size_ = 0;
  mutable bool snapshot_pending_ = false;
  static constexpr std::size_t kMaxCommands = 4096;
  std::atomic_bool enabled_{true};
  std::atomic_bool show_bboxes_{false};
  std::atomic_bool show_collision_{false};
  std::atomic_bool show_navmesh_{false};
  std::atomic_bool show_wireframe_{false};
  std::atomic<render::WmoDebugGeometryMode> wmo_debug_geometry_mode_{
      render::WmoDebugGeometryMode::Collision};
};

}

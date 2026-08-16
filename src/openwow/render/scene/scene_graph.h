#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "openwow/render/api/math/mat4.h"

namespace openwow::render {

enum class RenderableType : uint8_t {
  Terrain,
  WMO,
  M2,
  Particle,
  Water,
  Sky,
  UI,
  Debug,
  Unknown
};

struct AABB {
  float min_x{0.0f}, min_y{0.0f}, min_z{0.0f};
  float max_x{0.0f}, max_y{0.0f}, max_z{0.0f};

  [[nodiscard]] bool IsValid() const {
    return max_x >= min_x && max_y >= min_y && max_z >= min_z;
  }

  void Merge(const AABB& other) {
    if (other.min_x < min_x) min_x = other.min_x;
    if (other.min_y < min_y) min_y = other.min_y;
    if (other.min_z < min_z) min_z = other.min_z;
    if (other.max_x > max_x) max_x = other.max_x;
    if (other.max_y > max_y) max_y = other.max_y;
    if (other.max_z > max_z) max_z = other.max_z;
  }
};

struct Quat {
  float x{0.0f}, y{0.0f}, z{0.0f}, w{1.0f};
};

struct SceneNode {
  uint32_t id{0};

  float pos_x{0.0f}, pos_y{0.0f}, pos_z{0.0f};
  Quat rotation{};
  float scale_x{1.0f}, scale_y{1.0f}, scale_z{1.0f};

  std::unique_ptr<Mat4> local_affine_transform;

  uint32_t parent_id{0};
  std::vector<uint32_t> children;

  bool visible{true};
  AABB bounding_box{};
  RenderableType render_type{RenderableType::Unknown};

  mutable Mat4 world_transform{};
  mutable bool world_dirty{true};
};

class SceneGraph {
 public:
  SceneGraph() = default;
  ~SceneGraph() = default;

  SceneGraph(const SceneGraph&) = delete;
  SceneGraph& operator=(const SceneGraph&) = delete;

  uint32_t CreateNode(RenderableType type, float x, float y, float z);

  void DestroyNode(uint32_t node_id);

  void SetPosition(uint32_t node_id, float x, float y, float z);

  void SetRotation(uint32_t node_id, const Quat& q);

  void SetScale(uint32_t node_id, float sx, float sy, float sz);

  void SetLocalAffineTransform(uint32_t node_id, const Mat4& transform);

  void SetParent(uint32_t node_id, uint32_t parent_id);

  void SetVisible(uint32_t node_id, bool vis);

  [[nodiscard]] bool IsVisible(uint32_t node_id) const;

  void SetBoundingBox(uint32_t node_id, const AABB& box);

  struct Vec3 {
    float x, y, z;
  };
  [[nodiscard]] Vec3 GetWorldPosition(uint32_t node_id) const;

  [[nodiscard]] Mat4 GetWorldTransform(uint32_t node_id) const;

  [[nodiscard]] uint32_t GetNodeCount() const;

  [[nodiscard]] std::vector<uint32_t> GetVisibleNodes() const;

  [[nodiscard]] std::vector<uint32_t> FrustumCull(
      const float (*frustum_planes)[4]) const;

  [[nodiscard]] std::vector<uint32_t> GetNodesInRadius(float cx, float cy,
                                                        float cz,
                                                        float radius) const;

  [[nodiscard]] std::vector<uint32_t> GetNodesByType(
      RenderableType type) const;

  void ForEachVisible(
      const std::function<void(uint32_t, const SceneNode&)>& callback) const;

  void Clear();

  [[nodiscard]] const SceneNode* GetNode(uint32_t node_id) const;

 private:

  void InvalidateWorldTransform(uint32_t node_id);

  void UpdateWorldTransform(const SceneNode& node) const;

  static bool TestAABBFrustum(const AABB& box, const Vec3& world_pos,
                               const float (*planes)[4]);

  uint32_t next_id_{1};
  std::unordered_map<uint32_t, SceneNode> nodes_;
};

}

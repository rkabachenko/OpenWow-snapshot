
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/scene/scene_graph.h"

#include <algorithm>
#include <cmath>

namespace openwow::render {

uint32_t SceneGraph::CreateNode(RenderableType type, float x, float y,
                                float z) {
  uint32_t id = next_id_++;
  SceneNode& node = nodes_[id];
  node.id = id;
  node.pos_x = x;
  node.pos_y = y;
  node.pos_z = z;
  node.render_type = type;
  node.world_dirty = true;
  return id;
}

void SceneGraph::DestroyNode(uint32_t node_id) {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return;

  SceneNode& node = it->second;

  if (node.parent_id != 0) {
    auto pit = nodes_.find(node.parent_id);
    if (pit != nodes_.end()) {
      auto& pc = pit->second.children;
      pc.erase(std::remove(pc.begin(), pc.end(), node_id), pc.end());
    }
  }

  for (uint32_t child_id : node.children) {
    auto cit = nodes_.find(child_id);
    if (cit != nodes_.end()) {
      cit->second.parent_id = 0;
      cit->second.world_dirty = true;
    }
  }

  nodes_.erase(it);
}

void SceneGraph::SetPosition(uint32_t node_id, float x, float y, float z) {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return;
  it->second.local_affine_transform.reset();
  it->second.pos_x = x;
  it->second.pos_y = y;
  it->second.pos_z = z;
  InvalidateWorldTransform(node_id);
}

void SceneGraph::SetRotation(uint32_t node_id, const Quat& q) {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return;
  it->second.local_affine_transform.reset();
  it->second.rotation = q;
  InvalidateWorldTransform(node_id);
}

void SceneGraph::SetScale(uint32_t node_id, float sx, float sy, float sz) {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return;
  it->second.local_affine_transform.reset();
  it->second.scale_x = sx;
  it->second.scale_y = sy;
  it->second.scale_z = sz;
  InvalidateWorldTransform(node_id);
}

void SceneGraph::SetLocalAffineTransform(uint32_t node_id, const Mat4& transform) {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return;
  if (it->second.local_affine_transform == nullptr) {
    it->second.local_affine_transform = std::make_unique<Mat4>(transform);
  } else {
    *it->second.local_affine_transform = transform;
  }
  InvalidateWorldTransform(node_id);
}

void SceneGraph::SetParent(uint32_t node_id, uint32_t parent_id) {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return;
  SceneNode& node = it->second;

  if (node.parent_id != 0) {
    auto pit = nodes_.find(node.parent_id);
    if (pit != nodes_.end()) {
      auto& pc = pit->second.children;
      pc.erase(std::remove(pc.begin(), pc.end(), node_id), pc.end());
    }
  }

  node.parent_id = parent_id;

  if (parent_id != 0) {
    auto pit = nodes_.find(parent_id);
    if (pit != nodes_.end()) {
      pit->second.children.push_back(node_id);
    }
  }

  InvalidateWorldTransform(node_id);
}

void SceneGraph::SetVisible(uint32_t node_id, bool vis) {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return;
  it->second.visible = vis;
}

bool SceneGraph::IsVisible(uint32_t node_id) const {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return false;
  return it->second.visible;
}

void SceneGraph::SetBoundingBox(uint32_t node_id, const AABB& box) {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return;
  it->second.bounding_box = box;
}

SceneGraph::Vec3 SceneGraph::GetWorldPosition(uint32_t node_id) const {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return {0, 0, 0};

  const SceneNode& node = it->second;
  if (node.world_dirty) {
    UpdateWorldTransform(node);
  }

  return {node.world_transform.m[12], node.world_transform.m[13],
          node.world_transform.m[14]};
}

Mat4 SceneGraph::GetWorldTransform(uint32_t node_id) const {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return Mat4::Identity();

  const SceneNode& node = it->second;
  if (node.world_dirty) {
    UpdateWorldTransform(node);
  }
  return node.world_transform;
}

uint32_t SceneGraph::GetNodeCount() const {
  return static_cast<uint32_t>(nodes_.size());
}

std::vector<uint32_t> SceneGraph::GetVisibleNodes() const {
  std::vector<uint32_t> result;
  result.reserve(nodes_.size());
  for (const auto& [id, node] : nodes_) {
    if (node.visible) {
      result.push_back(id);
    }
  }
  return result;
}

std::vector<uint32_t> SceneGraph::FrustumCull(
    const float (*frustum_planes)[4]) const {
  std::vector<uint32_t> result;
  result.reserve(nodes_.size());
  for (const auto& [id, node] : nodes_) {
    if (!node.visible) continue;

    Vec3 wp = GetWorldPosition(id);

    if (TestAABBFrustum(node.bounding_box, wp, frustum_planes)) {
      result.push_back(id);
    }
  }
  return result;
}

std::vector<uint32_t> SceneGraph::GetNodesInRadius(float cx, float cy,
                                                    float cz,
                                                    float radius) const {
  float r2 = radius * radius;
  std::vector<uint32_t> result;
  for (const auto& [id, node] : nodes_) {
    Vec3 wp = GetWorldPosition(id);
    float dx = wp.x - cx;
    float dy = wp.y - cy;
    float dz = wp.z - cz;
    if (dx * dx + dy * dy + dz * dz <= r2) {
      result.push_back(id);
    }
  }
  return result;
}

std::vector<uint32_t> SceneGraph::GetNodesByType(RenderableType type) const {
  std::vector<uint32_t> result;
  for (const auto& [id, node] : nodes_) {
    if (node.render_type == type) {
      result.push_back(id);
    }
  }
  return result;
}

void SceneGraph::ForEachVisible(
    const std::function<void(uint32_t, const SceneNode&)>& callback) const {
  for (const auto& [id, node] : nodes_) {
    if (node.visible) {
      callback(id, node);
    }
  }
}

void SceneGraph::Clear() {
  nodes_.clear();
  next_id_ = 1;
}

const SceneNode* SceneGraph::GetNode(uint32_t node_id) const {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return nullptr;
  return &it->second;
}

void SceneGraph::InvalidateWorldTransform(uint32_t node_id) {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return;
  it->second.world_dirty = true;
  for (uint32_t child_id : it->second.children) {
    InvalidateWorldTransform(child_id);
  }
}

void SceneGraph::UpdateWorldTransform(const SceneNode& node) const {
  Mat4 local{};
  if (node.local_affine_transform != nullptr) {
    local = *node.local_affine_transform;
  } else {

    const auto rotation = BuildRotationMatrix4x4Quaternion(
        RenderVec4{node.rotation.x, node.rotation.y, node.rotation.z, node.rotation.w});
    const auto scaled_rotation = ScaleMatrix4x4BasisRows(
        rotation, RenderVec3{node.scale_x, node.scale_y, node.scale_z});
    std::copy(scaled_rotation.begin(), scaled_rotation.end(), local.m);
    local.m[12] = node.pos_x;
    local.m[13] = node.pos_y;
    local.m[14] = node.pos_z;
  }

  if (node.parent_id != 0) {
    auto pit = nodes_.find(node.parent_id);
    if (pit != nodes_.end()) {
      const SceneNode& parent = pit->second;
      if (parent.world_dirty) {
        UpdateWorldTransform(parent);
      }

      node.world_transform = Mat4::Multiply(local, parent.world_transform);
    } else {
      node.world_transform = local;
    }
  } else {
    node.world_transform = local;
  }

  node.world_dirty = false;
}

bool SceneGraph::TestAABBFrustum(const AABB& box, const Vec3& world_pos,
                                  const float (*planes)[4]) {

  float min_x = box.min_x + world_pos.x;
  float min_y = box.min_y + world_pos.y;
  float min_z = box.min_z + world_pos.z;
  float max_x = box.max_x + world_pos.x;
  float max_y = box.max_y + world_pos.y;
  float max_z = box.max_z + world_pos.z;

  for (int i = 0; i < 6; ++i) {
    float a = planes[i][0];
    float b = planes[i][1];
    float c = planes[i][2];
    float d = planes[i][3];

    float px = (a >= 0.0f) ? max_x : min_x;
    float py = (b >= 0.0f) ? max_y : min_y;
    float pz = (c >= 0.0f) ? max_z : min_z;

    if (a * px + b * py + c * pz + d < 0.0f) {
      return false;
    }
  }
  return true;
}

}

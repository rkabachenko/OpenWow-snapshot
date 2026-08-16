#include "openwow/render/m2/m2_instance_store.h"

#include <cmath>

namespace openwow::render::m2 {
namespace {

bool IsFinite(const RenderVec3& value) noexcept {
  return std::isfinite(value[0]) && std::isfinite(value[1]) &&
         std::isfinite(value[2]);
}

bool IsFinite(const RenderMatrix4x4& value) noexcept {
  return std::all_of(value.begin(), value.end(),
                     [](const float component) {
                       return std::isfinite(component);
                     });
}

}

M2ResultStatus M2InstanceStore::ApplyWorldTransformMatrixLocked(
    detail::M2Instance& instance, const RenderMatrix4x4& matrix) noexcept {
  if (!IsFinite(matrix)) return M2ResultStatus::kFailed;
  StoreExplicitWorldTransformLocked(instance, matrix);
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::SetWorldTransformMatrix(
    const std::uint32_t instance_id, const RenderMatrix4x4& matrix) {

  if (!IsFinite(matrix)) return M2ResultStatus::kFailed;
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  return ApplyWorldTransformMatrixLocked(*found->second, matrix);
}

M2ResultStatus M2InstanceStore::ClearWorldTransformMatrix(
    const std::uint32_t instance_id) {
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  found->second->world_transform = kRenderIdentityMatrix4x4;
  found->second->has_explicit_world_transform = false;
  found->second->dirty_flags |= detail::M2Instance::kDirtyWorldTransform;
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::SetTransform(
    const std::uint32_t instance_id,
    const std::optional<RenderVec3>& position,
    const std::optional<RenderVec3>& rotation_degrees, const float scale) {
  if ((position && !IsFinite(*position)) ||
      (rotation_degrees && !IsFinite(*rotation_degrees)) ||
      !std::isfinite(scale)) {
    return M2ResultStatus::kFailed;
  }
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  if (position) found->second->position = *position;
  if (rotation_degrees) found->second->rotation = *rotation_degrees;
  found->second->scale = scale;
  found->second->has_explicit_world_transform = false;
  found->second->dirty_flags |= detail::M2Instance::kDirtyWorldTransform;
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::SetPosition(
    const std::uint32_t instance_id, const RenderVec3& position) {
  if (!IsFinite(position)) return M2ResultStatus::kFailed;
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  found->second->position = position;
  found->second->has_explicit_world_transform = false;
  found->second->dirty_flags |= detail::M2Instance::kDirtyWorldTransform;
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::SetRotationDegrees(
    const std::uint32_t instance_id, const RenderVec3& rotation_degrees) {
  if (!IsFinite(rotation_degrees)) return M2ResultStatus::kFailed;
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  found->second->rotation = rotation_degrees;
  found->second->has_explicit_world_transform = false;
  found->second->dirty_flags |= detail::M2Instance::kDirtyWorldTransform;
  return M2ResultStatus::kReady;
}

M2ResultStatus M2InstanceStore::SetScale(const std::uint32_t instance_id,
                                          const float scale) {
  if (!std::isfinite(scale)) return M2ResultStatus::kFailed;
  std::lock_guard lock(mutex_);
  const auto found = instances_.find(instance_id);
  if (found == instances_.end()) return M2ResultStatus::kFailed;
  found->second->scale = scale;

  found->second->dirty_flags |= detail::M2Instance::kDirtyWorldTransform;
  return M2ResultStatus::kReady;
}

}

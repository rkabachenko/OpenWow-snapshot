#include "openwow/render/m2/m2_visibility.h"

#include "openwow/render/m2/m2_spatial_queries.h"
#include "openwow/render/scene/frustum_culler.h"

#include <cmath>

namespace openwow::render::m2 {

M2Visibility::M2Visibility(M2SystemMutex& mutex,
                           const M2ModelRepository::ModelMap& models,
                           M2InstanceStore& instances) noexcept
    : mutex_(mutex), models_(models), instances_(instances) {}

void M2Visibility::Update() {
  std::lock_guard lock(mutex_);
  visible_count_ = 0;
  for (const auto& [id, instance] : instances_) {
    (void)id;
    visible_count_ += instance->visible ? 1u : 0u;
  }
}

void M2Visibility::Update(const RenderMatrix4x4View view_projection) {
  std::lock_guard lock(mutex_);
  visible_count_ = 0;
  const FrustumPlanes frustum =
      FrustumCuller::ExtractFromViewProj(view_projection);
  for (const auto& [id, instance] : instances_) {
    (void)id;
    if (!instance->visible) continue;
    const auto model = models_.find(instance->model_id);
    if (model == models_.end() || !model->second->loaded) continue;

    const RenderMatrix4x4 matrix = ComputeM2ModelMatrix(*instance);
    const auto sphere = model->second->GetTransitionBoundingSphere(
        instance->animation_sequence_index,
        instance->GetBlendSourceSequenceIndex());
    const float world_x = matrix[0] * sphere[0] + matrix[4] * sphere[1] +
                          matrix[8] * sphere[2] + matrix[12];
    const float world_y = matrix[1] * sphere[0] + matrix[5] * sphere[1] +
                          matrix[9] * sphere[2] + matrix[13];
    const float world_z = matrix[2] * sphere[0] + matrix[6] * sphere[1] +
                          matrix[10] * sphere[2] + matrix[14];
    const float scale =
        std::sqrt(matrix[0] * matrix[0] + matrix[1] * matrix[1] +
                  matrix[2] * matrix[2]);
    if (FrustumCuller::TestSphere(frustum, world_x, world_y, world_z,
                                  sphere[3] * scale) != CullResult::Outside) {
      ++visible_count_;
    }
  }
}

std::uint32_t M2Visibility::visible_count() const {
  std::lock_guard lock(mutex_);
  return visible_count_;
}

}

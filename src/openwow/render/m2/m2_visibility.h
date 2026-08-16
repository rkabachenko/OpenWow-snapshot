#pragma once

#include "openwow/render/m2/m2_instance_store.h"
#include "openwow/render/m2/m2_model_repository.h"

namespace openwow::render::m2 {

class M2Visibility {
 public:
  M2Visibility(M2SystemMutex& mutex,
               const M2ModelRepository::ModelMap& models,
               M2InstanceStore& instances) noexcept;
  void Update();
  void Update(RenderMatrix4x4View view_projection);
  void Reset() noexcept { visible_count_ = 0; }
  [[nodiscard]] std::uint32_t visible_count() const;

 private:
  M2SystemMutex& mutex_;
  const M2ModelRepository::ModelMap& models_;
  M2InstanceStore& instances_;
  std::uint32_t visible_count_ = 0;
};

}

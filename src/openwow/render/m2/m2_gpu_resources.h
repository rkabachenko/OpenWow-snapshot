#pragma once

#include "openwow/render/m2/m2_model_preparation.h"

namespace openwow::render {
class TextureManager;
}

namespace openwow::render::m2 {

class M2GpuResources {
 public:
  ~M2GpuResources();

  void BindTextureManager(TextureManager* texture_manager) noexcept;
  [[nodiscard]] bool EnsureShaders();
  [[nodiscard]] M2ResourcePreparationResult Commit(
      detail::M2ModelResource& resource);
  void Shutdown();
  [[nodiscard]] const M2ShaderHandles& shaders() const noexcept;

 private:
  TextureManager* texture_manager_{nullptr};
  M2ShaderHandles shaders_;
  bool shaders_initialized_{false};
};

}

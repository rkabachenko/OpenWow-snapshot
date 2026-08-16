#include "openwow/render/m2/m2_gpu_resources.h"

#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"
#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::render::m2 {
namespace {

M2ResourcePreparationResult Failure(M2ResultReason reason,
                                    std::string detail) {
  return {.status = M2ResultStatus::kFailed,
          .reason = reason,
          .detail = std::move(detail)};
}

}

M2GpuResources::~M2GpuResources() { Shutdown(); }

void M2GpuResources::BindTextureManager(
    TextureManager* texture_manager) noexcept {
  texture_manager_ = texture_manager;
}

bool M2GpuResources::EnsureShaders() {
  if (!shaders_initialized_) {
    shaders_ = LoadM2Program();
    shaders_initialized_ = true;
  }
  const auto* shader = FindM2BonePaletteShader(shaders_, 1u);
  return shader != nullptr && bgfx::isValid(shader->program);
}

M2ResourcePreparationResult M2GpuResources::Commit(
    detail::M2ModelResource& resource) {
  if (!IsRendererContextActive()) {

    if (IsRendererDeviceRestarting()) {
      return Failure(M2ResultReason::kGpuGeometryNotReady,
                     "render device restart in progress");
    }
    return {};
  }
  const bool has_geometry = !resource.skin_geometry.vertices.empty() &&
                            !resource.skin_geometry.indices.empty();
  if (has_geometry) {
    if (!EnsureShaders()) {
      return Failure(M2ResultReason::kGpuGeometryNotReady,
                     "M2 shader program is not ready");
    }
    resource.skinned_mesh = std::make_unique<M2SkinnedMesh>();
    if (!resource.skinned_mesh->InitSkinnedFromGeometry(resource.skin_geometry,
                                                         shaders_)) {
      resource.skinned_mesh.reset();
    }
  }

  resource.gpu_texture_leases.assign(resource.model_data.textures.size(),
                                     TextureLease{});
  resource.gpu_textures.assign(resource.model_data.textures.size(),
                               BGFX_INVALID_HANDLE);
  if (texture_manager_ == nullptr) {
    return Failure(M2ResultReason::kMissingTexture,
                   "texture manager is not bound");
  }
  for (std::size_t index = 0; index < resource.model_data.textures.size();
       ++index) {
    const auto& texture = resource.model_data.textures[index];
    if (texture.type != 0u) {
      continue;
    }
    TextureLease lease = texture_manager_->AcquireTextureStrict(texture.name_text);
    if (!lease.valid()) {
      return Failure(M2ResultReason::kMissingTexture, texture.name_text);
    }
    resource.gpu_textures[index] = BgfxTextureLeaseAccess::Get(lease);
    resource.gpu_texture_leases[index] = std::move(lease);
  }
  if (has_geometry && !resource.HasReadyGpuGeometry()) {
    return Failure(M2ResultReason::kGpuGeometryNotReady,
                   "GPU render geometry is not ready");
  }
  return {};
}

void M2GpuResources::Shutdown() {
  if (shaders_initialized_) {
    DestroyM2Program(shaders_);
    shaders_initialized_ = false;
  }
}

const M2ShaderHandles& M2GpuResources::shaders() const noexcept {
  return shaders_;
}

}

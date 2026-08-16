#pragma once

#include "openwow/render/m2/m2_runtime_state.h"

#include <functional>

namespace openwow::render::m2 {

struct M2PreparedModel::Impl {
  detail::M2ModelResource resource;
  std::string cache_key;
  std::vector<M2ModelTextureDependency> texture_dependencies;
  M2ModelSpatialInfo spatial_info;
};

struct M2ResourcePreparationResult {
  M2ResultStatus status{M2ResultStatus::kReady};
  M2ResultReason reason{M2ResultReason::kNone};
  std::string detail;
};

struct M2PreparedModelAccess {
  static std::unique_ptr<M2PreparedModel> Create(
      detail::M2ModelResource resource, std::string cache_key,
      std::vector<M2ModelTextureDependency> texture_dependencies,
      M2ModelSpatialInfo spatial_info);
  static M2PreparedModel::Impl* Get(M2PreparedModel* prepared) noexcept;
};

using M2CpuModelBuilder = std::function<M2ResourcePreparationResult(
    const data::model::M2Model&, const data::model::M2Skin&,
    detail::M2ModelResource&)>;

[[nodiscard]] M2ModelSpatialInfo BuildM2ModelSpatialInfo(
    const detail::M2ModelResource& resource);

[[nodiscard]] M2ModelSpatialInfo BuildM2ModelSpatialInfoFromHeaderBounds(
    const data::model::M2HeaderBoundsResult& bounds);

[[nodiscard]] M2ModelPrepareResult PrepareM2ModelPackage(
    const std::string& path, bool require_render_data,
    std::uint32_t skin_profile_quality,
    const std::function<std::vector<std::uint8_t>(const std::string&)>& loader,
    const M2CpuModelBuilder& build_cpu,
    const M2ModelEarlyReadyCallback& on_early_ready = {});

}

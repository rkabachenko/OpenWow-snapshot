#pragma once

#include "openwow/render/diagnostics/render_submit_trace.h"
#include "openwow/render/m2/m2_material_pipeline.h"
#include "openwow/render/m2/m2_runtime_state.h"

#include <optional>
#include <string>
#include <string_view>

namespace openwow::render::m2 {

[[nodiscard]] std::string M2TraceHexValue(std::uint64_t value);
[[nodiscard]] std::string M2TraceTextureName(
    const data::model::M2Model& model, std::size_t texture_index);
void RecordM2EffectSubmitTrace(
    const std::optional<RenderSubmitTraceBinding>& trace, std::uint16_t view_id,
    std::string_view model_path, std::uint32_t skin_profile,
    std::uint32_t instance_id, std::string_view pass, std::string_view pipeline,
    std::uint64_t state, std::int32_t sort_key, std::string mesh,
    std::string material);
void RecordM2StatusTrace(
    const std::optional<RenderSubmitTraceBinding>& trace, std::uint16_t view_id,
    std::string_view model_path, std::uint32_t skin_profile,
    std::uint32_t instance_id, std::string_view pass, std::string_view pipeline,
    std::string_view skinning_mode, M2ResultStatus status,
    M2ResultReason reason, std::string detail);
void RecordM2TextureUnitSubmitTrace(
    const std::optional<RenderSubmitTraceBinding>& trace, std::uint16_t view_id,
    std::string_view model_path, std::uint32_t skin_profile,
    std::uint32_t instance_id, bool opaque_pass, bool use_gpu_skinning,
    std::uint64_t state, const M2SkinnedMesh& mesh_resource,
    const detail::M2ModelResource::RenderBatch& batch,
    const data::model::M2Model& model, const data::model::M2Skin& skin,
    const data::model::SkinTextureUnit& texture_unit,
    const data::model::SkinSubmesh& submesh,
    const M2TextureUnitMaterialSample& material_sample, std::size_t tex0_index,
    std::size_t tex1_index);

}

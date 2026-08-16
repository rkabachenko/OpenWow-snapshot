#include "openwow/render/m2/m2_submit_trace.h"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <type_traits>

namespace openwow::render::m2 {
namespace {

template <typename Value>
std::vector<std::uint8_t> CaptureTrivialBytes(const std::span<const Value> values) {
  static_assert(std::is_trivially_copyable_v<Value>);
  std::vector<std::uint8_t> bytes(values.size_bytes());
  if (!bytes.empty()) {
    std::memcpy(bytes.data(), values.data(), bytes.size());
  }
  return bytes;
}

void CaptureM2MeshResources(
    const std::optional<RenderSubmitTraceBinding>& trace,
    const M2SkinnedMesh& mesh_resource, const data::model::M2Model& model,
    const data::model::M2Skin& skin) {
  if (!trace.has_value()) {
    return;
  }
  auto& resources = trace->trace.get();
  const auto vertex_buffer = mesh_resource.CapturedVertexBufferKey();
  const auto index_buffer = mesh_resource.CapturedIndexBufferKey();
  if (!vertex_buffer.has_value() || !index_buffer.has_value()) {
    return;
  }

  const RenderResourceKey vertex_format = M2SkinnedMesh::CapturedVertexFormatKey();
  if (!resources.HasResource(vertex_format)) {
    RenderResourceContent format_content;
    format_content.vertex_attributes = {
        {.stream = 0, .semantic = 0,
         .byte_offset = offsetof(M2SkinnedMesh::SkinnedVertex, x),
         .byte_stride = sizeof(M2SkinnedMesh::SkinnedVertex),
         .component_type = RenderCapturedComponentType::Float,
         .component_count = 3, .normalized = false},
        {.stream = 0, .semantic = 3,
         .byte_offset = offsetof(M2SkinnedMesh::SkinnedVertex, nx),
         .byte_stride = sizeof(M2SkinnedMesh::SkinnedVertex),
         .component_type = RenderCapturedComponentType::Float,
         .component_count = 3, .normalized = false},
        {.stream = 0, .semantic = 6,
         .byte_offset = offsetof(M2SkinnedMesh::SkinnedVertex, u0),
         .byte_stride = sizeof(M2SkinnedMesh::SkinnedVertex),
         .component_type = RenderCapturedComponentType::Float,
         .component_count = 2, .normalized = false},
        {.stream = 0, .semantic = 7,
         .byte_offset = offsetof(M2SkinnedMesh::SkinnedVertex, u1),
         .byte_stride = sizeof(M2SkinnedMesh::SkinnedVertex),
         .component_type = RenderCapturedComponentType::Float,
         .component_count = 2, .normalized = false},
        {.stream = 0, .semantic = 4,
         .byte_offset = offsetof(M2SkinnedMesh::SkinnedVertex, color),
         .byte_stride = sizeof(M2SkinnedMesh::SkinnedVertex),
         .component_type = RenderCapturedComponentType::UnsignedByte,
         .component_count = 4, .normalized = true},
        {.stream = 0, .semantic = 2,
         .byte_offset = offsetof(M2SkinnedMesh::SkinnedVertex, bone_indices),
         .byte_stride = sizeof(M2SkinnedMesh::SkinnedVertex),
         .component_type = RenderCapturedComponentType::UnsignedByte,
         .component_count = 4, .normalized = true},
        {.stream = 0, .semantic = 1,
         .byte_offset = offsetof(M2SkinnedMesh::SkinnedVertex, bone_weights),
         .byte_stride = sizeof(M2SkinnedMesh::SkinnedVertex),
         .component_type = RenderCapturedComponentType::UnsignedByte,
         .component_count = 4, .normalized = true},
    };
    (void)resources.RegisterResource(
        {.key = vertex_format, .format = "M2SkinnedVertex",
         .name = "M2 skinned vertex format"},
        std::move(format_content));
  }

  if (!resources.HasResource(*vertex_buffer)) {
    const auto prepared = M2SkinnedMesh::PrepareSkinnedVertices(model.vertices);
    auto bytes = CaptureTrivialBytes<M2SkinnedMesh::SkinnedVertex>(prepared);
    const auto byte_size = bytes.size();
    (void)resources.RegisterResource(
        {.key = *vertex_buffer, .byte_size = byte_size,
         .format = "M2SkinnedVertex", .name = "M2 skinned vertex buffer"},
        {.bytes = std::move(bytes),
         .element_stride = sizeof(M2SkinnedMesh::SkinnedVertex)});
  }

  if (!resources.HasResource(*index_buffer)) {
    auto bytes = CaptureTrivialBytes<std::uint16_t>(skin.triangles);
    const auto byte_size = bytes.size();
    (void)resources.RegisterResource(
        {.key = *index_buffer, .byte_size = byte_size, .format = "R16_UINT",
         .name = "M2 triangle index buffer"},
        {.bytes = std::move(bytes), .element_stride = sizeof(std::uint16_t),
         .index_component_type = RenderCapturedComponentType::UnsignedShort});
  }
}

}

std::string M2TraceHexValue(const std::uint64_t value) {
  std::ostringstream out;
  out << "0x" << std::hex << std::nouppercase << value;
  return out.str();
}

std::string M2TraceTextureName(const data::model::M2Model& model,
                               const std::size_t texture_index) {
  if (texture_index >= model.textures.size()) {
    return "-";
  }
  const auto& texture = model.textures[texture_index];
  return texture.name_text.empty() ? "-" : texture.name_text;
}

void RecordM2EffectSubmitTrace(
    const std::optional<RenderSubmitTraceBinding>& trace,
    const std::uint16_t view_id, const std::string_view model_path,
    const std::uint32_t skin_profile, const std::uint32_t instance_id,
    const std::string_view pass, const std::string_view pipeline,
    const std::uint64_t state, const std::int32_t sort_key, std::string mesh,
    std::string material) {
  if (!trace.has_value()) {
    return;
  }
  RenderSubmitTraceEvent event;
  event.idx = trace->index.get()++;
  event.frame = trace->frame.get();
  event.view = view_id;
  event.model_path = std::string(model_path);
  event.skin_profile = skin_profile;
  event.instance_id = instance_id;
  event.pass = std::string(pass);
  event.pipeline = std::string(pipeline);
  event.skinning_mode = "effect";
  event.state_mask = static_cast<std::uint32_t>(state & 0xffffffffu);
  event.sort_key = sort_key;
  event.mesh = std::move(mesh);
  event.material = std::move(material);
  trace->trace.get().Add(std::move(event));
}

void RecordM2StatusTrace(
    const std::optional<RenderSubmitTraceBinding>& trace,
    const std::uint16_t view_id, const std::string_view model_path,
    const std::uint32_t skin_profile, const std::uint32_t instance_id,
    const std::string_view pass, const std::string_view pipeline,
    const std::string_view skinning_mode, const M2ResultStatus status,
    const M2ResultReason reason, std::string detail) {
  if (!trace.has_value()) {
    return;
  }
  RenderSubmitTraceEvent event;
  event.idx = trace->index.get()++;
  event.frame = trace->frame.get();
  event.view = view_id;
  event.model_path = std::string(model_path);
  event.skin_profile = skin_profile;
  event.instance_id = instance_id;
  event.pass = std::string(pass);
  event.pipeline = std::string(pipeline);
  event.skinning_mode = std::string(skinning_mode);
  event.status = M2ResultStatusName(status);
  event.reason = M2ResultReasonName(reason);
  event.detail = std::move(detail);
  trace->trace.get().Add(std::move(event));
}

void RecordM2TextureUnitSubmitTrace(
    const std::optional<RenderSubmitTraceBinding>& trace,
    const std::uint16_t view_id, const std::string_view model_path,
    const std::uint32_t skin_profile, const std::uint32_t instance_id,
    const bool opaque_pass, const bool use_gpu_skinning,
    const std::uint64_t state, const M2SkinnedMesh& mesh_resource,
    const detail::M2ModelResource::RenderBatch& batch,
    const data::model::M2Model& model, const data::model::M2Skin& skin,
    const data::model::SkinTextureUnit& texture_unit,
    const data::model::SkinSubmesh& submesh,
    const M2TextureUnitMaterialSample& material_sample,
    const std::size_t tex0_index, const std::size_t tex1_index) {
  if (!trace.has_value()) {
    return;
  }
  const auto next_idx = trace->index.get()++;
  CaptureM2MeshResources(trace, mesh_resource, model, skin);
  const auto* render_flags = texture_unit.render_flags_index < model.render_flags.size()
                                 ? &model.render_flags[texture_unit.render_flags_index]
                                 : nullptr;
  std::ostringstream mesh;
  mesh << "tu=" << batch.texture_unit_index << " submesh=" << batch.submesh_index
       << " section=" << submesh.section_id << " indices=" << submesh.index_start << "+"
       << submesh.index_count << " tex_unit=" << texture_unit.tex_unit_number
       << " mode=" << texture_unit.mode << " flags=" << M2TraceHexValue(texture_unit.flags)
       << " priority_plane=" << static_cast<int>(texture_unit.priority_plane);
  std::ostringstream material;
  material << "rf_idx=" << texture_unit.render_flags_index
           << " rf_flags=" << (render_flags ? M2TraceHexValue(render_flags->flags) : "-")
           << " rf_blend=" << (render_flags ? std::to_string(render_flags->blend_mode) : "-")
           << " shader=" << M2TraceHexValue(batch.shader_id) << " op=(" << batch.shader_op1
           << "," << batch.shader_op2 << ") tex_count=" << batch.texture_count
           << " variant=" << batch.shader_variant << " alpha=" << std::setprecision(6)
           << material_sample.color[3] << " trans=" << material_sample.transparency_alpha
           << " color_a=" << material_sample.color_alpha << " state=" << M2TraceHexValue(state);
  if (batch.texture_count > 0) {
    material << " tex0=" << tex0_index << ":" << M2TraceTextureName(model, tex0_index);
  }
  if (batch.texture_count > 1) {
    material << " tex1=" << tex1_index << ":" << M2TraceTextureName(model, tex1_index);
  }
  RenderSubmitTraceEvent event;
  event.idx = next_idx;
  event.frame = trace->frame.get();
  event.view = view_id;
  event.model_path = std::string(model_path);
  event.skin_profile = skin_profile;
  event.instance_id = instance_id;
  event.pass = opaque_pass ? "m2.opaque" : "m2.transparent";
  event.pipeline = use_gpu_skinning ? "m2.gpu_skinned" : "m2.static";
  event.skinning_mode = use_gpu_skinning ? "gpu" : "static";
  event.state_mask = static_cast<std::uint32_t>(state & 0xffffffffu);
  event.sort_key = static_cast<std::int32_t>(batch.texture_unit_index);
  event.mesh = mesh.str();
  event.material = material.str();
  event.primitive_reference = RenderSubmitPrimitiveTrace{
      .topology_code = 0x00000004u,
      .vertex_format = M2SkinnedMesh::CapturedVertexFormatKey(),
      .vertex_buffer = mesh_resource.CapturedVertexBufferKey(),
      .index_buffer = mesh_resource.CapturedIndexBufferKey(),
      .vertex_count = static_cast<std::uint32_t>(model.vertices.size()),
      .first_index = batch.start_index, .index_count = batch.index_count};
  event.resource_slots[0] = mesh_resource.CapturedVertexBufferKey();
  event.resource_slots[1] = mesh_resource.CapturedIndexBufferKey();
  event.resource_slots[2] = M2SkinnedMesh::CapturedVertexFormatKey();
  trace->trace.get().Add(std::move(event));
}

}

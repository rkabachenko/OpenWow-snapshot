#include "openwow/render/effects/projectiles/missile_trajectory_renderer.h"

#include "openwow/game/missile_trajectory.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace openwow::render {
namespace {

constexpr float kRetailEndpointDepthBias = 0.04f;

[[nodiscard]] std::uint32_t ArgbToAbgr(const std::uint32_t argb) noexcept {
  const std::uint32_t alpha = argb & 0xFF000000u;
  const std::uint32_t red = (argb >> 16u) & 0xFFu;
  const std::uint32_t green = argb & 0x0000FF00u;
  const std::uint32_t blue = (argb & 0xFFu) << 16u;
  return alpha | blue | green | red;
}

constexpr std::uint64_t kTransparentWorldState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
    BGFX_STATE_DEPTH_TEST_LEQUAL |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                          BGFX_STATE_BLEND_INV_SRC_ALPHA);

constexpr std::uint64_t kAdditiveWorldState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
    BGFX_STATE_DEPTH_TEST_LEQUAL |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);

[[nodiscard]] std::array<float, 3> Cross(
    const std::array<float, 3>& lhs,
    const std::array<float, 3>& rhs) noexcept {
  return {lhs[1] * rhs[2] - lhs[2] * rhs[1],
          lhs[2] * rhs[0] - lhs[0] * rhs[2],
          lhs[0] * rhs[1] - lhs[1] * rhs[0]};
}

[[nodiscard]] float Length(const std::array<float, 3>& value) noexcept {
  return std::sqrt(value[0] * value[0] + value[1] * value[1] +
                   value[2] * value[2]);
}

[[nodiscard]] std::array<float, 3> Normalize(
    const std::array<float, 3>& value,
    const std::array<float, 3>& fallback) noexcept {
  const float length = Length(value);
  if (length <= 0.0001f) {
    return fallback;
  }
  return {value[0] / length, value[1] / length, value[2] / length};
}

}

MissileTrajectoryRenderer::MissileTrajectoryRenderer(TextureManager& textures)
    : textures_(textures) {}

MissileTrajectoryRenderer::~MissileTrajectoryRenderer() { Shutdown(); }

bool MissileTrajectoryRenderer::Initialize() {
  if (initialized_) {
    return true;
  }

  program_ = CreateEmbeddedProgram(ShaderProgramId::Ribbon,
                                   bgfx::getRendererType());
  if (!bgfx::isValid(program_)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "MissileTrajectoryRenderer: ribbon shader unavailable");
    return false;
  }

  texture_sampler_ =
      bgfx::createUniform("s_ribbonTex", bgfx::UniformType::Sampler);
  ribbon_color_ =
      bgfx::createUniform("u_ribbonColor", bgfx::UniformType::Vec4);
  fog_color_ = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);
  fog_params_ = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
  if (!bgfx::isValid(texture_sampler_) || !bgfx::isValid(ribbon_color_) ||
      !bgfx::isValid(fog_color_) || !bgfx::isValid(fog_params_)) {
    Shutdown();
    return false;
  }

  layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();
  initialized_ = true;
  return true;
}

void MissileTrajectoryRenderer::Shutdown() {
  spell_chains_.clear();
  if (bgfx::isValid(fog_params_)) {
    bgfx::destroy(fog_params_);
    fog_params_ = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(fog_color_)) {
    bgfx::destroy(fog_color_);
    fog_color_ = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(ribbon_color_)) {
    bgfx::destroy(ribbon_color_);
    ribbon_color_ = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(texture_sampler_)) {
    bgfx::destroy(texture_sampler_);
    texture_sampler_ = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(program_)) {
    bgfx::destroy(program_);
    program_ = BGFX_INVALID_HANDLE;
  }
  initialized_ = false;
}

void MissileTrajectoryRenderer::Render(
    const std::uint8_t view_id, const float* view_mtx,
    const float* proj_mtx, const RenderFogState& fog,
    const game::MissileArcRenderSnapshot& snapshot,
    m2::M2TransparentDrawOrder& draw_order) {
  if (!initialized_ || view_mtx == nullptr || proj_mtx == nullptr) {
    return;
  }

  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);
  SubmitSpellChains(view_id, view_mtx, fog, draw_order);
  if (snapshot.active) {
    SubmitRibbon(view_id, snapshot, fog, draw_order);
    SubmitEndpointProjection(view_id, snapshot, fog, draw_order);
  }
}

namespace {

[[nodiscard]] std::uint32_t NextDrawDepth(m2::M2TransparentDrawOrder& draw_order) {
  return m2::M2TransparentDrawDepth::Encode(draw_order.Reserve(), 0u);
}

}

std::uint32_t MissileTrajectoryRenderer::CreateSpellChain(
    const game::SpellVisualChainRenderRequest& request) {
  if (!initialized_ || request.texture_path.empty()) {
    return 0u;
  }
  std::uint32_t handle = next_spell_chain_handle_++;
  if (handle == 0u) {
    handle = next_spell_chain_handle_++;
  }
  const auto& appearance = request.appearance;
  spell_chains_[handle] = SpellChainBeam{
      .chain_effect_id = request.chain_effect_id,
      .texture_path = std::string(request.texture_path),
      .source = request.source_position,
      .target = request.target_position,
      .average_segment_length = appearance.average_segment_length,
      .width = appearance.width,
      .noise_scale = appearance.noise_scale,
      .texture_coordinate_scale = appearance.texture_coordinate_scale,
      .wave_height = appearance.wave_height,
      .wave_frequency = appearance.wave_frequency,
      .arc_height = appearance.arc_height,
      .texture_length = appearance.texture_length,
      .wave_phase = appearance.wave_phase,
      .color_argb =
          (static_cast<std::uint32_t>(appearance.alpha) << 24u) |
          (static_cast<std::uint32_t>(appearance.red) << 16u) |
          (static_cast<std::uint32_t>(appearance.green) << 8u) |
          static_cast<std::uint32_t>(appearance.blue),
      .blend_mode = appearance.blend_mode,
      .visible = true,
  };
  return handle;
}

bool MissileTrajectoryRenderer::UpdateSpellChain(
    const std::uint32_t handle, const float* const source,
    const float* const target, const bool visible) {
  const auto found = spell_chains_.find(handle);
  if (!initialized_ || found == spell_chains_.end() || source == nullptr ||
      target == nullptr) {
    return false;
  }
  std::copy_n(source, 3u, found->second.source.begin());
  std::copy_n(target, 3u, found->second.target.begin());
  found->second.visible = visible;
  return true;
}

void MissileTrajectoryRenderer::DestroySpellChain(
    const std::uint32_t handle) {
  spell_chains_.erase(handle);
}

void MissileTrajectoryRenderer::SetDrawUniforms(
    const RenderFogState& fog) const {
  constexpr std::array<float, 4> kWhite{1.0f, 1.0f, 1.0f, 1.0f};
  bgfx::setUniform(ribbon_color_, kWhite.data());
  bgfx::setUniform(fog_color_, fog.color.data());
  bgfx::setUniform(fog_params_, fog.params.data());
}

void MissileTrajectoryRenderer::SubmitSpellChains(
    const std::uint8_t view_id, const float* const view_mtx,
    const RenderFogState& fog, m2::M2TransparentDrawOrder& draw_order) {
  const auto camera_forward = Normalize(
      {view_mtx[2], view_mtx[6], view_mtx[10]}, {0.0f, 0.0f, 1.0f});
  const auto camera_up = Normalize(
      {view_mtx[1], view_mtx[5], view_mtx[9]}, {0.0f, 0.0f, 1.0f});

  for (const auto& [_, beam] : spell_chains_) {
    if (!beam.visible || beam.texture_path.empty()) {
      continue;
    }
    const std::array<float, 3> delta{
        beam.target[0] - beam.source[0],
        beam.target[1] - beam.source[1],
        beam.target[2] - beam.source[2]};
    const float distance = Length(delta);
    if (distance <= 0.0001f) {
      continue;
    }
    const auto direction = Normalize(delta, {1.0f, 0.0f, 0.0f});
    auto side = Normalize(Cross(direction, camera_forward),
                          Cross(direction, camera_up));
    side = Normalize(side, {1.0f, 0.0f, 0.0f});
    const float half_width = std::max(std::abs(beam.width) * 0.5f, 0.01f);
    const auto segment_count = static_cast<std::uint32_t>(std::clamp(
        std::ceil(distance /
                  std::max(std::abs(beam.average_segment_length), 0.1f)),
        1.0f, 128.0f));
    const std::uint32_t vertex_count = (segment_count + 1u) * 2u;
    if (bgfx::getAvailTransientVertexBuffer(vertex_count, layout_) <
            vertex_count ||
        bgfx::getAvailTransientIndexBuffer(vertex_count) < vertex_count) {
      continue;
    }
    const auto texture = textures_.AcquireTextureAsync(
        beam.texture_path, TextureLoadFailurePolicy::kStrict,
        TextureLoadPriority::kDemand);
    const auto texture_handle = BgfxTextureLeaseAccess::Get(texture);
    if (!bgfx::isValid(texture_handle)) {
      continue;
    }

    bgfx::TransientVertexBuffer vertices;
    bgfx::TransientIndexBuffer indices;
    bgfx::allocTransientVertexBuffer(&vertices, vertex_count, layout_);
    bgfx::allocTransientIndexBuffer(&indices, vertex_count);
    auto* gpu_vertices = reinterpret_cast<GpuVertex*>(vertices.data);
    auto* gpu_indices = reinterpret_cast<std::uint16_t*>(indices.data);
    const float texture_scale =
        std::abs(beam.texture_length) > 0.0001f
            ? distance / std::abs(beam.texture_length)
            : distance * std::max(std::abs(beam.texture_coordinate_scale),
                                  0.01f);
    constexpr float kPi = 3.14159265358979323846f;
    for (std::uint32_t segment = 0u; segment <= segment_count; ++segment) {
      const float t = static_cast<float>(segment) /
                      static_cast<float>(segment_count);
      std::array<float, 3> center{
          beam.source[0] + delta[0] * t,
          beam.source[1] + delta[1] * t,
          beam.source[2] + delta[2] * t};

      const float envelope = std::sin(t * kPi);
      const float wave =
          std::sin(t * beam.wave_frequency * 2.0f * kPi + beam.wave_phase) *
          beam.wave_height;
      const float noise =
          std::sin((t * 17.0f + static_cast<float>(beam.chain_effect_id)) *
                   2.0f * kPi) *
          beam.noise_scale;
      const float displacement =
          envelope * (beam.arc_height + wave + noise);
      for (std::size_t axis = 0u; axis < center.size(); ++axis) {
        center[axis] += camera_up[axis] * displacement;
      }
      for (std::uint32_t edge = 0u; edge < 2u; ++edge) {
        const float sign = edge == 0u ? -1.0f : 1.0f;
        const auto vertex = segment * 2u + edge;
        gpu_vertices[vertex] = {
            center[0] + side[0] * half_width * sign,
            center[1] + side[1] * half_width * sign,
            center[2] + side[2] * half_width * sign,
            t * texture_scale, static_cast<float>(edge),
            ArgbToAbgr(beam.color_argb)};
        gpu_indices[vertex] = static_cast<std::uint16_t>(vertex);
      }
    }

    SetDrawUniforms(fog);
    bgfx::setState((beam.blend_mode == 0u ? kTransparentWorldState
                                          : kAdditiveWorldState) |
                   BGFX_STATE_PT_TRISTRIP);
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::setIndexBuffer(&indices);
    bgfx::setTexture(0, texture_sampler_, texture_handle,
                     BGFX_SAMPLER_V_CLAMP);

    bgfx::submit(view_id, program_, NextDrawDepth(draw_order));
  }
}

void MissileTrajectoryRenderer::SubmitRibbon(
    const std::uint8_t view_id,
    const game::MissileArcRenderSnapshot& snapshot,
    const RenderFogState& fog, m2::M2TransparentDrawOrder& draw_order) {
  if (!snapshot.ribbon.has_value()) {
    return;
  }
  const auto& ribbon = *snapshot.ribbon;
  if (ribbon.texture_path.empty() || ribbon.vertices.empty() ||
      ribbon.indices.empty()) {
    return;
  }
  const auto texture = textures_.AcquireTextureAsync(
      ribbon.texture_path, TextureLoadFailurePolicy::kStrict,
      TextureLoadPriority::kDemand);
  const auto texture_handle = BgfxTextureLeaseAccess::Get(texture);
  if (!bgfx::isValid(texture_handle)) {
    return;
  }

  const auto vertex_count = static_cast<std::uint32_t>(ribbon.vertices.size());
  const auto index_count = static_cast<std::uint32_t>(ribbon.indices.size());
  if (bgfx::getAvailTransientVertexBuffer(vertex_count, layout_) < vertex_count ||
      bgfx::getAvailTransientIndexBuffer(index_count) < index_count) {
    return;
  }

  bgfx::TransientVertexBuffer vertices;
  bgfx::TransientIndexBuffer indices;
  bgfx::allocTransientVertexBuffer(&vertices, vertex_count, layout_);
  bgfx::allocTransientIndexBuffer(&indices, index_count);
  auto* gpu_vertices = reinterpret_cast<GpuVertex*>(vertices.data);
  for (std::size_t index = 0; index < ribbon.vertices.size(); ++index) {
    const auto& source = ribbon.vertices[index];
    gpu_vertices[index] = {source.x, source.y, source.z, source.u, source.v,
                           ArgbToAbgr(source.color_argb)};
  }
  std::memcpy(indices.data, ribbon.indices.data(),
              ribbon.indices.size() * sizeof(std::uint16_t));

  SetDrawUniforms(fog);
  bgfx::setState(kTransparentWorldState | BGFX_STATE_PT_TRISTRIP);
  bgfx::setVertexBuffer(0, &vertices);
  bgfx::setIndexBuffer(&indices);
  bgfx::setTexture(0, texture_sampler_, texture_handle,
                   BGFX_SAMPLER_V_CLAMP);

  bgfx::submit(view_id, program_, NextDrawDepth(draw_order));
}

void MissileTrajectoryRenderer::SubmitEndpointProjection(
    const std::uint8_t view_id,
    const game::MissileArcRenderSnapshot& snapshot,
    const RenderFogState& fog, m2::M2TransparentDrawOrder& draw_order) {
  if (!snapshot.endpoint.has_value()) {
    return;
  }
  const auto& endpoint = *snapshot.endpoint;
  if (endpoint.texture_path.empty()) {
    return;
  }
  const auto texture = textures_.AcquireTextureAsync(
      endpoint.texture_path, TextureLoadFailurePolicy::kStrict,
      TextureLoadPriority::kDemand);
  const auto texture_handle = BgfxTextureLeaseAccess::Get(texture);
  if (!bgfx::isValid(texture_handle) ||
      bgfx::getAvailTransientVertexBuffer(4u, layout_) < 4u ||
      bgfx::getAvailTransientIndexBuffer(6u) < 6u) {
    return;
  }

  bgfx::TransientVertexBuffer vertices;
  bgfx::TransientIndexBuffer indices;
  bgfx::allocTransientVertexBuffer(&vertices, 4u, layout_);
  bgfx::allocTransientIndexBuffer(&indices, 6u);

  const float center_x = (endpoint.min_corner[0] + endpoint.max_corner[0]) * 0.5f;
  const float center_y = (endpoint.min_corner[1] + endpoint.max_corner[1]) * 0.5f;
  const float center_z = (endpoint.min_corner[2] + endpoint.max_corner[2]) * 0.5f +
                         kRetailEndpointDepthBias;
  const float half_x = (endpoint.max_corner[0] - endpoint.min_corner[0]) * 0.5f;
  const float half_y = (endpoint.max_corner[1] - endpoint.min_corner[1]) * 0.5f;
  const std::array<std::array<float, 2>, 4> local{{
      {{-half_x, -half_y}}, {{half_x, -half_y}},
      {{half_x, half_y}}, {{-half_x, half_y}},
  }};
  constexpr std::array<std::array<float, 2>, 4> uv{{
      {{0.0f, 0.0f}}, {{1.0f, 0.0f}},
      {{1.0f, 1.0f}}, {{0.0f, 1.0f}},
  }};
  auto* gpu_vertices = reinterpret_cast<GpuVertex*>(vertices.data);
  for (std::size_t index = 0; index < local.size(); ++index) {
    const float rotated_x = endpoint.rotation_matrix[0] * local[index][0] +
                            endpoint.rotation_matrix[4] * local[index][1];
    const float rotated_y = endpoint.rotation_matrix[1] * local[index][0] +
                            endpoint.rotation_matrix[5] * local[index][1];
    gpu_vertices[index] = {
        center_x + rotated_x, center_y + rotated_y, center_z,
        uv[index][0], uv[index][1], ArgbToAbgr(endpoint.color_argb)};
  }
  constexpr std::array<std::uint16_t, 6> kIndices{0u, 1u, 2u, 0u, 2u, 3u};
  std::memcpy(indices.data, kIndices.data(), sizeof(kIndices));

  SetDrawUniforms(fog);
  bgfx::setState(kTransparentWorldState);
  bgfx::setVertexBuffer(0, &vertices);
  bgfx::setIndexBuffer(&indices);
  bgfx::setTexture(0, texture_sampler_, texture_handle,
                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

  bgfx::submit(view_id, program_, NextDrawDepth(draw_order));
}

}

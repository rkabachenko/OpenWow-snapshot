
#include "openwow/render/scene/m2_projected_texture_decal.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/resources/shaders/shader_registry.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::render {

namespace {

[[nodiscard]] std::uint32_t PackAbgr(const std::array<float, 4>& rgba) {
  const auto channel = [](const float value) {

    if (!(value > 0.0f)) {
      return std::uint32_t{0};
    }
    if (value >= 1.0f) {
      return std::uint32_t{255};
    }
    return static_cast<std::uint32_t>(std::lround(value * 255.0f));
  };
  return (channel(rgba[3]) << 24) | (channel(rgba[2]) << 16) |
         (channel(rgba[1]) << 8) | channel(rgba[0]);
}

}

bool M2ProjectedTextureDecalRenderer::Initialize() {
  if (initialized_) return true;

  program_ = CreateEmbeddedProgram(ShaderProgramId::Decal,
                                   bgfx::getRendererType());
  if (!bgfx::isValid(program_)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "M2ProjectedTextureDecalRenderer: failed to load decal program");
    return false;
  }
  s_tex_ = bgfx::createUniform("s_decalTex", bgfx::UniformType::Sampler);
  u_decal_params_ =
      bgfx::createUniform("u_decalParams", bgfx::UniformType::Vec4);

  layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  initialized_ = true;
  return true;
}

void M2ProjectedTextureDecalRenderer::Shutdown() {
  if (bgfx::isValid(program_)) bgfx::destroy(program_);
  if (bgfx::isValid(s_tex_)) bgfx::destroy(s_tex_);
  if (bgfx::isValid(u_decal_params_)) bgfx::destroy(u_decal_params_);
  program_ = BGFX_INVALID_HANDLE;
  s_tex_ = BGFX_INVALID_HANDLE;
  u_decal_params_ = BGFX_INVALID_HANDLE;
  geometry_.clear();
  initialized_ = false;
}

void M2ProjectedTextureDecalRenderer::Render(
    const std::uint8_t view_id, const float* view_mtx, const float* proj_mtx,
    const std::span<const m2::M2ProjectedTextureDraw> draws,
    const FacetGather& gather) {
  if (!initialized_ || !gather || !bgfx::isValid(program_)) {
    return;
  }
  ++frame_stamp_;
  if (!draws.empty()) {
    bgfx::setViewTransform(view_id, view_mtx, proj_mtx);
    for (const auto& draw : draws) {
      SubmitDraw(view_id, draw, gather);
    }
  }

  for (auto it = geometry_.begin(); it != geometry_.end();) {
    if (it->second.frame_stamp + 2u < frame_stamp_) {
      it = geometry_.erase(it);
    } else {
      ++it;
    }
  }
}

void M2ProjectedTextureDecalRenderer::SubmitDraw(
    const std::uint8_t view_id, const m2::M2ProjectedTextureDraw& draw,
    const FacetGather& gather) {
  if (!bgfx::isValid(draw.texture)) {
    return;
  }

  const std::uint64_t memo_key =
      (static_cast<std::uint64_t>(draw.instance_id) << 32) | draw.batch_index;
  auto& memo = geometry_[memo_key];
  memo.frame_stamp = frame_stamp_;

  float drift_squared = 0.0f;
  for (std::size_t i = 0; i < memo.bounds.size(); ++i) {
    const float d = memo.bounds[i] - draw.world_bounds[i];
    drift_squared += d * d;
  }
  if (drift_squared > kCacheSlopSquared) {
    memo.bounds = draw.world_bounds;
    memo.vertices.clear();
    const auto& bounds = draw.world_bounds;
    const float box_center_z = 0.5f * (bounds[2] + bounds[5]);
    const float box_z_half = 0.5f * (bounds[5] - bounds[2]);

    gather(bounds, [&](const world::CollisionFacetView& facet) {
      if (memo.vertices.size() >= kMaxTrianglesPerDecal * 3u) {
        return;
      }

      if (!DecalFacetIsUpward(facet.normal)) {
        return;
      }
      float min_x = facet.vertices[0][0], max_x = min_x;
      float min_y = facet.vertices[0][1], max_y = min_y;
      float min_z = facet.vertices[0][2], max_z = min_z;
      for (std::size_t i = 1; i < 3; ++i) {
        min_x = std::min(min_x, facet.vertices[i][0]);
        max_x = std::max(max_x, facet.vertices[i][0]);
        min_y = std::min(min_y, facet.vertices[i][1]);
        max_y = std::max(max_y, facet.vertices[i][1]);
        min_z = std::min(min_z, facet.vertices[i][2]);
        max_z = std::max(max_z, facet.vertices[i][2]);
      }
      if (max_x < bounds[0] || min_x > bounds[3] || max_y < bounds[1] ||
          min_y > bounds[4] || max_z < bounds[2] || min_z > bounds[5]) {
        return;
      }
      for (const auto& vertex : facet.vertices) {

        const float dx = vertex[0] - draw.reference_xy[0];
        const float dy = vertex[1] - draw.reference_xy[1];
        memo.vertices.push_back(DecalVertex{
            vertex[0], vertex[1], vertex[2],
            draw.reference_uv[0] + draw.uv_jacobian[0] * dx +
                draw.uv_jacobian[2] * dy,
            draw.reference_uv[1] + draw.uv_jacobian[1] * dx +
                draw.uv_jacobian[3] * dy,
            DecalVerticalFadeCoord(vertex[2], box_center_z, box_z_half),
            0u});
      }
    });
  }

  auto& vertices = memo.vertices;
  if (vertices.empty()) {
    return;
  }

  const std::uint32_t abgr = PackAbgr(draw.color);
  for (auto& vertex : vertices) {
    vertex.abgr = abgr;
  }

  const auto vertex_count = static_cast<std::uint32_t>(vertices.size());
  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  if (bgfx::getAvailTransientVertexBuffer(vertex_count, layout_) < vertex_count ||
      bgfx::getAvailTransientIndexBuffer(vertex_count) < vertex_count) {
    return;
  }
  bgfx::allocTransientVertexBuffer(&tvb, vertex_count, layout_);
  bgfx::allocTransientIndexBuffer(&tib, vertex_count);
  std::memcpy(tvb.data, vertices.data(), vertex_count * sizeof(DecalVertex));
  auto* indices = reinterpret_cast<std::uint16_t*>(tib.data);
  for (std::uint32_t i = 0; i < vertex_count; ++i) {
    indices[i] = static_cast<std::uint16_t>(i);
  }

  const float decal_params[4]{ResolveDecalDepthBias(), 0.0f, 0.0f, 0.0f};
  bgfx::setUniform(u_decal_params_, decal_params);
  bgfx::setState(draw.render_state & ~(BGFX_STATE_WRITE_Z | BGFX_STATE_CULL_MASK));
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setIndexBuffer(&tib);
  bgfx::setTexture(0, s_tex_, draw.texture,
                   static_cast<std::uint32_t>(draw.sampler_flags));
  bgfx::submit(view_id, program_);
}

}

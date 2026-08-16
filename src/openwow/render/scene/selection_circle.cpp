
#include "openwow/render/scene/selection_circle.h"

#include "openwow/render/scene/selection_decal_math.h"

#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::render {

bool SelectionCircle::Initialize(TextureManager& texture_manager) {
  if (initialized_) return true;

  program_ = CreateEmbeddedProgram(ShaderProgramId::Decal,
                                   bgfx::getRendererType());
  if (!bgfx::isValid(program_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "SelectionCircle: decal shader program load failed");
    return false;
  }
  s_tex_ = bgfx::createUniform("s_decalTex", bgfx::UniformType::Sampler);
  u_decal_params_ =
      bgfx::createUniform("u_decalParams", bgfx::UniformType::Vec4);

  decal_texture_lease_ = texture_manager.AcquireTexture(kSelectionDecalTexturePath);
  if (!decal_texture_lease_) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "SelectionCircle: UnitSelectTexture.blp load failed");
  }

  layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)

      .add(bgfx::Attrib::TexCoord0, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "SelectionCircle: initialized");
  return true;
}

void SelectionCircle::Shutdown() {
  if (!initialized_) return;

  if (bgfx::isValid(program_)) bgfx::destroy(program_);
  if (bgfx::isValid(s_tex_)) bgfx::destroy(s_tex_);
  if (bgfx::isValid(u_decal_params_)) bgfx::destroy(u_decal_params_);
  program_ = BGFX_INVALID_HANDLE;
  s_tex_ = BGFX_INVALID_HANDLE;
  u_decal_params_ = BGFX_INVALID_HANDLE;
  decal_texture_lease_ = TextureLease();
  decals_.clear();
  vertices_.clear();
  vertices_.shrink_to_fit();

  initialized_ = false;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "SelectionCircle: shutdown");
}

void SelectionCircle::BeginFrame() { decals_.clear(); }

void SelectionCircle::Submit(const SelectionDecal& decal) {
  decals_.push_back(decal);
}

void SelectionCircle::Render(const std::uint8_t view_id, const float* view_mtx,
                             const float* proj_mtx, const float cam_x,
                             const float cam_y, const float cam_z,
                             const FacetGather& gather) {
  (void)cam_z;
  if (!initialized_ || decals_.empty() || !gather) return;
  if (!bgfx::isValid(program_) || !decal_texture_lease_) return;
  if (!bgfx::isValid(BgfxTextureLeaseAccess::Get(decal_texture_lease_))) return;

  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);
  for (const auto& decal : decals_) {
    SubmitDraw(view_id, decal, cam_x, cam_y, gather);
  }
}

void SelectionCircle::SubmitDraw(const std::uint8_t view_id,
                                 const SelectionDecal& decal,
                                 const float cam_x, const float cam_y,
                                 const FacetGather& gather) {

  if (2.0f * decal.xy_half_extent < kMinFootprintExtent) return;
  if (!(decal.z_half_extent > 0.0f)) return;

  const std::array<float, 6> bounds{
      decal.center_x - decal.xy_half_extent,
      decal.center_y - decal.xy_half_extent,
      decal.center_z - decal.z_half_extent,
      decal.center_x + decal.xy_half_extent,
      decal.center_y + decal.xy_half_extent,
      decal.center_z + decal.z_half_extent};

  const std::uint32_t a = (decal.packed_argb >> 24) & 0xFFu;
  const std::uint32_t r = (decal.packed_argb >> 16) & 0xFFu;
  const std::uint32_t g = (decal.packed_argb >> 8) & 0xFFu;
  const std::uint32_t b = decal.packed_argb & 0xFFu;
  const std::uint32_t abgr = (a << 24) | (b << 16) | (g << 8) | r;

  vertices_.clear();
  gather(bounds, [&](const world::CollisionFacetView& facet) {
    if (vertices_.size() >= kMaxTriangles * 3u) {
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
      const auto uv = SelectionDecalFootprintUv(
          vertex[0], vertex[1], decal.center_x, decal.center_y,
          decal.xy_half_extent, cam_x, cam_y);
      vertices_.push_back(DecalVertex{
          vertex[0], vertex[1], vertex[2], uv.u, uv.v,
          DecalVerticalFadeCoord(vertex[2], decal.center_z,
                                 decal.z_half_extent),
          abgr});
    }
  });

  if (vertices_.empty()) {
    return;
  }

  const auto vertex_count = static_cast<std::uint32_t>(vertices_.size());
  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  if (bgfx::getAvailTransientVertexBuffer(vertex_count, layout_) < vertex_count ||
      bgfx::getAvailTransientIndexBuffer(vertex_count) < vertex_count) {
    return;
  }
  bgfx::allocTransientVertexBuffer(&tvb, vertex_count, layout_);
  bgfx::allocTransientIndexBuffer(&tib, vertex_count);
  std::memcpy(tvb.data, vertices_.data(), vertex_count * sizeof(DecalVertex));
  auto* indices = reinterpret_cast<std::uint16_t*>(tib.data);
  for (std::uint32_t i = 0; i < vertex_count; ++i) {
    indices[i] = static_cast<std::uint16_t>(i);
  }

  const float decal_params[4]{ResolveDecalDepthBias(), 0.0f, 0.0f, 0.0f};
  bgfx::setUniform(u_decal_params_, decal_params);

  const std::uint64_t state =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                            BGFX_STATE_BLEND_INV_SRC_ALPHA);
  bgfx::setState(state);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setIndexBuffer(&tib);
  bgfx::setTexture(0, s_tex_, BgfxTextureLeaseAccess::Get(decal_texture_lease_),
                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  bgfx::submit(view_id, program_);
}

}


#include "openwow/render/scene/blob_shadow.h"

#include "openwow/data/formats/dbc/dbc_loader.h"

#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace openwow::render {

bool BlobShadowRenderer::Initialize() {
  if (initialized_) return true;

  program_ = CreateEmbeddedProgram(ShaderProgramId::Decal,
                                   bgfx::getRendererType());
  if (!bgfx::isValid(program_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "BlobShadowRenderer: Failed to load decal program");
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

  CreateShadowTexture();

  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "BlobShadowRenderer: initialized");
  return true;
}

void BlobShadowRenderer::Shutdown() {
  if (bgfx::isValid(shadow_tex_)) {
    bgfx::destroy(shadow_tex_);
    shadow_tex_ = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(program_)) bgfx::destroy(program_);
  if (bgfx::isValid(s_tex_)) bgfx::destroy(s_tex_);
  if (bgfx::isValid(u_decal_params_)) bgfx::destroy(u_decal_params_);
  program_ = BGFX_INVALID_HANDLE;
  s_tex_ = BGFX_INVALID_HANDLE;
  u_decal_params_ = BGFX_INVALID_HANDLE;
  initialized_ = false;
}

void BlobShadowRenderer::CreateShadowTexture() {

  static constexpr float kProfileDistances[] = {5.5f, 6.5f,  7.5f,  8.5f,
                                                9.5f, 10.5f, 11.5f, 12.5f};
  static constexpr float kProfileRgb[] = {160.0f, 169.0f, 183.0f, 200.0f,
                                          217.0f, 232.0f, 249.0f, 255.0f};
  static constexpr std::size_t kProfileCount =
      sizeof(kProfileDistances) / sizeof(kProfileDistances[0]);

  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(kTexSize) * kTexSize * 4);
  const float center = (static_cast<float>(kTexSize) - 1.0f) * 0.5f;
  for (int y = 0; y < kTexSize; ++y) {
    for (int x = 0; x < kTexSize; ++x) {
      const float dx = static_cast<float>(x) - center;
      const float dy = static_cast<float>(y) - center;
      const float d = std::sqrt(dx * dx + dy * dy);
      float rgb = kProfileRgb[kProfileCount - 1];
      if (d <= kProfileDistances[0]) {
        rgb = kProfileRgb[0];
      } else if (d < kProfileDistances[kProfileCount - 1]) {
        for (std::size_t i = 1; i < kProfileCount; ++i) {
          if (d <= kProfileDistances[i]) {
            const float t = (d - kProfileDistances[i - 1]) /
                            (kProfileDistances[i] - kProfileDistances[i - 1]);
            rgb = kProfileRgb[i - 1] +
                  (kProfileRgb[i] - kProfileRgb[i - 1]) * t;
            break;
          }
        }
      }
      const auto a = static_cast<std::uint8_t>(255.0f - rgb + 0.5f);
      const std::size_t idx =
          (static_cast<std::size_t>(y) * kTexSize + x) * 4;
      pixels[idx + 0] = 0;
      pixels[idx + 1] = 0;
      pixels[idx + 2] = 0;
      pixels[idx + 3] = a;
    }
  }

  shadow_tex_ = bgfx::createTexture2D(
      static_cast<std::uint16_t>(kTexSize),
      static_cast<std::uint16_t>(kTexSize), false, 1,
      bgfx::TextureFormat::RGBA8, 0,
      bgfx::copy(pixels.data(),
                  static_cast<std::uint32_t>(pixels.size())));
}

void BlobShadowRenderer::Render(std::uint8_t view_id, const float* view_mtx,
                                const float* proj_mtx,
                                const game::ObjectPresentationSnapshot& objects,
                                const data::dbc::DbcLoader* dbc,
                                const FacetGather& gather) {
  if (!initialized_ || dbc == nullptr || !gather) return;
  if (!bgfx::isValid(program_) || !bgfx::isValid(shadow_tex_)) return;

  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);

  const auto& displays = dbc->creature_display_info();
  const auto& models = dbc->creature_model_data();
  const auto resolve_model_data =
      [&](const std::uint32_t display_id)
      -> const data::dbc::CreatureModelDataEntry* {
    const auto* display = displays.LookupEntry(display_id);
    return display != nullptr ? models.LookupEntry(display->model_id) : nullptr;
  };

  for (const auto& obj : objects.active) {

    if (obj.type_id != game::TypeID::kPlayer &&
        obj.type_id != game::TypeID::kUnit) {
      continue;
    }

    static constexpr std::uint32_t kUnitFlags2SuppressBlobShadow = 0x1u;
    if (obj.health == 0u ||
        (obj.unit_flags2 & kUnitFlags2SuppressBlobShadow) != 0u) {
      continue;
    }

    static constexpr std::uint8_t kUnitStandStateSubmerged = 9u;
    if (obj.stand_state == kUnitStandStateSubmerged) {
      continue;
    }

    const auto* own = resolve_model_data(obj.display_id);
    if (own == nullptr) {
      continue;
    }
    float bmin[3] = {own->geo_box_min[0], own->geo_box_min[1],
                     own->geo_box_min[2]};
    float bmax[3] = {own->geo_box_max[0], own->geo_box_max[1],
                     own->geo_box_max[2]};
    if (obj.mount_display_id != 0u) {
      if (const auto* mount = resolve_model_data(obj.mount_display_id);
          mount != nullptr) {
        const float shift =
            -(bmax[2] - bmin[2]) * 0.5f + mount->mount_height;
        bmin[2] += shift;
        bmax[2] += shift;
        bmin[0] = std::min(bmin[0], mount->geo_box_min[0]);
        bmin[1] = std::min(bmin[1], mount->geo_box_min[1]);
        bmin[2] = std::min(bmin[2], mount->geo_box_min[2]);
        bmax[0] = std::max(bmax[0], mount->geo_box_max[0]);
        bmax[1] = std::max(bmax[1], mount->geo_box_max[1]);
        bmax[2] = std::max(bmax[2], mount->geo_box_max[2]);
      }
    }

    const float scale = obj.scale;
    if (scale <= 0.0f) {
      continue;
    }
    const float half_x = std::min((bmax[0] - bmin[0]) * 0.5f * scale,
                                  kShadowXyHalfExtentClamp);
    const float half_y = std::min((bmax[1] - bmin[1]) * 0.5f * scale,
                                  kShadowXyHalfExtentClamp);
    if (half_x <= 0.0f || half_y <= 0.0f) {
      continue;
    }
    const float clamp5 = kShadowXyHalfExtentClamp;
    const float min_z_scaled = std::clamp(bmin[2] * scale, -clamp5, clamp5);
    const float max_z_scaled = std::clamp(bmax[2] * scale, -clamp5, clamp5);
    const float half_z = (max_z_scaled - min_z_scaled) * 0.5f;
    if (half_z <= 0.0f) {
      continue;
    }

    const float cos_f = std::cos(obj.facing);
    const float sin_f = std::sin(obj.facing);
    const float c1x = half_x * cos_f - half_y * sin_f;
    const float c1y = half_x * sin_f + half_y * cos_f;
    const float c2x = half_x * cos_f + half_y * sin_f;
    const float c2y = half_x * sin_f - half_y * cos_f;
    const float ext_x = std::max(std::fabs(c1x), std::fabs(c2x));
    const float ext_y = std::max(std::fabs(c1y), std::fabs(c2y));

    const std::array<float, 6> box{
        obj.x - ext_x, obj.y - ext_y,
        obj.z - half_z * kShadowZBelowPivotFactor,
        obj.x + ext_x, obj.y + ext_y,
        obj.z + half_z * kShadowZAbovePivotFactor};

    const float opacity = std::clamp(obj.render_opacity, 0.0f, 1.0f);
    const auto vertex_alpha =
        static_cast<std::uint8_t>(opacity * 255.0f + 0.5f);
    if (vertex_alpha == 0u) {
      continue;
    }

    SubmitProjectedShadow(view_id, obj.handle.guid.GetRawValue(), box,
                          vertex_alpha, gather);
  }

  ++shadow_frame_stamp_;
  for (auto it = shadow_geometry_.begin(); it != shadow_geometry_.end();) {
    if (it->second.frame_stamp + 2u < shadow_frame_stamp_) {
      it = shadow_geometry_.erase(it);
    } else {
      ++it;
    }
  }
}

void BlobShadowRenderer::SubmitProjectedShadow(
    std::uint8_t view_id, const std::uint64_t guid,
    const std::array<float, 6>& box, const std::uint8_t vertex_alpha,
    const FacetGather& gather) {
  const std::uint32_t shadow_color =
      (static_cast<std::uint32_t>(vertex_alpha) << 24) | 0x00000000;

  auto& memo = shadow_geometry_[guid];
  memo.frame_stamp = shadow_frame_stamp_;
  float box_drift_sq = 0.0f;
  for (std::size_t i = 0; i < 6u; ++i) {
    const float d = memo.box[i] - box[i];
    box_drift_sq += d * d;
  }

  const bool memo_valid = !memo.vertices.empty() &&
                          memo.vertex_alpha == vertex_alpha &&
                          box_drift_sq <= kShadowCacheSlopSquared;
  if (!memo_valid) {
    memo.box = box;
    memo.vertex_alpha = vertex_alpha;
    memo.vertices.clear();

    const float inv_width = 1.0f / (box[3] - box[0]);
    const float inv_depth = 1.0f / (box[4] - box[1]);
    const float center_z = (box[2] + box[5]) * 0.5f;
    const float half_span_z = (box[5] - box[2]) * 0.5f;
    auto& vertices = memo.vertices;
    vertices.reserve(kMaxTrianglesPerShadow * 3u);

    gather(box, [&](const world::CollisionFacetView& facet) {
      if (vertices.size() >= kMaxTrianglesPerShadow * 3u) {
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
      if (max_x < box[0] || min_x > box[3] || max_y < box[1] ||
          min_y > box[4] || max_z < box[2] || min_z > box[5]) {
        return;
      }
      for (const auto& vertex : facet.vertices) {
        vertices.push_back(ShadowVertex{
            vertex[0], vertex[1], vertex[2],
            (vertex[0] - box[0]) * inv_width,
            (vertex[1] - box[1]) * inv_depth,
            DecalVerticalFadeCoord(vertex[2], center_z, half_span_z),
            shadow_color});
      }
    });
  }

  const auto& vertices = memo.vertices;
  if (vertices.empty()) {
    return;
  }

  const auto vertex_count = static_cast<std::uint32_t>(vertices.size());
  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  if (bgfx::getAvailTransientVertexBuffer(vertex_count, layout_) <
          vertex_count ||
      bgfx::getAvailTransientIndexBuffer(vertex_count) < vertex_count) {
    return;
  }
  bgfx::allocTransientVertexBuffer(&tvb, vertex_count, layout_);
  bgfx::allocTransientIndexBuffer(&tib, vertex_count);
  std::memcpy(tvb.data, vertices.data(), vertex_count * sizeof(ShadowVertex));
  auto* idx = reinterpret_cast<std::uint16_t*>(tib.data);
  for (std::uint32_t i = 0; i < vertex_count; ++i) {
    idx[i] = static_cast<std::uint16_t>(i);
  }

  const std::uint64_t state =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                             BGFX_STATE_BLEND_INV_SRC_ALPHA);

  const float decal_params[4]{ResolveDecalDepthBias(), 0.0f, 0.0f, 0.0f};
  bgfx::setUniform(u_decal_params_, decal_params);
  bgfx::setState(state);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setIndexBuffer(&tib);
  bgfx::setTexture(0, s_tex_, shadow_tex_,
                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  bgfx::submit(view_id, program_);
}

}

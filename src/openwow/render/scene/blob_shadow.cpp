
#include "openwow/render/scene/blob_shadow.h"

#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/render/scene/object_renderer.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace openwow::render {

namespace {

struct BlobShadowProjection {
  std::array<float, 6> box{};
  std::array<float, 2> reference_xy{};
  std::array<float, 4> uv_jacobian{};
};

[[nodiscard]] bool BuildBlobShadowProjection(
    const ModelSpatialQueryResult& spatial,
    BlobShadowProjection* const out) {
  if (out == nullptr) {
    return false;
  }

  const auto& bounds = spatial.local_bounds;
  if (!(bounds[0] < bounds[3]) || !(bounds[1] < bounds[4]) ||
      !(bounds[2] < bounds[5])) {
    return false;
  }
  if (!std::all_of(bounds.begin(), bounds.end(),
                   [](const float value) { return std::isfinite(value); }) ||
      !std::all_of(spatial.world_transform.begin(),
                   spatial.world_transform.end(),
                   [](const float value) { return std::isfinite(value); })) {
    return false;
  }

  const auto& matrix = spatial.world_transform;
  const float model_scale = std::sqrt(matrix[0] * matrix[0] +
                                      matrix[1] * matrix[1] +
                                      matrix[2] * matrix[2]);
  constexpr float kTransformEpsilon = 1.0e-6f;
  if (!(model_scale > kTransformEpsilon) || !std::isfinite(model_scale)) {
    return false;
  }

  constexpr float kHalfExtentClamp = 5.0f;
  const float half_x = std::min((bounds[3] - bounds[0]) * 0.5f * model_scale,
                                kHalfExtentClamp);
  const float half_y = std::min((bounds[4] - bounds[1]) * 0.5f * model_scale,
                                kHalfExtentClamp);
  if (!(half_x > kTransformEpsilon) || !(half_y > kTransformEpsilon)) {
    return false;
  }

  const float min_z =
      std::clamp(bounds[2] * model_scale, -kHalfExtentClamp, kHalfExtentClamp);
  const float max_z =
      std::clamp(bounds[5] * model_scale, -kHalfExtentClamp, kHalfExtentClamp);
  const float half_z = (max_z - min_z) * 0.5f;
  if (!(half_z > kTransformEpsilon)) {
    return false;
  }

  const float inv_scale = 1.0f / model_scale;
  const std::array<float, 2> x_axis{matrix[0] * inv_scale,
                                    matrix[1] * inv_scale};
  const std::array<float, 2> y_axis{matrix[4] * inv_scale,
                                    matrix[5] * inv_scale};
  const float determinant =
      x_axis[0] * y_axis[1] - x_axis[1] * y_axis[0];
  if (std::fabs(determinant) <= kTransformEpsilon) {
    return false;
  }

  const float center_x = matrix[12];
  const float center_y = matrix[13];
  float min_x = center_x;
  float min_y = center_y;
  float max_x = center_x;
  float max_y = center_y;
  for (const float local_x : {-half_x, half_x}) {
    for (const float local_y : {-half_y, half_y}) {
      const float world_x =
          center_x + local_x * x_axis[0] + local_y * y_axis[0];
      const float world_y =
          center_y + local_x * x_axis[1] + local_y * y_axis[1];
      min_x = std::min(min_x, world_x);
      min_y = std::min(min_y, world_y);
      max_x = std::max(max_x, world_x);
      max_y = std::max(max_y, world_y);
    }
  }

  constexpr float kBelowPivotFactor = 1.6666667f;
  constexpr float kAbovePivotFactor = 1.0f;
  out->box = {min_x, min_y, matrix[14] - half_z * kBelowPivotFactor,
              max_x, max_y, matrix[14] + half_z * kAbovePivotFactor};
  out->reference_xy = {center_x, center_y};

  const float inverse_determinant = 1.0f / determinant;
  const float inverse_width = 1.0f / (2.0f * half_x);
  const float inverse_depth = 1.0f / (2.0f * half_y);
  out->uv_jacobian = {
      y_axis[1] * inverse_determinant * inverse_width,
      -x_axis[1] * inverse_determinant * inverse_depth,
      -y_axis[0] * inverse_determinant * inverse_width,
      x_axis[0] * inverse_determinant * inverse_depth,
  };
  return true;
}

}

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
                                const ObjectRenderer& object_renderer,
                                const FacetGather& gather) {
  if (!initialized_ || !gather) return;
  if (!bgfx::isValid(program_) || !bgfx::isValid(shadow_tex_)) return;

  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);

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

    ModelSpatialQueryResult spatial{};
    if (!object_renderer.QueryModelSpatialState(obj.handle.guid, &spatial)) {
      continue;
    }
    BlobShadowProjection projection{};
    if (!BuildBlobShadowProjection(spatial, &projection)) {
      continue;
    }

    const float opacity = std::clamp(obj.render_opacity, 0.0f, 1.0f);
    const auto vertex_alpha =
        static_cast<std::uint8_t>(opacity * 255.0f + 0.5f);
    if (vertex_alpha == 0u) {
      continue;
    }

    SubmitProjectedShadow(view_id, obj.handle.guid.GetRawValue(),
                          projection.box, projection.reference_xy,
                          projection.uv_jacobian, vertex_alpha, gather);
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
    const std::array<float, 6>& box,
    const std::array<float, 2>& reference_xy,
    const std::array<float, 4>& uv_jacobian,
    const std::uint8_t vertex_alpha,
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
  for (std::size_t i = 0; i < reference_xy.size(); ++i) {
    const float d = memo.reference_xy[i] - reference_xy[i];
    box_drift_sq += d * d;
  }
  for (std::size_t i = 0; i < uv_jacobian.size(); ++i) {
    const float d = memo.uv_jacobian[i] - uv_jacobian[i];
    box_drift_sq += d * d;
  }

  const bool memo_valid = !memo.vertices.empty() &&
                          memo.vertex_alpha == vertex_alpha &&
                          box_drift_sq <= kShadowCacheSlopSquared;
  if (!memo_valid) {
    memo.box = box;
    memo.reference_xy = reference_xy;
    memo.uv_jacobian = uv_jacobian;
    memo.vertex_alpha = vertex_alpha;
    memo.vertices.clear();

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
        const float dx = vertex[0] - reference_xy[0];
        const float dy = vertex[1] - reference_xy[1];
        vertices.push_back(ShadowVertex{
            vertex[0], vertex[1], vertex[2],
            0.5f + uv_jacobian[0] * dx + uv_jacobian[2] * dy,
            0.5f + uv_jacobian[1] * dx + uv_jacobian[3] * dy,
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

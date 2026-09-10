#include "openwow/render/scene/shadow_data.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/api/draw_encoder.h"
#include "openwow/world/world_render_pipeline.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>

#include <bgfx/bgfx.h>
#include <bx/math.h>

namespace openwow::render {

namespace {

constexpr std::array<float, kWorldShadowProductCount> kProductHalfExtents{
    20.0f, 40.0f, 160.0f, 640.0f};
constexpr std::array<float, kWorldShadowProductCount> kRawReceiverBias{
    0.6f, 0.9f, 1.3f, 2.1f};
constexpr std::array<const char*, kWorldShadowProductCount> kSamplerNames{
    "s_worldShadow0", "s_worldShadow1", "s_worldShadow2",
    "s_worldShadow3"};
constexpr std::uint8_t kFirstShadowSamplerStage = 5u;
constexpr float kCasterDistance = 2000.0f;
constexpr float kCasterNear = 1.0f;
constexpr float kCasterFar = 4000.0f;
constexpr float kMaxLightUpAlignment = 0.99f;
std::atomic<const ShadowRenderData*> g_active_world_shadow_data{nullptr};

void BuildClipToTextureMatrix(float out[16]) {
  const bgfx::Caps* const caps = bgfx::getCaps();
  const float y_scale = caps->originBottomLeft ? 0.5f : -0.5f;
  const float depth_scale = caps->homogeneousDepth ? 0.5f : 1.0f;
  const float depth_offset = caps->homogeneousDepth ? 0.5f : 0.0f;
  const float matrix[16] = {
      0.5f, 0.0f,    0.0f,        0.0f,
      0.0f, y_scale, 0.0f,        0.0f,
      0.0f, 0.0f,    depth_scale, 0.0f,
      0.5f, 0.5f,    depth_offset, 1.0f,
  };
  std::copy_n(matrix, 16u, out);
}

}

struct ShadowRenderData::BackendResources {
  std::array<bgfx::TextureHandle, kWorldShadowProductCount> depth_textures{{
      BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
      BGFX_INVALID_HANDLE}};
  std::array<bgfx::FrameBufferHandle, kWorldShadowProductCount> framebuffers{{
      BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
      BGFX_INVALID_HANDLE}};
  std::array<bgfx::UniformHandle, kWorldShadowProductCount> samplers{{
      BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
      BGFX_INVALID_HANDLE}};
  bgfx::TextureHandle fallback_depth = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle matrices = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle parameters = BGFX_INVALID_HANDLE;
};

ShadowRenderData::ShadowRenderData()
    : backend_(std::make_unique<BackendResources>()) {
  light_views_.fill(kRenderIdentityMatrix4x4);
  light_projections_.fill(kRenderIdentityMatrix4x4);
  receiver_matrices_.fill(kRenderIdentityMatrix4x4);
}

ShadowRenderData::~ShadowRenderData() { DestroyResources(); }

void ShadowRenderData::Configure(const std::uint8_t quality,
                                 const std::uint16_t resolution) {
  quality_ = std::min<std::uint8_t>(quality, 5u);
  resolution_ = resolution >= 2048u ? 2048u : 1024u;
  published_product_count_ = 0u;
}

std::size_t ShadowRenderData::active_product_count() const noexcept {
  if (quality_ == 0u) {
    return 0u;
  }
  return quality_ < 3u ? 1u : kWorldShadowProductCount;
}

bool ShadowRenderData::CreateResources() {
  DestroyResources();
  const auto product_count = active_product_count();

  backend_->fallback_depth = bgfx::createTexture2D(
      1u, 1u, false, 1u, bgfx::TextureFormat::D16,
      BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL);
  backend_->matrices = bgfx::createUniform(
      "u_worldShadowMtx", bgfx::UniformType::Mat4,
      static_cast<std::uint16_t>(kWorldShadowProductCount));
  backend_->parameters =
      bgfx::createUniform("u_worldShadowParams", bgfx::UniformType::Vec4, 3u);

  bool valid = bgfx::isValid(backend_->fallback_depth) &&
               bgfx::isValid(backend_->matrices) &&
               bgfx::isValid(backend_->parameters);
  for (std::size_t index = 0u; index < kWorldShadowProductCount; ++index) {
    backend_->samplers[index] =
        bgfx::createUniform(kSamplerNames[index], bgfx::UniformType::Sampler);
    valid = valid && bgfx::isValid(backend_->samplers[index]);
  }
  for (std::size_t index = 0u; index < product_count; ++index) {
    backend_->framebuffers[index] = bgfx::createFrameBuffer(
        resolution_, resolution_, bgfx::TextureFormat::D16,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL);
    if (bgfx::isValid(backend_->framebuffers[index])) {
      backend_->depth_textures[index] =
          bgfx::getTexture(backend_->framebuffers[index]);
    }
    if (!bgfx::isValid(backend_->framebuffers[index]) ||
        !bgfx::isValid(backend_->depth_textures[index])) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kError,
          "ShadowRenderData: product resource creation failed product=" +
              std::to_string(index) + " resolution=" +
              std::to_string(resolution_));
      valid = false;
    }
  }
  if (!valid) {
    DestroyResources();
    return false;
  }

  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "ShadowRenderData: target-centered products created quality=" +
          std::to_string(quality_) + " products=" +
          std::to_string(product_count) + " resolution=" +
          std::to_string(resolution_));
  return true;
}

void ShadowRenderData::DestroyResources() {
  for (auto& framebuffer : backend_->framebuffers) {
    if (bgfx::isValid(framebuffer)) {
      bgfx::destroy(framebuffer);
      framebuffer = BGFX_INVALID_HANDLE;
    }
  }
  backend_->depth_textures.fill(BGFX_INVALID_HANDLE);
  for (auto& sampler : backend_->samplers) {
    if (bgfx::isValid(sampler)) {
      bgfx::destroy(sampler);
      sampler = BGFX_INVALID_HANDLE;
    }
  }
  if (bgfx::isValid(backend_->fallback_depth)) {
    bgfx::destroy(backend_->fallback_depth);
    backend_->fallback_depth = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(backend_->matrices)) {
    bgfx::destroy(backend_->matrices);
    backend_->matrices = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(backend_->parameters)) {
    bgfx::destroy(backend_->parameters);
    backend_->parameters = BGFX_INVALID_HANDLE;
  }
  published_product_count_ = 0u;
}

bool ShadowRenderData::resources_valid() const noexcept {
  const auto count = active_product_count();
  if (!bgfx::isValid(backend_->fallback_depth) ||
      !bgfx::isValid(backend_->matrices) ||
      !bgfx::isValid(backend_->parameters)) {
    return false;
  }
  for (std::size_t index = 0u; index < kWorldShadowProductCount; ++index) {
    if (!bgfx::isValid(backend_->samplers[index])) {
      return false;
    }
  }
  for (std::size_t index = 0u; index < count; ++index) {
    if (!bgfx::isValid(backend_->framebuffers[index]) ||
        !bgfx::isValid(backend_->depth_textures[index])) {
      return false;
    }
  }
  return true;
}

void ShadowRenderData::SetLightDirection(
    const RenderVec3& surface_to_light) noexcept {
  const float length_squared = surface_to_light[0] * surface_to_light[0] +
                               surface_to_light[1] * surface_to_light[1] +
                               surface_to_light[2] * surface_to_light[2];
  if (length_squared <= 1.0e-8f || !std::isfinite(length_squared)) {
    surface_to_light_ = {0.0f, -1.0f, 0.0f};
    return;
  }
  const float inverse_length = 1.0f / std::sqrt(length_squared);
  surface_to_light_ = {surface_to_light[0] * inverse_length,
                       surface_to_light[1] * inverse_length,
                       surface_to_light[2] * inverse_length};
}

bool ShadowRenderData::PrepareProduct(const std::size_t product_index,
                                      const RenderVec3& target) {
  if (!resources_valid() || product_index >= active_product_count() ||
      !std::all_of(target.begin(), target.end(),
                   [](const float value) { return std::isfinite(value); })) {
    return false;
  }

  product_targets_[product_index] = target;
  const RenderVec3 eye{
      target[0] + surface_to_light_[0] * kCasterDistance,
      target[1] + surface_to_light_[1] * kCasterDistance,
      target[2] + surface_to_light_[2] * kCasterDistance,
  };
  const bx::Vec3 reference_up =
      std::fabs(surface_to_light_[2]) > kMaxLightUpAlignment
          ? bx::Vec3(0.0f, 1.0f, 0.0f)
          : bx::Vec3(0.0f, 0.0f, 1.0f);
  auto& view = light_views_[product_index];
  auto& projection = light_projections_[product_index];
  bx::mtxLookAt(view.data(), bx::Vec3(eye[0], eye[1], eye[2]),
                bx::Vec3(target[0], target[1], target[2]), reference_up,
                bx::Handedness::Left);

  const float half_extent = kProductHalfExtents[product_index];
  bx::mtxOrtho(projection.data(), -half_extent, half_extent, -half_extent,
               half_extent, kCasterNear, kCasterFar, 0.0f,
               bgfx::getCaps()->homogeneousDepth);

  RenderMatrix4x4 receiver_projection{};
  bx::mtxOrtho(receiver_projection.data(), -half_extent, half_extent,
               -half_extent, half_extent, 0.0f, kCasterFar, 0.0f,
               bgfx::getCaps()->homogeneousDepth);
  RenderMatrix4x4 view_projection{};
  bx::mtxMul(view_projection.data(), view.data(), receiver_projection.data());
  RenderMatrix4x4 clip_to_texture{};
  BuildClipToTextureMatrix(clip_to_texture.data());
  bx::mtxMul(receiver_matrices_[product_index].data(), view_projection.data(),
             clip_to_texture.data());
  return true;
}

void ShadowRenderData::BeginShadowDepthPass(
    const std::size_t product_index, const std::uint8_t view_id) const {
  if (!resources_valid() || product_index >= active_product_count()) {
    return;
  }
  bgfx::setViewName(view_id, product_index == 0u ? "shadow_center"
                                                 : "shadow_exterior");
  bgfx::setViewMode(view_id, bgfx::ViewMode::Default);
  bgfx::setViewClear(view_id, BGFX_CLEAR_DEPTH, 0x00000000u, 1.0f, 0u);
  bgfx::setViewRect(view_id, 0u, 0u, resolution_, resolution_);
  bgfx::setViewFrameBuffer(view_id, backend_->framebuffers[product_index]);
  bgfx::setViewTransform(view_id, light_views_[product_index].data(),
                         light_projections_[product_index].data());
  bgfx::setViewScissor(view_id, 0u, 0u, resolution_, resolution_);
  bgfx::touch(view_id);
}

const float* ShadowRenderData::light_view(
    const std::size_t product_index) const {
  return product_index < kWorldShadowProductCount
             ? light_views_[product_index].data()
             : nullptr;
}

const float* ShadowRenderData::light_projection(
    const std::size_t product_index) const {
  return product_index < kWorldShadowProductCount
             ? light_projections_[product_index].data()
             : nullptr;
}

RenderVec3 ShadowRenderData::product_target(
    const std::size_t product_index) const {
  return product_index < kWorldShadowProductCount
             ? product_targets_[product_index]
             : RenderVec3{};
}

float ShadowRenderData::product_half_extent(
    const std::size_t product_index) const {
  return product_index < kWorldShadowProductCount
             ? kProductHalfExtents[product_index]
             : 0.0f;
}

void ShadowRenderData::SetPublishedProductCount(const std::size_t count) noexcept {
  published_product_count_ = std::min(count, active_product_count());
}

void ShadowRenderData::BindTerrainShadowState(bgfx::Encoder* const encoder) const {
  BindReceiverState(0u, true, encoder);
}

void ShadowRenderData::BindModelShadowState(
    const bool enabled, bgfx::Encoder* const encoder) const {
  BindReceiverState(1u, enabled && quality_ >= 3u, encoder);
}

void ShadowRenderData::BindReceiverState(const std::size_t first_product,
                                         const bool enabled,
                                         bgfx::Encoder* const encoder) const {
  if (!resources_valid()) {
    return;
  }
  const DrawEncoder draw{encoder};
  for (std::size_t index = 0u; index < kWorldShadowProductCount; ++index) {
    const bgfx::TextureHandle texture =
        index < published_product_count_ &&
                bgfx::isValid(backend_->depth_textures[index])
            ? backend_->depth_textures[index]
            : backend_->fallback_depth;
    draw.setTexture(static_cast<std::uint8_t>(kFirstShadowSamplerStage + index),
                    backend_->samplers[index], texture,
                    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                        BGFX_SAMPLER_COMPARE_LEQUAL);
  }
  draw.setUniform(backend_->matrices, receiver_matrices_.data(),
                  static_cast<std::uint16_t>(kWorldShadowProductCount));
  const bool active = enabled && published_product_count_ > first_product;
  const std::array<float, 4> shadow_mod =
      openwow::world::CWorld_GetTerrainShadowModColor();
  const std::array<RenderVec4, 3> parameters{{
      {static_cast<float>(published_product_count_),
       static_cast<float>(first_product), active ? 1.0f : 0.0f, 0.0f},
      {kRawReceiverBias[0] / kCasterFar,
       kRawReceiverBias[1] / kCasterFar,
       kRawReceiverBias[2] / kCasterFar,
       kRawReceiverBias[3] / kCasterFar},
      {shadow_mod[0], shadow_mod[1], shadow_mod[2], 0.0f},
  }};
  draw.setUniform(backend_->parameters, parameters.data(), 3u);
}

void SetActiveWorldShadowRenderData(const ShadowRenderData* const data) noexcept {
  g_active_world_shadow_data.store(data, std::memory_order_release);
}

void BindActiveWorldModelShadowState(const bool enabled,
                                     bgfx::Encoder* const encoder) noexcept {
  const ShadowRenderData* const data =
      g_active_world_shadow_data.load(std::memory_order_acquire);
  if (data != nullptr) {
    data->BindModelShadowState(enabled, encoder);
  }
}

}

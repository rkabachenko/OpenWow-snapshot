
#include "openwow/render/effects/postprocess/post_process.h"
#include "openwow/render/resources/textures/texture_surface_copy.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace openwow::render {

struct PosTexVertex {
  float x, y, z;
  float u, v;
};

static const PosTexVertex kQuadVertices[] = {
    {-1.0f, 1.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 0.0f, 1.0f, 0.0f},
    {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
    {1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
};

static const uint16_t kQuadIndices[] = {0, 2, 1, 1, 2, 3};

namespace {

std::uint16_t SaturatingUint16(const std::int64_t value) {
  return static_cast<std::uint16_t>(
      std::clamp<std::int64_t>(value, 0, std::numeric_limits<std::uint16_t>::max()));
}

std::uint8_t NormalizeMultisample(const std::uint8_t samples) noexcept {
  switch (samples) {
  case 2:
  case 4:
  case 8:
  case 16:
    return samples;
  default:
    return 1;
  }
}

std::uint64_t MultisampleTextureFlags(const std::uint8_t samples) noexcept {
  switch (samples) {
  case 2:
    return BGFX_TEXTURE_RT_MSAA_X2;
  case 4:
    return BGFX_TEXTURE_RT_MSAA_X4;
  case 8:
    return BGFX_TEXTURE_RT_MSAA_X8;
  case 16:
    return BGFX_TEXTURE_RT_MSAA_X16;
  default:
    return 0;
  }
}

}

std::optional<PostProcess::ClippedRect> PostProcess::ClipRectForOutput(Rect dest_rect) {
  if (dest_rect.width <= 0 || dest_rect.height <= 0) {
    return std::nullopt;
  }

  const std::int64_t src_x0 = dest_rect.x;
  const std::int64_t src_y0 = dest_rect.y;
  const std::int64_t src_x1 = src_x0 + dest_rect.width;
  const std::int64_t src_y1 = src_y0 + dest_rect.height;

  std::int64_t dst_x0 = src_x0;
  std::int64_t dst_y0 = src_y0;
  std::int64_t dst_x1 = src_x1;
  std::int64_t dst_y1 = src_y1;

  if (dest_rect.output_width > 0) {
    dst_x0 = std::max<std::int64_t>(dst_x0, 0);
    dst_x1 = std::min<std::int64_t>(dst_x1, dest_rect.output_width);
  }
  if (dest_rect.output_height > 0) {
    dst_y0 = std::max<std::int64_t>(dst_y0, 0);
    dst_y1 = std::min<std::int64_t>(dst_y1, dest_rect.output_height);
  }

  if (dst_x0 < 0 || dst_y0 < 0 || dst_x1 <= dst_x0 || dst_y1 <= dst_y0) {
    return std::nullopt;
  }

  const float inv_width = 1.0f / static_cast<float>(dest_rect.width);
  const float inv_height = 1.0f / static_cast<float>(dest_rect.height);
  return ClippedRect{
      .x = SaturatingUint16(dst_x0),
      .y = SaturatingUint16(dst_y0),
      .width = SaturatingUint16(dst_x1 - dst_x0),
      .height = SaturatingUint16(dst_y1 - dst_y0),
      .u0 = static_cast<float>(dst_x0 - src_x0) * inv_width,
      .v0 = static_cast<float>(dst_y0 - src_y0) * inv_height,
      .u1 = static_cast<float>(dst_x1 - src_x0) * inv_width,
      .v1 = static_cast<float>(dst_y1 - src_y0) * inv_height,
  };
}

void PostProcess::SyncDeathEffectState() {
  state_.death_enabled = state_.ffx_enabled && death_requested_enabled_ &&
                         death_cvar_enabled_;
  target_death_intensity_ =
      state_.death_enabled ? requested_death_intensity_ : 0.0f;
  if (!state_.death_enabled) {
    current_death_intensity_ = 0.0f;
    state_.death_intensity = 0.0f;
  }
}

bool PostProcess::CanRenderDeathEffect() const {
  return IsDeathEffectActive() && bgfx::isValid(prog_death_) &&
         bgfx::isValid(death_fb_) && bgfx::isValid(death_tex_) &&
         bgfx::isValid(u_deathParams_) && bgfx::isValid(u_texColor_) &&
         bgfx::isValid(u_texBloom_) && bgfx::isValid(u_sourceUvScale_) &&
         CanRenderSceneBlur();
}

bool PostProcess::CanRenderSceneBlur() const {
  return bgfx::isValid(prog_box4_) &&
         bgfx::isValid(prog_gauss4_h_) && bgfx::isValid(prog_gauss4_v_) &&
         bgfx::isValid(quarter_fb_) && bgfx::isValid(quarter_tex_) &&
         bgfx::isValid(quarter_scratch_fb_) &&
         bgfx::isValid(quarter_scratch_tex_) &&
         bgfx::isValid(u_texelSize_) && bgfx::isValid(u_texColor_);
}

bool PostProcess::CanRenderComposite() const {
  return bgfx::isValid(prog_glow_combine_) && bgfx::isValid(quad_vbh_) &&
         bgfx::isValid(quad_ibh_) && bgfx::isValid(u_texColor_) &&
         bgfx::isValid(u_texBloom_) && bgfx::isValid(u_compositeParams_) &&
         bgfx::isValid(u_colorGrade_) && bgfx::isValid(u_sourceUvScale_);
}

bool PostProcess::CanCaptureScene() const {
  return HasPostProcessFinalCopyPath({
      .scene_target = bgfx::isValid(scene_fb_) && bgfx::isValid(scene_tex_),
      .index_buffer = bgfx::isValid(quad_ibh_),
      .color_sampler = bgfx::isValid(u_texColor_),
      .blit_program = bgfx::isValid(prog_blit_),
      .composite_program = bgfx::isValid(prog_glow_combine_),
      .composite_vertex_buffer = bgfx::isValid(quad_vbh_),
      .composite_uniforms = bgfx::isValid(u_texBloom_) &&
                            bgfx::isValid(u_compositeParams_) &&
                            bgfx::isValid(u_colorGrade_) &&
                            bgfx::isValid(u_sourceUvScale_),
  });
}

PostProcess::PostProcess() = default;

PostProcess::~PostProcess() {
  Shutdown();
}

void PostProcess::Init(uint32_t width, uint32_t height, const PostProcessSettings settings) {
  width_ = std::max(1u, width);
  height_ = std::max(1u, height);
  state_ = {};
  world_view_effect_state_ = {};
  screen_effect_state_ = {};
  death_requested_enabled_ = false;
  death_cvar_enabled_ = settings.enabled && settings.death_enabled;
  rectangle_textures_ = settings.rectangle_textures;
  multisample_ = NormalizeMultisample(settings.multisample);
  state_.ffx_enabled = settings.enabled;
  state_.glow_enabled = settings.enabled && settings.glow_enabled;
  requested_death_intensity_ = 0.0f;
  target_death_intensity_ = 0.0f;
  current_death_intensity_ = 0.0f;
  scene_capture_active_ = false;
  initialized_ = true;

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "PostProcess: initialized " + std::to_string(width_) +
                     "x" + std::to_string(height_) +
                     (gpu_ready_ ? " (GPU ready)" : " (GPU deferred)"));
}

void PostProcess::InitGPU() {
  if (!initialized_) return;

  CreateResources();

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     std::string("PostProcess: GPU resources ") +
                     (gpu_ready_ ? "created" : "unavailable"));
}

bool PostProcess::Resize(uint32_t width, uint32_t height) {
  const uint32_t clamped_width = std::max(1u, width);
  const uint32_t clamped_height = std::max(1u, height);
  if (clamped_width == width_ && clamped_height == height_) {
    return false;
  }

  width_ = clamped_width;
  height_ = clamped_height;

  if (bgfx::isValid(scene_fb_)) {
    DestroyFramebuffers();
    CreateFramebuffers();
    gpu_ready_ = CanCaptureScene();
  }
  return true;
}

void PostProcess::Shutdown() {
  ReleaseRendererDeviceResources();
  state_ = {};
  world_view_effect_state_ = {};
  screen_effect_state_ = {};
  death_requested_enabled_ = false;
  death_cvar_enabled_ = true;
  requested_death_intensity_ = 0.0f;
  target_death_intensity_ = 0.0f;
  current_death_intensity_ = 0.0f;
  initialized_ = false;
  gpu_ready_ = false;
  scene_capture_active_ = false;
}

void PostProcess::ReleaseRendererDeviceResources() {
  DestroyResources();
  gpu_ready_ = false;
  scene_capture_active_ = false;
}

bool PostProcess::RestoreRendererDeviceResources() {
  if (!initialized_) {
    return false;
  }
  CreateResources();
  return gpu_ready_;
}

void PostProcess::Update(float dt) {
  if (!initialized_) return;

  if (current_death_intensity_ < target_death_intensity_) {
    current_death_intensity_ = std::min(
        current_death_intensity_ + kDeathFadeSpeed * dt,
        target_death_intensity_);
  } else if (current_death_intensity_ > target_death_intensity_) {
    current_death_intensity_ = std::max(
        current_death_intensity_ - kDeathFadeSpeed * dt,
        target_death_intensity_);
  }
  state_.death_intensity = current_death_intensity_;

}

void PostProcess::CreateResources() {

  quad_layout_.begin()
      .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();

  quad_vbh_ = bgfx::createVertexBuffer(
      bgfx::makeRef(kQuadVertices, sizeof(kQuadVertices)),
      quad_layout_);

  quad_ibh_ = bgfx::createIndexBuffer(
      bgfx::makeRef(kQuadIndices, sizeof(kQuadIndices)));

  u_texColor_        = bgfx::createUniform("s_ppTexColor",        bgfx::UniformType::Sampler);
  u_texBloom_        = bgfx::createUniform("s_ppTexBloom",         bgfx::UniformType::Sampler);
  u_deathParams_     = bgfx::createUniform("u_deathParams",      bgfx::UniformType::Vec4);
  u_texelSize_       = bgfx::createUniform("u_texelSize",        bgfx::UniformType::Vec4);
  u_compositeParams_ = bgfx::createUniform("u_compositeParams",  bgfx::UniformType::Vec4);
  u_colorGrade_      = bgfx::createUniform("u_colorGrade",       bgfx::UniformType::Vec4);
  u_sourceUvScale_   = bgfx::createUniform("u_sourceUvScale",    bgfx::UniformType::Vec4);

  gpu_ready_ = false;

  auto type = bgfx::getRendererType();

  prog_death_ = CreateEmbeddedProgram(ShaderProgramId::PostProcessDeath, type);
  prog_blit_ = CreateEmbeddedProgram(ShaderProgramId::PostProcessBlit, type);
  prog_box4_ = CreateEmbeddedProgram(ShaderProgramId::PostProcessBox4, type);
  prog_gauss4_h_ = CreateEmbeddedProgram(ShaderProgramId::PostProcessBlurH, type);
  prog_gauss4_v_ = CreateEmbeddedProgram(ShaderProgramId::PostProcessBlurV, type);

  prog_glow_combine_ = CreateEmbeddedProgram(ShaderProgramId::PostProcessComposite, type);

  CreateFramebuffers();

  gpu_ready_ = CanCaptureScene();
}

void PostProcess::CreateFramebuffers() {
  const uint64_t tex_flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
  const std::uint64_t scene_msaa_flags = MultisampleTextureFlags(multisample_);

  const auto *caps = bgfx::getCaps();
  const std::uint32_t maximum_dimension = std::min<std::uint32_t>(
      caps != nullptr ? caps->limits.maxTextureSize : std::numeric_limits<std::uint16_t>::max(),
      std::numeric_limits<std::uint16_t>::max());

  const auto texture_mode = SelectFfxTextureMode(rectangle_textures_, true, true, true);
  const auto targets =
      ResolveFfxTargetPyramid({.width = width_, .height = height_}, texture_mode,
                              {.width = maximum_dimension, .height = maximum_dimension});
  scene_target_ = targets.full;
  quarter_target_ = targets.quarter;

  scene_tex_ = bgfx::createTexture2D(static_cast<uint16_t>(scene_target_.backing.width),
                                     static_cast<uint16_t>(scene_target_.backing.height), false, 1,
                                     bgfx::TextureFormat::RGBA8, tex_flags | scene_msaa_flags);

  scene_depth_ = bgfx::createTexture2D(
      static_cast<uint16_t>(scene_target_.backing.width),
      static_cast<uint16_t>(scene_target_.backing.height), false, 1, bgfx::TextureFormat::D24S8,
      BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY | scene_msaa_flags);

  bgfx::TextureHandle scene_attachments[] = {scene_tex_, scene_depth_};
  scene_fb_ = bgfx::createFrameBuffer(2, scene_attachments, true);

  const uint16_t qw = static_cast<uint16_t>(quarter_target_.backing.width);
  const uint16_t qh = static_cast<uint16_t>(quarter_target_.backing.height);

  quarter_tex_ = bgfx::createTexture2D(qw, qh, false, 1, bgfx::TextureFormat::RGBA8, tex_flags);
  bgfx::TextureHandle q_att[] = {quarter_tex_};
  quarter_fb_ = bgfx::createFrameBuffer(1, q_att, true);

  quarter_scratch_tex_ = bgfx::createTexture2D(qw, qh, false, 1,
                                                bgfx::TextureFormat::RGBA8, tex_flags);
  bgfx::TextureHandle qs_att[] = {quarter_scratch_tex_};
  quarter_scratch_fb_ = bgfx::createFrameBuffer(1, qs_att, true);

  death_tex_ = bgfx::createTexture2D(
      static_cast<uint16_t>(scene_target_.backing.width),
      static_cast<uint16_t>(scene_target_.backing.height),
      false, 1, bgfx::TextureFormat::RGBA8, tex_flags);
  bgfx::TextureHandle d_att[] = {death_tex_};
  death_fb_ = bgfx::createFrameBuffer(1, d_att, true);
}

void PostProcess::DestroyFramebuffers() {
  auto destroyFB = [](bgfx::FrameBufferHandle& h) {
    if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; }
  };

  destroyFB(scene_fb_);          scene_tex_ = BGFX_INVALID_HANDLE; scene_depth_ = BGFX_INVALID_HANDLE;
  destroyFB(quarter_fb_);        quarter_tex_ = BGFX_INVALID_HANDLE;
  destroyFB(quarter_scratch_fb_); quarter_scratch_tex_ = BGFX_INVALID_HANDLE;
  destroyFB(death_fb_);          death_tex_ = BGFX_INVALID_HANDLE;
  scene_target_ = {};
  quarter_target_ = {};
}

void PostProcess::DestroyResources() {
  scene_capture_active_ = false;
  gpu_ready_ = false;
  DestroyFramebuffers();

  auto destroyVB = [](bgfx::VertexBufferHandle& h) {
    if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; }
  };
  auto destroyIB = [](bgfx::IndexBufferHandle& h) {
    if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; }
  };
  auto destroyProg = [](bgfx::ProgramHandle& h) {
    if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; }
  };
  auto destroyUni = [](bgfx::UniformHandle& h) {
    if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; }
  };

  destroyVB(quad_vbh_);
  destroyIB(quad_ibh_);

  destroyProg(prog_death_);
  destroyProg(prog_blit_);
  destroyProg(prog_box4_);
  destroyProg(prog_gauss4_h_);
  destroyProg(prog_gauss4_v_);
  destroyProg(prog_glow_combine_);

  destroyUni(u_texColor_);
  destroyUni(u_texBloom_);
  destroyUni(u_deathParams_);
  destroyUni(u_texelSize_);
  destroyUni(u_compositeParams_);
  destroyUni(u_colorGrade_);
  destroyUni(u_sourceUvScale_);
}

bool PostProcess::RenderFullscreenQuad(bgfx::ViewId view,
                                      bgfx::ProgramHandle program,
                                      bgfx::TextureHandle input_tex,
                                      bgfx::UniformHandle sampler,
                                      uint8_t stage,
                                      float u0,
                                      float v0,
                                      float u1,
                                      float v1,
                                      std::uint64_t sampler_flags,
                                      std::uint64_t state) {
  if (!bgfx::isValid(program) || !bgfx::isValid(input_tex) ||
      !bgfx::isValid(sampler) || !bgfx::isValid(quad_ibh_) ||
      bgfx::getAvailTransientVertexBuffer(4, quad_layout_) < 4) {
    return false;
  }

  bgfx::TransientVertexBuffer tvb;
  bgfx::allocTransientVertexBuffer(&tvb, 4, quad_layout_);
  auto* vertices = reinterpret_cast<PosTexVertex*>(tvb.data);
  vertices[0] = {-1.0f,  1.0f, 0.0f, u0, v0};
  vertices[1] = { 1.0f,  1.0f, 0.0f, u1, v0};
  vertices[2] = {-1.0f, -1.0f, 0.0f, u0, v1};
  vertices[3] = { 1.0f, -1.0f, 0.0f, u1, v1};

  bgfx::setTexture(stage, sampler, input_tex, sampler_flags);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setIndexBuffer(quad_ibh_);
  bgfx::setState(state);
  bgfx::submit(view, program);
  return true;
}

bool PostProcess::RenderSceneBlur(bgfx::ViewId& view,
                                  const bgfx::TextureHandle scene_input,
                                  const std::uint64_t sampler_flags) {
  if (!CanRenderSceneBlur() || !bgfx::isValid(scene_input)) {
    return false;
  }

  const auto qw = static_cast<std::uint16_t>(quarter_target_.logical.width);
  const auto qh = static_cast<std::uint16_t>(quarter_target_.logical.height);
  const auto scene_uv = ResolveFfxTextureUvScale(scene_target_);
  const auto quarter_uv = ResolveFfxTextureUvScale(quarter_target_);

  bgfx::setViewName(view, "PP: Box4 Downsample");
  bgfx::setViewRect(view, 0, 0, qw, qh);
  bgfx::setViewClear(view, BGFX_CLEAR_NONE);
  bgfx::setViewFrameBuffer(view, quarter_fb_);
  const float box_texel[4] = {
      scene_target_.reciprocal_width,
      scene_target_.reciprocal_height,
      0.0f,
      0.0f,
  };
  bgfx::setUniform(u_texelSize_, box_texel);
  bool ready = RenderFullscreenQuad(view, prog_box4_, scene_input,
                                    u_texColor_, 0, 0.0f, 0.0f,
                                    scene_uv.x, scene_uv.y,
                                    sampler_flags);
  ++view;

  const float blur_texel[4] = {
      quarter_target_.reciprocal_width,
      quarter_target_.reciprocal_height,
      0.0f,
      0.0f,
  };
  if (ready) {
    bgfx::setViewName(view, "PP: Gauss4 H");
    bgfx::setViewRect(view, 0, 0, qw, qh);
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);
    bgfx::setViewFrameBuffer(view, quarter_scratch_fb_);
    bgfx::setUniform(u_texelSize_, blur_texel);
    ready = RenderFullscreenQuad(
        view, prog_gauss4_h_, bgfx::getTexture(quarter_fb_, 0),
        u_texColor_, 0, 0.0f, 0.0f, quarter_uv.x, quarter_uv.y,
        sampler_flags);
    ++view;
  }

  if (ready) {
    bgfx::setViewName(view, "PP: Gauss4 V");
    bgfx::setViewRect(view, 0, 0, qw, qh);
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);
    bgfx::setViewFrameBuffer(view, quarter_fb_);
    bgfx::setUniform(u_texelSize_, blur_texel);
    ready = RenderFullscreenQuad(
        view, prog_gauss4_v_, bgfx::getTexture(quarter_scratch_fb_, 0),
        u_texColor_, 0, 0.0f, 0.0f, quarter_uv.x, quarter_uv.y,
        sampler_flags);
    ++view;
  }
  return ready;
}

bool PostProcess::RenderDeathEffect(bgfx::ViewId &view,
                                    const bgfx::TextureHandle scene_input,
                                    const std::uint64_t sampler_flags) {
  if (!CanRenderDeathEffect() || !bgfx::isValid(scene_input) ||
      !bgfx::isValid(quarter_fb_)) {
    return false;
  }

  bgfx::setViewName(view, "PP: FFXDeath");
  bgfx::setViewRect(view, 0, 0,
                    static_cast<std::uint16_t>(scene_target_.logical.width),
                    static_cast<std::uint16_t>(scene_target_.logical.height));
  bgfx::setViewClear(view, BGFX_CLEAR_NONE);
  bgfx::setViewFrameBuffer(view, death_fb_);

  const float params[4] = {
      kFfxDeathPrimaryColor[0],
      kFfxDeathPrimaryColor[1],
      kFfxDeathPrimaryColor[2],
      ResolveFfxDeathBlurCoefficient(
          world_view_effect_state_.glow_coefficient_byte),
  };
  bgfx::setUniform(u_deathParams_, params);
  const auto scene_uv = ResolveFfxTextureUvScale(scene_target_);
  const auto quarter_uv = ResolveFfxTextureUvScale(quarter_target_);
  const float source_uv_scale[4] = {
      scene_uv.x,
      scene_uv.y,
      quarter_uv.x,
      quarter_uv.y,
  };
  bgfx::setUniform(u_sourceUvScale_, source_uv_scale);
  bgfx::setTexture(1, u_texBloom_, bgfx::getTexture(quarter_fb_, 0), sampler_flags);
  const bool submitted = RenderFullscreenQuad(
      view, prog_death_, scene_input, u_texColor_, 0,
      0.0f, 0.0f, 1.0f, 1.0f, sampler_flags);
  ++view;
  return submitted;
}

PostProcessApplyResult PostProcess::Apply(bgfx::ViewId base_view) {
  if (!initialized_) {
    return {
        .next_view = base_view,
        .outcome = PostProcessApplyOutcome::kDirectBackbufferBypass,
    };
  }

  const bool capture_was_active = std::exchange(scene_capture_active_, false);
  if (!capture_was_active) {
    return {
        .next_view = base_view,
        .outcome = PostProcessApplyOutcome::kDirectBackbufferBypass,
    };
  }
  if (!gpu_ready_ || !CanCaptureScene()) {
    return {
        .next_view = base_view,
        .outcome = PostProcessApplyOutcome::kFailed,
    };
  }

  constexpr std::uint64_t kSceneCopySamplerFlags =
      TextureSurfaceCopySamplerFlags(
          SelectTextureSurfaceCopyFilter(nullptr, nullptr));

  const bool has_death = CanRenderDeathEffect();
  const bool has_glow = !has_death && IsGlowActive() && CanRenderSceneBlur();
  const bool has_grade = (std::fabs(state_.color_grade_r - 1.0f) > 0.001f ||
                          std::fabs(state_.color_grade_g - 1.0f) > 0.001f ||
                          std::fabs(state_.color_grade_b - 1.0f) > 0.001f ||
                          std::fabs(state_.color_grade_a - 1.0f) > 0.001f);

  bgfx::ViewId view = base_view;

  bgfx::TextureHandle scene_input = bgfx::getTexture(scene_fb_, 0);
  const auto scene_uv = ResolveFfxTextureUvScale(scene_target_);
  const auto quarter_uv = ResolveFfxTextureUvScale(quarter_target_);

  const bool blur_texture_ready =
      (has_death || has_glow) &&
      RenderSceneBlur(view, scene_input, kSceneCopySamplerFlags);
  if (has_death && blur_texture_ready &&
      RenderDeathEffect(view, scene_input, kSceneCopySamplerFlags)) {
    scene_input = bgfx::getTexture(death_fb_, 0);
  }
  const bool glow_texture_ready = has_glow && blur_texture_ready;

  bool ldr_composite_submitted = false;
  if (CanRenderComposite() && bgfx::isValid(scene_input)) {
    bgfx::setViewName(view, "PP: Glow Combine");
    bgfx::setViewRect(view, 0, 0,
                      static_cast<uint16_t>(width_),
                      static_cast<uint16_t>(height_));
    bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
    bgfx::setViewClear(view, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
    bgfx::touch(view);

    float composite_params[4] = {
        glow_texture_ready ? state_.glow_intensity : 0.0f,
        glow_texture_ready
            ? ResolvePassGlowCoefficient(
                  state_.glow_enabled,
                  world_view_effect_state_.glow_coefficient_byte)
            : 0.0f,
        0.0f,
        has_grade ? 1.0f : 0.0f,
    };
    bgfx::setUniform(u_compositeParams_, composite_params);

    float grade[4] = {state_.color_grade_r, state_.color_grade_g,
                      state_.color_grade_b, state_.color_grade_a};
    bgfx::setUniform(u_colorGrade_, grade);

    const float source_uv_scale[4] = {
        scene_uv.x,
        scene_uv.y,
        glow_texture_ready ? quarter_uv.x : scene_uv.x,
        glow_texture_ready ? quarter_uv.y : scene_uv.y,
    };
    bgfx::setUniform(u_sourceUvScale_, source_uv_scale);

    bgfx::setTexture(0, u_texColor_, scene_input);

    const bgfx::TextureHandle bloom_input =
        glow_texture_ready && bgfx::isValid(quarter_fb_)
            ? bgfx::getTexture(quarter_fb_, 0)
            : scene_input;
    bgfx::setTexture(1, u_texBloom_, bloom_input);

    bgfx::setVertexBuffer(0, quad_vbh_);
    bgfx::setIndexBuffer(quad_ibh_);
    bgfx::setState(kOpaqueCompositeState);
    bgfx::submit(view, prog_glow_combine_);
    ldr_composite_submitted = true;
    ++view;
  } else {

    bgfx::setViewName(view, "PP: Passthrough Blit");
    bgfx::setViewRect(view, 0, 0,
                      static_cast<uint16_t>(width_),
                      static_cast<uint16_t>(height_));
    bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
    bgfx::setViewClear(view, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);

    if (bgfx::isValid(prog_blit_) && bgfx::isValid(scene_input)) {

      ldr_composite_submitted = RenderFullscreenQuad(
          view, prog_blit_, scene_input, u_texColor_, 0, 0.0f,
          0.0f, scene_uv.x, scene_uv.y, kSceneCopySamplerFlags);
    } else {
      bgfx::touch(view);
    }
    ++view;
  }

  return {
      .next_view = view,
      .outcome = ldr_composite_submitted
                     ? PostProcessApplyOutcome::kSubmittedLdrComposite
                     : PostProcessApplyOutcome::kFailed,
  };
}

PostProcess::WorldCaptureExtent PostProcess::ResolveWorldCaptureExtent(
    const std::uint16_t native_width, const std::uint16_t native_height) const {

  if (!scene_capture_active_) {
    return {native_width, native_height};
  }
  return {
      static_cast<std::uint16_t>(scene_target_.logical.width),
      static_cast<std::uint16_t>(scene_target_.logical.height),
  };
}

void PostProcess::BindSceneFramebufferToViews(std::span<const std::uint8_t> view_ids,
                                              const std::uint16_t width,
                                              const std::uint16_t height,
                                              const std::uint32_t clear_rgba) {
  scene_capture_active_ = initialized_ && gpu_ready_ && CanCaptureScene() &&
                          IsAnyEffectActive() && !view_ids.empty();
  if (view_ids.empty()) {
    return;
  }

  bgfx::FrameBufferHandle target = BGFX_INVALID_HANDLE;
  if (scene_capture_active_) {
    target = scene_fb_;
  }

  const WorldCaptureExtent capture_extent = ResolveWorldCaptureExtent(width, height);
  for (const std::uint8_t view_id : view_ids) {
    const auto view = static_cast<bgfx::ViewId>(view_id);

    bgfx::setViewFrameBuffer(view, target);

    bgfx::setViewRect(view, 0, 0, capture_extent.width, capture_extent.height);
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);
  }

  const auto clear_view = static_cast<bgfx::ViewId>(view_ids.front());
  bgfx::setViewClear(clear_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                     clear_rgba, 1.0f, 0);
  bgfx::touch(clear_view);
}

bgfx::FrameBufferHandle PostProcess::GetSceneFramebuffer() const {
  if (initialized_ && gpu_ready_ && CanCaptureScene()) {
    return scene_fb_;
  }
  return BGFX_INVALID_HANDLE;
}

bool PostProcess::HasSceneFramebuffer() const {
  return bgfx::isValid(GetSceneFramebuffer());
}

bgfx::ViewId PostProcess::ApplyToRect(bgfx::ViewId base_view, Rect dest_rect) {
  if (!initialized_ || !gpu_ready_ || !CanCaptureScene()) return base_view;

  const auto clipped = ClipRectForOutput(dest_rect);
  if (!clipped.has_value()) {
    return base_view;
  }

  const TextureSurfaceCopyRect dest_copy_rect{
      0,
      0,
      static_cast<std::int32_t>(clipped->width),
      static_cast<std::int32_t>(clipped->height),
  };
  const std::uint64_t scene_copy_sampler_flags =
      TextureSurfaceCopySamplerFlags(
          SelectTextureSurfaceCopyFilter(nullptr, &dest_copy_rect));

  const bool has_death = CanRenderDeathEffect();
  const bool has_glow = !has_death && IsGlowActive() && CanRenderSceneBlur();
  const bool has_grade = (std::fabs(state_.color_grade_r - 1.0f) > 0.001f ||
                          std::fabs(state_.color_grade_g - 1.0f) > 0.001f ||
                          std::fabs(state_.color_grade_b - 1.0f) > 0.001f ||
                          std::fabs(state_.color_grade_a - 1.0f) > 0.001f);

  bgfx::ViewId view = base_view;
  bgfx::TextureHandle scene_input = bgfx::getTexture(scene_fb_, 0);
  const auto scene_uv = ResolveFfxTextureUvScale(scene_target_);
  const auto quarter_uv = ResolveFfxTextureUvScale(quarter_target_);

  const bool blur_texture_ready =
      (has_death || has_glow) &&
      RenderSceneBlur(view, scene_input, scene_copy_sampler_flags);
  if (has_death && blur_texture_ready &&
      RenderDeathEffect(view, scene_input, scene_copy_sampler_flags)) {
    scene_input = bgfx::getTexture(death_fb_, 0);
  }
  const bool glow_texture_ready = has_glow && blur_texture_ready;

  if (CanRenderComposite()) {
    bgfx::setViewName(view, "PP: Glow Combine");
    bgfx::setViewRect(view,
                      clipped->x,
                      clipped->y,
                      clipped->width,
                      clipped->height);
    bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);

    float composite_params[4] = {
        glow_texture_ready ? state_.glow_intensity : 0.0f,
        glow_texture_ready
            ? ResolvePassGlowCoefficient(
                  state_.glow_enabled,
                  world_view_effect_state_.glow_coefficient_byte)
            : 0.0f,
        0.0f,
        has_grade ? 1.0f : 0.0f,
    };
    bgfx::setUniform(u_compositeParams_, composite_params);

    float grade[4] = {state_.color_grade_r, state_.color_grade_g,
                      state_.color_grade_b, state_.color_grade_a};
    bgfx::setUniform(u_colorGrade_, grade);
    const float source_uv_scale[4] = {
        scene_uv.x,
        scene_uv.y,
        glow_texture_ready ? quarter_uv.x : scene_uv.x,
        glow_texture_ready ? quarter_uv.y : scene_uv.y,
    };
    bgfx::setUniform(u_sourceUvScale_, source_uv_scale);
    const bgfx::TextureHandle bloom_input =
        glow_texture_ready
            ? bgfx::getTexture(quarter_fb_, 0)
            : scene_input;
    bgfx::setTexture(1, u_texBloom_, bloom_input);
    (void)RenderFullscreenQuad(view, prog_glow_combine_, scene_input, u_texColor_, 0,
                               clipped->u0, clipped->v0, clipped->u1, clipped->v1,
                               scene_copy_sampler_flags, kLayeredRectCompositeState);
    ++view;
  } else {
    bgfx::setViewName(view, "PP: Passthrough Blit");
    bgfx::setViewRect(view,
                      clipped->x,
                      clipped->y,
                      clipped->width,
                      clipped->height);
    bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);

    if (bgfx::isValid(prog_blit_) && bgfx::isValid(scene_input)) {
      (void)RenderFullscreenQuad(view, prog_blit_, scene_input, u_texColor_, 0,
                                 clipped->u0 * scene_uv.x,
                                 clipped->v0 * scene_uv.y,
                                 clipped->u1 * scene_uv.x,
                                 clipped->v1 * scene_uv.y,
                                 scene_copy_sampler_flags, kLayeredRectCompositeState);
    } else {
      bgfx::touch(view);
    }
    ++view;
  }

  return view;
}

void PostProcess::SetDeathEffect(bool enabled, float intensity) {
  death_requested_enabled_ = enabled;
  requested_death_intensity_ = std::clamp(intensity, 0.0f, 1.0f);
  SyncDeathEffectState();
}

void PostProcess::SetDeathEffectImmediate(bool enabled, float intensity) {
  SetDeathEffect(enabled, intensity);
  current_death_intensity_ = target_death_intensity_;
  state_.death_intensity = current_death_intensity_;
}

void PostProcess::SetGlowEffect(bool enabled, float intensity) {
  state_.glow_enabled = state_.ffx_enabled && enabled;

  state_.glow_intensity = intensity;
}

void PostProcess::SetDrunkEffect(float drunkenness) {
  state_.drunkenness = std::clamp(drunkenness, 0.0f, 1.0f);
}

void PostProcess::SetColorGrading(float r, float g, float b, float a) {
  state_.color_grade_r = r;
  state_.color_grade_g = g;
  state_.color_grade_b = b;
  state_.color_grade_a = a;
}

void PostProcess::SetWorldViewEffectState(const WorldViewEffectState& state) {
  world_view_effect_state_ = state;
  state_.glow_intensity =
      screen_effect_state_.kind == ScreenEffectKind::kDefault
          ? static_cast<float>(state.effect_intensity_byte) / 255.0f
          : 0.0f;
}

void PostProcess::SetScreenEffectState(const ScreenEffectState &state) {
  screen_effect_state_ = state;
  state_.glow_intensity =
      state.kind == ScreenEffectKind::kDefault
          ? static_cast<float>(world_view_effect_state_.effect_intensity_byte) / 255.0f
          : 0.0f;
}

void PostProcess::SetSettings(const PostProcessSettings settings) {
  state_.ffx_enabled = settings.enabled;
  state_.glow_enabled = settings.enabled && settings.glow_enabled;
  death_cvar_enabled_ = settings.enabled && settings.death_enabled;
  rectangle_textures_ = settings.rectangle_textures;
  const std::uint8_t next_multisample = NormalizeMultisample(settings.multisample);
  if (next_multisample != multisample_) {
    multisample_ = next_multisample;
    if (bgfx::isValid(scene_fb_)) {
      DestroyFramebuffers();
      CreateFramebuffers();
      gpu_ready_ = CanCaptureScene();
    }
  }
  SyncDeathEffectState();
}

bool PostProcess::IsDeathEffectActive() const {
  return state_.death_enabled && state_.death_intensity > 0.001f;
}

bool PostProcess::IsGlowActive() const {

  return state_.ffx_enabled && state_.glow_enabled;
}

float PostProcess::GetDrunkenness() const {
  return state_.drunkenness;
}

bool PostProcess::IsInitialized() const {
  return initialized_;
}

bool PostProcess::IsAnyEffectActive() const {
  return multisample_ > 1u ||
         (state_.ffx_enabled && (IsDeathEffectActive() || IsGlowActive() ||
                                 std::fabs(state_.color_grade_r - 1.0f) > 0.001f ||
                                 std::fabs(state_.color_grade_g - 1.0f) > 0.001f ||
                                 std::fabs(state_.color_grade_b - 1.0f) > 0.001f ||
                                 std::fabs(state_.color_grade_a - 1.0f) > 0.001f));
}

}

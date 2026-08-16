#pragma once

#include "openwow/render/effects/ffx_glow.h"

#include <bgfx/bgfx.h>

#include <cstdint>

#include <algorithm>
#include <array>
#include <optional>
#include <span>

namespace openwow::render {

struct WorldViewEffectState {
  bool underwater_enabled = false;

  std::uint8_t glow_coefficient_byte = 0;
  std::uint8_t effect_intensity_byte = 0;
  float normalized_inebriation = 0.0f;
};

[[nodiscard]] inline constexpr float ResolvePassGlowCoefficient(
    const bool effect_chain_enabled,
    const std::uint8_t glow_coefficient_byte) noexcept {
  return effect_chain_enabled
             ? static_cast<float>(glow_coefficient_byte) / 255.0f
             : 0.0f;
}

[[nodiscard]] inline constexpr float EvaluatePassGlowChannel(
    const float scene,
    const float blurred_scene,
    const float effect_intensity,
    const float glow_coefficient) noexcept {

  return scene + effect_intensity * (blurred_scene - scene) +
         blurred_scene * blurred_scene * glow_coefficient;
}

inline constexpr std::array<float, 3> kFfxDeathPrimaryColor{
    83.0f / 255.0f,
    147.0f / 255.0f,
    168.0f / 255.0f,
};

[[nodiscard]] inline constexpr float ResolveFfxDeathBlurCoefficient(
    const std::uint8_t glow_coefficient_byte) noexcept {
  return static_cast<float>(glow_coefficient_byte) / 255.0f;
}

[[nodiscard]] inline constexpr std::array<float, 3> EvaluateFfxDeathColor(
    const std::array<float, 3> &scene,
    const std::array<float, 3> &blurred_scene,
    const float blur_alpha) noexcept {
  std::array<float, 3> base{};
  for (std::size_t channel = 0; channel < base.size(); ++channel) {
    base[channel] = scene[channel] +
                    blur_alpha * blurred_scene[channel] * blurred_scene[channel];
  }
  const float luminance = std::clamp(
      base[0] * 0.299f + base[1] * 0.587f + base[2] * 0.144f,
      0.0f, 1.0f);
  const float shaped = std::clamp(
      4.0f * luminance * (1.0f - luminance), 0.0f, 1.0f);
  return {
      luminance + kFfxDeathPrimaryColor[0] * shaped,
      luminance + kFfxDeathPrimaryColor[1] * shaped,
      luminance + kFfxDeathPrimaryColor[2] * shaped,
  };
}

enum class ScreenEffectKind : std::uint8_t {
  kDefault = 0,
  kDeath = 1,
  kNether = 2,
  kSpecial = 3,
  kUnknown = 4,
};

struct ScreenEffectState {
  std::uint32_t screen_effect_id = 0;
  ScreenEffectKind kind = ScreenEffectKind::kDefault;
  std::array<float, 4> param{};
  std::optional<std::uint8_t> light_param_slot_override;
  std::uint32_t sound_ambience_id = 0;
  std::uint32_t zone_music_id = 0;
};

struct PostProcessState {

  bool ffx_enabled = true;

  bool death_enabled = false;
  float death_intensity = 0.0f;

  bool glow_enabled = false;
  float glow_intensity = 0.0f;

  float drunkenness = 0.0f;

  float color_grade_r = 1.0f;
  float color_grade_g = 1.0f;
  float color_grade_b = 1.0f;
  float color_grade_a = 1.0f;
};

struct PostProcessSettings {
  bool enabled{true};
  bool glow_enabled{true};
  bool death_enabled{true};
  bool rectangle_textures{true};
  std::uint8_t multisample{1};
};

enum class PostProcessApplyOutcome : std::uint8_t {

  kDirectBackbufferBypass,

  kSubmittedLdrComposite,

  kFailed,
};

struct PostProcessApplyResult {
  bgfx::ViewId next_view{0};
  PostProcessApplyOutcome outcome{PostProcessApplyOutcome::kFailed};

  [[nodiscard]] bool HasFinalLdrBackbuffer() const noexcept {
    return outcome != PostProcessApplyOutcome::kFailed;
  }
};

struct PostProcessFinalCopyAvailability {
  bool scene_target = false;
  bool index_buffer = false;
  bool color_sampler = false;
  bool blit_program = false;
  bool composite_program = false;
  bool composite_vertex_buffer = false;
  bool composite_uniforms = false;
};

[[nodiscard]] inline constexpr bool HasPostProcessFinalCopyPath(
    const PostProcessFinalCopyAvailability availability) noexcept {
  const bool common_path = availability.scene_target &&
                           availability.index_buffer &&
                           availability.color_sampler;
  const bool composite_path = availability.composite_program &&
                              availability.composite_vertex_buffer &&
                              availability.composite_uniforms;
  return common_path && (availability.blit_program || composite_path);
}

class PostProcess {
 public:
  struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int output_width = 0;
    int output_height = 0;
  };

  struct ClippedRect {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
  };

  [[nodiscard]] static std::optional<ClippedRect> ClipRectForOutput(Rect dest_rect);

  PostProcess();
  ~PostProcess();

  PostProcess(const PostProcess&) = delete;
  PostProcess& operator=(const PostProcess&) = delete;

  void Init(uint32_t width, uint32_t height,
            PostProcessSettings settings = {});

  void InitGPU();

  bool Resize(uint32_t width, uint32_t height);

  void Shutdown();

  void ReleaseRendererDeviceResources();

  bool RestoreRendererDeviceResources();

  void Update(float dt);

  [[nodiscard]] PostProcessApplyResult Apply(bgfx::ViewId base_view);

  bgfx::ViewId ApplyToRect(bgfx::ViewId base_view, Rect dest_rect);

  void BindSceneFramebufferToViews(std::span<const std::uint8_t> view_ids,
                                   std::uint16_t width,
                                   std::uint16_t height,
                                   std::uint32_t clear_rgba);

  struct WorldCaptureExtent {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
  };
  [[nodiscard]] WorldCaptureExtent ResolveWorldCaptureExtent(
      std::uint16_t native_width, std::uint16_t native_height) const;

  [[nodiscard]] bgfx::FrameBufferHandle GetSceneFramebuffer() const;
  [[nodiscard]] bool HasSceneFramebuffer() const;

  [[nodiscard]] bgfx::TextureHandle GetSceneTexture() const {
    return scene_tex_;
  }

  void SetDeathEffect(bool enabled, float intensity = 1.0f);

  void SetDeathEffectImmediate(bool enabled, float intensity = 1.0f);
  void SetGlowEffect(bool enabled, float intensity = 0.0f);
  void SetDrunkEffect(float drunkenness);
  void SetColorGrading(float r, float g, float b, float a = 1.0f);
  void SetWorldViewEffectState(const WorldViewEffectState& state);
  void SetScreenEffectState(const ScreenEffectState& state);

  void SetSettings(PostProcessSettings settings);

  [[nodiscard]] bool IsDeathEffectActive() const;
  [[nodiscard]] bool IsGlowActive() const;
  [[nodiscard]] float GetDrunkenness() const;
  [[nodiscard]] bool IsInitialized() const;
  [[nodiscard]] bool IsAnyEffectActive() const;
  [[nodiscard]] uint32_t width() const { return width_; }
  [[nodiscard]] uint32_t height() const { return height_; }

  [[nodiscard]] const PostProcessState& GetState() const { return state_; }
  [[nodiscard]] const WorldViewEffectState& GetWorldViewEffectState() const {
    return world_view_effect_state_;
  }
  [[nodiscard]] const ScreenEffectState& GetScreenEffectState() const {
    return screen_effect_state_;
  }

  static constexpr uint8_t kMaxViews = 5;
  static constexpr std::uint64_t kOpaqueCompositeState =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;
  static constexpr std::uint64_t kLayeredRectCompositeState =
      BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
      BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                     BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);

private:
  void SyncDeathEffectState();
  [[nodiscard]] bool CanRenderDeathEffect() const;
  [[nodiscard]] bool CanRenderSceneBlur() const;
  [[nodiscard]] bool CanRenderComposite() const;
  [[nodiscard]] bool CanCaptureScene() const;
  [[nodiscard]] bool RenderSceneBlur(bgfx::ViewId& view,
                                     bgfx::TextureHandle scene_input,
                                     std::uint64_t sampler_flags);
  [[nodiscard]] bool RenderDeathEffect(bgfx::ViewId& view,
                                       bgfx::TextureHandle scene_input,
                                       std::uint64_t sampler_flags);

  void CreateResources();
  void DestroyResources();
  void CreateFramebuffers();
  void DestroyFramebuffers();

  [[nodiscard]] bool RenderFullscreenQuad(
      bgfx::ViewId view, bgfx::ProgramHandle program,
      bgfx::TextureHandle input_tex, bgfx::UniformHandle sampler,
      uint8_t stage = 0, float u0 = 0.0f, float v0 = 0.0f,
      float u1 = 1.0f, float v1 = 1.0f,
      std::uint64_t sampler_flags =
          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
      std::uint64_t state = kOpaqueCompositeState);

  PostProcessState state_;
  WorldViewEffectState world_view_effect_state_{};
  ScreenEffectState screen_effect_state_{};
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  bool initialized_ = false;
  bool gpu_ready_ = false;
  bool scene_capture_active_ = false;

  bool death_requested_enabled_ = false;
  bool death_cvar_enabled_ = true;
  bool rectangle_textures_ = true;
  std::uint8_t multisample_ = 1;
  float requested_death_intensity_ = 0.0f;
  float target_death_intensity_ = 0.0f;
  float current_death_intensity_ = 0.0f;

  static constexpr float kDeathFadeSpeed = 2.0f;

  bgfx::FrameBufferHandle scene_fb_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle scene_tex_    = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle scene_depth_  = BGFX_INVALID_HANDLE;
  FfxTextureAllocation scene_target_{};

  bgfx::FrameBufferHandle quarter_fb_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle quarter_tex_    = BGFX_INVALID_HANDLE;

  bgfx::FrameBufferHandle quarter_scratch_fb_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle quarter_scratch_tex_    = BGFX_INVALID_HANDLE;
  FfxTextureAllocation quarter_target_{};

  bgfx::FrameBufferHandle death_fb_  = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle     death_tex_ = BGFX_INVALID_HANDLE;

  bgfx::VertexBufferHandle quad_vbh_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle  quad_ibh_ = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout quad_layout_;

  bgfx::ProgramHandle prog_death_        = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle prog_blit_          = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle prog_box4_         = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle prog_gauss4_h_     = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle prog_gauss4_v_     = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle prog_glow_combine_ = BGFX_INVALID_HANDLE;

  bgfx::UniformHandle u_texColor_        = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_texBloom_        = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_deathParams_     = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_texelSize_       = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_compositeParams_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_colorGrade_      = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_sourceUvScale_   = BGFX_INVALID_HANDLE;
};

}

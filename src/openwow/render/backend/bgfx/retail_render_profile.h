#pragma once

#include <cstdint>

namespace openwow::render {

enum class RendererType {
  Auto,
  Vulkan,
  Metal,
  Direct3D11,
  Direct3D12,
  OpenGL,
};

enum class LegacyGxDeviceClass : std::uint32_t {
  kFixedFunction = 0,
  kHighestPixelShaderTarget = 12,
};

struct WotlkRendererCapabilities {
  bool supports_shadow_render_target = false;
  bool supports_depth_texture = false;
  bool vertex_shaders = false;
  bool pixel_shaders = false;
  uint32_t vertex_shader_version = 0;
  uint32_t pixel_shader_version = 0;
  bool supports_trilinear_filtering = false;
  bool supports_anisotropic_filtering = false;
  uint32_t max_anisotropy = 0;
  bool hardware_cursor = false;
  bool stereo_video = false;
  LegacyGxDeviceClass gx_device_class = LegacyGxDeviceClass::kFixedFunction;
};

struct WotlkVideoEffectsDefaults {
  const char* particle_density = "0.1";
  bool projected_textures = false;
};

struct ExtShadowQualitySupport {
  bool supports_basic_modes = false;
  bool supports_advanced_modes = false;
};

[[nodiscard]] bool IsRendererRuntimeAvailable();
[[nodiscard]] RendererType GetRendererType();
[[nodiscard]] WotlkRendererCapabilities QueryWotlkRendererCapabilities();
[[nodiscard]] WotlkVideoEffectsDefaults ResolveWotlkVideoEffectsDefaults(
    std::uint32_t gx_device_class) noexcept;
[[nodiscard]] bool IsExtShadowQualityTierSupported(const WotlkRendererCapabilities& caps,
                                                   RendererType renderer_type,
                                                   std::uint32_t quality) noexcept;
[[nodiscard]] ExtShadowQualitySupport GetExtShadowQualitySupport();

}

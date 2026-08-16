#include "openwow/render/backend/bgfx/retail_render_profile.h"

#include "openwow/render/backend/bgfx/renderer_context_services.h"

#include <bgfx/bgfx.h>

namespace openwow::render {
namespace {

RendererType FromBgfx(const bgfx::RendererType::Enum type) {
  switch (type) {
    case bgfx::RendererType::Vulkan:
      return RendererType::Vulkan;
    case bgfx::RendererType::Metal:
      return RendererType::Metal;
    case bgfx::RendererType::Direct3D11:
      return RendererType::Direct3D11;
    case bgfx::RendererType::Direct3D12:
      return RendererType::Direct3D12;
    case bgfx::RendererType::OpenGL:
      return RendererType::OpenGL;
    default:
      return RendererType::Auto;
  }
}

bool HasFormatCapability(const bgfx::Caps& caps,
                         const bgfx::TextureFormat::Enum format,
                         const std::uint16_t capability) {
  return (caps.formats[format] & capability) != 0u;
}

bool SupportsShadowFramebuffer(const bgfx::Caps& caps) {
  constexpr std::uint16_t capability =
      BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER;
  return HasFormatCapability(caps, bgfx::TextureFormat::D24S8, capability) ||
         HasFormatCapability(caps, bgfx::TextureFormat::D24, capability) ||
         HasFormatCapability(caps, bgfx::TextureFormat::D32, capability) ||
         HasFormatCapability(caps, bgfx::TextureFormat::D16, capability) ||
         HasFormatCapability(caps, bgfx::TextureFormat::D32F, capability) ||
         HasFormatCapability(caps, bgfx::TextureFormat::D24F, capability) ||
         HasFormatCapability(caps, bgfx::TextureFormat::D16F, capability);
}

}

bool IsRendererRuntimeAvailable() {
  return IsRendererContextActive();
}

RendererType GetRendererType() {
  return IsRendererRuntimeAvailable() ? FromBgfx(bgfx::getRendererType())
                                      : RendererType::Auto;
}

WotlkRendererCapabilities QueryWotlkRendererCapabilities() {
  WotlkRendererCapabilities result;
  if (!IsRendererRuntimeAvailable()) return result;
  const bgfx::Caps* caps = bgfx::getCaps();
  if (caps == nullptr) return result;

  result.supports_shadow_render_target = SupportsShadowFramebuffer(*caps);
  result.supports_depth_texture =
      result.supports_shadow_render_target &&
      (caps->supported & BGFX_CAPS_TEXTURE_COMPARE_LEQUAL) != 0u;
  result.vertex_shaders = true;
  result.pixel_shaders = true;
  result.vertex_shader_version = 3u;
  result.pixel_shader_version = 4u;
  result.supports_trilinear_filtering = true;
  result.supports_anisotropic_filtering = true;
  result.max_anisotropy = 16u;
  result.hardware_cursor = true;
  result.gx_device_class = LegacyGxDeviceClass::kHighestPixelShaderTarget;
  return result;
}

WotlkVideoEffectsDefaults ResolveWotlkVideoEffectsDefaults(
    const std::uint32_t gx_device_class) noexcept {

  WotlkVideoEffectsDefaults defaults;
  if (gx_device_class < 11u) {
    switch (gx_device_class) {
      case 2u:
        defaults.particle_density = "0.4";
        break;
      case 3u:
        defaults.particle_density = "0.7";
        defaults.projected_textures = true;
        break;
      case 4u:
      case 5u:
      case 6u:
        defaults.particle_density = "1.0";
        defaults.projected_textures = true;
        break;
      default:
        break;
    }
    return defaults;
  }

  defaults.particle_density = "1.0";
  defaults.projected_textures = true;
  return defaults;
}

bool IsExtShadowQualityTierSupported(
    const WotlkRendererCapabilities& caps,
    const RendererType renderer_type,
    const std::uint32_t quality) noexcept {
  if (quality == 0u) return true;
  const bool depth =
      caps.supports_depth_texture || caps.supports_shadow_render_target;
  if (!depth || quality > 5u) return false;

  switch (renderer_type) {
    case RendererType::Vulkan:
    case RendererType::Metal:
      return true;
    case RendererType::Direct3D11:
    case RendererType::Direct3D12:
    case RendererType::Auto:
      if (quality <= 2u) {
        return caps.vertex_shader_version >= 1u &&
               caps.vertex_shader_version <= 3u &&
               caps.pixel_shader_version >= 3u &&
               caps.pixel_shader_version <= 4u;
      }
      return caps.vertex_shader_version == 3u &&
             caps.pixel_shader_version == 4u;
    case RendererType::OpenGL:
      return quality <= 2u &&
             (caps.vertex_shader_version == 6u ||
              caps.vertex_shader_version == 10u) &&
             (caps.pixel_shader_version == 11u ||
              caps.pixel_shader_version == 12u);
  }
  return false;
}

ExtShadowQualitySupport GetExtShadowQualitySupport() {
  const auto capabilities = QueryWotlkRendererCapabilities();
  const auto renderer = GetRendererType();
  return {
      .supports_basic_modes =
          IsExtShadowQualityTierSupported(capabilities, renderer, 2u),
      .supports_advanced_modes =
          IsExtShadowQualityTierSupported(capabilities, renderer, 5u),
  };
}

}

#include "openwow/render/integration/ui/bgfx_display_device_adapter.h"

#include "openwow/platform/system/os_system_info.h"
#include "openwow/render/backend/bgfx/renderer_context_services.h"
#include "openwow/render/backend/bgfx/retail_render_profile.h"
#include "openwow/render/resources/textures/texture_filtering_mode.h"
#include "openwow/ui/game/cvar_system.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace openwow::render::integration::ui {
namespace {

bool SupportsMultisampling(const bgfx::TextureFormat::Enum format, const std::uint64_t flags) {
  return bgfx::isTextureValid(1, false, 1, format, flags);
}

bool ColorFormatSupportsMultisampling(const int color_bits, const std::uint64_t flags) {
  switch (color_bits) {
  case 16:
    return SupportsMultisampling(bgfx::TextureFormat::R5G6B5, flags) ||
           SupportsMultisampling(bgfx::TextureFormat::B5G6R5, flags);
  case 24:
    return SupportsMultisampling(bgfx::TextureFormat::RGBA8, flags) ||
           SupportsMultisampling(bgfx::TextureFormat::BGRA8, flags);
  default:
    return false;
  }
}

bool DepthFormatSupportsMultisampling(const int depth_bits, const std::uint64_t flags) {
  const std::uint64_t depth_flags = flags | BGFX_TEXTURE_RT_WRITE_ONLY;
  switch (depth_bits) {
  case 16:
    return SupportsMultisampling(bgfx::TextureFormat::D16, depth_flags) ||
           SupportsMultisampling(bgfx::TextureFormat::D16F, depth_flags);
  case 32:
    return SupportsMultisampling(bgfx::TextureFormat::D32, depth_flags) ||
           SupportsMultisampling(bgfx::TextureFormat::D32F, depth_flags);
  default:
    return SupportsMultisampling(bgfx::TextureFormat::D24S8, depth_flags) ||
           SupportsMultisampling(bgfx::TextureFormat::D24, depth_flags) ||
           SupportsMultisampling(bgfx::TextureFormat::D24F, depth_flags);
  }
}

}

std::vector<openwow::ui::display::MultisampleFormat>
BgfxDisplayDeviceAdapter::AvailableMultisampleFormats(const int color_bits,
                                                      const int depth_bits) const {
  constexpr std::array<std::pair<int, std::uint64_t>, 4> kLevels{{
      {2, BGFX_TEXTURE_RT_MSAA_X2},
      {4, BGFX_TEXTURE_RT_MSAA_X4},
      {8, BGFX_TEXTURE_RT_MSAA_X8},
      {16, BGFX_TEXTURE_RT_MSAA_X16},
  }};
  std::vector<openwow::ui::display::MultisampleFormat> formats{
      {color_bits, depth_bits, 1}};
  if (!openwow::render::IsRendererContextActive()) {

    return formats;
  }

  for (const auto& [samples, flags] : kLevels) {
    if (ColorFormatSupportsMultisampling(color_bits, flags) &&
        DepthFormatSupportsMultisampling(depth_bits, flags)) {
      formats.push_back({color_bits, depth_bits, samples});
    }
  }
  return formats;
}

std::optional<openwow::ui::display::VideoCapabilities>
BgfxDisplayDeviceAdapter::Capabilities() const {
  openwow::ui::display::VideoCapabilities result;
  const auto& cvars = openwow::ui::game::CVarSystem::Instance();
  result.buffering = cvars.GetCVarInt("gxTripleBuffer") != 0 ? 2 : 1;
  const auto device = openwow::render::QueryWotlkRendererCapabilities();
  const auto filtering = openwow::render::QueryLiveTextureFilterCaps();
  result.anisotropic = filtering.supports_tier5;
  result.pixel_shaders = device.pixel_shaders;
  result.vertex_shaders = device.vertex_shaders;
  result.trilinear = filtering.supports_tier4;
  if (filtering.max_anisotropy != 0) {
    result.max_anisotropy = static_cast<int>(filtering.max_anisotropy);
  }
  result.hardware_cursor = device.hardware_cursor;
  result.stereo_video = device.stereo_video;
  return result;
}

bool BgfxDisplayDeviceAdapter::PlayerResolutionCapabilityAvailable() const {
  auto& detector = openwow::core::OsSystemInfoDetector::Instance();
  detector.Init();
  return detector.GetInfo().processorCount > 1;
}

}

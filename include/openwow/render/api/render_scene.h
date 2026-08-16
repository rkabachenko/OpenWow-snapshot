#pragma once

#include <cstdint>

namespace openwow::render::api {

class ScreenshotReadbackTarget;

enum class RendererBackend : std::uint8_t {
  Auto,
  Vulkan,
  Metal,
  Direct3D11,
  Direct3D12,
  OpenGL,
  Unknown,
};

[[nodiscard]] constexpr const char* RendererBackendName(RendererBackend backend) noexcept {
  switch (backend) {
    case RendererBackend::Auto:
      return "Auto";
    case RendererBackend::Vulkan:
      return "Vulkan";
    case RendererBackend::Metal:
      return "Metal";
    case RendererBackend::Direct3D11:
      return "Direct3D 11";
    case RendererBackend::Direct3D12:
      return "Direct3D 12";
    case RendererBackend::OpenGL:
      return "OpenGL";
    case RendererBackend::Unknown:
      break;
  }
  return "Unknown";
}

enum class RendererFeature : std::uint8_t {
  DepthCompare,
  Texture3D,
  Instancing,
  Compute,
  FragmentDepth,
  IndependentBlend,
  TextureBlit,
  OcclusionQuery,
  AlphaToCoverage,
  ConservativeRaster,
};

using RendererFeatureFlags = std::uint64_t;

[[nodiscard]] constexpr RendererFeatureFlags RendererFeatureBit(
    RendererFeature feature) noexcept {
  return RendererFeatureFlags{1} << static_cast<std::uint8_t>(feature);
}

[[nodiscard]] constexpr bool HasRendererFeature(RendererFeatureFlags flags,
                                                RendererFeature feature) noexcept {
  return (flags & RendererFeatureBit(feature)) != 0;
}

[[nodiscard]] constexpr const char* RendererFeatureName(RendererFeature feature) noexcept {
  switch (feature) {
    case RendererFeature::DepthCompare:
      return "DepthCompare";
    case RendererFeature::Texture3D:
      return "Texture3D";
    case RendererFeature::Instancing:
      return "Instancing";
    case RendererFeature::Compute:
      return "Compute";
    case RendererFeature::FragmentDepth:
      return "FragmentDepth";
    case RendererFeature::IndependentBlend:
      return "IndependentBlend";
    case RendererFeature::TextureBlit:
      return "TextureBlit";
    case RendererFeature::OcclusionQuery:
      return "OcclusionQuery";
    case RendererFeature::AlphaToCoverage:
      return "AlphaToCoverage";
    case RendererFeature::ConservativeRaster:
      return "ConservativeRaster";
  }
  return "Unknown";
}

struct RenderExtent {
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  [[nodiscard]] constexpr bool IsValid() const noexcept {
    return width > 0 && height > 0;
  }

  [[nodiscard]] friend constexpr bool operator==(RenderExtent lhs,
                                                 RenderExtent rhs) noexcept {
    return lhs.width == rhs.width && lhs.height == rhs.height;
  }

  [[nodiscard]] friend constexpr bool operator!=(RenderExtent lhs,
                                                 RenderExtent rhs) noexcept {
    return !(lhs == rhs);
  }
};

struct RendererCreateInfo {
  void* platform_window = nullptr;
  void* native_display = nullptr;
  void* native_window = nullptr;
  RenderExtent extent{};
  ScreenshotReadbackTarget* screenshot_target = nullptr;
  RendererBackend backend = RendererBackend::Auto;
  struct PresentationConfig {
    bool vsync = true;
    std::uint8_t multisample = 1;
    std::uint8_t maximum_frame_latency = 2;
    bool flush_after_render = false;

    friend constexpr bool operator==(PresentationConfig,
                                     PresentationConfig) = default;
  } presentation{};
  bool debug = false;
  bool profile = false;
};

struct FrameInfo {
  std::uint64_t frame_number = 0;
  double delta_seconds = 0.0;
  double absolute_seconds = 0.0;
  RenderExtent backbuffer{};
};

struct RenderScene {
  RenderExtent viewport{};
  std::uint32_t terrain_batch_count = 0;
  std::uint32_t wmo_group_count = 0;
  std::uint32_t m2_instance_count = 0;
  std::uint32_t water_surface_count = 0;
  std::uint32_t particle_system_count = 0;
  std::uint32_t ui_draw_count = 0;
  std::uint32_t debug_draw_count = 0;
};

struct RendererCapabilities {
  RendererBackend backend = RendererBackend::Unknown;
  bool homogeneous_depth = false;
  std::uint16_t vendor_id = 0;
  std::uint16_t device_id = 0;
  std::uint8_t gpu_count = 0;
  std::uint32_t max_draw_calls = 0;
  std::uint32_t max_texture_size = 0;
  std::uint32_t max_views = 0;
  RendererFeatureFlags features = 0;
};

struct RendererStats {
  std::uint64_t frame_number = 0;
  std::uint32_t graph_pass_count = 0;
  std::uint32_t submitted_scene_batches = 0;
  std::uint32_t draw_calls = 0;
  std::uint32_t compute_calls = 0;
  std::uint32_t blit_calls = 0;
  std::uint32_t vertex_buffer_count = 0;
  std::uint32_t index_buffer_count = 0;
  std::uint32_t framebuffer_count = 0;
  std::uint32_t texture_count = 0;
  std::uint32_t program_count = 0;
  std::uint32_t shader_count = 0;
  std::int64_t gpu_time_begin = 0;
  std::int64_t gpu_time_end = 0;
  std::int64_t cpu_time_begin = 0;
  std::int64_t cpu_time_end = 0;
  float gpu_time_ms = 0.0F;
  float cpu_time_ms = 0.0F;
  std::int64_t texture_memory_used = 0;
  std::int64_t render_target_memory_used = 0;
  std::int64_t gpu_memory_used = 0;
  std::int64_t gpu_memory_max = 0;
  std::int32_t transient_vertex_buffer_used = 0;
  std::int32_t transient_index_buffer_used = 0;
  RenderExtent backbuffer{};
};

}

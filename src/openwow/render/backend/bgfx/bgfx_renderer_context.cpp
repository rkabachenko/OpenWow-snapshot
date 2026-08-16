#include "openwow/render/backend/bgfx/bgfx_renderer_context.h"

#include "openwow/render/api/renderer_context.h"
#include "openwow/render/api/screenshot_readback.h"
#include "openwow/render/backend/bgfx/bgfx_encoder_ledger.h"
#include "openwow/render/backend/bgfx/bgfx_renderer_backend.h"
#include "openwow/render/backend/bgfx/bgfx_resource_registry.h"
#if defined(__APPLE__)
#include "openwow/render/backend/bgfx/metal_presentation_layer.h"
#endif
#include "openwow/render/platform/renderer_backend_selection.h"
#include "openwow/render/resources/readback/pixel_readback.h"
#include "openwow/render/ui/ui_shaders.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/runtime/scheduling/frame_job_system.h"

#include <SDL2/SDL.h>
#include "openwow/platform/window/sdl_syswm.h"
#if defined(__APPLE__)
#include <SDL2/SDL_metal.h>
#endif

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace openwow::render {
namespace {

constexpr char kCoreScreenshotRequestPath[] = "__openwow_core_screenshot__";
std::atomic_bool s_renderer_context_active{false};

std::atomic_bool s_renderer_device_restarting{false};

constexpr std::uint32_t kBgfxImplicitEncoders = 1u;

std::atomic<std::uint32_t> s_frame_encoders_available{0};

#if defined(__APPLE__)
void *s_metal_view = nullptr;
#endif

std::uint32_t ResetFlags(const api::RendererCreateInfo::PresentationConfig config,
                         const api::RendererBackend backend) {
  std::uint32_t flags = config.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
  if (config.flush_after_render) {
    flags |= BGFX_RESET_FLUSH_AFTER_RENDER;
  }

  const std::uint8_t backbuffer_multisample =
      backend == api::RendererBackend::Metal ? 1u : config.multisample;
  switch (backbuffer_multisample) {
  case 2:
    flags |= BGFX_RESET_MSAA_X2;
    break;
  case 4:
    flags |= BGFX_RESET_MSAA_X4;
    break;
  case 8:
    flags |= BGFX_RESET_MSAA_X8;
    break;
  case 16:
    flags |= BGFX_RESET_MSAA_X16;
    break;
  default:
    break;
  }
  return flags;
}

[[nodiscard]] std::uint16_t ToBgfxClearFlags(RendererViewClearFlags flags) noexcept {
  std::uint16_t bgfx_flags = BGFX_CLEAR_NONE;
  if (HasRendererViewClearFlag(flags, RendererViewClearFlags::kColor)) {
    bgfx_flags |= BGFX_CLEAR_COLOR;
  }
  if (HasRendererViewClearFlag(flags, RendererViewClearFlags::kDepth)) {
    bgfx_flags |= BGFX_CLEAR_DEPTH;
  }
  if (HasRendererViewClearFlag(flags, RendererViewClearFlags::kStencil)) {
    bgfx_flags |= BGFX_CLEAR_STENCIL;
  }
  return bgfx_flags;
}

api::RendererFeatureFlags ToRendererFeatureFlags(std::uint64_t supported) {
  api::RendererFeatureFlags features = 0;
  auto add = [&](std::uint64_t bgfx_feature, api::RendererFeature feature) {
    if ((supported & bgfx_feature) != 0) {
      features |= api::RendererFeatureBit(feature);
    }
  };

  add(BGFX_CAPS_TEXTURE_COMPARE_LEQUAL, api::RendererFeature::DepthCompare);
  add(BGFX_CAPS_TEXTURE_3D, api::RendererFeature::Texture3D);
  add(BGFX_CAPS_INSTANCING, api::RendererFeature::Instancing);
  add(BGFX_CAPS_COMPUTE, api::RendererFeature::Compute);
  add(BGFX_CAPS_FRAGMENT_DEPTH, api::RendererFeature::FragmentDepth);
  add(BGFX_CAPS_BLEND_INDEPENDENT, api::RendererFeature::IndependentBlend);
  add(BGFX_CAPS_TEXTURE_BLIT, api::RendererFeature::TextureBlit);
  add(BGFX_CAPS_OCCLUSION_QUERY, api::RendererFeature::OcclusionQuery);
  add(BGFX_CAPS_ALPHA_TO_COVERAGE, api::RendererFeature::AlphaToCoverage);
  add(BGFX_CAPS_CONSERVATIVE_RASTER, api::RendererFeature::ConservativeRaster);
  return features;
}

api::RendererCapabilities ReadRendererCapabilities(api::RendererBackend backend) {
  api::RendererCapabilities capabilities{};
  capabilities.backend = backend;

  const bgfx::Caps* caps = bgfx::getCaps();
  if (caps == nullptr) {
    return capabilities;
  }

  {
    static bool logged = false;
    if (!logged) {
      logged = true;
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "RendererContext: bgfx encoder budget granted=" +
              std::to_string(caps->limits.maxEncoders));
    }
  }

  capabilities.homogeneous_depth = caps->homogeneousDepth;
  capabilities.vendor_id = caps->vendorId;
  capabilities.device_id = caps->deviceId;
  capabilities.gpu_count = caps->numGPUs;
  capabilities.max_draw_calls = caps->limits.maxDrawCalls;
  capabilities.max_texture_size = caps->limits.maxTextureSize;
  capabilities.max_views = caps->limits.maxViews;
  capabilities.features = ToRendererFeatureFlags(caps->supported);
  return capabilities;
}

void CopyBgfxStats(api::RendererStats& out) {
  const bgfx::Stats* stats = bgfx::getStats();
  if (stats == nullptr) {
    return;
  }

  out.draw_calls = stats->numDraw;
  out.compute_calls = stats->numCompute;
  out.blit_calls = stats->numBlit;
  out.vertex_buffer_count = stats->numVertexBuffers;
  out.index_buffer_count = stats->numIndexBuffers;
  out.framebuffer_count = stats->numFrameBuffers;
  out.texture_count = stats->numTextures;
  out.program_count = stats->numPrograms;
  out.shader_count = stats->numShaders;
  out.gpu_time_begin = stats->gpuTimeBegin;
  out.gpu_time_end = stats->gpuTimeEnd;
  out.cpu_time_begin = stats->cpuTimeBegin;
  out.cpu_time_end = stats->cpuTimeEnd;
  if (stats->gpuTimerFreq > 0) {
    out.gpu_time_ms = static_cast<float>(
        1000.0 * static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) /
        static_cast<double>(stats->gpuTimerFreq));
  }
  if (stats->cpuTimerFreq > 0) {
    out.cpu_time_ms = static_cast<float>(
        1000.0 * static_cast<double>(stats->cpuTimeEnd - stats->cpuTimeBegin) /
        static_cast<double>(stats->cpuTimerFreq));
  }
  out.texture_memory_used = stats->textureMemoryUsed;
  out.render_target_memory_used = stats->rtMemoryUsed;
  out.gpu_memory_used = stats->gpuMemoryUsed;
  out.gpu_memory_max = stats->gpuMemoryMax;
  out.transient_vertex_buffer_used = stats->transientVbUsed;
  out.transient_index_buffer_used = stats->transientIbUsed;
}

bool WriteBgra32Bmp(const char* path, std::uint32_t w, std::uint32_t h,
                    std::uint32_t pitch, std::span<const std::uint8_t> data,
                    bool yflip) {
  const std::vector<std::uint8_t> encoded =
      EncodeBgra32Bmp(w, h, pitch, data, yflip);
  if (encoded.empty()) {
    return false;
  }

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<const char*>(encoded.data()),
            static_cast<std::streamsize>(encoded.size()));
  return out.good();
}

class BgfxScreenshotCallback final : public bgfx::CallbackI {
 public:
  void SetTarget(api::ScreenshotReadbackTarget* target) noexcept {
    target_ = target;
  }
  [[nodiscard]] bool InFlight() const noexcept {
    return in_flight_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool TryBegin() noexcept {
    return !in_flight_.exchange(true, std::memory_order_acq_rel);
  }
  void Cancel() noexcept {
    in_flight_.store(false, std::memory_order_release);
  }

  ~BgfxScreenshotCallback() override = default;

  void fatal(const char* file_path, std::uint16_t line, bgfx::Fatal::Enum,
             const char* str) override {
    std::fprintf(stderr, "bgfx fatal: %s:%u %s\n", file_path ? file_path : "?",
                 static_cast<unsigned>(line), str ? str : "");
    std::fflush(stderr);
    std::abort();
  }

  void traceVargs(const char*, std::uint16_t, const char*, va_list) override {}
  void profilerBegin(const char*, std::uint32_t, const char*, std::uint16_t) override {}
  void profilerBeginLiteral(const char*, std::uint32_t, const char*, std::uint16_t) override {}
  void profilerEnd() override {}

  std::uint32_t cacheReadSize(std::uint64_t) override {
    return 0;
  }

  bool cacheRead(std::uint64_t, void*, std::uint32_t) override {
    return false;
  }

  void cacheWrite(std::uint64_t, const void*, std::uint32_t) override {}

  void screenShot(const char* file_path, std::uint32_t width, std::uint32_t height,
                  std::uint32_t pitch, const void* data, std::uint32_t size,
                  bool yflip) override {
    const std::span<const std::uint8_t> bytes(
        static_cast<const std::uint8_t*>(data), data != nullptr ? size : 0u);
    if (file_path != nullptr && std::strcmp(file_path, kCoreScreenshotRequestPath) == 0) {
      if (data != nullptr) {
        auto pixels = NormalizeBgra32Readback(width, height, pitch, bytes, yflip);
        if (!pixels.empty()) {
          if (target_ != nullptr) {
            target_->CompleteCapture(std::move(pixels), width, height);
          }
        } else {
          if (target_ != nullptr) {
            target_->FailCapture();
          }
        }
      } else {
        if (target_ != nullptr) {
          target_->FailCapture();
        }
      }
      Cancel();
      return;
    }

    if (file_path == nullptr || data == nullptr) {
      Cancel();
      return;
    }

    const bool ok = WriteBgra32Bmp(file_path, width, height, pitch, bytes, yflip);
    openwow::diagnostics::Log(ok ? openwow::diagnostics::LogLevel::kInfo : openwow::diagnostics::LogLevel::kWarn,
                       std::string(ok ? "Screenshot saved: " : "Screenshot write failed: ") +
                           file_path);
    Cancel();
  }

  void captureBegin(std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum,
                    bool) override {}
  void captureEnd() override {}
  void captureFrame(const void*, std::uint32_t) override {}

 private:
  api::ScreenshotReadbackTarget* target_{nullptr};
  std::atomic_bool in_flight_{false};
};

BgfxScreenshotCallback s_bgfx_callback;

bool FillPlatformData(SDL_Window* window, bgfx::PlatformData* out) {
  if (window == nullptr || out == nullptr) {
    return false;
  }

  SDL_SysWMinfo wmi;
  SDL_VERSION(&wmi.version);
  if (SDL_GetWindowWMInfo(window, &wmi) != SDL_TRUE) {
    return false;
  }

  bgfx::PlatformData pd{};

#if defined(SDL_VIDEO_DRIVER_X11)
  if (wmi.subsystem == SDL_SYSWM_X11) {
    pd.ndt = wmi.info.x11.display;
    pd.nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(wmi.info.x11.window));
  }
#endif
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
  if (wmi.subsystem == SDL_SYSWM_WAYLAND) {
    pd.ndt = wmi.info.wl.display;
    pd.nwh = wmi.info.wl.surface;
  }
#endif
#if defined(_WIN32)
  if (wmi.subsystem == SDL_SYSWM_WINDOWS) {
    pd.nwh = wmi.info.win.window;
  }
#endif
#if defined(__APPLE__)
  if (wmi.subsystem == SDL_SYSWM_COCOA) {
    if (s_metal_view != nullptr) {
      SDL_Metal_DestroyView(static_cast<SDL_MetalView>(s_metal_view));
      s_metal_view = nullptr;
    }
    SDL_MetalView metal_view = SDL_Metal_CreateView(window);
    if (metal_view != nullptr) {
      s_metal_view = metal_view;
      pd.nwh = SDL_Metal_GetLayer(metal_view);
    }
    if (pd.nwh == nullptr) {
      pd.nwh = wmi.info.cocoa.window;
    }
  }
#endif

  if (pd.nwh == nullptr) {
    return false;
  }

  *out = pd;
  return true;
}

api::TextureHandle CreateContextWhiteTexture(
    BgfxResourceRegistry& resources,
    const api::TextureHandle logical_handle = {}) {
  const std::uint32_t pixel = 0xffffffffu;
  const bgfx::Memory* memory = bgfx::copy(&pixel, sizeof(pixel));
  const bgfx::TextureHandle native =
      bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, memory);
  if (!bgfx::isValid(native)) {
    return {};
  }
  if (logical_handle) {
    if (resources.Bind(logical_handle, native)) {
      return logical_handle;
    }
    bgfx::destroy(native);
    return {};
  }
  return resources.AdoptTexture(
      native, api::LogicalRenderResourceDescriptor{
                  .debug_name = "renderer-context-white",
                  .content_identity = 0xffffffffu,
                  .byte_size = sizeof(pixel),
                  .width = 1u,
                  .height = 1u,
                  .depth = 1u,
                  .mip_count = 1u,
              });
}

class BgfxRendererContext final : public api::RendererContext {
 public:
  api::RendererStatus Initialize(
      const api::RendererCreateInfo& create_info) override {
    const bool restarting = initialized_;
    if (restarting) {
      status_ = api::RendererStatus::ResetRequired;
      s_renderer_device_restarting.store(true, std::memory_order_release);
      NotifyDeviceWillReset();
      s_renderer_context_active.store(false, std::memory_order_release);
      openwow::render::ui::InvalidateUiProgramCache();
      resources_.BeginDeviceRestart();
      bgfx::shutdown();
      initialized_ = false;
      frame_active_ = false;
      final_compositor_.Reset();
      graph_.Reset();
      stats_ = {};
    } else {
      Shutdown();
    }

    auto* window = static_cast<SDL_Window*>(create_info.platform_window);
    if (window == nullptr || !create_info.extent.IsValid()) {
      status_ = api::RendererStatus::SurfaceUnavailable;
      return status_;
    }

    bgfx::PlatformData platform_data{};
    if (!FillPlatformData(window, &platform_data)) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "RendererContext: SDL window platform data not available");
      status_ = api::RendererStatus::SurfaceUnavailable;
      return status_;
    }

    const auto requested = create_info.backend;
    auto resolved = ResolveRendererBackend(requested);
    if (requested != api::RendererBackend::Auto && requested != resolved) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         std::string("RendererContext: ") +
                             api::RendererBackendName(requested) +
                             " not supported on this platform, falling back to " +
                             api::RendererBackendName(resolved));
    }

    bgfx::Init init{};
    init.type = ToBgfxRendererType(resolved);
    init.platformData = platform_data;
    init.callback = &s_bgfx_callback;
    screenshot_target_ = create_info.screenshot_target;
    s_bgfx_callback.SetTarget(screenshot_target_);
    init.resolution.width = create_info.extent.width;
    init.resolution.height = create_info.extent.height;
    init.resolution.reset = ResetFlags(create_info.presentation, resolved);
    init.resolution.maxFrameLatency = create_info.presentation.maximum_frame_latency;

    init.limits.transientVbSize = 32u * 1024u * 1024u;
    init.limits.transientIbSize = 8u * 1024u * 1024u;

    constexpr std::uint32_t kDedicatedWorldEncodePassEncoders = 2u;
    constexpr std::uint32_t kM2RenderBatchWaveSitesPerFrame = 11u;
    const std::uint32_t frame_job_workers =
        openwow::core::FrameJobSystem::ResolveDefaultWorkerCount();
    const std::uint32_t m2_render_batch_participants_per_wave = frame_job_workers + 1u;
    const std::uint32_t requested_encoders =
        kBgfxImplicitEncoders + kDedicatedWorldEncodePassEncoders +
        kM2RenderBatchWaveSitesPerFrame * m2_render_batch_participants_per_wave;
    init.limits.maxEncoders = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(requested_encoders,
                                std::numeric_limits<std::uint16_t>::max()));

    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                              std::string("RendererContext: requesting ") +
                                  api::RendererBackendName(resolved));

    SDL_ShowWindow(window);
    SDL_PumpEvents();

    if (!bgfx::init(init)) {
      const auto platform_default = PlatformDefaultRendererBackend();
      if (resolved != platform_default) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                  std::string("RendererContext: ") +
                                      api::RendererBackendName(resolved) + " init failed, trying " +
                                      api::RendererBackendName(platform_default));
        resolved = platform_default;
        init.type = ToBgfxRendererType(resolved);
      }

      if (!bgfx::init(init)) {
        if (resolved != api::RendererBackend::OpenGL) {
          openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                    "RendererContext: fallback failed, trying OpenGL");
          resolved = api::RendererBackend::OpenGL;
          init.type = ToBgfxRendererType(resolved);
        }

        if (!bgfx::init(init)) {
          openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                                    "RendererContext: bgfx::init failed for all backends");
          status_ = api::RendererStatus::Fatal;
          return status_;
        }
      }
    }

    extent_ = create_info.extent;
    presentation_ = create_info.presentation;
    create_info_ = create_info;
    initialized_ = true;
    s_renderer_context_active.store(true, std::memory_order_release);

    s_renderer_device_restarting.store(false, std::memory_order_release);
    active_backend_ = FromBgfxRendererType(bgfx::getRendererType());
    capabilities_ = ReadRendererCapabilities(active_backend_);

    desired_debug_ = {.diagnostics = create_info.debug};
    applied_debug_ = {};
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1a1412ff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);
    graph_.BuildDefaultWorldFrame(extent_);
    white_texture_ =
        CreateContextWhiteTexture(resources_, restarting ? white_texture_ : api::TextureHandle{});
    if (!white_texture_) {
      Shutdown();
      status_ = api::RendererStatus::Fatal;
      return status_;
    }
    status_ = api::RendererStatus::Ready;
    NotifyDeviceReady();

    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                              std::string("Renderer backend: using ") +
                                  api::RendererBackendName(active_backend_));
    return status_;
  }

  void Shutdown() override {
    if (initialized_) {
      NotifyDeviceWillReset();
      s_renderer_context_active.store(false, std::memory_order_release);
      openwow::render::ui::InvalidateUiProgramCache();
      resources_.BeginDeviceRestart();
      resources_.DestroyAll();
      white_texture_ = {};
      bgfx::shutdown();
    }
#if defined(__APPLE__)
    if (s_metal_view != nullptr) {
      SDL_Metal_DestroyView(static_cast<SDL_MetalView>(s_metal_view));
      s_metal_view = nullptr;
    }
#endif
    s_bgfx_callback.Cancel();
    initialized_ = false;
    extent_ = {};
    active_backend_ = api::RendererBackend::Unknown;
    capabilities_ = {};
    screenshot_request_submitted_ = false;
    pending_explicit_screenshot_.reset();
    screenshot_target_ = nullptr;
    s_bgfx_callback.SetTarget(nullptr);
    frame_active_ = false;
    final_compositor_.Reset();
    graph_.Reset();
    stats_ = {};
    status_ = api::RendererStatus::SurfaceUnavailable;
  }

  void Resize(api::RenderExtent extent) override {
    if (!initialized_ || !extent.IsValid() || extent == extent_) {
      return;
    }

    extent_ = extent;
    bgfx::reset(extent_.width, extent_.height, ResetFlags(presentation_, active_backend_));
#if defined(__APPLE__)
    ApplyMetalPresentationConfig();
#endif
    bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);
    graph_.BuildDefaultWorldFrame(extent_);
  }

  void SetPresentationConfig(const api::RendererCreateInfo::PresentationConfig config) override {
    if (!initialized_ || presentation_ == config) {
      return;
    }
    presentation_ = config;
    create_info_.presentation = config;
    bgfx::reset(extent_.width, extent_.height, ResetFlags(presentation_, active_backend_));
#if defined(__APPLE__)
    ApplyMetalPresentationConfig();
#endif
    bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);
  }

  void BeginFrame(const api::FrameInfo& frame_info) override {
    if (!initialized_ || frame_active_) {
      return;
    }

    frame_active_ = true;

    const bgfx::Caps* const caps = bgfx::getCaps();
    const std::uint32_t granted_encoders =
        caps != nullptr ? caps->limits.maxEncoders : 0u;
    s_frame_encoders_available.store(
        granted_encoders > kBgfxImplicitEncoders
            ? granted_encoders - kBgfxImplicitEncoders
            : 0u,
        std::memory_order_release);
    ApplyDebugOptions();
    final_compositor_.BeginFrame();
    stats_.frame_number = frame_info.frame_number;
    stats_.backbuffer = frame_info.backbuffer.IsValid() ? frame_info.backbuffer : extent_;
    graph_.BuildDefaultWorldFrame(stats_.backbuffer);
    stats_.graph_pass_count = static_cast<std::uint32_t>(graph_.passes().size());
    CopyBgfxStats(stats_);
    bgfx::touch(0);
  }

  void SetDebugOptions(
      const api::RendererDebugOptions options) noexcept override {
    desired_debug_ = options;
  }

  void Render(const api::RenderScene& scene) override {
    if (!initialized_) {
      return;
    }

    stats_.submitted_scene_batches = scene.terrain_batch_count + scene.wmo_group_count +
                                     scene.m2_instance_count + scene.water_surface_count +
                                     scene.particle_system_count + scene.ui_draw_count +
                                     scene.debug_draw_count;
  }

  void MarkFinalCompositorStage(const api::FinalCompositorStage stage) noexcept override {
    if (initialized_) {
      final_compositor_.MarkStage(stage);
    }
  }

  void CompleteFinalCompositor(const api::FinalCompositorTarget target) noexcept override {
    if (initialized_) {
      final_compositor_.Complete(target);
    }
  }

  void EndFrame() override {
    if (!initialized_ || !frame_active_) {
      return;
    }

    if (final_compositor_.state().IsCurrentFrameComplete() &&
        !screenshot_request_submitted_ && !s_bgfx_callback.InFlight()) {
      if (pending_explicit_screenshot_.has_value()) {
        const std::string path = pending_explicit_screenshot_->string();
        pending_explicit_screenshot_.reset();
        if (s_bgfx_callback.TryBegin()) {
          screenshot_request_submitted_ = true;
          bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path.c_str());
        }
      } else if (screenshot_target_ != nullptr &&
                 screenshot_target_->HasPendingCapture()) {
        if (s_bgfx_callback.TryBegin()) {
          screenshot_request_submitted_ = true;
          bgfx::requestScreenShot(BGFX_INVALID_HANDLE, kCoreScreenshotRequestPath);
        }
      }
    }

    s_frame_encoders_available.store(0u, std::memory_order_release);
    bgfx::frame();
#if defined(__APPLE__)
    ApplyMetalPresentationConfig();
#endif
    screenshot_request_submitted_ = false;
    frame_active_ = false;
  }

  void AddDeviceLifecycleObserver(
      api::RendererDeviceLifecycleObserver& observer) override {
    if (std::find(device_observers_.begin(), device_observers_.end(),
                  &observer) == device_observers_.end()) {
      device_observers_.push_back(&observer);
    }
  }

  void RemoveDeviceLifecycleObserver(
      api::RendererDeviceLifecycleObserver& observer) override {
    std::erase(device_observers_, &observer);
  }

  bool RequestScreenshot(const std::filesystem::path& path) override {
    if (!initialized_ || path.empty() || pending_explicit_screenshot_.has_value() ||
        screenshot_request_submitted_ || s_bgfx_callback.InFlight()) {
      return false;
    }
    pending_explicit_screenshot_ = path;
    return true;
  }

  [[nodiscard]] api::RendererStatus Status() const noexcept override {
    return status_;
  }

  [[nodiscard]] api::RendererDebugOptions DebugOptions() const noexcept override {
    return desired_debug_;
  }

  [[nodiscard]] bool IsFrameActive() const noexcept override {
    return initialized_ && frame_active_;
  }

  [[nodiscard]] api::RenderExtent Extent() const noexcept override {
    return extent_;
  }

  [[nodiscard]] api::RendererBackend ActiveBackend() const noexcept override {
    return active_backend_;
  }

  [[nodiscard]] api::DeviceGeneration Generation() const noexcept override {
    return resources_.device_generation();
  }

  [[nodiscard]] const api::RendererCapabilities& Capabilities() const noexcept override {
    return capabilities_;
  }

  [[nodiscard]] const api::RendererStats& Stats() const noexcept override {
    return stats_;
  }

  [[nodiscard]] const api::FinalCompositorState& FinalCompositor() const noexcept override {
    return final_compositor_.state();
  }

  [[nodiscard]] api::FrameGraph& Graph() noexcept override {
    return graph_;
  }

  [[nodiscard]] const api::FrameGraph& Graph() const noexcept override {
    return graph_;
  }

  [[nodiscard]] bgfx::TextureHandle NativeWhiteTexture() const noexcept {
    if (!initialized_) {
      return BGFX_INVALID_HANDLE;
    }
    return resources_.TryGet(white_texture_).value_or(
        bgfx::TextureHandle{bgfx::kInvalidHandle});
  }

 private:
#if defined(__APPLE__)
  void ApplyMetalPresentationConfig() const {
    if (active_backend_ != api::RendererBackend::Metal ||
        s_metal_view == nullptr) {
      return;
    }
    EnsureMetalMaximumDrawableCount(
        SDL_Metal_GetLayer(static_cast<SDL_MetalView>(s_metal_view)),
        presentation_.maximum_frame_latency);
  }
#endif

  void ApplyDebugOptions() {
    if (desired_debug_ == applied_debug_) {
      return;
    }
    std::uint32_t flags = BGFX_DEBUG_NONE;
    if (desired_debug_.diagnostics) {
      flags |= BGFX_DEBUG_TEXT;
    }
    if (desired_debug_.wireframe) {
      flags |= BGFX_DEBUG_WIREFRAME;
    }
    bgfx::setDebug(flags);
    applied_debug_ = desired_debug_;
  }

  void NotifyDeviceWillReset() {
    const auto observers = device_observers_;
    for (auto it = observers.rbegin(); it != observers.rend(); ++it) {
      api::RendererDeviceLifecycleObserver* observer = *it;
      if (observer != nullptr &&
          std::find(device_observers_.begin(), device_observers_.end(),
                    observer) != device_observers_.end()) {
        observer->OnRendererDeviceWillReset();
      }
    }
  }

  void NotifyDeviceReady() {
    const auto observers = device_observers_;
    const api::DeviceGeneration generation = resources_.device_generation();
    for (api::RendererDeviceLifecycleObserver* observer : observers) {
      if (observer != nullptr &&
          std::find(device_observers_.begin(), device_observers_.end(),
                    observer) != device_observers_.end()) {
        observer->OnRendererDeviceReady(generation);
      }
    }
  }

  api::RenderExtent extent_{};
  api::RendererBackend active_backend_ = api::RendererBackend::Unknown;
  api::RendererCapabilities capabilities_{};
  api::FrameGraph graph_;
  api::RendererStats stats_;
  api::RendererStatus status_{api::RendererStatus::SurfaceUnavailable};
  api::RendererDebugOptions desired_debug_{};
  api::RendererDebugOptions applied_debug_{};
  api::FinalCompositorTracker final_compositor_;
  BgfxResourceRegistry resources_;
  api::TextureHandle white_texture_{};
  bool initialized_ = false;
  bool frame_active_ = false;
  api::RendererCreateInfo create_info_{};
  api::RendererCreateInfo::PresentationConfig presentation_{};
  bool screenshot_request_submitted_ = false;
  std::optional<std::filesystem::path> pending_explicit_screenshot_;
  api::ScreenshotReadbackTarget* screenshot_target_{nullptr};
  std::vector<api::RendererDeviceLifecycleObserver*> device_observers_;
};

}

std::unique_ptr<api::RendererContext> CreateRendererContext() {
  return std::make_unique<BgfxRendererContext>();
}

bool IsRendererContextActive() noexcept {
  return s_renderer_context_active.load(std::memory_order_acquire);
}

bool IsRendererDeviceRestarting() noexcept {
  return s_renderer_device_restarting.load(std::memory_order_acquire);
}

std::uint32_t ReserveBgfxFrameEncoders(const std::uint32_t requested) noexcept {

  std::uint32_t available = s_frame_encoders_available.load(std::memory_order_acquire);
  for (;;) {
    const std::uint32_t granted = std::min(available, requested);
    if (granted == 0u) {
      return 0u;
    }
    if (s_frame_encoders_available.compare_exchange_weak(
            available, available - granted, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      return granted;
    }
  }
}

void ReleaseBgfxFrameEncoders(const std::uint32_t unspent) noexcept {
  if (unspent != 0u) {
    s_frame_encoders_available.fetch_add(unspent, std::memory_order_acq_rel);
  }
}

bgfx::TextureHandle GetBgfxRendererContextWhiteTexture(
    const api::RendererContext* context) noexcept {
  const auto* bgfx_context = dynamic_cast<const BgfxRendererContext*>(context);
  return bgfx_context != nullptr ? bgfx_context->NativeWhiteTexture()
                                 : bgfx::TextureHandle{bgfx::kInvalidHandle};
}

bool ClearRendererContextView(const api::RendererContext* context,
                                  const std::uint8_t view_id,
                                  const std::uint32_t rgba,
                                  const std::uint32_t width,
                                  const std::uint32_t height) {
  if (context == nullptr ||
      context->Status() != api::RendererStatus::Ready) {
    return false;
  }
  if (width > 0 && height > 0) {
    bgfx::setViewRect(view_id, 0, 0, static_cast<std::uint16_t>(width),
                      static_cast<std::uint16_t>(height));
  }
  bgfx::setViewClear(view_id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, rgba, 1.0f, 0);
  bgfx::touch(view_id);
  return true;
}

bool ClearRendererDebugText(const api::RendererContext* context) {
  if (context == nullptr ||
      context->Status() != api::RendererStatus::Ready) {
    return false;
  }
  bgfx::dbgTextClear();
  return true;
}

bool PrintRendererDebugText(const api::RendererContext* context,
                        const std::uint16_t x,
                        const std::uint16_t y,
                        const std::uint8_t attr,
                        const std::string_view text) {
  if (context == nullptr ||
      context->Status() != api::RendererStatus::Ready) {
    return false;
  }
  bgfx::dbgTextPrintf(x, y, attr, "%.*s", static_cast<int>(text.size()), text.data());
  return true;
}

bool SetRendererContextViewTransform(const api::RendererContext* context,
                                         const std::uint8_t view_id,
                                         const float* view,
                                         const float* projection) {
  if (context == nullptr ||
      context->Status() != api::RendererStatus::Ready || view == nullptr ||
      projection == nullptr) {
    return false;
  }
  bgfx::setViewTransform(view_id, view, projection);
  return true;
}

bool ConfigureRendererContextView(const api::RendererContext* context,
                                  const std::uint8_t view_id,
                                  const RendererViewClearFlags clear_flags,
                                  const std::uint32_t rgba,
                                  const float depth,
                                  const std::uint8_t stencil,
                                  const std::uint32_t width,
                                  const std::uint32_t height,
                                  const float* view,
                                  const float* projection,
                                  const bool touch_view) {
  if (context == nullptr ||
      context->Status() != api::RendererStatus::Ready || view == nullptr ||
      projection == nullptr || width == 0 || height == 0) {
    return false;
  }
  bgfx::setViewRect(view_id, 0, 0, static_cast<std::uint16_t>(width),
                    static_cast<std::uint16_t>(height));
  bgfx::setViewTransform(view_id, view, projection);
  bgfx::setViewClear(view_id, ToBgfxClearFlags(clear_flags), rgba, depth, stencil);
  if (touch_view) {
    bgfx::touch(view_id);
  }
  return true;
}

}

#pragma once

#include "openwow/render/api/device_generation.h"
#include "openwow/render/api/final_compositor_fence.h"
#include "openwow/render/api/frame_graph.h"
#include "openwow/render/api/render_scene.h"

#include <filesystem>

namespace openwow::render::api {

enum class RendererStatus : std::uint8_t {
  SurfaceUnavailable,
  Ready,
  ResetRequired,
  Fatal,
};

struct RendererDebugOptions {
  bool diagnostics{false};
  bool wireframe{false};

  friend constexpr bool operator==(RendererDebugOptions,
                                   RendererDebugOptions) = default;
};

class RendererDeviceLifecycleObserver {
 public:
  virtual ~RendererDeviceLifecycleObserver() = default;

  virtual void OnRendererDeviceWillReset() = 0;

  virtual void OnRendererDeviceReady(DeviceGeneration generation) = 0;
};

class RendererContext {
 public:
  virtual ~RendererContext() = default;

  [[nodiscard]] virtual RendererStatus Initialize(
      const RendererCreateInfo& create_info) = 0;
  virtual void Shutdown() = 0;
  virtual void Resize(RenderExtent extent) = 0;
  virtual void SetPresentationConfig(
      RendererCreateInfo::PresentationConfig config) = 0;
  virtual void BeginFrame(const FrameInfo& frame_info) = 0;
  virtual void SetDebugOptions(RendererDebugOptions options) noexcept = 0;
  virtual void Render(const RenderScene& scene) = 0;
  virtual void MarkFinalCompositorStage(FinalCompositorStage stage) noexcept = 0;
  virtual void CompleteFinalCompositor(FinalCompositorTarget target) noexcept = 0;
  virtual void EndFrame() = 0;
  virtual void AddDeviceLifecycleObserver(
      RendererDeviceLifecycleObserver& observer) = 0;
  virtual void RemoveDeviceLifecycleObserver(
      RendererDeviceLifecycleObserver& observer) = 0;

  [[nodiscard]] virtual bool RequestScreenshot(const std::filesystem::path& path) = 0;

  [[nodiscard]] virtual RendererStatus Status() const noexcept = 0;
  [[nodiscard]] virtual RendererDebugOptions DebugOptions() const noexcept = 0;

  [[nodiscard]] virtual bool IsFrameActive() const noexcept = 0;
  [[nodiscard]] virtual RenderExtent Extent() const noexcept = 0;
  [[nodiscard]] virtual RendererBackend ActiveBackend() const noexcept = 0;

  [[nodiscard]] virtual DeviceGeneration Generation() const noexcept = 0;
  [[nodiscard]] virtual const RendererCapabilities& Capabilities() const noexcept = 0;
  [[nodiscard]] virtual const RendererStats& Stats() const noexcept = 0;
  [[nodiscard]] virtual const FinalCompositorState& FinalCompositor() const noexcept = 0;
  [[nodiscard]] virtual FrameGraph& Graph() noexcept = 0;
  [[nodiscard]] virtual const FrameGraph& Graph() const noexcept = 0;
};

}

#pragma once

#include "openwow/render/api/renderer_context.h"
#include "openwow/render/resources/textures/texture_lease.h"
#include "openwow/ui/game/stateful_widget_render.h"
#include "openwow/ui/model_natural_size.h"
#include "openwow/ui/texture_natural_size.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::render {
class BgfxTextCache;
class ModelPortrait;
class PortraitRenderer;
class TextureManager;
namespace m2 {
class M2System;
}
namespace ui {
struct MeshVertex;
class TextRenderer;
class UiRenderer;
}
}

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::ui::game::runtime::render {

struct UiTextureInfo {
  openwow::render::TextureLease lease;
  int width{0};
  int height{0};
};

using UiTextureLoad =
    std::function<std::optional<UiTextureInfo>(const std::string&)>;

class UiRenderResources final
    : public openwow::render::api::RendererDeviceLifecycleObserver,
      public openwow::ui::TextureNaturalSizeSource,
      public openwow::ui::ModelNaturalSizeSource {
 public:
  UiRenderResources(openwow::render::TextureManager& texture_manager,
                    openwow::render::m2::M2System& m2_system);
  ~UiRenderResources() override;

  UiRenderResources(const UiRenderResources&) = delete;
  UiRenderResources& operator=(const UiRenderResources&) = delete;

  void BindRendererContext(
      openwow::render::api::RendererContext* renderer_context);
  void SetVirtualFileSystem(const openwow::vfs::VirtualFileSystem* vfs) noexcept;
  void SetTextureLoader(UiTextureLoad loader);

  [[nodiscard]] bool Restore();
  void Release();
  void SetRuntimeActive(bool active) noexcept;

  [[nodiscard]] openwow::render::ui::UiRenderer& ui_renderer() noexcept;
  [[nodiscard]] const openwow::render::ui::UiRenderer& ui_renderer() const noexcept;
  [[nodiscard]] openwow::render::ui::TextRenderer& text_renderer() noexcept;
  [[nodiscard]] openwow::render::BgfxTextCache* text_cache() noexcept;
  [[nodiscard]] openwow::render::PortraitRenderer* portrait_renderer() noexcept;
  [[nodiscard]] openwow::render::TextureManager& texture_manager() noexcept;
  [[nodiscard]] openwow::render::m2::M2System& m2_system() noexcept;
  [[nodiscard]] const UiTextureLoad& texture_loader() const noexcept;

  void QueueTextureLoad(const std::string& texture_path) override;
  [[nodiscard]] std::optional<openwow::ui::TexturePixelSize>
  ResolveTexturePixelSize(const std::string& texture_path) override;

  [[nodiscard]] std::optional<openwow::ui::ModelBoundingBox>
  ResolveModelBoundingBox(const std::string& model_path) override;
  [[nodiscard]] bgfx::TextureHandle UploadMovieFrame(
      const std::uint8_t* rgba, int width, int height,
      std::uint32_t version);

  [[nodiscard]] std::vector<openwow::render::ui::MeshVertex>&
  text_mesh_vertices() noexcept;
  [[nodiscard]] std::vector<std::uint16_t>& text_mesh_indices() noexcept;
  [[nodiscard]] stateful_widgets::SliderState& slider_state() noexcept;
  [[nodiscard]] stateful_widgets::CooldownState& cooldown_state() noexcept;
  [[nodiscard]] stateful_widgets::ScrollingMessageFrameState&
  scrolling_message_state() noexcept;
  [[nodiscard]] std::vector<stateful_widgets::ScrollingMessageMeasurement>&
  scrolling_message_measurements() noexcept;
  [[nodiscard]] std::vector<stateful_widgets::ScrollingMessageLinePlan>&
  scrolling_message_line_plans() noexcept;
  [[nodiscard]] std::unordered_map<
      std::string, std::unique_ptr<openwow::render::ModelPortrait>>&
  model_surfaces() noexcept;
  [[nodiscard]] std::unique_ptr<openwow::render::ModelPortrait>&
  AcquireModelSurface(const std::string& key);

  void RetireIdleModelSurfaces();
  [[nodiscard]] std::unordered_map<std::string, std::string>&
  model_diagnostics() noexcept;

 private:
  void OnRendererDeviceWillReset() override;
  void OnRendererDeviceReady(
      openwow::render::api::DeviceGeneration generation) override;

  openwow::render::TextureManager& texture_manager_;
  openwow::render::m2::M2System& m2_system_;
  const openwow::vfs::VirtualFileSystem* vfs_{nullptr};
  std::unique_ptr<openwow::render::ui::UiRenderer> ui_renderer_;
  std::unique_ptr<openwow::render::ui::TextRenderer> text_renderer_;
  std::unique_ptr<openwow::render::BgfxTextCache> text_cache_;
  std::unique_ptr<openwow::render::PortraitRenderer> portrait_renderer_;
  std::vector<openwow::render::ui::MeshVertex> text_mesh_vertices_;
  std::vector<std::uint16_t> text_mesh_indices_;
  stateful_widgets::SliderState slider_state_;
  stateful_widgets::CooldownState cooldown_state_;
  stateful_widgets::ScrollingMessageFrameState scrolling_message_state_;
  std::vector<stateful_widgets::ScrollingMessageMeasurement>
      scrolling_message_measurements_;
  std::vector<stateful_widgets::ScrollingMessageLinePlan>
      scrolling_message_line_plans_;
  std::unordered_map<std::string,
                     std::unique_ptr<openwow::render::ModelPortrait>>
      model_surfaces_;

  std::unordered_map<std::string, std::uint64_t> model_surface_last_used_;
  std::uint64_t model_surface_pass_{0u};

  static constexpr std::uint64_t kModelSurfaceIdlePassLimit = 120u;
  std::unordered_map<std::string, std::string> model_diagnostics_;

  std::unordered_map<std::string, std::string> normalized_texture_path_cache_;
  UiTextureLoad texture_loader_;
  bgfx::TextureHandle movie_texture_ = BGFX_INVALID_HANDLE;
  int movie_texture_width_{0};
  int movie_texture_height_{0};
  std::uint32_t movie_texture_version_{0};
  bool movie_texture_has_version_{false};
  openwow::render::api::RendererContext* renderer_context_{nullptr};
  openwow::render::api::DeviceGeneration device_generation_{};
  bool observer_registered_{false};
  bool runtime_active_{false};
  bool owns_ui_program_cache_{false};
};

}

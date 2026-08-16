#pragma once

#include "openwow/game/minimap_ping.h"
#include "openwow/game/minimap_system.h"
#include "openwow/game/simple_script.h"
#include "openwow/game/commerce/mail/adapters/ui/mail_stationery_choices.h"
#include "openwow/ui/framexml/ui_frame.h"
#include "openwow/ui/game/addon_runtime_loader.h"
#include "openwow/ui/game/debug/ui_debug_snapshot.h"
#include "openwow/ui/game/framescript/core/frame_types_widgets.h"
#include "openwow/ui/game/minimap_surface_submission.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/ui/game/nameplate_frame_manager.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/frame_event_runtime.h"
#include "openwow/ui/game/runtime/frame_materializer.h"
#include "openwow/ui/game/runtime/frame_input_router.h"
#include "openwow/ui/game/runtime/frame_traversal_index.h"
#include "openwow/ui/game/runtime/movie_frame_runtime.h"
#include "openwow/ui/game/runtime/movie_recording_runtime.h"
#include "openwow/ui/surfaces/game/adapters/media/movie_playback_adapter.h"
#include "openwow/ui/game/runtime/retained_layout.h"
#include "openwow/ui/game/runtime/framexml_runtime_loader.h"
#include "openwow/ui/game/runtime/render/ui_render_resources.h"
#include "openwow/ui/game/runtime/world_lua_runtime.h"
#include "openwow/ui/game/runtime/world_ui_runtime_host.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/ui/game/runtime/ui_performance_counters.h"
#include "openwow/ui/game/world_map_system.h"
#include "openwow/vfs/virtual_file_system.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "openwow/game/world_session_fwd.h"

struct lua_State;

namespace openwow::ui::lua {
class FrameScriptRuntime;
struct NativeBindingCatalog;
}

namespace openwow::ui::display {
class ProductionDisplaySettingsRuntime;
}

namespace openwow::game {
namespace actions::held_cursor {
class HeldCursor;
}
class CursorSurface;
class GameLoop;
}

namespace openwow::ui::game {

class WorldUiLifecycleCommandPort;
struct TooltipFrameStackSnapshot;

namespace runtime::render {
class UiCompositor;
}

}

namespace openwow::render::ui {
struct MeshVertex;
class TextRenderer;
class UiRenderer;

}

namespace openwow::render::m2 {
class M2System;
}

namespace openwow::render {
class BgfxTextCache;
class ModelPortrait;
class PortraitRenderer;
class TextureManager;
class WorldFrame;

}

namespace openwow::ui::widgets {

class CScriptObject;
class StatusBar;

}

namespace openwow::world {
class WorldCamera;
}
namespace openwow::audio { class SoundRuntime; }

namespace openwow::ui::game {

class GameUIManager final {
public:
  using PerformanceCounters = runtime::UiPerformanceCounters;

  struct IsolatedRuntime final {};

  GameUIManager(IsolatedRuntime,
                openwow::render::TextureManager& texture_manager,
                 openwow::render::m2::M2System& m2_system,
                 openwow::render::WorldFrame& world_frame,
                 openwow::world::WorldCamera& world_camera,
                 openwow::audio::SoundRuntime& sound_runtime);
  GameUIManager(
      openwow::ui::display::ProductionDisplaySettingsRuntime& runtime,
      openwow::render::TextureManager& texture_manager,
      openwow::render::m2::M2System& m2_system,
      openwow::render::WorldFrame& world_frame,
      openwow::world::WorldCamera& world_camera,
      openwow::audio::SoundRuntime& sound_runtime);
  ~GameUIManager();
  void BindRendererContext(
      openwow::render::api::RendererContext* renderer_context);
  [[nodiscard]] openwow::ui::MinimapSystem& minimap_state() noexcept {
    return minimap_state_;
  }
  [[nodiscard]] const openwow::ui::MinimapSystem& minimap_state() const noexcept {
    return minimap_state_;
  }
  [[nodiscard]] openwow::ui::WorldMapSystem& world_map() noexcept {
    return world_map_;
  }
  [[nodiscard]] const openwow::ui::WorldMapSystem& world_map() const noexcept {
    return world_map_;
  }
  [[nodiscard]] openwow::game::MinimapSystem& minimap_content() noexcept {
    return minimap_content_;
  }
  [[nodiscard]] const openwow::game::MinimapSystem& minimap_content() const noexcept {
    return minimap_content_;
  }
  [[nodiscard]] openwow::game::MinimapPingSystem& minimap_ping() noexcept {
    return minimap_ping_;
  }
  [[nodiscard]] const openwow::game::MinimapPingSystem& minimap_ping() const noexcept {
    return minimap_ping_;
  }
  [[nodiscard]] openwow::render::WorldFrame& world_frame() const noexcept {
    return world_frame_;
  }
  [[nodiscard]] openwow::world::WorldCamera& world_camera() const noexcept {
    return world_camera_;
  }
  void BindHeldCursor(
      openwow::game::actions::held_cursor::HeldCursor& held_cursor) noexcept {
    held_cursor_ = &held_cursor;
  }

  [[nodiscard]] const PerformanceCounters& performance_counters() const noexcept;
  void ResetPerformanceCounters() noexcept;

  GameUIManager(const GameUIManager &) = delete;
  GameUIManager &operator=(const GameUIManager &) = delete;
  bool Initialize(const openwow::vfs::VirtualFileSystem *vfs,
                  openwow::game::WorldSession *session,
                  openwow::game::CursorSurface& cursor);

  bool LoadDefaultUI(std::function<void(float)> progress_callback = {},
                     UiLoadStatusSink *status_sink = nullptr);

  bool LoadToc(const std::string &toc_path, UiLoadStatusSink *status_sink = nullptr,
               TocLoadProgress *progress = nullptr,
               std::string_view addon_name = {},
               openwow::core::MD5Context *digest = nullptr);

  void Shutdown();

  void SaveSavedVariables();

  [[nodiscard]] const AddonRuntimeIdentity& persistence_identity() const {
    return ui_runtime_host_->persistence_identity();
  }

  [[nodiscard]] bool is_loaded() const {
    return ui_runtime_host_->loaded();
  }
  [[nodiscard]] bool is_initialized() const {
    return ui_runtime_host_->initialized();
  }

  void BindWorldUiLifecycleCommands(WorldUiLifecycleCommandPort* commands);
  void RequestWorldUiReload();

  void Update(float dt);

  void DestroyNamedFrame(const std::string &name);

  void Render(std::uint8_t view_id, float screen_w, float screen_h,
              std::uint64_t compositor_generation = 0u,
              std::uint8_t offscreen_view_begin = 160u,
              std::uint8_t offscreen_view_count = 16u);

  void SetMinimapSurfaceSubmitter(
      MinimapSurfaceSubmitter submitter) {
    minimap_surface_submitter_ = std::move(submitter);
  }

  [[nodiscard]] float screen_width() const {
    return retained_layout_.viewport_width();
  }

  [[nodiscard]] float screen_height() const {
    return retained_layout_.viewport_height();
  }

  [[nodiscard]] float root_scale() const {
    return retained_layout_.root_scale();
  }

  void SetViewportSize(float width, float height);

  void SetRootScale(float scale, bool force = false);
  [[nodiscard]] lua_State *lua_state() const {
    return world_lua_runtime_->state();
  }

  [[nodiscard]] const openwow::vfs::VirtualFileSystem *vfs() const {
    return vfs_;
  }

  [[nodiscard]] openwow::game::WorldSession *world_session() const {
    return session_;
  }

  void SetTextureLoader(runtime::render::UiTextureLoad func) {
    render_resources_->SetTextureLoader(std::move(func));
  }

  [[nodiscard]] runtime::FrameMaterializer& frame_materializer() noexcept {
    return *frame_materializer_;
  }
  [[nodiscard]] const runtime::FrameMaterializer& frame_materializer() const noexcept {
    return *frame_materializer_;
  }

  void NotifyFrameInputCategoryMutation(const std::string &frame_name, bool reindex_only);

  bool BuildFrameStackSnapshot(bool show_hidden, TooltipFrameStackSnapshot *out_snapshot);

  [[nodiscard]] debug::UiDebugSnapshot BuildDebugSnapshot(
      const debug::UiDebugQuery& query);
  void SetDebugSubmissionReceiptsEnabled(bool enabled);

  [[nodiscard]] NameplateFrameManager& nameplate_frames() noexcept {
    return nameplate_frames_;
  }
  [[nodiscard]] const NameplateFrameManager& nameplate_frames() const noexcept {
    return nameplate_frames_;
  }

  [[nodiscard]] runtime::FrameStore& frame_store() noexcept {
    return frame_store_;
  }
  [[nodiscard]] const runtime::FrameStore& frame_store() const noexcept {
    return frame_store_;
  }
  [[nodiscard]] runtime::RetainedLayout& retained_layout() noexcept {
    return retained_layout_;
  }
  [[nodiscard]] const runtime::RetainedLayout& retained_layout() const noexcept {
    return retained_layout_;
  }
  [[nodiscard]] runtime::FrameInputRouter& input_router() noexcept {
    return frame_input_router_;
  }
  [[nodiscard]] const runtime::FrameInputRouter& input_router() const noexcept {
    return frame_input_router_;
  }
  [[nodiscard]] runtime::FrameEventRuntime& frame_events() noexcept {
    return frame_event_runtime_;
  }
  [[nodiscard]] const runtime::FrameEventRuntime& frame_events() const noexcept {
    return frame_event_runtime_;
  }
  [[nodiscard]] runtime::MovieFrameRuntime& movie_runtime() noexcept {
    return movie_frame_runtime_;
  }
  [[nodiscard]] const runtime::MovieFrameRuntime& movie_runtime() const noexcept {
    return movie_frame_runtime_;
  }
  [[nodiscard]] runtime::MovieRecordingRuntime& movie_recording_runtime()
      noexcept {
    return movie_recording_runtime_;
  }
  [[nodiscard]] const runtime::MovieRecordingRuntime& movie_recording_runtime()
      const noexcept {
    return movie_recording_runtime_;
  }
  void BindMovieRecordingBackend(
      std::unique_ptr<runtime::MovieRecordingBackend> backend) noexcept {
    movie_recording_runtime_.BindBackend(std::move(backend));
  }

 private:
  explicit GameUIManager(
      openwow::ui::display::ProductionDisplaySettingsRuntime* runtime,
      openwow::render::TextureManager& texture_manager,
      openwow::render::m2::M2System& m2_system,
      openwow::render::WorldFrame& world_frame,
      openwow::world::WorldCamera& world_camera,
      openwow::audio::SoundRuntime& sound_runtime);
  friend class openwow::game::GameLoop;
  friend class runtime::WorldLuaRuntime;
  friend class runtime::FrameXmlRuntimeLoader;
  friend class runtime::WorldUiRuntimeHost;

  void FireWorldEntryPhaseEvent(const char *event_name);

  void BeforeFrameBindingRelease(std::string_view name, int lua_ref);
  void AfterFrameIdentityRelease(std::string_view name);
  void InvalidateFrameHierarchy();
  void InvalidateFrameLayoutGraph();
  void InvalidateFramePaintOrder();
  void InvalidateFrameHitTest();
  openwow::render::TextureManager& texture_manager_;
  openwow::render::m2::M2System& m2_system_;
  openwow::render::WorldFrame& world_frame_;
  openwow::world::WorldCamera& world_camera_;
  openwow::audio::SoundRuntime& sound_runtime_;
  runtime::FrameStore frame_store_;
  runtime::RetainedLayout retained_layout_;
  runtime::FrameTraversalIndex frame_traversal_index_;
  runtime::FrameInputRouter frame_input_router_;
  runtime::FrameEventRuntime frame_event_runtime_;
  MoviePlaybackAdapter movie_playback_adapter_;
  runtime::MovieFrameRuntime movie_frame_runtime_;
  runtime::MovieRecordingRuntime movie_recording_runtime_;
  std::unique_ptr<openwow::ui::display::ProductionDisplaySettingsRuntime>
      isolated_display_settings_runtime_;
  openwow::ui::display::ProductionDisplaySettingsRuntime*
      display_settings_runtime_{nullptr};
  WorldUiLifecycleCommandPort* lifecycle_commands_{nullptr};

  std::unique_ptr<runtime::render::UiRenderResources> render_resources_;
  openwow::game::actions::held_cursor::HeldCursor* held_cursor_{nullptr};
  MinimapSurfaceSubmitter minimap_surface_submitter_;
  openwow::ui::MinimapSystem minimap_state_;
  openwow::game::MinimapSystem minimap_content_;
  openwow::game::MinimapPingSystem minimap_ping_;
  openwow::ui::WorldMapSystem world_map_;
  std::unique_ptr<runtime::render::UiCompositor> compositor_;
  std::unique_ptr<runtime::FrameMaterializer> frame_materializer_;

  NameplateFrameManager nameplate_frames_;
  std::unique_ptr<runtime::WorldLuaRuntime> world_lua_runtime_;
  std::unique_ptr<runtime::FrameXmlRuntimeLoader> frame_xml_loader_;
  std::unique_ptr<runtime::WorldUiRuntimeHost> ui_runtime_host_;
  std::unique_ptr<runtime::WorldUiRuntimeContext> runtime_context_;
  const openwow::vfs::VirtualFileSystem *vfs_{nullptr};
  openwow::game::WorldSession *session_{nullptr};
  openwow::game::MailStationeryChoices mail_stationery_choices_;
  mutable PerformanceCounters performance_counters_{};

  std::uint64_t published_scroll_frame_ranges_generation_{
      frame_api::kUnpublishedScrollFrameRanges};

};

}

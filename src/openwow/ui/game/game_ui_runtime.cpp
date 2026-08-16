#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/runtime/render/ui_compositor.h"
#include "openwow/ui/surfaces/game/runtime/world_ui_lifecycle_command.h"
#include "openwow/audio/playback/audio_engine.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/input/hid_manager.h"
#include "openwow/platform/adapters/ime/os_ime.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/ui/display/settings/adapters/production_display_settings_runtime.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/game_ui_scale.h"
#include "openwow/ui/game/framescript/core/frame_types_widgets.h"
#include "openwow/ui/game/tooltip_object_bridge.h"
#include "openwow/ui/game/tooltip_frame_sync.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <lua.hpp>

#include <memory>
#include <string>
#include <utility>

namespace openwow::ui::game {
GameUIManager::GameUIManager(
    IsolatedRuntime, openwow::render::TextureManager& texture_manager,
    openwow::render::m2::M2System& m2_system,
    openwow::render::WorldFrame& world_frame,
    openwow::world::WorldCamera& world_camera,
    openwow::audio::SoundRuntime& sound_runtime)
    : GameUIManager(nullptr, texture_manager, m2_system, world_frame,
                    world_camera, sound_runtime) {}

GameUIManager::GameUIManager(
    openwow::ui::display::ProductionDisplaySettingsRuntime& runtime,
    openwow::render::TextureManager& texture_manager,
    openwow::render::m2::M2System& m2_system,
    openwow::render::WorldFrame& world_frame,
    openwow::world::WorldCamera& world_camera,
    openwow::audio::SoundRuntime& sound_runtime)
    : GameUIManager(&runtime, texture_manager, m2_system, world_frame,
                    world_camera, sound_runtime) {}

GameUIManager::GameUIManager(
    openwow::ui::display::ProductionDisplaySettingsRuntime* runtime,
    openwow::render::TextureManager& texture_manager,
    openwow::render::m2::M2System& m2_system,
    openwow::render::WorldFrame& world_frame,
    openwow::world::WorldCamera& world_camera,
    openwow::audio::SoundRuntime& sound_runtime)
    : texture_manager_(texture_manager),
      m2_system_(m2_system),
      world_frame_(world_frame),
      world_camera_(world_camera),
      sound_runtime_(sound_runtime),
      frame_store_({
          .before_binding_release =
              [this](const std::string_view name, const int ref) {
                BeforeFrameBindingRelease(name, ref);
              },
          .after_identity_release = [this](const std::string_view name) {
            AfterFrameIdentityRelease(name);
          },
          .hierarchy_invalidated = [this] { InvalidateFrameHierarchy(); },
          .layout_graph_invalidated = [this] {
            InvalidateFrameLayoutGraph();
          },
          .paint_order_invalidated = [this] { InvalidateFramePaintOrder(); },
          .order_key_invalidated =
              [this](const std::string_view key) {
                frame_traversal_index_.InvalidateFrameOrder(
                    key, runtime::FrameTraversalIndex::OrderInvalidationKind::
                             kOrderKey);
              },
          .inherited_order_invalidated =
              [this](const std::string_view key) {
                frame_traversal_index_.InvalidateFrameOrder(
                    key, runtime::FrameTraversalIndex::OrderInvalidationKind::
                             kInherited);
              },
          .hit_test_invalidated = [this] { InvalidateFrameHitTest(); },
      }),
      retained_layout_(frame_store_, {
          .order_invalidated = [this] {
            frame_traversal_index_.InvalidatePaintOrder();
          },
          .order_key_invalidated =
              [this](const std::string_view key) {
                frame_traversal_index_.InvalidateFrameOrder(
                    key, runtime::FrameTraversalIndex::OrderInvalidationKind::
                             kOrderKey);
              },
          .inherited_order_invalidated =
              [this](const std::string_view key) {
                frame_traversal_index_.InvalidateFrameOrder(
                    key, runtime::FrameTraversalIndex::OrderInvalidationKind::
                             kInherited);
              },
          .on_update_order_invalidated = [this] {
            frame_event_runtime_.MarkOnUpdateOrderDirty();
          },
          .hit_test_invalidated = [this] {
            frame_traversal_index_.InvalidateHitTest();
          },
      }),
      frame_traversal_index_(frame_store_, retained_layout_),
      frame_input_router_(frame_store_, frame_traversal_index_, retained_layout_),
      frame_event_runtime_({
          .frames = frame_store_,
          .traversal = frame_traversal_index_,
          .fire_world_entry_phase =
              [this](const char* event_name) {
                FireWorldEntryPhaseEvent(event_name);
              },
      }),
      movie_frame_runtime_({
          .frames = frame_store_,
          .audio = sound_runtime_,
          .playback_started = [this](const std::string_view path) {
            movie_playback_adapter_.PlaybackStarted(path);
          },
          .playback_finished = [this] {
            movie_playback_adapter_.PlaybackFinished();
          },
      }),
      isolated_display_settings_runtime_(
          runtime == nullptr
              ? std::make_unique<
                    openwow::ui::display::ProductionDisplaySettingsRuntime>(
                    openwow::ui::display::ProductionDisplaySettingsRuntime::
                        IsolatedRuntime{})
              : nullptr),
      display_settings_runtime_(
          runtime != nullptr ? runtime
                             : isolated_display_settings_runtime_.get()),
      render_resources_(
          std::make_unique<runtime::render::UiRenderResources>(
              texture_manager_, m2_system_)),
      minimap_ping_(minimap_state_),
      compositor_(std::make_unique<runtime::render::UiCompositor>(
          runtime::render::UiCompositor::Dependencies{
              .traversal = frame_traversal_index_,
              .frames = frame_store_,
              .layout = retained_layout_,
              .input = frame_input_router_,
              .resources = *render_resources_,
              .textures = texture_manager_,
              .m2 = m2_system_,
              .world_map = world_map_,
          })),
      frame_materializer_(std::make_unique<runtime::FrameMaterializer>(
          runtime::FrameMaterializer::Dependencies{
              .frames = frame_store_,
              .layout = retained_layout_,
              .traversal = frame_traversal_index_,
              .input = frame_input_router_,
              .minimap_state = minimap_state_,
              .minimap_content = minimap_content_,
              .world_frame = world_frame_,
              .register_on_update =
                  [this](const std::string& name, std::uint8_t strata,
                         std::int32_t level) {
                     frame_event_runtime_.RegisterOnUpdate(name, strata, level);
                   },
           })),
      nameplate_frames_(*frame_materializer_, frame_store_),
      world_lua_runtime_(std::make_unique<runtime::WorldLuaRuntime>(*this)),
      frame_xml_loader_(
          std::make_unique<runtime::FrameXmlRuntimeLoader>(*this)),
      ui_runtime_host_(std::make_unique<runtime::WorldUiRuntimeHost>(*this)) {

  retained_layout_.BindTextureNaturalSizeSource(render_resources_.get());

  retained_layout_.BindModelNaturalSizeSource(render_resources_.get());
  runtime_context_ = std::make_unique<runtime::WorldUiRuntimeContext>(
      runtime::WorldUiRuntimeContext::Ports{
          .frames = frame_store_,
          .layout = retained_layout_,
          .input = frame_input_router_,
          .materializer = *frame_materializer_,
          .events = frame_event_runtime_,
          .movies = movie_frame_runtime_,
          .movie_recording = movie_recording_runtime_,
          .traversal = frame_traversal_index_,
          .lua = *world_lua_runtime_,
          .runtime_host = *ui_runtime_host_,
          .frame_xml_loader = *frame_xml_loader_,
          .minimap = minimap_state_,
          .minimap_ping = minimap_ping_,
          .world_map = world_map_,
          .textures = texture_manager_,
          .world_frame = world_frame_,
          .world_camera = world_camera_,
          .sound = sound_runtime_,
          .vfs = vfs_,
          .session = session_,
          .notify_frame_mutation =
              [this](const std::string& name, const bool reindex_only) {
                NotifyFrameInputCategoryMutation(name, reindex_only);
              },
           .frame_stack_snapshot =
               [this](const bool show_hidden,
                      TooltipFrameStackSnapshot* snapshot) {
                 return BuildFrameStackSnapshot(show_hidden, snapshot);
               },
           .set_root_scale = [this](const float scale, const bool force) {
             SetRootScale(scale, force);
           },
           .request_world_ui_reload = [this] { RequestWorldUiReload(); },
       });
  openwow::input::BindWorldUiInputRouter(&frame_input_router_);
}

GameUIManager::~GameUIManager() {
  openwow::input::BindWorldUiInputRouter(nullptr);
  BindRendererContext(nullptr);
  Shutdown();
}

void GameUIManager::BindRendererContext(
    openwow::render::api::RendererContext* renderer_context) {
  render_resources_->BindRendererContext(renderer_context);
}

const GameUIManager::PerformanceCounters&
GameUIManager::performance_counters() const noexcept {
  const auto& materialization = frame_materializer_->metrics();
  performance_counters_.frame_tree_finalize_dispatches = materialization.finalize_dispatches;
  performance_counters_.frame_tree_finalize_stack_peak = materialization.finalize_stack_peak;
  performance_counters_.frame_tree_plans = materialization.tree_plans;
  performance_counters_.frame_tree_source_nodes = materialization.tree_source_nodes;
  performance_counters_.frame_tree_inherited_nodes = materialization.tree_inherited_nodes;
  performance_counters_.frame_tree_plan_nodes = materialization.tree_plan_nodes;
  performance_counters_.frame_tree_plan_node_peak = materialization.tree_plan_node_peak;
  performance_counters_.default_frame_xml_tree_plans = materialization.default_tree_plans;
  performance_counters_.default_frame_xml_tree_source_nodes = materialization.default_tree_source_nodes;
  performance_counters_.default_frame_xml_tree_inherited_nodes = materialization.default_tree_inherited_nodes;
  performance_counters_.default_frame_xml_tree_plan_nodes = materialization.default_tree_plan_nodes;
  performance_counters_.default_frame_xml_tree_plan_node_peak = materialization.default_tree_plan_node_peak;
  performance_counters_.frame_bindings_created = materialization.bindings_created;
  performance_counters_.texture_bindings_created = materialization.texture_bindings_created;
  performance_counters_.font_string_bindings_created = materialization.font_string_bindings_created;
  const auto& traversal = frame_traversal_index_.metrics();
  performance_counters_.traversal_snapshot_rebuilds = traversal.snapshot_rebuilds;
  performance_counters_.hit_test_cache_rebuilds = traversal.hit_test_rebuilds;
  performance_counters_.traversal_entries = traversal.traversal_entries;
  performance_counters_.hit_test_index_entries = traversal.hit_test_entries;
  performance_counters_.last_hit_test_candidates = traversal.last_hit_test_candidates;
  return performance_counters_;
}

void GameUIManager::ResetPerformanceCounters() noexcept {
  performance_counters_ = {};
  frame_materializer_->ResetMetrics();
  frame_traversal_index_.ResetMetrics();
}

void GameUIManager::SetViewportSize(const float width, const float height) {
  if (retained_layout_.SetViewport(width, height) &&
      frame_xml_loader_->post_bootstrap_complete()) {
    SyncGameUiScaleFromCVars(*this, true);
  }
}

void GameUIManager::SetRootScale(const float scale, const bool force) {
  retained_layout_.SetRootScale(
      scale, force, frame_materializer_->loading_default_frame_xml());
}

bool GameUIManager::Initialize(const openwow::vfs::VirtualFileSystem *vfs,
                               openwow::game::WorldSession *session,
                               openwow::game::CursorSurface& cursor) {
  return ui_runtime_host_->Initialize(vfs, session, cursor);
}

void GameUIManager::BindWorldUiLifecycleCommands(
    WorldUiLifecycleCommandPort* commands) {
  lifecycle_commands_ = commands;
}

void GameUIManager::RequestWorldUiReload() {
  if (lifecycle_commands_ != nullptr) {
    lifecycle_commands_->RequestWorldUiReload();
  }
}

bool GameUIManager::LoadDefaultUI(std::function<void(float)> progress_callback,
                                  UiLoadStatusSink *status_sink) {
  return ui_runtime_host_->LoadDefaultUI(std::move(progress_callback), status_sink);
}

bool GameUIManager::LoadToc(const std::string &toc_path, UiLoadStatusSink *status_sink,
                            TocLoadProgress *progress,
                            const std::string_view addon_name,
                            openwow::core::MD5Context *digest) {
  return ui_runtime_host_->LoadToc(toc_path, status_sink, progress, addon_name,
                                digest);
}

void GameUIManager::SaveSavedVariables() {
  ui_runtime_host_->SaveSavedVariables();
}

void GameUIManager::Shutdown() {
  ui_runtime_host_->Shutdown();
}

void GameUIManager::Update(float dt) {
  if (!is_initialized() || lua_state() == nullptr)
    return;

  GameUI_PollScreenshotCompletions();

  frame_api::UpdateLuaTooltipObjects(lua_state(), dt);
  frame_api::RefreshCursorAnchoredTooltipFrames(lua_state());

  if (frame_traversal_index_.order_dirty()) {
    frame_traversal_index_.Rebuild(root_scale(), screen_height());
  }

  frame_input_router_.UpdateFocusedEditBoxInputLanguage();

  openwow::platform::IME_ManageContextAssociation();

  frame_input_router_.FlushPendingEditBoxStateUpdates();

  frame_input_router_.UpdateFocusedEditBoxCaretBlink(dt);

  frame_api::RefreshScrollFrameWidgetState(
      lua_state(), &published_scroll_frame_ranges_generation_);

  frame_event_runtime_.Update(dt);
  movie_frame_runtime_.Update(dt);

  nameplate_frames_.Update();
}

void GameUIManager::DestroyNamedFrame(const std::string &name) {
  if (!is_initialized() || lua_state() == nullptr || name.empty()) {
    return;
  }
  frame_store_.DestroySubtree(name);
}

void GameUIManager::Render(const std::uint8_t view_id, const float screen_w,
                           const float screen_h,
                           const std::uint64_t compositor_generation,
                           const std::uint8_t offscreen_view_begin,
                           const std::uint8_t offscreen_view_count) {
  if (!is_initialized()) return;
  SetViewportSize(screen_w, screen_h);
  const auto& movie_player = movie_frame_runtime_.player();
  compositor_->Render(runtime::render::UiCompositorFrame{
      .lua = lua_state(),
      .session = session_,
      .vfs = vfs_,
      .minimap_surface_submitter = minimap_surface_submitter_,
      .movie = {
          .owner = movie_frame_runtime_.ActiveFrameName(),
          .rgba = movie_player.CurrentFrameRGBA(),
          .width = movie_player.FrameWidth(),
          .height = movie_player.FrameHeight(),
          .version = movie_player.FrameVersion(),
      },
      .telemetry = {
          performance_counters_.last_render_candidates,
          performance_counters_.last_render_lua_visibility_queries,
          performance_counters_.last_render_generation,
          performance_counters_.last_render_world_map_descendant_submissions,
          performance_counters_.last_render_world_map_background_submissions,
          performance_counters_.last_render_world_map_detail_tile_submissions,
          performance_counters_.last_render_character_panel_descendant_submissions,
          performance_counters_.last_render_character_panel_background_submissions,
          performance_counters_.last_render_character_model_submitted,
          performance_counters_.last_render_player_frame_background_submissions,
          performance_counters_.last_render_player_portrait_submitted,
          performance_counters_.last_render_player_health_submitted,
          performance_counters_.last_render_player_power_submitted,
          performance_counters_.last_render_action_icon_submitted,
          performance_counters_.last_render_chat_content_submitted,
      },
      .view_id = view_id,
      .generation = compositor_generation,
      .offscreen_view_begin = offscreen_view_begin,
      .offscreen_view_count = offscreen_view_count,
  });
}

void GameUIManager::FireWorldEntryPhaseEvent(const char *const event_name) {
  const auto full_before = retained_layout_.metrics().full_resolves;
  const auto incremental_before = retained_layout_.metrics().incremental_resolves;
  const auto rebuilds_before = retained_layout_.metrics().dependency_graph_rebuilds;
  const auto plans_before = performance_counters_.frame_tree_plans;
  const auto plan_nodes_before = performance_counters_.frame_tree_plan_nodes;
  const auto objects_before = frame_store_.size();
  const auto tracked_refs_before = frame_store_.binding_count();
  frame_event_runtime_.dispatcher().FireEvent(event_name);
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "GameUIManager layout phase: event=" + std::string(event_name) +
          " objects_created=" +
          std::to_string(frame_store_.size() - objects_before) +
          " tracked_refs_created=" +
          std::to_string(frame_store_.binding_count() - tracked_refs_before) +
          " frame_plans=" +
          std::to_string(performance_counters_.frame_tree_plans -
                         plans_before) +
          " plan_nodes=" +
          std::to_string(performance_counters_.frame_tree_plan_nodes -
                         plan_nodes_before) +
          " full_resolves=" +
          std::to_string(retained_layout_.metrics().full_resolves - full_before) +
          " incremental_resolves=" +
          std::to_string(retained_layout_.metrics().incremental_resolves - incremental_before) +
          " dependency_rebuilds=" +
          std::to_string(
              retained_layout_.metrics().dependency_graph_rebuilds - rebuilds_before) +
          " last_candidates=" +
          std::to_string(
              retained_layout_.metrics().last_resolve_candidates));
}

void GameUIManager::BeforeFrameBindingRelease(const std::string_view name,
                                              const int ref) {
  frame_event_runtime_.UnregisterOnUpdate(std::string(name));
  if (ref == LUA_NOREF || ref == LUA_REFNIL) {
    return;
  }
  frame_input_router_.BeforeFrameBindingRelease(ref);
  frame_event_runtime_.dispatcher().UnregisterAllForFrame(ref);
}

void GameUIManager::AfterFrameIdentityRelease(const std::string_view name) {
  const std::string key(name);
  retained_layout_.RemoveFrame(key);
  frame_traversal_index_.InvalidateHierarchy();

  frame_input_router_.AfterFrameIdentityRelease(name);
  movie_frame_runtime_.OnFrameDestroyed(name);
  frame_event_runtime_.MarkOnUpdateOrderDirty();
}

void GameUIManager::InvalidateFrameHierarchy() {
  retained_layout_.Invalidate();
  frame_traversal_index_.InvalidateHierarchy();
}

void GameUIManager::InvalidateFrameLayoutGraph() {
  retained_layout_.InvalidateGraph();
  frame_traversal_index_.InvalidateHierarchy();
}

void GameUIManager::InvalidateFramePaintOrder() {
  frame_traversal_index_.InvalidatePaintOrder();
}

void GameUIManager::InvalidateFrameHitTest() {
  frame_traversal_index_.InvalidateHitTest();
}

}

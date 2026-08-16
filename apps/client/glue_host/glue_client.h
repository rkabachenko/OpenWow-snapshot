#pragma once

#include "composition/client_helpers.h"
#include "glue_flow.h"
#include "glue_init_parity.h"
#include "realm_addon_handshake_composition.h"
#include "scenarios/scenario_runner.h"
#include "scenarios/scenario_world_ui_driver.h"
#include "sdl_glue_host.h"

#include "openwow/render/glue/glue_bgfx_renderer.h"
#include "openwow/audio/playback/sound_interface.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/activities/dance/model/dance_move_catalog.h"
#include "openwow/game/game_loop.h"
#include "openwow/game/character_world_runtime.h"
#include "openwow/game/realm_runtime.h"
#include "openwow/game/world_session.h"
#include "openwow/net/transport/network_recv_thread.h"
#include "openwow/net/transport/packet_queue.h"
#include "openwow/net/wotlk/protocol/auth_protocol.h"
#include "openwow/net/wotlk/realm_list.h"
#include "openwow/net/wotlk/protocol/world_protocol.h"
#include "openwow/platform/window/pending_window_event_queue.h"
#include "openwow/platform/window/stock_window_event_state.h"
#include "openwow/render/api/renderer_context.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/runtime/scheduling/frame_job_system.h"
#include "openwow/render/platform/gamma_controller.h"
#include "openwow/render/platform/present_pacer.h"
#include "openwow/render/diagnostics/render_submit_trace.h"
#include "openwow/render/ui/text_renderer.h"
#include "openwow/ui/glue/glue_binding_registry.h"
#include "openwow/ui/glue/glue_font_registry.h"
#include "openwow/ui/glue/glue_background_controller.h"
#include "openwow/ui/glue/glue_charselect_scene.h"
#include "openwow/ui/glue/character_customization_randomizer.h"
#include "openwow/ui/glue/glue_game_state.h"
#include "openwow/ui/glue/glue_lua_runtime.h"
#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/ui/screens/character_create_screen.h"
#include "openwow/ui/screens/character_select_screen.h"
#include "openwow/ui/screens/login_screen.h"
#include "openwow/ui/screens/realm_list_screen.h"
#include "openwow/ui/glue/glue_lua_event_trace.h"
#include "openwow/render/integration/ui/bgfx_display_device_adapter.h"
#include "openwow/ui/display/settings/adapters/production_display_settings_runtime.h"
#include "openwow/runtime/bootstrap/startup_trace.h"
#include "openwow/vfs/virtual_file_system.h"

#include <SDL2/SDL.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::debug::control {
class DebugControlServer;
}

namespace openwow::client {

class DebugUiControlAdapter;

struct ClientLaunchContext {
  std::filesystem::path game_root;
  std::filesystem::path enhanced_assets_root;
  std::filesystem::path diagnostic_output_root;
};

enum class UiMode {
  kLogin,
  kRealmDialog,
  kCharacterSelect,
  kCharacterCreate,
  kLoading,
  kInWorld,
};

struct GlueSceneState {
  std::string status_line;
  bool show_error{false};
};

class GlueClient {
 public:
  struct Options {
    SDL_Window* window{nullptr};
    ClientLaunchContext launch_context;
    std::optional<ScenarioOptions> scenario_opts;
    openwow::runtime::bootstrap::StartupTrace* startup_trace{nullptr};
    openwow::ui::glue::GlueLuaEventTrace* lua_trace{nullptr};
    std::optional<std::filesystem::path> ui_frame_tree_dump_path;
    std::optional<std::reference_wrapper<openwow::render::RenderSubmitTrace>> render_submit_trace;
    std::optional<std::filesystem::path> render_submit_trace_path;
  };

  explicit GlueClient(Options opts);
  ~GlueClient();

  GlueClient(const GlueClient&) = delete;
  GlueClient& operator=(const GlueClient&) = delete;

  bool Initialize();

  int Run();

  void Shutdown();

 private:

  bool InitCVars();

  bool InitVFS();

  void BuildAndPublishLoginVfs();

  bool InitGraphics();

  bool InitGlueUI();

  bool InitGameLoop();
  bool InitDebugControl();
  void ApplyClientCacheVersion(std::uint32_t version);

  void LoadGlueTocAndScripts();

  void CompleteGlueStartupTail();

  void HandleScreenTransition(const std::string& old_screen,
                              const std::string& new_screen,
                              bool apply_background_transition);

  openwow::ui::glue::GlueLuaValue MakeLuaString(std::string value) const;
  openwow::ui::glue::GlueLuaValue MakeLuaNumber(double value) const;
  openwow::ui::glue::GlueLuaValue MakeLuaBool(bool value) const;

  openwow::ui::LuaRunResult DispatchWidgetEvent(
      const std::string& widget_name,
      const std::string& event_name,
      const std::string& event_source,
      const std::vector<openwow::ui::glue::GlueLuaValue>& args);
  openwow::ui::LuaRunResult DispatchButtonClick(const std::string& widget_name,
                                                const std::string& button_name,
                                                bool is_down);

  void FireGlueEvent(const std::string& event_name,
                     const std::vector<openwow::ui::glue::GlueLuaValue>& args);

  void FireInitialScreenEventIfNeeded();

  void RunWidgetOnLoadPass();

  std::string FocusedEditbox() const;
  void UpdateTextInputState();
  void UpdateFocusedEditBoxInputLanguage();

  static bool IsUsernameEditbox(const std::string& name);
  static bool IsPasswordEditbox(const std::string& name);
  std::string FindUsernameWidget() const;
  std::string FindPasswordWidget() const;

  void SetMode(UiMode next_mode);
  void SyncGlueViewportFromWindow();
  void RefreshLayout();
  void DispatchPendingScrollRangeChangedEvents();

  void DoLoginAttempt();
  void ReloadLoginResources();
  void RefreshLoginConfiguration();
  void EnterSelectedRealm();
  void ShowCharacterCreate(bool dispatch_click = true);
  void EnterSelectedCharacter(bool dispatch_click = true);
  bool EnterOfflineScenarioWorld();
  void ReturnFromWorldToGlue(const char* screen_name,
                             bool disconnect_realm_session);
  void HandleWorldTransportDisconnect();

  void PumpPendingWindowEvents();
  void HandleEvent(const SDL_Event& event);
  void HandleTextInput(const SDL_Event& event);
  void HandleMouseDown(const SDL_Event& event);
  void HandleMouseUp(const SDL_Event& event);
  void HandleKeyDown(const SDL_Event& event);
  void UpdateInWorldMouseButtonState(std::uint8_t button, bool pressed);
  void ReleaseInWorldMouseButtons();

  void DispatchRelativeCursorMotionTick();

  void HandleClipboardPaste(const std::string& focused);
  void HandleEditBackspace(const std::string& focused);
  void HandleEditDelete(const std::string& focused);
  void SyncLoginModelFromEditbox(const std::string& editbox,
                                 const std::string& value);

  void PumpGlueRequests(float dt = 0.016f);
  void SyncGlueFrameDepthTargets();
  void UpdateHoverState();

  void ApplyWindowFocusChange(bool focused);

  void ReconcileWindowFocus();
  void UpdateOnUpdateScripts(double elapsed_sec);
  void SyncScreenModels(float frame_delta_seconds);
  void DrainScreenshotNotifications();

  void Render(std::uint32_t now_ms, std::uint32_t frame_delta_ms, double elapsed_sec);
  void UpdateWindowTitle();

  bool TickScenario(ScenarioRunner::Stage stage, std::uint32_t now_ms);

  [[nodiscard]] bool IsBenchmarkRun() const;

  Options opts_;
  SDL_Window* window_{nullptr};
  ClientLaunchContext launch_context_;
  std::string auth_host_;
  std::uint16_t auth_port_{0};
  openwow::runtime::bootstrap::StartupTrace* startup_trace_{nullptr};
  openwow::ui::glue::GlueLuaEventTrace* lua_trace_{nullptr};
  std::optional<std::filesystem::path> ui_frame_tree_dump_path_;
  bool ui_frame_tree_dump_written_{false};
  std::optional<std::reference_wrapper<openwow::render::RenderSubmitTrace>> render_submit_trace_;
  std::optional<std::filesystem::path> render_submit_trace_path_;
  std::unique_ptr<openwow::debug::control::DebugControlServer>
      debug_control_server_;
  std::unique_ptr<DebugUiControlAdapter> debug_ui_control_adapter_;
  std::filesystem::path debug_control_endpoint_path_;

  openwow::vfs::VirtualFileSystem login_vfs_;
  std::optional<openwow::ui::glue::GlueFontRegistry> glue_fonts_;
  openwow::data::LoginResourceValidationResult validation_;

  openwow::core::FrameJobSystem frame_job_system_;

  openwow::render::TextureManager texture_manager_;
  openwow::render::m2::M2System m2_system_;
  std::unique_ptr<openwow::render::api::RendererContext> renderer_context_;
  openwow::render::GammaController gamma_controller_;
  openwow::render::PresentPacer present_pacer_;
  openwow::audio::SoundRuntime sound_runtime_;
  GlueBgfxRenderer glue_renderer_;
  openwow::render::ui::TextRenderer fps_overlay_text_renderer_;

  openwow::render::integration::ui::BgfxDisplayDeviceAdapter display_device_;
  openwow::ui::display::ProductionDisplaySettingsRuntime display_settings_{
      display_device_};
  openwow::ui::glue::GlueLuaRuntime glue_runtime_;
  openwow::ui::glue::GlueBindingRegistry glue_bindings_;
  openwow::ui::glue::GlueWidgetRuntime glue_widgets_;
  openwow::ui::glue::GlueBackgroundController background_controller_;
  openwow::ui::glue::GlueCharSelectScene char_select_scene_;
  openwow::ui::glue::GlueCharSelectScene char_customize_scene_;
  openwow::ui::glue::detail::LegacyAdlerRandom glue_random_;
  openwow::ui::glue::GlueGameState game_state_;
  SdlGlueHost glue_host_;

  openwow::ui::glue::GlueLuaLoadResult glue_load_;

  openwow::ui::screens::LoginScreen login_screen_;
  openwow::ui::screens::RealmListScreen realm_screen_;
  openwow::ui::screens::CharacterSelectScreen character_screen_;
  openwow::ui::screens::CharacterCreateScreen create_screen_;

  UiMode mode_{UiMode::kLogin};
  GlueSceneState scene_state_;

  std::optional<std::pair<int, int>> last_glue_cursor_;
  std::string pressed_widget_name_;
  std::string mouse_capture_widget_name_;
  std::unordered_map<std::string, std::uint32_t> button_last_click_time_ms_;
  std::unordered_set<std::string> button_clicks_in_progress_;
  std::string dragging_slider_name_;
  bool text_input_active_{false};

  bool text_input_reactivation_pending_{true};
  bool show_error_{false};
  bool enter_world_init_active_{false};
  std::uint32_t enter_world_init_started_at_ms_{0};
  std::optional<std::string> pending_initial_screen_after_movie_;
  int layout_width_{1280};
  int layout_height_{720};
  bool layout_dirty_{true};
  std::string last_window_title_;
  bool is_fullscreen_desktop_{false};
  bool window_focused_{false};
  bool running_{false};
  bool trace_input_{false};
  bool simple_ui_fast_path_enabled_{true};
  std::uint32_t startup_m2_flags_{0};

  openwow::data::dbc::DbcLoader dbc_loader_;
  std::optional<openwow::game::DanceMoveCatalog> dance_move_catalog_;
  openwow::data::DBCacheRuntime db_cache_runtime_;
  openwow::game::RealmRuntime realm_runtime_;

  openwow::game::GameLoop game_loop_;
  openwow::game::CharacterWorldRuntime character_world_runtime_;
  openwow::platform::PendingWindowEventQueue pending_window_events_;
  openwow::platform::StockWindowEventState stock_window_event_state_;
  bool left_mouse_held_{false};
  bool right_mouse_held_{false};

  openwow::net::NetworkRecvThread recv_thread_;

  std::optional<openwow::net::wotlk::RealmInfo> selected_realm_;
  std::string auth_session_token_;

  GlueFlowState glue_flow_state_;

  std::unordered_set<std::string> logged_lua_failures_;

  std::optional<ScenarioRunner> scenario_runner_;
  ScenarioWorldUiDriver scenario_world_ui_driver_;
  std::atomic_uint64_t successful_forward_start_packets_{0};
  std::atomic_uint64_t successful_movement_heartbeat_packets_{0};
  std::atomic_uint64_t successful_movement_stop_packets_{0};
  std::string scenario_forward_binding_key_;
  bool offline_scenario_world_active_{false};
  RealmAddonHandshakeComposition realm_addon_handshake_composition_;
};

}

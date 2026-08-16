#pragma once

#include "openwow/runtime/scheduling/frame_scheduler.h"
#include "openwow/runtime/time/game_time.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/media/playback/movie_player.h"
#include "openwow/ui/glue/glue_binding_registry.h"
#include "openwow/ui/glue/glue_font_registry.h"
#include "openwow/ui/glue/glue_game_state.h"
#include "openwow/ui/glue/glue_host.h"
#include "openwow/ui/glue/glue_lua_event_trace.h"
#include "openwow/ui/glue/glue_lua_value.h"
#include "openwow/ui/lua_run_result.h"
#include "openwow/ui/framexml_font_registry.h"
#include "openwow/vfs/virtual_file_system.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct lua_State;

namespace openwow::client {
class GlueClient;
}
namespace openwow::audio { class SoundRuntime; }

namespace openwow::ui::glue {

class GlueWidgetRuntime;

}

namespace openwow::ui::lua {
class FrameScriptRuntime;
}

namespace openwow::ui::display {
class ProductionDisplaySettingsRuntime;
}

namespace openwow::ui::game::detail {
struct SoundLuaContext;
}

namespace openwow::ui::glue {

struct GlueLuaLoadResult {
  bool ok{false};
  std::string error;
  std::vector<std::string> loaded_scripts;
};

class GlueLuaRuntime {
 public:
  struct IsolatedRuntime final {};

  GlueLuaRuntime(IsolatedRuntime, openwow::audio::SoundRuntime& sound_runtime);

  explicit GlueLuaRuntime(
      openwow::ui::display::ProductionDisplaySettingsRuntime& runtime,
      openwow::audio::SoundRuntime& sound_runtime);
  ~GlueLuaRuntime();
  GlueLuaRuntime(const GlueLuaRuntime&) = delete;
  GlueLuaRuntime& operator=(const GlueLuaRuntime&) = delete;

  GlueLuaLoadResult LoadScripts(const openwow::vfs::VirtualFileSystem& vfs,
                                const std::vector<std::string>& script_paths);

  void InitializeVm(const openwow::vfs::VirtualFileSystem& vfs);

  struct SingleScriptResult {
    bool ok{false};
    std::string error;
  };
  SingleScriptResult ExecuteFile(const std::string& script_path);

  SingleScriptResult ExecuteString(const std::string& lua_code, const std::string& source_name);

  SingleScriptResult RegisterXmlFont(const openwow::ui::FontDefinition& definition);

  void PublishNewWidgets(const std::vector<std::string>& widget_names);

  const std::vector<std::string>& loaded_scripts() const;

  void BindWidgetRuntime(GlueWidgetRuntime* widget_runtime);
  void BindBindingRegistry(const GlueBindingRegistry* bindings);
  void BindFontRegistry(const GlueFontRegistry* font_registry);
  void BindHost(GlueHost* host);
  void BindGameState(GlueGameState* game_state);
  void BindGameTimeData(const openwow::core::ida::GameTimeData* game_time);
  void BindDbcLoader(const openwow::data::dbc::DbcLoader* dbc);
  void BindLuaEventTrace(GlueLuaEventTrace* trace);
  bool HasFunction(const std::string& function_name) const;
  bool HasAnyFunction(const std::vector<std::string>& function_names) const;
  bool HasGlobal(const std::string& global_name) const;
  void RegisterAccountMsgGlueFunctions();
  void ShutdownForWorld() noexcept;
  [[nodiscard]] bool IsAttachedToGlue() const noexcept { return attached_to_glue_; }
  void UnsetGlobal(const std::string& global_name);

  std::string GetGlobalStringOrEmpty(const std::string& global_name) const;
  const openwow::vfs::VirtualFileSystem* vfs() const;
  const GlueFontRegistry* font_registry() const;
  [[nodiscard]] openwow::audio::SoundRuntime& sound_runtime() const noexcept {
    return sound_runtime_;
  }
  bool GlobalIsTable(const std::string& global_name) const;
  bool GlobalTableHasField(const std::string& table_name, const std::string& field_name) const;
  bool GlobalTableHasFunction(const std::string& table_name, const std::string& field_name) const;
  LuaRunResult RunFunction(const std::string& function_name);
  LuaRunResult DispatchFirstAvailable(const std::vector<std::string>& function_names,
                                      const std::string& event_source);
  LuaRunResult DispatchFirstAvailableWithArgs(const std::vector<std::string>& function_names,
                                             const std::string& event_source,
                                             const std::vector<std::string>& args,
                                             bool pass_self_arg);
  LuaRunResult DispatchFirstAvailableWithNumberArgs(const std::vector<std::string>& function_names,
                                                    const std::string& event_source,
                                                    const std::vector<double>& args,
                                                    bool pass_self_arg);
  LuaRunResult RunWidgetEvent(const std::string& widget_name,
                              const std::string& event_name,
                              const std::string& event_source,
                              const std::vector<GlueLuaValue>& args);

  LuaRunResult SetEditBoxTextProgrammatically(const std::string& widget_name,
                                              const std::string& text);

  bool SetEditBoxFocus(const std::string& widget_name);
  bool ClearEditBoxFocus(const std::string& widget_name);
  LuaRunResult RunInlineScript(const std::string& script_body,
                               const std::string& event_name,
                               const std::string& event_source,
                               const std::string& self_widget_name,
                               const std::vector<GlueLuaValue>& args);
  bool HasWidgetScript(const std::string& widget_name, const std::string& event_name) const;
  void InvalidateWidgetScriptCache(const std::string& widget_name = {},
                                   const std::string& event_name = {});

  void InvalidatePerFrameWidgetCache() noexcept;

  void ApplyFontStringTextKeys();
  void RegisterEvent(const std::string& widget_name, const std::string& event_name);
  void UnregisterEvent(const std::string& widget_name, const std::string& event_name);
  void UnregisterAllEvents(const std::string& widget_name);
  void RegisterAllEvents(const std::string& widget_name);
  bool IsEventRegistered(const std::string& widget_name, const std::string& event_name) const;
  std::vector<std::string> RegisteredWidgetsForEvent(const std::string& event_name) const;
  LuaRunResult DispatchRegisteredEvent(const std::string& event_name,
                                       const std::vector<GlueLuaValue>& extra_args = {});
  [[nodiscard]] std::function<void()> MakeAsyncRegisteredEventPoster(
      std::string event_name,
      std::vector<GlueLuaValue> extra_args = {}) const;

  bool IsMoviePlaying() const;
  bool StartMovie(const std::string& avi_path, int volume);
  void StopMovie();
  void DispatchActiveMovieKeyUp(const std::string& key_name);
  bool BeginWidgetSizing(const std::string& widget_name, int move_sizing_mode);
  bool StopWidgetMoveSizing(const std::string& widget_name);
  [[nodiscard]] bool IsWidgetMoveSizingActive(const std::string& widget_name) const;
  void UpdateMovie(double elapsed_seconds);
  void UpdateWidgetAnimations(const std::string& widget_name, double elapsed_seconds);
  void AdvanceFrame(double elapsed_seconds);

  void PumpFrameServices();

  void PumpCursorDrivenUpdates();

  void ClearHoveredWidget();
  void SetMovieWidgetName(const std::string& name) { movie_widget_name_ = name; }
  void SetDataDirectory(const std::filesystem::path& dir) { data_dir_ = dir; }

  [[nodiscard]] const openwow::media::MoviePlayer& GetMoviePlayer() const noexcept { return movie_player_; }

  void PumpVisibilityTransitions(int max_iterations = 8);
  void PumpVisibilityTransitionsFor(const std::string& widget_name,
                                    int max_iterations = 8);

  bool SetWidgetParentWithVisibilityLifecycle(const std::string& widget_name,
                                              const std::string& new_parent);

  void InitializeVisibilityForNewWidgets(const std::vector<std::string>& widget_names);

  void UpdateVisibilityCacheFor(const std::string& widget_name);
  const std::vector<std::string>& invocation_history() const;

  friend class ::openwow::client::GlueClient;

 private:
  explicit GlueLuaRuntime(
      openwow::ui::display::ProductionDisplaySettingsRuntime* runtime,
      openwow::audio::SoundRuntime& sound_runtime);
  void DestroyVmState() noexcept;
  void ResetVm();
  void DiscardMoviePlayback();
  LuaRunResult RunFunctionWithContext(const std::string& function_name,
                                      const std::string& self_widget_name);
  LuaRunResult RunFunctionWithThisAndArgs(const std::string& function_name,
                                         const std::string& self_widget_name,
                                         const std::vector<std::string>& args,
                                         bool pass_self_arg);
  LuaRunResult RunFunctionWithThisAndNumberArgs(const std::string& function_name,
                                                const std::string& self_widget_name,
                                                const std::vector<double>& args,
                                                bool pass_self_arg);
  LuaRunResult RunScriptRef(int script_ref,
                            const std::string& event_name,
                            const std::string& event_source,
                            const std::string& self_widget_name,
                            const std::vector<GlueLuaValue>& args);
  std::optional<int> CompileInlineScript(const std::string& event_source,
                                        const std::string& event_name,
                                        const std::string& script_body);
  std::vector<GlueScriptBinding> ResolveBindingsForWidgetEvent(const std::string& widget_name,
                                                               const std::string& event_name) const;
  bool HasWidgetGlobal(const std::string& widget_name) const;
  bool HasWidgetAnimationGroups(const std::string& widget_name) const;
  struct PerFrameWidgetEntry {
    std::string name;
    bool has_on_update{false};
    bool has_on_update_model{false};
    bool has_animation_groups{false};
  };
  const std::vector<PerFrameWidgetEntry>& PerFrameWidgetsInUpdateOrder();
  void DrainPostedEvents();
  bool ApplyActiveMoveSizingFromCachedCursor();
  void RefreshHoveredWidgetFromCachedCursor(bool motion);
  void ClearEditBoxFocusAfterEffectiveHide(const std::string& widget_name);
  void AutoFocusEditBoxAfterEffectiveShow(const std::string& widget_name);
  void SeedVisibilityCacheFromRuntime();
  struct VisibilitySnapshotEntry {
    std::string name;
    std::size_t parent_entry{std::numeric_limits<std::size_t>::max()};
    bool was_visible{false};
    std::uint64_t generation{0};
  };
  struct VisibilitySnapshotStep {
    std::size_t entry_index{0};
    bool entering{false};
  };
  struct VisibilitySnapshot {
    std::vector<VisibilitySnapshotEntry> entries;
    std::vector<VisibilitySnapshotStep> steps;
  };
  VisibilitySnapshot CaptureVisibilitySubtree(const std::string& widget_name) const;
  bool DispatchVisibilitySnapshot(VisibilitySnapshot& snapshot,
                                  bool force_hidden,
                                  bool clear_overrides);
  void ClearVisibilityOverrides(const VisibilitySnapshot& snapshot);
  std::string EffectiveVisibilityParent(const std::string& widget_name) const;
  static std::string WidgetFromEventSource(const std::string& event_source);
  static std::string WidgetFromFunctionName(const std::string& function_name);

  struct PostedEvent {
    std::string event_name;
    std::vector<GlueLuaValue> extra_args;
  };

  struct AsyncEventQueue {
    std::mutex mutex;
    bool accept_events{true};
    std::vector<PostedEvent> pending_events;
  };

  struct ActiveMoveSizingState {
    std::string widget_name;
    std::string relative_name;
    int mode{8};
    float cursor_x{0.0f};
    float cursor_y{0.0f};
    bool active{false};
  };

  std::unordered_map<std::string, std::string> scripts_;
  std::vector<std::string> invocation_history_;
  lua_State* lua_state_{nullptr};
  std::unique_ptr<openwow::ui::lua::FrameScriptRuntime> frame_script_runtime_;
  std::unique_ptr<openwow::ui::display::ProductionDisplaySettingsRuntime>
      isolated_display_settings_runtime_;
  openwow::ui::display::ProductionDisplaySettingsRuntime*
      display_settings_runtime_{nullptr};

  std::shared_ptr<lua_State*> xml_text_lua_state_;
  GlueWidgetRuntime* widget_runtime_{nullptr};
  const GlueBindingRegistry* bindings_{nullptr};
  const GlueFontRegistry* font_registry_{nullptr};
  openwow::ui::FontDefinitionRegistry xml_font_registry_;
  const openwow::vfs::VirtualFileSystem* vfs_{nullptr};
  GlueHost* host_{nullptr};
  GlueGameState* game_state_{nullptr};
  const openwow::core::ida::GameTimeData* game_time_{nullptr};
  const openwow::data::dbc::DbcLoader* dbc_loader_{nullptr};
  std::unordered_map<std::string, std::vector<std::string>> widgets_by_event_;
  std::unordered_map<std::string, std::vector<std::string>> events_by_widget_;
  mutable std::unordered_map<std::string, bool> widget_script_presence_cache_;
  std::vector<PerFrameWidgetEntry> per_frame_widget_cache_;
  std::uint64_t per_frame_widget_order_revision_{0};
  std::uint64_t per_frame_widget_cache_rebuild_count_{0};
  bool per_frame_widget_cache_dirty_{true};

  std::unordered_map<std::string, bool> last_effective_visible_;
  std::unordered_map<std::string, std::uint64_t> visibility_generations_;
  std::uint64_t last_visibility_revision_{0};
  std::unordered_set<std::string> logged_visibility_errors_;

  std::unordered_map<std::string, int> inline_script_refs_by_source_;
  std::vector<std::string> interleaved_loaded_scripts_;
  std::shared_ptr<AsyncEventQueue> async_event_queue_;

  openwow::media::MoviePlayer movie_player_;
  openwow::audio::SoundRuntime& sound_runtime_;
  std::unique_ptr<openwow::ui::game::detail::SoundLuaContext>
      sound_lua_context_;
  std::string movie_widget_name_;
  std::string hovered_widget_;
  ActiveMoveSizingState active_move_sizing_;
  std::filesystem::path data_dir_;

  GlueLuaEventTrace* lua_event_trace_{nullptr};
  openwow::core::CallbackHandle frame_scheduler_handle_{
      openwow::core::CallbackHandle::Invalid};
  bool attached_to_glue_{true};
};

}

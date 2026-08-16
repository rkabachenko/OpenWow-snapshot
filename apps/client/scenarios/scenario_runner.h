#pragma once

#include "scenario_world_oracle.h"

#include "openwow/ui/glue/glue_lua_runtime.h"
#include "openwow/ui/glue/glue_game_state.h"
#include "openwow/ui/glue/glue_widget_runtime.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::client {

enum class ScenarioForwardMovementCommand : std::uint8_t {
  kStart,
  kStop,
};

enum class ScenarioWorldUiAction : std::uint8_t {
  kInjectChatProbe,
  kClickActionButton,
  kZoomMinimap,
  kEnableNameplates,
  kRestoreTransientState,
  kOpenWorldMap,
  kCloseWorldMap,
  kOpenCharacterPanel,
  kCloseCharacterPanel,
  kRequestLogout,
};

struct ScenarioWorldUiActionResult {
  bool handled{false};
  bool state_changed{false};
  bool fallback_used{false};
  double value_before{0.0};
  double value_after{0.0};
  std::string error;
};

struct ScenarioPlayState {
  bool ready{false};
  bool connected{false};
  bool cinematic_playing{false};
  bool cinematic_presenting{false};
  bool cinematic_can_skip{false};
  bool forward_binding_available{false};
  bool forward_active{false};
  std::uint64_t mover_guid{0};
  std::uint64_t forward_start_packets_sent{0};
  std::uint64_t movement_heartbeat_packets_sent{0};
  std::uint64_t movement_stop_packets_sent{0};
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  std::size_t terrain_tiles_loaded{0};
  std::size_t object_instances{0};
  bool player_render_ready{false};
  bool player_visible_draw_submitted{false};
  float camera_desired_distance{0.0f};
  float camera_resolved_distance{0.0f};
  bool player_camera_alpha_visible{false};
  std::size_t game_ui_frames{0};
  bool game_ui_loaded{false};
  bool main_menu_visible{false};
  bool world_ui_regions_ready{false};
  bool world_ui_anchors_valid{false};
  bool world_ui_text_contained{false};
  bool world_ui_player_frame_ready{false};
  bool world_ui_player_portrait_ready{false};
  bool world_ui_health_power_ready{false};
  bool world_ui_unit_frames_ready{false};
  bool world_ui_action_icon_ready{false};
  bool world_ui_chat_ready{false};
  bool world_ui_minimap_ready{false};
  bool world_ui_world_map_ready{false};
  bool world_ui_character_panel_ready{false};
  bool world_ui_character_model_ready{false};
  bool world_ui_character_identity_ready{false};
  std::size_t world_ui_character_runtime_name_length{0};
  std::size_t world_ui_character_expected_name_length{0};
  bool world_ui_world_map_visible{false};
  bool world_ui_character_panel_visible{false};
  std::uint64_t world_ui_render_generation{0};
  std::size_t world_ui_world_map_descendant_submissions{0};
  std::size_t world_ui_world_map_background_submissions{0};
  std::size_t world_ui_world_map_detail_tile_submissions{0};
  std::size_t world_ui_character_panel_descendant_submissions{0};
  std::size_t world_ui_character_panel_background_submissions{0};
  bool final_backbuffer_ready{false};
  bool loading_screen_visible{false};
  bool loading_screen_sole_owner{false};
  bool loading_final_backbuffer_ready{false};
  std::uint64_t loading_render_submissions{0};
  std::uint64_t loading_self_presented_frames{0};
  std::uint64_t loading_coalesced_callbacks{0};
  bool nameplate_pipeline_ready{false};
  std::uint64_t nameplate_render_generation{0};
  bool logout_request_pending{false};
  bool logout_countdown_visible{false};
  float logout_countdown_seconds{0.0f};
  std::uint64_t frame_generation{0};
  std::size_t visible_nameplates{0};
  std::size_t ui_traversal_entries{0};
  std::size_t ui_render_candidates{0};
  std::uint32_t render_draw_calls{0};
  float render_cpu_time_ms{0.0f};
  float render_gpu_time_ms{0.0f};
  float camera_x{0.0f};
  float camera_y{0.0f};
  float camera_z{0.0f};
  float camera_yaw{0.0f};
  float camera_pitch{0.0f};
};

namespace detail {

inline bool HasPlayerRenderProof(const ScenarioPlayState& state) {
  return state.player_render_ready && state.player_visible_draw_submitted &&
         state.player_camera_alpha_visible;
}

inline bool HasWorldUiRenderProof(const ScenarioPlayState& state) {
  return state.game_ui_loaded && state.game_ui_frames > 0u &&
         state.main_menu_visible && state.world_ui_regions_ready &&
         state.world_ui_anchors_valid && state.world_ui_text_contained &&
         state.world_ui_player_frame_ready &&
         state.world_ui_player_portrait_ready &&
         state.world_ui_health_power_ready &&
         state.world_ui_unit_frames_ready &&
         state.world_ui_action_icon_ready && state.world_ui_chat_ready &&
         state.world_ui_minimap_ready && state.world_ui_world_map_ready &&
         state.world_ui_character_panel_ready;
}

inline bool HasNewerWorldUiSubmission(
    const ScenarioPlayState& state,
    const std::uint64_t mutation_frame_generation) {
  return state.final_backbuffer_ready &&
         state.frame_generation > mutation_frame_generation &&
         state.game_ui_frames > 0u;
}

inline bool HasCurrentChatProbePaint(
    const ScenarioPlayState& state,
    const std::uint64_t mutation_frame_generation) {
  return HasNewerWorldUiSubmission(state, mutation_frame_generation) &&
         state.world_ui_render_generation == state.frame_generation &&
         state.world_ui_chat_ready;
}

inline bool HasCurrentWorldMapPaint(
    const ScenarioPlayState& state,
    const std::uint64_t mutation_frame_generation) {
  return state.final_backbuffer_ready && state.world_ui_world_map_visible &&
         state.frame_generation > mutation_frame_generation &&
         state.world_ui_render_generation == state.frame_generation &&
         state.world_ui_world_map_descendant_submissions > 0u &&
         state.world_ui_world_map_background_submissions > 0u &&
         state.world_ui_world_map_detail_tile_submissions == 12u;
}

inline bool HasStableWorldMapReopen(
    const LiveE2eFrameComparison& comparison) {

  return comparison.comparable &&
         comparison.world_viewport_changed_fraction <= 0.08 &&
         comparison.mean_absolute_channel_delta <= 5.0;
}

inline bool HasCurrentCharacterPanelPaint(
    const ScenarioPlayState& state,
    const std::uint64_t mutation_frame_generation) {
  return state.final_backbuffer_ready &&
         state.world_ui_character_panel_visible &&
         state.frame_generation > mutation_frame_generation &&
         state.world_ui_render_generation == state.frame_generation &&
         state.world_ui_character_panel_descendant_submissions > 0u &&
         state.world_ui_character_panel_background_submissions > 0u &&
         state.world_ui_character_model_ready &&
         state.world_ui_character_identity_ready;
}

inline bool HasCurrentNameplatePaint(const ScenarioPlayState& state) {
  return state.final_backbuffer_ready && state.nameplate_pipeline_ready &&
         state.visible_nameplates > 0u &&
         state.nameplate_render_generation == state.frame_generation;
}

inline bool HasActionUseTransition(const bool click_invoked,
                                   const std::uint64_t before,
                                   const std::uint64_t after) noexcept {
  return click_invoked && after > before;
}

inline bool HasCameraMotionProof(const ScenarioPlayState& baseline,
                                 const ScenarioPlayState& current) {
  const float dx = current.camera_x - baseline.camera_x;
  const float dy = current.camera_y - baseline.camera_y;
  const float dz = current.camera_z - baseline.camera_z;
  return dx * dx + dy * dy + dz * dz > 0.0001F;
}

inline bool HasFinalCompositorCaptureProof(const ScenarioPlayState& state) {
  return state.final_backbuffer_ready && state.frame_generation != 0u;
}

inline bool HasLoadingCompositorCaptureProof(const ScenarioPlayState& state) {
  return state.loading_screen_visible && state.loading_screen_sole_owner &&
         state.loading_final_backbuffer_ready &&
         state.loading_render_submissions > 0u &&
         state.frame_generation != 0u;
}

enum class LiveE2eCharacterAction {
  kDeleteScenarioCharacter,
  kCreateCharacter,
  kAccountFull,
};

struct LiveE2eCharacterPlan {
  LiveE2eCharacterAction action{LiveE2eCharacterAction::kCreateCharacter};
  std::size_t one_based_index{0};
};

struct LiveE2eCreateFields {
  std::uint8_t race{0};
  std::uint8_t char_class{0};
  std::uint8_t gender{0};
  std::uint8_t skin{0};
  std::uint8_t face{0};
  std::uint8_t hair_style{0};
  std::uint8_t hair_color{0};
  std::uint8_t facial_hair{0};

  bool operator==(const LiveE2eCreateFields&) const = default;
};

struct LiveE2eCharacterCreationFixture {
  std::uint32_t race_selection_index;
  std::uint32_t class_selection_index;
  std::uint32_t sex_selection_index;
};

inline constexpr LiveE2eCharacterCreationFixture
    kLiveE2eCharacterCreationFixture{1u, 2u, 2u};

inline std::string BuildLiveE2eCharacterCreationScript() {
  const auto& fixture = kLiveE2eCharacterCreationFixture;
  return "SetSelectedRace(" + std::to_string(fixture.race_selection_index) +
         "); SetSelectedClass(" +
         std::to_string(fixture.class_selection_index) +
         "); SetSelectedSex(" +
         std::to_string(fixture.sex_selection_index) +
         "); CharacterCreate_Okay()";
}

inline std::string MakeLiveE2eScenarioCharacterName(std::uint32_t seed) {
  std::string name = "Owow";
  name.reserve(10);
  for (int index = 0; index < 6; ++index) {
    char letter = static_cast<char>('a' + (seed % 26u));
    seed = seed / 26u + 17u;

    if (name.size() >= 2 && name[name.size() - 1] == letter &&
        name[name.size() - 2] == letter) {
      letter = static_cast<char>('a' + ((letter - 'a' + 1) % 26));
    }
    name.push_back(letter);
  }
  return name;
}

inline LiveE2eCreateFields LiveE2eCreateFieldsFromRequest(
    const openwow::ui::glue::GlueGameState::CharCreateRequest& request) {
  return {
      .race = request.race,
      .char_class = request.cls,
      .gender = request.gender,
      .skin = request.skin,
      .face = request.face,
      .hair_style = request.hair_style,
      .hair_color = request.hair_color,
      .facial_hair = request.facial_hair,
  };
}

inline LiveE2eCreateFields LiveE2eCreateFieldsFromSummary(
    const openwow::net::wotlk::CharacterSummary& character) {
  return {
      .race = character.race_id,
      .char_class = character.class_id,
      .gender = character.gender,
      .skin = character.skin,
      .face = character.face,
      .hair_style = character.hair_style,
      .hair_color = character.hair_color,
      .facial_hair = character.facial_hair,
  };
}

inline bool IsLiveE2eScenarioCharacter(const std::string_view name) {
  if (name.size() != 10 || !name.starts_with("Owow")) {
    return false;
  }
  for (const char c : name.substr(4)) {
    if (c < 'a' || c > 'z') {
      return false;
    }
  }
  return true;
}

inline LiveE2eCharacterPlan PlanLiveE2eCharacterSelection(
    const std::vector<openwow::net::wotlk::CharacterSummary>& characters) {
  for (std::size_t index = 0; index < characters.size(); ++index) {
    if (IsLiveE2eScenarioCharacter(characters[index].name)) {
      return {LiveE2eCharacterAction::kDeleteScenarioCharacter, index + 1};
    }
  }

  return characters.size() >= 10
             ? LiveE2eCharacterPlan{LiveE2eCharacterAction::kAccountFull, 0}
             : LiveE2eCharacterPlan{LiveE2eCharacterAction::kCreateCharacter, 0};
}

inline bool HasStableLiveE2eCharacterList(
    const openwow::ui::glue::GlueGameState& game_state,
    const int flow_phase) {
  return game_state.current_screen == "charselect" && game_state.connected &&
         flow_phase == 0 && !game_state.wants_character_list_refresh;
}

inline bool HasForwardStartProof(const ScenarioPlayState& baseline,
                                 const ScenarioPlayState& current) {
  return current.ready && current.connected && current.mover_guid != 0 &&
         current.mover_guid == baseline.mover_guid && current.forward_active &&
         current.forward_start_packets_sent >
             baseline.forward_start_packets_sent;
}

inline bool HasForwardStopProof(const ScenarioPlayState& moving,
                                const ScenarioPlayState& current) {
  return current.ready && current.connected && current.mover_guid != 0 &&
         current.mover_guid == moving.mover_guid && !current.forward_active &&
         current.movement_stop_packets_sent >
             moving.movement_stop_packets_sent;
}

inline bool HasSustainedForwardProof(const ScenarioPlayState& baseline,
                                     const ScenarioPlayState& current) {
  const float dx = current.x - baseline.x;
  const float dy = current.y - baseline.y;
  const float dz = current.z - baseline.z;
  return current.ready && current.connected && current.forward_active &&
         current.mover_guid != 0 && current.mover_guid == baseline.mover_guid &&
         current.movement_heartbeat_packets_sent >
             baseline.movement_heartbeat_packets_sent &&
         (dx * dx + dy * dy + dz * dz) > 0.0001F;
}

}

struct ScenarioOptions {
  std::string name;
  std::filesystem::path artifacts_dir;
  std::string account;
  std::string password;

  int benchmark_frames{0};

  std::string benchmark_scene;
};

struct ScenarioContext {

  openwow::ui::glue::GlueLuaRuntime* glue_runtime{nullptr};
  openwow::ui::glue::GlueWidgetRuntime* glue_widgets{nullptr};
  const openwow::ui::glue::GlueGameState* game_state{nullptr};
  bool in_world{false};
  int flow_phase{0};
  bool show_error{false};
  std::string flow_status_text;

  std::function<void(std::string_view account, std::string_view password)>
      set_login_credentials;
  std::function<void()> clear_login_password;

  std::function<bool()> enter_offline_world;
  std::function<bool(ScenarioForwardMovementCommand)> control_forward_movement;
  std::function<bool()> skip_cinematic;
  std::function<ScenarioPlayState()> query_play_state;

  std::function<ScenarioWorldUiActionResult(ScenarioWorldUiAction)>
      exercise_world_ui;

  std::function<std::string(std::uint32_t, int, int)> dump_world_ui_json;

  std::function<bool(const std::filesystem::path&)> request_screenshot;
  int viewport_width{0};
  int viewport_height{0};
};

class ScenarioRunner {
 public:
  [[nodiscard]] const ScenarioOptions& options() const noexcept { return options_; }
  explicit ScenarioRunner(ScenarioOptions options);

  enum class Stage {
    kPreRender,
    kPostRender,
  };

  bool Tick(Stage stage, std::uint32_t now_ms, ScenarioContext* ctx);

  [[nodiscard]] bool failed() const { return failed_; }

 private:
  [[nodiscard]] std::filesystem::path CaptureFramePath(
      std::uint32_t now_ms) const;
  [[nodiscard]] std::filesystem::path WorldOracleReportPath() const;
  bool DumpUiTree(std::uint32_t now_ms, const ScenarioContext& ctx) const;
  bool CaptureFrame(std::uint32_t now_ms, const ScenarioContext& ctx) const;
  void MarkWorldMilestone(std::string name, std::uint32_t elapsed_ms);
  void RecordWorldSemanticSample(std::uint32_t elapsed_ms,
                                 const ScenarioPlayState& state);
  void FlushWorldOracleReport(bool completed, bool passed,
                              std::string failure = {});

  ScenarioOptions options_;
  std::optional<std::uint32_t> start_ms_;
  int step_{0};
  int captures_{0};
  bool pending_capture_{false};
  void ReportBenchmark();

  bool should_exit_{false};

  bool benchmark_active_{false};
  int benchmark_frames_seen_{0};
  std::vector<double> benchmark_frame_ms_;
  std::vector<double> benchmark_cpu_ms_;
  double benchmark_last_cpu_ms_{0.0};
  std::chrono::steady_clock::time_point benchmark_last_;
  bool e2e_realm_requested_{false};
  std::string e2e_character_name_;
  std::optional<detail::LiveE2eCreateFields> e2e_create_fields_;
  std::optional<std::size_t> e2e_selected_character_index_;
  std::optional<std::uint32_t> e2e_step_started_ms_;
  std::optional<std::uint32_t> e2e_world_entered_ms_;
  std::optional<ScenarioPlayState> e2e_play_baseline_;
  std::optional<ScenarioPlayState> e2e_play_moving_;
  std::optional<std::uint64_t> e2e_offline_chat_probe_generation_;
  std::optional<std::uint64_t> e2e_live_chat_probe_generation_;
  std::optional<std::uint64_t> e2e_world_map_mutation_generation_;
  std::optional<std::uint64_t> e2e_character_panel_mutation_generation_;
  bool e2e_forward_binding_held_{false};
  bool e2e_validate_next_capture_{false};
  bool e2e_world_visual_validated_{false};
  std::optional<std::string> e2e_world_visual_failure_;
  bool e2e_cinematic_started_{false};
  bool e2e_cinematic_skip_requested_{false};
  bool e2e_nameplate_probe_enabled_{false};
  bool e2e_world_interactions_proved_{false};
  bool e2e_logout_requested_{false};
  bool e2e_logout_countdown_observed_{false};
  bool failed_{false};
  std::optional<std::filesystem::path> e2e_world_frame_pending_validation_;
  std::optional<detail::LiveE2eCapturePurpose> e2e_next_capture_purpose_;
  std::optional<detail::LiveE2eCapturePurpose> e2e_pending_capture_purpose_;
  std::optional<std::uint64_t> e2e_pending_capture_generation_;
  std::optional<std::filesystem::path> e2e_world_baseline_frame_;
  std::optional<std::filesystem::path> e2e_world_map_open_frame_;
  std::optional<std::filesystem::path> e2e_post_movement_frame_;
  detail::LiveE2eWorldOracleReport e2e_world_report_;
};

}

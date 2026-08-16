#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::client::detail {

enum class LiveE2eFrameStatus : std::uint8_t {
  kPending,
  kPlayable,
  kDegenerate,
  kInvalid,
};

struct LiveE2eRegionMetrics {
  std::size_t sample_count{0};
  std::size_t quantized_color_count{0};
  std::uint8_t minimum_luma{0};
  std::uint8_t maximum_luma{0};
  double mean_luma{0.0};
  double dark_fraction{0.0};
  double yellow_fraction{0.0};
  double saturated_fraction{0.0};
};

struct LiveE2eFrameValidation {
  LiveE2eFrameStatus status{LiveE2eFrameStatus::kPending};
  std::string reason;
  std::uint32_t width{0};
  std::uint32_t height{0};

  std::size_t quantized_color_count{0};
  std::uint8_t minimum_luma{0};
  std::uint8_t maximum_luma{0};

  LiveE2eRegionMetrics full_frame;
  LiveE2eRegionMetrics world_viewport;
  LiveE2eRegionMetrics player_ui_corner;
  LiveE2eRegionMetrics minimap_ui_corner;
  LiveE2eRegionMetrics bottom_ui_band;
  std::uint64_t perceptual_hash{0};
};

struct LiveE2eFrameComparison {
  bool comparable{false};
  std::string reason;
  std::size_t sample_count{0};
  double changed_fraction{0.0};
  double world_viewport_changed_fraction{0.0};
  double minimap_corner_changed_fraction{0.0};
  double bottom_ui_changed_fraction{0.0};
  double mean_absolute_channel_delta{0.0};
  std::uint32_t first_width{0};
  std::uint32_t first_height{0};
  std::uint32_t second_width{0};
  std::uint32_t second_height{0};
};

[[nodiscard]] LiveE2eFrameValidation ValidateLiveE2eWorldFrame(
    const std::filesystem::path& path);

[[nodiscard]] LiveE2eFrameValidation ValidateLiveE2eLoadingFrame(
    const std::filesystem::path& path);

[[nodiscard]] LiveE2eFrameComparison CompareLiveE2eWorldFrames(
    const std::filesystem::path& first,
    const std::filesystem::path& second);

enum class LiveE2eCapturePurpose : std::uint8_t {
  kLoadingScreen,
  kGameplayBaseline,
  kWorldUiInteractions,
  kWorldMapOpen,
  kWorldMapReopened,
  kCharacterPanelOpen,
  kPostMovement,
  kStableGameplay,
  kFailureDiagnostic,
};

[[nodiscard]] std::string_view LiveE2eCapturePurposeName(
    LiveE2eCapturePurpose purpose) noexcept;

struct LiveE2eWorldMilestone {
  std::string name;
  std::uint32_t elapsed_ms{0};
};

struct LiveE2eWorldSemanticSample {
  std::uint32_t elapsed_ms{0};
  std::uint64_t frame_generation{0};
  bool final_backbuffer_ready{false};
  bool loading_screen_visible{false};
  bool loading_screen_sole_owner{false};
  bool loading_final_backbuffer_ready{false};
  std::uint64_t loading_render_submissions{0};
  std::uint64_t loading_self_presented_frames{0};
  std::uint64_t loading_coalesced_callbacks{0};
  bool player_render_ready{false};
  bool world_ui_ready{false};
  bool player_frame_ready{false};
  bool portrait_ready{false};
  bool health_power_ready{false};
  bool action_bar_ready{false};
  bool chat_ready{false};
  bool minimap_ready{false};
  bool world_map_ready{false};
  bool world_map_visible{false};
  bool character_panel_ready{false};
  bool character_model_ready{false};
  bool character_identity_ready{false};
  bool character_panel_visible{false};
  bool nameplates_ready{false};
  std::size_t visible_nameplates{0};
  std::size_t terrain_tiles_loaded{0};
  std::size_t object_instances{0};
  std::size_t ui_traversal_entries{0};
  std::size_t ui_render_candidates{0};
  std::uint32_t render_draw_calls{0};
  float render_cpu_time_ms{0.0F};
  float render_gpu_time_ms{0.0F};
};

struct LiveE2eWorldCaptureRecord {
  LiveE2eCapturePurpose purpose{LiveE2eCapturePurpose::kFailureDiagnostic};
  std::string filename;
  std::uint32_t elapsed_ms{0};
  std::uint64_t frame_generation{0};
  LiveE2eFrameValidation validation;
  std::optional<LiveE2eFrameComparison> comparison_to_baseline;
};

struct LiveE2eWorldOracleReport {
  bool completed{false};
  bool passed{false};
  std::string failure;
  std::vector<LiveE2eWorldMilestone> milestones;
  std::vector<LiveE2eWorldSemanticSample> semantic_samples;
  std::vector<LiveE2eWorldCaptureRecord> captures;
};

[[nodiscard]] bool WriteLiveE2eWorldOracleReport(
    const std::filesystem::path& path,
    const LiveE2eWorldOracleReport& report);

}

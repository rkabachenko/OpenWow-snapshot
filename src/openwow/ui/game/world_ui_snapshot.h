#pragma once

#include "openwow/ui/framexml/layout_resolver.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui::game {

class GameUIManager;
class MinimapIntegration;

namespace detail {

[[nodiscard]] bool RegionIntersectsViewport(
    const openwow::ui::framexml::FrameRect& rect,
    int viewport_width, int viewport_height) noexcept;

struct StatusBarDescriptorEvidence {
  float minimum{0.0F};
  float maximum{0.0F};
  float value{0.0F};
  bool styled{false};
  bool fill_submitted{false};
  bool owner_submitted{false};
};

[[nodiscard]] bool StatusBarMatchesDescriptor(
    const StatusBarDescriptorEvidence& evidence,
    std::uint32_t descriptor_value, std::uint32_t descriptor_maximum,
    bool require_positive_value) noexcept;

}

struct WorldUiRegionSnapshot {
  std::string name;
  std::string kind;
  std::string parent;
  openwow::ui::framexml::FrameRect rect{};
  bool present{false};
  bool visible{false};
  bool within_viewport{false};
};

struct WorldUiStatusBarSnapshot {
  std::string unit;
  bool disconnected{false};
  float minimum{0.0F};
  float maximum{0.0F};
  float value{0.0F};
  std::uint32_t descriptor_value{0};
  std::uint32_t descriptor_maximum{0};
  bool styled{false};
  bool fill_submitted{false};
  bool owner_submitted{false};
  float red{0.0F};
  float green{0.0F};
  float blue{0.0F};
};

struct WorldUiPlayerStatusBarsSnapshot {
  std::string frame_unit;
  WorldUiStatusBarSnapshot health;
  WorldUiStatusBarSnapshot power;
};

struct WorldUiSnapshot {
  float root_scale{1.0F};
  bool frame_xml_loaded{false};
  bool required_regions_present{false};
  bool stock_anchors_valid{false};
  bool named_text_contained{false};
  bool player_portrait_ready{false};
  bool player_status_bars_ready{false};
  WorldUiPlayerStatusBarsSnapshot player_status_bars;
  bool action_icon_ready{false};
  bool chat_content_ready{false};
  std::size_t chat_message_count{0};
  bool chat_fading{false};
  float chat_time_visible{0.0F};
  float chat_fade_duration{0.0F};
  double chat_latest_inserted_at_seconds{0.0};
  bool minimap_surface_ready{false};
  std::size_t tracked_frame_count{0};
  std::size_t render_candidate_count{0};
  std::size_t minimap_terrain_submission_count{0};
  std::vector<WorldUiRegionSnapshot> regions;
  std::vector<std::string> failures;

  [[nodiscard]] bool ready() const {
    return frame_xml_loaded && required_regions_present &&
           stock_anchors_valid && named_text_contained &&
           player_portrait_ready && player_status_bars_ready &&
           action_icon_ready && chat_content_ready &&
           minimap_surface_ready && failures.empty();
  }
};

[[nodiscard]] WorldUiSnapshot BuildWorldUiSnapshot(
    GameUIManager& manager, const MinimapIntegration& minimap,
    int viewport_width, int viewport_height);

[[nodiscard]] std::string SerializeWorldUiSnapshotJson(
    const WorldUiSnapshot& snapshot, std::uint32_t now_ms,
    int viewport_width, int viewport_height);

}

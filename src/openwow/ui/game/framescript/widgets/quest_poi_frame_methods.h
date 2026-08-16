#pragma once

#include "openwow/game/quest_poi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct lua_State;

namespace openwow::game {
class WorldSession;
}

namespace openwow::ui {
class WorldMapSystem;
}

namespace openwow::ui::framexml {
struct UiFrame;
}

namespace openwow::ui::game::frame_api {

inline constexpr std::size_t kQuestPoiFrameSlotCount = 4;

struct QuestPoiRenderedEntry {
  openwow::game::QuestPOIRenderData render_data;
  std::array<std::int32_t, kQuestPoiFrameSlotCount> tooltip_indices{};
  std::size_t tooltip_count = 0;
};

struct QuestPoiHoverResult {
  std::uint32_t quest_id = 0;
  std::array<std::int32_t, kQuestPoiFrameSlotCount> tooltip_indices{};
  std::size_t tooltip_count = 0;
};

struct QuestPoiFrameRenderConfig {
  bool smoothing_enabled = true;
  bool merging_enabled = true;
  float merge_threshold = 0.25f;
  int spline_point_count = 20;
  float border_scalar = 1.0f;
};

struct QuestPoiFrameVisualState {
  std::array<std::uint32_t, kQuestPoiFrameSlotCount> registered_quest_ids{};
  QuestPoiFrameRenderConfig render_config;
  std::string fill_texture_path;
  std::string border_texture_path;
  std::uint8_t fill_alpha = 255;
  std::uint8_t border_alpha = 255;
};

void InitializeQuestPOIFrameDefaults(lua_State* lua, int frame_index);
void ApplyQuestPOIFrameSpecificMethods(lua_State* lua);
QuestPoiFrameVisualState ReadQuestPoiFrameVisualState(
    lua_State* lua, int frame_index,
    const openwow::ui::framexml::UiFrame& frame);
std::array<std::vector<QuestPoiRenderedEntry>, kQuestPoiFrameSlotCount>
BuildQuestPoiFrameRenderedSlots(const QuestPoiFrameVisualState& visual_state,
                                const openwow::ui::WorldMapSystem& world_map,
                                const openwow::game::WorldSession& session);
QuestPoiHoverResult ResolveQuestPoiHoverForRenderedSlots(
    const std::array<std::vector<QuestPoiRenderedEntry>,
                     kQuestPoiFrameSlotCount>& rendered_slots,
    const std::array<std::uint32_t, kQuestPoiFrameSlotCount>& registered_quests,
    float mouse_x, float mouse_y);

}

#include "openwow/ui/game/framescript/widgets/quest_poi_frame_methods.h"

#include "openwow/ui/framexml/ui_frame.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/world_map_system.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#undef lua_pushcfunction
#define lua_pushcfunction(L, ...) lua_pushcclosure(L, (__VA_ARGS__), 0)

namespace openwow::ui::game::frame_api {

using detail::TruncateLuaNumberToSseI32;
using detail::FindInterleavedQuestIndexById;
using detail::FrameScript_PushNumberFromInt;

namespace {
int ValidateQuestPOIFrameSelf(lua_State* lua) {
  return ValidateTypedFramescriptSelf(lua, "QuestPOIFrame");
}

constexpr const char *kQuestPoiFillAlphaField = "__ow_poi_fill_alpha";
constexpr const char *kQuestPoiBorderAlphaField = "__ow_poi_border_alpha";
constexpr const char *kQuestPoiFillTextureField = "__ow_poi_fill_tex";
constexpr const char *kQuestPoiBorderTextureField = "__ow_poi_border_tex";
constexpr const char *kQuestPoiFillTextureOverrideField = "__ow_poi_fill_tex_override";
constexpr const char *kQuestPoiBorderTextureOverrideField = "__ow_poi_border_tex_override";
constexpr const char *kQuestPoiBorderScalarField = "__ow_poi_border_scalar";
constexpr const char *kQuestPoiEnableSmoothingField = "__ow_poi_enable_smoothing";
constexpr const char *kQuestPoiEnableMergingField = "__ow_poi_enable_merging";
constexpr const char *kQuestPoiMergeThresholdField = "__ow_poi_merge_threshold";
constexpr const char *kQuestPoiSplinePointCountField = "__ow_poi_num_spline_points";
constexpr const char *kQuestPoiRegisteredQuestIdsField = "__ow_poi_registered_quest_ids";
constexpr const char *kQuestPoiTooltipCountField = "__ow_poi_tooltip_count";
constexpr const char *kQuestPoiTooltipIndicesField = "__ow_poi_tooltip_indices";
}

float ClampQuestPoiMergeThreshold(const double value) {
  if (!(value >= 0.1)) {
    return 0.1f;
  }
  if (value > 0.5) {
    return 0.5f;
  }
  return static_cast<float>(value);
}

std::uint8_t ClampQuestPoiAlphaByte(const double value) {
  double clamped_value = value;
  if (!(clamped_value >= 0.0)) {
    clamped_value = 0.0;
  } else if (clamped_value > 255.0) {
    clamped_value = 255.0;
  }
  return static_cast<std::uint8_t>(std::nearbyint(clamped_value));
}

float ClampQuestPoiBorderScalar(const double value) {
  if (!(value <= 10.0)) {
    return 10.0f;
  }
  if (value < 0.0) {
    return 0.0f;
  }
  return static_cast<float>(value);
}

int ClampQuestPoiSplinePointCount(const double value) {
  int spline_points = static_cast<int>(value);
  if (spline_points < 8) {
    return 8;
  }
  if (spline_points > 30) {
    return 30;
  }
  return spline_points;
}

void InitializeQuestPOIFrameDefaults(lua_State *L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);

  auto ensure_number = [&](const char *field_name, const lua_Number value) {
    lua_getfield(L, frame_index, field_name);
    const bool needs_default = lua_isnil(L, -1) != 0;
    lua_pop(L, 1);
    if (needs_default) {
      lua_pushnumber(L, value);
      lua_setfield(L, frame_index, field_name);
    }
  };

  auto ensure_boolean = [&](const char *field_name, const bool value) {
    lua_getfield(L, frame_index, field_name);
    const bool needs_default = lua_isnil(L, -1) != 0;
    lua_pop(L, 1);
    if (needs_default) {
      lua_pushboolean(L, value ? 1 : 0);
      lua_setfield(L, frame_index, field_name);
    }
  };

  ensure_number(kQuestPoiFillAlphaField, 255.0);
  ensure_number(kQuestPoiBorderAlphaField, 255.0);
  ensure_number(kQuestPoiBorderScalarField, 1.0);
  ensure_boolean(kQuestPoiEnableSmoothingField, true);
  ensure_boolean(kQuestPoiEnableMergingField, true);
  ensure_number(kQuestPoiMergeThresholdField, 0.25);
  ensure_number(kQuestPoiSplinePointCountField, 20.0);
  ensure_number(kQuestPoiTooltipCountField, 0.0);

  auto ensure_number_array = [&](const char *field_name) {
    lua_getfield(L, frame_index, field_name);
    const bool needs_default = lua_istable(L, -1) == 0;
    lua_pop(L, 1);
    if (!needs_default) {
      return;
    }

    lua_createtable(L, static_cast<int>(kQuestPoiFrameSlotCount), 0);
    for (std::size_t index = 0; index < kQuestPoiFrameSlotCount; ++index) {
      lua_pushnumber(L, 0.0);
      lua_seti(L, -2, static_cast<lua_Integer>(index + 1));
    }
    lua_setfield(L, frame_index, field_name);
  };

  ensure_number_array(kQuestPoiRegisteredQuestIdsField);
  ensure_number_array(kQuestPoiTooltipIndicesField);
  lua_pushboolean(L, 1);
  lua_setfield(L, frame_index, "__ow_protected");
  openwow::ui::game::detail::BumpLuaLayoutProtectionGeneration(L);
}

void SetQuestPoiAlphaField(lua_State *L, const char *field_name) {
  const int self_idx = ValidateQuestPOIFrameSelf(L);
  lua_pushinteger(L, static_cast<lua_Integer>(ClampQuestPoiAlphaByte(lua_tonumber(L, 2))));
  lua_setfield(L, self_idx, field_name);
}

void SetQuestPoiTextureField(lua_State *L, const char *field_name,
                             const char *override_field_name) {
  const int self_idx = ValidateQuestPOIFrameSelf(L);
  const char *texture_path = lua_tostring(L, 2);
  if (texture_path != nullptr) {
    lua_pushstring(L, texture_path);
  } else {
    lua_pushnil(L);
  }
  lua_setfield(L, self_idx, field_name);
  lua_pushboolean(L, 1);
  lua_setfield(L, self_idx, override_field_name);
}

std::array<std::uint32_t, kQuestPoiFrameSlotCount> ReadQuestPoiRegisteredQuestIds(lua_State *L,
                                                                                  int frame_index) {
  std::array<std::uint32_t, kQuestPoiFrameSlotCount> quest_ids{};
  frame_index = lua_absindex(L, frame_index);
  lua_getfield(L, frame_index, kQuestPoiRegisteredQuestIdsField);
  if (lua_istable(L, -1) != 0) {
    for (std::size_t slot = 0; slot < quest_ids.size(); ++slot) {
      lua_geti(L, -1, static_cast<lua_Integer>(slot + 1));
      if (lua_isnumber(L, -1) != 0) {
        const auto quest_id = TruncateLuaNumberToSseI32(lua_tonumber(L, -1));
        if (quest_id > 0) {
          quest_ids[slot] = static_cast<std::uint32_t>(quest_id);
        }
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  return quest_ids;
}

void WriteQuestPoiRegisteredQuestIds(
    lua_State *L, int frame_index,
    const std::array<std::uint32_t, kQuestPoiFrameSlotCount> &quest_ids) {
  frame_index = lua_absindex(L, frame_index);
  lua_createtable(L, static_cast<int>(quest_ids.size()), 0);
  for (std::size_t slot = 0; slot < quest_ids.size(); ++slot) {
    lua_pushnumber(L, static_cast<lua_Number>(quest_ids[slot]));
    lua_seti(L, -2, static_cast<lua_Integer>(slot + 1));
  }
  lua_setfield(L, frame_index, kQuestPoiRegisteredQuestIdsField);
}

void WriteQuestPoiHoverState(lua_State *L, int frame_index, const QuestPoiHoverResult &result) {
  frame_index = lua_absindex(L, frame_index);
  lua_pushnumber(L, static_cast<lua_Number>(result.tooltip_count));
  lua_setfield(L, frame_index, kQuestPoiTooltipCountField);

  lua_createtable(L, static_cast<int>(result.tooltip_indices.size()), 0);
  for (std::size_t index = 0; index < result.tooltip_indices.size(); ++index) {
    lua_pushnumber(L, static_cast<lua_Number>(result.tooltip_indices[index]));
    lua_seti(L, -2, static_cast<lua_Integer>(index + 1));
  }
  lua_setfield(L, frame_index, kQuestPoiTooltipIndicesField);
}

QuestPoiHoverResult ReadQuestPoiHoverState(lua_State *L, int frame_index) {
  QuestPoiHoverResult result;
  frame_index = lua_absindex(L, frame_index);

  lua_getfield(L, frame_index, kQuestPoiTooltipCountField);
  if (lua_isnumber(L, -1) != 0) {
    const auto count = TruncateLuaNumberToSseI32(lua_tonumber(L, -1));
    if (count > 0) {
      result.tooltip_count = static_cast<std::size_t>(
          std::min<int>(count, static_cast<int>(result.tooltip_indices.size())));
    }
  }
  lua_pop(L, 1);

  lua_getfield(L, frame_index, kQuestPoiTooltipIndicesField);
  if (lua_istable(L, -1) != 0) {
    for (std::size_t index = 0; index < result.tooltip_indices.size(); ++index) {
      lua_geti(L, -1, static_cast<lua_Integer>(index + 1));
      if (lua_isnumber(L, -1) != 0) {
        result.tooltip_indices[index] = TruncateLuaNumberToSseI32(lua_tonumber(L, -1));
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);

  return result;
}

QuestPoiHoverResult MakeEmptyQuestPoiHoverResult() {
  return {};
}

QuestPoiFrameRenderConfig ReadQuestPoiFrameRenderConfig(lua_State *L, int frame_index) {
  QuestPoiFrameRenderConfig config;
  frame_index = lua_absindex(L, frame_index);

  lua_getfield(L, frame_index, kQuestPoiEnableSmoothingField);
  config.smoothing_enabled = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);

  lua_getfield(L, frame_index, kQuestPoiEnableMergingField);
  config.merging_enabled = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);

  lua_getfield(L, frame_index, kQuestPoiMergeThresholdField);
  config.merge_threshold = ClampQuestPoiMergeThreshold(lua_tonumber(L, -1));
  lua_pop(L, 1);

  lua_getfield(L, frame_index, kQuestPoiSplinePointCountField);
  config.spline_point_count = ClampQuestPoiSplinePointCount(lua_tonumber(L, -1));
  lua_pop(L, 1);

  lua_getfield(L, frame_index, kQuestPoiBorderScalarField);
  config.border_scalar = ClampQuestPoiBorderScalar(lua_tonumber(L, -1));
  lua_pop(L, 1);

  return config;
}

std::uint8_t ReadQuestPoiFrameAlphaField(lua_State *L, int frame_index, const char *field_name) {
  frame_index = lua_absindex(L, frame_index);
  lua_getfield(L, frame_index, field_name);
  const auto alpha = ClampQuestPoiAlphaByte(lua_tonumber(L, -1));
  lua_pop(L, 1);
  return alpha;
}

std::string ReadQuestPoiFrameTextureField(lua_State *L, int frame_index, const char *field_name,
                                          const char *override_field_name,
                                          const std::string &fallback) {
  frame_index = lua_absindex(L, frame_index);
  lua_getfield(L, frame_index, override_field_name);
  const bool has_override = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (!has_override) {
    return fallback;
  }

  lua_getfield(L, frame_index, field_name);
  std::string texture_path;
  if (lua_isstring(L, -1) != 0) {
    texture_path = lua_tostring(L, -1);
  }
  lua_pop(L, 1);
  return texture_path;
}

QuestPoiFrameVisualState
BuildQuestPoiFrameVisualStateInternal(lua_State *L, int frame_index,
                                      const openwow::ui::framexml::UiFrame &frame) {
  QuestPoiFrameVisualState visual_state;
  visual_state.registered_quest_ids = ReadQuestPoiRegisteredQuestIds(L, frame_index);
  visual_state.render_config = ReadQuestPoiFrameRenderConfig(L, frame_index);
  visual_state.fill_texture_path = ReadQuestPoiFrameTextureField(
      L, frame_index, kQuestPoiFillTextureField, kQuestPoiFillTextureOverrideField,
      frame.quest_poi_fill_texture);
  visual_state.border_texture_path = ReadQuestPoiFrameTextureField(
      L, frame_index, kQuestPoiBorderTextureField, kQuestPoiBorderTextureOverrideField,
      frame.quest_poi_border_texture);
  visual_state.fill_alpha = ReadQuestPoiFrameAlphaField(L, frame_index, kQuestPoiFillAlphaField);
  visual_state.border_alpha =
      ReadQuestPoiFrameAlphaField(L, frame_index, kQuestPoiBorderAlphaField);
  return visual_state;
}

openwow::game::QuestPOIBoundingBox
ComputeQuestPoiBoundingBox(const std::vector<openwow::game::QuestPOIPoint> &points) {
  openwow::game::QuestPOIBoundingBox bounds;
  if (points.empty()) {
    return bounds;
  }

  bounds.minX = bounds.maxX = points.front().x;
  bounds.minY = bounds.maxY = points.front().y;
  for (std::size_t index = 1; index < points.size(); ++index) {
    bounds.minX = std::min(bounds.minX, points[index].x);
    bounds.maxX = std::max(bounds.maxX, points[index].x);
    bounds.minY = std::min(bounds.minY, points[index].y);
    bounds.maxY = std::max(bounds.maxY, points[index].y);
  }
  return bounds;
}

bool QuestPoiBoundingBoxContains(const openwow::game::QuestPOIBoundingBox &bounds, float x,
                                 float y) {
  return x >= bounds.minX && x <= bounds.maxX && y >= bounds.minY && y <= bounds.maxY;
}

float QuestPoiBoundingBoxArea(const openwow::game::QuestPOIBoundingBox &bounds) {
  return std::max(bounds.maxX - bounds.minX, 0.0f) * std::max(bounds.maxY - bounds.minY, 0.0f);
}

bool QuestPoiPolygonsOverlap(const openwow::game::QuestPOIBoundingBox &lhs,
                             const openwow::game::QuestPOIBoundingBox &rhs) {
  return lhs.maxX > rhs.minX && rhs.maxX > lhs.minX && lhs.maxY > rhs.minY && rhs.maxY > lhs.minY;
}

bool QuestPoiContainsPoint(const std::vector<openwow::game::QuestPOIPoint> &points, float x,
                           float y) {
  if (points.size() < 3) {
    return false;
  }

  bool inside = false;
  for (std::size_t index = 0, previous = points.size() - 1; index < points.size();
       previous = index++) {
    const auto &current = points[index];
    const auto &prior = points[previous];
    if (((current.y > y) != (prior.y > y)) &&
        (x < (prior.x - current.x) * (y - current.y) / (prior.y - current.y) + current.x)) {
      inside = !inside;
    }
  }
  return inside;
}

QuestPoiRenderedEntry BuildQuestPoiRenderedEntry(const openwow::game::QuestPOIEntry &poi,
                                                 const openwow::ui::WorldMapSystem &world_map,
                                                 const QuestPoiFrameRenderConfig &config,
                                                 int selected_dungeon_map_id) {
  QuestPoiRenderedEntry rendered_entry;

  const auto boundary_points =
      config.smoothing_enabled
          ? openwow::game::SampleQuestPOISpline(poi.points, config.spline_point_count)
          : poi.points;
  if (boundary_points.size() < 3) {
    return rendered_entry;
  }

  std::vector<openwow::game::QuestPOIPoint> transformed_points;
  transformed_points.reserve(boundary_points.size());
  for (const auto &point : boundary_points) {
    const auto map_coord = const_cast<openwow::ui::WorldMapSystem&>(world_map).WorldToMapForCurrentSelection(poi.mapId, point.x, point.y,
                                                                   0.0f, selected_dungeon_map_id);
    if (!map_coord.valid) {
      return rendered_entry;
    }
    transformed_points.push_back({map_coord.x, map_coord.y});
  }

  rendered_entry.render_data =
      openwow::game::BuildQuestPOIRenderDataFromMapPoints(transformed_points, config.border_scalar);
  if (!rendered_entry.render_data.active) {
    return rendered_entry;
  }

  rendered_entry.tooltip_indices[0] = poi.objectiveIndex;
  rendered_entry.tooltip_count = 1;
  return rendered_entry;
}

void MergeQuestPoiRenderedEntries(std::vector<QuestPoiRenderedEntry> &entries,
                                  float merge_threshold) {
  for (std::size_t index = 0; index < entries.size(); ++index) {
    auto &lhs = entries[index];
    if (!lhs.render_data.active) {
      continue;
    }

    for (std::size_t other_index = index + 1; other_index < entries.size(); ++other_index) {
      auto &rhs = entries[other_index];
      if (!rhs.render_data.active ||
          !QuestPoiPolygonsOverlap(lhs.render_data.fillBounds, rhs.render_data.fillBounds)) {
        continue;
      }

      QuestPoiRenderedEntry *smaller = &lhs;
      QuestPoiRenderedEntry *larger = &rhs;
      if (QuestPoiBoundingBoxArea(rhs.render_data.fillBounds) <=
          QuestPoiBoundingBoxArea(lhs.render_data.fillBounds)) {
        smaller = &rhs;
        larger = &lhs;
      }

      std::size_t enclosed_points = 0;
      for (const auto &point : smaller->render_data.fillPoints) {
        if (QuestPoiContainsPoint(larger->render_data.fillPoints, point.x, point.y)) {
          ++enclosed_points;
        }
      }
      if (smaller->render_data.fillPoints.empty()) {
        continue;
      }

      const float enclosed_ratio = static_cast<float>(enclosed_points) /
                                   static_cast<float>(smaller->render_data.fillPoints.size());
      if (enclosed_ratio <= merge_threshold) {
        continue;
      }

      smaller->render_data.active = false;
      for (std::size_t tooltip_index = 0; tooltip_index < smaller->tooltip_count; ++tooltip_index) {
        const auto candidate = smaller->tooltip_indices[tooltip_index];
        bool already_present = false;
        for (std::size_t existing_index = 0; existing_index < larger->tooltip_count;
             ++existing_index) {
          if (larger->tooltip_indices[existing_index] == candidate) {
            already_present = true;
            break;
          }
        }

        if (!already_present && larger->tooltip_count < larger->tooltip_indices.size()) {
          larger->tooltip_indices[larger->tooltip_count++] = candidate;
        }
      }
    }
  }
}

std::vector<QuestPoiRenderedEntry> RebuildQuestPoiFrameSlotRenderEntries(
    const std::uint32_t quest_id, const QuestPoiFrameRenderConfig &render_config,
    const openwow::ui::WorldMapSystem &world_map,
    const openwow::ui::WorldMapSystem::QuestPoiSelectionContext &selection,
    const openwow::game::WorldSession &session) {
  if (quest_id == 0) {
    return {};
  }

  const auto objective_mask =
      openwow::ui::game::detail::BuildQuestPoiIncompleteObjectiveMask(session, quest_id);

  std::vector<QuestPoiRenderedEntry> rendered_entries;
  for (const auto &poi : openwow::game::QuestPOIData::Get().GetPOIsForQuest(quest_id)) {
    if (!openwow::ui::game::detail::QuestPoiPassesObjectiveMask(poi, objective_mask)) {
      continue;
    }
    if (!openwow::ui::game::detail::IsQuestPoiVisibleOnCurrentSelection(world_map, selection,
                                                                        poi)) {
      continue;
    }

    auto rendered_entry = BuildQuestPoiRenderedEntry(poi, world_map, render_config,
                                                     selection.selected_dungeon_map_id);
    if (rendered_entry.render_data.active) {
      rendered_entries.push_back(std::move(rendered_entry));
    }
  }

  if (render_config.merging_enabled && objective_mask != -1) {
    MergeQuestPoiRenderedEntries(rendered_entries, render_config.merge_threshold);
  }

  return rendered_entries;
}

std::array<std::vector<QuestPoiRenderedEntry>, kQuestPoiFrameSlotCount>
BuildQuestPoiFrameRenderedSlotsInternal(const QuestPoiFrameVisualState &visual_state,
                                        const openwow::ui::WorldMapSystem &world_map,
                                        const openwow::game::WorldSession &session) {
  std::array<std::vector<QuestPoiRenderedEntry>, kQuestPoiFrameSlotCount> rendered_slots;
  const auto selection = world_map.GetQuestPoiSelectionContext();
  if (selection.displayed_world_map_area_id < 0) {
    return rendered_slots;
  }

  for (std::size_t slot = 0; slot < visual_state.registered_quest_ids.size(); ++slot) {
    const auto quest_id = visual_state.registered_quest_ids[slot];
    if (quest_id == 0) {
      continue;
    }

    rendered_slots[slot] = RebuildQuestPoiFrameSlotRenderEntries(
        quest_id, visual_state.render_config, world_map, selection, session);
  }

  return rendered_slots;
}

QuestPoiHoverResult ResolveQuestPoiHoverForRenderedSlotsInternal(
    const std::array<std::vector<QuestPoiRenderedEntry>, kQuestPoiFrameSlotCount> &rendered_slots,
    const std::array<std::uint32_t, kQuestPoiFrameSlotCount> &registered_quests, float mouse_x,
    float mouse_y) {
  for (std::size_t slot = 0; slot < rendered_slots.size(); ++slot) {
    const auto quest_id = registered_quests[slot];
    if (quest_id == 0) {
      continue;
    }

    for (const auto &rendered_entry : rendered_slots[slot]) {
      if (!rendered_entry.render_data.active ||
          !QuestPoiBoundingBoxContains(rendered_entry.render_data.fillBounds, mouse_x, mouse_y)) {
        continue;
      }
      if (!QuestPoiContainsPoint(rendered_entry.render_data.fillPoints, mouse_x, mouse_y)) {
        continue;
      }

      QuestPoiHoverResult result;
      result.quest_id = quest_id;
      result.tooltip_indices = rendered_entry.tooltip_indices;
      result.tooltip_count = rendered_entry.tooltip_count;
      return result;
    }
  }

  return MakeEmptyQuestPoiHoverResult();
}

int QuestPoiFrameDrawQuestBlob(lua_State *L) {
  const int self_idx = ValidateQuestPOIFrameSelf(L);
  auto quest_ids = ReadQuestPoiRegisteredQuestIds(L, self_idx);
  const auto quest_id = TruncateLuaNumberToSseI32(lua_tonumber(L, 2));
  const bool enabled = lua_toboolean(L, 3) != 0;

  if (enabled) {
    const auto *session = openwow::ui::game::detail::GetWorldSession(L);
    const auto *active_player = session != nullptr
                                    ? session->objects().GetActivePlayer()
                                    : nullptr;
    if (active_player == nullptr || quest_id <= 0 ||
        !openwow::game::QuestPOIData::Get().HasQuestQueryResult(
            static_cast<std::uint32_t>(quest_id))) {
      return 0;
    }

    for (const auto existing_quest_id : quest_ids) {
      if (existing_quest_id == static_cast<std::uint32_t>(quest_id)) {
        return 0;
      }
    }

    for (auto &slot_quest_id : quest_ids) {
      if (slot_quest_id == 0) {
        slot_quest_id = static_cast<std::uint32_t>(quest_id);
        break;
      }
    }
  } else {
    for (auto &slot_quest_id : quest_ids) {
      if (slot_quest_id == static_cast<std::uint32_t>(quest_id)) {
        slot_quest_id = 0;
        break;
      }
    }
  }

  WriteQuestPoiRegisteredQuestIds(L, self_idx, quest_ids);
  return 0;
}

int QuestPoiFrameUpdateQuestPOI(lua_State *L) {
  ValidateQuestPOIFrameSelf(L);
  return 0;
}

int QuestPoiFrameUpdateMouseOverTooltip(lua_State *L) {
  const int self_idx = ValidateQuestPOIFrameSelf(L);
  WriteQuestPoiHoverState(L, self_idx, MakeEmptyQuestPoiHoverResult());

  const float mouse_x = static_cast<float>(lua_tonumber(L, 2));
  const float mouse_y = static_cast<float>(lua_tonumber(L, 3));
  if (mouse_x < 0.0f || mouse_y < 0.0f || mouse_x > 1.0f || mouse_y > 1.0f) {
    return 0;
  }

  auto *session = openwow::ui::game::detail::GetWorldSession(L);
  auto* const game_ui = openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(L);
  if (session == nullptr || game_ui == nullptr) {
    return 0;
  }

  openwow::ui::framexml::UiFrame visual_frame;
  const auto visual_state = BuildQuestPoiFrameVisualStateInternal(L, self_idx, visual_frame);
  const auto rendered_slots = BuildQuestPoiFrameRenderedSlotsInternal(
      visual_state, game_ui->world_map(), *session);
  const auto hover_result = ResolveQuestPoiHoverForRenderedSlotsInternal(
      rendered_slots, visual_state.registered_quest_ids, mouse_x, mouse_y);
  if (hover_result.quest_id == 0 || hover_result.tooltip_count == 0) {
    return 0;
  }

  WriteQuestPoiHoverState(L, self_idx, hover_result);
  const int quest_log_index =
      FindInterleavedQuestIndexById(*session, hover_result.quest_id);
  lua_pushnumber(L, static_cast<lua_Number>(quest_log_index > 0 ? quest_log_index - 1 : -1));
  lua_pushnumber(L, static_cast<lua_Number>(hover_result.tooltip_count));
  return 2;
}

int QuestPoiFrameGetNumTooltips(lua_State *L) {
  const int self_idx = ValidateQuestPOIFrameSelf(L);
  FrameScript_PushNumberFromInt(
      L, static_cast<int>(ReadQuestPoiHoverState(L, self_idx).tooltip_count));
  return 1;
}

int QuestPoiFrameGetTooltipIndex(lua_State *L) {
  const int self_idx = ValidateQuestPOIFrameSelf(L);
  const auto tooltip_slot = TruncateLuaNumberToSseI32(lua_tonumber(L, 2)) - 1;
  if (tooltip_slot < 0 || tooltip_slot >= static_cast<int>(kQuestPoiFrameSlotCount)) {
    FrameScript_PushNumberFromInt(L, 0);
    return 1;
  }

  const auto hover_state = ReadQuestPoiHoverState(L, self_idx);
  FrameScript_PushNumberFromInt(L, hover_state.tooltip_indices[tooltip_slot]);
  return 1;
}

QuestPoiFrameVisualState ReadQuestPoiFrameVisualState(lua_State *L, int frame_idx,
                                                      const openwow::ui::framexml::UiFrame &frame) {
  return BuildQuestPoiFrameVisualStateInternal(L, frame_idx, frame);
}

std::array<std::vector<QuestPoiRenderedEntry>, kQuestPoiFrameSlotCount>
BuildQuestPoiFrameRenderedSlots(const QuestPoiFrameVisualState &visual_state,
                                const openwow::ui::WorldMapSystem &world_map,
                                const openwow::game::WorldSession &session) {
  return BuildQuestPoiFrameRenderedSlotsInternal(visual_state, world_map, session);
}

QuestPoiHoverResult ResolveQuestPoiHoverForRenderedSlots(
    const std::array<std::vector<QuestPoiRenderedEntry>, kQuestPoiFrameSlotCount> &rendered_slots,
    const std::array<std::uint32_t, kQuestPoiFrameSlotCount> &registered_quests, float mouse_x,
    float mouse_y) {
  return ResolveQuestPoiHoverForRenderedSlotsInternal(rendered_slots, registered_quests, mouse_x,
                                                      mouse_y);
}

void ApplyQuestPOIFrameSpecificMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    SetQuestPoiAlphaField(Ls, kQuestPoiFillAlphaField);
    return 0;
  });
  lua_setfield(L, f, "SetFillAlpha");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    SetQuestPoiAlphaField(Ls, kQuestPoiBorderAlphaField);
    return 0;
  });
  lua_setfield(L, f, "SetBorderAlpha");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    SetQuestPoiTextureField(Ls, kQuestPoiFillTextureField, kQuestPoiFillTextureOverrideField);
    return 0;
  });
  lua_setfield(L, f, "SetFillTexture");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    SetQuestPoiTextureField(Ls, kQuestPoiBorderTextureField, kQuestPoiBorderTextureOverrideField);
    return 0;
  });
  lua_setfield(L, f, "SetBorderTexture");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "QuestPOIFrame");
    lua_pushnumber(Ls, ClampQuestPoiBorderScalar(lua_tonumber(Ls, 2)));
    lua_setfield(Ls, self_idx, kQuestPoiBorderScalarField);
    return 0;
  });
  lua_setfield(L, f, "SetBorderScalar");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateQuestPOIFrameSelf(Ls);
    lua_pushboolean(Ls, lua_toboolean(Ls, 2));
    lua_setfield(Ls, self_idx, kQuestPoiEnableSmoothingField);
    return 0;
  });
  lua_setfield(L, f, "EnableSmoothing");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateQuestPOIFrameSelf(Ls);
    lua_pushboolean(Ls, lua_toboolean(Ls, 2));
    lua_setfield(Ls, self_idx, kQuestPoiEnableMergingField);
    return 0;
  });
  lua_setfield(L, f, "EnableMerging");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateQuestPOIFrameSelf(Ls);
    lua_pushnumber(Ls, ClampQuestPoiMergeThreshold(lua_tonumber(Ls, 2)));
    lua_setfield(Ls, self_idx, kQuestPoiMergeThresholdField);
    return 0;
  });
  lua_setfield(L, f, "SetMergeThreshold");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateQuestPOIFrameSelf(Ls);
    lua_pushinteger(Ls,
                    static_cast<lua_Integer>(ClampQuestPoiSplinePointCount(lua_tonumber(Ls, 2))));
    lua_setfield(Ls, self_idx, kQuestPoiSplinePointCountField);
    return 0;
  });
  lua_setfield(L, f, "SetNumSplinePoints");

  lua_pushcfunction(L, QuestPoiFrameDrawQuestBlob);
  lua_setfield(L, f, "DrawQuestBlob");

  lua_pushcfunction(L, QuestPoiFrameUpdateQuestPOI);
  lua_setfield(L, f, "UpdateQuestPOI");

  lua_pushcfunction(L, QuestPoiFrameUpdateMouseOverTooltip);
  lua_setfield(L, f, "UpdateMouseOverTooltip");

  lua_pushcfunction(L, QuestPoiFrameGetTooltipIndex);
  lua_setfield(L, f, "GetTooltipIndex");

  lua_pushcfunction(L, QuestPoiFrameGetNumTooltips);
  lua_setfield(L, f, "GetNumTooltips");
}

}

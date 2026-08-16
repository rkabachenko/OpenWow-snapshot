#include "openwow/ui/game/runtime/retained_layout.h"

#include "openwow/ui/animation/animation_coordinate_space.h"
#include "openwow/ui/framexml/layout_anchor_resolution.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/widgets/edit_box_state.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/runtime/layout_persistence.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/lua_interned_field_key.h"
#include "openwow/ui/game/runtime/texture_render_state_source.h"
#include "openwow/ui/model_natural_size.h"
#include "openwow/ui/texture_natural_size.h"
#include "openwow/ui/transparent_string_map.h"
#include "openwow/ui/lua_table_field.h"
#include "openwow/ui/font_string_layout.h"
#include "openwow/ui/rect_utils.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/foundation/text/ascii.h"

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openwow::ui::game::runtime {
namespace {

constexpr float kUiVirtualHeight = 768.0F;
constexpr float kScaleWriteEpsilon = 0.00000023841858F;

namespace layout_field {

constexpr const char* const kNames[] = {
    "__ow_parent_name",
    "__ow_parent",
    "__ow_type",
    "__ow_width",
    "__ow_height",
    "__ow_layout_offset_x",
    "__ow_layout_offset_y",
    "__ow_scale",
    "__ow_depth",
    "__ow_frame_level",
    "__ow_id",
    "__ow_frame_strata",
    "__ow_toplevel",
    "__ow_user_placed",
    "__ow_dontsavepos",
    "__ow_draw_layer_enabled",
    "__ow_visible",
    "__ow_enableMouse",
    "__ow_enableKeyboard",
    "__ow_clamped",
    "__ow_clamp_l",
    "__ow_clamp_r",
    "__ow_clamp_t",
    "__ow_clamp_b",
    "__ow_anchors",
    "__ow_setAllPoints",
    "__ow_hit_l",
    "__ow_hit_r",
    "__ow_hit_t",
    "__ow_hit_b",
    "__ow_font_width_from_layout",
    "__ow_font_height_from_layout",
    "__ow_text",
    "__ow_frame_key",
    "__ow_name",
    "__ow_flags",
    "__ow_model_path",
    "point",
    "relativePoint",
    "relativeTo",
    "x",
    "y",
};

consteval int Ordinal(const std::string_view name) {
  return InternedLuaFieldKeyOrdinal(kNames, name);
}

using Keys = InternedLuaFieldKeys;

[[nodiscard]] Keys PushKeys(lua_State* lua) {
  return PushInternedLuaFieldKeys(lua, kNames);
}

template <int Key>
void Get(lua_State* lua, int index, const Keys keys) {
  GetInternedLuaBlockField(lua, index, keys.index, Key);
}

}

#define OW_LAYOUT_KEY(name) ::openwow::ui::game::runtime::layout_field::Ordinal(name)

bool ReadNumber(lua_State* lua, int index, const char* field, double* value) {
  index = lua_absindex(lua, index);
  GetInternedLuaField(lua, index, field);
  const bool found = lua_isnumber(lua, -1) != 0;
  if (found && value != nullptr) *value = lua_tonumber(lua, -1);
  lua_pop(lua, 1);
  return found;
}

template <int Key>
bool ReadNumber(lua_State* lua, int index, layout_field::Keys keys,
                double* value) {
  layout_field::Get<Key>(lua, index, keys);
  const bool found = lua_isnumber(lua, -1) != 0;
  if (found && value != nullptr) *value = lua_tonumber(lua, -1);
  lua_pop(lua, 1);
  return found;
}

template <int Key>
bool ReadInteger(lua_State* lua, int index, layout_field::Keys keys,
                 int* value) {
  double number = 0.0;
  if (!ReadNumber<Key>(lua, index, keys, &number)) return false;
  if (value != nullptr) *value = static_cast<int>(number);
  return true;
}

template <int Key>
bool ReadBoolean(lua_State* lua, int index, layout_field::Keys keys) {
  layout_field::Get<Key>(lua, index, keys);
  const bool value = lua_toboolean(lua, -1) != 0;
  lua_pop(lua, 1);
  return value;
}

template <int Key>
std::optional<bool> ReadOptionalBoolean(lua_State* lua, int index,
                                        layout_field::Keys keys) {
  layout_field::Get<Key>(lua, index, keys);
  std::optional<bool> value;
  if (lua_isboolean(lua, -1) != 0) {
    value = lua_toboolean(lua, -1) != 0;
  }
  lua_pop(lua, 1);
  return value;
}

template <int Key>
std::optional<std::string> ReadStringField(lua_State* lua, int index,
                                           layout_field::Keys keys) {
  if (lua == nullptr || lua_istable(lua, index) == 0) return std::nullopt;
  layout_field::Get<Key>(lua, index, keys);
  std::optional<std::string> value;
  if (lua_isstring(lua, -1) != 0) {
    if (const char* text = lua_tostring(lua, -1); text != nullptr) value = text;
  }
  lua_pop(lua, 1);
  return value;
}

bool ReadInteger(lua_State* lua, int index, const char* field, int* value) {
  double number = 0.0;
  if (!ReadNumber(lua, index, field, &number)) return false;
  if (value != nullptr) *value = static_cast<int>(number);
  return true;
}

bool ReadBoolean(lua_State* lua, int index, const char* field) {
  index = lua_absindex(lua, index);
  GetInternedLuaField(lua, index, field);
  const bool value = lua_toboolean(lua, -1) != 0;
  lua_pop(lua, 1);
  return value;
}

void ClearField(lua_State* lua, int index, const char* field) {
  lua_pushnil(lua);
  lua_setfield(lua, lua_absindex(lua, index), field);
}

std::optional<std::string> ReadInternedStringField(lua_State* lua, int index,
                                                   const char* field) {
  if (lua == nullptr || lua_istable(lua, index) == 0) return std::nullopt;
  index = lua_absindex(lua, index);
  GetInternedLuaField(lua, index, field);
  std::optional<std::string> value;
  if (lua_isstring(lua, -1) != 0) {
    if (const char* text = lua_tostring(lua, -1); text != nullptr) value = text;
  }
  lua_pop(lua, 1);
  return value;
}

std::optional<std::string> ReadFrameKey(lua_State* lua, int index,
                                        layout_field::Keys keys) {
  if (lua_istable(lua, index) == 0) return std::nullopt;
  auto key = ReadStringField<OW_LAYOUT_KEY("__ow_frame_key")>(lua, index, keys);
  if (key.has_value() && !key->empty()) return key;
  return ReadStringField<OW_LAYOUT_KEY("__ow_name")>(lua, index, keys);
}

template <int Key>
const char* BorrowStringField(lua_State* lua, int index, layout_field::Keys keys) {
  layout_field::Get<Key>(lua, index, keys);
  return lua_isstring(lua, -1) != 0 ? lua_tostring(lua, -1) : nullptr;
}

void ReadAnchorsWithKeysInto(lua_State* lua, int frame_index,
                             layout_field::Keys keys,
                             std::vector<openwow::ui::framexml::UiAnchor>* out) {
  frame_index = lua_absindex(lua, frame_index);
  std::size_t stored = 0;
  layout_field::Get<OW_LAYOUT_KEY("__ow_anchors")>(lua, frame_index, keys);
  if (lua_istable(lua, -1) == 0) {
    lua_pop(lua, 1);
    out->clear();
    return;
  }
  const int table = lua_absindex(lua, -1);
  const lua_Integer count = luaL_len(lua, table);
  out->reserve(static_cast<std::size_t>(std::max<lua_Integer>(count, 0)));
  for (lua_Integer i = 1; i <= count; ++i) {
    lua_rawgeti(lua, table, i);
    if (lua_istable(lua, -1) != 0) {
      int flags = 0;
      if (ReadInteger<OW_LAYOUT_KEY("__ow_flags")>(lua, -1, keys, &flags) &&
          (static_cast<std::uint32_t>(flags) & 0x800U) != 0U) {
        lua_pop(lua, 1);
        continue;
      }
      if (stored == out->size()) out->emplace_back();
      openwow::ui::framexml::UiAnchor& anchor = (*out)[stored++];
      const int anchor_table = lua_absindex(lua, -1);

      if (const char* const point =
              BorrowStringField<OW_LAYOUT_KEY("point")>(lua, anchor_table, keys);
          point != nullptr) {
        anchor.point.assign(point);
      } else {
        anchor.point.assign("CENTER");
      }
      lua_pop(lua, 1);

      if (const char* const relative_point =
              BorrowStringField<OW_LAYOUT_KEY("relativePoint")>(lua, anchor_table,
                                                                 keys);
          relative_point != nullptr) {
        anchor.relative_point.assign(relative_point);
      } else {
        anchor.relative_point = anchor.point;
      }
      lua_pop(lua, 1);
      anchor.relative_to.clear();
      anchor.relative_to_explicit = false;
      layout_field::Get<OW_LAYOUT_KEY("relativeTo")>(lua, anchor_table, keys);
      if (lua_isstring(lua, -1) != 0) {
        anchor.relative_to.assign(lua_tostring(lua, -1));
      } else if (const auto key = ReadFrameKey(lua, -1, keys); key.has_value()) {
        anchor.relative_to = *key;
      }
      lua_pop(lua, 1);

      double value = 0.0;
      anchor.x = ReadNumber<OW_LAYOUT_KEY("x")>(lua, anchor_table, keys, &value)
                     ? static_cast<float>(value)
                     : 0.0F;
      anchor.y = ReadNumber<OW_LAYOUT_KEY("y")>(lua, anchor_table, keys, &value)
                     ? static_cast<float>(value)
                     : 0.0F;
      anchor.flags = static_cast<std::uint32_t>(flags);
    }
    lua_pop(lua, 1);
  }
  lua_pop(lua, 1);
  out->resize(stored);
}

std::vector<openwow::ui::framexml::UiAnchor> ReadAnchorsWithKeys(
    lua_State* lua, int frame_index, layout_field::Keys keys) {
  std::vector<openwow::ui::framexml::UiAnchor> anchors;
  ReadAnchorsWithKeysInto(lua, frame_index, keys, &anchors);
  return anchors;
}

std::vector<openwow::ui::framexml::UiAnchor> ReadAnchors(lua_State* lua,
                                                         int frame_index) {
  frame_index = lua_absindex(lua, frame_index);
  const int top = lua_gettop(lua);
  const layout_field::Keys keys = layout_field::PushKeys(lua);
  auto anchors = ReadAnchorsWithKeys(lua, frame_index, keys);
  lua_settop(lua, top);
  return anchors;
}

void SetAnchor(lua_State* lua, int frame_index, std::string_view point,
               std::string_view relative_name, double x, double y) {
  frame_index = lua_absindex(lua, frame_index);
  lua_newtable(lua);
  const int anchors = lua_absindex(lua, -1);
  lua_newtable(lua);
  const int anchor = lua_absindex(lua, -1);
  lua_pushlstring(lua, point.data(), point.size());
  lua_setfield(lua, anchor, "point");
  if (!relative_name.empty()) {
    lua_getglobal(lua, std::string(relative_name).c_str());
    if (lua_istable(lua, -1) != 0) {
      lua_setfield(lua, anchor, "relativeTo");
    } else {
      lua_pop(lua, 1);
      lua_pushlstring(lua, relative_name.data(), relative_name.size());
      lua_setfield(lua, anchor, "relativeTo");
    }
  }
  lua_pushlstring(lua, point.data(), point.size());
  lua_setfield(lua, anchor, "relativePoint");
  openwow::ui::WriteLuaNumberField(lua, anchor, "x", x);
  openwow::ui::WriteLuaNumberField(lua, anchor, "y", y);
  lua_rawseti(lua, anchors, 1);
  lua_setfield(lua, frame_index, "__ow_anchors");
  ClearField(lua, frame_index, "__ow_setAllPoints");
  openwow::ui::game::detail::ReindexLuaAnchorDependents(lua, frame_index);
}

bool ApplyAnchorDelta(lua_State* lua, int frame_index, float dx, float dy,
                      float scale) {
  if (scale == 0.0F) return false;
  lua_getfield(lua, frame_index, "__ow_anchors");
  if (lua_istable(lua, -1) == 0) { lua_pop(lua, 1); return false; }
  lua_rawgeti(lua, -1, 1);
  if (lua_istable(lua, -1) == 0) { lua_pop(lua, 2); return false; }
  int flags = 0;
  if (ReadInteger(lua, -1, "__ow_flags", &flags) &&
      (static_cast<std::uint32_t>(flags) & 0x800U) != 0U) {
    lua_pop(lua, 2);
    return false;
  }
  double value = 0.0;
  const double x = ReadNumber(lua, -1, "x", &value) ? value : 0.0;
  const double y = ReadNumber(lua, -1, "y", &value) ? value : 0.0;
  openwow::ui::WriteLuaNumberField(lua, -1, "x", x + dx / scale);
  openwow::ui::WriteLuaNumberField(lua, -1, "y", y - dy / scale);
  lua_pop(lua, 2);
  return true;
}

float EffectiveScale(const openwow::ui::framexml::UiFrame& frame,
                     const FrameStore& frames, float screen_height,
                     float root_scale) {
  float scale = (screen_height > 0.0F ? screen_height / kUiVirtualHeight : 1.0F) * root_scale;
  const auto* current = &frame;
  for (int depth = 0; current != nullptr && depth < 64; ++depth) {
    scale *= current->scale > 0.0F ? current->scale : 1.0F;
    current = current->parent.empty() ? nullptr : frames.FindFrame(current->parent);
  }
  return scale > 0.0F ? scale : 1.0F;
}

std::string RelativeName(const openwow::ui::framexml::UiFrame& frame,
                         const std::vector<openwow::ui::framexml::UiAnchor>& anchors) {
  if (!anchors.empty() && !anchors.front().relative_to.empty()) return anchors.front().relative_to;
  return frame.parent.empty() ? "UIParent" : frame.parent;
}

openwow::ui::framexml::FrameRect ReferenceRect(
    const openwow::ui::TransparentStringMap<openwow::ui::framexml::FrameRect>& rects,
    float width, float height, std::string_view name) {
  if (!name.empty() && name != "UIParent") {
    if (const auto it = rects.find(name); it != rects.end()) return it->second;
  }
  return {.x = 0, .y = 0, .width = static_cast<int>(std::lround(width)),
          .height = static_cast<int>(std::lround(height))};
}

NearestMatchingFramePointPlacement NearestPlacement(
    double left, double top, double right, double bottom,
    const openwow::ui::framexml::FrameRect& relative) {
  static constexpr std::array<double, 9> x = {0, .5, 1, 0, .5, 1, 0, .5, 1};
  static constexpr std::array<double, 9> y = {0, 0, 0, .5, .5, .5, 1, 1, 1};
  NearestMatchingFramePointPlacement best{};
  double distance = std::numeric_limits<double>::max();
  for (std::size_t point = 0; point < x.size(); ++point) {
    const double dx = left + (right - left) * x[point] -
                      (relative.x + relative.width * x[point]);
    const double dy = top + (bottom - top) * y[point] -
                      (relative.y + relative.height * y[point]);
    const double candidate = dx * dx + dy * dy;
    if (candidate < distance) {
      distance = candidate;
      best = {.point_index = point, .pixel_offset_x = static_cast<float>(dx),
              .pixel_offset_y = static_cast<float>(-dy)};
    }
  }
  return best;
}

bool RectanglesOverlap(const openwow::ui::framexml::FrameRect& lhs,
                       const openwow::ui::framexml::FrameRect& rhs) {
  return lhs.x < rhs.x + rhs.width && lhs.x + lhs.width > rhs.x &&
         lhs.y < rhs.y + rhs.height && lhs.y + lhs.height > rhs.y;
}

struct RectDdc { float min_x; float min_y; float max_x; float max_y; };

std::optional<RectDdc> ResolveRegionDdc(
    const openwow::ui::framexml::UiFrame& region,
    const openwow::ui::TransparentStringMap<RectDdc>& rects, float scale) {
  if (region.set_all_points) {
    const auto it = rects.find(
        openwow::ui::framexml::detail::SetAllPointsTargetName(region));
    return it == rects.end() ? std::nullopt : std::optional(it->second);
  }
  const auto slots = openwow::ui::framexml::detail::BuildAnchorSlots(region.anchors);
  const auto lookup = [&](std::string_view name)
      -> std::optional<openwow::ui::framexml::detail::AnchorRect> {
    const auto it = rects.find(name);
    if (it == rects.end()) return std::nullopt;
    return openwow::ui::framexml::detail::AnchorRect{
        .min_x = it->second.min_x, .min_y = it->second.min_y,
        .max_x = it->second.max_x, .max_y = it->second.max_y};
  };
  const auto optional = [](float value) -> std::optional<float> {
    return value == openwow::ui::framexml::detail::kAnchorUnresolvedCoordinate
               ? std::nullopt : std::optional(value);
  };
  const auto resolve_x = [&](const auto& points) {
    return optional(openwow::ui::framexml::detail::ResolvePointSetWithRetries(
        slots, points, region.parent, scale, lookup,
        openwow::ui::framexml::detail::ResolveRelativeX));
  };
  const auto resolve_y = [&](const auto& points) {
    return optional(openwow::ui::framexml::detail::ResolvePointSetWithRetries(
        slots, points, region.parent, scale, lookup,
        openwow::ui::framexml::detail::ResolveRelativeY));
  };
  auto left = resolve_x(openwow::ui::framexml::detail::kLeftPointSlots);
  auto right = resolve_x(openwow::ui::framexml::detail::kRightPointSlots);
  const auto center_x = resolve_x(openwow::ui::framexml::detail::kHorizontalCenterPointSlots);
  auto bottom = resolve_y(openwow::ui::framexml::detail::kBottomPointSlots);
  auto top = resolve_y(openwow::ui::framexml::detail::kTopPointSlots);
  const auto center_y = resolve_y(openwow::ui::framexml::detail::kVerticalCenterPointSlots);
  const float width = region.width.value_or(0.0F) * scale;
  const float height = region.height.value_or(0.0F) * scale;
  const auto synthesize = [](std::optional<float> center,
                             std::optional<float> opposite, float size) {
    if (opposite.has_value() && size != 0.0F) return std::optional(*opposite + size);
    if (center.has_value() && size == 0.0F && opposite.has_value())
      return std::optional(*center + *center - *opposite);
    if (center.has_value() && size != 0.0F) return std::optional(*center + size * 0.5F);
    return std::optional<float>{};
  };
  if (!left) left = synthesize(center_x, right, -width);
  if (!right) right = synthesize(center_x, left, width);
  if (!bottom) bottom = synthesize(center_y, top, -height);
  if (!top) top = synthesize(center_y, bottom, height);
  if (!left || !right || !bottom || !top) return std::nullopt;
  RectDdc result{*left, *bottom, *right, *top};
  if (result.max_x < result.min_x) std::swap(result.max_x, result.min_x);
  if (result.max_y < result.min_y) std::swap(result.max_y, result.min_y);
  return result;
}

}

struct RetainedLayout::Impl {
  FrameStore& frames;
  Ports ports;
  lua_State* lua{nullptr};
  openwow::ui::TextureNaturalSizeSource* texture_natural_size{nullptr};
  openwow::ui::ModelNaturalSizeSource* model_natural_size{nullptr};

  struct PendingNaturalSize {
    enum class Kind : std::uint8_t { kTexture, kModel };
    Kind kind{Kind::kTexture};
    std::string path;
  };
  std::unordered_map<std::string, PendingNaturalSize> natural_size_pending;
  float width{1280.0F};
  float height{720.0F};
  float root_scale{1.0F};
  std::int32_t mode{0};
  bool dirty{true};
  bool solving{false};
  bool graph_valid{false};
  bool full_solve_required{true};
  bool bootstrap_active{false};
  std::size_t construction_depth{0};
  openwow::ui::TransparentStringMap<openwow::ui::framexml::FrameRect> rects;

  std::uint64_t rects_generation{0};
  openwow::ui::TransparentStringMap<ScrollFrameRanges> scroll_frame_ranges;

  std::uint64_t scroll_frame_ranges_generation{0};
  std::unordered_map<std::string, std::vector<std::string>> dependencies;
  std::unordered_map<std::string, std::vector<std::string>> dependents;
  std::vector<std::string> pending_names;
  std::unordered_set<std::string> pending_members;

  std::vector<const openwow::ui::framexml::UiFrame*> affected_scratch;
  std::unordered_set<const openwow::ui::framexml::UiFrame*> closure_member_scratch;
  std::vector<std::optional<openwow::ui::framexml::FrameRect>> solved_scratch;
  Metrics metrics;

  void RebuildScrollFrameRanges() {
    struct PresentedBounds {
      int right{0};
      int bottom{0};
      bool populated{false};
    };

    scroll_frame_ranges.clear();
    ++scroll_frame_ranges_generation;
    openwow::ui::TransparentStringMap<PresentedBounds> bounds_by_owner;
    constexpr int kMaxParentDepth = 64;

    struct OwnerResolution {
      const std::string* owner{nullptr};
    };
    std::unordered_map<const openwow::ui::framexml::UiFrame*, OwnerResolution>
        resolved_owners;
    std::vector<const openwow::ui::framexml::UiFrame*> climb_path;
    const auto resolve_owner =
        [&](const openwow::ui::framexml::UiFrame& start) -> OwnerResolution {
      climb_path.clear();
      const auto* current = &start;
      OwnerResolution result{};
      for (int depth = 0; depth < kMaxParentDepth; ++depth) {
        if (const auto memo = resolved_owners.find(current);
            memo != resolved_owners.end()) {
          result = memo->second;
          break;
        }
        climb_path.push_back(current);
        if (!current->visible || current->parent.empty()) break;
        const auto* parent = frames.FindFrame(current->parent);
        if (parent == nullptr) break;

        if (openwow::text::EqualsIgnoreCaseAscii(parent->kind, "ScrollFrame") &&
            current->scroll_child_membership_count >
                parent->scroll_child_membership_count) {
          result = {.owner = &current->parent};
          break;
        }
        current = parent;
      }
      for (const auto* member : climb_path) {
        resolved_owners.insert_or_assign(member, result);
      }
      return result;
    };

    for (const auto& key : frames.ScrollChildContentFrames()) {
      const auto* const frame = frames.FindFrame(key);
      if (frame == nullptr || !frame->visible) continue;
      const auto rect = rects.find(key);
      if (rect == rects.end() || rect->second.width <= 0 ||
          rect->second.height <= 0) {
        continue;
      }
      const auto owner = resolve_owner(*frame).owner;
      if (owner == nullptr) continue;
      auto bounds = bounds_by_owner.find(*owner);
      if (bounds == bounds_by_owner.end()) {
        bounds = bounds_by_owner.emplace(*owner, PresentedBounds{}).first;
      }
      const int right = rect->second.x + rect->second.width;
      const int bottom = rect->second.y + rect->second.height;
      if (!bounds->second.populated) {
        bounds->second = {.right = right, .bottom = bottom, .populated = true};
      } else {
        bounds->second.right = std::max(bounds->second.right, right);
        bounds->second.bottom = std::max(bounds->second.bottom, bottom);
      }
    }

    scroll_frame_ranges.reserve(bounds_by_owner.size());
    for (const auto& [owner_name, bounds] : bounds_by_owner) {
      const auto owner_rect = rects.find(owner_name);
      const auto* owner = frames.FindFrame(owner_name);
      if (!bounds.populated || owner_rect == rects.end() || owner == nullptr)
        continue;
      const double pixels_per_script_unit = static_cast<double>(
          EffectiveScale(*owner, frames, height, root_scale));
      if (pixels_per_script_unit <= 0.0) continue;
      scroll_frame_ranges.emplace(
          owner_name,
          ScrollFrameRanges{
              .horizontal =
                  std::max(0, bounds.right -
                                  (owner_rect->second.x +
                                   owner_rect->second.width)) /
                  pixels_per_script_unit,
              .vertical =
                  std::max(0, bounds.bottom -
                                  (owner_rect->second.y +
                                   owner_rect->second.height)) /
                  pixels_per_script_unit,
          });
    }
  }

  void Reindex(std::string_view key) {
    if (!graph_valid || key.empty()) return;
    const std::string name(key);
    if (const auto previous = dependencies.find(name); previous != dependencies.end()) {
      for (const auto& dependency : previous->second) {
        if (auto it = dependents.find(dependency); it != dependents.end()) {
          std::erase(it->second, name);
          if (it->second.empty()) dependents.erase(it);
        }
      }
    }
    const auto* frame = frames.FindFrame(name);
    if (frame == nullptr) {
      dependencies.erase(name);
      return;
    }
    std::vector<std::string> values;
    const auto append = [&](std::string_view dependency) {
      if (!dependency.empty() && dependency != name &&
          std::find(values.begin(), values.end(), dependency) == values.end()) {
        values.emplace_back(dependency);
      }
    };
    append(frame->parent);
    for (const auto& anchor : frame->anchors) {
      append(anchor.relative_to.empty() ? std::string_view(frame->parent)
                                        : std::string_view(anchor.relative_to));
    }

    for (const auto& dependency : values) {
      auto& receivers = dependents[dependency];
      if (std::find(receivers.begin(), receivers.end(), name) == receivers.end())
        receivers.push_back(name);
    }
    dependencies.insert_or_assign(name, std::move(values));
  }

  void RebuildGraph() {
    dependencies.clear();
    dependents.clear();
    dependencies.reserve(frames.size());
    dependents.reserve(frames.size());
    graph_valid = true;
    for (const auto& view : frames.CollectStableFrames()) Reindex(view.key);
    ++metrics.dependency_graph_rebuilds;
  }

  void Sync(const std::span<const std::string> names) {
    if (lua == nullptr) return;
    metrics.last_lua_sync_candidates = names.size();

    const int keys_base_top = lua_gettop(lua);
    const layout_field::Keys keys = layout_field::PushKeys(lua);
    struct KeyBlockPop {
      lua_State* lua;
      int restore_top;
      ~KeyBlockPop() { lua_settop(lua, restore_top); }
    } const key_block_pop{lua, keys_base_top};
    for (const auto& name : names) {
      const auto ref = frames.FindLuaRef(name);
      auto* frame = frames.FindFrame(name);
      if (!ref.has_value() || frame == nullptr) continue;
      const int top = lua_gettop(lua);
      lua_rawgeti(lua, LUA_REGISTRYINDEX, *ref);
      if (lua_istable(lua, -1) == 0) {
        lua_settop(lua, top);
        continue;
      }
      const int index = lua_absindex(lua, -1);

      std::string old_parent;
      bool parent_changed = false;
      bool kind_changed = false;
      bool strata_changed = false;
      const int old_level = frame->frame_level;
      const float old_depth = frame->depth;
      const float old_scale = frame->scale;
      const bool old_visible = frame->visible;
      const bool old_draw_layer_enabled = frame->runtime_draw_layer_enabled;
      const auto replace_parent = [&](const char* const parent) {
        old_parent = std::move(frame->parent);
        frame->parent.assign(parent);
        parent_changed = true;
      };
      if (const char* const parent =
              BorrowStringField<OW_LAYOUT_KEY("__ow_parent_name")>(lua, index,
                                                                   keys);
          parent != nullptr) {
        if (frame->parent != parent) replace_parent(parent);
        lua_pop(lua, 1);
      } else {
        lua_pop(lua, 1);
        layout_field::Get<OW_LAYOUT_KEY("__ow_parent")>(lua, index, keys);
        if (const auto parent_ref_key = ReadFrameKey(lua, -1, keys)) {
          if (frame->parent != *parent_ref_key) {
            replace_parent(parent_ref_key->c_str());
          }
        } else if (!frame->parent.empty()) {
          replace_parent("");
        }
        lua_pop(lua, 1);
      }
      if (const char* const kind =
              BorrowStringField<OW_LAYOUT_KEY("__ow_type")>(lua, index, keys);
          kind != nullptr && kind[0] != '\0') {
        if (frame->kind != kind) {
          frame->kind.assign(kind);
          kind_changed = true;
        }

        frame->runtime_kind =
            openwow::ui::framexml::DeriveUiFrameRuntimeKind(frame->kind);

        if (kind_changed) frames.InvalidateClassificationIndex();
      }
      lua_pop(lua, 1);
      double number = 0.0;
      if (ReadNumber<OW_LAYOUT_KEY("__ow_width")>(lua, index, keys, &number))
        frame->width = static_cast<float>(number);
      if (ReadNumber<OW_LAYOUT_KEY("__ow_height")>(lua, index, keys, &number))
        frame->height = static_cast<float>(number);
      frame->layout_offset_x =
          ReadNumber<OW_LAYOUT_KEY("__ow_layout_offset_x")>(lua, index, keys,
                                                            &number)
              ? static_cast<float>(number)
              : 0.0F;
      frame->layout_offset_y =
          ReadNumber<OW_LAYOUT_KEY("__ow_layout_offset_y")>(lua, index, keys,
                                                            &number)
              ? static_cast<float>(number)
              : 0.0F;
      if (ReadNumber<OW_LAYOUT_KEY("__ow_scale")>(lua, index, keys, &number))
        frame->scale = static_cast<float>(number);
      if (ReadNumber<OW_LAYOUT_KEY("__ow_depth")>(lua, index, keys, &number))
        frame->depth = static_cast<float>(number);
      (void)ReadInteger<OW_LAYOUT_KEY("__ow_frame_level")>(lua, index, keys,
                                                           &frame->frame_level);
      int id = 0;
      if (ReadInteger<OW_LAYOUT_KEY("__ow_id")>(lua, index, keys, &id)) {
        frame->id = id;
        frame->has_id = true;
      }
      if (const char* const strata =
              BorrowStringField<OW_LAYOUT_KEY("__ow_frame_strata")>(lua, index,
                                                                    keys);
          strata != nullptr && frame->frame_strata != strata) {
        frame->frame_strata.assign(strata);
        strata_changed = true;
      }
      lua_pop(lua, 1);
      frame->top_level = ReadBoolean<OW_LAYOUT_KEY("__ow_toplevel")>(lua, index, keys);
      frame->toplevel = frame->top_level;
      frame->user_placed = ReadBoolean<OW_LAYOUT_KEY("__ow_user_placed")>(lua, index, keys);
      frame->dont_save_position = ReadBoolean<OW_LAYOUT_KEY("__ow_dontsavepos")>(lua, index, keys);

      layout_field::Get<OW_LAYOUT_KEY("__ow_visible")>(lua, index, keys);
      frame->visible = lua_isboolean(lua, -1) == 0 || lua_toboolean(lua, -1) != 0;
      lua_pop(lua, 1);
      layout_field::Get<OW_LAYOUT_KEY("__ow_draw_layer_enabled")>(lua, index, keys);
      frame->runtime_draw_layer_enabled =
          lua_isboolean(lua, -1) == 0 || lua_toboolean(lua, -1) != 0;
      lua_pop(lua, 1);
      frame->enable_mouse = ReadBoolean<OW_LAYOUT_KEY("__ow_enableMouse")>(lua, index, keys);
      frame->enable_keyboard = ReadBoolean<OW_LAYOUT_KEY("__ow_enableKeyboard")>(lua, index, keys);
      layout_field::Get<OW_LAYOUT_KEY("__ow_clamped")>(lua, index, keys);
      if (lua_isboolean(lua, -1) != 0) frame->clamped_to_screen = lua_toboolean(lua, -1) != 0;
      lua_pop(lua, 1);
      if (ReadNumber<OW_LAYOUT_KEY("__ow_clamp_l")>(lua, index, keys, &number))
        frame->clamp_rect_inset_left = number;
      if (ReadNumber<OW_LAYOUT_KEY("__ow_clamp_r")>(lua, index, keys, &number))
        frame->clamp_rect_inset_right = number;
      if (ReadNumber<OW_LAYOUT_KEY("__ow_clamp_t")>(lua, index, keys, &number))
        frame->clamp_rect_inset_top = number;
      if (ReadNumber<OW_LAYOUT_KEY("__ow_clamp_b")>(lua, index, keys, &number))
        frame->clamp_rect_inset_bottom = number;
      layout_field::Get<OW_LAYOUT_KEY("__ow_anchors")>(lua, index, keys);
      const bool has_anchors = lua_istable(lua, -1) != 0;
      lua_pop(lua, 1);
      if (has_anchors)
        frame->set_all_points =
            ReadBoolean<OW_LAYOUT_KEY("__ow_setAllPoints")>(lua, index, keys);
      ReadAnchorsWithKeysInto(lua, index, keys, &frame->anchors);
      if (ReadNumber<OW_LAYOUT_KEY("__ow_hit_l")>(lua, index, keys, &number))
        frame->hit_rect_inset_left = number;
      if (ReadNumber<OW_LAYOUT_KEY("__ow_hit_r")>(lua, index, keys, &number))
        frame->hit_rect_inset_right = number;
      if (ReadNumber<OW_LAYOUT_KEY("__ow_hit_t")>(lua, index, keys, &number))
        frame->hit_rect_inset_top = number;
      if (ReadNumber<OW_LAYOUT_KEY("__ow_hit_b")>(lua, index, keys, &number))
        frame->hit_rect_inset_bottom = number;
      if (frame->runtime_kind == openwow::ui::framexml::UiFrame::RuntimeKind::FontString) {
        if (const auto from_layout =
                ReadOptionalBoolean<OW_LAYOUT_KEY("__ow_font_width_from_layout")>(
                    lua, index, keys)) {
          frame->font_intrinsic_width = !*from_layout;
        }
        if (const auto from_layout =
                ReadOptionalBoolean<OW_LAYOUT_KEY("__ow_font_height_from_layout")>(
                    lua, index, keys)) {
          frame->font_intrinsic_height = !*from_layout;
        }
        const bool width_intrinsic = !openwow::ui::FontStringWidthComesFromLayout(*frame);
        const bool height_intrinsic = !openwow::ui::FontStringHeightComesFromLayout(*frame);
        layout_field::Get<OW_LAYOUT_KEY("__ow_text")>(lua, index, keys);
        const char* text = lua_tostring(lua, -1);
        const bool has_text = text != nullptr && text[0] != '\0';
        lua_pop(lua, 1);
        if (!has_text) {
          if (width_intrinsic) frame->width.reset();
          if (height_intrinsic) frame->height.reset();
          frame->font_intrinsic_width = false;
          frame->font_intrinsic_height = false;
        } else if (const auto measured = frame_api::MeasureLuaFontStringMetrics(lua, index);
                   measured.has_value()) {
          if (width_intrinsic && measured->width > 0.0F) {
            frame->width = openwow::ui::ResolveFontStringEffectiveScriptDimension(0.0F, measured->width);
            frame->font_intrinsic_width = true;
          }
          if (height_intrinsic && measured->height > 0.0F) {
            frame->height = openwow::ui::ResolveFontStringEffectiveScriptDimension(0.0F, measured->height);
            frame->font_intrinsic_height = true;
          }
        }
      }
      if (frame->runtime_kind == openwow::ui::framexml::UiFrame::RuntimeKind::Texture) {
        SyncTextureNaturalSizeFromLua(index, name, frame);
      } else if (openwow::ui::framexml::IsSimpleModelRuntimeKind(frame->runtime_kind)) {
        SyncModelNaturalSizeFromLua(index, name, keys, frame);
      }
      SynchronizeEditBoxRetainedTextInsets(lua, index, name, frame, &frames);
      if (parent_changed) frames.NotifyHierarchyMutation(name, old_parent);

      if (parent_changed || kind_changed) {
        if (ports.order_invalidated) ports.order_invalidated();
      } else {

        if ((strata_changed || old_level != frame->frame_level) &&
            ports.order_key_invalidated) {
          ports.order_key_invalidated(name);
        }

        if ((std::fabs(old_depth - frame->depth) >= kScaleWriteEpsilon ||
             std::fabs(old_scale - frame->scale) >= kScaleWriteEpsilon ||
             old_visible != frame->visible ||
             old_draw_layer_enabled != frame->runtime_draw_layer_enabled) &&
            ports.inherited_order_invalidated) {
          ports.inherited_order_invalidated(name);
        }
      }
      if (graph_valid) Reindex(name);
      lua_settop(lua, top);
    }
  }

  void SyncTextureNaturalSizeFromLua(const int index, const std::string& name,
                                     openwow::ui::framexml::UiFrame* const frame) {
    const openwow::ui::game::runtime::TextureHandleState handle =
        openwow::ui::game::runtime::ResolveTextureHandleState(lua, index, frame);
    const bool unknown = openwow::ui::SyncTextureNaturalSize(
        *frame, texture_natural_size, *handle.texture_path, handle.solid_colour);
    if (unknown) {
      natural_size_pending.insert_or_assign(
          name, PendingNaturalSize{PendingNaturalSize::Kind::kTexture,
                                   *handle.texture_path});
    } else if (!natural_size_pending.empty()) {
      natural_size_pending.erase(name);
    }
  }

  void SyncModelNaturalSizeFromLua(const int index, const std::string& name,
                                   const layout_field::Keys keys,
                                   openwow::ui::framexml::UiFrame* const frame) {
    const char* const lua_model_path =
        BorrowStringField<OW_LAYOUT_KEY("__ow_model_path")>(lua, index, keys);
    const std::string& model_path =
        lua_model_path != nullptr && lua_model_path[0] != '\0'
            ? model_path_scratch.assign(lua_model_path)
            : frame->file;
    lua_pop(lua, 1);
    const bool unknown =
        openwow::ui::SyncModelNaturalSize(*frame, model_natural_size, model_path);
    if (unknown) {
      natural_size_pending.insert_or_assign(
          name, PendingNaturalSize{PendingNaturalSize::Kind::kModel, model_path});
    } else if (!natural_size_pending.empty()) {
      natural_size_pending.erase(name);
    }
  }

  std::string model_path_scratch;

  void Sync() {
    if (lua == nullptr) return;
    const std::vector<std::string> names = frames.CollectBindingKeys();
    Sync(names);
  }

  void SolveOnDemand(std::string_view requested) {
    if (lua == nullptr || requested.empty() || width <= 0.0F || height <= 0.0F) return;
    std::vector<std::string> names{std::string(requested)};
    std::unordered_set<std::string> members{std::string(requested)};
    std::size_t synchronized = 0;
    for (std::size_t cursor = 0; cursor < names.size(); ++cursor) {
      const auto& name = names[cursor];
      if (pending_members.erase(name) != 0) {
        Sync(std::span<const std::string>(&name, 1));
        ++synchronized;
      }
      const auto* frame = frames.FindFrame(name);
      if (frame == nullptr) continue;
      const auto append = [&](const std::string& dependency) {
        if (!dependency.empty() && members.insert(dependency).second) names.push_back(dependency);
      };
      append(frame->parent);
      for (const auto& anchor : frame->anchors)
        append(anchor.relative_to.empty() ? frame->parent : anchor.relative_to);
    }
    metrics.last_lua_sync_candidates = synchronized;
    std::vector<const openwow::ui::framexml::UiFrame*> solve_frames;
    solve_frames.reserve(names.size());
    for (const auto& name : names)
      if (const auto* frame = frames.FindFrame(name)) solve_frames.push_back(frame);
    metrics.last_on_demand_frames = solve_frames.size();
    auto solved = openwow::ui::framexml::ResolveExpandedLayout(
        solve_frames, static_cast<int>(width), static_cast<int>(height),
        height / kUiVirtualHeight * root_scale);
    for (auto& [name, rect] : solved) {
      if (rects.insert_or_assign(std::move(name), rect).second)
        ++rects_generation;
    }
  }
};

RetainedLayout::RetainedLayout(FrameStore& frames, Ports ports)
    : impl_(std::make_unique<Impl>(Impl{.frames = frames, .ports = std::move(ports)})) {}
RetainedLayout::~RetainedLayout() = default;
void RetainedLayout::BindLuaState(lua_State* lua) noexcept { impl_->lua = lua; }
void RetainedLayout::BindTextureNaturalSizeSource(
    openwow::ui::TextureNaturalSizeSource* const source) noexcept {
  impl_->texture_natural_size = source;
}
openwow::ui::TextureNaturalSizeSource*
RetainedLayout::texture_natural_size_source() const noexcept {
  return impl_->texture_natural_size;
}
void RetainedLayout::BindModelNaturalSizeSource(
    openwow::ui::ModelNaturalSizeSource* const source) noexcept {
  impl_->model_natural_size = source;
}
openwow::ui::ModelNaturalSizeSource*
RetainedLayout::model_natural_size_source() const noexcept {
  return impl_->model_natural_size;
}
void RetainedLayout::RetryPendingNaturalSizes() {
  auto& x = *impl_;
  if (x.natural_size_pending.empty()) return;
  for (auto it = x.natural_size_pending.begin(); it != x.natural_size_pending.end();) {
    if (x.frames.FindFrame(it->first) == nullptr) {
      it = x.natural_size_pending.erase(it);
      continue;
    }
    bool answered = false;
    switch (it->second.kind) {
      case Impl::PendingNaturalSize::Kind::kTexture:
        answered = x.texture_natural_size != nullptr &&
                   x.texture_natural_size->ResolveTexturePixelSize(it->second.path)
                       .has_value();
        break;
      case Impl::PendingNaturalSize::Kind::kModel:
        answered = x.model_natural_size != nullptr &&
                   x.model_natural_size->ResolveModelBoundingBox(it->second.path)
                       .has_value();
        break;
    }
    if (!answered) {
      ++it;
      continue;
    }

    QueueLuaMutation(it->first);
    it = x.natural_size_pending.erase(it);
  }
}
void RetainedLayout::Reserve(std::size_t capacity) {
  impl_->rects.reserve(capacity);
  impl_->pending_names.reserve(capacity);
  impl_->pending_members.reserve(capacity);
}
void RetainedLayout::Clear() {
  auto& x = *impl_;
  x.lua = nullptr;
  x.rects.clear(); ++x.rects_generation;
  x.scroll_frame_ranges.clear(); ++x.scroll_frame_ranges_generation;
  x.dependencies.clear(); x.dependents.clear();
  x.pending_names.clear(); x.pending_members.clear();
  x.natural_size_pending.clear();

  x.affected_scratch.clear(); x.closure_member_scratch.clear();
  x.solved_scratch.clear();
  x.graph_valid = false; x.full_solve_required = true; x.dirty = true;
  x.solving = false; x.bootstrap_active = false; x.construction_depth = 0;
  x.metrics = {};
}
float RetainedLayout::viewport_width() const noexcept { return impl_->width; }
float RetainedLayout::viewport_height() const noexcept { return impl_->height; }
float RetainedLayout::root_scale() const noexcept { return impl_->root_scale; }
std::int32_t RetainedLayout::mode() const noexcept { return impl_->mode; }
bool RetainedLayout::dirty() const noexcept { return impl_->dirty; }
const RetainedLayout::Metrics& RetainedLayout::metrics() const noexcept { return impl_->metrics; }
const openwow::ui::TransparentStringMap<openwow::ui::framexml::FrameRect>&
RetainedLayout::rects() const noexcept { return impl_->rects; }
std::uint64_t RetainedLayout::RectsGeneration() const noexcept {
  return impl_->rects_generation;
}
std::uint64_t RetainedLayout::ScrollFrameRangesGeneration() const noexcept {
  return impl_->scroll_frame_ranges_generation;
}

bool RetainedLayout::SetViewport(float width, float height) {
  width = std::max(1.0F, width); height = std::max(1.0F, height);
  if (impl_->width == width && impl_->height == height) return false;
  impl_->width = width; impl_->height = height;
  impl_->dirty = true;
  impl_->full_solve_required = true;
  return true;
}
void RetainedLayout::SetRootScale(float scale, bool force, bool defer_solve) {
  if (scale == 0.0F || (!force && std::fabs(impl_->root_scale - scale) < kScaleWriteEpsilon)) return;
  impl_->root_scale = scale;
  impl_->dirty = true;
  impl_->full_solve_required = true;
  if (impl_->ports.order_invalidated) impl_->ports.order_invalidated();
  if (!defer_solve) SolveIfDirty();
}
void RetainedLayout::SetMode(std::int32_t mode) noexcept { impl_->mode = mode; }
void RetainedLayout::SetBootstrapActive(bool active) noexcept { impl_->bootstrap_active = active; }
void RetainedLayout::EnterFrameTreeInstantiation() noexcept { ++impl_->construction_depth; }
void RetainedLayout::LeaveFrameTreeInstantiation() noexcept { --impl_->construction_depth; }
void RetainedLayout::Invalidate() noexcept { impl_->dirty = true; }
void RetainedLayout::InvalidateGraph() noexcept {
  impl_->dirty = true; impl_->full_solve_required = true; impl_->graph_valid = false;
  if (impl_->ports.order_invalidated) impl_->ports.order_invalidated();
  if (impl_->ports.hit_test_invalidated) impl_->ports.hit_test_invalidated();
}
void RetainedLayout::QueueLuaMutation(std::string_view name) {
  if (!name.empty() && impl_->pending_members.emplace(name).second) impl_->pending_names.emplace_back(name);
  impl_->dirty = true;
}
void RetainedLayout::PublishFrame(std::string_view name) {
  QueueLuaMutation(name);
  if (impl_->graph_valid) {
    impl_->Reindex(name);
    ++impl_->metrics.construction_nodes_published;
  }
}
void RetainedLayout::RemoveFrame(std::string_view name) {
  impl_->rects.erase(std::string(name));
  impl_->dependencies.erase(std::string(name));
  if (!impl_->natural_size_pending.empty()) impl_->natural_size_pending.erase(std::string(name));
  impl_->graph_valid = false;
  impl_->full_solve_required = true;
  impl_->dirty = true;
}
void RetainedLayout::ReindexDependencies(std::string_view name) { impl_->Reindex(name); }
void RetainedLayout::SyncTrackedFramesFromLua() { impl_->Sync(); }

void RetainedLayout::SolveIfDirty() {
  auto& x = *impl_;
  if (!x.dirty || x.width <= 0.0F || x.height <= 0.0F || x.solving) return;
  x.solving = true;
  struct Guard { bool& active; ~Guard() { active = false; } } guard{x.solving};
  x.dirty = false;
  auto names = std::move(x.pending_names);
  auto members = std::move(x.pending_members);
  x.pending_names = {}; x.pending_members = {};
  std::vector<std::string> pending;
  pending.reserve(members.size());
  for (auto& name : names) if (members.erase(name) != 0) pending.push_back(std::move(name));
  x.metrics.last_lua_sync_candidates = 0;
  if (!pending.empty()) x.Sync(pending);
  bool incremental = !x.full_solve_required && x.graph_valid && !x.rects.empty() && !pending.empty();

  auto& affected_frames = x.affected_scratch;
  auto& closure_members = x.closure_member_scratch;
  affected_frames.clear();
  closure_members.clear();
  if (incremental) {
    for (const auto& name : pending)
      if (const auto* const frame = x.frames.FindFrame(name);
          frame != nullptr && closure_members.insert(frame).second) {
        affected_frames.push_back(frame);
      }
    for (std::size_t cursor = 0; cursor < affected_frames.size(); ++cursor) {
      if (const auto it = x.dependents.find(affected_frames[cursor]->name);
          it != x.dependents.end())
        for (const auto& name : it->second)
          if (const auto* const frame = x.frames.FindFrame(name);
              frame != nullptr && closure_members.insert(frame).second) {
            affected_frames.push_back(frame);
          }
    }
    incremental = !affected_frames.empty() &&
                  affected_frames.size() * 3U <
                      std::max<std::size_t>(x.frames.size() * 2U, 1U);
  }
  const float scale = x.height / kUiVirtualHeight * x.root_scale;
  if (incremental) {

    const std::size_t affected_count = affected_frames.size();
    auto& solve_frames = affected_frames;
    auto& solve_members = closure_members;
    for (std::size_t cursor = 0; cursor < solve_frames.size(); ++cursor) {
      if (const auto it = x.dependencies.find(solve_frames[cursor]->name);
          it != x.dependencies.end())
        for (const auto& name : it->second)
          if (const auto* const frame = x.frames.FindFrame(name);
              frame != nullptr && solve_members.insert(frame).second) {
            solve_frames.push_back(frame);
          }
    }

    x.solved_scratch.clear();
    openwow::ui::framexml::ResolveExpandedLayoutInto(
        solve_frames, static_cast<int>(x.width), static_cast<int>(x.height),
        scale, &x.solved_scratch);

    for (std::size_t index = 0; index < solve_frames.size(); ++index) {
      if (const auto& rect = x.solved_scratch[index]; rect.has_value()) {

        if (x.rects.insert_or_assign(solve_frames[index]->name, *rect).second)
          ++x.rects_generation;
      }
    }
    ++x.metrics.incremental_resolves;
    x.metrics.last_resolve_candidates = solve_frames.size();

    bool scroll_ranges_may_have_changed = false;
    for (std::size_t index = 0; index < affected_count; ++index) {
      const auto* const frame = solve_frames[index];
      if (frame->scroll_child_content ||
          openwow::text::EqualsIgnoreCaseAscii(frame->kind, "ScrollFrame")) {
        scroll_ranges_may_have_changed = true;
        break;
      }
    }
    if (scroll_ranges_may_have_changed) {
      x.RebuildScrollFrameRanges();
    }
  } else {
    const auto frames = x.frames.CollectFramePointersInRegistrationOrder();

    x.rects = openwow::ui::framexml::ResolveExpandedLayout(
        frames, static_cast<int>(x.width), static_cast<int>(x.height), scale);
    ++x.rects_generation;
    x.RebuildGraph();
    x.full_solve_required = false;
    ++x.metrics.full_resolves;
    x.metrics.last_resolve_candidates = frames.size();

    x.RebuildScrollFrameRanges();
  }
  if (x.ports.hit_test_invalidated) x.ports.hit_test_invalidated();
}

void RetainedLayout::RefreshTrackedLayout() {
  impl_->Sync();
  impl_->pending_names.clear(); impl_->pending_members.clear();
  impl_->dirty = true; impl_->full_solve_required = true;
  SolveIfDirty();
  if (impl_->ports.order_invalidated) impl_->ports.order_invalidated();
}

const openwow::ui::framexml::FrameRect* RetainedLayout::FindRect(std::string_view name) {
  if (impl_->dirty && (impl_->construction_depth != 0 || impl_->bootstrap_active))
    impl_->SolveOnDemand(name);
  else SolveIfDirty();
  const auto it = impl_->rects.find(name);
  return it == impl_->rects.end() ? nullptr : &it->second;
}

RetainedLayout::ScrollFrameRanges RetainedLayout::ResolveScrollFrameRanges(
    const std::string_view frame_name) {
  SolveIfDirty();
  const auto found = impl_->scroll_frame_ranges.find(frame_name);
  return found != impl_->scroll_frame_ranges.end() ? found->second
                                                    : ScrollFrameRanges{};
}

std::string RetainedLayout::BuildLayoutCache() {
  RefreshTrackedLayout();
  return SerializeLayoutCache(impl_->frames.CollectFramesInRegistrationOrder(),
                              static_cast<int>(impl_->width), static_cast<int>(impl_->height));
}

void RetainedLayout::ApplyLayoutCache(std::string_view text) {
  struct Placement { std::string name; int level{-1}; int point{0}; int x{0}; int y{0}; int w{0}; int h{0}; } p;
  const auto starts = [](std::string_view line, std::string_view prefix) {
    return line.size() >= prefix.size() && openwow::text::EqualsIgnoreCaseAscii(line.substr(0, prefix.size()), prefix);
  };
  const auto integer = [](std::string_view value, int* out) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    int parsed = 0;
    if (std::from_chars(value.data(), value.data() + value.size(), parsed).ec == std::errc{}) *out = parsed;
  };
  const auto apply = [&](const Placement& current) {
    if (current.name.empty() || impl_->lua == nullptr) return;
    const auto ref = impl_->frames.FindLuaRef(current.name);
    auto* frame = impl_->frames.FindFrame(current.name);
    if (!ref.has_value() || frame == nullptr) return;
    const int top = lua_gettop(impl_->lua);
    lua_rawgeti(impl_->lua, LUA_REGISTRYINDEX, *ref);
    if (lua_istable(impl_->lua, -1) != 0) {
      const int index = lua_absindex(impl_->lua, -1);
      if (current.level >= 0) {
        lua_pushinteger(impl_->lua, current.level); lua_setfield(impl_->lua, index, "__ow_frame_level");
        frame->frame_level = current.level;
        if (impl_->ports.order_invalidated) impl_->ports.order_invalidated();
        if (impl_->ports.on_update_order_invalidated) impl_->ports.on_update_order_invalidated();
      }
      if (ReadBoolean(impl_->lua, index, "__ow_movable")) {
        const std::string point(LayoutCacheFramePointName(std::clamp(current.point, 0, 8)));
        SetAnchor(impl_->lua, index, point, "UIParent", current.x, current.y);
        lua_pushboolean(impl_->lua, 1); lua_setfield(impl_->lua, index, "__ow_user_placed");
        frame->user_placed = true;
      }
      if (ReadBoolean(impl_->lua, index, "__ow_resizable")) {
        openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_width", current.w);
        openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_height", current.h);
      }
    }
    lua_settop(impl_->lua, top);
    QueueLuaMutation(current.name);
  };
  for (std::size_t cursor = 0; cursor < text.size();) {
    const auto line_end = text.find_first_of("\r\n", cursor);
    const auto end = line_end == std::string_view::npos ? text.size() : line_end;
    const auto line = text.substr(cursor, end - cursor);
    if (starts(line, "Frame: ")) {
      apply(p); p.name.assign(line.substr(7)); p.level = -1; p.point = p.x = p.y = p.w = 0;

    } else if (starts(line, "FrameLevel: ")) integer(line.substr(12), &p.level);
    else if (starts(line, "Anchor: ")) { std::string point(line.substr(8)); StringToFramePoint(point.c_str(), &p.point); }
    else if (starts(line, "X: ")) integer(line.substr(3), &p.x);
    else if (starts(line, "Y: ")) integer(line.substr(3), &p.y);
    else if (starts(line, "W: ")) integer(line.substr(3), &p.w);
    else if (starts(line, "H: ")) integer(line.substr(3), &p.h);
    if (line_end == std::string_view::npos) break;
    cursor = line_end + 1;
    if (cursor < text.size() && text[line_end] == '\r' && text[cursor] == '\n') ++cursor;
  }
  apply(p);
  SolveIfDirty();
  if (impl_->ports.order_invalidated) impl_->ports.order_invalidated();
}

bool RetainedLayout::BeginMoveSizing(std::string_view name, int mode, float cursor_x,
                                     float cursor_y, MoveSizingSession* session) {
  if (session == nullptr || session->active) return false;
  SolveIfDirty();
  auto* frame = impl_->frames.FindFrame(name);
  const auto ref = impl_->frames.FindLuaRef(name);
  const auto* rect = FindRect(name);
  if (frame == nullptr || !ref.has_value() || rect == nullptr || impl_->lua == nullptr) return false;
  *session = {.frame_name = std::string(name), .mode = mode, .cursor_x = cursor_x,
              .cursor_y = cursor_y, .active = true};
  const int top = lua_gettop(impl_->lua);
  lua_rawgeti(impl_->lua, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(impl_->lua, -1) != 0) {
    const int index = lua_absindex(impl_->lua, -1);
    session->relative_name = RelativeName(*frame, ReadAnchors(impl_->lua, index));
    lua_pushboolean(impl_->lua, 1); lua_setfield(impl_->lua, index, "__ow_drag_active");
    lua_pushinteger(impl_->lua, mode); lua_setfield(impl_->lua, index, "__ow_drag_mode");
    openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_drag_left", rect->x);
    openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_drag_top", rect->y);
    openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_drag_right", rect->x + rect->width);
    openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_drag_bottom", rect->y + rect->height);
    lua_pushboolean(impl_->lua, 1); lua_setfield(impl_->lua, index, "__ow_user_placed");
    frame->top_level = ReadBoolean(impl_->lua, index, "__ow_toplevel");
    frame->toplevel = frame->top_level;
  }
  lua_settop(impl_->lua, top);
  impl_->frames.SetFrameUserPlaced(name, true);
  if (frame->top_level || frame->toplevel) {
    int highest = frame->frame_level;
    for (const auto& view : impl_->frames.CollectStableFrames()) {
      if (view.key == name || view.frame->frame_strata != frame->frame_strata) continue;
      const auto other = impl_->rects.find(view.key);
      if (other != impl_->rects.end() && RectanglesOverlap(*rect, other->second))
        highest = std::max(highest, view.frame->frame_level + 1);
    }
    frame->frame_level = highest;
    const int stack_top = lua_gettop(impl_->lua);
    lua_rawgeti(impl_->lua, LUA_REGISTRYINDEX, *ref);
    if (lua_istable(impl_->lua, -1) != 0) {
      lua_pushinteger(impl_->lua, highest);
      lua_setfield(impl_->lua, -2, "__ow_frame_level");
    }
    lua_settop(impl_->lua, stack_top);
    if (impl_->ports.order_invalidated) impl_->ports.order_invalidated();
  }
  return true;
}

bool RetainedLayout::UpdateMoveSizing(MoveSizingSession* session, float x, float y) {
  if (session == nullptr || !session->active || impl_->lua == nullptr) return false;
  auto* frame = impl_->frames.FindFrame(session->frame_name);
  const auto ref = impl_->frames.FindLuaRef(session->frame_name);
  const auto* current = FindRect(session->frame_name);
  if (frame == nullptr || !ref.has_value() || current == nullptr) { *session = {}; return false; }
  const float dx = x - session->cursor_x, dy = y - session->cursor_y;
  if (dx == 0.0F && dy == 0.0F) return true;
  float left = current->x, top_edge = current->y;
  float right = left + current->width, bottom = top_edge + current->height;
  switch (session->mode) {
    case 0: left += dx; top_edge += dy; break; case 1: top_edge += dy; break;
    case 2: right += dx; top_edge += dy; break; case 3: left += dx; break;
    case 4: left += dx; right += dx; top_edge += dy; bottom += dy; break;
    case 5: right += dx; break; case 6: left += dx; bottom += dy; break;
    case 7: bottom += dy; break; case 8: right += dx; bottom += dy; break;
    default: left += dx; right += dx; top_edge += dy; bottom += dy; break;
  }
  const float scale = EffectiveScale(*frame, impl_->frames, impl_->height, impl_->root_scale);
  const int top = lua_gettop(impl_->lua);
  lua_rawgeti(impl_->lua, LUA_REGISTRYINDEX, *ref);
  const int index = lua_absindex(impl_->lua, -1);
  if (lua_istable(impl_->lua, index) != 0 && session->mode == 4 &&
      ApplyAnchorDelta(impl_->lua, index, dx, dy, scale)) {
    lua_settop(impl_->lua, top);
    session->cursor_x = x; session->cursor_y = y;
    QueueLuaMutation(session->frame_name);
    SolveIfDirty();
    if (const auto* solved = FindRect(session->frame_name)) {
      const int drag_top = lua_gettop(impl_->lua);
      lua_rawgeti(impl_->lua, LUA_REGISTRYINDEX, *ref);
      if (lua_istable(impl_->lua, -1) != 0) {
        openwow::ui::WriteLuaNumberField(impl_->lua, -1, "__ow_drag_left", solved->x);
        openwow::ui::WriteLuaNumberField(impl_->lua, -1, "__ow_drag_top", solved->y);
        openwow::ui::WriteLuaNumberField(impl_->lua, -1, "__ow_drag_right", solved->x + solved->width);
        openwow::ui::WriteLuaNumberField(impl_->lua, -1, "__ow_drag_bottom", solved->y + solved->height);
      }
      lua_settop(impl_->lua, drag_top);
    }
    return true;
  }
  const auto adjust_width = [&](float target) {
    if (session->mode == 0 || session->mode == 3 || session->mode == 6) left = right - target;
    else if (session->mode != 4) right = left + target;
  };
  const auto adjust_height = [&](float target) {
    if (session->mode == 0 || session->mode == 1 || session->mode == 2) top_edge = bottom - target;
    else if (session->mode != 4) bottom = top_edge + target;
  };
  if (lua_istable(impl_->lua, index) != 0) {
    double bound = 0.0;
    if (ReadNumber(impl_->lua, index, "__ow_min_w", &bound) &&
        right - left < openwow::ui::anim::StoredAnimationOffsetToPixels(bound) * scale)
      adjust_width(openwow::ui::anim::StoredAnimationOffsetToPixels(bound) * scale);
    if (ReadNumber(impl_->lua, index, "__ow_min_h", &bound) &&
        bottom - top_edge < openwow::ui::anim::StoredAnimationOffsetToPixels(bound) * scale)
      adjust_height(openwow::ui::anim::StoredAnimationOffsetToPixels(bound) * scale);
    if (ReadNumber(impl_->lua, index, "__ow_max_w", &bound) && bound > 0.0 &&
        right - left > openwow::ui::anim::StoredAnimationOffsetToPixels(bound) * scale)
      adjust_width(openwow::ui::anim::StoredAnimationOffsetToPixels(bound) * scale);
    if (ReadNumber(impl_->lua, index, "__ow_max_h", &bound) && bound > 0.0 &&
        bottom - top_edge > openwow::ui::anim::StoredAnimationOffsetToPixels(bound) * scale)
      adjust_height(openwow::ui::anim::StoredAnimationOffsetToPixels(bound) * scale);
  }
  if (frame->clamped_to_screen.value_or(false)) {
    openwow::ui::RectEdgesYDown edges{
        .left = left, .top = top_edge, .right = right, .bottom = bottom};
    openwow::ui::ClampRectEdgesYDownPreservingSpan(
        &edges, openwow::ui::RectBoundsYDown{
                    .min_left = -frame->clamp_rect_inset_left * scale,
                    .min_top = frame->clamp_rect_inset_top * scale,
                    .max_right = impl_->width - frame->clamp_rect_inset_right * scale,
                    .max_bottom = impl_->height + frame->clamp_rect_inset_bottom * scale});
    left = edges.left; top_edge = edges.top; right = edges.right; bottom = edges.bottom;
  }
  const auto relative = ReferenceRect(impl_->rects, impl_->width, impl_->height, session->relative_name);
  if (lua_istable(impl_->lua, -1) != 0) {
    SetAnchor(impl_->lua, index, "TOPLEFT", session->relative_name,
              (left - relative.x) / scale, (relative.y - top_edge) / scale);
    openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_width", (right - left) / scale);
    openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_height", (bottom - top_edge) / scale);
    openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_drag_left", left);
    openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_drag_top", top_edge);
    openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_drag_right", right);
    openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_drag_bottom", bottom);
  }
  lua_settop(impl_->lua, top);
  session->cursor_x = x; session->cursor_y = y;
  QueueLuaMutation(session->frame_name);
  SolveIfDirty();
  return true;
}

bool RetainedLayout::CommitMoveSizing(MoveSizingSession* session) {
  if (session == nullptr || !session->active || impl_->lua == nullptr) return false;
  const auto ref = impl_->frames.FindLuaRef(session->frame_name);
  if (!ref.has_value()) return false;
  RefreshTrackedLayout();
  const auto* frame = impl_->frames.FindFrame(session->frame_name);
  if (frame == nullptr) return false;
  bool committed = false;
  const int top = lua_gettop(impl_->lua);
  lua_rawgeti(impl_->lua, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(impl_->lua, -1) != 0) {
    const int index = lua_absindex(impl_->lua, -1);
    double left = 0, top_edge = 0, right = 0, bottom = 0;
    if (!ReadNumber(impl_->lua, index, "__ow_drag_left", &left) ||
        !ReadNumber(impl_->lua, index, "__ow_drag_top", &top_edge) ||
        !ReadNumber(impl_->lua, index, "__ow_drag_right", &right) ||
        !ReadNumber(impl_->lua, index, "__ow_drag_bottom", &bottom)) {
      if (const auto* rect = FindRect(session->frame_name)) {
        left = rect->x; top_edge = rect->y; right = rect->x + rect->width; bottom = rect->y + rect->height;
      }
    }
    const std::string relative_name = session->relative_name.empty()
        ? RelativeName(*frame, ReadAnchors(impl_->lua, index)) : session->relative_name;
    const auto relative = ReferenceRect(impl_->rects, impl_->width, impl_->height, relative_name);
    const auto placement = NearestPlacement(left, top_edge, right, bottom, relative);
    const float scale = EffectiveScale(*frame, impl_->frames, impl_->height, impl_->root_scale);
    const std::string point(LayoutCacheFramePointName(placement.point_index));
    SetAnchor(impl_->lua, index, point, relative_name,
              placement.pixel_offset_x / scale, placement.pixel_offset_y / scale);
    openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_width", (right - left) / scale);
    openwow::ui::WriteLuaNumberField(impl_->lua, index, "__ow_height", (bottom - top_edge) / scale);
    for (const char* field : {"__ow_drag_active", "__ow_drag_mode", "__ow_drag_left",
                              "__ow_drag_top", "__ow_drag_right", "__ow_drag_bottom"})
      ClearField(impl_->lua, index, field);
    committed = true;
  }
  lua_settop(impl_->lua, top);
  const std::string name = session->frame_name;
  *session = {};
  if (committed) { QueueLuaMutation(name); SolveIfDirty(); }
  return committed;
}

std::optional<openwow::ui::framexml::FrameRect> RetainedLayout::ResolveAnonymousRegionRect(
    lua_State* lua, int index, std::string_view parent_name,
    std::optional<openwow::ui::framexml::FrameRect> parent_override) const {
  if (lua == nullptr) return std::nullopt;
  openwow::ui::framexml::UiFrame region;
  region.parent = std::string(parent_name);
  region.set_all_points = ReadBoolean(lua, index, "__ow_setAllPoints");
  region.anchors = ReadAnchors(lua, index);
  double value = 0.0;
  if (ReadNumber(lua, index, "__ow_width", &value)) region.width = static_cast<float>(value);
  if (ReadNumber(lua, index, "__ow_height", &value)) region.height = static_cast<float>(value);
  const auto region_type =
      ReadInternedStringField(lua, index, "__ow_type");
  if (region_type.has_value() && *region_type == "FontString") {

    if ((!region.width.has_value() || *region.width == 0.0F) ||
        (!region.height.has_value() || *region.height == 0.0F)) {
      if (const auto measured =
              frame_api::MeasureLuaFontStringMetrics(lua, index);
          measured.has_value()) {
        if (!region.width.has_value() || *region.width == 0.0F) {
          region.width = openwow::ui::ResolveFontStringEffectiveScriptDimension(
              0.0F, measured->width);
        }
        if (!region.height.has_value() || *region.height == 0.0F) {
          region.height =
              openwow::ui::ResolveFontStringEffectiveScriptDimension(
                  0.0F, measured->height);
        }
      }
    }
  } else if (region_type.has_value() && *region_type == "Texture") {

    if ((!region.width.has_value() || *region.width == 0.0F) ||
        (!region.height.has_value() || *region.height == 0.0F)) {
      const openwow::ui::game::runtime::TextureHandleState handle =
          openwow::ui::game::runtime::ResolveTextureHandleState(lua, index, nullptr);
      const openwow::ui::TextureNaturalSize natural =
          openwow::ui::ResolveTextureNaturalSize(
              impl_->texture_natural_size, *handle.texture_path, handle.solid_colour);
      if ((!region.width.has_value() || *region.width == 0.0F) && natural.width.has_value())
        region.width = natural.width;
      if ((!region.height.has_value() || *region.height == 0.0F) && natural.height.has_value())
        region.height = natural.height;
    }
  }
  if (ReadNumber(lua, index, "__ow_layout_offset_x", &value))
    region.layout_offset_x = static_cast<float>(value);
  if (ReadNumber(lua, index, "__ow_layout_offset_y", &value))
    region.layout_offset_y = static_cast<float>(value);
  openwow::ui::TransparentStringMap<RectDdc> rects;
  rects.reserve(impl_->rects.size() + 2U);
  rects.emplace("UIParent", RectDdc{0.0F, 0.0F, impl_->width, impl_->height});
  for (const auto& [name, rect] : impl_->rects) {
    const float max_y = impl_->height - rect.y;
    rects.insert_or_assign(name, RectDdc{static_cast<float>(rect.x),
        max_y - rect.height, static_cast<float>(rect.x + rect.width), max_y});
  }
  if (parent_override.has_value()) {
    const auto& parent = *parent_override;
    const float max_y = impl_->height - parent.y;
    rects.insert_or_assign(std::string(parent_name),
        RectDdc{static_cast<float>(parent.x), max_y - parent.height,
                static_cast<float>(parent.x + parent.width), max_y});
  }
  float scale = impl_->height / kUiVirtualHeight * impl_->root_scale;
  if (const auto* parent = impl_->frames.FindFrame(parent_name))
    scale = EffectiveScale(*parent, impl_->frames, impl_->height, impl_->root_scale);
  const auto resolved = ResolveRegionDdc(region, rects, scale);
  if (!resolved.has_value()) return std::nullopt;

  const int x0 = static_cast<int>(std::lround(resolved->min_x));
  const int x1 = static_cast<int>(std::lround(resolved->max_x));
  const int y0 = static_cast<int>(std::lround(impl_->height - resolved->max_y));
  const int y1 = static_cast<int>(std::lround(impl_->height - resolved->min_y));
  return openwow::ui::framexml::FrameRect{
      .x = x0,
      .y = y0,
      .width = x1 - x0,
      .height = y1 - y0};
}

}

#undef OW_LAYOUT_KEY

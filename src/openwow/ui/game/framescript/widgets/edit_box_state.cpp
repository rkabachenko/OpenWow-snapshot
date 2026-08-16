#include "openwow/ui/game/framescript/widgets/edit_box_state.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/frame_traversal_index.h"
#include "openwow/ui/game/runtime/retained_layout.h"

#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/font_layout.h"
#include "openwow/ui/font_string_layout.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/foundation/text/utf8.h"

#include <lua.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace openwow::ui::game {
namespace {

constexpr const char *kTextDirty = "__ow_eb_text_dirty";
constexpr const char *kUserInputDirty = "__ow_eb_user_input_dirty";
constexpr const char *kCursorDirty = "__ow_eb_cursor_dirty";

constexpr const char *kBlinkAccumulator = "__ow_eb_blink_accum";

constexpr const char *kBlinkSpeed = "__ow_eb_blink";
constexpr float kRetailEditBoxDefaultBlinkSeconds = 0.5F;

constexpr float kRetailFloatCompareEpsilon = 2.3841858e-07F;

constexpr std::string_view kRetailEditBoxCaretDrawLayer = "OVERLAY";
constexpr std::string_view kRetailEditBoxHighlightDrawLayer = "ARTWORK";

std::optional<float> ReadNumber(lua_State *state, int table,
                                const char *field) {
  table = lua_absindex(state, table);
  lua_getfield(state, table, field);
  std::optional<float> result;
  if (lua_isnumber(state, -1) != 0) {
    result = static_cast<float>(lua_tonumber(state, -1));
  }
  lua_pop(state, 1);
  return result;
}

std::optional<std::int32_t> ReadInteger(lua_State *state, int table,
                                        const char *field) {
  table = lua_absindex(state, table);
  lua_getfield(state, table, field);
  std::optional<std::int32_t> result;
  if (lua_isnumber(state, -1) != 0) {
    result = static_cast<std::int32_t>(lua_tointeger(state, -1));
  }
  lua_pop(state, 1);
  return result;
}

bool ReadBoolean(lua_State *state, int table, const char *field,
                 const bool fallback = false) {
  table = lua_absindex(state, table);
  lua_getfield(state, table, field);
  const bool result = lua_isnil(state, -1) != 0
                          ? fallback
                          : lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  return result;
}

std::string ReadString(lua_State *state, int table, const char *field) {
  table = lua_absindex(state, table);
  lua_getfield(state, table, field);
  const char *value = lua_tostring(state, -1);
  std::string result = value != nullptr ? value : "";
  lua_pop(state, 1);
  return result;
}

bool ReadAndClearBoolean(lua_State *state, const int table,
                         const char *field) {
  const bool value = ReadBoolean(state, table, field);
  lua_pushnil(state);
  lua_setfield(state, lua_absindex(state, table), field);
  return value;
}

void SetTrue(lua_State *state, const int table, const char *field,
             const bool value) {
  if (value) {
    lua_pushboolean(state, 1);
    lua_setfield(state, lua_absindex(state, table), field);
  }
}

bool IsAbsolutePath(const std::string &path) {
  return std::filesystem::path(path).is_absolute() ||
         (path.size() > 2u &&
          std::isalpha(static_cast<unsigned char>(path[0])) != 0 &&
          path[1] == ':');
}

void Invoke(lua_State *state, const int edit_box, const char *handler,
            const int argument_count) {
  const auto invocation = InvokeFrameScriptHandler(
      state, lua_absindex(state, edit_box), handler, argument_count);
  if (invocation.status != LUA_OK) {
    lua_pop(state, 1);
  }
}

struct FontContext {
  std::string path;
  int height{0};
  float scale{1.0F};
  float width{0.0F};
  float spacing{0.0F};
  float line_height_override{0.0F};
  openwow::render::text::WrapMode wrap{
      openwow::render::text::WrapMode::None};
  bool indented_wrap{false};
};

FontContext ReadFontContext(lua_State *state, const int font_string) {
  FontContext context;
  context.path = ReadString(state, font_string, "__ow_font_path");
  context.height = static_cast<int>(
      ReadNumber(state, font_string, "__ow_font_size").value_or(0.0F));

  context.scale =
      frame_api::ResolveLuaFontStringRasterScale(state, font_string);
  context.width = ReadNumber(state, font_string, "__ow_width").value_or(0.0F);
  context.spacing = openwow::ui::StoredUiHorizontalCoordinateToPixels(
      ReadNumber(state, font_string, "__ow_spacing").value_or(0.0F));
  context.line_height_override =
      openwow::ui::StoredUiHorizontalCoordinateToPixels(
          ReadNumber(state, font_string, "__ow_text_height").value_or(0.0F));
  context.wrap = openwow::render::text::ResolveWrapMode(
      ReadBoolean(state, font_string, "__ow_wordwrap", true),
      ReadBoolean(state, font_string, "__ow_nonspacewrap"));
  context.indented_wrap =
      ReadBoolean(state, font_string, "__ow_indented_wrap");
  return context;
}

const openwow::vfs::VirtualFileSystem *MeasurementVfs(lua_State *state,
                                                      const FontContext &font) {
  if (const auto *manager = runtime::WorldUiRuntimeContext::FromLua(state);
      manager != nullptr && manager->vfs() != nullptr &&
      !IsAbsolutePath(font.path)) {
    return manager->vfs();
  }
  return nullptr;
}

std::optional<openwow::render::text::TextLayout> LayoutDisplayText(
    lua_State *state, const FontContext &font, const std::string_view text) {
  if (font.path.empty() || font.height <= 0 || text.empty()) {
    return std::nullopt;
  }
  openwow::render::text::TextLayoutRequest request;
  request.maximum_width = font.width;
  request.line_spacing = font.spacing;
  request.line_height = font.line_height_override;
  request.wrap = font.wrap;
  request.indent_continuation_lines = font.indented_wrap;
  return openwow::ui::LayoutFontText(MeasurementVfs(state, font), font.path,
                                     font.height, text, request, font.scale);
}

float MeasureFragment(lua_State *state, const FontContext &font,
                      const std::string_view text) {
  if (font.path.empty() || font.height <= 0 || text.empty()) {
    return 0.0F;
  }
  openwow::render::text::TextLayoutRequest request;
  request.line_spacing = font.spacing;
  request.line_height = font.line_height_override;
  const auto layout = openwow::ui::LayoutFontText(
      MeasurementVfs(state, font), font.path, font.height, text, request,
      font.scale);
  return layout ? layout->width : 0.0F;
}

enum class HorizontalJustify : std::uint8_t { kNone, kLeft, kCenter, kRight };

HorizontalJustify ReadHorizontalJustify(lua_State *state,
                                        const int font_string) {
  const std::string justify = ReadString(state, font_string, "__ow_justifyH");
  if (openwow::text::EqualsIgnoreCaseAscii(justify, "CENTER")) {
    return HorizontalJustify::kCenter;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(justify, "RIGHT")) {
    return HorizontalJustify::kRight;
  }

  return HorizontalJustify::kLeft;
}

struct EditBoxTextContext {
  FontContext font;

  std::string display_text;
  std::string real_text;
  bool password{false};
  bool multiline{false};
  HorizontalJustify justify{HorizontalJustify::kLeft};

  std::vector<std::uint32_t> line_starts;

  float line_height{1.0F};

  float line_advance{1.0F};

  float rendered_width{0.0F};
};

std::size_t MapToDisplayIndex(const EditBoxTextContext &context,
                              const std::size_t real_index) {
  if (!context.password) {
    return std::min(real_index, context.display_text.size());
  }
  const auto prefix = std::string_view(context.real_text)
                          .substr(0, std::min(real_index,
                                              context.real_text.size()));
  return std::min<std::size_t>(
      static_cast<std::size_t>(std::max(0, openwow::text::Utf8CodepointCount(prefix))),
      context.display_text.size());
}

std::size_t MapToRealIndex(const EditBoxTextContext &context,
                           const std::size_t display_index) {
  if (!context.password) {
    return std::min(display_index, context.real_text.size());
  }
  const std::string prefix = openwow::text::Utf8TakeCodepoints(
      context.real_text, static_cast<int>(display_index));
  return prefix.size();
}

EditBoxTextContext ReadEditBoxTextContext(lua_State *state, const int edit_box,
                                          const int font_string) {
  EditBoxTextContext context;
  context.font = ReadFontContext(state, font_string);
  context.real_text = ReadString(state, edit_box, "__ow_eb_text");
  context.display_text = ReadString(state, font_string, "__ow_text");
  context.password = ReadBoolean(state, edit_box, "__ow_eb_password");
  context.multiline = ReadBoolean(state, edit_box, "__ow_eb_multiline");
  context.justify = ReadHorizontalJustify(state, font_string);

  const auto layout =
      LayoutDisplayText(state, context.font, context.display_text);
  if (layout && !layout->lines.empty()) {
    context.line_starts.reserve(layout->lines.size() + 1u);
    for (const auto &line : layout->lines) {
      context.line_starts.push_back(static_cast<std::uint32_t>(line.begin));
    }

    context.line_starts.push_back(
        static_cast<std::uint32_t>(context.display_text.size()));

    context.line_advance = std::max(1.0F, layout->lines.front().height);
    context.line_height =
        std::max(1.0F, context.line_advance - context.font.spacing);
    context.rendered_width = layout->width;
    return context;
  }

  context.line_starts = {0u, 0u};
  const auto probe = LayoutDisplayText(state, context.font, "A");
  if (probe && !probe->lines.empty()) {
    context.line_advance = std::max(1.0F, probe->lines.front().height);
    context.line_height =
        std::max(1.0F, context.line_advance - context.font.spacing);
    return context;
  }
  context.line_height = std::max(1.0F, static_cast<float>(context.font.height));
  context.line_advance = context.line_height + context.font.spacing;
  return context;
}

std::size_t DisplayLineCount(const EditBoxTextContext &context) {
  return context.line_starts.size() >= 2u ? context.line_starts.size() - 1u
                                          : 1u;
}

std::size_t ResolveLineIndex(const EditBoxTextContext &context,
                             const std::size_t position) {
  const std::size_t lines = DisplayLineCount(context);
  std::size_t index = 0;
  for (std::size_t candidate = 1; candidate < lines; ++candidate) {
    if (position < context.line_starts[candidate]) break;
    index = candidate;
  }
  return index;
}

struct RegionPlacement {
  std::string_view point;
  std::string_view relative_point;
  float x{0.0F};
  float y{0.0F};
  float width{0.0F};
  float height{0.0F};
  bool visible{false};
};

void ApplyJustifyAnchor(const EditBoxTextContext &context,
                        RegionPlacement *placement) {
  placement->point = context.multiline ? "TOPLEFT" : "LEFT";
  switch (context.justify) {
    case HorizontalJustify::kCenter:
      placement->relative_point = context.multiline ? "TOP" : "CENTER";
      break;
    case HorizontalJustify::kRight:
      placement->relative_point = context.multiline ? "TOPRIGHT" : "RIGHT";
      break;
    case HorizontalJustify::kLeft:
    case HorizontalJustify::kNone:
    default:
      placement->relative_point = context.multiline ? "TOPLEFT" : "LEFT";
      break;
  }
}

std::string_view LineSlice(const EditBoxTextContext &context,
                           const std::size_t begin, const std::size_t end) {
  const std::size_t size = context.display_text.size();
  const std::size_t from = std::min(begin, size);
  const std::size_t to = std::clamp(end, from, size);
  return std::string_view(context.display_text).substr(from, to - from);
}

RegionPlacement PlaceCaret(lua_State *state, const EditBoxTextContext &context,
                           const std::size_t display_cursor) {
  RegionPlacement placement;
  placement.width = openwow::ui::kRetailEditBoxCaretWidthUiUnits;
  placement.height = context.line_height;
  placement.visible = true;
  ApplyJustifyAnchor(context, &placement);

  const std::size_t line = ResolveLineIndex(context, display_cursor);
  const std::size_t line_start = context.line_starts[line];
  const std::size_t line_end = context.line_starts[line + 1u];
  placement.y = -static_cast<float>(line) * context.line_advance;

  switch (context.justify) {
    case HorizontalJustify::kCenter: {
      const float prefix = MeasureFragment(
          state, context.font, LineSlice(context, line_start, display_cursor));
      const float line_width = MeasureFragment(
          state, context.font, LineSlice(context, line_start, line_end));
      placement.x = prefix - line_width * 0.5F;
      break;
    }
    case HorizontalJustify::kRight:
      placement.x = -MeasureFragment(
          state, context.font, LineSlice(context, display_cursor, line_end));
      break;
    case HorizontalJustify::kLeft:
    case HorizontalJustify::kNone:
    default:
      placement.x = MeasureFragment(
          state, context.font, LineSlice(context, line_start, display_cursor));
      break;
  }
  return placement;
}

RegionPlacement PlaceSelectionSpan(lua_State *state,
                                   const EditBoxTextContext &context,
                                   std::size_t begin, std::size_t end) {
  RegionPlacement placement;
  placement.height = context.line_height;
  ApplyJustifyAnchor(context, &placement);

  const std::size_t line = ResolveLineIndex(context, begin);
  const std::size_t line_start = context.line_starts[line];
  const std::size_t line_end = context.line_starts[line + 1u];
  placement.y = -static_cast<float>(line) * context.line_advance;
  begin = std::max(begin, line_start);
  end = std::min(end, line_end);

  placement.width = (line + 1u < DisplayLineCount(context) && end == line_end)
                        ? context.rendered_width
                        : MeasureFragment(state, context.font,
                                          LineSlice(context, begin, end));

  switch (context.justify) {
    case HorizontalJustify::kCenter: {
      const float prefix = MeasureFragment(
          state, context.font, LineSlice(context, line_start, begin));
      const float line_width = MeasureFragment(
          state, context.font, LineSlice(context, line_start, line_end));
      placement.x = prefix - line_width * 0.5F;
      break;
    }
    case HorizontalJustify::kRight:
      placement.x = -MeasureFragment(state, context.font,
                                     LineSlice(context, begin, line_end));
      break;
    case HorizontalJustify::kLeft:
    case HorizontalJustify::kNone:
    default:
      placement.x = MeasureFragment(state, context.font,
                                    LineSlice(context, line_start, begin));
      break;
  }

  placement.visible =
      begin < end && std::fabs(placement.width) >= kRetailFloatCompareEpsilon;
  return placement;
}

float FiniteFloat(const double value) {
  return std::isfinite(value) ? static_cast<float>(value) : 0.0F;
}

std::string EditBoxTextRegionKey(const std::string_view edit_box_key) {
  return std::string(edit_box_key) + ".__EditBoxText";
}

void ApplyRegionPlacement(const EditBoxRegionPorts &ports,
                          const std::string &region_key,
                          const std::string &text_region_key,
                          const RegionPlacement &placement) {
  auto *const region = ports.frames->FindFrame(region_key);
  if (region == nullptr) return;
  const bool was_visible = region->visible;
  region->visible = placement.visible;
  if (placement.visible) {
    region->set_all_points = false;
    region->anchors = {openwow::ui::framexml::UiAnchor{
        .point = std::string(placement.point),
        .relative_to = text_region_key,
        .relative_point = std::string(placement.relative_point),
        .x = FiniteFloat(placement.x),
        .y = FiniteFloat(placement.y),
    }};
    region->width = std::max(0.0F, FiniteFloat(placement.width));
    region->height = std::max(0.0F, FiniteFloat(placement.height));
    if (ports.layout != nullptr) ports.layout->PublishFrame(region_key);
  }
  if (was_visible != region->visible && ports.traversal != nullptr) {
    ports.traversal->InvalidateFrameOrder(
        region_key,
        runtime::FrameTraversalIndex::OrderInvalidationKind::kInherited);
  }
}

void HideRegion(const EditBoxRegionPorts &ports,
                const std::string &region_key) {
  auto *const region = ports.frames->FindFrame(region_key);
  if (region == nullptr || !region->visible) return;
  region->visible = false;
  if (ports.traversal != nullptr) {
    ports.traversal->InvalidateFrameOrder(
        region_key,
        runtime::FrameTraversalIndex::OrderInvalidationKind::kInherited);
  }
}

void PlaceSelectionRegions(lua_State *state, const EditBoxRegionPorts &ports,
                           const std::string_view edit_box_key,
                           const std::string &text_region_key,
                           const EditBoxTextContext &context,
                           const std::size_t begin, const std::size_t end) {
  const std::array<std::string, kEditBoxHighlightRegionCount> keys{
      EditBoxHighlightRegionKey(edit_box_key, 0),
      EditBoxHighlightRegionKey(edit_box_key, 1),
      EditBoxHighlightRegionKey(edit_box_key, 2),
  };
  if (begin >= end) {
    for (const auto &key : keys) HideRegion(ports, key);
    return;
  }
  const std::size_t first_line = ResolveLineIndex(context, begin);
  const std::size_t last_line = ResolveLineIndex(context, end);
  if (first_line == last_line) {
    ApplyRegionPlacement(ports, keys[0], text_region_key,
                         PlaceSelectionSpan(state, context, begin, end));
    HideRegion(ports, keys[1]);
    HideRegion(ports, keys[2]);
    return;
  }
  const std::size_t first_line_end = context.line_starts[first_line + 1u];
  ApplyRegionPlacement(
      ports, keys[0], text_region_key,
      PlaceSelectionSpan(state, context, begin, first_line_end));
  if (last_line - first_line == 1u) {
    HideRegion(ports, keys[1]);
  } else {

    auto *const middle = ports.frames->FindFrame(keys[1]);
    if (middle != nullptr) {
      const bool was_visible = middle->visible;
      middle->set_all_points = false;
      middle->width.reset();
      middle->height.reset();
      middle->anchors = {
          openwow::ui::framexml::UiAnchor{.point = "TOPRIGHT",
                                          .relative_to = keys[0],
                                          .relative_point = "BOTTOMRIGHT"},
          openwow::ui::framexml::UiAnchor{.point = "BOTTOMLEFT",
                                          .relative_to = keys[2],
                                          .relative_point = "TOPLEFT"},
      };
      middle->visible = true;
      if (ports.layout != nullptr) ports.layout->PublishFrame(keys[1]);
      if (!was_visible && ports.traversal != nullptr) {
        ports.traversal->InvalidateFrameOrder(
            keys[1],
            runtime::FrameTraversalIndex::OrderInvalidationKind::kInherited);
      }
    }
  }
  ApplyRegionPlacement(
      ports, keys[2], text_region_key,
      PlaceSelectionSpan(state, context, context.line_starts[last_line], end));
}

struct EditBoxPumpInputs {
  EditBoxTextContext context;
  std::size_t display_cursor{0};
  std::size_t display_selection_begin{0};
  std::size_t display_selection_end{0};
  bool resolved{false};
};

EditBoxPumpInputs ReadEditBoxPumpInputs(lua_State *state,
                                        const int edit_box_index) {
  EditBoxPumpInputs inputs;
  lua_getfield(state, edit_box_index, "__ow_eb_fontstr");
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    return inputs;
  }
  const int font_string = lua_absindex(state, -1);
  inputs.context = ReadEditBoxTextContext(state, edit_box_index, font_string);
  const auto clamp_real = [&](const char *field, const std::int32_t fallback) {
    return static_cast<std::size_t>(openwow::text::ClampUtf8ByteIndex(
        inputs.context.real_text,
        ReadInteger(state, edit_box_index, field).value_or(fallback)));
  };
  const std::size_t cursor = clamp_real("__ow_eb_cursor", 0);
  std::size_t selection_begin = clamp_real(
      "__ow_eb_hl_start", static_cast<std::int32_t>(cursor));
  std::size_t selection_end =
      clamp_real("__ow_eb_hl_end", static_cast<std::int32_t>(cursor));
  if (selection_begin > selection_end) std::swap(selection_begin, selection_end);
  inputs.display_cursor = MapToDisplayIndex(inputs.context, cursor);
  inputs.display_selection_begin =
      MapToDisplayIndex(inputs.context, selection_begin);
  inputs.display_selection_end =
      MapToDisplayIndex(inputs.context, selection_end);
  inputs.resolved = true;
  lua_pop(state, 1);
  return inputs;
}

}

void QueueEditBoxDirtyState(lua_State *state, int edit_box_index,
                            const bool text_changed, const bool user_input,
                            const bool cursor_changed) {
  if (state == nullptr || lua_istable(state, edit_box_index) == 0) {
    return;
  }
  edit_box_index = lua_absindex(state, edit_box_index);
  SetTrue(state, edit_box_index, kTextDirty, text_changed);
  SetTrue(state, edit_box_index, kUserInputDirty,
          text_changed && user_input);
  SetTrue(state, edit_box_index, kCursorDirty, cursor_changed);
  lua_getfield(state, edit_box_index, "__ow_ref");
  const int ref = lua_isinteger(state, -1) != 0
                      ? static_cast<int>(lua_tointeger(state, -1))
                      : LUA_NOREF;
  lua_pop(state, 1);
  if (auto *manager = runtime::WorldUiRuntimeContext::FromLua(state); manager != nullptr) {
    manager->input_router().QueueEditBoxStateUpdate(ref);
  }
}

std::string EditBoxCaretRegionKey(const std::string_view edit_box_key) {
  return std::string(edit_box_key) + ".__EditBoxCaret";
}

std::string EditBoxHighlightRegionKey(const std::string_view edit_box_key,
                                      const std::size_t index) {
  static constexpr std::array<std::string_view, kEditBoxHighlightRegionCount>
      kSuffixes{".__EditBoxHighlight1", ".__EditBoxHighlight2",
                ".__EditBoxHighlight3"};
  return std::string(edit_box_key) +
         std::string(kSuffixes[std::min(index,
                                        kEditBoxHighlightRegionCount - 1u)]);
}

void CreateEditBoxCaretRegions(const std::string_view edit_box_key,
                               runtime::FrameStore &frames,
                               runtime::RetainedLayout &layout) {
  using openwow::ui::framexml::UiFrame;
  const auto insert = [&](std::string key, const UiFrame::RegionRole role,
                          const std::string_view draw_layer,
                          const float channel) {
    if (frames.contains(key)) return;
    UiFrame region;
    region.kind = "Texture";
    region.runtime_kind = UiFrame::RuntimeKind::Texture;
    region.name = key;
    region.parent = std::string(edit_box_key);
    region.publish_to_lua = false;
    region.region_role = role;
    region.draw_layer = std::string(draw_layer);
    region.color_r = channel;
    region.color_g = channel;
    region.color_b = channel;
    region.color_a = 1.0F;

    region.has_vertex_color = true;

    region.visible = false;
    frames.InsertFrame(std::move(key), std::move(region));
  };
  insert(EditBoxCaretRegionKey(edit_box_key),
         UiFrame::RegionRole::EditBoxCaret, kRetailEditBoxCaretDrawLayer,
         openwow::ui::kRetailEditBoxCaretColorChannel);
  for (std::size_t slot = 0; slot < kEditBoxHighlightRegionCount; ++slot) {
    insert(EditBoxHighlightRegionKey(edit_box_key, slot),
           UiFrame::RegionRole::EditBoxHighlight,
           kRetailEditBoxHighlightDrawLayer, openwow::ui::kRetailEditBoxHighlightColorChannel);
  }
  layout.PublishFrame(EditBoxCaretRegionKey(edit_box_key));
  for (std::size_t slot = 0; slot < kEditBoxHighlightRegionCount; ++slot) {
    layout.PublishFrame(EditBoxHighlightRegionKey(edit_box_key, slot));
  }
}

void FlushEditBoxDirtyState(lua_State *state, int edit_box_index,
                            const std::string_view edit_box_key,
                            const EditBoxRegionPorts &ports) {
  if (state == nullptr || lua_istable(state, edit_box_index) == 0) {
    return;
  }
  edit_box_index = lua_absindex(state, edit_box_index);

  const bool cursor_changed =
      ReadAndClearBoolean(state, edit_box_index, kCursorDirty);
  const bool text_changed =
      ReadAndClearBoolean(state, edit_box_index, kTextDirty);
  const bool user_input =
      ReadAndClearBoolean(state, edit_box_index, kUserInputDirty);

  if (cursor_changed) {
    const auto inputs = ReadEditBoxPumpInputs(state, edit_box_index);
    const bool has_regions = ports.frames != nullptr && !edit_box_key.empty();
    RegionPlacement caret;
    if (inputs.resolved) {
      caret = PlaceCaret(state, inputs.context, inputs.display_cursor);
    } else {
      caret.width = openwow::ui::kRetailEditBoxCaretWidthUiUnits;
      caret.height = 1.0F;
    }
    if (has_regions) {
      const std::string text_region_key = EditBoxTextRegionKey(edit_box_key);
      if (!inputs.resolved || !ports.focused || !ports.application_active) {
        HideRegion(ports, EditBoxCaretRegionKey(edit_box_key));
      } else {
        ApplyRegionPlacement(ports, EditBoxCaretRegionKey(edit_box_key),
                             text_region_key, caret);

        lua_pushnumber(state, 0.0);
        lua_setfield(state, edit_box_index, kBlinkAccumulator);
      }
      if (inputs.resolved) {
        PlaceSelectionRegions(state, ports, edit_box_key, text_region_key,
                              inputs.context, inputs.display_selection_begin,
                              inputs.display_selection_end);
      } else {
        for (std::size_t slot = 0; slot < kEditBoxHighlightRegionCount;
             ++slot) {
          HideRegion(ports, EditBoxHighlightRegionKey(edit_box_key, slot));
        }
      }
    }
    lua_pushnumber(state, caret.x);
    lua_pushnumber(state, caret.y);
    lua_pushnumber(state, caret.width);
    lua_pushnumber(state, caret.height);
    Invoke(state, edit_box_index, "OnCursorChanged", 4);
  }
  if (text_changed) {
    lua_pushboolean(state, user_input ? 1 : 0);
    Invoke(state, edit_box_index, "OnTextChanged", 1);
  }
}

void UpdateEditBoxCaretBlink(lua_State *state, int edit_box_index,
                             const std::string_view edit_box_key,
                             const float dt, const EditBoxRegionPorts &ports) {
  if (state == nullptr || ports.frames == nullptr || edit_box_key.empty() ||
      lua_istable(state, edit_box_index) == 0) {
    return;
  }
  edit_box_index = lua_absindex(state, edit_box_index);

  if (!ports.application_active || !ports.focused) {
    HideRegion(ports, EditBoxCaretRegionKey(edit_box_key));
    return;
  }

  const float period =
      ReadNumber(state, edit_box_index, kBlinkSpeed)
          .value_or(kRetailEditBoxDefaultBlinkSeconds);
  if (!(period > 0.0F) || !std::isfinite(period)) return;

  const float accumulated =
      ReadNumber(state, edit_box_index, kBlinkAccumulator).value_or(0.0F) +
      (std::isfinite(dt) ? dt : 0.0F);

  if (accumulated <= period) {
    lua_pushnumber(state, static_cast<lua_Number>(accumulated));
    lua_setfield(state, edit_box_index, kBlinkAccumulator);
    return;
  }
  lua_pushnumber(state, 0.0);
  lua_setfield(state, edit_box_index, kBlinkAccumulator);

  const std::string caret_key = EditBoxCaretRegionKey(edit_box_key);
  auto *const caret = ports.frames->FindFrame(caret_key);
  if (caret == nullptr) return;

  if (!caret->visible && !caret->width.has_value()) return;
  caret->visible = !caret->visible;
  if (ports.traversal != nullptr) {
    ports.traversal->InvalidateFrameOrder(
        caret_key,
        runtime::FrameTraversalIndex::OrderInvalidationKind::kInherited);
  }
}

std::optional<int> ResolveEditBoxCharacterIndexAtPoint(
    lua_State *state, int edit_box_index, const std::string_view edit_box_key,
    const float device_x, const float device_y,
    runtime::RetainedLayout &layout) {
  if (state == nullptr || edit_box_key.empty() ||
      lua_istable(state, edit_box_index) == 0) {
    return std::nullopt;
  }
  edit_box_index = lua_absindex(state, edit_box_index);

  const auto *const rect = layout.FindRect(EditBoxTextRegionKey(edit_box_key));
  if (rect == nullptr || rect->width <= 0) return std::nullopt;

  const auto inputs = ReadEditBoxPumpInputs(state, edit_box_index);
  if (!inputs.resolved) return std::nullopt;
  const auto &context = inputs.context;
  const float scale = context.font.scale > 0.0F ? context.font.scale : 1.0F;

  const std::size_t lines = DisplayLineCount(context);
  std::size_t line = 0;
  if (context.multiline && lines > 1u && context.line_advance > 0.0F) {
    const float from_top =
        (device_y - static_cast<float>(rect->y)) / scale;
    const float steps = std::floor(from_top / context.line_advance);
    line = static_cast<std::size_t>(
        std::clamp(steps, 0.0F, static_cast<float>(lines - 1u)));
  }
  const std::size_t line_start = context.line_starts[line];
  const std::size_t line_end = context.line_starts[line + 1u];

  const float rect_width_units = static_cast<float>(rect->width) / scale;
  const float line_width =
      MeasureFragment(state, context.font, LineSlice(context, line_start, line_end));
  float target = (device_x - static_cast<float>(rect->x)) / scale;

  if (context.justify == HorizontalJustify::kCenter) {
    target -= std::max(0.0F, rect_width_units - line_width) * 0.5F;
  } else if (context.justify == HorizontalJustify::kRight) {
    target -= std::max(0.0F, rect_width_units - line_width);
  }
  if (target <= 0.0F) {
    return static_cast<int>(MapToRealIndex(context, line_start));
  }

  std::size_t index = line_start;
  while (index < line_end) {
    const std::size_t next = static_cast<std::size_t>(
        openwow::text::Utf8NextByteIndex(context.display_text,
                                         static_cast<int>(index)));
    if (next <= index) break;
    if (MeasureFragment(state, context.font,
                        LineSlice(context, line_start, next)) > target) {
      break;
    }
    index = next;
  }
  return static_cast<int>(MapToRealIndex(context, index));
}

void SynchronizeEditBoxRetainedTextInsets(
    lua_State *const state, const int edit_box_index,
    const std::string &edit_box_name,
    openwow::ui::framexml::UiFrame *const edit_box,
    runtime::FrameStore *const frames) {
  if (state == nullptr || edit_box == nullptr || frames == nullptr ||
      edit_box->runtime_kind !=
          openwow::ui::framexml::UiFrame::RuntimeKind::EditBox) {
    return;
  }
  const auto left = ReadNumber(state, edit_box_index, "__ow_eb_inset_l");
  const auto right = ReadNumber(state, edit_box_index, "__ow_eb_inset_r");
  const auto top = ReadNumber(state, edit_box_index, "__ow_eb_inset_t");
  const auto bottom = ReadNumber(state, edit_box_index, "__ow_eb_inset_b");
  if (!left && !right && !top && !bottom) {
    return;
  }
  edit_box->text_inset_left = FiniteFloat(left.value_or(0.0F));
  edit_box->text_inset_right = FiniteFloat(right.value_or(0.0F));
  edit_box->text_inset_top = FiniteFloat(top.value_or(0.0F));
  edit_box->text_inset_bottom = FiniteFloat(bottom.value_or(0.0F));
  edit_box->has_text_insets = true;

  auto *const found = frames->FindFrame(edit_box_name + ".__EditBoxText");
  if (found == nullptr) {
    return;
  }
  auto &text = *found;
  text.set_all_points = false;
  text.anchors = {
      openwow::ui::framexml::UiAnchor{
          .point = "TOPLEFT",
          .relative_to = edit_box_name,
          .relative_point = "TOPLEFT",
          .x = edit_box->text_inset_left,
          .y = -edit_box->text_inset_top,
      },
      openwow::ui::framexml::UiAnchor{
          .point = "BOTTOMRIGHT",
          .relative_to = edit_box_name,
          .relative_point = "BOTTOMRIGHT",
          .x = -edit_box->text_inset_right,
          .y = edit_box->text_inset_bottom,
      },
  };
}

}

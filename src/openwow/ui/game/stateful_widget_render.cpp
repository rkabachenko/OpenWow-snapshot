#include "openwow/ui/game/stateful_widget_render.h"

#include "openwow/foundation/text/ascii.h"
#include "openwow/ui/game/framescript/core/frame_alpha_lua.h"
#include "openwow/ui/game/runtime/lua_interned_field_key.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/widgets/cooldown_visuals.h"
#include "openwow/ui/widgets/status_bar.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace openwow::ui::game::stateful_widgets {
namespace {

class LuaStackGuard {
 public:
  explicit LuaStackGuard(lua_State* lua) noexcept
      : lua_(lua), top_(lua != nullptr ? lua_gettop(lua) : 0) {}

  ~LuaStackGuard() {
    if (lua_ != nullptr) {
      lua_settop(lua_, top_);
    }
  }

  LuaStackGuard(const LuaStackGuard&) = delete;
  LuaStackGuard& operator=(const LuaStackGuard&) = delete;

 private:
  lua_State* lua_;
  int top_;
};

[[nodiscard]] float Clamp01(const float value) noexcept {
  if (!std::isfinite(value)) {
    return 0.0f;
  }
  return std::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] float ReadNumber(lua_State* lua, const int table_index,
                               const char* field_name, const float fallback) {
  runtime::GetInternedLuaField(lua, table_index, field_name);
  const float value = lua_isnumber(lua, -1) != 0
                          ? static_cast<float>(lua_tonumber(lua, -1))
                          : fallback;
  lua_pop(lua, 1);
  return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] std::size_t ReadNonNegativeSize(lua_State* lua,
                                              const int table_index,
                                              const char* field_name,
                                              const std::size_t fallback) {
  runtime::GetInternedLuaField(lua, table_index, field_name);
  std::size_t value = fallback;
  if (lua_isnumber(lua, -1) != 0) {
    const lua_Number number = lua_tonumber(lua, -1);
    if (std::isfinite(static_cast<double>(number)) && number >= 0.0) {
      constexpr auto kMax =
          static_cast<lua_Number>(std::numeric_limits<std::size_t>::max());
      value = number >= kMax ? std::numeric_limits<std::size_t>::max()
                             : static_cast<std::size_t>(number);
    }
  }
  lua_pop(lua, 1);
  return value;
}

[[nodiscard]] std::int32_t ReadInt32(lua_State* lua, const int table_index,
                                     const char* field_name,
                                     const std::int32_t fallback) {
  runtime::GetInternedLuaField(lua, table_index, field_name);
  std::int32_t value = fallback;
  if (lua_isnumber(lua, -1) != 0) {
    const lua_Number number = lua_tonumber(lua, -1);
    if (std::isfinite(static_cast<double>(number))) {
      const auto clamped = std::clamp(
          static_cast<double>(number),
          static_cast<double>(std::numeric_limits<std::int32_t>::min()),
          static_cast<double>(std::numeric_limits<std::int32_t>::max()));
      value = static_cast<std::int32_t>(clamped);
    }
  }
  lua_pop(lua, 1);
  return value;
}

[[nodiscard]] std::uint32_t SecondsToCooldownTicks(
    const double seconds) noexcept {
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    return 0u;
  }
  constexpr double kMaximumTick =
      static_cast<double>(std::numeric_limits<std::uint32_t>::max());
  return static_cast<std::uint32_t>(std::min(seconds * 1000.0, kMaximumTick));
}

[[nodiscard]] bool ReadBoolean(lua_State* lua, const int table_index,
                               const char* field_name, const bool fallback) {
  runtime::GetInternedLuaField(lua, table_index, field_name);
  const bool value =
      lua_isnil(lua, -1) != 0 ? fallback : lua_toboolean(lua, -1) != 0;
  lua_pop(lua, 1);
  return value;
}

void ReadStringInto(lua_State* lua, const int table_index,
                    const char* field_name, std::string* out_value,
                    const std::string_view fallback = {}) {
  if (out_value == nullptr) {
    return;
  }
  runtime::GetInternedLuaField(lua, table_index, field_name);
  if (lua_type(lua, -1) == LUA_TSTRING) {
    std::size_t length = 0u;
    if (const char* text = lua_tolstring(lua, -1, &length); text != nullptr) {
      out_value->assign(text, length);
    } else {
      out_value->assign(fallback);
    }
  } else {
    out_value->assign(fallback);
  }
  lua_pop(lua, 1);
}

[[nodiscard]] std::string ReadString(lua_State* lua, const int table_index,
                                     const char* field_name,
                                     const std::string_view fallback = {}) {
  std::string value;
  ReadStringInto(lua, table_index, field_name, &value, fallback);
  return value;
}

[[nodiscard]] Orientation ReadOrientation(lua_State* lua, const int table_index,
                                          const char* field_name,
                                          const Orientation fallback) {
  const std::string token = ReadString(lua, table_index, field_name);
  if (openwow::text::EqualsIgnoreCaseAscii(token, "HORIZONTAL")) {
    return Orientation::kHorizontal;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(token, "VERTICAL")) {
    return Orientation::kVertical;
  }
  return fallback;
}

[[nodiscard]] float NormalizeRangeValue(const float minimum,
                                        const float maximum,
                                        const float value) noexcept {
  if (!std::isfinite(minimum) || !std::isfinite(maximum) ||
      !std::isfinite(value) || maximum <= minimum) {
    return 0.0f;
  }
  return Clamp01((std::clamp(value, minimum, maximum) - minimum) /
                 (maximum - minimum));
}

[[nodiscard]] double ReadDouble(lua_State* lua, const int table_index,
                                const char* field_name, const double fallback) {
  runtime::GetInternedLuaField(lua, table_index, field_name);
  const double value = lua_isnumber(lua, -1) != 0
                           ? static_cast<double>(lua_tonumber(lua, -1))
                           : fallback;
  lua_pop(lua, 1);
  return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] float ResolveMessageAlpha(const ScrollingMessageFrameState& state,
                                        const ScrollingMessage& message,
                                        const double now_seconds) noexcept {
  if (!message.enabled) {
    return 0.0f;
  }
  if (!state.fading || state.scroll_offset != 0u ||
      message.inserted_at_seconds <= 0.0 ||
      now_seconds <= message.inserted_at_seconds) {
    return Clamp01(message.color.a);
  }

  const double age = now_seconds - message.inserted_at_seconds;
  if (age <= static_cast<double>(state.time_visible)) {
    return Clamp01(message.color.a);
  }
  if (state.fade_duration <= 0.0f) {
    return 0.0f;
  }

  const double fade_progress = (age - static_cast<double>(state.time_visible)) /
                               static_cast<double>(state.fade_duration);
  return Clamp01(message.color.a * static_cast<float>(1.0 - fade_progress));
}

}

StatusBarRenderPlan BuildStatusBarRenderPlan(
    const openwow::ui::widgets::StatusBarSnapshot& status_bar,
    const Rect& frame_rect) {
  StatusBarRenderPlan plan;
  plan.normalized_value = NormalizeRangeValue(
      status_bar.minimum, status_bar.maximum, status_bar.value);

  plan.fill_rect = frame_rect;
  const float retained = plan.normalized_value;
  if (status_bar.orientation ==
      openwow::ui::widgets::StatusBarOrientation::Vertical) {
    plan.fill_rect.height = frame_rect.height * retained;
    plan.fill_rect.y =
        frame_rect.y + (frame_rect.height - plan.fill_rect.height);
  } else {
    plan.fill_rect.width = frame_rect.width * retained;
  }

  plan.visible = status_bar.has_texture && frame_rect.HasArea() &&
                 plan.fill_rect.HasArea();
  return plan;
}

SliderThumbRenderPlan BuildSliderThumbRenderPlan(const SliderState& state,
                                                 const Rect& track_rect,
                                                 const float thumb_width,
                                                 const float thumb_height) {
  SliderThumbRenderPlan plan;
  plan.normalized_value =
      NormalizeRangeValue(state.minimum, state.maximum, state.value);
  if (!track_rect.HasArea() || thumb_width <= 0.0f || thumb_height <= 0.0f ||
      !std::isfinite(thumb_width) || !std::isfinite(thumb_height)) {
    return plan;
  }

  plan.thumb_rect.width = thumb_width;
  plan.thumb_rect.height = thumb_height;
  if (state.orientation == Orientation::kVertical) {
    const float travel = std::max(0.0f, track_rect.height - thumb_height);
    plan.thumb_rect.x = track_rect.x + (track_rect.width - thumb_width) * 0.5f;
    plan.thumb_rect.y = track_rect.y + plan.normalized_value * travel;
  } else {
    const float travel = std::max(0.0f, track_rect.width - thumb_width);
    plan.thumb_rect.x = track_rect.x + plan.normalized_value * travel;
    plan.thumb_rect.y =
        track_rect.y + (track_rect.height - thumb_height) * 0.5f;
  }
  plan.visible = plan.thumb_rect.HasArea();
  return plan;
}

bool ReadSliderState(lua_State* lua, int frame_index, SliderState* out_state) {
  if (lua == nullptr || out_state == nullptr ||
      lua_istable(lua, frame_index) == 0) {
    return false;
  }

  LuaStackGuard stack(lua);
  frame_index = lua_absindex(lua, frame_index);
  SliderState state;
  state.minimum = ReadNumber(lua, frame_index, "__ow_sl_min", 0.0f);
  state.maximum = ReadNumber(lua, frame_index, "__ow_sl_max", 100.0f);
  state.value = ReadNumber(lua, frame_index, "__ow_sl_val", 0.0f);

  state.orientation = ReadOrientation(lua, frame_index, "__ow_sl_orient",
                                      Orientation::kVertical);
  *out_state = state;
  return true;
}

CooldownRenderPlan BuildCooldownRenderPlan(const CooldownState& state,
                                           const Rect& frame_rect,
                                           const std::uint32_t now_tick_ms) {
  CooldownRenderPlan plan;
  if (!state.enabled || !frame_rect.HasArea() || state.duration_ms == 0u) {
    return plan;
  }

  constexpr float kTau = 6.28318530717958647692f;
  constexpr float kSectorCount = 8.0f;
  constexpr std::uint32_t kReadyFlashDurationMs = 1000u;

  constexpr std::array<float, 7u> kReadyAlpha{0.0f, 0.5f,  1.0f, 0.75f,
                                              0.5f, 0.25f, 0.0f};
  constexpr std::array<float, 7u> kReadyRotation{
      0.0f,         -0.13090062f, -0.25918141f, -0.39269909f,
      -0.51836282f, -0.65145040f, -0.78539819f};
  constexpr std::array<float, 7u> kReadyUvScale{
      1.0f,         0.699300706f, 0.540540516f, 0.628930807f,
      0.751879692f, 0.625f,       0.869565248f};

  const std::uint32_t elapsed_ms = now_tick_ms - state.start_tick_ms;
  const double raw_progress =
      static_cast<double>(elapsed_ms) / static_cast<double>(state.duration_ms);
  plan.normalized_elapsed =
      static_cast<float>(std::clamp(raw_progress, 0.0, 1.0));
  plan.edge_rotation_radians = -plan.normalized_elapsed * kTau;

  if (raw_progress >= 1.0) {
    if (state.reverse) {

      plan.normalized_elapsed = static_cast<float>(state.duration_ms - 1u) /
                                static_cast<float>(state.duration_ms);
      plan.edge_rotation_radians = -plan.normalized_elapsed * kTau;
    } else {
      const std::uint32_t ready_elapsed_ms = elapsed_ms - state.duration_ms;
      plan.draw_ready_flash = ready_elapsed_ms < kReadyFlashDurationMs &&
                              ((state.ready_flash_abgr >> 24u) != 0u ||
                               !state.bling_texture.empty());
      if (plan.draw_ready_flash) {
        constexpr std::size_t kSegmentCount = kReadyAlpha.size() - 1u;
        const float keyed_progress = static_cast<float>(ready_elapsed_ms) /
                                     static_cast<float>(kReadyFlashDurationMs) *
                                     static_cast<float>(kSegmentCount);
        const auto segment = std::min(static_cast<std::size_t>(keyed_progress),
                                      kSegmentCount - 1u);
        const float fraction = keyed_progress - static_cast<float>(segment);
        const auto interpolate = [segment, fraction](const auto& values) {
          return values[segment] +
                 (values[segment + 1u] - values[segment]) * fraction;
        };
        plan.ready_flash_alpha = interpolate(kReadyAlpha);
        plan.ready_flash_rotation_radians = interpolate(kReadyRotation);
        plan.ready_flash_uv_scale = interpolate(kReadyUvScale);
      }
      return plan;
    }
  }
  plan.draw_edge = state.draw_edge && !state.edge_texture.empty() &&
                   (state.edge_abgr >> 24u) != 0u;

  const float progress_sectors = plan.normalized_elapsed * kSectorCount;
  const float arc_start = state.reverse ? 0.0f : progress_sectors;
  const float arc_span =
      state.reverse ? progress_sectors : kSectorCount - progress_sectors;
  if (arc_span <= 0.000001f || (state.fill_abgr >> 24u) == 0u) {
    return plan;
  }

  const auto perimeter_point = [](const float sector) {
    constexpr std::array<std::array<float, 2>, 9u> points{{
        {{0.5f, 0.0f}},
        {{1.0f, 0.0f}},
        {{1.0f, 0.5f}},
        {{1.0f, 1.0f}},
        {{0.5f, 1.0f}},
        {{0.0f, 1.0f}},
        {{0.0f, 0.5f}},
        {{0.0f, 0.0f}},
        {{0.5f, 0.0f}},
    }};
    const float clamped = std::clamp(sector, 0.0f, 8.0f);
    const std::size_t index = std::min<std::size_t>(
        static_cast<std::size_t>(std::floor(clamped)), 7u);
    const float fraction = clamped - static_cast<float>(index);
    return std::array<float, 2>{
        points[index][0] +
            (points[index + 1u][0] - points[index][0]) * fraction,
        points[index][1] +
            (points[index + 1u][1] - points[index][1]) * fraction,
    };
  };
  const auto append_vertex = [&](const std::array<float, 2>& point) {
    auto& vertex = plan.sweep_vertices[plan.sweep_vertex_count++];
    vertex.x = frame_rect.x + point[0] * frame_rect.width;
    vertex.y = frame_rect.y + point[1] * frame_rect.height;
    vertex.u = point[0];
    vertex.v = point[1];
  };

  append_vertex({0.5f, 0.5f});
  append_vertex(perimeter_point(arc_start));
  const float arc_end = arc_start + arc_span;
  for (float boundary = std::floor(arc_start) + 1.0f;
       boundary < arc_end && plan.sweep_vertex_count < 10u; boundary += 1.0f) {
    append_vertex(perimeter_point(boundary));
  }
  append_vertex(perimeter_point(arc_end));

  for (std::size_t vertex = 1u; vertex + 1u < plan.sweep_vertex_count;
       ++vertex) {
    plan.sweep_indices[plan.sweep_index_count++] = 0u;
    plan.sweep_indices[plan.sweep_index_count++] =
        static_cast<std::uint16_t>(vertex);
    plan.sweep_indices[plan.sweep_index_count++] =
        static_cast<std::uint16_t>(vertex + 1u);
  }
  plan.draw_sweep = plan.sweep_index_count != 0u;
  return plan;
}

bool ReadCooldownState(lua_State* lua, int frame_index,
                       CooldownState* out_state) {
  if (lua == nullptr || out_state == nullptr ||
      lua_istable(lua, frame_index) == 0) {
    return false;
  }
  LuaStackGuard stack(lua);
  frame_index = lua_absindex(lua, frame_index);
  CooldownState state;
  state.bling_texture = std::move(out_state->bling_texture);
  state.edge_texture = std::move(out_state->edge_texture);
  state.start_tick_ms = SecondsToCooldownTicks(
      ReadDouble(lua, frame_index, "__ow_cd_start", 0.0));
  state.duration_ms = SecondsToCooldownTicks(
      ReadDouble(lua, frame_index, "__ow_cd_duration",
                 ReadDouble(lua, frame_index, "__ow_cd_dur", 0.0)));
  state.enabled = ReadBoolean(lua, frame_index, "__ow_cd_enabled", false);
  state.reverse = ReadBoolean(lua, frame_index, "__ow_cd_reverse", false);
  state.draw_edge = ReadBoolean(lua, frame_index, "__ow_cd_draw_edge", false);

  const auto own_alpha =
      openwow::ui::game::ReadLuaFrameAlphaByteOrDefault(lua, frame_index);
  const auto inherited_alpha =
      openwow::ui::game::ComputeLuaFrameParentInheritedAlpha(lua, frame_index);
  const auto sweep_colors = openwow::ui::widgets::ComputeCooldownVisualColors(
      false, own_alpha, inherited_alpha);
  const auto flash_colors = openwow::ui::widgets::ComputeCooldownVisualColors(
      true, own_alpha, inherited_alpha);
  state.fill_abgr = sweep_colors.fill_abgr;
  state.edge_abgr = sweep_colors.edge_abgr;
  state.ready_flash_abgr = flash_colors.ready_flash_abgr;
  ReadStringInto(lua, frame_index, "__ow_cd_bling_texture",
                 &state.bling_texture, "interface\\cooldown\\star4.blp");
  ReadStringInto(lua, frame_index, "__ow_cd_edge_texture", &state.edge_texture,
                 "interface\\cooldown\\edge.blp");
  *out_state = std::move(state);
  return true;
}

void BuildScrollingMessageLinePlans(
    const ScrollingMessageFrameState& state, const Rect& frame_rect,
    const std::vector<ScrollingMessageMeasurement>& measurements,
    const double now_seconds,
    std::vector<ScrollingMessageLinePlan>* out_plans) {
  if (out_plans == nullptr) {
    return;
  }
  out_plans->clear();
  if (!frame_rect.HasArea() || state.messages.empty()) {
    return;
  }

  const std::size_t message_count = state.messages.size();
  const std::size_t selected_offset =
      std::min(state.scroll_offset, message_count - 1u);
  std::size_t message_index = message_count - 1u - selected_offset;
  float consumed_height = 0.0f;
  bool first = true;

  for (;;) {
    float measured_height = state.font_height;
    if (message_index < measurements.size() &&
        std::isfinite(measurements[message_index].height) &&
        measurements[message_index].height > 0.0f) {
      measured_height = measurements[message_index].height;
    }
    measured_height = std::max(1.0f, measured_height);

    if (!first && consumed_height + measured_height > frame_rect.height) {
      break;
    }

    Rect line_rect{
        .x = frame_rect.x,
        .y = frame_rect.y,
        .width = frame_rect.width,
        .height = measured_height,
    };
    if (state.insert_at_top) {
      line_rect.y += consumed_height;
    } else {
      line_rect.y += frame_rect.height - consumed_height - measured_height;
    }

    out_plans->push_back({
        .message_index = message_index,
        .rect = line_rect,
        .clip_rect = frame_rect,
        .alpha = ResolveMessageAlpha(state, state.messages[message_index],
                                     now_seconds),
    });
    consumed_height += measured_height;
    first = false;

    if (message_index == 0u) {
      break;
    }
    --message_index;
  }
}

bool ReadScrollingMessageFrameState(lua_State* lua, int frame_index,
                                    ScrollingMessageFrameState* out_state) {
  if (lua == nullptr || out_state == nullptr ||
      lua_istable(lua, frame_index) == 0) {
    return false;
  }

  LuaStackGuard stack(lua);
  frame_index = lua_absindex(lua, frame_index);

  out_state->default_color = {
      .r = Clamp01(ReadNumber(lua, frame_index, "__ow_text_r", 1.0f)),
      .g = Clamp01(ReadNumber(lua, frame_index, "__ow_text_g", 1.0f)),
      .b = Clamp01(ReadNumber(lua, frame_index, "__ow_text_b", 1.0f)),
      .a = Clamp01(ReadNumber(lua, frame_index, "__ow_text_a", 1.0f)),
  };
  ReadStringInto(lua, frame_index, "__ow_font_path", &out_state->font_path,
                 "Fonts\\FRIZQT__.TTF");
  out_state->font_height =
      ReadNumber(lua, frame_index, "__ow_font_size", 12.0f);
  out_state->fade_duration =
      std::max(0.0f, ReadNumber(lua, frame_index, "__ow_smf_fadedur", 3.0f));
  out_state->time_visible =
      std::max(0.0f, ReadNumber(lua, frame_index, "__ow_smf_timevis", 10.0f));
  out_state->max_lines = std::max<std::size_t>(
      1u, ReadNonNegativeSize(lua, frame_index, "__ow_smf_maxlines", 128u));
  out_state->scroll_offset =
      ReadNonNegativeSize(lua, frame_index, "__ow_smf_scroll", 0u);
  out_state->fading = ReadBoolean(lua, frame_index, "__ow_smf_fading", true);
  out_state->hyperlinks_enabled =
      ReadBoolean(lua, frame_index, "__ow_smf_hyperlinks", true);
  const std::string insert_mode =
      ReadString(lua, frame_index, "__ow_smf_insert", "BOTTOM");
  out_state->insert_at_top =
      openwow::text::EqualsIgnoreCaseAscii(insert_mode, "TOP");

  runtime::GetInternedLuaField(lua, frame_index, "__ow_smf_messages");
  if (lua_istable(lua, -1) == 0) {
    out_state->messages.clear();
    return true;
  }
  const int messages_index = lua_absindex(lua, -1);
  std::size_t message_count = lua_rawlen(lua, messages_index);
  const std::size_t stored_count =
      ReadNonNegativeSize(lua, frame_index, "__ow_smf_num_msg", message_count);
  message_count = std::min(message_count, stored_count);
  const std::size_t first_message = message_count > out_state->max_lines
                                        ? message_count - out_state->max_lines
                                        : 0u;
  const std::size_t retained_count = message_count - first_message;
  out_state->messages.resize(retained_count);
  std::size_t output_index = 0u;

  for (std::size_t index = first_message; index < message_count; ++index) {
    lua_rawgeti(lua, messages_index, static_cast<lua_Integer>(index + 1u));
    if (lua_istable(lua, -1) == 0) {
      lua_pop(lua, 1);
      continue;
    }
    const int message_index = lua_absindex(lua, -1);
    auto& message = out_state->messages[output_index];
    ReadStringInto(lua, message_index, "text", &message.text);
    message.color = {
        .r = Clamp01(
            ReadNumber(lua, message_index, "r", out_state->default_color.r)),
        .g = Clamp01(
            ReadNumber(lua, message_index, "g", out_state->default_color.g)),
        .b = Clamp01(
            ReadNumber(lua, message_index, "b", out_state->default_color.b)),
        .a = Clamp01(
            ReadNumber(lua, message_index, "a", out_state->default_color.a)),
    };
    message.inserted_at_seconds =
        ReadDouble(lua, message_index, "insertedAt",
                   ReadDouble(lua, message_index, "timeAdded", 0.0));
    message.type_id = ReadInt32(lua, message_index, "lineID",
                                ReadInt32(lua, message_index, "typeID", 0));
    message.access_id = ReadInt32(lua, message_index, "accessID", 0);
    message.extra_data = ReadInt32(lua, message_index, "extraData", 0);
    message.enabled = ReadBoolean(lua, message_index, "enabled", true);
    ++output_index;
    lua_pop(lua, 1);
  }
  out_state->messages.resize(output_index);

  if (!out_state->messages.empty()) {
    out_state->scroll_offset =
        std::min(out_state->scroll_offset, out_state->messages.size() - 1u);
  } else {
    out_state->scroll_offset = 0u;
  }
  return true;
}

}

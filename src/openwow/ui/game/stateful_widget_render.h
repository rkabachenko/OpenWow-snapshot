
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "openwow/ui/game/texture_quad_uv.h"

struct lua_State;

namespace openwow::ui::widgets {
struct StatusBarSnapshot;
}

namespace openwow::ui::game::stateful_widgets {

struct Rect {
  float x{0.0f};
  float y{0.0f};
  float width{0.0f};
  float height{0.0f};

  [[nodiscard]] constexpr bool HasArea() const noexcept {
    return width > 0.0f && height > 0.0f;
  }
};

struct Color {
  float r{1.0f};
  float g{1.0f};
  float b{1.0f};
  float a{1.0f};
};

enum class Orientation : std::uint8_t {
  kHorizontal,
  kVertical,
};

struct StatusBarRenderPlan {
  Rect fill_rect{};
  float normalized_value{0.0f};
  bool visible{false};
};

[[nodiscard]] StatusBarRenderPlan BuildStatusBarRenderPlan(
    const openwow::ui::widgets::StatusBarSnapshot& status_bar,
    const Rect& frame_rect);

struct SliderState {
  float minimum{0.0f};
  float maximum{100.0f};
  float value{0.0f};
  Orientation orientation{Orientation::kVertical};
};

struct SliderThumbRenderPlan {
  Rect thumb_rect{};
  float normalized_value{0.0f};
  bool visible{false};
};

[[nodiscard]] SliderThumbRenderPlan BuildSliderThumbRenderPlan(
    const SliderState& state, const Rect& track_rect, float thumb_width,
    float thumb_height);

[[nodiscard]] bool ReadSliderState(lua_State* lua, int frame_index,
                                   SliderState* out_state);

struct CooldownState {
  std::uint32_t start_tick_ms{0u};
  std::uint32_t duration_ms{0u};
  std::uint32_t fill_abgr{0u};
  std::uint32_t ready_flash_abgr{0u};
  std::uint32_t edge_abgr{0u};
  std::string bling_texture{"interface\\cooldown\\star4.blp"};
  std::string edge_texture{"interface\\cooldown\\edge.blp"};
  bool enabled{false};
  bool reverse{false};
  bool draw_edge{false};
};

struct CooldownVertex {
  float x{0.0f};
  float y{0.0f};
  float u{0.0f};
  float v{0.0f};
};

struct CooldownRenderPlan {
  std::array<CooldownVertex, 11u> sweep_vertices{};
  std::array<std::uint16_t, 24u> sweep_indices{};
  std::size_t sweep_vertex_count{0u};
  std::size_t sweep_index_count{0u};
  float normalized_elapsed{0.0f};
  float edge_rotation_radians{0.0f};
  float ready_flash_rotation_radians{0.0f};
  float ready_flash_uv_scale{1.0f};
  float ready_flash_alpha{0.0f};
  bool draw_sweep{false};
  bool draw_edge{false};
  bool draw_ready_flash{false};
};

[[nodiscard]] CooldownRenderPlan BuildCooldownRenderPlan(
    const CooldownState& state, const Rect& frame_rect,
    std::uint32_t now_tick_ms);

[[nodiscard]] bool ReadCooldownState(lua_State* lua, int frame_index,
                                     CooldownState* out_state);

struct ScrollingMessage {
  std::string text;
  Color color{};
  double inserted_at_seconds{0.0};
  std::int32_t type_id{0};
  std::int32_t access_id{0};
  std::int32_t extra_data{0};
  bool enabled{true};
};

struct ScrollingMessageFrameState {
  std::vector<ScrollingMessage> messages;
  Color default_color{};
  std::string font_path{"Fonts\\FRIZQT__.TTF"};
  float font_height{12.0f};
  float fade_duration{3.0f};
  float time_visible{10.0f};
  std::size_t max_lines{128u};
  std::size_t scroll_offset{0u};
  bool fading{true};
  bool insert_at_top{false};
  bool hyperlinks_enabled{true};
};

struct ScrollingMessageMeasurement {
  float width{0.0f};
  float height{0.0f};
};

struct ScrollingMessageLinePlan {
  std::size_t message_index{0u};
  Rect rect{};
  Rect clip_rect{};
  float alpha{1.0f};
};

void BuildScrollingMessageLinePlans(
    const ScrollingMessageFrameState& state, const Rect& frame_rect,
    const std::vector<ScrollingMessageMeasurement>& measurements,
    double now_seconds, std::vector<ScrollingMessageLinePlan>* out_plans);

[[nodiscard]] bool ReadScrollingMessageFrameState(
    lua_State* lua, int frame_index, ScrollingMessageFrameState* out_state);

}

#include "openwow/ui/game/runtime/frame_input_router.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <lua.hpp>
#include <utility>

#include "openwow/foundation/text/ascii.h"
#include "openwow/game/actions/bindings/adapters/lua/binding_script_executor.h"
#include "openwow/input/input_manager.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/framescript/core/script_region_ownership.h"
#include "openwow/ui/game/framescript/widgets/button_method_support.h"
#include "openwow/ui/game/framescript/widgets/edit_box_state.h"
#include "openwow/ui/game/lua_cpu_profiler.h"
#include "openwow/ui/game/lua_mouse_button_context.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/lua_interned_field_key.h"
#include "openwow/ui/game/runtime/frame_traversal_index.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/widgets/simple_edit_box.h"
#include "openwow/ui/widgets/simple_frame.h"

namespace openwow::ui::game::runtime {
namespace {

constexpr float kDragThresholdSquared = 100.0F;
constexpr char kRegisteredDragButtonMaskField[] = "__ow_registered_drag_button_mask";

template <typename PushArguments>
bool FireFrameHandler(lua_State *state, int frame_ref, const char *handler, int argument_count,
                      PushArguments &&push_arguments) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  const int frame_index = lua_absindex(state, -1);
  push_arguments();
  const auto invocation = InvokeFrameScriptHandler(state, frame_index, handler, argument_count);
  if (invocation.status != LUA_OK) {
    lua_pop(state, 1);
  }
  lua_settop(state, top);
  return invocation.invoked;
}

bool FireNoArg(lua_State *state, int frame_ref, const char *handler) {
  return FireFrameHandler(state, frame_ref, handler, 0, [] {});
}

bool FireString(lua_State *state, int frame_ref, const char *handler, std::string_view argument) {
  return FireFrameHandler(state, frame_ref, handler, 1,
                          [&] { lua_pushlstring(state, argument.data(), argument.size()); });
}

bool FireBoolean(lua_State *state, int frame_ref, const char *handler, bool argument) {
  return FireFrameHandler(state, frame_ref, handler, 1,
                          [&] { lua_pushboolean(state, argument ? 1 : 0); });
}

bool FireButton(lua_State *state, int frame_ref, const char *handler, const char *button_name) {
  return FireFrameHandler(state, frame_ref, handler, 1,
                          [&] { lua_pushstring(state, button_name); });
}

bool FireHyperlink(lua_State *state, int frame_ref, const char *handler, std::string_view link,
                   std::string_view text) {
  return FireFrameHandler(state, frame_ref, handler, 2, [&] {
    lua_pushlstring(state, link.data(), link.size());
    lua_pushlstring(state, text.data(), text.size());
  });
}

bool FireHyperlinkClick(lua_State *state, int frame_ref, std::string_view link,
                        std::string_view text, const char *button_name) {
  return FireFrameHandler(state, frame_ref, "OnHyperlinkClick", 3, [&] {
    lua_pushlstring(state, link.data(), link.size());
    lua_pushlstring(state, text.data(), text.size());
    lua_pushstring(state, button_name);
  });
}

bool GetBooleanField(lua_State *state, int index, const char *field) {
  index = lua_absindex(state, index);
  lua_getfield(state, index, field);
  const bool value = lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  return value;
}

std::optional<std::string> GetStringField(lua_State *state, int index, const char *field) {
  index = lua_absindex(state, index);
  lua_getfield(state, index, field);
  std::optional<std::string> value;
  if (const char *text = lua_tostring(state, -1); text != nullptr) {
    value.emplace(text);
  }
  lua_pop(state, 1);
  return value;
}

double ReadRegistryNumberField(lua_State *state, int frame_ref, const char *field,
                               double fallback) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  double value = fallback;
  if (lua_istable(state, -1) != 0) {
    lua_getfield(state, -1, field);
    if (lua_isnumber(state, -1) != 0) {
      value = lua_tonumber(state, -1);
    }
    lua_pop(state, 1);
  }
  lua_settop(state, top);
  return value;
}

bool GetOptionalBooleanField(lua_State *state, int index, const char *field, bool *value) {
  index = lua_absindex(state, index);
  GetInternedLuaField(state, index, field);
  const bool present = lua_isboolean(state, -1) != 0;
  if (present) {
    *value = lua_toboolean(state, -1) != 0;
  }
  lua_pop(state, 1);
  return present;
}

bool FrameUsesMouse(lua_State *state, const FrameStore &frames, std::string_view frame_name) {
  const auto ref = frames.FindLuaRef(frame_name);
  if (state == nullptr || !ref.has_value()) {
    return false;
  }
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, *ref);
  bool enabled = false;
  if (lua_istable(state, -1) != 0 &&
      !(GetOptionalBooleanField(state, -1, "__ow_mouse_enabled", &enabled) ||
        GetOptionalBooleanField(state, -1, "__ow_enableMouse", &enabled))) {

    const auto *frame = frames.FindFrame(frame_name);
    enabled = frame != nullptr &&
              openwow::ui::framexml::StockConstructorEnablesMouse(frame->kind);
  }
  lua_settop(state, top);
  return enabled;
}

bool FrameIsShown(lua_State *state, const FrameStore &frames, std::string_view frame_name) {
  if (const auto ref = frames.FindLuaRef(frame_name); ref.has_value()) {
    const int top = lua_gettop(state);
    lua_rawgeti(state, LUA_REGISTRYINDEX, *ref);
    const bool shown = lua_istable(state, -1) != 0 && detail::GetLuaWidgetShownState(state, -1);
    lua_settop(state, top);
    return shown;
  }
  const auto *frame = frames.FindFrame(frame_name);
  return frame != nullptr && frame->visible;
}

bool FrameIsEditBox(const openwow::ui::framexml::UiFrame &frame) {
  return frame.runtime_kind == openwow::ui::framexml::UiFrame::RuntimeKind::EditBox ||
         openwow::text::EqualsIgnoreCaseAscii(frame.kind, "EditBox");
}

bool IsButtonFrame(const openwow::ui::framexml::UiFrame &frame) {
  return openwow::text::EqualsIgnoreCaseAscii(frame.kind, "Button") ||
         openwow::text::EqualsIgnoreCaseAscii(frame.kind, "CheckButton");
}

bool IsSliderFrame(const openwow::ui::framexml::UiFrame &frame) {
  return openwow::text::EqualsIgnoreCaseAscii(frame.kind, "Slider");
}

bool InvokeNumberMethod(lua_State *state, int frame_ref, const char *method_name,
                        double argument) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  lua_getfield(state, -1, method_name);
  if (lua_isfunction(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  lua_pushvalue(state, -2);
  lua_pushnumber(state, argument);
  const int status = ProfiledPCall(state, 2, 0, 0);
  if (status != LUA_OK) {
    lua_pop(state, 1);
  }
  lua_settop(state, top);
  return true;
}

struct ButtonVisualState {
  bool disabled{false};
  bool locked{false};
};

ButtonVisualState ReadButtonVisualState(lua_State *state, int frame_ref) {
  ButtonVisualState result;
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) != 0) {
    const auto visual = GetStringField(state, -1, "__ow_btn_state").value_or("NORMAL");
    result.disabled = openwow::text::EqualsIgnoreCaseAscii(visual, "DISABLED");

    result.locked = GetBooleanField(state, -1, "__ow_btn_state_locked");
    lua_getfield(state, -1, "__ow_btn_enabled");
    result.disabled |= lua_isboolean(state, -1) != 0 && lua_toboolean(state, -1) == 0;
    lua_pop(state, 1);
  }
  lua_settop(state, top);
  return result;
}

void SetButtonVisualState(lua_State *state, int frame_ref, const char *state_name) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) != 0 &&
      !GetBooleanField(state, -1, "__ow_btn_state_locked")) {
    lua_pushstring(state, state_name);
    lua_setfield(state, -2, "__ow_btn_state");
    frame_api::RefreshButtonLabelFont(state, -1);
  }
  lua_settop(state, top);
}

bool FrameRefHasHandler(lua_State *state, int frame_ref, const char *handler) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  const bool found = lua_istable(state, -1) != 0 && PushFrameScriptHandler(state, -1, handler);
  lua_settop(state, top);
  return found;
}

bool IsRegisteredClickPhase(lua_State *state, int frame_ref, std::string_view button_name,
                            bool is_down) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  lua_getfield(state, -1, "__ow_registered_clicks");
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return !is_down && openwow::text::EqualsIgnoreCaseAscii(button_name, "LeftButton");
  }
  const std::string specific = std::string(button_name) + (is_down ? "Down" : "Up");
  const std::string any = is_down ? "AnyDown" : "AnyUp";
  const auto count = lua_rawlen(state, -1);
  for (lua_Integer i = 1; i <= static_cast<lua_Integer>(count); ++i) {
    lua_geti(state, -1, i);
    const char *token = lua_tostring(state, -1);
    const bool matches =
        token != nullptr && (openwow::text::EqualsIgnoreCaseAscii(token, specific) ||
                             openwow::text::EqualsIgnoreCaseAscii(token, any));
    lua_pop(state, 1);
    if (matches) {
      lua_settop(state, top);
      return true;
    }
  }
  lua_settop(state, top);
  return false;
}

bool InvokeButtonClick(lua_State *state, int frame_ref, const char *button_name, bool is_down) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  lua_getfield(state, -1, "Click");
  if (lua_isfunction(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  lua_pushvalue(state, -2);
  lua_pushstring(state, button_name);
  lua_pushboolean(state, is_down ? 1 : 0);

  const int status = ProfiledPCall(state, 3, 0, 0);
  if (status != LUA_OK) {
    lua_pop(state, 1);
  }
  lua_settop(state, top);
  return true;
}

void ToggleCheckButton(lua_State *state, int frame_ref) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) != 0) {
    lua_getfield(state, -1, "__ow_checked");
    const bool checked = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    lua_pushboolean(state, checked ? 0 : 1);
    lua_setfield(state, -2, "__ow_checked");
  }
  lua_settop(state, top);
}

std::uint32_t MouseButtonFlag(int button) {
  switch (button) {
  case 0:
    return 1u;
  case 1:
    return 4u;
  case 2:
    return 2u;
  default:
    return button >= 3 && button <= 30 ? 1u << button : 0u;
  }
}

std::uint32_t LiveButtonMask(std::uint32_t button_flag, bool down) {
  return detail::ApplyMouseButtonEventToScriptMask(detail::GetCurrentScriptMouseButtonMask(),
                                                   button_flag, down);
}

int ResolveSizingAnchor(const openwow::ui::framexml::FrameRect &rect, float x, float y) {
  const float horizontal_band = static_cast<float>(rect.width) * 0.25F;
  const float vertical_band = static_cast<float>(rect.height) * 0.25F;
  const float horizontal_offset = x - static_cast<float>(rect.x);
  const float vertical_offset = y - static_cast<float>(rect.y);
  const int column = horizontal_offset < horizontal_band
                         ? 0
                         : (horizontal_offset >= horizontal_band * 3.0F ? 2 : 1);
  const int row =
      vertical_offset < vertical_band ? 0 : (vertical_offset >= vertical_band * 3.0F ? 2 : 1);
  return row * 3 + column;
}

bool TitleRegionContainsPoint(lua_State *state, int frame_ref, std::string_view frame_name,
                              const RetainedLayout &layout, float x, float y) {
  const int top = lua_gettop(state);
  lua_rawgeti(state, LUA_REGISTRYINDEX, frame_ref);
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  lua_getfield(state, -1, "__ow_title_region");
  if (lua_istable(state, -1) == 0) {
    lua_settop(state, top);
    return false;
  }
  const auto rect = layout.ResolveAnonymousRegionRect(state, lua_absindex(state, -1), frame_name);
  lua_settop(state, top);
  return rect.has_value() && x >= static_cast<float>(rect->x) &&
         x <= static_cast<float>(rect->x + rect->width) && y >= static_cast<float>(rect->y) &&
         y <= static_cast<float>(rect->y + rect->height);
}

}

FrameInputRouter::FrameInputRouter(FrameStore &frames, FrameTraversalIndex &traversal,
                                   RetainedLayout &layout)
    : frames_(frames), traversal_(traversal), layout_(layout) {}

void FrameInputRouter::BindLuaState(lua_State *state) noexcept {
  lua_ = state;
}

void FrameInputRouter::Reset() noexcept {
  lua_ = nullptr;
  focused_frame_.clear();
  mouseover_frame_.clear();
  mouse_button_captures_ = {};
  button_last_click_time_ms_.clear();
  active_move_sizing_ = {};
  last_mouse_x_ = 0.0F;
  last_mouse_y_ = 0.0F;
  have_last_mouse_position_ = false;
  pressed_button_mask_ = 0;
  hyperlink_hit_regions_.clear();
  hovered_hyperlink_.reset();
  pending_edit_box_updates_.clear();
  processing_edit_box_updates_.clear();
  pending_edit_box_update_refs_.clear();
  edit_box_drag_select_frame_.clear();
  running_macro_input_button_provider_ = {};
}

void FrameInputRouter::BeginHyperlinkHitTestFrame() {
  hyperlink_hit_regions_.clear();
}

void FrameInputRouter::AddHyperlinkHitRegion(std::string frame_name, const float left,
                                             const float top, const float right, const float bottom,
                                             std::string link, std::string text) {
  if (frame_name.empty() || link.empty() || right <= left || bottom <= top) {
    return;
  }
  hyperlink_hit_regions_.push_back({
      .frame_name = std::move(frame_name),
      .left = left,
      .top = top,
      .right = right,
      .bottom = bottom,
      .link = std::move(link),
      .text = std::move(text),
  });
}

const FrameInputRouter::HyperlinkHitRegion *FrameInputRouter::FindHyperlinkAt(const float x,
                                                                              const float y) const {
  for (auto region = hyperlink_hit_regions_.rbegin(); region != hyperlink_hit_regions_.rend();
       ++region) {
    if (x >= region->left && x <= region->right && y >= region->top && y <= region->bottom) {
      return &*region;
    }
  }
  return nullptr;
}

bool FrameInputRouter::FramesShareInputHierarchy(const std::string_view first,
                                                 const std::string_view second) const {
  const auto contains = [&](std::string_view descendant, const std::string_view ancestor) {
    constexpr int kMaxParentDepth = 64;
    for (int depth = 0; depth < kMaxParentDepth && !descendant.empty(); ++depth) {
      if (descendant == ancestor) {
        return true;
      }
      const auto *frame = frames_.FindFrame(descendant);
      if (frame == nullptr || frame->parent.empty()) {
        break;
      }
      descendant = frame->parent;
    }
    return false;
  };
  return contains(first, second) || contains(second, first);
}

const FrameInputRouter::HyperlinkHitRegion *
FrameInputRouter::ResolveHyperlinkAt(const std::string_view ordinary_hit, const float x,
                                     const float y) const {
  const auto *hyperlink = FindHyperlinkAt(x, y);
  if (hyperlink == nullptr) {
    return nullptr;
  }

  return ordinary_hit.empty() || FramesShareInputHierarchy(ordinary_hit, hyperlink->frame_name)
             ? hyperlink
             : nullptr;
}

void FrameInputRouter::UpdateHoveredHyperlink(const float x, const float y) {
  const auto *next = ResolveHyperlinkAt(mouseover_frame_, x, y);
  const bool unchanged = next != nullptr && hovered_hyperlink_.has_value() &&
                         next->frame_name == hovered_hyperlink_->frame_name &&
                         next->link == hovered_hyperlink_->link &&
                         next->text == hovered_hyperlink_->text;
  if (unchanged || (next == nullptr && !hovered_hyperlink_.has_value())) {
    return;
  }
  if (hovered_hyperlink_.has_value()) {
    if (const auto ref = frames_.FindLuaRef(hovered_hyperlink_->frame_name); ref.has_value()) {
      (void)FireHyperlink(lua_, *ref, "OnHyperlinkLeave", hovered_hyperlink_->link,
                          hovered_hyperlink_->text);
    }
  }
  hovered_hyperlink_.reset();
  if (next != nullptr) {
    hovered_hyperlink_ = *next;
    if (const auto ref = frames_.FindLuaRef(next->frame_name); ref.has_value()) {
      (void)FireHyperlink(lua_, *ref, "OnHyperlinkEnter", next->link, next->text);
    }
  }
}

void FrameInputRouter::UpdateSliderValueFromCursor(const std::string &frame_name, float x,
                                                    float y) {
  if (lua_ == nullptr) {
    return;
  }
  const auto rect_it = layout_.rects().find(frame_name);
  if (rect_it == layout_.rects().end() || rect_it->second.width <= 0 ||
      rect_it->second.height <= 0) {
    return;
  }
  const auto ref = frames_.FindLuaRef(frame_name);
  if (!ref.has_value()) {
    return;
  }
  const auto &rect = rect_it->second;
  bool horizontal = false;
  const int top = lua_gettop(lua_);
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(lua_, -1) != 0) {
    lua_getfield(lua_, -1, "__ow_sl_orient");
    if (const char *token = lua_tostring(lua_, -1); token != nullptr) {
      horizontal = openwow::text::EqualsIgnoreCaseAscii(token, "HORIZONTAL");
    }
    lua_pop(lua_, 1);
  }
  lua_settop(lua_, top);

  const double minimum = ReadRegistryNumberField(lua_, *ref, "__ow_sl_min", 0.0);
  const double maximum = ReadRegistryNumberField(lua_, *ref, "__ow_sl_max", 100.0);
  if (!(maximum > minimum)) {
    return;
  }
  const double fraction =
      horizontal
          ? std::clamp((static_cast<double>(x) - rect.x) / static_cast<double>(rect.width),
                       0.0, 1.0)
          : std::clamp((static_cast<double>(y) - rect.y) / static_cast<double>(rect.height),
                       0.0, 1.0);
  const double value = minimum + fraction * (maximum - minimum);
  (void)InvokeNumberMethod(lua_, *ref, "SetValue", value);
}

std::size_t FrameInputRouter::ButtonCaptureIndex(std::uint32_t button_flag) noexcept {
  if (button_flag == 0 || (button_flag & (button_flag - 1u)) != 0) {
    return 31;
  }
  std::size_t index = 0;
  while ((button_flag >>= 1u) != 0) {
    ++index;
  }
  return index;
}

FrameInputRouter::MouseButtonCaptureState *
FrameInputRouter::FindCapture(std::uint32_t button_flag) noexcept {
  const auto index = ButtonCaptureIndex(button_flag);
  return index < mouse_button_captures_.size() ? &mouse_button_captures_[index] : nullptr;
}

float FrameInputRouter::viewport_height() const noexcept {
  return layout_.viewport_height();
}

float FrameInputRouter::root_scale() const noexcept {
  return layout_.root_scale();
}

void FrameInputRouter::RebuildTraversalIfDirty() {
  if (traversal_.order_dirty()) {
    traversal_.Rebuild(root_scale(), viewport_height());
  }
}

bool FrameInputRouter::HandleMouseDown(float x, float y, int button) {
  return HandleMouseButtonDownByFlag(x, y, MouseButtonFlag(button));
}

bool FrameInputRouter::HandleMouseButtonDownByFlag(float x, float y, std::uint32_t button_flag) {
  if (lua_ == nullptr || button_flag == 0) {
    return false;
  }

  SecureExecution::SecureScope hardware_input_scope(lua_);

  SecureExecution::HardwareActionGrantScope hardware_action_grant;
  const ::openwow::game::actions::bindings::adapters::lua::ScopedLuaModifierState modifier_snapshot(
      lua_, static_cast<std::uint16_t>(SDL_GetModState()));
  pressed_button_mask_ |= button_flag;
  last_mouse_x_ = x;
  last_mouse_y_ = y;
  have_last_mouse_position_ = true;
  layout_.SolveIfDirty();
  RebuildTraversalIfDirty();
  std::string hit = traversal_.HitTarget(x, y, viewport_height());
  const auto *pressed_hyperlink = ResolveHyperlinkAt(hit, x, y);
  if (pressed_hyperlink != nullptr) {
    hit = pressed_hyperlink->frame_name;
  }

  if (!hit.empty()) {
    if (const auto *frame = frames_.FindFrame(hit); frame != nullptr && FrameIsEditBox(*frame)) {

      PlaceEditBoxCursorFromClick(hit, x, y);
      TransitionKeyboardFocus(hit);
    }
  }

  if (hit.empty()) {
    if (!mouseover_frame_.empty()) {
      if (const auto ref = frames_.FindLuaRef(mouseover_frame_); ref.has_value()) {
        FireBoolean(lua_, *ref, "OnLeave", false);
      }
      mouseover_frame_.clear();
    }
    for (auto &capture : mouse_button_captures_) {
      if (!capture.active) {
        continue;
      }
      if (const auto ref = frames_.FindLuaRef(capture.frame_name); ref.has_value()) {
        const char *button_name = openwow::ui::widgets::MouseButtonName(capture.button_flag);
        FireButton(lua_, *ref, "OnMouseUp", button_name);
        if (const auto *frame = frames_.FindFrame(capture.frame_name);
            frame != nullptr && IsButtonFrame(*frame)) {
          const auto visual = ReadButtonVisualState(lua_, *ref);
          if (!visual.disabled && !visual.locked) {
            SetButtonVisualState(lua_, *ref, "NORMAL");
          }
        }
      }
      capture = {};
    }
    return false;
  }

  const auto ref = frames_.FindLuaRef(hit);
  auto *capture = FindCapture(button_flag);
  if (!ref.has_value() || capture == nullptr) {
    return false;
  }
  *capture = {};

  const std::uint16_t modifiers = static_cast<std::uint16_t>(SDL_GetModState());
  const bool layout_ctrl = (modifiers & KMOD_LCTRL) != 0;
  const bool layout_alt = (modifiers & KMOD_LALT) != 0;
  if (layout_.mode() != 0 && (layout_ctrl || layout_alt)) {
    const auto rect = layout_.rects().find(hit);
    if (rect != layout_.rects().end()) {
      bool start = false;
      int mode = 0;
      const int top = lua_gettop(lua_);
      lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
      if (lua_istable(lua_, -1) != 0) {
        if (layout_ctrl && GetBooleanField(lua_, -1, "__ow_resizable")) {
          mode = ResolveSizingAnchor(rect->second, x, y);
          start = true;
        } else if (layout_alt && GetBooleanField(lua_, -1, "__ow_movable")) {
          mode = 4;
          start = true;
        }
      }
      lua_settop(lua_, top);
      if (start) {
        (void)BeginFrameMoveSizing(hit, mode);
      }
    }
    return true;
  }

  if (TitleRegionContainsPoint(lua_, *ref, hit, layout_, x, y)) {
    (void)BeginFrameMoveSizing(hit, 4);
    return true;
  }

  const char *button_name = openwow::ui::widgets::MouseButtonName(button_flag);
  *capture = {.frame_name = hit,
              .button_flag = button_flag,
              .start_x = x,
              .start_y = y,
              .active = true,
              .drag_started = false};
  if (pressed_hyperlink != nullptr) {
    capture->hyperlink_link = pressed_hyperlink->link;
    capture->hyperlink_text = pressed_hyperlink->text;
  }
  const lua_adapter::ScopedMouseButtonOverride button_override(lua_, button_name);
  const detail::ScopedCurrentMouseButtonMaskOverride mask_override(
      lua_, LiveButtonMask(button_flag, true));
  (void)FireButton(lua_, *ref, "OnMouseDown", button_name);
  const auto *frame = frames_.FindFrame(hit);
  if (frame != nullptr && IsSliderFrame(*frame) &&
      openwow::text::EqualsIgnoreCaseAscii(button_name, "LeftButton")) {

    capture->is_slider = true;
    UpdateSliderValueFromCursor(hit, x, y);
  }
  if (frame != nullptr && IsButtonFrame(*frame)) {
    const auto visual = ReadButtonVisualState(lua_, *ref);
    if (!visual.disabled) {
      if (IsRegisteredClickPhase(lua_, *ref, button_name, true)) {
        (void)InvokeButtonClick(lua_, *ref, button_name, true);
      }
      if (!visual.locked) {
        SetButtonVisualState(lua_, *ref, "PUSHED");
      }
    }
  } else if (IsRegisteredClickPhase(lua_, *ref, button_name, true)) {
    (void)FireButton(lua_, *ref, "OnClick", button_name);
  }
  return true;
}

bool FrameInputRouter::HandleMouseUp(float x, float y, int button) {
  return HandleMouseButtonUpByFlag(x, y, MouseButtonFlag(button));
}

bool FrameInputRouter::HandleMouseButtonUpByFlag(float x, float y, std::uint32_t button_flag) {
  if (lua_ == nullptr || button_flag == 0) {
    return false;
  }
  SecureExecution::SecureScope hardware_input_scope(lua_);

  SecureExecution::HardwareActionGrantScope hardware_action_grant;
  const ::openwow::game::actions::bindings::adapters::lua::ScopedLuaModifierState modifier_snapshot(
      lua_, static_cast<std::uint16_t>(SDL_GetModState()));
  pressed_button_mask_ &= ~button_flag;
  layout_.SolveIfDirty();

  edit_box_drag_select_frame_.clear();
  const char *button_name = openwow::ui::widgets::MouseButtonName(button_flag);
  bool handled = false;

  if (active_move_sizing_.active) {
    (void)StopFrameMoveSizing(active_move_sizing_.frame_name);
    handled = true;
  }

  auto *capture = FindCapture(button_flag);
  if (capture != nullptr && capture->active) {
    const std::string capture_name = capture->frame_name;
    const auto ref = frames_.FindLuaRef(capture_name);
    if (ref.has_value()) {
      if (capture->drag_started) {
        const lua_adapter::ScopedMouseButtonOverride button_override(lua_, button_name);
        const detail::ScopedCurrentMouseButtonMaskOverride mask_override(
            lua_, LiveButtonMask(button_flag, false));
        (void)FireNoArg(lua_, *ref, "OnDragStop");
        layout_.SolveIfDirty();
        RebuildTraversalIfDirty();
        const std::string target = traversal_.HitTarget(x, y, viewport_height());
        if (const auto target_ref = frames_.FindLuaRef(target); target_ref.has_value()) {
          (void)FireNoArg(lua_, *target_ref, "OnReceiveDrag");
        }
        handled = true;
      } else {
        const lua_adapter::ScopedMouseButtonOverride button_override(lua_, button_name);
        const detail::ScopedCurrentMouseButtonMaskOverride mask_override(
            lua_, LiveButtonMask(button_flag, false));
        handled = FireButton(lua_, *ref, "OnMouseUp", button_name) || handled;
        bool hyperlink_clicked = false;
        if (!capture->hyperlink_link.empty()) {
          const std::string release_hit = traversal_.HitTarget(x, y, viewport_height());
          if (const auto *hyperlink = ResolveHyperlinkAt(release_hit, x, y);
              hyperlink != nullptr && hyperlink->frame_name == capture_name &&
              hyperlink->link == capture->hyperlink_link &&
              hyperlink->text == capture->hyperlink_text) {
            hyperlink_clicked =
                FireHyperlinkClick(lua_, *ref, hyperlink->link, hyperlink->text, button_name);
            handled = hyperlink_clicked || handled;
          }
        }
        const auto *frame = frames_.FindFrame(capture_name);
        if (frame != nullptr && IsButtonFrame(*frame)) {
          const auto visual = ReadButtonVisualState(lua_, *ref);
          const bool inside = traversal_.Contains(capture_name, x, y, viewport_height());

          if (!visual.disabled && inside &&
              IsRegisteredClickPhase(lua_, *ref, button_name, false)) {
            const std::uint32_t now = openwow::core::GameClock::GetTickCount32();
            auto &last_click = button_last_click_time_ms_[capture_name];
            if (last_click != 0 && now - last_click <= 300 &&
                FrameRefHasHandler(lua_, *ref, "OnDoubleClick")) {
              (void)FireButton(lua_, *ref, "OnDoubleClick", button_name);
              last_click = 0;
            } else {
              if (openwow::text::EqualsIgnoreCaseAscii(frame->kind, "CheckButton")) {
                ToggleCheckButton(lua_, *ref);
              }
              (void)InvokeButtonClick(lua_, *ref, button_name, false);
              last_click = now;
            }
            handled = true;
          }
        } else if (!hyperlink_clicked && IsRegisteredClickPhase(lua_, *ref, button_name, false)) {
          (void)FireButton(lua_, *ref, "OnClick", button_name);
          handled = true;
        }
      }
    }
    if (const auto *frame = frames_.FindFrame(capture_name);
        frame != nullptr && IsButtonFrame(*frame)) {
      if (const auto current_ref = frames_.FindLuaRef(capture_name); current_ref.has_value()) {
        const auto visual = ReadButtonVisualState(lua_, *current_ref);
        if (!visual.disabled && !visual.locked) {
          SetButtonVisualState(lua_, *current_ref, "NORMAL");
        }
      }
    }
    *capture = {};
    return handled;
  }

  RebuildTraversalIfDirty();
  const std::string hit = traversal_.HitTarget(x, y, viewport_height());
  const auto ref = frames_.FindLuaRef(hit);
  if (!ref.has_value()) {
    return handled;
  }
  const lua_adapter::ScopedMouseButtonOverride button_override(lua_, button_name);
  const detail::ScopedCurrentMouseButtonMaskOverride mask_override(
      lua_, LiveButtonMask(button_flag, false));
  handled = FireButton(lua_, *ref, "OnMouseUp", button_name) || handled;
  const auto *frame = frames_.FindFrame(hit);
  if ((frame == nullptr || !IsButtonFrame(*frame)) &&
      IsRegisteredClickPhase(lua_, *ref, button_name, false)) {
    (void)FireButton(lua_, *ref, "OnClick", button_name);
    handled = true;
  }
  return handled;
}

bool FrameInputRouter::HandleMouseMove(float x, float y) {
  if (lua_ == nullptr) {
    return false;
  }
  layout_.SolveIfDirty();
  last_mouse_x_ = x;
  last_mouse_y_ = y;
  have_last_mouse_position_ = true;

  if (active_move_sizing_.active && layout_.UpdateMoveSizing(&active_move_sizing_, x, y)) {

    traversal_.InvalidateHitTest();
    RebuildTraversalIfDirty();
    return true;
  }

  if (!edit_box_drag_select_frame_.empty()) {
    MoveEditBoxCursorToPoint(edit_box_drag_select_frame_, x, y,
                             true);
  }

  for (auto &capture : mouse_button_captures_) {
    if (!capture.active) {
      continue;
    }
    if (capture.is_slider) {

      UpdateSliderValueFromCursor(capture.frame_name, x, y);
      continue;
    }
    if (capture.drag_started) {
      continue;
    }
    const auto ref = frames_.FindLuaRef(capture.frame_name);
    if (!ref.has_value()) {
      capture = {};
      continue;
    }
    const float dx = x - capture.start_x;
    const float dy = y - capture.start_y;
    if (dx * dx + dy * dy < kDragThresholdSquared) {
      continue;
    }
    std::uint32_t drag_mask = 0;
    const int top = lua_gettop(lua_);
    lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
    if (lua_istable(lua_, -1) != 0) {
      lua_getfield(lua_, -1, kRegisteredDragButtonMaskField);
      if (lua_isnumber(lua_, -1) != 0 && lua_tointeger(lua_, -1) > 0) {
        drag_mask = static_cast<std::uint32_t>(lua_tointeger(lua_, -1));
      }
    }
    lua_settop(lua_, top);
    if ((drag_mask & capture.button_flag) != 0) {
      const char *button_name = openwow::ui::widgets::MouseButtonName(capture.button_flag);
      const lua_adapter::ScopedMouseButtonOverride button_override(lua_, button_name);
      (void)FireButton(lua_, *ref, "OnDragStart", button_name);
      capture.drag_started = true;
      capture.start_x = 0.0F;
      capture.start_y = 0.0F;
    }
  }

  RebuildTraversalIfDirty();
  const std::string next = traversal_.HitTarget(x, y, viewport_height());
  if (next != mouseover_frame_) {
    if (const auto old_ref = frames_.FindLuaRef(mouseover_frame_); old_ref.has_value()) {
      FireBoolean(lua_, *old_ref, "OnLeave", true);
    }
    if (const auto new_ref = frames_.FindLuaRef(next); new_ref.has_value()) {
      FireBoolean(lua_, *new_ref, "OnEnter", true);
    }
    mouseover_frame_ = next;
  }
  UpdateHoveredHyperlink(x, y);
  return !mouseover_frame_.empty();
}

bool FrameInputRouter::HandleMouseWheel(float x, float y, float delta) {
  if (lua_ == nullptr) {
    return false;
  }
  layout_.SolveIfDirty();
  RebuildTraversalIfDirty();
  const lua_Integer normalized_delta = delta >= 0.0F ? 1 : -1;
  for (const auto &entry : traversal_.input_snapshot()) {
    if (!entry.effective_visible || !entry.uses_mouse_wheel || entry.lua_ref == LUA_NOREF ||
        !traversal_.Contains(entry.key, x, y, viewport_height())) {
      continue;
    }
    if (FireFrameHandler(lua_, entry.lua_ref, "OnMouseWheel", 1,
                         [&] { lua_pushinteger(lua_, normalized_delta); })) {
      return true;
    }
  }
  return false;
}

bool FrameInputRouter::PushTrackedFrame(lua_State *state, std::string_view frame_name) const {
  if (state == nullptr || frame_name.empty()) {
    return false;
  }
  const auto ref = frames_.FindLuaRef(frame_name);
  if (!ref.has_value()) {
    return false;
  }
  lua_rawgeti(state, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    return false;
  }
  return true;
}

bool FrameInputRouter::PushFocusedFrame(lua_State *state) const {
  return PushTrackedFrame(state, focused_frame_);
}

bool FrameInputRouter::PushMouseoverFrame(lua_State *state) const {
  return PushTrackedFrame(state, mouseover_frame_);
}

void FrameInputRouter::QueueEditBoxCaretRefresh(
    const std::string &frame_name) {
  if (lua_ == nullptr || frame_name.empty()) return;
  const auto *const frame = frames_.FindFrame(frame_name);
  if (frame == nullptr || !FrameIsEditBox(*frame)) return;
  const auto ref = frames_.FindLuaRef(frame_name);
  if (!ref.has_value()) return;
  const int top = lua_gettop(lua_);
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(lua_, -1) != 0) {
    QueueEditBoxDirtyState(lua_, -1, false, false, true);
  }
  lua_settop(lua_, top);
}

void FrameInputRouter::TransitionKeyboardFocus(const std::string &new_frame_name) {
  if (new_frame_name == focused_frame_) {
    return;
  }
  const std::string old_frame_name = focused_frame_;
  if (new_frame_name.empty()) {
    focused_frame_.clear();
    QueueEditBoxCaretRefresh(old_frame_name);
    if (const auto ref = frames_.FindLuaRef(old_frame_name); ref.has_value()) {
      (void)FireNoArg(lua_, *ref, "OnEditFocusLost");
    }
    return;
  }
  if (const auto ref = frames_.FindLuaRef(old_frame_name); ref.has_value()) {
    (void)FireNoArg(lua_, *ref, "OnEditFocusLost");
  }
  focused_frame_ = new_frame_name;
  QueueEditBoxCaretRefresh(old_frame_name);
  QueueEditBoxCaretRefresh(focused_frame_);
  if (const auto ref = frames_.FindLuaRef(focused_frame_); ref.has_value()) {
    (void)FireNoArg(lua_, *ref, "OnEditFocusGained");
  }
}

void FrameInputRouter::SetFocus(const std::string &frame_name) {
  if (lua_ == nullptr) {
    return;
  }
  const auto *frame = frames_.FindFrame(frame_name);
  if (frame != nullptr && FrameIsEditBox(*frame) && FrameIsShown(lua_, frames_, frame_name)) {
    TransitionKeyboardFocus(frame_name);
  }
}

void FrameInputRouter::ClearFocus() {
  if (lua_ != nullptr) {
    TransitionKeyboardFocus("");
  }
}

void FrameInputRouter::ClearFocusIfOwnedBy(std::string_view frame_name) {
  if (lua_ != nullptr && frame_name == focused_frame_) {
    TransitionKeyboardFocus("");
  }
}

std::optional<std::string> FrameInputRouter::ResolveModifiedMouseoverUnitToken() {
  if (lua_ == nullptr || mouseover_frame_.empty()) {
    return std::nullopt;
  }
  const auto ref = frames_.FindLuaRef(mouseover_frame_);
  if (!ref.has_value()) {
    return std::nullopt;
  }
  const int top = lua_gettop(lua_);
  lua_getglobal(lua_, "SecureButton_GetModifiedUnit");
  if (lua_isfunction(lua_, -1) == 0) {
    lua_settop(lua_, top);
    return std::nullopt;
  }
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(lua_, -1) == 0) {
    lua_settop(lua_, top);
    return std::nullopt;
  }
  const auto button =
      running_macro_input_button_provider_ ? running_macro_input_button_provider_() : std::nullopt;
  if (button.has_value()) {
    const auto value = button->value();
    lua_pushlstring(lua_, value.data(), value.size());
  } else {
    lua_pushnil(lua_);
  }
  std::optional<std::string> token;
  SecureExecution::SecureScope scope;
  if (lua_pcall(lua_, 2, 1, 0) == LUA_OK) {
    if (const char *value = lua_tostring(lua_, -1); value != nullptr && value[0] != '\0') {
      token.emplace(value);
    }
  } else {
    lua_pop(lua_, 1);
  }
  lua_settop(lua_, top);
  return token;
}

void FrameInputRouter::SetRunningMacroInputButtonProvider(
    RunningMacroInputButtonProvider provider) {
  running_macro_input_button_provider_ = std::move(provider);
}

bool FrameInputRouter::BeginFrameMoveSizing(const std::string &name, int mode) {
  if (active_move_sizing_.active) {
    return false;
  }
  layout_.SolveIfDirty();
  RebuildTraversalIfDirty();
  float cursor_x = last_mouse_x_;
  float cursor_y = last_mouse_y_;
  if (!have_last_mouse_position_) {
    const auto [mouse_x, mouse_y] = openwow::input::InputManager::Get().GetMousePosition();
    cursor_x = static_cast<float>(mouse_x);
    cursor_y = static_cast<float>(mouse_y);
  }
  return layout_.BeginMoveSizing(name, mode, cursor_x, cursor_y, &active_move_sizing_);
}

bool FrameInputRouter::StopFrameMoveSizing(const std::string &name) {
  if (!active_move_sizing_.active || active_move_sizing_.frame_name != name) {
    return false;
  }
  const bool committed = layout_.CommitMoveSizing(&active_move_sizing_);
  if (committed) {
    RebuildTraversalIfDirty();
  }
  return committed;
}

void FrameInputRouter::QueueEditBoxStateUpdate(int lua_ref) {
  if (lua_ == nullptr || lua_ref == LUA_NOREF || lua_ref == LUA_REFNIL) {
    return;
  }
  if (pending_edit_box_update_refs_.insert(lua_ref).second) {
    pending_edit_box_updates_.push_back(lua_ref);
  }
}

EditBoxRegionPorts FrameInputRouter::BuildEditBoxRegionPorts(
    const std::string_view edit_box_key) {
  return EditBoxRegionPorts{
      .frames = &frames_,
      .layout = &layout_,
      .traversal = &traversal_,
      .focused = !edit_box_key.empty() && edit_box_key == focused_frame_,
      .application_active = application_active_,
  };
}

void FrameInputRouter::FlushPendingEditBoxStateUpdates() {
  if (lua_ == nullptr) {
    return;
  }
  processing_edit_box_updates_.clear();
  processing_edit_box_updates_.swap(pending_edit_box_updates_);
  for (const int ref : processing_edit_box_updates_) {
    pending_edit_box_update_refs_.erase(ref);
    const int top = lua_gettop(lua_);
    lua_rawgeti(lua_, LUA_REGISTRYINDEX, ref);
    if (lua_istable(lua_, -1) != 0) {

      const char *const borrowed =
          frame_api::GetFrameRuntimeKeyOrName(lua_, lua_absindex(lua_, -1));
      const std::string edit_box_key = borrowed != nullptr ? borrowed : "";
      FlushEditBoxDirtyState(lua_, -1, edit_box_key,
                             BuildEditBoxRegionPorts(edit_box_key));
    }
    lua_settop(lua_, top);
  }
  processing_edit_box_updates_.clear();
}

void FrameInputRouter::MoveEditBoxCursorToPoint(const std::string &frame_name,
                                                const float x, const float y,
                                                const bool extend) {
  if (lua_ == nullptr || frame_name.empty()) return;
  const auto ref = frames_.FindLuaRef(frame_name);
  if (!ref.has_value()) return;
  layout_.SolveIfDirty();
  const int top = lua_gettop(lua_);
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(lua_, -1) == 0) {
    lua_settop(lua_, top);
    return;
  }
  const int edit_box = lua_absindex(lua_, -1);

  lua_getfield(lua_, edit_box, "__ow_eb_ime_composing");
  const bool composing = lua_toboolean(lua_, -1) != 0;
  lua_pop(lua_, 1);
  if (composing) {
    lua_settop(lua_, top);
    return;
  }
  const auto index = ResolveEditBoxCharacterIndexAtPoint(lua_, edit_box,
                                                         frame_name, x, y,
                                                         layout_);
  if (!index.has_value()) {
    lua_settop(lua_, top);
    return;
  }
  lua_getfield(lua_, edit_box, "__ow_eb_text");
  std::size_t text_size = 0;
  (void)lua_tolstring(lua_, -1, &text_size);
  lua_pop(lua_, 1);
  const int clamped =
      std::clamp(*index, 0, static_cast<int>(text_size));

  int selection_start = clamped;
  int selection_end = clamped;
  if (extend) {
    const auto read = [&](const char *field) {
      lua_getfield(lua_, edit_box, field);
      const int value = lua_isnumber(lua_, -1) != 0
                            ? static_cast<int>(lua_tointeger(lua_, -1))
                            : clamped;
      lua_pop(lua_, 1);
      return std::clamp(value, 0, static_cast<int>(text_size));
    };
    selection_start = read("__ow_eb_hl_start");
    selection_end = read("__ow_eb_hl_end");
    if (selection_start > selection_end) {
      std::swap(selection_start, selection_end);
    }
    if (clamped < selection_start) {
      selection_start = clamped;
    } else if (clamped > selection_end) {
      selection_end = clamped;
    } else {
      selection_start = clamped;
    }
  }
  lua_pushinteger(lua_, clamped);
  lua_setfield(lua_, edit_box, "__ow_eb_cursor");
  lua_pushinteger(lua_, selection_start);
  lua_setfield(lua_, edit_box, "__ow_eb_hl_start");
  lua_pushinteger(lua_, selection_end);
  lua_setfield(lua_, edit_box, "__ow_eb_hl_end");
  QueueEditBoxDirtyState(lua_, edit_box, false, false, true);
  lua_settop(lua_, top);
}

void FrameInputRouter::PlaceEditBoxCursorFromClick(
    const std::string &frame_name, const float x, const float y) {
  MoveEditBoxCursorToPoint(frame_name, x, y, false);

  edit_box_drag_select_frame_ = frame_name;
}

void FrameInputRouter::SetApplicationActive(const bool active) {
  if (application_active_ == active) return;
  application_active_ = active;

  QueueEditBoxCaretRefresh(focused_frame_);
}

void FrameInputRouter::UpdateFocusedEditBoxCaretBlink(const float dt) {
  if (lua_ == nullptr || focused_frame_.empty()) {
    return;
  }
  const auto *const frame = frames_.FindFrame(focused_frame_);
  if (frame == nullptr || !FrameIsEditBox(*frame)) {
    return;
  }
  const auto ref = frames_.FindLuaRef(focused_frame_);
  if (!ref.has_value()) {
    return;
  }
  const int top = lua_gettop(lua_);
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(lua_, -1) != 0) {
    UpdateEditBoxCaretBlink(lua_, -1, focused_frame_, dt,
                            BuildEditBoxRegionPorts(focused_frame_));
  }
  lua_settop(lua_, top);
}

void FrameInputRouter::UpdateFocusedEditBoxInputLanguage() {
  if (lua_ == nullptr || focused_frame_.empty() || !FrameIsShown(lua_, frames_, focused_frame_)) {
    return;
  }
  const auto *frame = frames_.FindFrame(focused_frame_);
  const auto ref = frames_.FindLuaRef(focused_frame_);
  if (frame == nullptr || !FrameIsEditBox(*frame) || !ref.has_value()) {
    return;
  }
  const char *next = openwow::ui::widgets::EditBoxInputLanguageToken(
      openwow::ui::widgets::DetectActiveEditBoxInputLanguage());
  std::string current = openwow::ui::widgets::EditBoxInputLanguageToken(
      openwow::ui::widgets::EditBoxInputLanguage::Roman);
  const int top = lua_gettop(lua_);
  lua_rawgeti(lua_, LUA_REGISTRYINDEX, *ref);
  if (lua_istable(lua_, -1) == 0) {
    lua_settop(lua_, top);
    return;
  }
  lua_getfield(lua_, -1, "__ow_eb_input_language");
  if (const char *stored = lua_tostring(lua_, -1); stored != nullptr) {
    current = stored;
  }
  lua_pop(lua_, 1);
  const bool changed = current != next;
  if (changed || current.empty()) {
    lua_pushstring(lua_, next);
    lua_setfield(lua_, -2, "__ow_eb_input_language");
  }
  lua_settop(lua_, top);
  if (changed) {
    (void)FireString(lua_, *ref, "OnInputLanguageChanged", next);
  }
}

bool FrameInputRouter::FocusedFrameIsEffectivelyVisible() const {
  return !focused_frame_.empty() && traversal_.IsEffectivelyVisible(focused_frame_);
}

void FrameInputRouter::ReconcileFrameInputMutation(std::string_view frame_name,
                                                   bool focused_was_effectively_visible) {
  for (auto &capture : mouse_button_captures_) {
    if (capture.active && capture.frame_name == frame_name &&
        !FrameUsesMouse(lua_, frames_, frame_name)) {
      capture = {};
    }
  }
  if (mouseover_frame_ == frame_name &&
      !(FrameIsShown(lua_, frames_, frame_name) && FrameUsesMouse(lua_, frames_, frame_name))) {
    if (const auto ref = frames_.FindLuaRef(frame_name); ref.has_value()) {
      FireBoolean(lua_, *ref, "OnLeave", false);
    }
    mouseover_frame_.clear();
  }
  if (focused_was_effectively_visible && !focused_frame_.empty() &&
      !traversal_.IsEffectivelyVisible(focused_frame_)) {
    TransitionKeyboardFocus("");
  }
}

void FrameInputRouter::BeforeFrameBindingRelease(int lua_ref) {
  if (lua_ref == LUA_NOREF || lua_ref == LUA_REFNIL) {
    return;
  }
  pending_edit_box_update_refs_.erase(lua_ref);
  pending_edit_box_updates_.erase(
      std::remove(pending_edit_box_updates_.begin(), pending_edit_box_updates_.end(), lua_ref),
      pending_edit_box_updates_.end());
}

void FrameInputRouter::AfterFrameIdentityRelease(std::string_view frame_name) {
  if (mouseover_frame_ == frame_name) {
    mouseover_frame_.clear();
  }
  if (focused_frame_ == frame_name) {
    focused_frame_.clear();
  }
  if (active_move_sizing_.frame_name == frame_name) {
    active_move_sizing_ = {};
  }
  for (auto &capture : mouse_button_captures_) {
    if (capture.frame_name == frame_name) {
      capture = {};
    }
  }
}

}

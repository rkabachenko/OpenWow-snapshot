#include "glue_client.h"
#include "glue_slider_input.h"

#include "openwow/audio/playback/sound_interface.h"
#include "openwow/core/client_init.h"
#include "openwow/net/client_services.h"
#include "openwow/runtime/scheduling/evt_sched.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/actions/bindings/adapters/platform/sdl_binding_input.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/input/input_control.h"
#include "openwow/input/input_manager.h"
#include "openwow/platform/adapters/clipboard/os_clipboard.h"
#include "openwow/platform/window/system_mouse_speed.h"
#include "openwow/platform/window/window_manager.h"
#include "openwow/ui/glue/editbox_input_dispatch.h"
#include "openwow/ui/game/api/game_lua_api_movement.h"
#include "openwow/ui/lua_call_helpers.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/utf8.h"

extern "C" {
#include <lua.h>
}

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace openwow::client {

namespace {

constexpr int kRealmListRowHeightPx = 52;
constexpr int kCharacterSelectRowHeightPx = 58;

void RequestApplicationQuit() {
  auto& client_services = openwow::net::ClientServices::Instance();
  if (client_services.HasPendingLogoutRequest()) {
    return;
  }

  if (client_services.RequestQuit()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                              "System close requested graceful world logout");
    return;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                            "System close requested immediate glue shutdown");
  (void)openwow::core::RequestClientShutdownWithErrorCode(0);
}

std::uint32_t WowMouseButtonBitmaskFromSdlButton(const std::uint8_t button) {
  switch (button) {
    case SDL_BUTTON_LEFT:
      return 1u;
    case SDL_BUTTON_MIDDLE:
      return 2u;
    case SDL_BUTTON_RIGHT:
      return 4u;
    default:
      break;
  }

  if (button < SDL_BUTTON_X1 || button > 31u) {
    return 0u;
  }

  return 1u << (button - 1u);
}

void RunMouseButtonDownPrelude(const std::uint32_t button_flag,
                               openwow::game::GameLoop& game_loop,
                               openwow::game::WorldSession* const session) {
  if (button_flag != 4u) {
    return;
  }

  if (session != nullptr && session->spells().GetTargeting().IsTargeting()) {
    session->spells().GetTargeting().CancelTargeting();
  }

  if (!game_loop.held_cursor().empty()) {
    game_loop.held_cursor().Clear();
  }

  auto& cursor_manager = game_loop.cursor_manager();
  if (cursor_manager.GetBaseCursorType() == openwow::game::CursorType::kRepair) {
    cursor_manager.SetBaseCursor(openwow::game::CursorType::kDefault);
    cursor_manager.SetImmediateCursorType(1);
  }
}

void SetDrawableMousePosition(SDL_Window* window, int x, int y) {
  ScaleMouseToDrawable(window, x, y);
  openwow::input::InputManager::Get().SetMousePosition(x, y);
}

void SyncDrawableMousePositionFromWindow(SDL_Window* window) {

  const auto cursor =
      openwow::platform::WindowManager::Get().ResolveLogicalCursorPosition();
  if (cursor.has_value()) {
    SetDrawableMousePosition(window, cursor->first, cursor->second);
  }
}

void RestoreCursorAnchorToWindow(SDL_Window* window) {
  auto& window_manager = openwow::platform::WindowManager::Get();
  if (!window_manager.RestoreCursorAnchor()) {
    return;
  }

  SyncDrawableMousePositionFromWindow(window);
}

void EnterRelativeCursorMode(SDL_Window* window) {
  auto& window_manager = openwow::platform::WindowManager::Get();
  if (!window_manager.BeginRelativeCursorMode()) {
    return;
  }

  SyncDrawableMousePositionFromWindow(window);
}

void LeaveRelativeCursorMode() {
  openwow::platform::WindowManager::Get().EndRelativeCursorMode();
}

std::pair<int, int> ResolveInWorldMouseButtonDispatchPosition(const SDL_MouseButtonEvent& event) {
  return openwow::platform::WindowManager::Get().ResolveMouseButtonDispatchPosition(
      event.x, event.y, openwow::platform::WindowManager::Get().IsRelativeCursorModeActive());
}

}

void GlueClient::UpdateInWorldMouseButtonState(std::uint8_t button, bool pressed) {
  const bool had_held_button = left_mouse_held_ || right_mouse_held_;

  switch (button) {
    case SDL_BUTTON_LEFT:
      left_mouse_held_ = pressed;
      break;
    case SDL_BUTTON_RIGHT:
      right_mouse_held_ = pressed;
      break;
    default:
      return;
  }

  const bool has_held_button = left_mouse_held_ || right_mouse_held_;
  if (!had_held_button && has_held_button) {
    (void)openwow::platform::WindowManager::Get().CaptureCursorAnchor();
    game_loop_.BeginCameraFreelook();
    EnterRelativeCursorMode(window_);
  } else if (had_held_button && !has_held_button) {
    game_loop_.EndCameraFreelook();
    LeaveRelativeCursorMode();
    RestoreCursorAnchorToWindow(window_);
  }
}

void GlueClient::ReleaseInWorldMouseButtons() {
  if (game_loop_.game_ui().is_initialized()) {
    openwow::ui::game::detail::CancelWorldMouseInput(
        game_loop_.game_ui().lua_state());
  }
  left_mouse_held_ = false;
  right_mouse_held_ = false;
  openwow::platform::WindowManager::Get().ResetMouseButtonCapture();
  LeaveRelativeCursorMode();
  game_loop_.EndCameraFreelook();
  RestoreCursorAnchorToWindow(window_);
  openwow::input::OnMouseButtonClear();
  openwow::platform::WindowManager::Get().ClearCursorAnchor();
}

void GlueClient::PumpPendingWindowEvents() {
  SDL_Event event;

  bool have_pending_motion = false;
  SDL_Event pending_motion{};
  const auto flush_pending_motion = [&]() {
    if (have_pending_motion) {
      pending_window_events_.Enqueue(pending_motion, openwow::core::GameClock::GetTickCount32());
      have_pending_motion = false;
    }
  };

  while (SDL_PollEvent(&event) != 0) {
    if (event.type == SDL_MOUSEMOTION) {
      if (have_pending_motion) {
        const Sint32 accumulated_xrel = pending_motion.motion.xrel + event.motion.xrel;
        const Sint32 accumulated_yrel = pending_motion.motion.yrel + event.motion.yrel;
        pending_motion = event;
        pending_motion.motion.xrel = accumulated_xrel;
        pending_motion.motion.yrel = accumulated_yrel;
      } else {
        pending_motion = event;
        have_pending_motion = true;
      }
      continue;
    }
    flush_pending_motion();
    pending_window_events_.Enqueue(event, openwow::core::GameClock::GetTickCount32());
  }
  flush_pending_motion();

  openwow::platform::PendingWindowEventQueue::Entry pending_event;
  while (pending_window_events_.TryDequeue(pending_event)) {
    HandleEvent(pending_event.event);
    if (!running_) {
      break;
    }
  }

  if (running_ && (left_mouse_held_ || right_mouse_held_)) {
    DispatchRelativeCursorMotionTick();
  }
}

void GlueClient::DispatchRelativeCursorMotionTick() {

  const auto relative_motion =
      openwow::platform::WindowManager::Get().HandleRelativeCursorMotion();
  if (!relative_motion.has_delta) {
    return;
  }

  auto [mouse_x, mouse_y] =
      openwow::platform::WindowManager::Get().ResolveMouseButtonDispatchPosition(
          0, 0, true);
  ScaleMouseToDrawable(window_, mouse_x, mouse_y);
  const auto motion = openwow::input::DispatchInWorldMouseMotion(
      mouse_x,
      mouse_y,
      relative_motion.delta_x,
      relative_motion.delta_y,
      true);
  if (motion.has_camera_delta) {

    game_loop_.HandleMouseDelta(motion.camera_dx, motion.camera_dy);
  }
}

void GlueClient::ApplyWindowFocusChange(const bool focused) {
  window_focused_ = focused;
  text_input_reactivation_pending_ = true;
  openwow::platform::SystemMouseSpeedController::Instance().SetWindowActive(
      window_focused_);
  if (window_focused_) {

    game_loop_.cursor_manager().ReassertPresentation();
  } else {
    if (mode_ == UiMode::kInWorld) {
      ReleaseInWorldMouseButtons();
    } else {
      openwow::platform::WindowManager::Get().ResetMouseButtonCapture();
    }
  }

  if (mode_ == UiMode::kInWorld && game_loop_.game_ui().is_initialized()) {
    openwow::ui::game::detail::HandleWorldApplicationActivation(
        game_loop_.game_ui().lua_state(), window_focused_);
  }
  UpdateTextInputState();
}

void GlueClient::ReconcileWindowFocus() {
  if (window_ == nullptr) {
    return;
  }
  const bool platform_focused =
      (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
  if (platform_focused != window_focused_) {
    ApplyWindowFocusChange(platform_focused);
  }
}

void GlueClient::HandleEvent(const SDL_Event &event) {
  const std::uint32_t mouse_button_flag =
      (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP)
          ? WowMouseButtonBitmaskFromSdlButton(event.button.button)
          : 0u;
  if (mouse_button_flag != 0u) {
    auto& window_manager = openwow::platform::WindowManager::Get();
    if (event.type == SDL_MOUSEBUTTONDOWN) {
      window_manager.BeginMouseButtonCapture(mouse_button_flag);
    } else {
      window_manager.EndMouseButtonCapture(mouse_button_flag);
    }
  }

  if (const auto size_change = stock_window_event_state_.ConsumeClientSizeChange(event);
      size_change.has_value()) {

    layout_dirty_ = true;
    openwow::platform::WindowManager::Get().SetClientSize(size_change->width, size_change->height);
    RefreshLayout();
    return;
  }

  if (openwow::platform::StockWindowEventState::IsTerminationRequest(event)) {

    (void)openwow::core::EvtWindow_InvokeTerminationCallback();
    RequestApplicationQuit();
    return;
  }

  if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                                        event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)) {
    ApplyWindowFocusChange(event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED);
    return;
  }

  if (event.type == SDL_WINDOWEVENT &&
      (event.window.event == SDL_WINDOWEVENT_ENTER ||
       event.window.event == SDL_WINDOWEVENT_LEAVE)) {

    openwow::input::InputManager::Get().OnMouseEnterLeave(
        event.window.event == SDL_WINDOWEVENT_ENTER);
    return;
  }

  if (event.type == SDL_AUDIODEVICEADDED ||
      event.type == SDL_AUDIODEVICEREMOVED) {
    (void)sound_runtime_
        .RefreshEnumeratedDevicesAndReconcile(true);
    if (mode_ != UiMode::kInWorld) {
      FireGlueEvent("SOUND_DEVICE_UPDATE", {});
    }
    return;
  }

  if (event.type == SDL_JOYAXISMOTION && mode_ == UiMode::kInWorld) {
    (void)game_loop_.binding_input().JoystickAxisMotion(
        static_cast<std::uint32_t>(event.jaxis.axis),
        static_cast<std::int32_t>(event.jaxis.value));
    return;
  }

  if (mode_ == UiMode::kInWorld) {
    auto* const game_ui = game_loop_.game_ui().is_initialized() ? &game_loop_.game_ui() : nullptr;
    const SDL_Keymod modifier_state = SDL_GetModState();

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
      auto [ui_x, ui_y] = ResolveInWorldMouseButtonDispatchPosition(event.button);
      ScaleMouseToDrawable(window_, ui_x, ui_y);
      const bool ui_handled =
          game_ui && game_ui->input_router().HandleMouseDown(
                         static_cast<float>(ui_x), static_cast<float>(ui_y), 1);
      UpdateTextInputState();
      if (ui_handled) {
        return;
      }
      RunMouseButtonDownPrelude(4u, game_loop_, character_world_runtime_.session());

      openwow::input::InputManager::Get().SetMousePosition(ui_x, ui_y);
      openwow::input::InputManager::Get().RecordClickDown(
          openwow::input::MouseButton::Right, openwow::core::GameClock::GetTickCount32());
      UpdateInWorldMouseButtonState(SDL_BUTTON_RIGHT, true);
      const bool binding_handled = game_loop_.binding_input().MouseButtonDown(
          4u, static_cast<std::uint16_t>(modifier_state));

      if (!binding_handled && game_ui) {
        (void)openwow::ui::CallLuaGlobalIfFunction(
            game_ui->lua_state(), "TurnOrActionStart");
      }
      return;
    }
    if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_RIGHT) {
      auto [ui_x, ui_y] = ResolveInWorldMouseButtonDispatchPosition(event.button);
      ScaleMouseToDrawable(window_, ui_x, ui_y);
      const bool ui_handled =
          game_ui && game_ui->input_router().HandleMouseUp(
                         static_cast<float>(ui_x), static_cast<float>(ui_y), 1);
      UpdateTextInputState();
      if (!right_mouse_held_ && ui_handled) {
        return;
      }
      UpdateInWorldMouseButtonState(SDL_BUTTON_RIGHT, false);
      openwow::input::InputManager::Get().SetMousePosition(ui_x, ui_y);
      if (!left_mouse_held_ && !right_mouse_held_) {
        openwow::platform::WindowManager::Get().ClearCursorAnchor();
      }
      const bool binding_handled = game_loop_.binding_input().MouseButtonUp(
          4u, static_cast<std::uint16_t>(modifier_state));

      if (!binding_handled && game_ui) {
        (void)openwow::ui::CallLuaGlobalIfFunction(
            game_ui->lua_state(), "TurnOrActionStop");
      }
      return;
    }
    if (event.type == SDL_MOUSEMOTION) {
      if (left_mouse_held_ || right_mouse_held_) {

        return;
      }

      int mouse_x = event.motion.x;
      int mouse_y = event.motion.y;
      ScaleMouseToDrawable(window_, mouse_x, mouse_y);
      (void)openwow::input::DispatchInWorldMouseMotion(
          mouse_x, mouse_y, event.motion.xrel, event.motion.yrel, false);
      if (game_ui) {
        int ui_x = event.motion.x;
        int ui_y = event.motion.y;
        ScaleMouseToDrawable(window_, ui_x, ui_y);
        (void)game_ui->input_router().HandleMouseMove(
            static_cast<float>(ui_x), static_cast<float>(ui_y));
      }
      return;
    }

    if (event.type == SDL_MOUSEWHEEL) {
      if (game_ui && !left_mouse_held_ && !right_mouse_held_) {
        int ui_x = 0;
        int ui_y = 0;
        SDL_GetMouseState(&ui_x, &ui_y);
        ScaleMouseToDrawable(window_, ui_x, ui_y);
        if (game_ui->input_router().HandleMouseWheel(
                static_cast<float>(ui_x), static_cast<float>(ui_y),
                static_cast<float>(event.wheel.y))) {
          return;
        }
      }
      if (game_loop_.binding_input().MouseWheel(
              static_cast<std::int32_t>(event.wheel.y))) {
        return;
      }
      game_loop_.HandleScrollDelta(static_cast<float>(event.wheel.y));
      return;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
      auto [wx, wy] = ResolveInWorldMouseButtonDispatchPosition(event.button);
      ScaleMouseToDrawable(window_, wx, wy);
      const bool ui_handled = game_ui &&
                               game_ui->input_router().HandleMouseDown(
                                   static_cast<float>(wx),
                                   static_cast<float>(wy), 0);
      UpdateTextInputState();
      if (ui_handled) {
        return;
      }

      openwow::input::InputManager::Get().SetMousePosition(wx, wy);
      openwow::input::InputManager::Get().RecordClickDown(
          openwow::input::MouseButton::Left, openwow::core::GameClock::GetTickCount32());
      UpdateInWorldMouseButtonState(SDL_BUTTON_LEFT, true);
      const bool binding_handled = game_loop_.binding_input().MouseButtonDown(
          1u, static_cast<std::uint16_t>(modifier_state));
      if (!binding_handled && game_ui) {
        (void)openwow::ui::CallLuaGlobalIfFunction(
            game_ui->lua_state(), "CameraOrSelectOrMoveStart");
      }
      return;
    }
    if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
      auto [ui_x, ui_y] = ResolveInWorldMouseButtonDispatchPosition(event.button);
      ScaleMouseToDrawable(window_, ui_x, ui_y);
      const bool ui_handled =
          game_ui && game_ui->input_router().HandleMouseUp(
                         static_cast<float>(ui_x), static_cast<float>(ui_y), 0);
      UpdateTextInputState();
      if (!left_mouse_held_ && ui_handled) {
        return;
      }
      UpdateInWorldMouseButtonState(SDL_BUTTON_LEFT, false);
      openwow::input::InputManager::Get().SetMousePosition(ui_x, ui_y);
      if (!left_mouse_held_ && !right_mouse_held_) {
        openwow::platform::WindowManager::Get().ClearCursorAnchor();
      }
      const bool binding_handled = game_loop_.binding_input().MouseButtonUp(
          1u, static_cast<std::uint16_t>(modifier_state));
      const bool command_handled =
          !binding_handled && game_ui &&
          openwow::ui::CallLuaGlobalIfFunction(
              game_ui->lua_state(), "CameraOrSelectOrMoveStop");
      if (binding_handled) {
        return;
      }
      if (command_handled) {
        return;
      }
      const auto now_ms = openwow::core::GameClock::GetTickCount32();
      if (!openwow::input::InputManager::Get().HasDoubleClickElapsed(
              openwow::input::MouseButton::Left, now_ms)) {
        game_loop_.OnLeftClickWorld(static_cast<float>(ui_x), static_cast<float>(ui_y));
      }
      return;
    }
    if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
      if (mouse_button_flag == 0u) {
        return;
      }
      if (event.type == SDL_MOUSEBUTTONDOWN) {
        RunMouseButtonDownPrelude(
            mouse_button_flag, game_loop_, character_world_runtime_.session());
        (void)game_loop_.binding_input().MouseButtonDown(
            mouse_button_flag, static_cast<std::uint16_t>(modifier_state));
      } else {
        (void)game_loop_.binding_input().MouseButtonUp(
            mouse_button_flag, static_cast<std::uint16_t>(modifier_state));
      }
      return;
    }
  }

  if (event.type == SDL_TEXTINPUT) {
    if (mode_ == UiMode::kInWorld) {
      if (game_loop_.game_ui().is_initialized()) {
        (void)game_loop_.game_ui().input_router().HandleTextInput(
            event.text.text);
      }
    } else {
      HandleTextInput(event);
    }
    return;
  }

  if (event.type == SDL_MOUSEWHEEL && mode_ != UiMode::kInWorld) {
    float delta = static_cast<float>(event.wheel.y);
#if SDL_VERSION_ATLEAST(2, 0, 18)

    if (event.wheel.preciseY != 0.0f) {
      delta = event.wheel.preciseY;
    }
#endif
    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
      delta = -delta;
    }
    if (delta != 0.0f) {
      int mx = 0;
      int my = 0;
      SDL_GetMouseState(&mx, &my);
      ScaleMouseToDrawable(window_, mx, my);

      auto hit = glue_widgets_.HitTestTopmostVisibleWidget(mx, my);
      if (hit.has_value()) {

        bool handled =
            glue_widgets_.IsMouseWheelEnabled(hit->name)
            && glue_runtime_.HasWidgetScript(hit->name, "OnMouseWheel");
        if (handled) {
          (void)DispatchWidgetEvent(hit->name, "OnMouseWheel", hit->name + ".OnMouseWheel",
                                    {MakeLuaNumber(static_cast<double>(delta))});
        } else {

          std::string ancestor = hit->parent;
          for (int depth = 0; depth < 32 && !ancestor.empty(); ++depth) {
            const auto aw = glue_widgets_.GetWidget(ancestor);
            if (!aw.has_value())
              break;
            if (glue_widgets_.IsMouseWheelEnabled(ancestor)
                && glue_widgets_.WidgetHitRectContainsPoint(ancestor, mx, my)
                && glue_runtime_.HasWidgetScript(ancestor, "OnMouseWheel")) {
              (void)DispatchWidgetEvent(ancestor, "OnMouseWheel", ancestor + ".OnMouseWheel",
                                        {MakeLuaNumber(static_cast<double>(delta))});
              break;
            }
            ancestor = aw->parent;
          }
        }
      }
    }
    return;
  }

  if (event.type == SDL_MOUSEMOTION && !dragging_slider_name_.empty()) {
    int mouse_x = event.motion.x;
    int mouse_y = event.motion.y;
    ScaleMouseToDrawable(window_, mouse_x, mouse_y);
    const auto sw = glue_widgets_.GetWidget(dragging_slider_name_);
    if (sw.has_value()) {
      const auto new_value =
          detail::SliderValueAtPoint(glue_widgets_, *sw, mouse_x, mouse_y);
      if (!new_value.has_value()) {
        return;
      }
      const double previous_value =
          glue_widgets_.GetValue(dragging_slider_name_);
      glue_widgets_.SetValue(dragging_slider_name_, *new_value);
      const double current_value =
          glue_widgets_.GetValue(dragging_slider_name_);
      if (current_value != previous_value) {
        (void)DispatchWidgetEvent(dragging_slider_name_, "OnValueChanged",
                                  dragging_slider_name_ + ".OnValueChanged",
                                  {MakeLuaNumber(current_value)});
      }
    }
    return;
  }

  if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
    HandleMouseDown(event);
    return;
  }

  if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
    HandleMouseUp(event);
    return;
  }

  if (mode_ != UiMode::kInWorld && glue_runtime_.IsMoviePlaying() &&
      (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)) {
    if (event.type == SDL_KEYUP) {
      glue_runtime_.DispatchActiveMovieKeyUp(
          openwow::game::actions::bindings::adapters::platform::SdlScancodeToBaseKey(
              event.key.keysym.scancode));
    }
    return;
  }

  if (event.type == SDL_KEYDOWN) {
    HandleKeyDown(event);
    return;
  }

  if (event.type == SDL_KEYUP && mode_ == UiMode::kInWorld) {
    if (game_loop_.game_ui().is_initialized() &&
        game_loop_.game_ui().input_router().HandleKeyUp(
            static_cast<std::uint32_t>(event.key.keysym.scancode))) {
      UpdateTextInputState();
      return;
    }

    const std::string key_name =
        openwow::game::actions::bindings::adapters::platform::SdlScancodeToBindingChord(
            event.key.keysym.scancode,
            static_cast<std::uint16_t>(event.key.keysym.mod));
    if (!key_name.empty() &&
        game_loop_.binding_input().KeyUp(key_name)) {
      return;
    }
    return;
  }
}

void GlueClient::HandleTextInput(const SDL_Event &event) {
  UpdateFocusedEditBoxInputLanguage();

  const auto focused = FocusedEditbox();
  if (focused.empty())
    return;

  const auto w = glue_widgets_.GetWidget(focused);
  const int max_letters = w.has_value() ? w->max_letters : -1;

  const std::string before = glue_widgets_.GetText(focused);
  const auto sel = glue_widgets_.GetEditSelectionBytes(focused);
  int cursor = glue_widgets_.GetEditCursorByte(focused);
  cursor = openwow::text::ClampUtf8ByteIndex(before, cursor);

  std::string base = before;
  if (sel.first >= 0 && sel.second >= 0 && sel.first < sel.second) {
    const int s = openwow::text::ClampUtf8ByteIndex(base, sel.first);
    const int e = openwow::text::ClampUtf8ByteIndex(base, sel.second);
    if (e > s) {
      base.erase(static_cast<std::size_t>(s), static_cast<std::size_t>(e - s));
      cursor = s;
    }
  }

  const std::string prefix = base.substr(0, static_cast<std::size_t>(cursor));
  const std::string suffix = base.substr(static_cast<std::size_t>(cursor));
  std::string insert = event.text.text;
  if (max_letters > 0) {
    const int used =
        openwow::text::Utf8CodepointCount(prefix) + openwow::text::Utf8CodepointCount(suffix);
    const int remaining = max_letters - used;
    insert = openwow::text::Utf8TakeCodepoints(insert, remaining);
  }
  if (insert.empty())
    return;

  std::string next;
  next.reserve(prefix.size() + insert.size() + suffix.size());
  next.append(prefix);
  next.append(insert);
  next.append(suffix);

  glue_widgets_.SetText(focused, next);
  glue_widgets_.SetEditCursorByte(focused, cursor + static_cast<int>(insert.size()));
  glue_widgets_.ClearEditSelection(focused);
  (void)DispatchWidgetEvent(focused, "OnTextChanged", focused + ".OnTextChanged",
                            {MakeLuaBool(true)});
  SyncLoginModelFromEditbox(focused, next);
}

void GlueClient::HandleMouseDown(const SDL_Event &event) {
  int mouse_x = event.button.x;
  int mouse_y = event.button.y;
  ScaleMouseToDrawable(window_, mouse_x, mouse_y);

  if (mode_ != UiMode::kInWorld) {
    const auto target = glue_widgets_.HitTestMouseTarget(mouse_x, mouse_y);
    if (trace_input_) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "UI click: x=" + std::to_string(mouse_x) + " y=" + std::to_string(mouse_y) +
              " target=" + (target.has_value() ? target->name : std::string("<none>")) +
              " targetKind=" + (target.has_value() ? target->kind : std::string("<none>")));
    }
    if (target.has_value()) {
      const auto kind = ToLowerAscii(target->kind);
      if (!target->enabled) {
        return;
      }
      if (kind == "editbox") {
        openwow::ui::glue::DispatchEditBoxMouseDownWithFocusTransfer(
            glue_widgets_.focused_widget(), target->name,
            [&] {
              (void)glue_runtime_.RunInlineScript("self:SetFocus()", "SetFocus",
                                                  target->name + ".__openwow_SetFocus",
                                                  target->name, {});
            },
            [&] {
              if (const auto cursor_byte =
                      glue_renderer_.ResolveEditBoxCursorByteAtPixel(
                          glue_widgets_, target->name, mouse_x);
                  cursor_byte.has_value()) {
                glue_widgets_.SetEditCursorByte(target->name, *cursor_byte);
                glue_widgets_.ClearEditSelection(target->name);
              }
              (void)DispatchWidgetEvent(target->name, "OnMouseDown",
                                        target->name + ".OnMouseDown",
                                        {MakeLuaString("LeftButton")});
            });
        UpdateTextInputState();
        return;
      }

      if (const auto focused = FocusedEditbox(); !focused.empty()) {
        (void)glue_runtime_.RunInlineScript("self:ClearFocus()", "ClearFocus",
                                            focused + ".__openwow_ClearFocus", focused, {});
        UpdateTextInputState();
      }

      if (kind == "slider") {
        mouse_capture_widget_name_ = target->name;
        (void)DispatchWidgetEvent(target->name, "OnMouseDown",
                                  target->name + ".OnMouseDown",
                                  {MakeLuaString("LeftButton")});
        dragging_slider_name_ = target->name;
        if (const auto new_value = detail::SliderValueAtPoint(
                glue_widgets_, *target, mouse_x, mouse_y);
            new_value.has_value()) {
          const double previous_value =
              glue_widgets_.GetValue(target->name);
          glue_widgets_.SetValue(target->name, *new_value);
          const double current_value = glue_widgets_.GetValue(target->name);
          if (current_value != previous_value) {
            (void)DispatchWidgetEvent(target->name, "OnValueChanged",
                                      target->name + ".OnValueChanged",
                                      {MakeLuaNumber(current_value)});
          }
        }
        return;
      }

      if (kind == "button" || kind == "checkbutton") {
        pressed_widget_name_ = target->name;
        (void)DispatchWidgetEvent(target->name, "OnMouseDown", target->name + ".OnMouseDown",
                                  {MakeLuaString("LeftButton")});
        if (glue_widgets_.IsClickRegistered(target->name, 1u, true)) {
          (void)DispatchButtonClick(target->name, "LeftButton", true);
        }
        if (!glue_widgets_.HighlightLocked(target->name) &&
            !openwow::text::EqualsIgnoreCaseAscii(
                glue_widgets_.GetButtonState(target->name), "DISABLED")) {
          glue_widgets_.SetButtonState(target->name, "PUSHED");
        }
        return;
      }

      mouse_capture_widget_name_ = target->name;
      (void)DispatchWidgetEvent(target->name, "OnMouseDown",
                                target->name + ".OnMouseDown",
                                {MakeLuaString("LeftButton")});
      return;
    }

    if (const auto focused = FocusedEditbox(); !focused.empty()) {
      (void)glue_runtime_.RunInlineScript("self:ClearFocus()", "ClearFocus",
                                          focused + ".__openwow_ClearFocus", focused, {});
      UpdateTextInputState();
    }
  }

  if (mode_ == UiMode::kRealmDialog) {
    if (const auto enter_button =
            FindFirstVisibleWidget(glue_widgets_, {"RealmListOkButton"});
        enter_button.has_value() && PointInWidget(*enter_button, mouse_x, mouse_y)) {
      EnterSelectedRealm();
      return;
    }

    const auto list = FindFirstVisibleWidget(glue_widgets_, {"RealmListScrollFrame"});
    if (list.has_value() && PointInWidget(*list, mouse_x, mouse_y)) {
      const int clicked_index = (mouse_y - list->y) / kRealmListRowHeightPx;
      const int realm_count = static_cast<int>(realm_screen_.realms().size());
      if (clicked_index >= 0 && clicked_index < realm_count) {
        const int delta = clicked_index - static_cast<int>(realm_screen_.selected_index());
        realm_screen_.MoveSelection(delta);
        if (event.button.clicks >= 2) {
          EnterSelectedRealm();
        }
      }
    }
  }

  if (mode_ == UiMode::kCharacterSelect) {
    if (const auto create_button = FindFirstVisibleWidget(
            glue_widgets_, {"CharSelectCreateCharacterButton", "CharCreateCharacterButton",
                            "CreateCharacterButton"});
        create_button.has_value() && PointInWidget(*create_button, mouse_x, mouse_y)) {
      ShowCharacterCreate();
      return;
    }

    if (const auto enter_button = FindFirstVisibleWidget(
            glue_widgets_, {"CharSelectEnterWorldButton", "EnterWorldButton"});
        enter_button.has_value() && PointInWidget(*enter_button, mouse_x, mouse_y)) {
      EnterSelectedCharacter();
      return;
    }

    const auto list = FindFirstVisibleWidget(glue_widgets_, {"CharacterList", "CharSelectCharacterName"});
    if (list.has_value() && PointInWidget(*list, mouse_x, mouse_y)) {
      const int clicked_index = (mouse_y - list->y) / kCharacterSelectRowHeightPx;
      const int character_count = static_cast<int>(character_screen_.characters().size());
      if (clicked_index >= 0 && clicked_index < character_count) {
        const int delta = clicked_index - static_cast<int>(character_screen_.selected_index());
        character_screen_.MoveSelection(delta);
        if (event.button.clicks >= 2) {
          EnterSelectedCharacter();
        }
      }
    }
  }
}

void GlueClient::HandleMouseUp(const SDL_Event &event) {

  if (!dragging_slider_name_.empty()) {
    const std::string released = dragging_slider_name_;
    dragging_slider_name_.clear();
    mouse_capture_widget_name_.clear();
    (void)DispatchWidgetEvent(released, "OnMouseUp", released + ".OnMouseUp",
                              {MakeLuaString("LeftButton")});
    return;
  }

  if (mode_ != UiMode::kInWorld && !mouse_capture_widget_name_.empty()) {
    const std::string released = mouse_capture_widget_name_;
    mouse_capture_widget_name_.clear();
    (void)DispatchWidgetEvent(released, "OnMouseUp", released + ".OnMouseUp",
                              {MakeLuaString("LeftButton")});
    return;
  }

  if (mode_ != UiMode::kInWorld && !pressed_widget_name_.empty()) {
    const std::string released = pressed_widget_name_;
    pressed_widget_name_.clear();

    const bool was_pushed = openwow::text::EqualsIgnoreCaseAscii(
        glue_widgets_.GetButtonState(released), "PUSHED");
    (void)DispatchWidgetEvent(released, "OnMouseUp", released + ".OnMouseUp",
                              {MakeLuaString("LeftButton")});

    int mouse_x = event.button.x;
    int mouse_y = event.button.y;
    ScaleMouseToDrawable(window_, mouse_x, mouse_y);
    if (glue_widgets_.WidgetContainsInputPoint(released, mouse_x, mouse_y)) {
      const auto released_widget = glue_widgets_.GetWidget(released);
      const bool enabled = released_widget.has_value() ? released_widget->enabled : true;
      if (enabled && was_pushed && glue_widgets_.IsClickRegistered(released, 1u, false)) {
        const std::uint32_t now = SDL_GetTicks();
        auto &last_click = button_last_click_time_ms_[released];
        if (last_click != 0u && now - last_click <= 300u &&
            glue_runtime_.HasWidgetScript(released, "OnDoubleClick")) {
          (void)DispatchWidgetEvent(released, "OnDoubleClick", released + ".OnDoubleClick",
                                    {MakeLuaString("LeftButton")});
          last_click = 0u;
        } else {
          (void)DispatchButtonClick(released, "LeftButton", false);
          last_click = now;
        }

        if (mode_ == UiMode::kCharacterSelect &&
            (released == "CharSelectCreateCharacterButton" ||
             released == "CharCreateCharacterButton" ||
             released == "CreateCharacterButton")) {
          ShowCharacterCreate(false);
        } else if (mode_ == UiMode::kCharacterSelect &&
                   (released == "CharSelectEnterWorldButton" ||
                    released == "EnterWorldButton")) {
          EnterSelectedCharacter(false);
        }
      }
    }
    if (!glue_widgets_.HighlightLocked(released) &&
        !openwow::text::EqualsIgnoreCaseAscii(glue_widgets_.GetButtonState(released),
                                               "DISABLED")) {
      glue_widgets_.SetButtonState(released, "NORMAL");
    }
  } else if (!pressed_widget_name_.empty()) {
    const std::string released = pressed_widget_name_;
    pressed_widget_name_.clear();
    glue_widgets_.SetButtonState(released, "NORMAL");
    (void)DispatchWidgetEvent(released, "OnMouseUp", released + ".OnMouseUp",
                              {MakeLuaString("LeftButton")});
  }
}

void GlueClient::HandleKeyDown(const SDL_Event &event) {
  const SDL_Scancode scancode = event.key.keysym.scancode;

  if ((event.key.keysym.mod & KMOD_ALT) != 0 &&
      (scancode == SDL_SCANCODE_RETURN ||
       scancode == SDL_SCANCODE_KP_ENTER)) {
    const auto next_flag = is_fullscreen_desktop_ ? 0U : SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (SDL_SetWindowFullscreen(window_, next_flag) == 0) {
      is_fullscreen_desktop_ = !is_fullscreen_desktop_;

      RefreshLayout();
    } else {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         std::string("Fullscreen toggle failed: ") + SDL_GetError());
    }
    return;
  }

  const bool ctrl_down = (event.key.keysym.mod & KMOD_CTRL) != 0;
  const bool shift_down = (event.key.keysym.mod & KMOD_SHIFT) != 0;

  if (mode_ == UiMode::kInWorld && ctrl_down &&
      scancode == SDL_SCANCODE_V &&
      game_loop_.game_ui().is_initialized() &&
      !game_loop_.game_ui().input_router().focused_frame_name().empty()) {
    if (const auto clipboard_text = openwow::platform::TryGetSystemClipboardText();
        clipboard_text.has_value() && !clipboard_text->empty()) {
      (void)game_loop_.game_ui().input_router().HandleTextInput(
          clipboard_text->c_str());
    }
    return;
  }

  if (mode_ == UiMode::kInWorld && game_loop_.game_ui().is_initialized() &&
      game_loop_.game_ui().input_router().HandleKeyDown(
          static_cast<std::uint32_t>(event.key.keysym.scancode), shift_down,
          ctrl_down)) {
    UpdateTextInputState();
    return;
  }

  if (mode_ != UiMode::kInWorld) {
    const auto focused = FocusedEditbox();
    const bool editing = !focused.empty();

    if (editing) {
      UpdateFocusedEditBoxInputLanguage();
    }

    if (ctrl_down && scancode == SDL_SCANCODE_V && editing) {
      HandleClipboardPaste(focused);
      return;
    }

    if (scancode == SDL_SCANCODE_TAB) {
      const std::string focus_before_handler = focused;
      if (editing) {
        (void)DispatchWidgetEvent(focused, "OnTabPressed", focused + ".OnTabPressed", {});
      }

      if (FocusedEditbox() != focus_before_handler) {
        UpdateTextInputState();
        return;
      }
      if (mode_ != UiMode::kLogin) {
        UpdateTextInputState();
        return;
      }
      const auto now_focused = FocusedEditbox();
      const auto username_widget = FindUsernameWidget();
      const auto password_widget = FindPasswordWidget();
      if (now_focused.empty() || now_focused == password_widget) {
        if (glue_widgets_.GetWidget(username_widget).has_value()) {
          (void)glue_runtime_.RunInlineScript("self:SetFocus()", "SetFocus",
                                              username_widget + ".__openwow_SetFocus",
                                              username_widget, {});
        }
      } else if (now_focused == username_widget) {
        if (glue_widgets_.GetWidget(password_widget).has_value()) {
          (void)glue_runtime_.RunInlineScript("self:SetFocus()", "SetFocus",
                                              password_widget + ".__openwow_SetFocus",
                                              password_widget, {});
        }
      } else {
        if (glue_widgets_.GetWidget(username_widget).has_value()) {
          (void)glue_runtime_.RunInlineScript("self:SetFocus()", "SetFocus",
                                              username_widget + ".__openwow_SetFocus",
                                              username_widget, {});
        }
      }
      UpdateTextInputState();
      return;
    }

    if (scancode == SDL_SCANCODE_BACKSPACE && editing) {
      HandleEditBackspace(focused);
      return;
    }

    if ((scancode == SDL_SCANCODE_DELETE ||
         scancode == SDL_SCANCODE_KP_PERIOD) &&
        editing) {
      HandleEditDelete(focused);
      return;
    }

    if ((scancode == SDL_SCANCODE_LEFT || scancode == SDL_SCANCODE_RIGHT ||
         scancode == SDL_SCANCODE_HOME || scancode == SDL_SCANCODE_END) &&
        editing) {
      const std::string value = glue_widgets_.GetText(focused);
      int cursor = glue_widgets_.GetEditCursorByte(focused);
      cursor = openwow::text::ClampUtf8ByteIndex(value, cursor);

      if (scancode == SDL_SCANCODE_LEFT) {
        cursor = openwow::text::Utf8PrevByteIndex(value, cursor);
      } else if (scancode == SDL_SCANCODE_RIGHT) {
        cursor = openwow::text::Utf8NextByteIndex(value, cursor);
      } else if (scancode == SDL_SCANCODE_HOME) {
        cursor = 0;
      } else {
        cursor = static_cast<int>(value.size());
      }

      glue_widgets_.SetEditCursorByte(focused, cursor);
      glue_widgets_.ClearEditSelection(focused);
      return;
    }

    if ((scancode == SDL_SCANCODE_RETURN ||
         scancode == SDL_SCANCODE_KP_ENTER) &&
        editing) {
      (void)DispatchWidgetEvent(focused, "OnEnterPressed", focused + ".OnEnterPressed", {});
      UpdateTextInputState();
      return;
    }

    if (scancode == SDL_SCANCODE_ESCAPE && editing) {
      (void)DispatchWidgetEvent(focused, "OnEscapePressed", focused + ".OnEscapePressed", {});
      if (!FocusedEditbox().empty()) {
        (void)glue_runtime_.RunInlineScript("self:ClearFocus()", "ClearFocus",
                                            focused + ".__openwow_ClearFocus", focused, {});
      }
      UpdateTextInputState();
      return;
    }

    if (editing)
      return;
  }

  if (mode_ == UiMode::kLogin &&
      (scancode == SDL_SCANCODE_RETURN ||
       scancode == SDL_SCANCODE_KP_ENTER)) {
    if (glue_load_.ok) {
      const auto login_button = FindFirstVisibleWidget(
          glue_widgets_, {"AccountLoginLoginButton", "AccountLoginButton", "LoginButton"});
      if (login_button.has_value()) {
        (void)DispatchWidgetEvent(login_button->name, "OnClick", login_button->name + ".OnClick",
                                  {MakeLuaString("LeftButton")});
      }
    } else {
      DoLoginAttempt();
    }
  } else if (mode_ == UiMode::kLogin &&
             ((ctrl_down && scancode == SDL_SCANCODE_R) ||
              scancode == SDL_SCANCODE_F5)) {
    ReloadLoginResources();
  } else if (mode_ == UiMode::kRealmDialog && scancode == SDL_SCANCODE_UP) {
    realm_screen_.MoveSelection(-1);
  } else if (mode_ == UiMode::kRealmDialog && scancode == SDL_SCANCODE_DOWN) {
    realm_screen_.MoveSelection(1);
  } else if (mode_ == UiMode::kRealmDialog && scancode == SDL_SCANCODE_RETURN) {
    EnterSelectedRealm();
  } else if (mode_ == UiMode::kCharacterSelect && scancode == SDL_SCANCODE_UP) {
    character_screen_.MoveSelection(-1);
  } else if (mode_ == UiMode::kCharacterSelect && scancode == SDL_SCANCODE_DOWN) {
    character_screen_.MoveSelection(1);
  } else if (mode_ == UiMode::kCharacterSelect && scancode == SDL_SCANCODE_N) {
    ShowCharacterCreate();
  } else if (mode_ == UiMode::kCharacterSelect && scancode == SDL_SCANCODE_RETURN) {
    EnterSelectedCharacter();
  } else if (scancode == SDL_SCANCODE_ESCAPE) {
    if (mode_ == UiMode::kCharacterCreate) {
      (void)glue_runtime_.DispatchFirstAvailableWithArgs({"SetGlueScreen"}, "GlueParent",
                                                         {"charselect"}, false);
      SetMode(UiMode::kCharacterSelect);
    } else if (mode_ == UiMode::kCharacterSelect) {
      (void)DispatchWidgetEvent("CharacterSelect", "OnHide", "CharacterSelect.OnHide", {});
      (void)DispatchWidgetEvent("RealmList", "OnShow", "RealmList.OnShow", {});
      SetMode(UiMode::kRealmDialog);
    } else if (mode_ == UiMode::kRealmDialog) {
      (void)DispatchWidgetEvent("RealmList", "OnHide", "RealmList.OnHide", {});
      GlueFlowContext cancel_ctx;
      cancel_ctx.realm_session = &realm_runtime_.session;
      CancelGlueFlowNetworkOperations(cancel_ctx, glue_flow_state_);
      glue_flow_state_.phase = GlueFlowState::Phase::kIdle;
      realm_runtime_.session.Disconnect();
      game_state_.connected = false;
      SetMode(UiMode::kLogin);
    } else if (mode_ == UiMode::kInWorld) {

      if (auto* const session = character_world_runtime_.session();
          session != nullptr && game_loop_.cinematic_player().IsPlaying()) {
        game_loop_.cinematic_player().Skip(*session);
        return;
      }

      if (game_loop_.targeting().HasTarget()) {
        game_loop_.targeting().ClearTarget();
      } else {
        if (lua_State *L = game_loop_.game_ui().lua_state()) {
          (void)openwow::ui::CallLuaGlobalIfFunction(L, "ToggleGameMenu");
        }
      }
    } else {
      running_ = false;
    }
  } else if (mode_ == UiMode::kInWorld) {

    const std::string key_name =
        openwow::game::actions::bindings::adapters::platform::SdlKeyDownToBindingChord(
            scancode,
            static_cast<std::uint16_t>(event.key.keysym.mod),
            event.key.repeat != 0);
    if (!key_name.empty() &&
        game_loop_.binding_input().KeyDown(key_name)) {
      return;
    }
  }
}

void GlueClient::HandleClipboardPaste(const std::string &focused) {
  const auto clipboard_text = openwow::platform::TryGetSystemClipboardText();
  if (!clipboard_text.has_value() || clipboard_text->empty()) {
    return;
  }

  const auto w = glue_widgets_.GetWidget(focused);
  const int max_letters = w.has_value() ? w->max_letters : -1;
  const std::string before = glue_widgets_.GetText(focused);
  const auto sel = glue_widgets_.GetEditSelectionBytes(focused);
  int cursor = glue_widgets_.GetEditCursorByte(focused);
  cursor = openwow::text::ClampUtf8ByteIndex(before, cursor);

  std::string base = before;
  if (sel.first >= 0 && sel.second >= 0 && sel.first < sel.second) {
    const int s = openwow::text::ClampUtf8ByteIndex(base, sel.first);
    const int e = openwow::text::ClampUtf8ByteIndex(base, sel.second);
    if (e > s) {
      base.erase(static_cast<std::size_t>(s), static_cast<std::size_t>(e - s));
      cursor = s;
    }
  }

  const std::string prefix = base.substr(0, static_cast<std::size_t>(cursor));
  const std::string suffix = base.substr(static_cast<std::size_t>(cursor));
  std::string insert = *clipboard_text;
  if (max_letters > 0) {
    const int used =
        openwow::text::Utf8CodepointCount(prefix) + openwow::text::Utf8CodepointCount(suffix);
    const int remaining = max_letters - used;
    insert = openwow::text::Utf8TakeCodepoints(insert, remaining);
  }
  if (insert.empty()) {
    return;
  }

  std::string next;
  next.reserve(prefix.size() + insert.size() + suffix.size());
  next.append(prefix);
  next.append(insert);
  next.append(suffix);
  if (next == before) {
    return;
  }

  glue_widgets_.SetText(focused, next);
  glue_widgets_.SetEditCursorByte(focused, cursor + static_cast<int>(insert.size()));
  glue_widgets_.ClearEditSelection(focused);
  (void)DispatchWidgetEvent(focused, "OnTextChanged", focused + ".OnTextChanged",
                            {MakeLuaBool(true)});
  SyncLoginModelFromEditbox(focused, next);
}

void GlueClient::HandleEditBackspace(const std::string &focused) {
  const std::string before = glue_widgets_.GetText(focused);
  const auto sel = glue_widgets_.GetEditSelectionBytes(focused);
  int cursor = glue_widgets_.GetEditCursorByte(focused);
  cursor = openwow::text::ClampUtf8ByteIndex(before, cursor);
  std::string value = before;

  if (sel.first >= 0 && sel.second >= 0 && sel.first < sel.second) {
    const int s = openwow::text::ClampUtf8ByteIndex(value, sel.first);
    const int e = openwow::text::ClampUtf8ByteIndex(value, sel.second);
    if (e > s) {
      value.erase(static_cast<std::size_t>(s), static_cast<std::size_t>(e - s));
      cursor = s;
    }
  } else if (cursor > 0) {
    const int prev = openwow::text::Utf8PrevByteIndex(value, cursor);
    if (cursor > prev) {
      value.erase(static_cast<std::size_t>(prev), static_cast<std::size_t>(cursor - prev));
      cursor = prev;
    }
  } else {
    return;
  }

  glue_widgets_.SetText(focused, value);
  glue_widgets_.SetEditCursorByte(focused, cursor);
  glue_widgets_.ClearEditSelection(focused);
  (void)DispatchWidgetEvent(focused, "OnTextChanged", focused + ".OnTextChanged",
                            {MakeLuaBool(true)});
  SyncLoginModelFromEditbox(focused, value);
}

void GlueClient::HandleEditDelete(const std::string &focused) {
  const std::string before = glue_widgets_.GetText(focused);
  const auto sel = glue_widgets_.GetEditSelectionBytes(focused);
  int cursor = glue_widgets_.GetEditCursorByte(focused);
  cursor = openwow::text::ClampUtf8ByteIndex(before, cursor);
  std::string value = before;

  if (sel.first >= 0 && sel.second >= 0 && sel.first < sel.second) {
    const int s = openwow::text::ClampUtf8ByteIndex(value, sel.first);
    const int e = openwow::text::ClampUtf8ByteIndex(value, sel.second);
    if (e > s) {
      value.erase(static_cast<std::size_t>(s), static_cast<std::size_t>(e - s));
      cursor = s;
    }
  } else if (cursor < static_cast<int>(value.size())) {
    const int next = openwow::text::Utf8NextByteIndex(value, cursor);
    if (next > cursor) {
      value.erase(static_cast<std::size_t>(cursor), static_cast<std::size_t>(next - cursor));
    } else {
      return;
    }
  } else {
    return;
  }

  glue_widgets_.SetText(focused, value);
  glue_widgets_.SetEditCursorByte(focused, cursor);
  glue_widgets_.ClearEditSelection(focused);
  (void)DispatchWidgetEvent(focused, "OnTextChanged", focused + ".OnTextChanged",
                            {MakeLuaBool(true)});
  SyncLoginModelFromEditbox(focused, value);
}

void GlueClient::SyncLoginModelFromEditbox(const std::string& editbox,
                                           const std::string& value) {

  if (IsUsernameEditbox(editbox)) {
    login_screen_.SetUsername(value);
  } else if (IsPasswordEditbox(editbox)) {
    login_screen_.SetPassword(value);
  }
}

}

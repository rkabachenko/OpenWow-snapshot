
#include "openwow/input/input_manager.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/platform/window/window_manager.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <optional>
#include <unordered_map>

namespace openwow::input {

namespace {

WowMouseProbeCallback g_wow_mouse_probe_callback = nullptr;
WowMouseEnumerationCallback g_wow_mouse_enumeration_callback = nullptr;
WowMouseButtonStateSink g_wow_mouse_button_state_sink = nullptr;
void* g_wow_mouse_button_state_context = nullptr;

constexpr std::uint32_t MouseButtonToFlag(const MouseButton button) {
    const auto idx = static_cast<std::uint32_t>(button);
    if (idx >= kMouseButtonMax) return 0;
    return 1u << idx;
}

std::optional<MouseButton> MouseButtonFromFlag(const std::uint32_t button_flag) {
    if (button_flag == 0u) return std::nullopt;
    const auto idx = static_cast<std::size_t>(std::countr_zero(button_flag));
    if (idx >= kMouseButtonMax) return std::nullopt;
    return static_cast<MouseButton>(idx);
}

void ApplyMouseButtonFlag(MouseState& state, const std::uint32_t button_flag,
                          const bool pressed) {
    if (const auto mapped_button = MouseButtonFromFlag(button_flag);
        mapped_button.has_value()) {
        const auto index = static_cast<std::size_t>(*mapped_button);
        if (index < state.buttons.size()) {
            state.buttons[index] = pressed;
        }
    }
}

}

static uint32_t GetNowMs() {
    using Clock = std::chrono::steady_clock;
    static const auto kEpoch = Clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - kEpoch).count());
}

std::uint32_t GetCurrentMessageTimestamp() {
    return openwow::core::GameClock::GetTickCount32();
}

InputManager& InputManager::Get() {
    static InputManager instance;
    return instance;
}

void InputManager::BeginFrame() {
    std::lock_guard<std::mutex> lock(mutex_);
    keys_previous_ = keys_current_;
    prev_mouse_    = mouse_;
    prev_mouse_button_flags_ = mouse_button_flags_;
    mouse_.scroll_delta = 0;
    mouse_dx_ = 0;
    mouse_dy_ = 0;
    frame_text_input_.clear();
}

void InputManager::EndFrame() {

}

void SetWowMouseProbeCallback(const WowMouseProbeCallback callback) {
    g_wow_mouse_probe_callback = callback;
}

void SetWowMouseEnumerationCallback(
    const WowMouseEnumerationCallback callback) {
    g_wow_mouse_enumeration_callback = callback;
}

void EnumerateWowMouseDevices(std::vector<WowMouseEnumeratedDevice>& devices) {
    devices.clear();
    if (g_wow_mouse_enumeration_callback != nullptr) {
        g_wow_mouse_enumeration_callback(devices);
        return;
    }

    if (g_wow_mouse_probe_callback != nullptr && g_wow_mouse_probe_callback()) {
        devices.push_back({
            .devinst = 1u,
            .has_button_report_interface = true,
            .has_auxiliary_interface = true,
        });
    }
}

bool DetectWowMouse() {
    std::vector<WowMouseEnumeratedDevice> devices;
    EnumerateWowMouseDevices(devices);
    return std::any_of(
        devices.begin(), devices.end(),
        [](const WowMouseEnumeratedDevice& device) {
            return device.has_button_report_interface ||
                   device.has_auxiliary_interface;
        });
}

void SetWowMouseButtonStateSink(const WowMouseButtonStateSink sink,
                                void* const context) {
    g_wow_mouse_button_state_sink = sink;
    g_wow_mouse_button_state_context = context;
}

void DispatchWowMouseButtonState(const std::uint32_t raw_button_state) {
    if (g_wow_mouse_button_state_sink != nullptr) {
        g_wow_mouse_button_state_sink(g_wow_mouse_button_state_context,
                                      raw_button_state);
    }
}

bool InputManager::IsKeyDown(uint32_t keyCode) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (keyCode >= kMaxKeys) return false;
    return keys_current_[keyCode];
}

bool InputManager::IsKeyPressed(uint32_t keyCode) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (keyCode >= kMaxKeys) return false;
    return keys_current_[keyCode] && !keys_previous_[keyCode];
}

bool InputManager::IsKeyReleased(uint32_t keyCode) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (keyCode >= kMaxKeys) return false;
    return !keys_current_[keyCode] && keys_previous_[keyCode];
}

KeyModifiers InputManager::GetModifiers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    KeyModifiers mod;

    mod.shift = keys_current_[225] || keys_current_[229];
    mod.ctrl  = keys_current_[224] || keys_current_[228];
    mod.alt   = keys_current_[226] || keys_current_[230];
    return mod;
}

const MouseState& InputManager::GetMouse() const {

    return mouse_;
}

bool InputManager::IsMouseButtonDown(MouseButton btn) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto idx = static_cast<size_t>(btn);
    if (idx >= mouse_.buttons.size()) return false;
    return mouse_.buttons[idx];
}

bool InputManager::IsMouseButtonPressed(MouseButton btn) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto idx = static_cast<size_t>(btn);
    if (idx >= mouse_.buttons.size()) return false;
    return mouse_.buttons[idx] && !prev_mouse_.buttons[idx];
}

bool InputManager::IsMouseButtonReleased(MouseButton btn) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto idx = static_cast<size_t>(btn);
    if (idx >= mouse_.buttons.size()) return false;
    return !mouse_.buttons[idx] && prev_mouse_.buttons[idx];
}

bool InputManager::IsMouseButtonFlagDown(const uint32_t button_flag) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return button_flag != 0u && (mouse_button_flags_ & button_flag) != 0u;
}

uint32_t InputManager::GetMouseButtonFlags() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mouse_button_flags_;
}

bool InputManager::IsMouseLooking() const { return mouse_looking_; }

void InputManager::SetMouseLooking(bool looking) { mouse_looking_ = looking; }

int32_t InputManager::GetMouseDeltaX() const { return mouse_dx_; }
int32_t InputManager::GetMouseDeltaY() const { return mouse_dy_; }

void InputManager::StartTextInput() { text_input_ = true; }
void InputManager::StopTextInput()  { text_input_ = false; }
bool InputManager::IsTextInputActive() const { return text_input_; }

const std::string& InputManager::GetTextInput() const { return frame_text_input_; }

void InputManager::OnKeyDown(uint32_t , uint32_t scanCode, bool ) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (scanCode < kMaxKeys) {
        keys_current_[scanCode] = true;
    }
}

void InputManager::OnKeyUp(uint32_t , uint32_t scanCode) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (scanCode < kMaxKeys) {
        keys_current_[scanCode] = false;
    }
}

void InputManager::OnTextInput(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_text_input_ += text;
}

void InputManager::OnMouseMove(int32_t x, int32_t y) {
    std::lock_guard<std::mutex> lock(mutex_);
    mouse_dx_ += x - mouse_.x;
    mouse_dy_ += y - mouse_.y;
    mouse_.x = x;
    mouse_.y = y;
}

void InputManager::OnMouseButton(MouseButton btn, bool pressed) {
    const auto idx = static_cast<size_t>(btn);
    if (idx >= mouse_.buttons.size()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    mouse_.buttons[idx] = pressed;
    const auto button_flag = MouseButtonToFlag(btn);
    if (pressed) {
        mouse_button_flags_ |= button_flag;
    } else {
        mouse_button_flags_ &= ~button_flag;
    }
}

void InputManager::OnMouseWheel(int32_t delta) {
    std::lock_guard<std::mutex> lock(mutex_);
    mouse_.scroll_delta += delta;
}

void InputManager::OnMouseEnterLeave(bool entered) {
    std::lock_guard<std::mutex> lock(mutex_);
    mouse_.in_window = entered;
}

void InputManager::SetMousePosition(const int32_t x, const int32_t y) {
    std::lock_guard<std::mutex> lock(mutex_);
    mouse_.x = x;
    mouse_.y = y;
    prev_mouse_.x = x;
    prev_mouse_.y = y;
    mouse_dx_ = 0;
    mouse_dy_ = 0;
}

bool InputManager::SyncMousePositionFromWindow() {

    const auto cursor = openwow::platform::WindowManager::Get()
                            .ResolveLogicalCursorPositionInDrawablePixels();
    if (!cursor.has_value()) {
        return false;
    }

    SetMousePosition(cursor->first, cursor->second);
    return true;
}

std::string InputManager::KeyCodeToName(uint32_t keyCode) {

    static const std::unordered_map<uint32_t, std::string> kMap = {

        {4, "A"}, {5, "B"}, {6, "C"}, {7, "D"}, {8, "E"}, {9, "F"},
        {10, "G"}, {11, "H"}, {12, "I"}, {13, "J"}, {14, "K"}, {15, "L"},
        {16, "M"}, {17, "N"}, {18, "O"}, {19, "P"}, {20, "Q"}, {21, "R"},
        {22, "S"}, {23, "T"}, {24, "U"}, {25, "V"}, {26, "W"}, {27, "X"},
        {28, "Y"}, {29, "Z"},

        {30, "1"}, {31, "2"}, {32, "3"}, {33, "4"}, {34, "5"},
        {35, "6"}, {36, "7"}, {37, "8"}, {38, "9"}, {39, "0"},

        {40, "ENTER"}, {41, "ESCAPE"}, {42, "BACKSPACE"}, {43, "TAB"},
        {44, "SPACE"}, {45, "-"}, {46, "="}, {47, "["}, {48, "]"},
        {49, "\\"}, {51, ";"}, {52, "'"}, {53, "`"}, {54, ","}, {55, "."},
        {56, "/"},

        {58, "F1"}, {59, "F2"}, {60, "F3"}, {61, "F4"}, {62, "F5"},
        {63, "F6"}, {64, "F7"}, {65, "F8"}, {66, "F9"}, {67, "F10"},
        {68, "F11"}, {69, "F12"},

        {79, "RIGHT"}, {80, "LEFT"}, {81, "DOWN"}, {82, "UP"},

        {70, "PRINTSCREEN"}, {71, "SCROLLLOCK"}, {72, "PAUSE"},
        {73, "INSERT"}, {74, "HOME"}, {75, "PAGEUP"},
        {76, "DELETE"}, {77, "END"}, {78, "PAGEDOWN"},

        {83, "NUMLOCK"}, {84, "NUMPADDIVIDE"}, {85, "NUMPADMULTIPLY"},
        {86, "NUMPADMINUS"}, {87, "NUMPADPLUS"},
        {88, "NUMPADENTER"},
        {89, "NUMPAD1"}, {90, "NUMPAD2"}, {91, "NUMPAD3"},
        {92, "NUMPAD4"}, {93, "NUMPAD5"}, {94, "NUMPAD6"},
        {95, "NUMPAD7"}, {96, "NUMPAD8"}, {97, "NUMPAD9"},
        {98, "NUMPAD0"}, {99, "NUMPADDECIMAL"},

        {224, "LCTRL"}, {225, "LSHIFT"}, {226, "LALT"},
        {228, "RCTRL"}, {229, "RSHIFT"}, {230, "RALT"},
    };

    auto it = kMap.find(keyCode);
    if (it != kMap.end()) return it->second;
    return "UNKNOWN" + std::to_string(keyCode);
}

uint32_t InputManager::KeyNameToCode(const std::string& name) {

    static std::unordered_map<std::string, uint32_t> sReverse;
    if (sReverse.empty()) {
        for (uint32_t sc = 0; sc < 300; ++sc) {
            std::string n = KeyCodeToName(sc);
            if (n.rfind("UNKNOWN", 0) != 0) {
                sReverse[n] = sc;
            }
        }
    }

    std::string upper = name;
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto it = sReverse.find(upper);
    return (it != sReverse.end()) ? it->second : 0;
}

std::string InputManager::GetCurrentKeyString(uint32_t keyCode) const {
    auto mod = GetModifiers();
    std::string result;
    if (mod.ctrl)  result += "CTRL-";
    if (mod.alt)   result += "ALT-";
    if (mod.shift) result += "SHIFT-";
    result += KeyCodeToName(keyCode);
    return result;
}

void InputManager::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    keys_current_.fill(false);
    keys_previous_.fill(false);
    mouse_      = {};
    prev_mouse_ = {};
    mouse_button_flags_ = 0;
    prev_mouse_button_flags_ = 0;
    mouse_looking_   = false;
    text_input_      = false;
    mouse_dx_        = 0;
    mouse_dy_        = 0;
    frame_text_input_.clear();
    event_queue_.clear();
    mouse_captured_ = false;
    input_enabled_  = true;
    key_callbacks_.clear();
    next_callback_id_ = 1;

    click_timestamp_ms_.fill(0);
    click_accu_abs_dx_.fill(0.0f);
    click_accu_abs_dy_.fill(0.0f);
    last_clicked_button_idx_ = 0;
    last_message_timestamp_ms_ = 0xFFFFFFFFu;
}

void InputManager::ProcessKeyDown(uint32_t keyCode, uint32_t modifiers) {
    std::vector<std::function<void(bool)>> to_invoke;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!input_enabled_) return;
        if (keyCode < kMaxKeys) keys_current_[keyCode] = true;
        last_message_timestamp_ms_ = GetCurrentMessageTimestamp();

        InputEvent ev{};
        ev.type = InputEventType::KeyDown;
        ev.keyCode = keyCode;
        ev.modifiers = modifiers;
        event_queue_.push_back(ev);

        for (const auto& cb : key_callbacks_) {
            if (cb.keyCode == keyCode && cb.callback)
                to_invoke.push_back(cb.callback);
        }
    }
    for (auto& fn : to_invoke) fn(true);
}

void InputManager::ProcessKeyUp(uint32_t keyCode, uint32_t modifiers) {
    std::vector<std::function<void(bool)>> to_invoke;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!input_enabled_) return;
        if (keyCode < kMaxKeys) keys_current_[keyCode] = false;
        last_message_timestamp_ms_ = GetCurrentMessageTimestamp();

        InputEvent ev{};
        ev.type = InputEventType::KeyUp;
        ev.keyCode = keyCode;
        ev.modifiers = modifiers;
        event_queue_.push_back(ev);

        for (const auto& cb : key_callbacks_) {
            if (cb.keyCode == keyCode && cb.callback)
                to_invoke.push_back(cb.callback);
        }
    }
    for (auto& fn : to_invoke) fn(false);
}

void InputManager::ProcessMouseDown(MouseButton btn, int x, int y, uint32_t modifiers) {
    ProcessMouseButtonFlagDown(MouseButtonToFlag(btn), x, y, modifiers);
}

void InputManager::ProcessMouseUp(MouseButton btn, int x, int y, uint32_t modifiers) {
    ProcessMouseButtonFlagUp(MouseButtonToFlag(btn), x, y, modifiers);
}

void InputManager::ProcessMouseButtonFlagDown(const uint32_t button_flag, int x,
                                              int y, uint32_t modifiers) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!input_enabled_ || button_flag == 0u) return;
    last_message_timestamp_ms_ = GetCurrentMessageTimestamp();
    mouse_button_flags_ |= button_flag;
    ApplyMouseButtonFlag(mouse_, button_flag, true);
    mouse_.x = x;
    mouse_.y = y;

    if (const auto mapped_button = MouseButtonFromFlag(button_flag);
        mapped_button.has_value()) {
        const auto idx = static_cast<size_t>(*mapped_button);
        if (idx < kMaxButtons) {
            click_timestamp_ms_[idx] = GetNowMs();
            click_accu_abs_dx_[idx]  = 0.0f;
            click_accu_abs_dy_[idx]  = 0.0f;
            last_clicked_button_idx_ = static_cast<uint8_t>(idx);
        }
    }

    InputEvent ev{};
    ev.type = InputEventType::MouseDown;
    if (const auto mapped_button = MouseButtonFromFlag(button_flag);
        mapped_button.has_value()) {
        ev.mouseButton = *mapped_button;
    }
    ev.mouseButtonFlag = button_flag;
    ev.mouseX = x;
    ev.mouseY = y;
    ev.modifiers = modifiers;
    event_queue_.push_back(ev);
}

void InputManager::ProcessMouseButtonFlagUp(const uint32_t button_flag, int x,
                                            int y, uint32_t modifiers) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!input_enabled_ || button_flag == 0u) return;
    last_message_timestamp_ms_ = GetCurrentMessageTimestamp();
    mouse_button_flags_ &= ~button_flag;
    ApplyMouseButtonFlag(mouse_, button_flag, false);
    mouse_.x = x;
    mouse_.y = y;

    InputEvent ev{};
    ev.type = InputEventType::MouseUp;
    if (const auto mapped_button = MouseButtonFromFlag(button_flag);
        mapped_button.has_value()) {
        ev.mouseButton = *mapped_button;
    }
    ev.mouseButtonFlag = button_flag;
    ev.mouseX = x;
    ev.mouseY = y;
    ev.modifiers = modifiers;
    event_queue_.push_back(ev);
}

void InputManager::ProcessMouseMove(int x, int y, int dx, int dy) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!input_enabled_) return;
    last_message_timestamp_ms_ = GetCurrentMessageTimestamp();
    mouse_dx_ += dx;
    mouse_dy_ += dy;
    mouse_.x = x;
    mouse_.y = y;

    const size_t bidx = last_clicked_button_idx_;
    if (bidx < kMaxButtons) {
        click_accu_abs_dx_[bidx] += std::fabs(static_cast<float>(dx));
        click_accu_abs_dy_[bidx] += std::fabs(static_cast<float>(dy));
    }

    InputEvent ev{};
    ev.type = InputEventType::MouseMove;
    ev.mouseX = x;
    ev.mouseY = y;
    ev.deltaX = dx;
    ev.deltaY = dy;
    event_queue_.push_back(ev);
}

void InputManager::ProcessMouseWheel(float delta) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!input_enabled_) return;
    last_message_timestamp_ms_ = GetCurrentMessageTimestamp();
    mouse_.scroll_delta += static_cast<int32_t>(delta);

    InputEvent ev{};
    ev.type = InputEventType::MouseWheel;
    ev.wheelDelta = delta;
    event_queue_.push_back(ev);
}

void InputManager::ProcessTextInput(char32_t ch) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!input_enabled_) return;
    last_message_timestamp_ms_ = GetCurrentMessageTimestamp();

    InputEvent ev{};
    ev.type = InputEventType::TextInput;
    ev.character = ch;
    event_queue_.push_back(ev);
}

std::pair<int, int> InputManager::GetMousePosition() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {mouse_.x, mouse_.y};
}

uint32_t InputManager::GetModifierBitmask() const {
    auto mod = GetModifiers();
    uint32_t bitmask = ModNone;
    if (mod.shift) bitmask |= ModShift;
    if (mod.ctrl)  bitmask |= ModCtrl;
    if (mod.alt)   bitmask |= ModAlt;
    return bitmask;
}

bool InputManager::IsShiftDown() const {
    return GetModifiers().shift;
}

bool InputManager::IsCtrlDown() const {
    return GetModifiers().ctrl;
}

bool InputManager::IsAltDown() const {
    return GetModifiers().alt;
}

std::uint32_t InputManager::GetLastMessageTimestamp() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_message_timestamp_ms_;
}

uint32_t InputManager::RegisterKeyCallback(uint32_t keyCode,
                                           std::function<void(bool down)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t id = next_callback_id_++;
    key_callbacks_.push_back({id, keyCode, std::move(callback)});
    return id;
}

void InputManager::UnregisterCallback(uint32_t callbackId) {
    std::lock_guard<std::mutex> lock(mutex_);
    key_callbacks_.erase(
        std::remove_if(key_callbacks_.begin(), key_callbacks_.end(),
                       [callbackId](const KeyCallback& cb) { return cb.id == callbackId; }),
        key_callbacks_.end());
}

void InputManager::SetMouseCapture(bool capture) {
    std::lock_guard<std::mutex> lock(mutex_);
    mouse_captured_ = capture;
}

bool InputManager::IsMouseCaptured() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mouse_captured_;
}

void InputManager::SetInputEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    input_enabled_ = enabled;
}

bool InputManager::IsInputEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return input_enabled_;
}

bool InputManager::HasDoubleClickElapsed(MouseButton btn, uint32_t now_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto idx = static_cast<size_t>(btn);
    if (idx >= kMaxButtons) return true;
    const uint32_t elapsed = now_ms - click_timestamp_ms_[idx];
    if (elapsed >= 800u) return true;
    if (click_accu_abs_dx_[idx] >= 8.0f || click_accu_abs_dy_[idx] >= 8.0f)
        return elapsed >= 200u;
    return false;
}

void InputManager::RecordClickDown(MouseButton btn, uint32_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto idx = static_cast<size_t>(btn);
    if (idx >= kMaxButtons) return;
    click_timestamp_ms_[idx]  = now_ms;
    click_accu_abs_dx_[idx]   = 0.0f;
    click_accu_abs_dy_[idx]   = 0.0f;
    last_clicked_button_idx_  = static_cast<uint8_t>(idx);
}

void InputManager::AccumulateClickDelta(float dx, float dy) {

    std::lock_guard<std::mutex> lock(mutex_);
    const size_t idx = last_clicked_button_idx_;
    if (idx >= kMaxButtons) return;
    click_accu_abs_dx_[idx] += std::fabs(dx);
    click_accu_abs_dy_[idx] += std::fabs(dy);
}

const std::vector<InputEvent>& InputManager::GetEventQueue() const {
    return event_queue_;
}

void InputManager::ClearEventQueue() {
    std::lock_guard<std::mutex> lock(mutex_);
    event_queue_.clear();
}

}

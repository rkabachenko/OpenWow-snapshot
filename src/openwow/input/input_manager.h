
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace openwow::input {

inline constexpr std::size_t kMouseButtonMax = 32;

enum class MouseButton : uint8_t {
    Left     = 0,
    Right    = 1,
    Middle   = 2,
    X1       = 3,
    Button4  = 3,
    X2       = 4,
    Button5  = 4,
    Button6  = 5,
    Button7  = 6,
    Button8  = 7,
    Button9  = 8,
    Button10 = 9,
    Button11 = 10,
    Button12 = 11,
    Button13 = 12,
    Button14 = 13,
    Button15 = 14,
    Button16 = 15,
    Button17 = 16,
    Button18 = 17,
    Button19 = 18,
    Button20 = 19,
    Button21 = 20,
    Button22 = 21,
    Button23 = 22,
    Button24 = 23,
    Button25 = 24,
    Button26 = 25,
    Button27 = 26,
    Button28 = 27,
    Button29 = 28,
    Button30 = 29,
    Button31 = 30,
    ButtonMax = 31,
};

enum class InputEventType : uint8_t {
    KeyDown,
    KeyUp,
    MouseDown,
    MouseUp,
    MouseMove,
    MouseWheel,
    TextInput
};

enum KeyModifier : uint32_t {
    ModNone  = 0,
    ModShift = 1,
    ModCtrl  = 2,
    ModAlt   = 4,
    ModMeta  = 8,
};

struct InputEvent {
    InputEventType type{};
    uint32_t keyCode{};
    uint32_t modifiers{};
    MouseButton mouseButton{};
    uint32_t mouseButtonFlag{};
    int32_t mouseX{};
    int32_t mouseY{};
    int32_t deltaX{};
    int32_t deltaY{};
    float wheelDelta{};
    char32_t character{};
};

struct KeyModifiers {
    bool shift = false;
    bool ctrl  = false;
    bool alt   = false;
};

struct MouseState {
    int32_t x = 0;
    int32_t y = 0;
    int32_t scroll_delta = 0;
    std::array<bool, kMouseButtonMax> buttons = {};
    bool in_window = false;
};

class InputManager {
public:
    static InputManager& Get();

    [[nodiscard]] bool IsKeyDown(uint32_t keyCode) const;
    [[nodiscard]] bool IsKeyPressed(uint32_t keyCode) const;
    [[nodiscard]] bool IsKeyReleased(uint32_t keyCode) const;
    [[nodiscard]] KeyModifiers GetModifiers() const;

    [[nodiscard]] const MouseState& GetMouse() const;
    [[nodiscard]] bool IsMouseButtonDown(MouseButton btn) const;
    [[nodiscard]] bool IsMouseButtonPressed(MouseButton btn) const;
    [[nodiscard]] bool IsMouseButtonReleased(MouseButton btn) const;
    [[nodiscard]] bool IsMouseButtonFlagDown(uint32_t button_flag) const;
    [[nodiscard]] uint32_t GetMouseButtonFlags() const;

    [[nodiscard]] bool IsMouseLooking() const;
    void SetMouseLooking(bool looking);
    [[nodiscard]] int32_t GetMouseDeltaX() const;
    [[nodiscard]] int32_t GetMouseDeltaY() const;

    void StartTextInput();
    void StopTextInput();
    [[nodiscard]] bool IsTextInputActive() const;

    void BeginFrame();
    void EndFrame();

    void OnKeyDown(uint32_t keyCode, uint32_t scanCode, bool repeat);
    void OnKeyUp(uint32_t keyCode, uint32_t scanCode);
    void OnTextInput(const std::string& text);
    void OnMouseMove(int32_t x, int32_t y);
    void OnMouseButton(MouseButton btn, bool pressed);
    void OnMouseWheel(int32_t delta);
    void OnMouseEnterLeave(bool entered);

    void SetMousePosition(int32_t x, int32_t y);
    bool SyncMousePositionFromWindow();

    static std::string KeyCodeToName(uint32_t keyCode);
    static uint32_t KeyNameToCode(const std::string& name);

    [[nodiscard]] std::string GetCurrentKeyString(uint32_t keyCode) const;

    [[nodiscard]] const std::string& GetTextInput() const;

    void Reset();

    void ProcessKeyDown(uint32_t keyCode, uint32_t modifiers);
    void ProcessKeyUp(uint32_t keyCode, uint32_t modifiers);
    void ProcessMouseDown(MouseButton btn, int x, int y, uint32_t modifiers);
    void ProcessMouseUp(MouseButton btn, int x, int y, uint32_t modifiers);
    void ProcessMouseButtonFlagDown(uint32_t button_flag, int x, int y,
                                    uint32_t modifiers);
    void ProcessMouseButtonFlagUp(uint32_t button_flag, int x, int y,
                                  uint32_t modifiers);
    void ProcessMouseMove(int x, int y, int dx, int dy);
    void ProcessMouseWheel(float delta);
    void ProcessTextInput(char32_t ch);

    [[nodiscard]] std::pair<int, int> GetMousePosition() const;
    [[nodiscard]] uint32_t GetModifierBitmask() const;
    [[nodiscard]] bool IsShiftDown() const;
    [[nodiscard]] bool IsCtrlDown() const;
    [[nodiscard]] bool IsAltDown() const;
    [[nodiscard]] std::uint32_t GetLastMessageTimestamp() const;

    uint32_t RegisterKeyCallback(uint32_t keyCode,
                                 std::function<void(bool down)> callback);
    void UnregisterCallback(uint32_t callbackId);

    void SetMouseCapture(bool capture);
    [[nodiscard]] bool IsMouseCaptured() const;

    void SetInputEnabled(bool enabled);
    [[nodiscard]] bool IsInputEnabled() const;

    [[nodiscard]] const std::vector<InputEvent>& GetEventQueue() const;
    void ClearEventQueue();

    [[nodiscard]] bool HasDoubleClickElapsed(MouseButton btn, uint32_t now_ms) const;

    void RecordClickDown(MouseButton btn, uint32_t now_ms);

    void AccumulateClickDelta(float dx, float dy);

private:
    InputManager() = default;

    static constexpr size_t kMaxKeys = 512;

    std::array<bool, kMaxKeys> keys_current_  = {};
    std::array<bool, kMaxKeys> keys_previous_ = {};

    MouseState mouse_     = {};
    MouseState prev_mouse_ = {};
    uint32_t mouse_button_flags_ = 0;
    uint32_t prev_mouse_button_flags_ = 0;

    bool     mouse_looking_ = false;
    bool     text_input_    = false;
    int32_t  mouse_dx_      = 0;
    int32_t  mouse_dy_      = 0;

    std::string frame_text_input_;

    std::vector<InputEvent> event_queue_;
    bool mouse_captured_ = false;
    bool input_enabled_  = true;
    uint32_t next_callback_id_ = 1;
    std::uint32_t last_message_timestamp_ms_ = 0xFFFFFFFFu;

    static constexpr size_t kMaxButtons = kMouseButtonMax;
    std::array<uint32_t, kMaxButtons> click_timestamp_ms_  = {};
    std::array<float, kMaxButtons>    click_accu_abs_dx_   = {};
    std::array<float, kMaxButtons>    click_accu_abs_dy_   = {};

    uint8_t last_clicked_button_idx_ = 0;

    struct KeyCallback {
        uint32_t id;
        uint32_t keyCode;
        std::function<void(bool)> callback;
    };
    std::vector<KeyCallback> key_callbacks_;

    mutable std::mutex mutex_;
};

using WowMouseProbeCallback = bool (*)();

struct WowMouseEnumeratedDevice {
    std::uint32_t devinst = 0;
    bool has_button_report_interface = false;
    bool has_auxiliary_interface = false;
};
using WowMouseEnumerationCallback =
    void (*)(std::vector<WowMouseEnumeratedDevice>& devices);
using WowMouseButtonStateSink = void (*)(void* context,
                                         std::uint32_t raw_button_state);

void SetWowMouseProbeCallback(WowMouseProbeCallback callback);
void SetWowMouseEnumerationCallback(WowMouseEnumerationCallback callback);
void EnumerateWowMouseDevices(std::vector<WowMouseEnumeratedDevice>& devices);
[[nodiscard]] bool DetectWowMouse();
void SetWowMouseButtonStateSink(WowMouseButtonStateSink sink, void* context);
void DispatchWowMouseButtonState(std::uint32_t raw_button_state);

}

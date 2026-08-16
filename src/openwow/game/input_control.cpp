
#include "openwow/game/input_control.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/console.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/actions/bindings/adapters/platform/sdl_binding_input.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/keybind_system.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/vfs/sfile_core.h"

#include <charconv>
#include <memory>
#include <string_view>

extern "C" {
#include <lua.hpp>
}

namespace openwow::game::input {

namespace {

std::unique_ptr<CInputControl> g_owned_input_control_singleton;
std::uint32_t g_joystick_cvar_callback_handle = 0;
JoystickConfigXmlTextProvider g_joystick_config_xml_text_provider = nullptr;
ActiveJoystickNameProvider g_active_joystick_name_provider = nullptr;

struct AppliedJoystickConfigRuntime {
    bool mouse_enabled = false;
    AppliedJoystickConfigState active_state;
};

AppliedJoystickConfigRuntime g_applied_joystick_config_runtime;

bool ParseIntegerStyleCVarEnabled(const std::string& value) {
    int parsed_value = 0;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed_value);
    if (ec != std::errc{} || ptr == begin) {
        return false;
    }

    return parsed_value != 0;
}

bool EnableWowMouseValidationCallback(const std::string&,
                                      const std::string&,
                                      const std::string& new_value) {
    if (auto* input = GetInputControlSingleton(); input != nullptr) {
        input->SetJoystickEnabled(ParseIntegerStyleCVarEnabled(new_value));
    }

    return true;
}

void PublishCursorVisibilityToActiveSurface(bool visible) {
    if (auto* cursor = GetActiveCursorSurface(); cursor != nullptr) {
        cursor->ShowCursor(visible);
    }
}

bool ParseJoystickCVarEnabled(std::string_view value) {
    return !value.empty() && value.front() == '1';
}

std::optional<std::string> LoadJoystickConfigXmlText() {
    void* raw_data = nullptr;
    std::size_t raw_size = 0;
    if (!openwow::vfs::SFileReadFileToBuffer_Wrapper(
            "Interface\\Joystick\\JoystickConfig.xml", &raw_data, &raw_size,
            0, 0)) {
        return std::nullopt;
    }

    const std::string xml_text(static_cast<const char*>(raw_data), raw_size);
    (void)openwow::vfs::SFileFreeLoadedData(raw_data);
    return xml_text;
}

void ApplyParsedJoystickConfigProfile(const JoystickConfigProfile& profile) {
    g_applied_joystick_config_runtime.active_state = {};
    g_applied_joystick_config_runtime.active_state.has_active_profile = true;

    for (const auto& stick : profile.sticks) {
        if (stick.stick_index >=
                g_applied_joystick_config_runtime.active_state.sticks.size()
            || stick.axis_x >=
                g_applied_joystick_config_runtime.active_state.slider_axes.size()
            || stick.axis_y >=
                g_applied_joystick_config_runtime.active_state.slider_axes.size()) {
            continue;
        }

        g_applied_joystick_config_runtime.active_state.sticks[stick.stick_index] =
            stick;
    }

    for (const auto slider_axis : profile.slider_axes) {
        if (slider_axis <
            g_applied_joystick_config_runtime.active_state.slider_axes.size()) {
            g_applied_joystick_config_runtime.active_state.slider_axes[slider_axis] =
                true;
        }
    }

    for (const auto& hat : profile.hats) {
        const bool buttons_valid =
            hat.buttons[0] >= 0 && hat.buttons[1] >= 0
            && hat.buttons[2] >= 0 && hat.buttons[3] >= 0
            && hat.buttons[0] < 32 && hat.buttons[1] < 32
            && hat.buttons[2] < 32 && hat.buttons[3] < 32;
        if (hat.hat_index >=
                g_applied_joystick_config_runtime.active_state.hats.size()
            || !buttons_valid) {
            continue;
        }

        g_applied_joystick_config_runtime.active_state.hats[hat.hat_index] = hat;
    }

    if (profile.mouse.has_value()) {
        if (profile.mouse->enabled.has_value()) {
            g_applied_joystick_config_runtime.mouse_enabled =
                *profile.mouse->enabled;
        }

        if (g_applied_joystick_config_runtime.mouse_enabled) {
            g_applied_joystick_config_runtime.active_state.mouse_stick_index =
                profile.mouse->stick_index;
            g_applied_joystick_config_runtime.active_state.mouse_button_left =
                profile.mouse->button_left;
            g_applied_joystick_config_runtime.active_state.mouse_button_right =
                profile.mouse->button_right;
            g_applied_joystick_config_runtime.active_state.mouse_speed_scale =
                profile.mouse->speed_scale;
        }
    }
}

void ApplyJoystickConfigForValue(std::string_view value) {
    if (!ParseJoystickCVarEnabled(value)) {
        g_applied_joystick_config_runtime.active_state = {};
        return;
    }

    g_applied_joystick_config_runtime.active_state = {};

    const auto joystick_name =
        g_active_joystick_name_provider != nullptr
            ? g_active_joystick_name_provider()
            : actions::bindings::adapters::platform::
                  QueryPrimaryJoystickName();
    if (!joystick_name.has_value()) {
        return;
    }

    openwow::core::ida::ConsoleLog("Joystick: %s", joystick_name->c_str());

    const auto xml_text =
        g_joystick_config_xml_text_provider != nullptr
            ? g_joystick_config_xml_text_provider()
            : LoadJoystickConfigXmlText();
    if (!xml_text.has_value()) {
        return;
    }

    const auto profile =
        actions::bindings::adapters::platform::ParseJoystickConfigProfile(
            *xml_text, *joystick_name);
    if (!profile.has_value()) {
        return;
    }

    ApplyParsedJoystickConfigProfile(*profile);
}

void HandleJoystickCVarChanged(const std::string&,
                               const std::string& new_value) {
    ApplyJoystickConfigForValue(new_value);
}

void RegisterInputControlStartupCVars(openwow::ui::game::CVarSystem& cvars) {
    using CVarFlags = openwow::ui::game::CVarFlags;

    cvars.RegisterCVar("Joystick", "0", CVarFlags::None,
                       "enable joystick control");
    cvars.RegisterCVar("CinematicJoystick", "0", CVarFlags::None,
                       "enable cinematic joystick control");
    cvars.RegisterCVar("enableWowMouse", "0", CVarFlags::Archive,
                       "Enable Steelseries World of Warcraft Mouse");
}

void ReleaseOwnedInputControlSingleton() {
    if (g_owned_input_control_singleton &&
        g_owned_input_control_singleton.get() != GetInputControlSingleton()) {
        g_owned_input_control_singleton->SetJoystickEnabled(false);
    }

    g_owned_input_control_singleton.reset();
}

CInputControl* ResolveInputControlArgument(void* input_control) {
    if (input_control != nullptr) {
        return static_cast<CInputControl*>(input_control);
    }

    return GetInputControlSingleton();
}

bool CanWriteProtectedMouselookBinding() {
    const auto check = GetProtectedActionCheck();
    return check == nullptr || check();
}

}

int InputControl_PitchDownStart() {
    return openwow::game::InputControl_PitchDownStart(
        openwow::core::GameClock::GetTickCount32());
}

int InputControl_MouselookStart() {
    return openwow::game::InputControl_MouselookStart(
        openwow::core::GameClock::GetTickCount32());
}

void InputControl_StartupInitialize() {
    auto& cvars = openwow::ui::game::CVarSystem::Instance();
    RegisterInputControlStartupCVars(cvars);

    cvars.SetValidationCallback("enableWowMouse",
                                EnableWowMouseValidationCallback);
    if (g_joystick_cvar_callback_handle != 0) {
        cvars.RemoveCallback("Joystick", g_joystick_cvar_callback_handle);
    }
    g_joystick_cvar_callback_handle =
        cvars.AddCallback("Joystick", HandleJoystickCVarChanged);

    ShutdownInputControlRuntime();
    ReleaseOwnedInputControlSingleton();

    SetCursorVisibilityCallback(PublishCursorVisibilityToActiveSurface);
    g_owned_input_control_singleton = std::make_unique<CInputControl>(
        KeybindSystem::Get().GetManager());
    SetInputControlSingleton(g_owned_input_control_singleton.get());
    g_owned_input_control_singleton->SetJoystickEnabled(
        cvars.GetCVarBool("enableWowMouse"));
    ApplyJoystickConfigForValue(cvars.GetCVar("Joystick"));
}

void ChatLog_Shutdown() {

    ShutdownInputControlRuntime();
    ReleaseOwnedInputControlSingleton();
}

void ChatLog_Shutdown_Thunk() {
    ChatLog_Shutdown();
}

void InputControl_SetOverrideBinding(void* inputCtrl, const char* key,
                                      const char* binding) {
    auto* control = ResolveInputControlArgument(inputCtrl);
    if (control == nullptr || key == nullptr) {
        return;
    }

    openwow::game::InputControl_SetStoredMouselookOverrideBindingForControl(
        *control, key, binding);
}

int InputControl_SetMouselookOverrideBinding(void* luaState) {
    auto* state = static_cast<lua_State*>(luaState);
    if (state == nullptr) {
        return 0;
    }

    const char* key = luaL_checkstring(state, 1);
    if (!CanWriteProtectedMouselookBinding()) {
        return 0;
    }

    const char* binding = nullptr;
    if (lua_isnoneornil(state, 2) == 0) {
        binding = luaL_checkstring(state, 2);
    }

    openwow::game::InputControl_SetStoredMouselookOverrideBinding(key, binding);
    return 0;
}

void InputControl_ApplyMouselookBindings(void* inputCtrl) {
    auto* control = ResolveInputControlArgument(inputCtrl);
    if (control == nullptr) {
        return;
    }

    openwow::game::InputControl_ApplyStoredMouselookOverrideBindingsForControl(
        *control);
}

bool IsJoystickMouseConfigEnabled() {
    return g_applied_joystick_config_runtime.mouse_enabled;
}

const AppliedJoystickConfigState& GetAppliedJoystickConfigState() {
    return g_applied_joystick_config_runtime.active_state;
}

void ResetJoystickConfigRuntimeForTests() {
    g_applied_joystick_config_runtime = {};
    g_joystick_config_xml_text_provider = nullptr;
    g_active_joystick_name_provider = nullptr;
}

void SetJoystickMouseConfigEnabledForTests(const bool enabled) {
    g_applied_joystick_config_runtime.mouse_enabled = enabled;
}

void SetJoystickConfigXmlTextProvider(
    const JoystickConfigXmlTextProvider provider) {
    g_joystick_config_xml_text_provider = provider;
}

void SetActiveJoystickNameProvider(const ActiveJoystickNameProvider provider) {
    g_active_joystick_name_provider = provider;
}

}

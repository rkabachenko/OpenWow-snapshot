
#include "openwow/game/keybind_system.h"
#include "openwow/game/actions/bindings/adapters/lua/binding_lua_values.h"
#include "openwow/game/actions/bindings/adapters/persistence/binding_profile_storage.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <sstream>

namespace openwow::game {

KeybindSystem& KeybindSystem::Get() {
    static KeybindSystem instance;
    return instance;
}

bool KeybindSystem::Initialize() {
    std::lock_guard lock(mutex_);
    return manager_.Initialize();
}

void KeybindSystem::Shutdown() {
    std::lock_guard lock(mutex_);
    input_runtime_.Reset();
    manager_.Shutdown();
}

void KeybindSystem::SetBinding(const std::string& key, const std::string& command) {
    std::lock_guard lock(mutex_);
    (void)actions::bindings::adapters::lua::AssignBindingValue(
        manager_, key, command, 0);
}

void KeybindSystem::ClearBinding(const std::string& key) {
    std::lock_guard lock(mutex_);
    (void)actions::bindings::adapters::lua::AssignBindingValue(
        manager_, key, "", 0);
}

std::string KeybindSystem::GetBindingForKey(const std::string& key) const {
    std::lock_guard lock(mutex_);
    return actions::bindings::adapters::lua::ReadBindingAction(
        manager_, key, true, BindingProfiles::BindingSlotSelector::kMode0);
}

std::string KeybindSystem::GetKeyForCommand(const std::string& command) const {
    std::lock_guard lock(mutex_);
    const auto keys = actions::bindings::adapters::lua::ReadBindingKeys(
        manager_, command, BindingProfiles::BindingSlotSelector::kMode0);
    return keys.empty() ? std::string{} : keys.front();
}

size_t KeybindSystem::GetNumBindings() const {
    std::lock_guard lock(mutex_);
    return static_cast<size_t>(manager_.GetNumBindings());
}

bool KeybindSystem::GetBinding(size_t index, std::string& command,
                                std::string& key1, std::string& key2) const {
    std::lock_guard lock(mutex_);
    const auto binding = manager_.BindingAt(
        static_cast<int>(index), BindingProfileScope::kDefault);
    if (!binding) {
        return false;
    }
    command = binding->command.value();
    key1 = binding->chords.empty() ? std::string{}
                                    : binding->chords[0].value();
    key2 = binding->chords.size() < 2 ? std::string{}
                                      : binding->chords[1].value();
    return true;
}

std::vector<BindingAssignment> KeybindSystem::GetAllBindings() const {
    std::lock_guard lock(mutex_);
    std::vector<BindingAssignment> result;
    int num = manager_.GetNumBindings();
    for (int i = 1; i <= num; ++i) {
        const auto binding = manager_.BindingAt(
            i, BindingProfileScope::kActive);
        if (binding) {
            int binding_index = 0;
            for (const auto& chord : binding->chords) {
                result.emplace_back(chord, binding->command,
                                    BindingProfileScope::kActive,
                                    BindingSlot::Primary(), binding_index++);
            }
        }
    }
    return result;
}

void KeybindSystem::LoadDefaults() {
    std::lock_guard lock(mutex_);
    manager_.LoadDefaults();
}

void KeybindSystem::SaveBindings() {
    std::lock_guard lock(mutex_);
    manager_.SaveBindings(BindingProfileScope::kAccount);
    actions::bindings::adapters::persistence::BindingProfileStorage::SaveFile(
        manager_, "WTF/bindings-cache.wtf");
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "KeybindSystem: SaveBindings persisted to WTF/bindings-cache.wtf");
}

std::string KeybindSystem::GetBindingCategory(const std::string& command) {
    if (command.find("MOVE") != std::string::npos ||
        command.find("TURN") != std::string::npos ||
        command.find("STRAFE") != std::string::npos ||
        command.find("JUMP") != std::string::npos ||
        command.find("PITCH") != std::string::npos ||
        command.find("AUTORUN") != std::string::npos ||
        command.find("RUNWALK") != std::string::npos) {
        return "MOVEMENT";
    }
    if (command.find("ACTIONBUTTON") != std::string::npos ||
        command.find("MULTIACTIONBAR") != std::string::npos) {
        return "ACTIONBAR";
    }
    if (command.find("TARGET") != std::string::npos ||
        command.find("ASSIST") != std::string::npos ||
        command.find("FOCUS") != std::string::npos) {
        return "TARGETING";
    }
    if (command.find("CHAT") != std::string::npos ||
        command.find("REPLY") != std::string::npos) {
        return "CHAT";
    }
    if (command.find("TOGGLE") != std::string::npos) {
        return "INTERFACE";
    }
    return "MISC";
}

void KeybindSystem::SetOverrideBinding(const std::string& owner, bool priority,
                                        const std::string& key,
                                        const std::string& command) {
    std::lock_guard lock(mutex_);
    actions::bindings::adapters::lua::AssignOverrideBindingValue(
        manager_, BindingProfiles::OverrideOwner::FromStableTag(owner),
        priority, key, command);
}

void KeybindSystem::ClearOverrideBindings(const std::string& owner) {
    std::lock_guard lock(mutex_);
    manager_.ClearOverrideBindings(
        BindingProfiles::OverrideOwner::FromStableTag(owner));
}

std::string KeybindSystem::ActionButtonCommand(uint8_t slot) {
    return "ACTIONBUTTON" + std::to_string(slot);
}

bool KeybindSystem::ProcessKeyDown(const std::string& key) {
    std::lock_guard lock(mutex_);
    return input_runtime_.KeyDown(key);
}

bool KeybindSystem::ProcessKeyUp(const std::string& key) {
    std::lock_guard lock(mutex_);
    return input_runtime_.KeyUp(key);
}

bool KeybindSystem::ProcessMouseButtonDown(const std::uint32_t button_flag,
                                           const std::uint16_t modifier_state) {
    std::lock_guard lock(mutex_);
    return input_runtime_.MouseButtonDown(button_flag, modifier_state);
}

bool KeybindSystem::ProcessMouseButtonUp(const std::uint32_t button_flag,
                                         const std::uint16_t modifier_state) {
    std::lock_guard lock(mutex_);
    return input_runtime_.MouseButtonUp(button_flag, modifier_state);
}

void KeybindSystem::Reset() {
    std::lock_guard lock(mutex_);
    input_runtime_.Reset();
    manager_.Shutdown();
    manager_.Initialize();
}

}

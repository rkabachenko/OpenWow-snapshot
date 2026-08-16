#pragma once

#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/actions/bindings/adapters/platform/sdl_binding_input_runtime.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openwow::game {

class KeybindSystem {
 public:
    static KeybindSystem& Get();

    bool Initialize();
    void Shutdown();

    void SetBinding(const std::string& key, const std::string& command);
    void ClearBinding(const std::string& key);
    [[nodiscard]] std::string GetBindingForKey(const std::string& key) const;
    [[nodiscard]] std::string GetKeyForCommand(const std::string& command) const;

    [[nodiscard]] size_t GetNumBindings() const;
    bool GetBinding(size_t index, std::string& command,
                    std::string& key1, std::string& key2) const;
    [[nodiscard]] std::vector<BindingAssignment> GetAllBindings() const;

    void LoadDefaults();
    void SaveBindings();

    static std::string GetBindingCategory(const std::string& command);

    void SetOverrideBinding(const std::string& owner, bool priority,
                            const std::string& key, const std::string& command);
    void ClearOverrideBindings(const std::string& owner);

    static std::string ActionButtonCommand(uint8_t slot);

    bool ProcessKeyDown(const std::string& key);
    bool ProcessKeyUp(const std::string& key);
    bool ProcessMouseButtonDown(std::uint32_t button_flag,
                                std::uint16_t modifier_state);
    bool ProcessMouseButtonUp(std::uint32_t button_flag,
                              std::uint16_t modifier_state);

    [[nodiscard]] BindingProfiles& GetManager() { return manager_; }
    [[nodiscard]] const BindingProfiles& GetManager() const { return manager_; }

    void Reset();

 private:
    KeybindSystem() : input_runtime_(manager_) {}

    BindingProfiles manager_;
    actions::bindings::adapters::platform::SdlBindingInputRuntime
        input_runtime_;
    mutable std::mutex mutex_;
};

}

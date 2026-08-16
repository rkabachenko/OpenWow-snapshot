#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class KeybindCategory : uint8_t {
    Movement      = 0,
    Chat          = 1,
    ActionBar     = 2,
    Targeting     = 3,
    Interface     = 4,
    Camera        = 5,
    Misc          = 6,
    MultiActionBar = 7,
    RaidTarget    = 8,
    Vehicle       = 9,
};

struct KeybindActionEntry {
    std::string      actionName;
    std::string      displayName;
    KeybindCategory  category{KeybindCategory::Movement};
    std::string      primaryKey;
    std::string      secondaryKey;
};

class KeybindUIDisplay {
 public:
    KeybindUIDisplay() = default;

    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const;

    void SetBindings(const std::vector<KeybindActionEntry>& bindings);
    [[nodiscard]] std::vector<KeybindActionEntry> GetBindings() const;
    [[nodiscard]] std::vector<KeybindActionEntry> GetBindingsForCategory(
        KeybindCategory category) const;

    [[nodiscard]] uint8_t GetCategoryCount() const;
    [[nodiscard]] KeybindCategory GetActiveCategory() const;
    void SetActiveCategory(KeybindCategory category);

    bool SetBinding(const std::string& actionName, bool isPrimary,
                    const std::string& keyString);
    void ClearBinding(const std::string& actionName, bool isPrimary);

    [[nodiscard]] std::optional<std::string> GetConflict(
        const std::string& keyString) const;

    [[nodiscard]] bool HasUnsavedChanges() const;
    void SaveChanges();
    void RevertChanges();
    void ResetToDefaults();
    [[nodiscard]] uint32_t GetPendingChangeCount() const;

 private:
    static constexpr uint8_t kCategoryCount = 10;

    bool                             open_{false};
    KeybindCategory                  activeCategory_{KeybindCategory::Movement};
    std::vector<KeybindActionEntry>  bindings_;
    std::vector<KeybindActionEntry>  savedBindings_;

    [[nodiscard]] int FindAction(const std::string& actionName) const;
};

}


#include "openwow/game/keybinding_ui_display.h"

#include <algorithm>

namespace openwow::game {

int KeybindUIDisplay::FindAction(const std::string& actionName) const {
    for (int i = 0; i < static_cast<int>(bindings_.size()); ++i) {
        if (bindings_[i].actionName == actionName) return i;
    }
    return -1;
}

void KeybindUIDisplay::Open() {
    open_ = true;

    savedBindings_ = bindings_;
}

void KeybindUIDisplay::Close() {
    open_ = false;
}

bool KeybindUIDisplay::IsOpen() const {
    return open_;
}

void KeybindUIDisplay::SetBindings(
    const std::vector<KeybindActionEntry>& bindings) {
    bindings_ = bindings;
    savedBindings_ = bindings;
}

std::vector<KeybindActionEntry> KeybindUIDisplay::GetBindings() const {
    return bindings_;
}

std::vector<KeybindActionEntry> KeybindUIDisplay::GetBindingsForCategory(
    KeybindCategory category) const {
    std::vector<KeybindActionEntry> result;
    for (const auto& b : bindings_) {
        if (b.category == category) {
            result.push_back(b);
        }
    }
    return result;
}

uint8_t KeybindUIDisplay::GetCategoryCount() const {
    return kCategoryCount;
}

KeybindCategory KeybindUIDisplay::GetActiveCategory() const {
    return activeCategory_;
}

void KeybindUIDisplay::SetActiveCategory(KeybindCategory category) {
    activeCategory_ = category;
}

bool KeybindUIDisplay::SetBinding(const std::string& actionName, bool isPrimary,
                                   const std::string& keyString) {
    int idx = FindAction(actionName);
    if (idx < 0) return false;
    if (keyString.empty()) return false;

    if (isPrimary) {
        bindings_[idx].primaryKey = keyString;
    } else {
        bindings_[idx].secondaryKey = keyString;
    }
    return true;
}

void KeybindUIDisplay::ClearBinding(const std::string& actionName,
                                     bool isPrimary) {
    int idx = FindAction(actionName);
    if (idx < 0) return;

    if (isPrimary) {
        bindings_[idx].primaryKey.clear();
    } else {
        bindings_[idx].secondaryKey.clear();
    }
}

std::optional<std::string> KeybindUIDisplay::GetConflict(
    const std::string& keyString) const {
    if (keyString.empty()) return std::nullopt;
    for (const auto& b : bindings_) {
        if (b.primaryKey == keyString || b.secondaryKey == keyString) {
            return b.actionName;
        }
    }
    return std::nullopt;
}

bool KeybindUIDisplay::HasUnsavedChanges() const {
    return GetPendingChangeCount() > 0;
}

void KeybindUIDisplay::SaveChanges() {
    savedBindings_ = bindings_;
}

void KeybindUIDisplay::RevertChanges() {
    bindings_ = savedBindings_;
}

void KeybindUIDisplay::ResetToDefaults() {
    for (auto& b : bindings_) {
        b.primaryKey.clear();
        b.secondaryKey.clear();
    }
}

uint32_t KeybindUIDisplay::GetPendingChangeCount() const {
    if (bindings_.size() != savedBindings_.size()) {

        return static_cast<uint32_t>(bindings_.size());
    }
    uint32_t count = 0;
    for (size_t i = 0; i < bindings_.size(); ++i) {
        if (bindings_[i].primaryKey != savedBindings_[i].primaryKey ||
            bindings_[i].secondaryKey != savedBindings_[i].secondaryKey) {
            ++count;
        }
    }
    return count;
}

}

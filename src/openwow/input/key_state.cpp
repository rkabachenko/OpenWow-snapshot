
#include "openwow/input/key_state.h"
#include <algorithm>

namespace openwow::input {

void KeyStateTracker::SetKeyDown(uint32_t keyCode) {
    auto& info = keys_[keyCode];
    if (!info.down) {
        info.down     = true;
        info.duration = 0.0f;
        pressed_this_frame_.insert(keyCode);
    }
}

void KeyStateTracker::SetKeyUp(uint32_t keyCode) {
    auto it = keys_.find(keyCode);
    if (it != keys_.end() && it->second.down) {
        it->second.down     = false;
        it->second.duration = 0.0f;
        released_this_frame_.insert(keyCode);
    }
}

bool KeyStateTracker::IsKeyDown(uint32_t keyCode) const {
    auto it = keys_.find(keyCode);
    return it != keys_.end() && it->second.down;
}

bool KeyStateTracker::IsKeyUp(uint32_t keyCode) const {
    return !IsKeyDown(keyCode);
}

bool KeyStateTracker::WasKeyPressed(uint32_t keyCode) const {
    return pressed_this_frame_.contains(keyCode);
}

bool KeyStateTracker::WasKeyReleased(uint32_t keyCode) const {
    return released_this_frame_.contains(keyCode);
}

float KeyStateTracker::GetDownDuration(uint32_t keyCode) const {
    auto it = keys_.find(keyCode);
    if (it != keys_.end() && it->second.down) {
        return it->second.duration;
    }
    return 0.0f;
}

bool KeyStateTracker::IsModDown(uint32_t modifier) const {

    if (modifier & kModMaskShift) {
        if (!IsKeyDown(225) && !IsKeyDown(229)) return false;
    }
    if (modifier & kModMaskCtrl) {
        if (!IsKeyDown(224) && !IsKeyDown(228)) return false;
    }
    if (modifier & kModMaskAlt) {
        if (!IsKeyDown(226) && !IsKeyDown(230)) return false;
    }
    return true;
}

std::vector<uint32_t> KeyStateTracker::GetPressedKeys() const {
    return {pressed_this_frame_.begin(), pressed_this_frame_.end()};
}

std::vector<uint32_t> KeyStateTracker::GetHeldKeys() const {
    std::vector<uint32_t> result;
    for (auto& [code, info] : keys_) {
        if (info.down) {
            result.push_back(code);
        }
    }
    return result;
}

std::string KeyStateTracker::GetInputString() const {
    return input_string_;
}

void KeyStateTracker::AddCharInput(char c) {
    input_string_ += c;
}

void KeyStateTracker::ClearFrameState() {
    pressed_this_frame_.clear();
    released_this_frame_.clear();
    input_string_.clear();
}

void KeyStateTracker::Update(float dt) {
    for (auto& [code, info] : keys_) {
        if (info.down) {
            info.duration += dt;
        }
    }
}

void KeyStateTracker::Reset() {
    keys_.clear();
    pressed_this_frame_.clear();
    released_this_frame_.clear();
    input_string_.clear();
}

}

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::input {

enum KeyModMask : uint32_t {
    kModMaskShift = 1u << 0,
    kModMaskCtrl  = 1u << 1,
    kModMaskAlt   = 1u << 2,
};

class KeyStateTracker {
public:
    KeyStateTracker() = default;

    void SetKeyDown(uint32_t keyCode);
    void SetKeyUp(uint32_t keyCode);

    [[nodiscard]] bool IsKeyDown(uint32_t keyCode) const;
    [[nodiscard]] bool IsKeyUp(uint32_t keyCode) const;

    [[nodiscard]] bool WasKeyPressed(uint32_t keyCode) const;
    [[nodiscard]] bool WasKeyReleased(uint32_t keyCode) const;

    [[nodiscard]] float GetDownDuration(uint32_t keyCode) const;

    [[nodiscard]] bool IsModDown(uint32_t modifier) const;

    [[nodiscard]] std::vector<uint32_t> GetPressedKeys() const;
    [[nodiscard]] std::vector<uint32_t> GetHeldKeys() const;

    [[nodiscard]] std::string GetInputString() const;
    void AddCharInput(char c);

    void ClearFrameState();

    void Update(float dt);

    void Reset();

private:
    struct KeyInfo {
        bool  down     = false;
        float duration = 0.0f;
    };

    std::unordered_map<uint32_t, KeyInfo> keys_;
    std::unordered_set<uint32_t> pressed_this_frame_;
    std::unordered_set<uint32_t> released_this_frame_;
    std::string input_string_;
};

}

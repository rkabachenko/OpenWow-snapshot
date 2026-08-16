
#include "openwow/core/window_manager.h"

#include <algorithm>

namespace openwow::core {

WindowManager& WindowManager::Instance() {
    static WindowManager instance;
    return instance;
}

void WindowManager::SetState(const WindowState& state) {
    std::lock_guard lock(mutex_);
    state_ = state;
}

WindowState WindowManager::GetState() const {
    std::lock_guard lock(mutex_);
    return state_;
}

void WindowManager::SetSize(uint32_t w, uint32_t h) {
    std::lock_guard lock(mutex_);
    state_.width  = w;
    state_.height = h;
}

uint32_t WindowManager::GetWidth() const {
    std::lock_guard lock(mutex_);
    return state_.width;
}

uint32_t WindowManager::GetHeight() const {
    std::lock_guard lock(mutex_);
    return state_.height;
}

void WindowManager::SetPosition(int32_t x, int32_t y) {
    std::lock_guard lock(mutex_);
    state_.posX = x;
    state_.posY = y;
}

void WindowManager::SetMode(WindowMode mode) {
    std::lock_guard lock(mutex_);
    state_.mode = mode;
}

WindowMode WindowManager::GetMode() const {
    std::lock_guard lock(mutex_);
    return state_.mode;
}

void WindowManager::SetTitle(const std::string& title) {
    std::lock_guard lock(mutex_);
    state_.title = title;
}

std::string WindowManager::GetTitle() const {
    std::lock_guard lock(mutex_);
    return state_.title;
}

void WindowManager::SetMinimized(bool minimized) {
    std::lock_guard lock(mutex_);
    state_.isMinimized = minimized;
}

bool WindowManager::IsMinimized() const {
    std::lock_guard lock(mutex_);
    return state_.isMinimized;
}

void WindowManager::SetFocused(bool focused) {
    std::lock_guard lock(mutex_);
    state_.isFocused = focused;
}

bool WindowManager::IsFocused() const {
    std::lock_guard lock(mutex_);
    return state_.isFocused;
}

void WindowManager::SetMouseCapture(bool capture) {
    std::lock_guard lock(mutex_);
    state_.isMouseCaptured = capture;
}

bool WindowManager::IsMouseCaptured() const {
    std::lock_guard lock(mutex_);
    return state_.isMouseCaptured;
}

float WindowManager::GetDPIScale() const {
    std::lock_guard lock(mutex_);
    return state_.dpiScale;
}

float WindowManager::GetAspectRatio() const {
    std::lock_guard lock(mutex_);
    if (state_.height == 0) return 0.0f;
    return static_cast<float>(state_.width) / static_cast<float>(state_.height);
}

std::string WindowManager::GetModeName(WindowMode mode) {
    switch (mode) {
        case WindowMode::Windowed:           return "Windowed";
        case WindowMode::Fullscreen:         return "Fullscreen";
        case WindowMode::FullscreenDesktop:  return "Fullscreen (Desktop)";
        case WindowMode::BorderlessWindowed: return "Borderless Windowed";
    }
    return "Unknown";
}

std::vector<std::pair<uint32_t, uint32_t>> WindowManager::GetAvailableResolutions() {
    return {
        {800,  600},
        {1024, 768},
        {1280, 720},
        {1280, 800},
        {1280, 1024},
        {1366, 768},
        {1440, 900},
        {1600, 900},
        {1680, 1050},
        {1920, 1080},
        {1920, 1200},
        {2560, 1440},
        {2560, 1600},
        {3440, 1440},
        {3840, 2160},
    };
}

void WindowManager::SetVSync(bool on) {
    std::lock_guard lock(mutex_);
    vsync_ = on;
}

bool WindowManager::GetVSync() const {
    std::lock_guard lock(mutex_);
    return vsync_;
}

void WindowManager::SetGamma(float gamma) {
    std::lock_guard lock(mutex_);
    gamma_ = std::clamp(gamma, 0.5f, 2.5f);
}

float WindowManager::GetGamma() const {
    std::lock_guard lock(mutex_);
    return gamma_;
}

void WindowManager::Reset() {
    std::lock_guard lock(mutex_);
    state_ = WindowState{};
    vsync_ = true;
    gamma_ = 1.0f;
}

}

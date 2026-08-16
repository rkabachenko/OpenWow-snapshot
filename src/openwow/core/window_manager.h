
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace openwow::core {

enum class WindowMode : uint8_t {
    Windowed          = 0,
    Fullscreen        = 1,
    FullscreenDesktop = 2,
    BorderlessWindowed = 3,
};

struct WindowState {
    uint32_t    width          = 1024;
    uint32_t    height         = 768;
    int32_t     posX           = 0;
    int32_t     posY           = 0;
    WindowMode  mode           = WindowMode::Windowed;
    bool        isMinimized    = false;
    bool        isFocused      = true;
    bool        isMouseCaptured = false;
    std::string title          = "World of Warcraft";
    float       dpiScale       = 1.0f;
};

class WindowManager {
public:
    static WindowManager& Instance();

    void                SetState(const WindowState& state);
    [[nodiscard]] WindowState GetState() const;

    void                SetSize(uint32_t w, uint32_t h);
    [[nodiscard]] uint32_t   GetWidth() const;
    [[nodiscard]] uint32_t   GetHeight() const;

    void                SetPosition(int32_t x, int32_t y);

    void                SetMode(WindowMode mode);
    [[nodiscard]] WindowMode GetMode() const;

    void                SetTitle(const std::string& title);
    [[nodiscard]] std::string GetTitle() const;

    void                SetMinimized(bool minimized);
    [[nodiscard]] bool  IsMinimized() const;

    void                SetFocused(bool focused);
    [[nodiscard]] bool  IsFocused() const;

    void                SetMouseCapture(bool capture);
    [[nodiscard]] bool  IsMouseCaptured() const;

    [[nodiscard]] float GetDPIScale() const;
    [[nodiscard]] float GetAspectRatio() const;

    [[nodiscard]] static std::string GetModeName(WindowMode mode);

    [[nodiscard]] static std::vector<std::pair<uint32_t, uint32_t>> GetAvailableResolutions();

    void                SetVSync(bool on);
    [[nodiscard]] bool  GetVSync() const;

    void                SetGamma(float gamma);
    [[nodiscard]] float GetGamma() const;

    void Reset();

private:
    WindowManager() = default;

    mutable std::mutex mutex_;
    WindowState state_;
    bool  vsync_ = true;
    float gamma_ = 1.0f;
};

}

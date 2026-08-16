
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace openwow::platform {

inline constexpr std::uint32_t kRetailMinimumClientWidth = 320;
inline constexpr std::uint32_t kRetailMinimumClientHeight = 240;

struct RelativeCursorMotion {
    bool has_delta = false;
    int delta_x = 0;
    int delta_y = 0;
};

enum class WindowMode {
    Windowed,
    Fullscreen,
    WindowedFullscreen,
};

struct DisplayModeRequest {
    WindowMode mode = WindowMode::Windowed;
    uint32_t pixel_width = 0;
    uint32_t pixel_height = 0;
    uint32_t refresh_rate = 0;
    bool maximize = false;
};

struct WindowConfig {
    std::string title    = "World of Warcraft";
    uint32_t    width    = 1024;
    uint32_t    height   = 768;
    WindowMode  mode     = WindowMode::Windowed;
    bool        resizable = true;
    bool        vsync    = true;
    int32_t     x        = -1;
    int32_t     y        = -1;
    uint32_t    min_width  = kRetailMinimumClientWidth;
    uint32_t    min_height = kRetailMinimumClientHeight;
};

class WindowManager {
public:
    static WindowManager& Get();

    bool Initialize(const WindowConfig& config);
    void AdoptExternalWindow(void* sdl_window);
    void Shutdown();
    [[nodiscard]] bool IsInitialized() const;

    [[nodiscard]] uint32_t GetWidth() const;
    [[nodiscard]] uint32_t GetHeight() const;
    [[nodiscard]] float    GetAspectRatio() const;
    [[nodiscard]] bool     IsFullscreen() const;
    [[nodiscard]] bool     IsMinimized() const;
    [[nodiscard]] bool     IsFocused() const;
    void SetTitle(const std::string& title);
    [[nodiscard]] std::string GetTitle() const;

    void SetWindowMode(WindowMode mode);
    void SetResolution(uint32_t width, uint32_t height);
    [[nodiscard]] bool ApplyDisplayMode(const DisplayModeRequest& request);
    void SetClientSize(uint32_t width, uint32_t height);
    void ToggleFullscreen();

    void  SetGamma(float gamma);
    [[nodiscard]] float GetGamma() const;

    void SetCursorPosition(int x, int y);
    [[nodiscard]] std::optional<std::pair<int, int>> GetCursorPositionInWindow();

    [[nodiscard]] std::optional<std::pair<int, int>> ResolveLogicalCursorPosition();

    [[nodiscard]] std::optional<std::pair<int, int>>
    ResolveLogicalCursorPositionInDrawablePixels();
    [[nodiscard]] std::pair<int, int> ResolveMouseButtonDispatchPosition(int raw_x,
                                                                         int raw_y,
                                                                         bool relative_mode_active) const;

    [[nodiscard]] bool BeginRelativeCursorMode();

    void EndRelativeCursorMode();
    [[nodiscard]] bool IsRelativeCursorModeActive() const;

    [[nodiscard]] RelativeCursorMotion HandleRelativeCursorMotion();
    [[nodiscard]] bool CaptureCursorAnchor();
    [[nodiscard]] bool RestoreCursorAnchor();
    [[nodiscard]] bool HasCursorAnchor() const;
    void ClearCursorAnchor();
    void CaptureMouse(bool capture);
    void BeginMouseButtonCapture(std::uint32_t button_flag);
    void EndMouseButtonCapture(std::uint32_t button_flag);
    void ResetMouseButtonCapture();
    [[nodiscard]] std::uint32_t GetMouseButtonCaptureMask() const;

    [[nodiscard]] void* GetNativeHandle() const;
    [[nodiscard]] void* GetNativeDisplayHandle() const;

    void PollEvents();
    [[nodiscard]] bool ShouldClose() const;

    using ResizeCallback = std::function<void(uint32_t, uint32_t)>;
    using FocusCallback  = std::function<void(bool)>;
    using CloseCallback  = std::function<void()>;

    void SetResizeCallback(ResizeCallback cb);
    void SetFocusCallback(FocusCallback cb);
    void SetCloseCallback(CloseCallback cb);

    void SetCursorPositionOverrideForTests(int x, int y);
    void ClearCursorPositionOverrideForTests();
    void Reset();

private:
    WindowManager() = default;
    void SaveCursorAnchorIfUnlocked(int x, int y);

    void* window_ = nullptr;

    uint32_t   width_  = 0;
    uint32_t   height_ = 0;
    uint32_t   min_width_ = kRetailMinimumClientWidth;
    uint32_t   min_height_ = kRetailMinimumClientHeight;
    WindowMode mode_   = WindowMode::Windowed;
    bool       should_close_ = false;
    bool       minimized_    = false;
    bool       focused_      = true;
    float      gamma_        = 1.0f;
    std::string title_       = "World of Warcraft";
    bool       initialized_  = false;
    bool       owns_window_  = false;
    bool       title_was_set_explicitly_ = false;
    std::uint32_t mouse_button_capture_mask_ = 0;

    ResizeCallback resize_cb_;
    FocusCallback  focus_cb_;
    CloseCallback  close_cb_;
    std::optional<std::pair<int, int>> cursor_position_override_;
    std::optional<std::pair<int, int>> cursor_anchor_;

    bool relative_cursor_mode_active_ = false;

    std::optional<std::pair<uint32_t, uint32_t>> pending_windowed_size_;
    bool pending_windowed_retried_ = false;
};

}

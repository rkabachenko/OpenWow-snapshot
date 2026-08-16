
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::core {

void LoadingScreen_InitFont(int loading_screen_type, bool update_flag);

using RenderBootstrapFpsOverlayReleaseFn = void (*)(void*);

enum class RenderBootstrapFpsOverlaySeverity : std::uint8_t {
    kInfo    = 0,
    kWarning = 1,
    kError   = 2,
    kFatal   = 3,
};

struct RenderBootstrapFpsOverlayEntry {
    std::uint32_t timestamp_ms{0};
    std::array<char, 256> text{};
    void* string_handle{nullptr};
    RenderBootstrapFpsOverlayReleaseFn release{nullptr};
    std::uint8_t red{0};
    std::uint8_t green{0};
    std::uint8_t blue{0};
    std::uint8_t alpha{0};
};

struct RenderBootstrapFpsOverlayState {
    bool initialized{false};
    bool error_output_enabled{false};

    bool error_display_hidden{false};

    bool dirty{false};

    std::uint32_t next_entry_index{0};

    void* string_batch{nullptr};
    RenderBootstrapFpsOverlayReleaseFn release_string_batch{nullptr};
    std::uintptr_t font_object{0};
    float font_height{0.0f};
    std::array<RenderBootstrapFpsOverlayEntry, 10> entries{};
};

struct RenderBootstrapFpsOverlayPaintLine {
    std::string_view text{};
    float x{0.0f};
    float y{0.0f};
    std::uint8_t red{0};
    std::uint8_t green{0};
    std::uint8_t blue{0};
    std::uint8_t alpha{0};
};

struct RenderBootstrapFpsOverlayPaintOutput {
    std::array<RenderBootstrapFpsOverlayPaintLine, 10> lines{};
    std::size_t count{0};
};

RenderBootstrapFpsOverlayState& GetRenderBootstrapFpsOverlayState();

void RenderBootstrap_FpsOverlayEnableErrors();

void RenderBootstrap_FpsOverlayDisableErrors();

void RenderBootstrap_FpsOverlayShowErrors();

void RenderBootstrap_FpsOverlayHideErrors();

bool RenderBootstrap_FpsOverlayErrorsEnabled();

bool RenderBootstrap_FpsOverlayErrorsShown();

void RenderBootstrap_FpsOverlayQueueLine(
    std::string_view text,
    RenderBootstrapFpsOverlaySeverity severity);

RenderBootstrapFpsOverlayPaintOutput RenderBootstrap_FpsOverlayPaint(
    float anchor_x,
    float anchor_top);

void RenderBootstrap_FpsCleanup();

std::string RaceId_ToModelName(int race_id);

}

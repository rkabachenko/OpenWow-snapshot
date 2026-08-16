
#include "openwow/core/init_subsystems.h"

#include "openwow/core/client_misc.h"
#include "openwow/core/console.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/screens/loading_screen_manager.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>

namespace openwow::core {

namespace {

RenderBootstrapFpsOverlayState g_renderBootstrapFpsOverlayState;

struct RenderBootstrapFpsOverlayRgb {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

constexpr std::array<RenderBootstrapFpsOverlayRgb, 4>
    kRenderBootstrapFpsOverlaySeverityColors{{
        {255, 255, 255},
        {255, 255, 127},
        {255, 127, 127},
        {127, 255, 255},
    }};

constexpr std::uint32_t kRenderBootstrapFpsOverlayFadeStartMs = 10000u;
constexpr std::uint32_t kRenderBootstrapFpsOverlayExpireMs = 13000u;
constexpr float kRenderBootstrapFpsOverlayRowStep = 0.025f;
constexpr int kRenderBootstrapFpsOverlayFirstRow = 12;

std::uint32_t RenderBootstrapFpsOverlayNowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

void CopyRenderBootstrapFpsOverlayText(std::array<char, 256>& storage,
                                       std::string_view text) {
    storage.fill('\0');

    const auto copy_length = std::min(text.size(), storage.size() - 1u);
    if (copy_length != 0u) {
        std::memcpy(storage.data(), text.data(), copy_length);
    }
}

const RenderBootstrapFpsOverlayRgb& GetRenderBootstrapFpsOverlaySeverityColor(
    RenderBootstrapFpsOverlaySeverity severity) {
    return kRenderBootstrapFpsOverlaySeverityColors[static_cast<std::size_t>(
        severity)];
}

int GetRenderBootstrapFpsOverlayConsoleColorClass(
    RenderBootstrapFpsOverlaySeverity severity) {
    return severity == RenderBootstrapFpsOverlaySeverity::kInfo ||
                   severity == RenderBootstrapFpsOverlaySeverity::kWarning
               ? ida::COLOR_WARNING
               : ida::COLOR_ERROR;
}

void ResetRenderBootstrapFpsOverlayEntry(RenderBootstrapFpsOverlayEntry& entry) {
    if (entry.string_handle && entry.release) {
        entry.release(entry.string_handle);
    }

    entry.string_handle = nullptr;
    entry.release = nullptr;
}

}

void LoadingScreen_InitFont(int loading_screen_type, bool update_flag) {
    openwow::game::LoadingScreen_PreInitFont();

    auto& loading_screen = openwow::screens::LoadingScreenManager::Get();
    const openwow::ui::UiAspectScaleState aspect_state =
        openwow::ui::GetCachedUiAspectScaleState();
    loading_screen.RebuildTextPresentation(
        loading_screen_type, aspect_state.kx,
        aspect_state.vertical_scale);
    loading_screen.SetTransportWorldEntryHold(false);
    loading_screen.ResetCompositeProgress(update_flag);
    LoadingScreen_RegisterStormInitHandlers();
}

RenderBootstrapFpsOverlayState& GetRenderBootstrapFpsOverlayState() {
    return g_renderBootstrapFpsOverlayState;
}

void RenderBootstrap_FpsOverlayEnableErrors() {
    auto& state = GetRenderBootstrapFpsOverlayState();
    if (!state.error_output_enabled && state.initialized) {
        state.error_output_enabled = true;
    }
}

void RenderBootstrap_FpsOverlayDisableErrors() {
    auto& state = GetRenderBootstrapFpsOverlayState();
    if (state.error_output_enabled) {
        state.error_output_enabled = false;
    }
}

void RenderBootstrap_FpsOverlayShowErrors() {
    auto& state = GetRenderBootstrapFpsOverlayState();
    if (state.error_display_hidden) {
        state.error_display_hidden = false;
    }
}

void RenderBootstrap_FpsOverlayHideErrors() {
    auto& state = GetRenderBootstrapFpsOverlayState();
    if (!state.error_display_hidden) {
        state.error_display_hidden = true;
    }
}

bool RenderBootstrap_FpsOverlayErrorsEnabled() {
    return GetRenderBootstrapFpsOverlayState().error_output_enabled;
}

bool RenderBootstrap_FpsOverlayErrorsShown() {
    return !GetRenderBootstrapFpsOverlayState().error_display_hidden;
}

void RenderBootstrap_FpsOverlayQueueLine(
    std::string_view text,
    RenderBootstrapFpsOverlaySeverity severity) {
    auto& state = GetRenderBootstrapFpsOverlayState();
    if (!state.error_output_enabled) {
        return;
    }

    auto& entry = state.entries[state.next_entry_index];
    entry.timestamp_ms = RenderBootstrapFpsOverlayNowMs();
    CopyRenderBootstrapFpsOverlayText(entry.text, text);

    const auto& color = GetRenderBootstrapFpsOverlaySeverityColor(severity);
    entry.red = color.red;
    entry.green = color.green;
    entry.blue = color.blue;
    entry.alpha = 0xFF;

    state.dirty = true;
    state.next_entry_index = (state.next_entry_index + 1u) %
                             static_cast<std::uint32_t>(state.entries.size());

    ida::ConsoleAddLine(std::string(text),
                        GetRenderBootstrapFpsOverlayConsoleColorClass(severity));
}

RenderBootstrapFpsOverlayPaintOutput RenderBootstrap_FpsOverlayPaint(
    float anchor_x,
    float anchor_top) {
    RenderBootstrapFpsOverlayPaintOutput output{};
    auto& state = GetRenderBootstrapFpsOverlayState();
    if (state.error_display_hidden) {
        return output;
    }

    const std::uint32_t now_ms = RenderBootstrapFpsOverlayNowMs();
    for (std::size_t row = 0; row < state.entries.size(); ++row) {
        auto& entry =
            state.entries[(state.next_entry_index + row) % state.entries.size()];
        if (entry.timestamp_ms == 0u) {
            continue;
        }

        const std::uint32_t age_ms = now_ms - entry.timestamp_ms;
        if (age_ms > kRenderBootstrapFpsOverlayExpireMs) {
            ResetRenderBootstrapFpsOverlayEntry(entry);
            continue;
        }

        if (entry.text[0] == '\0') {
            continue;
        }

        auto& line = output.lines[output.count++];
        line.text = std::string_view(entry.text.data());
        line.x = anchor_x;
        line.y = anchor_top - static_cast<float>(kRenderBootstrapFpsOverlayFirstRow +
                                                 static_cast<int>(row)) *
                                 kRenderBootstrapFpsOverlayRowStep;
        line.red = entry.red;
        line.green = entry.green;
        line.blue = entry.blue;
        line.alpha = age_ms > kRenderBootstrapFpsOverlayFadeStartMs
                         ? static_cast<std::uint8_t>(
                               (kRenderBootstrapFpsOverlayExpireMs - age_ms) /
                               12u)
                         : entry.alpha;
    }

    state.dirty = false;
    return output;
}

void RenderBootstrap_FpsCleanup() {
    auto& state = GetRenderBootstrapFpsOverlayState();
    if (!state.initialized) {
        return;
    }

    state.initialized = false;

    if (state.string_batch && state.release_string_batch) {
        state.release_string_batch(state.string_batch);
    }
    state.string_batch = nullptr;
    state.release_string_batch = nullptr;

    for (auto& entry : state.entries) {
        ResetRenderBootstrapFpsOverlayEntry(entry);
    }

}

std::string RaceId_ToModelName(int race_id) {
    switch (race_id) {
        case 1: return "Human";
        case 2: return "Orc";
        case 3: return "Dwarf";
        case 4: return "NightElf";
        case 5: return "Undead";
        case 6: return "Tauren";
        case 7: return "Gnome";
        case 8: return "Troll";
        default: return {};
    }
}

}

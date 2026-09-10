#pragma once

namespace openwow::ui::game {

class GameUIManager;

// Platform option range, not a clamp on frame scales or console CVar writes.
#if defined(OPENWOW_PLATFORM_IOS)
inline constexpr float kConfiguredUiScaleSliderMaximum = 1.5F;
#else
inline constexpr float kConfiguredUiScaleSliderMaximum = 1.0F;
#endif

float ComputeDefaultGameUiRootScale(int viewport_width,
                                    int viewport_height,
                                    double aspect_ratio);
float ClampConfiguredGameUiScale(float requested_scale, double aspect_ratio);

[[nodiscard]] float ComputeGameUiRenderPixelScale(
    float viewport_height, float effective_frame_scale) noexcept;

void ApplyDefaultGameUiScale(GameUIManager& manager,
                             bool force_viewport_refresh,
                             bool fire_display_size_changed);
void ApplyConfiguredGameUiScale(GameUIManager& manager, float requested_scale);
void SyncGameUiScaleFromUseUiScaleCVar(GameUIManager& manager);
void SyncGameUiScaleFromCVars(GameUIManager& manager,
                              bool force_default_viewport_refresh);
void RegisterGameUiScaleCallbacks();

void ResetGameUiScaleStateForTests();

}

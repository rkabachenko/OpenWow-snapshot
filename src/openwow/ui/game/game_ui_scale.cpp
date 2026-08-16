#include "openwow/ui/game/game_ui_scale.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "openwow/game/game_misc_utils.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/ui_coordinate_space.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/game_ui_manager.h"

namespace openwow::ui::game {
namespace {

constexpr float kMinimumConfiguredUiScale = 0.64f;
constexpr float kAspectReloadEpsilon = 0.001f;

struct GameUiScaleState {
  int cached_default_width{0};
  int cached_default_height{0};
  float last_aspect_ratio{4.0f / 3.0f};
  bool aspect_initialized{false};
};

GameUiScaleState g_game_ui_scale_state;

template <typename Manager>
void FireScaleChangedEvents(Manager& manager,
                            const bool fire_display_size_changed) {
  manager.frame_events().dispatcher().FireEvent(
      events::UPDATE_FLOATING_CHAT_WINDOWS);
  if (fire_display_size_changed) {
    manager.frame_events().dispatcher().FireEvent(events::DISPLAY_SIZE_CHANGED);
  }
}

template <typename Manager>
void RequestLifecycleReloadIfAspectChanged(Manager& manager,
                                            const float aspect_ratio) {
  if (!g_game_ui_scale_state.aspect_initialized) {
    g_game_ui_scale_state.last_aspect_ratio = aspect_ratio;
    g_game_ui_scale_state.aspect_initialized = true;
    return;
  }

  if (std::fabs(g_game_ui_scale_state.last_aspect_ratio - aspect_ratio)
      < kAspectReloadEpsilon) {
    return;
  }

  manager.RequestWorldUiReload();
  g_game_ui_scale_state.last_aspect_ratio = aspect_ratio;
}

}

namespace {

template <typename Manager>
void ApplyDefaultGameUiScaleImpl(Manager& manager,
                                 const bool force_viewport_refresh,
                                 const bool fire_display_size_changed) {
  const int viewport_width = static_cast<int>(manager.screen_width());
  const int viewport_height = static_cast<int>(manager.screen_height());
  if (viewport_width <= 0 || viewport_height <= 0) {
    if (fire_display_size_changed) {
      manager.frame_events().dispatcher().FireEvent(events::DISPLAY_SIZE_CHANGED);
    }
    return;
  }

  const bool viewport_changed =
      force_viewport_refresh
      || g_game_ui_scale_state.cached_default_width != viewport_width
      || g_game_ui_scale_state.cached_default_height != viewport_height;
  if (!viewport_changed) {
    if (fire_display_size_changed) {
      manager.frame_events().dispatcher().FireEvent(events::DISPLAY_SIZE_CHANGED);
    }
    return;
  }

  g_game_ui_scale_state.cached_default_width = viewport_width;
  g_game_ui_scale_state.cached_default_height = viewport_height;

  const float aspect_ratio =
      static_cast<float>(openwow::game::GetWidescreenAspectRatio());
  openwow::ui::SetCachedUiAspectScaleState(aspect_ratio);
  manager.SetRootScale(
      ComputeDefaultGameUiRootScale(viewport_width, viewport_height, aspect_ratio),
      true);
  FireScaleChangedEvents(manager, fire_display_size_changed);
  RequestLifecycleReloadIfAspectChanged(manager, aspect_ratio);
}

template <typename Manager>
void ApplyConfiguredGameUiScaleImpl(Manager& manager,
                                    const float requested_scale) {
  const float aspect_ratio =
      static_cast<float>(openwow::game::GetWidescreenAspectRatio());
  openwow::ui::SetCachedUiAspectScaleState(aspect_ratio);
  const float root_scale = std::max(
      ClampConfiguredGameUiScale(requested_scale, aspect_ratio),
      kMinimumConfiguredUiScale);
  manager.SetRootScale(root_scale, true);
  FireScaleChangedEvents(manager, false);
}

template <typename Manager>
void SyncGameUiScaleFromUseUiScaleCVarImpl(Manager& manager) {
  auto& cvars = CVarSystem::Instance();
  if (cvars.GetCVarInt("useUiScale") == 0) {
    ApplyDefaultGameUiScaleImpl(manager, true, true);
    return;
  }
  ApplyConfiguredGameUiScaleImpl(manager, cvars.GetCVarFloat("uiScale"));
}

}

float ComputeDefaultGameUiRootScale(const int viewport_width,
                                    const int viewport_height,
                                    const double aspect_ratio) {

  (void)viewport_width;
  (void)viewport_height;
  (void)aspect_ratio;
  return 1.0f;
}

float ClampConfiguredGameUiScale(const float requested_scale,
                                 const double aspect_ratio) {
  float clamped_scale = requested_scale;
  if (aspect_ratio * 3.0 < 4.0) {
    clamped_scale = std::min(clamped_scale,
                             static_cast<float>(aspect_ratio * 0.75));
  }
  return clamped_scale;
}

float ComputeGameUiRenderPixelScale(
    const float viewport_height,
    const float effective_frame_scale) noexcept {

  return openwow::ui::ResolveDevicePixelsPerUiUnit(viewport_height,
                                                   effective_frame_scale)
      .value;
}

void ApplyDefaultGameUiScale(GameUIManager& manager,
                             const bool force_viewport_refresh,
                             const bool fire_display_size_changed) {
  ApplyDefaultGameUiScaleImpl(manager, force_viewport_refresh,
                              fire_display_size_changed);
}

void ApplyConfiguredGameUiScale(GameUIManager& manager,
                                const float requested_scale) {
  ApplyConfiguredGameUiScaleImpl(manager, requested_scale);
}

void SyncGameUiScaleFromUseUiScaleCVar(GameUIManager& manager) {
  SyncGameUiScaleFromUseUiScaleCVarImpl(manager);
}

void SyncGameUiScaleFromCVars(GameUIManager& manager,
                              const bool force_default_viewport_refresh) {
  auto& cvars = CVarSystem::Instance();
  const bool use_ui_scale = cvars.GetCVarInt("useUiScale") != 0;
  const float requested_scale = cvars.GetCVarFloat("uiScale");
  const float aspect_ratio =
      static_cast<float>(openwow::game::GetWidescreenAspectRatio());
  const float clamped_scale =
      ClampConfiguredGameUiScale(requested_scale, aspect_ratio);

  if (!use_ui_scale || clamped_scale < kMinimumConfiguredUiScale) {
    ApplyDefaultGameUiScale(manager, force_default_viewport_refresh, true);
    return;
  }

  openwow::ui::SetCachedUiAspectScaleState(aspect_ratio);
  manager.SetRootScale(clamped_scale, true);
  FireScaleChangedEvents(manager, force_default_viewport_refresh);
  RequestLifecycleReloadIfAspectChanged(manager, aspect_ratio);
}

void RegisterGameUiScaleCallbacks() {
  static std::uint32_t ui_scale_callback_handle = 0;
  static std::uint32_t use_ui_scale_callback_handle = 0;

  auto& cvars = CVarSystem::Instance();
  if (ui_scale_callback_handle != 0) {
    cvars.RemoveCallback("uiScale", ui_scale_callback_handle);
  }
  if (use_ui_scale_callback_handle != 0) {
    cvars.RemoveCallback("useUiScale", use_ui_scale_callback_handle);
  }

  ui_scale_callback_handle =
      cvars.AddCallback("uiScale", [](const std::string&, const std::string&) {
        if (CVarSystem::Instance().GetCVarInt("useUiScale") == 0) {
          return;
        }

        auto* manager = runtime::WorldUiRuntimeContext::FromActiveLua();
        if (manager != nullptr) {
          ApplyConfiguredGameUiScaleImpl(
              *manager, CVarSystem::Instance().GetCVarFloat("uiScale"));
        }
      });

  use_ui_scale_callback_handle = cvars.AddCallback(
      "useUiScale", [](const std::string&, const std::string&) {
        auto* manager = runtime::WorldUiRuntimeContext::FromActiveLua();
        if (manager != nullptr) {
          SyncGameUiScaleFromUseUiScaleCVarImpl(*manager);
        }
      });
}

void ResetGameUiScaleStateForTests() {
  g_game_ui_scale_state = {};
}

}

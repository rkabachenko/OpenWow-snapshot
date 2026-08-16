#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/render/ui/ui_acceleration.h"
#include "openwow/render/resources/textures/texture_cache_budget.h"
#include "openwow/render/resources/textures/texture_filtering_mode.h"
#include "openwow/ui/game/cvar_system.h"

namespace openwow::client {

enum class InitialGlueScreen {
  kLogin,
  kMovie,
};

inline InitialGlueScreen SelectInitialGlueScreenAndConsumeMovieCvrs(
    ::openwow::ui::game::CVarSystem& cvars,
    int expansion_level_zero_based) {
  if (expansion_level_zero_based == 1) {
    if (cvars.GetCVarInt("expansionMovie") == 1) {
      (void)cvars.SetCVar("expansionMovie", "0");
      (void)cvars.SetCVar("movie", "0");
      return InitialGlueScreen::kMovie;
    }
    return InitialGlueScreen::kLogin;
  }

  if (cvars.GetCVarInt("movie") == 1) {
    (void)cvars.SetCVar("movie", "0");
    return InitialGlueScreen::kMovie;
  }

  return InitialGlueScreen::kLogin;
}

inline const char* InitialGlueScreenName(InitialGlueScreen screen) {
  return screen == InitialGlueScreen::kMovie ? "movie" : "login";
}

enum class GraphicsStartupCVarMode {
  kResetGammaAndWindowResizeLock = 0,
  kSyncUiFaster = 1,
  kResetStereoDefaults = 2,
};

struct GraphicsStartupBootstrapGateState {
  bool registration_immediate_pass{false};
  bool display_ready_pass{false};
};

inline bool ApplyGraphicsStartupCVarMode(
    ::openwow::ui::game::CVarSystem& cvars,
    GraphicsStartupCVarMode mode,
    bool startup_simple_ui_fast_path_enabled = false) {
  bool changed = false;
  auto set_default = [&](const char* name) {
    if (!cvars.Exists(name)) return false;
    const std::string default_value = cvars.GetCVarDefault(name);
    return cvars.SetCVar(name, default_value, true);
  };

  switch (mode) {
    case GraphicsStartupCVarMode::kResetGammaAndWindowResizeLock:
      if (cvars.Exists("DesktopGamma")) {
        changed |= cvars.SetCVar("DesktopGamma", "0", true);
      }
      if (cvars.Exists("Gamma")) {
        changed |= cvars.SetCVar("Gamma", "1.0", true);
      }
      changed |= set_default("windowResizeLock");
      break;

    case GraphicsStartupCVarMode::kSyncUiFaster:
      if (cvars.Exists("UIFaster")) {
        changed |= cvars.SetCVar("UIFaster",
                                 startup_simple_ui_fast_path_enabled ? "1" : "0",
                                 true);
      }
      break;

    case GraphicsStartupCVarMode::kResetStereoDefaults:
      changed |= set_default("gxStereoEnabled");
      changed |= set_default("gxStereoConvergence");
      changed |= set_default("gxStereoSeparation");
      break;
  }

  return changed;
}

inline int CountGraphicsStartupBootstrapPasses(
    const GraphicsStartupBootstrapGateState& gates) {
  return static_cast<int>(gates.registration_immediate_pass)
         + static_cast<int>(gates.display_ready_pass);
}

inline int ApplyGraphicsStartupBootstrapSequence(
    ::openwow::ui::game::CVarSystem& cvars,
    bool startup_simple_ui_fast_path_enabled,
    const GraphicsStartupBootstrapGateState& gates) {
  const auto apply_pass = [&]() {
    (void)ApplyGraphicsStartupCVarMode(
        cvars,
        GraphicsStartupCVarMode::kResetGammaAndWindowResizeLock);
    (void)ApplyGraphicsStartupCVarMode(
        cvars,
        GraphicsStartupCVarMode::kSyncUiFaster,
        startup_simple_ui_fast_path_enabled);
    (void)ApplyGraphicsStartupCVarMode(
        cvars,
        GraphicsStartupCVarMode::kResetStereoDefaults);
  };

  int passes = 0;
  if (gates.registration_immediate_pass) {
    apply_pass();
    ++passes;
  }
  if (gates.display_ready_pass) {
    apply_pass();
    ++passes;
  }

  return passes;
}

inline bool IsValidTextureFilteringMode(std::uint32_t mode) {
  return mode <= 5u;
}

struct TextureFilteringModeValidationResult {
  bool accepted{false};
  std::uint32_t requested_mode{0};
  std::string console_message;
};

inline TextureFilteringModeValidationResult ValidateTextureFilteringModeCallback(
    std::uint32_t requested_mode) {
  TextureFilteringModeValidationResult result;
  result.requested_mode = requested_mode;
  result.accepted = IsValidTextureFilteringMode(requested_mode);
  if (!result.accepted) {
    result.console_message =
        "Texture filtering mode must be in range 0 to 6.";
  }
  return result;
}

inline std::uint32_t ComputeTextureCacheBudgetLimitBytes(
    std::uint64_t physical_memory_bytes,
    int gx_api_id) {
  return ::openwow::render::GetTextureCacheBudgetLimitBytes(
      ::openwow::render::TextureCacheBudgetContext{.physical_memory_bytes = physical_memory_bytes,
       .backend = gx_api_id == 0
                      ? ::openwow::render::TextureCacheBackendClass::kOpenGl
                      : ::openwow::render::TextureCacheBackendClass::kModern});
}

inline std::uint32_t ComputeTextureCacheBudgetBytes(
    int requested_megabytes,
    std::uint64_t physical_memory_bytes,
    int gx_api_id) {
  return ::openwow::render::ResolveTextureCacheBudgetBytes(
      ::openwow::render::TextureCacheMegabytesToBytes(
          static_cast<std::uint32_t>(requested_megabytes)),
      {.physical_memory_bytes = physical_memory_bytes,
       .backend = gx_api_id == 0
                      ? ::openwow::render::TextureCacheBackendClass::kOpenGl
                      : ::openwow::render::TextureCacheBackendClass::kModern});
}

using ::openwow::render::TextureCacheSizeValidationResult;

inline TextureCacheSizeValidationResult ValidateTextureCacheSizeCallback(
    int requested_megabytes,
    std::uint64_t physical_memory_bytes,
    int gx_api_id) {
  return ::openwow::render::ValidateTextureCacheSizeChange(
      static_cast<std::uint32_t>(requested_megabytes),
      {.physical_memory_bytes = physical_memory_bytes,
       .backend = gx_api_id == 0
                      ? ::openwow::render::TextureCacheBackendClass::kOpenGl
                      : ::openwow::render::TextureCacheBackendClass::kModern});
}

using openwow::render::ResolveUiFasterCallback;
using openwow::render::UiFasterCallbackResult;
using openwow::render::UiFasterDevicePathState;

using TextureFilteringCaps = ::openwow::render::TextureFilterCaps;
using TextureFilteringStartupState =
    ::openwow::render::TextureFilteringStartupState;

inline TextureFilteringStartupState ResolveTextureFilteringStartupState(
    std::uint32_t requested_mode,
    const TextureFilteringCaps& caps) {
  return ::openwow::render::ResolveTextureFilteringStartupState(requested_mode,
                                                                caps);
}

struct PendingStartupStringState {
  std::optional<std::string> pending;
  std::vector<std::string> queue;
};

inline const std::optional<std::string>& GetPendingStartupString(
    const PendingStartupStringState& state) {
  return state.pending;
}

inline const std::optional<std::string>& SetPendingStartupString(
    PendingStartupStringState& state,
    std::optional<std::string> pending) {
  state.pending = std::move(pending);
  return state.pending;
}

inline bool EnqueuePendingStartupStringCopy(PendingStartupStringState& state) {
  if (!state.pending.has_value()) {
    return false;
  }
  state.queue.push_back(*state.pending);
  return true;
}

inline bool FlushPendingStartupStringToQueue(PendingStartupStringState& state) {
  if (!GetPendingStartupString(state).has_value()) {
    return false;
  }
  (void)EnqueuePendingStartupStringCopy(state);
  (void)SetPendingStartupString(state, std::nullopt);
  return true;
}

struct DataPreloadThreadGateState {
  bool preload_enabled{false};
  std::uintptr_t existing_thread_handle{0};
  bool existing_thread_handle_is_valid{false};
};

inline bool ShouldStartDataPreloadThread(
    const DataPreloadThreadGateState& state) {
  if (!state.preload_enabled) {
    return false;
  }
  return state.existing_thread_handle == 0u
         || !state.existing_thread_handle_is_valid;
}

}

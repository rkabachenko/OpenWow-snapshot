#pragma once

#include "openwow/screens/loading_screen_manager.h"
#include "openwow/vfs/sfile_core.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

namespace openwow::game {

struct LoadingScreenProgressPumpResult {
  bool accepted = false;
  bool should_present = false;
  bool should_send_keep_alive = false;
  float display_progress = 0.0f;
};

class LoadingScreenProgressPump {
 public:
  void SetPendingEventPump(std::function<void()> pump) {
    pending_event_pump_ = std::move(pump);
  }

  void Reset(bool split_secondary_progress, std::uint32_t now_ms = 0) {
    keep_alive_deadline_ms_ = now_ms + kKeepAliveIntervalMs;
    openwow::screens::LoadingScreenManager::Get().ResetCompositeProgress(
        split_secondary_progress);
    PumpPendingEvents();
  }

  [[nodiscard]] LoadingScreenProgressPumpResult Advance(
      openwow::screens::LoadingProgressInput input, float progress,
      std::uint32_t now_ms, bool online_mode = false,
      bool online_trial_gate_open = false) {
    auto& manager = openwow::screens::LoadingScreenManager::Get();
    const bool input_updated =
        manager.UpdateCompositeProgressInput(input, progress);
    const auto effective_state =
        ResolveEffectiveOnlineState(manager, online_mode, online_trial_gate_open);

    float trial_alpha = 0.0f;
    const bool trial_alpha_updated =
        effective_state.track_trial_alpha
        && UpdateTrialLoadingAlphaFromSelectedRace(manager, &trial_alpha);
    if (!input_updated && !trial_alpha_updated) {
      return {};
    }

    PumpPendingEvents();
    auto result = PumpSharedUpdate(
        manager, now_ms, effective_state.online_mode,
        effective_state.online_trial_gate_open,
        progress == 1.0f || (trial_alpha_updated && trial_alpha == 1.0f));
    result.accepted = true;
    return result;
  }

  [[nodiscard]] LoadingScreenProgressPumpResult AdvanceTrialLoadingAlpha(
      float alpha, std::uint32_t now_ms, bool online_mode = false,
      bool online_trial_gate_open = false) {
    auto& manager = openwow::screens::LoadingScreenManager::Get();
    const auto effective_state =
        ResolveEffectiveOnlineState(manager, online_mode, online_trial_gate_open);
    if (!effective_state.track_trial_alpha
        || !manager.UpdateTrialLoadingAlpha(alpha)) {
      return {};
    }

    PumpPendingEvents();
    auto result =
        PumpSharedUpdate(manager, now_ms, effective_state.online_mode,
                         effective_state.online_trial_gate_open,
                         alpha == 1.0f);
    result.accepted = true;
    return result;
  }

  template <typename Sink>
  [[nodiscard]] LoadingScreenProgressPumpResult Apply(
      openwow::screens::LoadingProgressInput input, float progress,
      std::uint32_t now_ms, Sink&& on_present,
      bool online_mode = false, bool online_trial_gate_open = false) {
    const auto result =
        Advance(input, progress, now_ms, online_mode, online_trial_gate_open);
    if (result.should_present) {
      on_present(result.display_progress);
    }
    return result;
  }

 private:
  void PumpPendingEvents() const {
    if (pending_event_pump_) {
      pending_event_pump_();
    }
  }

  struct EffectiveOnlineState {
    bool online_mode = false;
    bool online_trial_gate_open = false;
    bool track_trial_alpha = false;
  };

  static EffectiveOnlineState ResolveEffectiveOnlineState(
      openwow::screens::LoadingScreenManager& manager, bool online_mode,
      bool online_trial_gate_open) {
    EffectiveOnlineState state;
    state.online_mode = online_mode;
    state.online_trial_gate_open = online_trial_gate_open;

    if (!state.online_mode && !state.online_trial_gate_open
        && manager.IsTrialMode()) {
      state.online_mode = true;
      state.online_trial_gate_open =
          openwow::vfs::IsCurrentDataPreloadRaceReadyForLoading();
    }

    state.track_trial_alpha =
        state.online_mode && manager.IsTrialMode()
        && !state.online_trial_gate_open;
    return state;
  }

  static bool UpdateTrialLoadingAlphaFromSelectedRace(
      openwow::screens::LoadingScreenManager& manager, float* out_alpha) {
    const float trial_alpha = static_cast<float>(
        openwow::vfs::GetStartRaceDataPreloadProgress(
            openwow::vfs::GetDataPreloadSelectedRace()));
    if (out_alpha) {
      *out_alpha = trial_alpha;
    }
    return manager.UpdateTrialLoadingAlpha(trial_alpha);
  }

  [[nodiscard]] LoadingScreenProgressPumpResult PumpSharedUpdate(
      openwow::screens::LoadingScreenManager& manager, std::uint32_t now_ms,
      bool online_mode, bool online_trial_gate_open,
      bool force_present) {
    LoadingScreenProgressPumpResult result;
    if (!force_present && now_ms - last_present_tick_ms_ < 250u) {
      return result;
    }

    last_present_tick_ms_ = now_ms;
    manager.UpdateDisplayProgress(online_mode, online_trial_gate_open);
    result.should_present = true;
    if (static_cast<std::int32_t>(now_ms - keep_alive_deadline_ms_) >= 0) {
      result.should_send_keep_alive = true;
      keep_alive_deadline_ms_ = now_ms + kKeepAliveIntervalMs;
    }
    result.display_progress = manager.GetProgress();
    return result;
  }

 private:
  static constexpr std::uint32_t kKeepAliveIntervalMs = 30000;

  std::function<void()> pending_event_pump_;
  std::uint32_t last_present_tick_ms_ = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t keep_alive_deadline_ms_ = kKeepAliveIntervalMs;
};

}

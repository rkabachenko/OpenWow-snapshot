#include "openwow/audio/playback/sound_runtime_internal.h"

namespace openwow::audio {

int SoundRuntime::ChaosMode() {
  const std::uint32_t now = openwow::core::GameClock::GetTickCount32();
  double delta_seconds = 0.0;
  if (chaos_runtime_.tick_initialized) {
    delta_seconds = static_cast<double>(now - chaos_runtime_.last_tick_ms) / 1000.0;
  }
  chaos_runtime_.last_tick_ms = now;
  chaos_runtime_.tick_initialized = true;

  const int mode = openwow::ui::game::CVarSystem::Instance().GetCVarInt("Sound_ChaosMode");
  (void)AdvanceChaosModeFrame(mode, delta_seconds, true);
  return 1;
}

ChaosModeFrameStats
SoundRuntime::AdvanceChaosModeFrame(const int mode, const double delta_seconds,
                                      const bool allow_engine_restart) {
  ChaosModeFrameStats stats{};
  const double bounded_delta = std::isfinite(delta_seconds)
                                   ? std::max(0.0, delta_seconds)
                                   : 0.0;

  const std::uint64_t delta_ms =
      bounded_delta >= 0.1
          ? 100u
          : static_cast<std::uint64_t>(std::llround(bounded_delta * 1000.0));
  chaos_runtime_.interval_elapsed_ms = std::min<std::uint64_t>(
      100u, chaos_runtime_.interval_elapsed_ms + delta_ms);

  const auto random_kit = [this]() -> std::uint32_t {

    return ConsumePlaybackRandomBoundedValue(max_sound_kit_id_);
  };
  const std::uint32_t frame_kit = random_kit();

  const auto random_position = [this]() {
    std::array<float, 3> origin{};
    (void)GetActivePlayerPosition(origin.data());

    EnsurePlaybackRandomStateSeeded(random_seed_);
    const float signed_radius =
        retail_rng::AdlerSeedNextSignedUnitFloat(random_seed_) * 50.0f;
    const float z = retail_rng::AdlerSeedNextSignedUnitFloat(random_seed_);
    const float azimuth =
        retail_rng::AdlerSeedNextUnitFloat(random_seed_) *
        (2.0f * std::numbers::pi_v<float>);
    const float xy = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const std::array<float, 3> direction{
        xy * std::cos(azimuth), xy * std::sin(azimuth), z};
    for (std::size_t index = 0; index < origin.size(); ++index) {
      origin[index] += direction[index] * signed_radius;
    }
    return origin;
  };

  SoundKitPlaybackOptions stress_options{};
  stress_options.volume_scale = 0.3f;
  stress_options.exclusivity_mode = SoundKitExclusivityMode::kDisableExclusiveRepeat;

  const auto play_2d = [this, &stats](const std::uint32_t kit,
                                      const SoundKitPlaybackOptions &options,
                                      std::uint32_t *handle_out = nullptr) {
    ++stats.play_2d_attempts;
    return PlaySoundKit(kit, nullptr, handle_out, options);
  };
  const auto play_3d = [this, &stats, &random_position](
                           const std::uint32_t kit,
                           const SoundKitPlaybackOptions &options,
                           std::uint32_t *handle_out = nullptr) {
    const auto position = random_position();
    ++stats.play_3d_attempts;
    return PlaySoundKit(kit, position.data(), handle_out, options);
  };
  const auto play_3d_with_deferred_kit =
      [this, &stats, &random_position](const auto &select_kit,
                                       const SoundKitPlaybackOptions &options,
                                       std::uint32_t *handle_out = nullptr) {

        const auto position = random_position();
        const std::uint32_t kit = select_kit();
        ++stats.play_3d_attempts;
        return PlaySoundKit(kit, position.data(), handle_out, options);
      };
  const auto play_and_stop = [this, &stats](const auto &play) {
    std::uint32_t handle = 0;
    (void)play(&handle);
    if (handle != 0) {
      (void)StopActiveSoundHandle(handle, true, -1.0f, true);
    }
    ++stats.immediate_stops;
  };

  const auto restart_if_due = [this, mode, allow_engine_restart, &stats]() {
    if (chaos_runtime_.restart_elapsed_seconds <= 10.0) {
      return;
    }
    chaos_runtime_.restart_elapsed_seconds = 0.0;
    stats.restart_requested = true;
    if (!allow_engine_restart) {
      return;
    }

    openwow::core::ida::ConsoleLog("##RESTART SOUND ENGINE!!");
    Shutdown(true);
    const bool initialized = InitializeFull(true) == 0;
    RegisterEnterWorldAudioCallbacks();
    (void)openwow::ui::game::CVarSystem::Instance().SetCVar(
        "Sound_ChaosMode", std::to_string(mode), true);
    stats.restart_performed = initialized;
  };

  switch (mode) {
  case 0:
    break;
  case 1:
  case 2: {
    if (chaos_runtime_.interval_elapsed_ms < 100) {
      break;
    }
    const int count = mode == 2 ? 2 : 1;
    for (int index = 0; index < count; ++index) {
      (void)play_3d_with_deferred_kit(random_kit, stress_options);
    }
    chaos_runtime_.interval_elapsed_ms = 0;
    break;
  }
  case 3:
  case 4: {

    const int leading_2d = mode == 4 ? 2 : 0;
    for (int index = 0; index < leading_2d; ++index) {
      play_and_stop([&](std::uint32_t *handle) {
        return play_2d(frame_kit, stress_options, handle);
      });
    }
    play_and_stop([&](std::uint32_t *handle) {
      return play_2d(frame_kit, stress_options, handle);
    });
    play_and_stop([&](std::uint32_t *handle) {
      return play_3d(frame_kit, stress_options, handle);
    });
    break;
  }
  case 5:
  case 6:
  case 7: {
    chaos_runtime_.burst_elapsed_seconds += bounded_delta;
    if (chaos_runtime_.burst_elapsed_seconds <= 1.0) {
      break;
    }
    if (mode == 5) {
      for (int index = 0; index < 80; ++index) {
        (void)play_2d(frame_kit, stress_options);
      }
    } else if (mode == 6) {
      for (int index = 0; index < 80; ++index) {
        (void)play_3d(frame_kit, stress_options);
      }
    } else {
      SoundKitPlaybackOptions loop_options = stress_options;
      loop_options.loop_mode = SoundLoopMode::kForceLoop;
      for (std::uint32_t &handle : chaos_runtime_.persistent_loop_handles) {
        if (handle != 0 && IsSoundHandlePlaying(handle)) {
          continue;
        }
        handle = 0;
        ++stats.persistent_loop_starts;
        (void)play_3d(frame_kit, loop_options, &handle);
      }
    }
    chaos_runtime_.burst_elapsed_seconds = 0.0;
    break;
  }
  case 8:
    chaos_runtime_.burst_elapsed_seconds += bounded_delta;
    if (chaos_runtime_.burst_elapsed_seconds > 0.05) {
      std::puts("----------playing...");
      ++stats.script_file_attempts;
      (void)PlayScriptSound("Data\\Alert.mp3", 4);
      chaos_runtime_.burst_elapsed_seconds = 0.0;
    }
    break;
  case 9:
    chaos_runtime_.restart_elapsed_seconds += bounded_delta;
    restart_if_due();
    break;
  case 10:
    chaos_runtime_.restart_elapsed_seconds += bounded_delta;
    if (chaos_runtime_.interval_elapsed_ms >= 100) {
      (void)play_3d_with_deferred_kit(random_kit, stress_options);
      (void)play_3d_with_deferred_kit(random_kit, stress_options);
      chaos_runtime_.interval_elapsed_ms = 0;
      restart_if_due();
    }
    break;
  case 11:
  case 12:
  case 13: {
    if (chaos_runtime_.interval_elapsed_ms < 100) {
      break;
    }
    if (mode == 13) {
      (void)play_3d_with_deferred_kit(random_kit, stress_options);
      (void)play_3d_with_deferred_kit(random_kit, stress_options);
    }
    const int count_2d = mode == 11 ? 1 : 2;
    for (int index = 0; index < count_2d; ++index) {
      (void)play_2d(random_kit(), stress_options);
    }
    chaos_runtime_.interval_elapsed_ms = 0;
    break;
  }
  case 14:
    chaos_runtime_.restart_elapsed_seconds += bounded_delta;
    if (chaos_runtime_.interval_elapsed_ms >= 100) {
      chaos_runtime_.interval_elapsed_ms = 0;
      const std::uint32_t repeated_kit = random_kit();
      for (int index = 0; index < 20; ++index) {
        (void)play_3d(repeated_kit, stress_options);
      }
      (void)play_3d_with_deferred_kit(random_kit, stress_options);
      restart_if_due();
    }
    break;
  case 15: {
    static constexpr std::array<std::uint32_t, 3> kMode15Kits{
        0x3305u, 0x3e61u, 0x2f8du};
    const auto mode_15_kit = [this]() {
      return kMode15Kits[ConsumePlaybackRandomBoundedValue(
          static_cast<std::uint32_t>(kMode15Kits.size()))];
    };
    for (int index = 0; index < 8; ++index) {
      std::uint32_t ignored_handle = 0;
      (void)play_3d_with_deferred_kit(mode_15_kit, stress_options,
                                      &ignored_handle);
    }
    break;
  }
  default:
    break;
  }

  last_chaos_mode_frame_stats_ = stats;
  return stats;
}

void SoundRuntime::RegisterChaosModeIfEnabled() {
  if (!openwow::core::StormCmd::Instance().IsCommandEnabled(
          openwow::core::StartupCommandId::kSoundChaos)) {
    return;
  }

  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  const bool already_registered = cvars.Exists("Sound_ChaosMode");

  cvars.RegisterCVar("Sound_ChaosMode", "0", openwow::ui::game::CVarFlags::None,
                      "Testing to break sound engine");

  if (chaos_mode_cvar_callback_handle_ != 0) {
    cvars.RemoveCallback("Sound_ChaosMode", chaos_mode_cvar_callback_handle_);
  }

  chaos_mode_cvar_callback_handle_ = cvars.AddCallback(
      "Sound_ChaosMode", [](const std::string &, const std::string &new_value) {
        openwow::core::ida::ConsoleLog("CHAOS!!");
        openwow::core::ida::ConsoleLog("Modes:");
        openwow::core::ida::ConsoleLog("0: Off");
        openwow::core::ida::ConsoleLog("1: Generate 1 random 3d sound per frame");
        openwow::core::ida::ConsoleLog("2: Generate 2 random 3d sounds per frame");
        openwow::core::ida::ConsoleLog("3: Start and stop 1 2d and 1 3d sound every frame");
        openwow::core::ida::ConsoleLog("4: Start and stop 2 2d and 2 3d sounds every frame");
        openwow::core::ida::ConsoleLog("5: Play the same random sound x20 (in 2d) every second");
        openwow::core::ida::ConsoleLog("6: Play the same random sound x20 (in 3d) every second");
        openwow::core::ida::ConsoleLog(
            "7: Play the same random looping sound x20 (in 3d) every second");
        openwow::core::ida::ConsoleLog("8: Play Alert.mp3 from Data folder a lot");
        openwow::core::ida::ConsoleLog(
            "9: Restart game sound system (NOT INCLUDING VOICE CHAT SYSTEM) every 10 seconds");
        openwow::core::ida::ConsoleLog(
            "   ALERT! This mode breaks voice chat, and will cause a crash on game exit.");
        openwow::core::ida::ConsoleLog("10: Mode 2 + Mode 9");
        openwow::core::ida::ConsoleLog("11: Generate 1 random 2d sound per frame");
        openwow::core::ida::ConsoleLog("12: Generate 2 random 2d sounds per frame");
        openwow::core::ida::ConsoleLog(
            "13: Generate 2 random 2d sounds AND 2 random 3d sounds per frame");
        openwow::core::ida::ConsoleLog("14: Mode 10 + Mode 6");

        const long mode = std::atol(new_value.c_str());

        auto &cvar_sys = openwow::ui::game::CVarSystem::Instance();
        if (!cvar_sys.Exists("Sound_DisableStreams")) {
          return;
        }

        if (mode == 0) {
          cvar_sys.SetCVar("Sound_DisableStreams", "0", true);
        } else {
          cvar_sys.SetCVar("Sound_DisableStreams", "1", true);
        }
      });

  if (!already_registered) {
    (void)cvars.SetCVar("Sound_ChaosMode", "0", true);
  }

  auto &registration = *world_audio_callbacks_;
  std::lock_guard lock(registration.mutex);
  if (registration.chaos_mode_handle == openwow::core::CallbackHandle::Invalid) {
    const std::weak_ptr<void> lifetime = callback_lifetime_;
    registration.chaos_mode_handle = openwow::core::FrameScheduler::Instance().Register(
        openwow::core::Phase::Update, 1000,
        [this, lifetime](const double delta_seconds) {
          if (lifetime.expired()) return;
          const int selected_mode =
              openwow::ui::game::CVarSystem::Instance().GetCVarInt("Sound_ChaosMode");
          (void)AdvanceChaosModeFrame(selected_mode, delta_seconds, true);
        },
        "SoundInterface_ChaosMode");
  }
}

}

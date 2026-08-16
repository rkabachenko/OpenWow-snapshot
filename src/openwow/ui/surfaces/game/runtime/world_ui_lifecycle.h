#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/ui/surfaces/game/runtime/world_ui_lifecycle_command.h"

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>

namespace openwow::ui::game {

class WorldUiGeneration {
 public:
  [[nodiscard]] constexpr std::uint64_t value() const noexcept {
    return value_;
  }

  constexpr auto operator<=>(const WorldUiGeneration&) const = default;

 private:
  friend class WorldUiLifecycle;

  constexpr explicit WorldUiGeneration(std::uint64_t value) : value_(value) {}
  std::uint64_t value_;
};

enum class WorldUiAccountDataSlot : std::uint8_t {
  Account = 0x10,
  Character = 0x20,
};

enum class WorldUiLifecycleEvent : std::uint8_t {
  VariablesLoaded,
  PlayerLogin,
  PlayerEnteringWorld,
  PlayerLeavingWorld,
  PlayerLogout,
};

enum class WorldUiLifecycleState : std::uint8_t {
  Stopped,
  Starting,
  RestoringAccountData,
  Ready,
  Active,
  ReloadPending,
  Stopping,
  Failed,
};

enum class WorldUiStopReason : std::uint8_t {
  WorldLeave,
  Reload,
  StartupFailure,
};

enum class WorldUiStartResult : std::uint8_t {
  Started,
  AlreadyRunning,
  RuntimeStartFailed,
};

enum class WorldUiReloadPumpResult : std::uint8_t {
  Idle,
  DeferredByCinematic,
  Reloaded,
  RuntimeStartFailed,
};

struct WorldUiVoiceSettings {
  bool voice_enabled{false};
  bool microphone_enabled{false};
};

using WorldUiAccountDataCompletion =
    std::function<void(WorldUiGeneration, WorldUiAccountDataSlot)>;

struct WorldUiLifecycleOperations {
  std::function<bool(WorldUiGeneration, std::function<void(float)>)>
      start_runtime;
  std::function<void(WorldUiGeneration)> detach_runtime_callbacks;
  std::function<void(WorldUiGeneration, WorldUiStopReason)> destroy_runtime;
  std::function<void(WorldUiLifecycleEvent)> fire_event;
  std::function<bool()> has_local_player;
  std::function<bool()> prepare_local_player;
  std::function<void()> run_pre_enter_player_setup;
  std::function<void()> run_post_enter_player_fanout;
  std::function<void(WorldUiStopReason)> prepare_player_leave;
  std::function<void(WorldUiStopReason)> prepare_player_logout;
  std::function<void(WorldUiStopReason)> persist_state;
  std::function<void(WorldUiGeneration, WorldUiAccountDataSlot,
                     WorldUiAccountDataCompletion)> restore_account_data;
  std::function<void(WorldUiGeneration)> cancel_account_data;
  std::function<bool()> reload_blocked;
  std::function<void()> prepare_reload;
};

class WorldUiSessionCommandAdapter;
class WorldUiVoiceSettingsAdapter;

class WorldUiLifecycle final : public WorldUiLifecycleCommandPort {
 public:
  WorldUiLifecycle(WorldUiLifecycleOperations operations,
                   WorldUiSessionCommandAdapter& session_commands,
                   WorldUiVoiceSettingsAdapter& voice_settings);

  [[nodiscard]] WorldUiStartResult Start(
      std::function<void(float)> progress_callback = {});
  void BeginPlayerWorldTransition();
  [[nodiscard]] bool TryActivateLocalPlayer();
  void Stop(WorldUiStopReason reason = WorldUiStopReason::WorldLeave);

  void RequestWorldUiReload() override;
  [[nodiscard]] bool IsPlayerLoginFired() const noexcept override {
    return player_login_fired_;
  }
  [[nodiscard]] WorldUiReloadPumpResult PumpReload();

  [[nodiscard]] WorldUiLifecycleState state() const noexcept { return state_; }
  [[nodiscard]] std::optional<WorldUiGeneration> generation() const noexcept {
    return generation_;
  }

 private:
  [[nodiscard]] WorldUiGeneration AllocateGeneration();
  void OnAccountDataRestored(WorldUiGeneration generation,
                             WorldUiAccountDataSlot slot);
  void CompleteStartupIfReady();
  [[nodiscard]] WorldUiStartResult StartGeneration(
      std::function<void(float)> progress_callback);
  [[nodiscard]] bool IsCurrentGeneration(
      WorldUiGeneration generation) const noexcept;
  void StopGeneration(WorldUiStopReason reason);

  WorldUiLifecycleOperations operations_;
  WorldUiSessionCommandAdapter& session_commands_;
  WorldUiVoiceSettingsAdapter& voice_settings_;
  WorldUiLifecycleState state_{WorldUiLifecycleState::Stopped};
  std::optional<WorldUiGeneration> generation_;
  std::uint64_t next_generation_{1};
  bool account_slot_restored_{false};
  bool character_slot_restored_{false};
  bool player_login_fired_{false};
  bool player_activated_{false};
  bool reload_requested_{false};
  bool stop_completed_{false};
};

}

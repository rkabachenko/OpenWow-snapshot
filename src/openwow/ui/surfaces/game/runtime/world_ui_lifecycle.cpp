#include "openwow/ui/surfaces/game/runtime/world_ui_lifecycle.h"
#include "openwow/ui/surfaces/game/adapters/protocol/world_ui_session_command_adapter.h"
#include "openwow/ui/surfaces/game/adapters/settings/world_ui_voice_settings_adapter.h"

#include <utility>

namespace openwow::ui::game {

WorldUiLifecycle::WorldUiLifecycle(
    WorldUiLifecycleOperations operations,
    WorldUiSessionCommandAdapter& session_commands,
    WorldUiVoiceSettingsAdapter& voice_settings)
    : operations_(std::move(operations)),
      session_commands_(session_commands),
      voice_settings_(voice_settings) {}

WorldUiStartResult WorldUiLifecycle::Start(
    std::function<void(float)> progress_callback) {
  if (state_ != WorldUiLifecycleState::Stopped &&
      state_ != WorldUiLifecycleState::Failed) {
    return WorldUiStartResult::AlreadyRunning;
  }
  return StartGeneration(std::move(progress_callback));
}

WorldUiStartResult WorldUiLifecycle::StartGeneration(
    std::function<void(float)> progress_callback) {
  const auto starting_generation = AllocateGeneration();
  generation_ = starting_generation;
  account_slot_restored_ = false;
  character_slot_restored_ = false;
  player_login_fired_ = false;
  player_activated_ = false;
  reload_requested_ = false;
  stop_completed_ = false;
  state_ = WorldUiLifecycleState::Starting;

  const bool runtime_started = operations_.start_runtime(
      starting_generation, std::move(progress_callback));
  if (!IsCurrentGeneration(starting_generation)) {
    return WorldUiStartResult::RuntimeStartFailed;
  }
  if (!runtime_started) {
    state_ = WorldUiLifecycleState::Stopping;
    operations_.detach_runtime_callbacks(starting_generation);
    operations_.destroy_runtime(starting_generation,
                                WorldUiStopReason::StartupFailure);
    generation_.reset();
    account_slot_restored_ = false;
    character_slot_restored_ = false;
    player_login_fired_ = false;
    player_activated_ = false;
    reload_requested_ = false;
    state_ = WorldUiLifecycleState::Failed;
    return WorldUiStartResult::RuntimeStartFailed;
  }

  state_ = WorldUiLifecycleState::RestoringAccountData;
  const auto completion =
      [this](const WorldUiGeneration completed_generation,
             const WorldUiAccountDataSlot slot) {
        OnAccountDataRestored(completed_generation, slot);
      };
  operations_.restore_account_data(
      starting_generation, WorldUiAccountDataSlot::Account, completion);
  if (!IsCurrentGeneration(starting_generation)) {
    return WorldUiStartResult::RuntimeStartFailed;
  }
  operations_.restore_account_data(
      starting_generation, WorldUiAccountDataSlot::Character, completion);
  if (!IsCurrentGeneration(starting_generation)) {
    return WorldUiStartResult::RuntimeStartFailed;
  }
  return WorldUiStartResult::Started;
}

void WorldUiLifecycle::OnAccountDataRestored(
    const WorldUiGeneration generation, const WorldUiAccountDataSlot slot) {
  if (!generation_.has_value() || generation != *generation_ ||
      state_ != WorldUiLifecycleState::RestoringAccountData) {
    return;
  }

  if (slot == WorldUiAccountDataSlot::Account) {
    account_slot_restored_ = true;
  } else {
    character_slot_restored_ = true;
  }
  CompleteStartupIfReady();
}

void WorldUiLifecycle::CompleteStartupIfReady() {
  if (!account_slot_restored_ || !character_slot_restored_) {
    return;
  }

  operations_.fire_event(WorldUiLifecycleEvent::VariablesLoaded);
  if (!generation_.has_value() ||
      state_ != WorldUiLifecycleState::RestoringAccountData) {
    return;
  }
  if (reload_requested_) {
    state_ = WorldUiLifecycleState::ReloadPending;
    return;
  }
  state_ = WorldUiLifecycleState::Ready;
  (void)TryActivateLocalPlayer();
}

bool WorldUiLifecycle::TryActivateLocalPlayer() {
  if (state_ == WorldUiLifecycleState::Active) {
    return true;
  }
  if (state_ != WorldUiLifecycleState::Ready ||
      !operations_.has_local_player()) {
    return false;
  }
  const auto activating_generation = *generation_;
  if (!operations_.prepare_local_player() ||
      !IsCurrentGeneration(activating_generation)) {
    return false;
  }

  if (!player_login_fired_) {

    player_login_fired_ = true;
    operations_.fire_event(WorldUiLifecycleEvent::PlayerLogin);
    if (!IsCurrentGeneration(activating_generation)) {
      return false;
    }
  }
  operations_.run_pre_enter_player_setup();
  if (!IsCurrentGeneration(activating_generation)) {
    return false;
  }
  operations_.fire_event(WorldUiLifecycleEvent::PlayerEnteringWorld);
  if (!IsCurrentGeneration(activating_generation)) {
    return false;
  }
  operations_.run_post_enter_player_fanout();
  if (!IsCurrentGeneration(activating_generation)) {
    return false;
  }

  if (const auto selection = session_commands_.CurrentWorldUiSelection();
      selection.has_value() && !selection->IsEmpty()) {
    session_commands_.SetWorldUiSelection(*selection);
    if (!IsCurrentGeneration(activating_generation)) {
      return false;
    }
  }
  session_commands_.EnableWorldUiVoice(
      voice_settings_.CurrentWorldUiVoiceSettings());
  if (!IsCurrentGeneration(activating_generation)) {
    return false;
  }
  player_activated_ = true;
  state_ = reload_requested_ ? WorldUiLifecycleState::ReloadPending
                             : WorldUiLifecycleState::Active;
  return true;
}

void WorldUiLifecycle::BeginPlayerWorldTransition() {
  stop_completed_ = false;
  if (state_ == WorldUiLifecycleState::Active) {
    state_ = WorldUiLifecycleState::Ready;
  }
}

void WorldUiLifecycle::Stop(const WorldUiStopReason reason) {
  if (state_ == WorldUiLifecycleState::Stopping || stop_completed_) {
    return;
  }

  state_ = WorldUiLifecycleState::Stopping;
  operations_.prepare_player_leave(reason);
  if (generation_.has_value() && player_activated_) {
    operations_.fire_event(WorldUiLifecycleEvent::PlayerLeavingWorld);
  }
  operations_.prepare_player_logout(reason);
  if (generation_.has_value() && player_activated_) {
    operations_.fire_event(WorldUiLifecycleEvent::PlayerLogout);
  }
  operations_.persist_state(reason);
  if (generation_.has_value()) {
    StopGeneration(reason);
  }
  state_ = WorldUiLifecycleState::Stopped;
  reload_requested_ = false;
  stop_completed_ = true;
}

void WorldUiLifecycle::StopGeneration(const WorldUiStopReason reason) {
  if (!generation_.has_value()) {
    return;
  }

  operations_.detach_runtime_callbacks(*generation_);
  operations_.cancel_account_data(*generation_);
  operations_.destroy_runtime(*generation_, reason);
  generation_.reset();
  account_slot_restored_ = false;
  character_slot_restored_ = false;
  player_login_fired_ = false;
  player_activated_ = false;
}

void WorldUiLifecycle::RequestWorldUiReload() {
  if (!generation_.has_value() ||
      (state_ != WorldUiLifecycleState::Starting &&
       state_ != WorldUiLifecycleState::RestoringAccountData &&
       state_ != WorldUiLifecycleState::Ready &&
       state_ != WorldUiLifecycleState::Active &&
       state_ != WorldUiLifecycleState::ReloadPending)) {
    return;
  }
  reload_requested_ = true;
  if (state_ == WorldUiLifecycleState::Starting ||
      state_ == WorldUiLifecycleState::RestoringAccountData) {
    return;
  }
  state_ = WorldUiLifecycleState::ReloadPending;
}

WorldUiReloadPumpResult WorldUiLifecycle::PumpReload() {
  if (!reload_requested_ ||
      state_ != WorldUiLifecycleState::ReloadPending) {
    return WorldUiReloadPumpResult::Idle;
  }
  if (operations_.reload_blocked()) {
    return WorldUiReloadPumpResult::DeferredByCinematic;
  }

  reload_requested_ = false;
  operations_.prepare_reload();
  Stop(WorldUiStopReason::Reload);
  return StartGeneration({}) == WorldUiStartResult::Started
             ? WorldUiReloadPumpResult::Reloaded
             : WorldUiReloadPumpResult::RuntimeStartFailed;
}

WorldUiGeneration WorldUiLifecycle::AllocateGeneration() {
  return WorldUiGeneration(next_generation_++);
}

bool WorldUiLifecycle::IsCurrentGeneration(
    const WorldUiGeneration generation) const noexcept {
  return generation_.has_value() && *generation_ == generation;
}

}

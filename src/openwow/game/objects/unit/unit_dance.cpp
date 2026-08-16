#include "openwow/game/objects/unit/unit_dance.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/client_misc.h"
#include "openwow/game/activities/dance/application/unit_dance_state.h"
#include "openwow/game/event_scheduler.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/runtime/time/game_clock.h"

#include <chrono>
#include <functional>
#include <utility>

namespace openwow::game {

namespace {

constexpr float kMillisecondsToSeconds = 0.001f;

DanceStateCallbacks MakeDanceCallbacks(CGUnit_C &unit, const WorldSession &session) {
  DanceStateCallbacks callbacks;
  auto *const objects = unit.object_manager();
  callbacks.play_emote_animation = [&unit](const DanceEmoteAnimationId animation) {
    unit.Animation().PlayEmoteAnimation(
        static_cast<std::int32_t>(animation.value), 0u);
  };
  callbacks.play_sound = [&unit](const DanceSoundKitId sound_kit_id) {
    const auto position = unit.GetPosition();
    const float sound_position[3] = {position.x, position.y, position.z};
    (void)unit.sound_runtime().PlaySoundKit(
        sound_kit_id.value, sound_position);
  };
  callbacks.schedule_advance =
      [objects, guid = unit.GetGuid(), session = &session](
          const std::chrono::milliseconds delay,
          std::function<void()> ) {
    EventScheduler::Get().ScheduleOnce(
        static_cast<float>(delay.count()) * kMillisecondsToSeconds,
        [objects, guid, session]() {
          auto *scheduled_unit = objects != nullptr ? objects->GetMutableUnit(guid) : nullptr;
          if (scheduled_unit != nullptr) {
            scheduled_unit->Animation().Dance().Get(*scheduled_unit, *session)
                .AdvanceDanceStep();
          }
        },
        "CGUnit_C::DanceTimerAdvance");
  };
  callbacks.request_sync = []() {
    (void)openwow::net::ClientServices__SendPacket(
        openwow::net::wotlk::WorldPacket(
            openwow::net::wotlk::Opcode::CMSG_SYNC_DANCE));
  };
  callbacks.request_stop = []() {
    (void)openwow::net::ClientServices__SendPacket(
        openwow::net::wotlk::WorldPacket(
            openwow::net::wotlk::Opcode::CMSG_STOP_DANCE, {0}));
  };
  callbacks.refresh_stand_animation =
      [objects, guid = unit.GetGuid(), session = &session]() {
    auto *dancer = objects != nullptr ? objects->GetMutableUnit(guid) : nullptr;
    if (dancer != nullptr) {
      dancer->Animation().RefreshSelectedStandAnimation(*session, 0u, ~0u);
    }
  };
  return callbacks;
}

}

UnitDanceState &UnitDanceComponent::Get(CGUnit_C &owner,
                                        const WorldSession &session) {
  if (!state_) {
    state_ = std::make_unique<UnitDanceState>();
    state_->SetCallbacks(MakeDanceCallbacks(owner, session));
  }
  return *state_;
}

const UnitDanceState &UnitDanceComponent::Get() const noexcept {
  static const UnitDanceState inactive_state;
  if (!state_) {
    return inactive_state;
  }
  return *state_;
}

void UnitDanceComponent::Cancel() noexcept {
  if (state_) {
    state_->CancelDance();
  }
}

void UnitDanceComponent::SetData(CGUnit_C &owner, const WorldSession &session,
                                 DanceSequence sequence,
                                 const DanceMoveCatalog &catalog) {
  auto &state = Get(owner, session);
  state.SetSynchronizationRole(
      owner.IsActivePlayer() ? DanceSynchronizationRole::kActivePlayer
                              : DanceSynchronizationRole::kRemoteUnit);
  state.SetDanceData(std::move(sequence), catalog);
}

}

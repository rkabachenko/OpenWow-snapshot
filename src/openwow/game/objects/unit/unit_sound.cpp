#include "openwow/game/objects/unit/unit_sound.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/client_misc.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/creature_sound.h"
#include "openwow/game/char_create_helpers.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <cstdint>
#include <string>

namespace openwow::game {

UnitSoundComponent &CGUnit_C::Sound() noexcept { return sound_; }

const UnitSoundComponent &CGUnit_C::Sound() const noexcept { return sound_; }

namespace {

std::uint32_t g_last_creature_stand_sound_tick_ms = 0u;

const char *NpcVoicePlaybackResultName(const int result) {
  switch (result) {
  case 5: return "sound-kit-missing";
  case 6: return "sound-kit-empty";
  case 8: return "invalid-sound-type";
  case 9: return "sound-effects-disabled";
  case 14: return "sound-instance-limit";
  case 15: return "sound-already-playing";
  case 16: return "sound-file-playback-failed";
  case 17: return "audio-unavailable-or-disabled";
  default: return "playback-rejected";
  }
}

}

const data::dbc::CreatureSoundDataEntry *
UnitSoundComponent::ResolveActive(const CGUnit_C &unit) const {
  const auto *const dbc = unit.dbc_loader();
  if (dbc == nullptr) {
    return nullptr;
  }

  if (active_sound_data_id_ != 0u) {
    return dbc->creature_sound_data().LookupEntry(active_sound_data_id_);
  }

  const auto *const display =
      dbc->creature_display_info().LookupEntry(unit.Presentation().CurrentDisplayId());
  if (display == nullptr) {
    return nullptr;
  }

  if (display->sound_id != 0u) {
    if (const auto *const sound =
            dbc->creature_sound_data().LookupEntry(display->sound_id);
        sound != nullptr) {
      return sound;
    }
  }

  const auto *const model =
      dbc->creature_model_data().LookupEntry(display->model_id);
  return model != nullptr && model->sound_id != 0u
             ? dbc->creature_sound_data().LookupEntry(model->sound_id)
             : nullptr;
}

const data::dbc::NPCSoundsEntry *
UnitSoundComponent::ResolveNpcSounds(const CGUnit_C &unit, const char *source) const {
  const auto display_id = unit.Presentation().CurrentDisplayId();
  const auto *const dbc = unit.dbc_loader();
  const auto *const display =
      dbc != nullptr ? dbc->creature_display_info().LookupEntry(display_id) : nullptr;
  const auto npc_sound_id = display != nullptr ? display->npc_sound_id : 0u;
  if (display != nullptr && npc_sound_id == 0u) {
    // An authored zero means this appearance has no NPC voice set.
    return nullptr;
  }
  const auto *const sounds =
      display != nullptr ? dbc->npc_sounds().LookupEntry(npc_sound_id) : nullptr;
  if (sounds == nullptr) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "NPC sound stage=resolve source=" + std::string(source) +
            " guid=" + unit.GetGuid().ToString() +
            " entry=" + std::to_string(unit.GetEntry()) +
            " display=" + std::to_string(display_id) +
            " npc_sounds=" + std::to_string(npc_sound_id) +
            " reason=" + (dbc == nullptr ? "dbc-unavailable"
                           : display == nullptr ? "CreatureDisplayInfo-missing"
                                                : "NPCSounds-missing"));
  }
  return sounds;
}

bool UnitSoundComponent::IsNpcVoicePlaying(const CGUnit_C &unit) const {
  return npc_voice_handle_ != 0u &&
         unit.sound_runtime().IsSoundHandlePlaying(npc_voice_handle_);
}

int UnitSoundComponent::PlayNpcVoice(const CGUnit_C &unit,
                                    const data::dbc::NPCSoundsEntry &sounds,
                                    const std::uint32_t sound_kit_id,
                                    const char *source,
                                    const std::int32_t variation_index) const {
  if (IsNpcVoicePlaying(unit)) {
    return 15;
  }
  const auto position = unit.GetPosition();
  const float sound_position[3] = {position.x, position.y, position.z};
  audio::SoundKitPlaybackOptions options{};
  if (variation_index >= 0) {
    options.forced_file_index = variation_index;
  }
  npc_voice_handle_ = 0u;
  const auto result = unit.sound_runtime().PlaySoundKit(
      sound_kit_id, sound_position, &npc_voice_handle_, options);
  if (result != 0) {
    const auto level = result == 9 || result == 14 || result == 15 || result == 17
                           ? openwow::diagnostics::LogLevel::kDebug
                           : openwow::diagnostics::LogLevel::kWarn;
    openwow::diagnostics::Log(
        level, "NPC sound stage=play source=" + std::string(source) +
                   " guid=" + unit.GetGuid().ToString() +
                   " display=" + std::to_string(unit.Presentation().CurrentDisplayId()) +
                   " npc_sounds=" + std::to_string(sounds.id) +
                   " kit=" + std::to_string(sound_kit_id) +
                   " reason=" + NpcVoicePlaybackResultName(result) +
                   " result=" + std::to_string(result));
  }
  return result;
}

const data::dbc::CreatureSoundDataEntry *
UnitSoundComponent::ResolveMount(const CGUnit_C &unit) const {
  const std::uint32_t mount_display_id = unit.Mount().CachedDisplayForSpell();
  if (mount_display_id == 0) {
    return nullptr;
  }

  const auto *dbc = unit.dbc_loader();
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto *display_info =
      dbc->creature_display_info().LookupEntry(mount_display_id);
  if (display_info == nullptr) {
    return nullptr;
  }

  if (display_info->sound_id != 0) {
    const auto *sound_data =
        dbc->creature_sound_data().LookupEntry(display_info->sound_id);
    if (sound_data != nullptr) {
      return sound_data;
    }
  }

  const auto *model_data =
      dbc->creature_model_data().LookupEntry(display_info->model_id);
  if (model_data == nullptr) {
    return nullptr;
  }

  if (model_data->sound_id == 0) {
    return nullptr;
  }

  return dbc->creature_sound_data().LookupEntry(model_data->sound_id);
}

void UnitSoundComponent::PlayCreatureSound(CGUnit_C &unit,
                                          const std::uint32_t creature_sound_type,
                                          const bool force_play) {
  CreatureSoundUnitState state;
  state.guid = unit.GetGuid().GetRawValue();

  state.sound_data = ResolveActive(unit);

  const auto pos = unit.GetPosition();
  state.position_x = pos.x;
  state.position_y = pos.y;
  state.position_z = pos.z;
  state.is_active_player = unit.IsActivePlayer();
  state.is_in_combat = unit.State().IsInCombat();
  state.unit_class = unit.State().GetClass();
  state.race = unit.State().GetRace();
  state.gender = unit.State().GetGender();

  const bool listener_at_char =
      state.is_active_player &&
      unit.sound_runtime().GetCVarBool(
          "Sound_ListenerAtCharacter");

  const std::uint32_t current_tick = core::GameClock::GetTickCount32();

  openwow::game::PlayCreatureSound(unit.sound_runtime(), creature_sound_type, force_play, state,
                                   current_tick,
                                   g_last_creature_stand_sound_tick_ms,
                                   listener_at_char);
}

int UnitSoundComponent::PlayPrioritySound(
    const CGUnit_C &unit, const std::uint32_t priority_state) const {
  if (priority_state > 4u) {
    return 0;
  }

  const auto *const sound_data = ResolveActive(unit);
  if (sound_data == nullptr) {
    return 0;
  }

  auto &sound = unit.sound_runtime();
  if (priority_sound_handle_ != 0u &&
      sound.IsSoundHandlePlaying(priority_sound_handle_) &&
      priority_state <= priority_sound_state_) {
    return 0;
  }
  priority_sound_state_ = priority_state;

  if (priority_state == 3u) {
    return 0;
  }

  if (priority_state < 3u &&
      !openwow::ui::game::CVarSystem::Instance().GetCVarBool(
          "Sound_EnablePetSounds") &&
      unit.Casts().GetCreatedBySpell(unit) != 0u) {
    return 0;
  }

  std::uint32_t sound_kit_id = 0u;
  switch (priority_state) {
    case 0u:
      sound_kit_id = sound_data->sound_aggro_id;
      break;
    case 1u:
      sound_kit_id = sound_data->sound_pet_order_id;
      break;
    case 2u:
      sound_kit_id = sound_data->sound_pet_attack_id;
      break;
    case 4u:
      sound_kit_id = sound_data->sound_death_id;
      break;
    default:
      break;
  }
  if (sound_kit_id == 0u) {
    return 0;
  }

  const Position position = unit.GetPosition();
  const float sound_position[3] = {
      position.x, position.y, position.z + 2.0f};
  const auto *const objects = unit.object_manager();
  const bool is_active_mover =
      objects != nullptr &&
      unit.GetGuid() == objects->player_control().ActiveMoverGuid();

  openwow::audio::SoundKitPlaybackOptions options{};
  if (is_active_mover) {
    options.playback_priority = 110u;
    if (unit.State().GetClass() == 6u) {
      options.sound_model_override = CharCreate_GetDeathKnightModelName(
          unit.State().GetRace(), unit.State().GetGender());
    }
  }

  const bool listener_at_character =
      is_active_mover && sound.IsListenerAtCharacter();
  if (listener_at_character) {
    options.volume_scale = 0.65f;
  }

  return sound.PlaySoundKit(
      sound_kit_id,
      listener_at_character ? nullptr : sound_position,
      &priority_sound_handle_, options);
}

void UnitSoundComponent::SetCreatureStandSoundThrottle(
    const std::uint32_t last_tick_ms) {
  g_last_creature_stand_sound_tick_ms = last_tick_ms;
}

int UnitSoundComponent::PlayServerObjectSound(
    const CGUnit_C &unit, const std::uint32_t sound_kit_id) const {
  auto &sound = unit.sound_runtime();
  if (server_object_sound_handle_ != 0u) {
    (void)sound.StopActiveSoundHandle(server_object_sound_handle_, true,
                                      -1.0f, true);
    server_object_sound_handle_ = 0u;
  }

  const auto *const objects = unit.object_manager();
  const bool is_active_mover =
      objects != nullptr &&
      unit.GetGuid() == objects->player_control().ActiveMoverGuid();
  const bool listener_at_character =
      is_active_mover && sound.IsListenerAtCharacter();

  openwow::audio::SoundKitPlaybackOptions options{};
  if (listener_at_character) {
    options.playback_priority =
        openwow::audio::kSelfUnitSoundPlaybackPriority;
    options.volume_scale = 0.65f;
  }

  const Position position = unit.GetPosition();
  const float emitter[3] = {position.x, position.y, position.z};
  const int result = sound.PlaySoundKit(
      sound_kit_id, listener_at_character ? nullptr : emitter,
      &server_object_sound_handle_, options);
  if (result == 0 && !listener_at_character &&
      server_object_sound_handle_ != 0u) {
    (void)sound.BindSoundHandleToObjectGuid(server_object_sound_handle_,
                                            unit.GetGuid().GetRawValue());
  }
  return result;
}

void UnitSoundComponent::RefreshAmbientLoopSound(CGUnit_C &unit) const {
  const auto *const sound_data = unit.Mount().CachedDisplayForSpell() != 0u
                                     ? ResolveMount(unit)
                                     : ResolveActive(unit);
  const std::uint32_t next_kit =
      sound_data != nullptr ? sound_data->loop_sound_id : 0u;
  auto &sound = unit.sound_runtime();

  if (ambient_loop_sound_handle_ != 0u &&
      (next_kit != ambient_loop_sound_kit_id_ ||
       !sound.IsSoundHandlePlaying(ambient_loop_sound_handle_))) {
    (void)sound.StopActiveSoundHandle(ambient_loop_sound_handle_, false, 0.5f,
                                      true);
    ambient_loop_sound_handle_ = 0u;
    ambient_loop_sound_kit_id_ = 0u;
  }
  if (next_kit == 0u || ambient_loop_sound_handle_ != 0u) {
    return;
  }

  const Position position = unit.GetPosition();
  const float sound_position[3] = {position.x, position.y, position.z};
  openwow::audio::SoundKitPlaybackOptions options{};
  options.loop_mode = openwow::audio::SoundLoopMode::kForceLoop;
  if (sound.PlaySoundKit(next_kit, sound_position,
                         &ambient_loop_sound_handle_, options) == 0) {
    ambient_loop_sound_kit_id_ = next_kit;
    (void)sound.BindSoundHandleToObjectGuid(
        ambient_loop_sound_handle_, unit.GetGuid().GetRawValue());
  } else {
    ambient_loop_sound_handle_ = 0u;
  }
}

}

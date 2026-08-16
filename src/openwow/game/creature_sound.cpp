
#include "openwow/game/creature_sound.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/game/char_create_helpers.h"

#include <cstdint>

namespace openwow::game {

bool CreatureSound_ThrottleCheck(std::uint32_t type,
                                  std::uint32_t current_tick_ms,
                                  std::uint32_t& last_stand_tick_ms) {

  if (type != static_cast<std::uint32_t>(CreatureSoundType::Stand)) {
    return true;
  }

  constexpr std::uint32_t kStandThrottleMs = 10'000;
  if (static_cast<std::int32_t>(current_tick_ms - last_stand_tick_ms -
                                 kStandThrottleMs) < 0) {
    return false;
  }
  last_stand_tick_ms = current_tick_ms;
  return true;
}

std::uint32_t CreatureSoundData_GetSoundKitByType(
    const data::dbc::CreatureSoundDataEntry* entry, std::uint32_t type) {
  if (!entry) return 0;

  switch (type) {
    case 0:  return entry->sound_exertion_id;
    case 1:  return entry->sound_exertion_critical_id;
    case 2:
    case 13: return entry->sound_injury_id;
    case 3:  return entry->sound_injury_critical_id;
    case 4:  return entry->sound_stun_id;
    case 5:  return entry->sound_stand_id;
    case 7:  return entry->sound_wing_flap_id;
    case 8:  return entry->sound_alert_id;
    case 9:  return entry->sound_injury_crushing_blow_id;
    case 10: return entry->sound_wing_glide_id;
    case 11: return entry->sound_jump_start_id;
    case 12: return entry->sound_jump_end_id;
    default: return 0;
  }
}

std::uint32_t CreatureSound_GetSoundTypeForChannel(
    std::uint32_t creature_sound_type, bool is_in_combat) {

  switch (creature_sound_type) {
    case 0:
    case 1:
      return is_in_combat ? 15u : 9u;
    case 2:
    case 3:
    case 9:
      return is_in_combat ? 16u : 11u;
    default:
      return 0;
  }
}

bool CreatureSound_NeedsDKModelOverride(std::uint32_t type,
                                         std::uint8_t unit_class) {
  constexpr std::uint8_t kDeathKnightClass = 6;
  if (unit_class != kDeathKnightClass) return false;

  switch (type) {
    case 5:
    case 6:
    case 10:
    case 11:
    case 12:
    case 13:
      return false;
    case 7:
      return false;
    default:
      return true;
  }
}

PlayCreatureSoundResult PlayCreatureSound(
    openwow::audio::SoundRuntime& sound_runtime,
    std::uint32_t type, bool force_play,
    const CreatureSoundUnitState& unit,
    std::uint32_t current_tick_ms,
    std::uint32_t& last_stand_tick_ms,
    bool listener_at_char) {

  PlayCreatureSoundResult result{};

  if (type >= kCreatureSoundTypeCount) return result;

  if (!force_play) {
    const std::uint32_t threshold = kCreatureSoundChanceTable[type];
    const std::uint32_t roll =
        sound_runtime.ConsumePlaybackRandomBoundedValue(101u);
    if (roll > threshold) {
      return result;
    }
  }

  if (!CreatureSound_ThrottleCheck(type, current_tick_ms,
                                    last_stand_tick_ms)) {
    return result;
  }

  const std::uint32_t sound_kit_id =
      CreatureSoundData_GetSoundKitByType(unit.sound_data, type);
  if (sound_kit_id == 0) return result;

  result.sound_kit_id = sound_kit_id;

  std::uint32_t sound_type = 0;
  if ((type == 2 || type == 3 || type == 9) && unit.is_active_player) {
    sound_type = 12;
  } else {
    sound_type = CreatureSound_GetSoundTypeForChannel(type,
                                                       unit.is_in_combat);
  }
  result.sound_type = sound_type;

  float pos[3] = {unit.position_x, unit.position_y,
                   unit.position_z + 2.0f};

  audio::SoundKitPlaybackOptions options{};
  options.sound_type = sound_type;

  std::string dk_model_name;
  if (CreatureSound_NeedsDKModelOverride(type, unit.unit_class)) {
    dk_model_name =
        CharCreate_GetDeathKnightModelName(unit.race, unit.gender);
    result.dk_model_override = dk_model_name;
    options.sound_model_override = dk_model_name;
  }

  if (unit.is_active_player) {
    options.playback_priority = audio::kSelfUnitSoundPlaybackPriority;

    if (type == static_cast<std::uint32_t>(CreatureSoundType::WingFlap)) {
      options.max_audible_behavior =
          audio::SoundKitMaxAudibleBehavior::kMuteAndContinue;
    }

    if (listener_at_char) {
      options.volume_scale = 0.65f;
      result.used_listener_at_char = true;
    }

    const float* play_pos = listener_at_char ? nullptr : pos;
    sound_runtime.PlaySoundKit(
        sound_kit_id, play_pos, nullptr, options);
  } else {

    sound_runtime.PlaySoundKit(
        sound_kit_id, pos, nullptr, options);
  }

  result.played = true;
  return result;
}

}

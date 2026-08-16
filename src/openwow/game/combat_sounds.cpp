
#include "openwow/game/combat_sounds.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/data/formats/dbc/dbc_enums.h"

namespace openwow::game {

std::uint32_t PlayWoundDeathSound(audio::SoundRuntime& sound_runtime,
                                   std::uint32_t sound_class,
                                   bool is_crit,
                                   const float* position,
                                   bool half_volume,
                                   bool use_subtype) {
  if (sound_class >= static_cast<std::uint32_t>(WoundDeathSoundClass::kCount)) {
    return 0;
  }

  const std::uint32_t sound_kit_id = sound_runtime
                                         .GetWoundDeathSoundTable()
                                         .Get(sound_class, is_crit);

  if (sound_kit_id == 0) return 0;

  audio::SoundKitPlaybackOptions options{};
  options.sound_type = 10;
  options.explicit_volume = half_volume ? 0.5f : 1.0f;

  if (use_subtype) {
    options.playback_priority = 110;
  }

  const int result = sound_runtime.PlaySoundKit(
      sound_kit_id, position, nullptr, options);
  return static_cast<std::uint32_t>(result);
}

int PlayCombatMissSound(audio::SoundRuntime& sound_runtime,
                        bool is_one_handed, const float* position,
                        bool use_priority) {
  static constexpr const char* kCombatMiss1H = "(DONOTRENAME)Combat Miss 1H";
  static constexpr const char* kCombatMiss2H = "(DONOTRENAME)Combat Miss 2H";

  audio::SoundKitPlaybackOptions options{};
  if (use_priority) {
    options.playback_priority = 110;
  }

  const char* name = is_one_handed ? kCombatMiss1H : kCombatMiss2H;
  return sound_runtime.PlaySoundKitByName(
      name, position, nullptr, options);
}

EmoteSoundParams GetEmoteSoundParams(bool is_active_player,
                                      bool listener_at_character) {
  EmoteSoundParams p;
  p.is_player = is_active_player;
  p.play_2d = is_active_player && listener_at_character;
  if (is_active_player) {
    p.subtype = 110;
    if (listener_at_character) {
      p.attenuation = 0.65f;
    }
  }
  return p;
}

int PlaySoundKitListenerAware(audio::SoundRuntime& sound_runtime,
                               std::uint32_t sound_kit_id,
                               const float* position,
                               bool is_active_mover,
                               bool listener_at_character) {
  if (sound_kit_id == 0) return 0;

  audio::SoundKitPlaybackOptions options{};
  options.sound_type = 8;

  if (is_active_mover) {
    options.playback_priority = 110;
    if (listener_at_character) {
      options.volume_scale = 0.65f;
    }
  }

  const float* play_position = listener_at_character ? nullptr : position;

  return sound_runtime.PlaySoundKit(
      sound_kit_id, play_position, nullptr, options);
}

}

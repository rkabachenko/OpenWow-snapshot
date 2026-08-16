
#pragma once

#include <cstdint>
namespace openwow::audio { class SoundRuntime; }

namespace openwow::game {

enum class WoundDeathSoundClass : std::uint32_t {
  kClass0  = 0,
  kClass1  = 1,
  kClass2  = 2,
  kCount   = 3,
};

std::uint32_t PlayWoundDeathSound(openwow::audio::SoundRuntime& sound_runtime,
                                   std::uint32_t sound_class,
                                   bool is_crit,
                                   const float* position,
                                   bool half_volume,
                                   bool use_subtype);

int PlayCombatMissSound(openwow::audio::SoundRuntime& sound_runtime,
                        bool is_one_handed, const float* position,
                        bool use_priority);

struct EmoteSoundParams {
  bool is_player{false};
  bool play_2d{false};
  std::uint32_t subtype{0};
  float attenuation{1.0f};
};

EmoteSoundParams GetEmoteSoundParams(bool is_active_player,
                                      bool listener_at_character);

int PlaySoundKitListenerAware(openwow::audio::SoundRuntime& sound_runtime,
                               std::uint32_t sound_kit_id,
                               const float* position,
                               bool is_active_mover,
                               bool listener_at_character);

}

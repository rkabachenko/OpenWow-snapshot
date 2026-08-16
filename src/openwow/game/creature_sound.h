
#pragma once
namespace openwow::audio { class SoundRuntime; }

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace openwow::data::dbc {
struct CreatureSoundDataEntry;
}

namespace openwow::game {

enum class CreatureSoundType : std::uint32_t {
  Exertion         = 0,
  ExertionCritical = 1,
  Injury           = 2,
  InjuryCritical   = 3,
  Stun             = 4,
  Stand            = 5,

  WingFlap         = 7,
  Alert            = 8,
  InjuryCrush      = 9,
  WingGlide        = 10,
  JumpStart        = 11,
  JumpEnd          = 12,
  InjuryAlias      = 13,
};

inline constexpr std::uint32_t kCreatureSoundTypeCount = 14;

inline constexpr std::array<std::uint32_t, kCreatureSoundTypeCount>
    kCreatureSoundChanceTable = {
        35,
        100,
        30,
        100,
        100,
        40,
        100,
        100,
        100,
        100,
        100,
        100,
        100,
        0,
};

[[nodiscard]] bool CreatureSound_ThrottleCheck(
    std::uint32_t type, std::uint32_t current_tick_ms,
    std::uint32_t& last_stand_tick_ms);

[[nodiscard]] std::uint32_t CreatureSoundData_GetSoundKitByType(
    const data::dbc::CreatureSoundDataEntry* entry, std::uint32_t type);

[[nodiscard]] std::uint32_t CreatureSound_GetSoundTypeForChannel(
    std::uint32_t creature_sound_type, bool is_in_combat);

[[nodiscard]] bool CreatureSound_NeedsDKModelOverride(
    std::uint32_t type, std::uint8_t unit_class);

struct CreatureSoundUnitState {
  std::uint64_t guid{0};
  const data::dbc::CreatureSoundDataEntry* sound_data{nullptr};
  float position_x{0.0f};
  float position_y{0.0f};
  float position_z{0.0f};
  bool is_active_player{false};
  bool is_in_combat{false};
  std::uint8_t unit_class{0};
  std::uint8_t race{0};
  std::uint8_t gender{0};
};

struct PlayCreatureSoundResult {
  bool played{false};
  std::uint32_t sound_kit_id{0};
  std::uint32_t sound_type{0};
  bool used_listener_at_char{false};
  std::string dk_model_override;
};

PlayCreatureSoundResult PlayCreatureSound(
    openwow::audio::SoundRuntime& sound_runtime,
    std::uint32_t type, bool force_play,
    const CreatureSoundUnitState& unit,
    std::uint32_t current_tick_ms,
    std::uint32_t& last_stand_tick_ms,
    bool listener_at_char);

}

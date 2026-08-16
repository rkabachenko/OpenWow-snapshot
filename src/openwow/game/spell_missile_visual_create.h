#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::data::dbc {
struct SpellVisualEntry;
struct SpellVisualKitEntry;
struct SpellVisualEffectNameEntry;
struct SpellMissileMotionEntry;
}

namespace openwow::game {

struct SpellMissileEventRecord {
  std::uint32_t record_guid_low{0};
  std::uint32_t record_guid_high{0};
  std::uint32_t payload_word_2{0};
  std::uint32_t payload_word_3{0};
  std::uint32_t spell_id{0};
  float         source_x{0.0f};
  float         source_y{0.0f};
  float         source_z{0.0f};
  float         destination_x{0.0f};
  float         destination_y{0.0f};
  float         destination_z{0.0f};
  float         trajectory_pitch{0.0f};
  float         trajectory_speed{0.0f};
  std::uint32_t duration_ms{0};
  std::uint8_t  progression_rank{0};
  std::uint8_t  missile_cast_count{0};
  std::uint8_t  reserved[6]{};

  [[nodiscard]] static SpellMissileEventRecord Parse(
      const std::uint8_t* data);

  [[nodiscard]] std::uint64_t record_guid() const {
    return (static_cast<std::uint64_t>(record_guid_high) << 32) |
           record_guid_low;
  }
};

static_assert(sizeof(SpellMissileEventRecord) == 0x40);
static_assert(offsetof(SpellMissileEventRecord, source_x) == 0x14);
static_assert(offsetof(SpellMissileEventRecord, destination_x) == 0x20);
static_assert(offsetof(SpellMissileEventRecord, progression_rank) == 0x38);
static_assert(offsetof(SpellMissileEventRecord, missile_cast_count) == 0x39);

struct MissileTimingFromDbc {
  float follow_ground_height{0.25f};
  float follow_ground_drop_speed{0.01f};
  float follow_ground_approach{0.0f};

  [[nodiscard]] static MissileTimingFromDbc FromRaw(
      std::int32_t raw_height_ms,
      std::int32_t raw_drop_speed_ms,
      std::int32_t raw_approach_ms);
};

struct DestLocAreaEffectFlags {

  static constexpr std::uint32_t kDefault = 0x2010;

  static constexpr std::uint32_t kSpellMissile = 0x402010;

  static constexpr std::uint32_t kMaskClearCastBit = 0xFFFFFFEF;
  static constexpr std::uint32_t kSetImpactBit     = 0x400;
  [[nodiscard]] static std::uint32_t ComputeInitialFlags(
      std::uint32_t spell_missile_id);

  [[nodiscard]] static std::uint32_t AfterPrecastEffect(
      std::uint32_t flags);
};

struct DestLocMissileFlags {
  static constexpr std::uint32_t kInitial = 0x1;
  static constexpr std::uint32_t kFollowGround = 0x7800;
  static constexpr std::uint32_t kMotion = 0xB800;
  static constexpr std::uint32_t kTimedTrajectory = 0x10800;
};

[[nodiscard]] std::uint32_t ResolveSalvoCount(
    const data::dbc::SpellMissileMotionEntry* motion_entry);

[[nodiscard]] float ResolveEffectNameModelScale(
    const data::dbc::SpellVisualEffectNameEntry* effect);

}

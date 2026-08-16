
#include "openwow/game/spell_missile_visual_create.h"

#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

#include <algorithm>
#include <cstring>

namespace openwow::game {

SpellMissileEventRecord SpellMissileEventRecord::Parse(
    const std::uint8_t* data) {
  SpellMissileEventRecord r{};
  if (data == nullptr) {
    return r;
  }
  std::memcpy(&r.record_guid_low, data + 0x00, 4);
  std::memcpy(&r.record_guid_high, data + 0x04, 4);
  std::memcpy(&r.payload_word_2, data + 0x08, 4);
  std::memcpy(&r.payload_word_3, data + 0x0C, 4);
  std::memcpy(&r.spell_id, data + 0x10, 4);
  std::memcpy(&r.source_x, data + 0x14, 4);
  std::memcpy(&r.source_y, data + 0x18, 4);
  std::memcpy(&r.source_z, data + 0x1C, 4);
  std::memcpy(&r.destination_x, data + 0x20, 4);
  std::memcpy(&r.destination_y, data + 0x24, 4);
  std::memcpy(&r.destination_z, data + 0x28, 4);
  std::memcpy(&r.trajectory_pitch, data + 0x2C, 4);
  std::memcpy(&r.trajectory_speed, data + 0x30, 4);
  std::memcpy(&r.duration_ms, data + 0x34, 4);
  r.progression_rank = data[0x38];
  r.missile_cast_count = data[0x39];
  std::memcpy(r.reserved, data + 0x3A, 6);
  return r;
}

MissileTimingFromDbc MissileTimingFromDbc::FromRaw(
    const std::int32_t raw_height_ms,
    const std::int32_t raw_drop_speed_ms,
    const std::int32_t raw_approach_ms) {
  MissileTimingFromDbc t{};

  const float height = static_cast<float>(raw_height_ms) * 0.001f;
  t.follow_ground_height = (height > 0.25f) ? height : 0.25f;

  const float drop_speed =
      static_cast<float>(raw_drop_speed_ms) * 0.001f;
  t.follow_ground_drop_speed =
      (drop_speed > 0.0099999998f) ? drop_speed : 0.0099999998f;

  const float approach = static_cast<float>(raw_approach_ms) * 0.001f;
  t.follow_ground_approach = std::clamp(approach, 0.0f, 1.0f);

  return t;
}

std::uint32_t DestLocAreaEffectFlags::ComputeInitialFlags(
    const std::uint32_t spell_missile_id) {
  return spell_missile_id != 0u ? kSpellMissile : kDefault;
}

std::uint32_t DestLocAreaEffectFlags::AfterPrecastEffect(
    std::uint32_t flags) {
  return (flags & kMaskClearCastBit) | kSetImpactBit;
}

std::uint32_t ResolveSalvoCount(
    const data::dbc::SpellMissileMotionEntry* motion_entry) {

  if (motion_entry == nullptr) {
    return 1;
  }
  const auto count = motion_entry->instance_count;
  return (count >= 1) ? count : 1;
}

float ResolveEffectNameModelScale(
    const data::dbc::SpellVisualEffectNameEntry* effect) {
  if (effect == nullptr) {
    return 1.0f;
  }
  return effect->scale;
}

}

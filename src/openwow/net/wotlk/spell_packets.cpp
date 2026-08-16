
#include "openwow/net/wotlk/spell_packets.h"
#include "openwow/network/serialization/packed_guid_codec.h"

#include <cstring>

namespace openwow::net::wotlk {

namespace {

std::size_t ReadU8(const std::uint8_t* data, std::size_t len,
                   std::size_t offset, std::uint8_t& out) {
  if (offset + 1 > len) return 0;
  out = data[offset];
  return offset + 1;
}

std::size_t ReadU32(const std::uint8_t* data, std::size_t len,
                    std::size_t offset, std::uint32_t& out) {
  if (offset + 4 > len) return 0;
  std::memcpy(&out, data + offset, 4);
  return offset + 4;
}

std::size_t ReadI32(const std::uint8_t* data, std::size_t len,
                    std::size_t offset, std::int32_t& out) {
  if (offset + 4 > len) return 0;
  std::memcpy(&out, data + offset, 4);
  return offset + 4;
}

std::size_t ReadFloat(const std::uint8_t* data, std::size_t len,
                      std::size_t offset, float& out) {
  if (offset + 4 > len) return 0;
  std::memcpy(&out, data + offset, 4);
  return offset + 4;
}

std::size_t ReadFullGuid(const std::uint8_t* data, std::size_t len,
                         std::size_t offset, game::ObjectGuid& out) {
  if (offset + 8 > len) return 0;
  std::uint64_t raw = 0;
  std::memcpy(&raw, data + offset, 8);
  out = game::ObjectGuid(raw);
  return offset + 8;
}

std::size_t ReadSpellTargetStringBlock(const std::uint8_t* data,
                                       std::size_t len, std::size_t offset,
                                       std::string& out) {
  out.clear();
  if (offset + kSpellTargetStringBlockBytes > len) return 0;

  const auto* const block = data + offset;
  const auto* const nul = static_cast<const std::uint8_t*>(
      std::memchr(block, 0, kSpellTargetStringBlockBytes));
  out.assign(reinterpret_cast<const char*>(block),
             nul != nullptr ? static_cast<std::size_t>(nul - block)
                            : kSpellTargetStringBlockBytes);
  return offset + kSpellTargetStringBlockBytes;
}

std::size_t ReadSpellPosition(const std::uint8_t* data, std::size_t len,
                              std::size_t offset, SpellPosition& out) {
  offset = ReadPackedGuid(data, len, offset, out.transport_guid);
  if (offset == 0) return 0;
  offset = ReadFloat(data, len, offset, out.x);
  if (offset == 0) return 0;
  offset = ReadFloat(data, len, offset, out.y);
  if (offset == 0) return 0;
  offset = ReadFloat(data, len, offset, out.z);
  return offset;
}

std::size_t ReadSpellCastHeader(const std::uint8_t* data, const std::size_t len,
                                std::size_t offset, auto& result) {
  offset = ReadPackedGuid(data, len, offset, result.caster_guid);
  if (offset == 0) return 0;
  offset = ReadPackedGuid(data, len, offset, result.caster_unit_guid);
  if (offset == 0) return 0;
  offset = ReadU8(data, len, offset, result.cast_count);
  if (offset == 0) return 0;
  offset = ReadU32(data, len, offset, result.spell_id);
  if (offset == 0) return 0;

  std::uint32_t flags = 0;
  offset = ReadU32(data, len, offset, flags);
  result.cast_flags = static_cast<SpellCastFlags>(flags);
  return offset;
}

constexpr std::uint8_t kPlayerRuneSlotCount = 6;

std::size_t ReadCastPowerAndRunes(
    const std::uint8_t* data, const std::size_t len, std::size_t offset,
    const SpellCastFlags flags, std::optional<std::uint32_t>& current_power,
    std::optional<RuneData>& rune_data) {
  if (HasFlag(flags, SpellCastFlags::kPowerLeftSelf)) {
    std::uint32_t power = 0;
    offset = ReadU32(data, len, offset, power);
    if (offset == 0) return 0;
    current_power = power;
  }

  if (!HasFlag(flags, SpellCastFlags::kRuneList)) return offset;

  RuneData runes;
  offset = ReadU8(data, len, offset, runes.mask_before);
  if (offset == 0) return 0;
  offset = ReadU8(data, len, offset, runes.mask_after);
  if (offset == 0) return 0;
  for (std::uint8_t slot = 0; slot < kPlayerRuneSlotCount; ++slot) {
    const auto slot_mask = static_cast<std::uint8_t>(1u << slot);
    const bool consumed =
        (runes.mask_before & slot_mask) != 0 &&
        (runes.mask_after & slot_mask) == 0;
    if (!consumed) continue;

    std::uint8_t cooldown = 0;
    offset = ReadU8(data, len, offset, cooldown);
    if (offset == 0) return 0;
    runes.cooldowns.push_back(cooldown);
  }
  rune_data = std::move(runes);
  return offset;
}

std::size_t ReadOptionalAmmo(const std::uint8_t* data, const std::size_t len,
                             std::size_t offset, const SpellCastFlags flags,
                             std::optional<AmmoData>& ammo) {
  if (!HasFlag(flags, SpellCastFlags::kProjectile)) return offset;

  AmmoData value;
  offset = ReadU32(data, len, offset, value.display_id);
  if (offset == 0) return 0;
  offset = ReadU32(data, len, offset, value.inventory_type);
  if (offset != 0) ammo = value;
  return offset;
}

}

std::size_t ReadPackedGuid(const std::uint8_t* data, std::size_t len,
                           std::size_t offset, game::ObjectGuid& out) {
  if (!data || offset >= len) return 0;
  const auto decoded = openwow::net::DecodePackedGuid(
      data + offset, len - offset);
  if (!decoded) return 0;
  out = game::ObjectGuid(decoded.value);
  return offset + decoded.bytes_consumed;
}

std::size_t ReadSpellCastTargets(const std::uint8_t* data, std::size_t len,
                                 std::size_t offset, SpellCastTargets& out) {
  out = {};

  std::uint32_t mask_raw = 0;
  offset = ReadU32(data, len, offset, mask_raw);
  if (offset == 0) return 0;
  out.target_mask = static_cast<SpellCastTargetFlags>(mask_raw);

  constexpr auto kObjectGuidMask =
      SpellCastTargetFlags::kUnit | SpellCastTargetFlags::kCorpseAlly |
      SpellCastTargetFlags::kGameObject | SpellCastTargetFlags::kCorpseEnemy |
      SpellCastTargetFlags::kUnitMinipet;
  static_assert(static_cast<std::uint32_t>(kObjectGuidMask) == 0x00018a02u);
  if (HasFlag(out.target_mask, kObjectGuidMask)) {
    offset = ReadPackedGuid(data, len, offset, out.object_target_guid);
    if (offset == 0) return 0;
  }

  constexpr auto kItemGuidMask =
      SpellCastTargetFlags::kItem | SpellCastTargetFlags::kTradeItem;
  static_assert(static_cast<std::uint32_t>(kItemGuidMask) == 0x00001010u);
  if (HasFlag(out.target_mask, kItemGuidMask)) {
    offset = ReadPackedGuid(data, len, offset, out.item_target_guid);
    if (offset == 0) return 0;
  }

  if (HasFlag(out.target_mask, SpellCastTargetFlags::kSourceLocation)) {
    SpellPosition pos;
    offset = ReadSpellPosition(data, len, offset, pos);
    if (offset == 0) return 0;
    out.source_location = pos;
  }

  if (HasFlag(out.target_mask, SpellCastTargetFlags::kDestLocation)) {
    SpellPosition pos;
    offset = ReadSpellPosition(data, len, offset, pos);
    if (offset == 0) return 0;
    out.dest_location = pos;
  }

  if (HasFlag(out.target_mask, SpellCastTargetFlags::kString)) {
    offset = ReadSpellTargetStringBlock(data, len, offset, out.target_string);
    if (offset == 0) return 0;
  }

  return offset;
}

std::optional<SpellStartData>
ParseSpellStart(const std::uint8_t* data, std::size_t len) {
  if (!data || len == 0) return std::nullopt;

  SpellStartData result;
  std::size_t off = 0;

  off = ReadSpellCastHeader(data, len, off, result);
  if (off == 0) return std::nullopt;

  off = ReadI32(data, len, off, result.cast_time);
  if (off == 0) return std::nullopt;

  off = ReadSpellCastTargets(data, len, off, result.targets);
  if (off == 0) return std::nullopt;

  off = ReadCastPowerAndRunes(data, len, off, result.cast_flags,
                              result.current_power, result.rune_data);
  if (off == 0) return std::nullopt;
  off = ReadOptionalAmmo(data, len, off, result.cast_flags, result.ammo);
  if (off == 0) return std::nullopt;

  if (HasFlag(result.cast_flags, SpellCastFlags::kImmunity)) {
    SpellImmunityData immunity;
    off = ReadU32(data, len, off, immunity.school_mask);
    if (off == 0) return std::nullopt;
    off = ReadU32(data, len, off, immunity.mechanic_mask);
    if (off == 0) return std::nullopt;
    result.immunity = immunity;
  }

  return result;
}

std::optional<SpellGoData>
ParseSpellGo(const std::uint8_t* data, std::size_t len) {
  if (!data || len == 0) return std::nullopt;

  SpellGoData result;
  std::size_t off = 0;

  off = ReadSpellCastHeader(data, len, off, result);
  if (off == 0) return std::nullopt;

  off = ReadU32(data, len, off, result.timestamp);
  if (off == 0) return std::nullopt;

  std::uint8_t hit_count = 0;
  off = ReadU8(data, len, off, hit_count);
  if (off == 0) return std::nullopt;
  result.hit_targets.reserve(hit_count);
  for (std::uint8_t i = 0; i < hit_count; ++i) {
    game::ObjectGuid guid;
    off = ReadFullGuid(data, len, off, guid);
    if (off == 0) return std::nullopt;
    result.hit_targets.push_back(guid);
  }

  std::uint8_t miss_count = 0;
  off = ReadU8(data, len, off, miss_count);
  if (off == 0) return std::nullopt;
  result.miss_targets.reserve(miss_count);
  for (std::uint8_t i = 0; i < miss_count; ++i) {
    SpellMissEntry entry;
    off = ReadFullGuid(data, len, off, entry.target);
    if (off == 0) return std::nullopt;
    std::uint8_t reason = 0;
    off = ReadU8(data, len, off, reason);
    if (off == 0) return std::nullopt;
    entry.reason = static_cast<SpellMissInfo>(reason);
    if (entry.reason == SpellMissInfo::kReflect) {
      std::uint8_t reflect = 0;
      off = ReadU8(data, len, off, reflect);
      if (off == 0) return std::nullopt;
      entry.reflect = static_cast<SpellMissInfo>(reflect);
    }
    result.miss_targets.push_back(entry);
  }

  off = ReadSpellCastTargets(data, len, off, result.targets);
  if (off == 0) return std::nullopt;

  off = ReadCastPowerAndRunes(data, len, off, result.cast_flags,
                              result.current_power, result.rune_data);
  if (off == 0) return std::nullopt;

  if (HasFlag(result.cast_flags, SpellCastFlags::kAdjustMissile)) {
    MissileData missile;
    off = ReadFloat(data, len, off, missile.elevation);
    if (off == 0) return std::nullopt;
    off = ReadU32(data, len, off, missile.delay_time);
    if (off == 0) return std::nullopt;
    result.missile = missile;
  }

  off = ReadOptionalAmmo(data, len, off, result.cast_flags, result.ammo);
  if (off == 0) return std::nullopt;

  if (HasFlag(result.cast_flags, SpellCastFlags::kVisualChain)) {
    std::uint32_t a = 0, b = 0;
    off = ReadU32(data, len, off, a);
    if (off == 0) return std::nullopt;
    off = ReadU32(data, len, off, b);
    if (off == 0) return std::nullopt;
    result.visual_chain = {a, b};
  }

  if (HasFlag(result.targets.target_mask, SpellCastTargetFlags::kDestLocation)) {
    std::uint8_t idx = 0;
    off = ReadU8(data, len, off, idx);
    if (off == 0) return std::nullopt;
    result.dest_loc_spell_cast_index = idx;
  }

  if (HasFlag(result.targets.target_mask,
              SpellCastTargetFlags::kExtraTargets)) {
    std::uint32_t count = 0;
    off = ReadU32(data, len, off, count);
    if (off == 0) return std::nullopt;

    constexpr std::size_t kExtraTargetWireBytes =
        sizeof(float) * 3u + sizeof(std::uint64_t);
    if (count > (len - off) / kExtraTargetWireBytes) {
      return std::nullopt;
    }

    result.extra_targets.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      SpellExtraTarget target;
      off = ReadFloat(data, len, off, target.x);
      if (off == 0) return std::nullopt;
      off = ReadFloat(data, len, off, target.y);
      if (off == 0) return std::nullopt;
      off = ReadFloat(data, len, off, target.z);
      if (off == 0) return std::nullopt;
      off = ReadFullGuid(data, len, off, target.transport_guid);
      if (off == 0) return std::nullopt;
      result.extra_targets.push_back(target);
    }
  }

  return result;
}

std::optional<SpellFailureData>
ParseSpellFailure(const std::uint8_t* data, std::size_t len) {
  if (!data || len == 0) return std::nullopt;

  SpellFailureData result;
  std::size_t off = 0;

  off = ReadPackedGuid(data, len, off, result.caster_guid);
  if (off == 0) return std::nullopt;
  off = ReadU8(data, len, off, result.cast_count);
  if (off == 0) return std::nullopt;
  off = ReadU32(data, len, off, result.spell_id);
  if (off == 0) return std::nullopt;
  off = ReadU8(data, len, off, result.result);
  if (off == 0) return std::nullopt;

  return result;
}

std::optional<CastFailedData>
ParseCastFailed(const std::uint8_t* data, std::size_t len) {
  if (!data || len == 0) return std::nullopt;

  CastFailedData result;
  std::size_t off = 0;

  off = ReadU8(data, len, off, result.cast_count);
  if (off == 0) return std::nullopt;
  off = ReadU32(data, len, off, result.spell_id);
  if (off == 0) return std::nullopt;
  off = ReadU8(data, len, off, result.result);
  if (off == 0) return std::nullopt;

  while (off + 4 <= len) {
    std::uint32_t extra_val = 0;
    off = ReadU32(data, len, off, extra_val);
    if (off == 0) break;
    result.extra.push_back(extra_val);
  }

  return result;
}

std::optional<SpellDelayedData>
ParseSpellDelayed(const std::uint8_t* data, std::size_t len) {
  if (!data || len == 0) return std::nullopt;

  SpellDelayedData result;
  std::size_t off = 0;

  off = ReadPackedGuid(data, len, off, result.caster_guid);
  if (off == 0) return std::nullopt;
  off = ReadU32(data, len, off, result.delay_time);
  if (off == 0) return std::nullopt;

  return result;
}

std::optional<ChannelStartData>
ParseChannelStart(const std::uint8_t* data, std::size_t len) {
  if (!data || len == 0) return std::nullopt;

  ChannelStartData result;
  std::size_t off = 0;

  off = ReadPackedGuid(data, len, off, result.caster_guid);
  if (off == 0) return std::nullopt;
  off = ReadU32(data, len, off, result.spell_id);
  if (off == 0) return std::nullopt;
  off = ReadU32(data, len, off, result.duration);
  if (off == 0) return std::nullopt;

  return result;
}

std::optional<ChannelUpdateData>
ParseChannelUpdate(const std::uint8_t* data, std::size_t len) {
  if (!data || len == 0) return std::nullopt;

  ChannelUpdateData result;
  std::size_t off = 0;

  off = ReadPackedGuid(data, len, off, result.caster_guid);
  if (off == 0) return std::nullopt;
  off = ReadU32(data, len, off, result.remaining);
  if (off == 0) return std::nullopt;

  return result;
}

}

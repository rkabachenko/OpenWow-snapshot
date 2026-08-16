
#include "openwow/net/wotlk/combat_packets.h"

#include <cstring>

namespace openwow::net::wotlk {

namespace {

std::size_t RdU8(const std::uint8_t* data, std::size_t len,
                 std::size_t off, std::uint8_t& out) {
  if (off + 1 > len) return 0;
  out = data[off];
  return off + 1;
}

std::size_t RdU32(const std::uint8_t* data, std::size_t len,
                  std::size_t off, std::uint32_t& out) {
  if (off + 4 > len) return 0;
  std::memcpy(&out, data + off, 4);
  return off + 4;
}

std::size_t RdI32(const std::uint8_t* data, std::size_t len,
                  std::size_t off, std::int32_t& out) {
  if (off + 4 > len) return 0;
  std::memcpy(&out, data + off, 4);
  return off + 4;
}

std::size_t RdU64(const std::uint8_t* data, std::size_t len,
                  std::size_t off, std::uint64_t& out) {
  if (off + 8 > len) return 0;
  std::memcpy(&out, data + off, 8);
  return off + 8;
}

std::size_t RdFloat(const std::uint8_t* data, std::size_t len,
                    std::size_t off, float& out) {
  if (off + 4 > len) return 0;
  std::memcpy(&out, data + off, 4);
  return off + 4;
}

std::size_t RdGuid(const std::uint8_t* data, std::size_t len,
                   std::size_t off, game::ObjectGuid& out) {
  std::uint64_t raw;
  auto next = RdU64(data, len, off, raw);
  if (next == 0) return 0;
  out = game::ObjectGuid(raw);
  return next;
}

std::size_t RdPackedGuid(const std::uint8_t* data, std::size_t len,
                         std::size_t off, game::ObjectGuid& out) {
  if (off >= len) return 0;
  auto consumed = game::ObjectGuid::Unpack(data + off, len - off, out);
  if (consumed == 0) return 0;
  return off + consumed;
}

std::size_t Skip(std::size_t off, std::size_t n, std::size_t len) {
  if (off + n > len) return 0;
  return off + n;
}

}

std::size_t CombatReadPackedGuid(const std::uint8_t* data,
                                  std::size_t len,
                                  std::size_t offset,
                                  game::ObjectGuid& out) {
  return RdPackedGuid(data, len, offset, out);
}

std::optional<AttackerStateUpdateData>
ParseAttackerStateUpdate(const std::uint8_t* data, std::size_t len) {
  AttackerStateUpdateData out;
  std::size_t off = 0;

  off = RdU32(data, len, off, out.hit_info);
  if (off == 0) return std::nullopt;

  off = RdPackedGuid(data, len, off, out.attacker);
  if (off == 0) return std::nullopt;

  off = RdPackedGuid(data, len, off, out.victim);
  if (off == 0) return std::nullopt;

  off = RdU32(data, len, off, out.total_damage);
  if (off == 0) return std::nullopt;

  off = RdU32(data, len, off, out.overkill);
  if (off == 0) return std::nullopt;

  std::uint8_t sub_count;
  off = RdU8(data, len, off, sub_count);
  if (off == 0) return std::nullopt;

  out.sub_damages.resize(sub_count);
  for (std::uint8_t i = 0; i < sub_count; ++i) {
    off = RdU32(data, len, off, out.sub_damages[i].school_mask);
    if (off == 0) return std::nullopt;
    off = RdFloat(data, len, off, out.sub_damages[i].damage_float);
    if (off == 0) return std::nullopt;
    off = RdU32(data, len, off, out.sub_damages[i].damage);
    if (off == 0) return std::nullopt;
  }

  if (out.hit_info & (CombatHitInfo::kFullAbsorb | CombatHitInfo::kPartialAbsorb)) {
    for (auto& sub : out.sub_damages) {
      off = RdU32(data, len, off, sub.absorbed);
      if (off == 0) return std::nullopt;
    }
  }

  if (out.hit_info & (CombatHitInfo::kFullResist | CombatHitInfo::kPartialResist)) {
    for (auto& sub : out.sub_damages) {
      off = RdU32(data, len, off, sub.resisted);
      if (off == 0) return std::nullopt;
    }
  }

  std::uint8_t vs;
  off = RdU8(data, len, off, vs);
  if (off == 0) return std::nullopt;
  out.victim_state = static_cast<CombatVictimState>(vs);

  off = RdU32(data, len, off, out.attacker_state);
  if (off == 0) return std::nullopt;

  off = RdU32(data, len, off, out.melee_spell_id);
  if (off == 0) return std::nullopt;

  if (out.hit_info & CombatHitInfo::kBlock) {
    off = RdU32(data, len, off, out.blocked_amount);
    if (off == 0) return std::nullopt;
  }

  if (out.hit_info & CombatHitInfo::kRageGain) {
    off = RdU32(data, len, off, out.rage_gain);
    if (off == 0) return std::nullopt;
  }

  if (out.hit_info & CombatHitInfo::kUnk1) {
    off = Skip(off, 48, len);
    if (off == 0) return std::nullopt;
  }

  return out;
}

std::optional<AttackStartData>
ParseAttackStart(const std::uint8_t* data, std::size_t len) {
  AttackStartData out;
  std::size_t off = 0;

  off = RdGuid(data, len, off, out.attacker);
  if (off == 0) return std::nullopt;

  off = RdGuid(data, len, off, out.victim);
  if (off == 0) return std::nullopt;

  return out;
}

std::optional<AttackStopData>
ParseAttackStop(const std::uint8_t* data, std::size_t len) {
  AttackStopData out;
  std::size_t off = 0;

  off = RdPackedGuid(data, len, off, out.attacker);
  if (off == 0) return std::nullopt;

  off = RdPackedGuid(data, len, off, out.victim);
  if (off == 0) return std::nullopt;

  off = RdU32(data, len, off, out.now_dead);
  if (off == 0) return std::nullopt;

  return out;
}

std::optional<PowerUpdateData>
ParsePowerUpdate(const std::uint8_t* data, std::size_t len) {
  PowerUpdateData out;
  std::size_t off = 0;

  off = RdPackedGuid(data, len, off, out.unit);
  if (off == 0) return std::nullopt;

  std::uint8_t pt;
  off = RdU8(data, len, off, pt);
  if (off == 0) return std::nullopt;
  out.power_type = static_cast<game::PowerType>(pt);

  off = RdU32(data, len, off, out.value);
  if (off == 0) return std::nullopt;

  return out;
}

std::optional<HealthUpdateData>
ParseHealthUpdate(const std::uint8_t* data, std::size_t len) {
  HealthUpdateData out;
  std::size_t off = 0;

  off = RdPackedGuid(data, len, off, out.unit);
  if (off == 0) return std::nullopt;

  off = RdU32(data, len, off, out.health);
  if (off == 0) return std::nullopt;

  return out;
}

std::optional<AiReactionData>
ParseAiReaction(const std::uint8_t* data, std::size_t len) {
  AiReactionData out;
  std::size_t off = 0;

  off = RdGuid(data, len, off, out.unit);
  if (off == 0) return std::nullopt;

  off = RdU32(data, len, off, out.reaction);
  if (off == 0) return std::nullopt;

  return out;
}

std::optional<EnvironmentalDamageData>
ParseEnvironmentalDamage(const std::uint8_t* data, std::size_t len) {
  EnvironmentalDamageData out;
  std::size_t off = 0;

  off = RdGuid(data, len, off, out.guid);
  if (off == 0) return std::nullopt;

  off = RdU8(data, len, off, out.type);
  if (off == 0) return std::nullopt;

  off = RdU32(data, len, off, out.damage);
  if (off == 0) return std::nullopt;

  off = RdU32(data, len, off, out.absorbed);
  if (off == 0) return std::nullopt;

  off = RdU32(data, len, off, out.resisted);
  if (off == 0) return std::nullopt;

  return out;
}

std::optional<XpGainData>
ParseLogXpGain(const std::uint8_t* data, std::size_t len) {
  XpGainData out;
  std::size_t off = 0;

  off = RdGuid(data, len, off, out.victim);
  if (off == 0) return std::nullopt;

  off = RdU32(data, len, off, out.xp_total);
  if (off == 0) return std::nullopt;

  off = RdU8(data, len, off, out.xp_type);
  if (off == 0) return std::nullopt;

  if (out.xp_type == 0) {

    float rate;
    auto next = RdFloat(data, len, off, rate);
    if (next != 0) {
      out.group_rate = rate;
      off = next;
    }
  }

  return out;
}

bool ParseAuraSlotBlock(const std::uint8_t* data, std::size_t len,
                        std::size_t& offset, AuraSlotData& out,
                        const game::ObjectGuid& target) {
  auto off = RdU8(data, len, offset, out.slot);
  if (off == 0) return false;

  off = RdU32(data, len, off, out.spell_id);
  if (off == 0) return false;

  if (out.spell_id == 0) {

    offset = off;
    return true;
  }

  off = RdU8(data, len, off, out.flags);
  if (off == 0) return false;

  off = RdU8(data, len, off, out.caster_level);
  if (off == 0) return false;

  off = RdU8(data, len, off, out.stack_or_charges);
  if (off == 0) return false;

  if ((out.flags & 0x08) != 0) {
    out.caster_guid = target;
  } else {
    off = RdPackedGuid(data, len, off, out.caster_guid);
    if (off == 0) return false;
  }

  if (out.flags & 0x20) {
    off = RdU32(data, len, off, out.max_duration);
    if (off == 0) return false;
    off = RdU32(data, len, off, out.remaining_duration);
    if (off == 0) return false;
  }

  offset = off;
  return true;
}

std::optional<AuraUpdateData>
ParseAuraUpdate(const std::uint8_t* data, std::size_t len) {
  AuraUpdateData out;
  std::size_t off = 0;

  off = RdPackedGuid(data, len, off, out.target);
  if (off == 0) return std::nullopt;

  while (off < len) {
    AuraSlotData slot;
    if (!ParseAuraSlotBlock(data, len, off, slot, out.target)) break;
    out.slots.push_back(slot);
  }
  if (out.slots.empty()) return std::nullopt;

  return out;
}

std::optional<AuraUpdateAllData>
ParseAuraUpdateAll(const std::uint8_t* data, std::size_t len) {
  AuraUpdateAllData out;
  std::size_t off = 0;

  off = RdPackedGuid(data, len, off, out.target);
  if (off == 0) return std::nullopt;

  while (off < len) {
    AuraSlotData slot;
    if (!ParseAuraSlotBlock(data, len, off, slot, out.target)) break;
    out.slots.push_back(slot);
  }

  return out;
}

namespace {

bool ReadThreatEntries(const std::uint8_t* data, std::size_t len,
                       std::size_t& off,
                       std::vector<ThreatEntryData>& entries) {
  std::uint32_t count;
  auto next = RdU32(data, len, off, count);
  if (next == 0) return false;
  off = next;

  entries.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    ThreatEntryData te;
    off = RdPackedGuid(data, len, off, te.unit);
    if (off == 0) return false;
    off = RdU32(data, len, off, te.threat);
    if (off == 0) return false;
    entries.push_back(te);
  }
  return true;
}

}

std::optional<ThreatUpdateData>
ParseThreatUpdate(const std::uint8_t* data, std::size_t len) {
  ThreatUpdateData out;
  std::size_t off = 0;

  off = RdPackedGuid(data, len, off, out.target);
  if (off == 0) return std::nullopt;

  if (!ReadThreatEntries(data, len, off, out.entries)) return std::nullopt;

  return out;
}

std::optional<HighestThreatUpdateData>
ParseHighestThreatUpdate(const std::uint8_t* data, std::size_t len) {
  HighestThreatUpdateData out;
  std::size_t off = 0;

  off = RdPackedGuid(data, len, off, out.target);
  if (off == 0) return std::nullopt;

  off = RdPackedGuid(data, len, off, out.highest);
  if (off == 0) return std::nullopt;

  if (!ReadThreatEntries(data, len, off, out.entries)) return std::nullopt;

  return out;
}

std::optional<LevelUpInfoData>
ParseLevelUpInfo(const std::uint8_t* data, std::size_t len) {

  if (len < 56) return std::nullopt;

  LevelUpInfoData out;
  std::size_t off = 0;

  off = RdU32(data, len, off, out.level);
  if (off == 0) return std::nullopt;

  off = RdI32(data, len, off, out.health_delta);
  if (off == 0) return std::nullopt;

  off = RdI32(data, len, off, out.mana_delta);
  if (off == 0) return std::nullopt;

  for (int i = 0; i < 6; ++i) {
    off = RdI32(data, len, off, out.power_delta[i]);
    if (off == 0) return std::nullopt;
  }

  for (int i = 0; i < 5; ++i) {
    off = RdI32(data, len, off, out.stat_delta[i]);
    if (off == 0) return std::nullopt;
  }

  return out;
}

}

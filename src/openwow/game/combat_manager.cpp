
#include "openwow/game/combat_manager.h"
#include "openwow/game/threat_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>

namespace openwow::game {

using openwow::diagnostics::Log;
using openwow::diagnostics::LogLevel;

bool CombatManager::HandleAttackStart(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  AttackStartInfo info;
  if (!r.ReadGuid(info.attacker)) return false;
  if (!r.ReadGuid(info.victim)) return false;
  attack_start_ = info;
  attack_stop_.reset();
  in_combat_ = true;
  return true;
}

bool CombatManager::HandleAttackStop(const std::uint8_t* data,
                                      std::size_t len) {
  PacketReader r(data, len);

  ObjectGuid attacker, victim;
  if (!r.ReadPackedGuid(attacker)) return false;
  if (!r.ReadPackedGuid(victim)) return false;
  std::uint32_t unk;
  if (!r.ReadU32(unk)) return false;

  attack_stop_ = AttackStartInfo{.attacker = attacker, .victim = victim};

  in_combat_ = false;
  attack_start_.reset();
  return true;
}

bool CombatManager::HandleAttackerStateUpdate(const std::uint8_t* data,
                                               std::size_t len) {
  PacketReader r(data, len);

  return HandleAttackerStateUpdate(r);
}

bool CombatManager::HandleAttackerStateUpdate(PacketReader& r) {

  AttackerStateUpdate upd;
  if (!r.ReadU32(upd.hit_info)) return false;
  if (!r.ReadPackedGuid(upd.attacker)) return false;
  if (!r.ReadPackedGuid(upd.victim)) return false;
  if (!r.ReadU32(upd.total_damage)) return false;
  if (!r.ReadU32(upd.overkill)) return false;

  std::uint8_t sub_count;
  if (!r.ReadU8(sub_count)) return false;

  for (std::uint8_t i = 0; i < sub_count; ++i) {
    SubDamageInfo sub;
    if (!r.ReadU32(sub.school_mask)) return false;
    if (!r.ReadFloat(sub.damage_float)) return false;
    if (!r.ReadU32(sub.damage)) return false;
    upd.sub_damages.push_back(sub);
  }

  if (upd.hit_info & (HitInfoFlag::kFullAbsorb | HitInfoFlag::kPartialAbsorb)) {
    for (auto& sub : upd.sub_damages) {
      if (!r.ReadU32(sub.absorbed)) return false;
    }
  }

  if (upd.hit_info & (HitInfoFlag::kFullResist | HitInfoFlag::kPartialResist)) {
    for (auto& sub : upd.sub_damages) {
      if (!r.ReadU32(sub.resisted)) return false;
    }
  }

  std::uint8_t victim_state_raw;
  if (!r.ReadU8(victim_state_raw)) return false;
  upd.victim_state = static_cast<VictimState>(victim_state_raw);
  if (!r.ReadU32(upd.attacker_state)) return false;
  if (!r.ReadU32(upd.melee_spell_id)) return false;

  if (upd.hit_info & HitInfoFlag::kBlock) {
    if (!r.ReadU32(upd.blocked_amount)) return false;
  }

  if (upd.hit_info & HitInfoFlag::kRageGain) {
    std::uint32_t rage;
    if (!r.ReadU32(rage)) return false;
  }

  if (upd.hit_info & HitInfoFlag::kUnk1) {

    if (!r.HasBytes(48)) return false;
    r.Skip(48);
  }

  last_state_update_ = upd;
  return true;
}

bool CombatManager::HandleCancelCombat(const std::uint8_t* ,
                                        std::size_t ) {
  in_combat_ = false;
  return true;
}

bool CombatManager::HandleAiReaction(const std::uint8_t* data,
                                      std::size_t len) {
  PacketReader r(data, len);
  AiReaction info;
  if (!r.ReadGuid(info.unit)) return false;
  if (!r.ReadU32(info.reaction)) return false;
  ai_reaction_ = info;
  return true;
}

bool CombatManager::HandleAttackSwingError(AttackSwingError error) {
  swing_error_ = error;
  return true;
}

void CombatManager::ClearSwingError(std::uint32_t current_time_ms) {
  swing_error_ = AttackSwingError::kNone;
  swing_error_next_display_ms_ = current_time_ms;
}

void CombatManager::SetSwingError(AttackSwingError error,
                                   std::uint32_t current_time_ms) {
  swing_error_ = error;
  swing_error_next_display_ms_ = current_time_ms;
}

bool CombatManager::TryConsumeSwingErrorForDisplay(
    std::uint32_t current_time_ms, AttackSwingError& out_error) {
  if (swing_error_ != AttackSwingError::kNotInRange &&
      swing_error_ != AttackSwingError::kBadFacing) {
    return false;
  }

  if (static_cast<std::int32_t>(current_time_ms - swing_error_next_display_ms_) < 0) {
    return false;
  }
  swing_error_next_display_ms_ = current_time_ms + kSwingErrorDisplayThrottleMs;
  out_error = swing_error_;
  return true;
}

bool CombatManager::HandleHealthUpdate(const std::uint8_t* data,
                                        std::size_t len) {
  PacketReader r(data, len);
  HealthUpdate info;
  if (!r.ReadPackedGuid(info.guid)) return false;
  if (!r.ReadU32(info.health)) return false;
  last_health_update_ = info;
  return true;
}

bool CombatManager::HandlePowerUpdate(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  PowerUpdate info;
  if (!r.ReadPackedGuid(info.guid)) return false;
  std::uint8_t power_type_raw;
  if (!r.ReadU8(power_type_raw)) return false;
  info.power_type = static_cast<PowerType>(power_type_raw);
  if (!r.ReadU32(info.value)) return false;
  last_power_update_ = info;
  return true;
}

static bool ReadThreatList(PacketReader& r,
                           std::vector<ThreatUpdateEntry>& entries) {
  std::uint32_t count;
  if (!r.ReadU32(count)) return false;
  entries.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    ThreatUpdateEntry te;
    if (!r.ReadPackedGuid(te.unit)) return false;
    if (!r.ReadU32(te.threat)) return false;
    entries.push_back(te);
  }
  return true;
}

bool CombatManager::HandleThreatUpdate(const std::uint8_t* data,
                                        std::size_t len) {
  PacketReader r(data, len);
  ThreatUpdate upd;
  if (!r.ReadPackedGuid(upd.target)) return false;
  if (!ReadThreatList(r, upd.entries)) return false;
  threat_update_ = upd;

  std::vector<ThreatInfo> threats;
  threats.reserve(upd.entries.size());
  for (const auto& entry : upd.entries) {
    ThreatInfo threat;
    threat.unit_guid = entry.unit;
    threat.threat_value = entry.threat;
    threats.push_back(threat);
  }
  ThreatSystem::Get().ApplyThreatPacketUpdate(upd.target, threats);
  return true;
}

bool CombatManager::HandleHighestThreatUpdate(const std::uint8_t* data,
                                               std::size_t len) {
  PacketReader r(data, len);
  HighestThreatUpdate upd;
  if (!r.ReadPackedGuid(upd.target)) return false;
  if (!r.ReadPackedGuid(upd.highest)) return false;
  if (!ReadThreatList(r, upd.entries)) return false;
  highest_threat_update_ = upd;

  std::vector<ThreatInfo> threats;
  threats.reserve(upd.entries.size());
  for (const auto& entry : upd.entries) {
    ThreatInfo threat;
    threat.unit_guid = entry.unit;
    threat.threat_value = entry.threat;
    threats.push_back(threat);
  }
  ThreatSystem::Get().ApplyHighestThreatPacketUpdate(
      upd.target, upd.highest, threats);
  return true;
}

bool CombatManager::HandleThreatClear(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid guid;
  if (!r.ReadPackedGuid(guid)) return false;

  if (threat_update_.has_value() &&
      threat_update_->target == guid) {
    threat_update_.reset();
  }
  if (highest_threat_update_.has_value() &&
      highest_threat_update_->target == guid) {
    highest_threat_update_.reset();
  }
  ThreatSystem::Get().ClearThreatList(guid);
  return true;
}

bool CombatManager::HandleThreatRemove(const std::uint8_t* data,
                                        std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid target, victim;
  if (!r.ReadPackedGuid(target)) return false;
  if (!r.ReadPackedGuid(victim)) return false;

  if (threat_update_.has_value() &&
      threat_update_->target == target) {
    auto& entries = threat_update_->entries;
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
                        [&](const ThreatUpdateEntry& e) {
                          return e.unit == victim;
                        }),
        entries.end());
  }
  if (highest_threat_update_.has_value() &&
      highest_threat_update_->target == target) {
    auto& entries = highest_threat_update_->entries;
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
                       [&](const ThreatUpdateEntry& e) {
                         return e.unit == victim;
                       }),
        entries.end());
    if (highest_threat_update_->highest == victim) {
      highest_threat_update_->highest = ObjectGuid{};
    }
  }
  ThreatSystem::Get().RemoveThreatEntry(target, victim);
  return true;
}

bool CombatManager::HandleLogXpGain(const std::uint8_t* data,
                                     std::size_t len) {
  PacketReader r(data, len);
  XpGainLog info;
  if (!r.ReadGuid(info.victim)) return false;
  if (!r.ReadU32(info.xp_total)) return false;
  if (!r.ReadU8(info.xp_type)) return false;
  if (info.xp_type == 0) {
    float group_rate;
    if (r.ReadFloat(group_rate)) {
      info.group_rate = group_rate;
    }
  }
  xp_gain_ = info;
  return true;
}

bool CombatManager::HandleLevelUpInfo(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);

  if (!r.HasBytes(56)) return false;

  LevelUpInfo info;
  if (!r.ReadU32(info.level)) return false;
  if (!r.ReadI32(info.health_delta)) return false;
  if (!r.ReadI32(info.mana_delta)) return false;
  for (int i = 0; i < 6; ++i) {
    if (!r.ReadI32(info.power_delta[i])) return false;
  }
  for (int i = 0; i < 5; ++i) {
    if (!r.ReadI32(info.stat_delta[i])) return false;
  }

  level_up_ = info;
  return true;
}

bool CombatManager::HandleEnvironmentalDamageLog(const std::uint8_t* data,
                                                  std::size_t len) {
  PacketReader r(data, len);

  EnvironmentalDamage info;
  if (!r.ReadGuid(info.guid)) return false;
  if (!r.ReadU8(info.type)) return false;
  if (!r.ReadU32(info.damage)) return false;

  if (!r.ReadU32(info.absorbed)) return false;
  if (!r.ReadU32(info.resisted)) return false;

  env_damage_ = info;
  return true;
}

net::wotlk::WorldPacket CombatManager::BuildAttackSwing(
    std::uint64_t target_guid) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_ATTACKSWING);
  pkt.AppendU64(target_guid);
  return pkt;
}

net::wotlk::WorldPacket CombatManager::BuildAttackStop() {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_ATTACKSTOP);
  return pkt;
}

void CombatManager::Clear() {
  in_combat_ = false;
  last_state_update_.reset();
  attack_start_.reset();
  attack_stop_.reset();
  last_health_update_.reset();
  last_power_update_.reset();
  threat_update_.reset();
  highest_threat_update_.reset();
  ai_reaction_.reset();
  xp_gain_.reset();
  level_up_.reset();
  env_damage_.reset();
  swing_error_ = AttackSwingError::kNone;
  swing_error_next_display_ms_ = 0;
  ThreatSystem::Get().Reset();
}

}

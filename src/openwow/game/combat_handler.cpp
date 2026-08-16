
#include "openwow/game/combat_handler.h"
#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::game {

using openwow::diagnostics::Log;
using openwow::diagnostics::LogLevel;

bool CombatHandler::HandleBreakTarget(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid guid{0};
  if (!r.ReadPackedGuid(guid)) return false;
  last_break_target_guid_ = guid.GetRawValue();
  return true;
}

bool CombatHandler::HandleClearTarget(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(last_clear_target_guid_)) return false;
  return true;
}

bool CombatHandler::HandleForceDisplayUpdate(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid guid{0};
  if (!r.ReadPackedGuid(guid)) return false;
  last_force_display_update_guid_ = guid.GetRawValue();
  return true;
}

bool CombatHandler::HandleResurrectFailed(const std::uint8_t* ,
                                           std::size_t ) {

  last_resurrect_failed_ = 1u;
  return true;
}

bool CombatHandler::ParseSpiritHealerConfirm(const std::uint8_t* data,
                                             std::size_t len,
                                             ObjectGuid& healer) const {
  PacketReader r(data, len);
  return r.ReadGuid(healer);
}

bool CombatHandler::HandleAreaSpiritHealerTime(const std::uint8_t* data,
                                                std::size_t len) {
  PacketReader r(data, len);
  AreaSpiritHealerTime info;
  std::uint32_t time_left_milliseconds = 0;
  if (!r.ReadGuid(info.healer)) return false;
  if (!r.ReadU32(time_left_milliseconds)) return false;
  info.time_left = std::chrono::milliseconds(time_left_milliseconds);
  last_area_spirit_healer_time_ = info;
  return true;
}

bool CombatHandler::HandleDestructibleBuildingDamage(const std::uint8_t* data,
                                                      std::size_t len) {
  PacketReader r(data, len);
  DestructibleBuildingDamage info;

  ObjectGuid target{0};
  if (!r.ReadPackedGuid(target)) return false;
  info.target_guid = target.GetRawValue();

  ObjectGuid caster{0};
  if (!r.ReadPackedGuid(caster)) return false;
  info.caster_guid = caster.GetRawValue();

  ObjectGuid owner{0};
  if (!r.ReadPackedGuid(owner)) return false;
  info.owner_guid = owner.GetRawValue();

  if (!r.ReadI32(info.damage)) return false;
  if (!r.ReadU32(info.spell_id)) return false;

  last_building_damage_ = info;
  return true;
}

bool CombatHandler::HandleCombatEventFailed() {
  combat_event_failed_ = true;
  return true;
}

bool CombatHandler::HandleProcResist(const std::uint8_t* data,
                                     std::size_t len) {
  PacketReader r(data, len);
  return HandleProcResist(r);
}

bool CombatHandler::HandleProcResist(PacketReader& r) {
  ProcResist info;
  if (!r.ReadU64(info.caster)) return false;
  if (!r.ReadU64(info.target)) return false;
  if (!r.ReadU32(info.spell_id)) return false;
  if (!r.ReadU8(info.is_debug)) return false;
  last_proc_resist_ = info;
  return true;
}

bool CombatHandler::HandleSpellBreakLog(const std::uint8_t* data,
                                        std::size_t len) {
  PacketReader r(data, len);
  return HandleSpellBreakLog(r);
}

bool CombatHandler::HandleSpellBreakLog(PacketReader& r) {
  SpellBreakLog info;

  ObjectGuid victim{0};
  if (!r.ReadPackedGuid(victim)) return false;
  info.victim = victim.GetRawValue();

  ObjectGuid caster{0};
  if (!r.ReadPackedGuid(caster)) return false;
  info.caster = caster.GetRawValue();

  if (!r.ReadU32(info.breaking_spell_id)) return false;

  std::uint8_t unused = 0;
  if (!r.ReadU8(unused)) return false;

  std::uint32_t count;
  if (!r.ReadU32(count)) return false;

  info.broken_auras.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    BrokenAuraEntry broken_aura;
    std::uint8_t aura_type_flag = 0;
    if (!r.ReadU32(broken_aura.spell_id)) return false;
    if (!r.ReadU8(aura_type_flag)) return false;
    broken_aura.is_debuff = aura_type_flag != 0;
    info.broken_auras.push_back(broken_aura);
  }

  last_spell_break_log_ = std::move(info);
  return true;
}

bool CombatHandler::HandleAuraCastLog(const std::uint8_t* data,
                                      std::size_t len) {
  PacketReader r(data, len);
  AuraCastLog info;
  if (!r.ReadU64(info.caster)) return false;
  if (!r.ReadU64(info.target)) return false;
  if (!r.ReadU32(info.spell_id)) return false;

  std::uint32_t count;
  if (!r.ReadU32(count)) return false;

  info.triggered_spells.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    std::uint32_t spell;
    if (!r.ReadU32(spell)) return false;
    info.triggered_spells.push_back(spell);
  }

  last_aura_cast_log_ = std::move(info);
  return true;
}

bool CombatHandler::HandleResetRangedCombatTimer(const std::uint8_t* data,
                                                  std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(ranged_timer_ms_)) return false;
  return true;
}

bool CombatHandler::HandleSetProjectilePosition(const std::uint8_t* data,
                                                 std::size_t len) {
  PacketReader r(data, len);
  ProjectilePosition info;
  if (!r.ReadU64(info.caster)) return false;
  if (!r.ReadU8(info.cast_id)) return false;
  if (!r.ReadFloat(info.x)) return false;
  if (!r.ReadFloat(info.y)) return false;
  if (!r.ReadFloat(info.z)) return false;
  last_projectile_position_ = info;
  return true;
}

void CombatHandler::Clear() {
  last_break_target_guid_ = 0;
  last_clear_target_guid_ = 0;
  last_force_display_update_guid_ = 0;
  last_resurrect_failed_ = 0;
  last_spirit_healer_guid_ = {};
  last_area_spirit_healer_time_.reset();
  last_building_damage_.reset();

  combat_event_failed_ = false;
  last_proc_resist_.reset();
  last_spell_break_log_.reset();
  last_aura_cast_log_.reset();
  ranged_timer_ms_ = 0;
  last_projectile_position_.reset();
}

}

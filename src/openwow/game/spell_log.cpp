
#include "openwow/game/spell_log.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/chat_message_formatters.h"
#include "openwow/game/combat_log_display.h"
#include "openwow/game/world_session.h"

namespace openwow::game {

namespace {

bool ConsumeSpellLogExecuteRecord(WorldSession& session, PacketReader& reader,
                                  const std::uint32_t effect_type,
                                  const std::uint32_t spell_id,
                                  const ObjectGuid& caster_guid,
                                  std::vector<SpellLogExecuteResurrect>&
                                      execute_resurrects,
                                  std::vector<SpellLogExecuteDrain>&
                                      execute_drains,
                                  std::vector<SpellLogExecuteExtraAttacks>&
                                      execute_extra_attacks,
                                  std::vector<SpellLogExecuteInterrupt>&
                                      execute_interrupts,
                                  std::vector<SpellLogExecuteSummon>&
                                      execute_summons,
                                  std::vector<SpellLogExecuteDurabilityDamage>&
                                      execute_durability_damages,
                                  std::vector<SpellLogExecuteDurabilityDamageAll>&
                                      execute_durability_damage_alls) {
  ObjectGuid target_guid;
  std::uint32_t value0 = 0;
  std::uint32_t value1 = 0;
  float float_value = 0.0f;

  switch (effect_type) {
    case 8:
    case 62:
      if (!reader.ReadPackedGuid(target_guid) || !reader.ReadU32(value0) ||
          !reader.ReadU32(value1) || !reader.ReadFloat(float_value)) {
        return false;
      }
      execute_drains.push_back({
          caster_guid.GetRawValue(),
          target_guid.GetRawValue(),
          spell_id,
          value1,
          value0,
          float_value,
          false,
      });
      return true;

    case 18:
    case 113:
      if (!reader.ReadPackedGuid(target_guid)) {
        return false;
      }
      execute_resurrects.push_back({
          caster_guid.GetRawValue(),
          target_guid.GetRawValue(),
          spell_id,
      });
      return true;

    case 28:
    case 50:
    case 56:
    case 76:
    case 81:
    case 83:
    case 68:
    case 104:
    case 105:
    case 106:
    case 107:
      if (!reader.ReadPackedGuid(target_guid)) {
        return false;
      }
      execute_summons.push_back({
          caster_guid.GetRawValue(), target_guid.GetRawValue(), spell_id});
      return true;

    case 19:
      if (!reader.ReadPackedGuid(target_guid) || !reader.ReadU32(value0)) {
        return false;
      }
      execute_extra_attacks.push_back({
          caster_guid.GetRawValue(), target_guid.GetRawValue(), spell_id, value0});
      return true;

    case 44:
      if (!reader.ReadPackedGuid(target_guid) || !reader.ReadU32(value0)) {
        return false;
      }
      execute_interrupts.push_back({
          caster_guid.GetRawValue(), target_guid.GetRawValue(), spell_id, value0});
      return true;

    case 24:
    case 59:
    case 157:
    case 101:
      if (!reader.ReadU32(value0)) {
        return false;
      }
      if (effect_type == 101) {
        FormatFeedPetLog(session, caster_guid.GetRawValue(), static_cast<int>(value0));
      } else {
        FormatTradeskillLog(session, caster_guid.GetRawValue(), static_cast<int>(value0));
      }
      return true;

    case 33:

      return reader.ReadPackedGuid(target_guid);

    case 102:

      return reader.ReadPackedGuid(target_guid);

    case 111: {
      if (!reader.ReadPackedGuid(target_guid) || !reader.ReadU32(value0) ||
          !reader.ReadU32(value1)) {
        return false;
      }
      if (value0 != 0xFFFFFFFFu) {
        execute_durability_damages.push_back({
            caster_guid.GetRawValue(),
            target_guid.GetRawValue(),
            value0,
            spell_id,
        });
      } else {
        execute_durability_damage_alls.push_back({
            caster_guid.GetRawValue(),
            target_guid.GetRawValue(),
            spell_id,
        });
      }
      return true;
    }

    default:
      return true;
  }
}

}

bool SpellLogHandler::ParseDispelOrSteal(PacketReader& r, const bool is_steal) {
  SpellDispelLog log;
  log.is_steal = is_steal;

  bool overrun = !r.ReadPackedGuid(log.victim);
  if (!overrun && !r.ReadPackedGuid(log.caster)) overrun = true;
  if (!overrun && !r.ReadU32(log.spell_id)) overrun = true;
  std::uint8_t unused = 0;
  if (!overrun && !r.ReadU8(unused)) overrun = true;
  std::uint32_t count = 0;
  if (!overrun && !r.ReadU32(count)) overrun = true;
  log.entries.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    DispelEntry e;
    if (!overrun && !r.ReadU32(e.spell_id)) overrun = true;
    if (!overrun && !r.ReadU8(e.is_cleansed)) overrun = true;
    log.entries.push_back(e);
  }

  dispel_logs_.clear();
  dispel_logs_.push_back(std::move(log));
  return true;
}

bool SpellLogHandler::HandleSpellDispelLog(const std::uint8_t* data,
                                           std::size_t len) {
  PacketReader r(data, len);
  return HandleSpellDispelLog(r);
}

bool SpellLogHandler::HandleSpellDispelLog(PacketReader& r) {
  return ParseDispelOrSteal(r, false);
}

bool SpellLogHandler::HandleSpellStealLog(const std::uint8_t* data,
                                          std::size_t len) {
  PacketReader r(data, len);
  return HandleSpellStealLog(r);
}

bool SpellLogHandler::HandleSpellStealLog(PacketReader& r) {
  return ParseDispelOrSteal(r, true);
}

bool SpellLogHandler::HandleSpellDamageShield(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);
  return HandleSpellDamageShield(r);
}

bool SpellLogHandler::HandleSpellDamageShield(PacketReader& r) {
  last_damage_shield_ = {};
  if (!r.ReadU64(last_damage_shield_.victim_guid)) return false;
  if (!r.ReadU64(last_damage_shield_.attacker_guid)) return false;
  if (!r.ReadU32(last_damage_shield_.spell_id)) return false;
  if (!r.ReadU32(last_damage_shield_.damage)) return false;
  if (!r.ReadU32(last_damage_shield_.absorb_amount)) return false;
  if (!r.ReadU32(last_damage_shield_.resist_amount)) return false;
  DispatchWoundEvent(ObjectGuid(last_damage_shield_.attacker_guid),
                     static_cast<int>(last_damage_shield_.damage),
                     static_cast<int>(last_damage_shield_.resist_amount),
                     0);
  return true;
}

bool SpellLogHandler::HandleSpellLogMiss(const std::uint8_t* data,
                                         std::size_t len) {
  PacketReader r(data, len);
  return HandleSpellLogMiss(r);
}

bool SpellLogHandler::HandleSpellLogMiss(PacketReader& r) {
  if (!r.ReadU32(last_log_miss_.spell_id)) return false;
  if (!r.ReadU64(last_log_miss_.caster_guid)) return false;
  if (!r.ReadU8(last_log_miss_.unknown)) return false;
  std::uint32_t count = 0;
  if (!r.ReadU32(count)) return false;
  last_log_miss_.targets.clear();
  last_log_miss_.targets.reserve(count);
  const auto* spell =
      dbc_ != nullptr ? dbc_->spell().LookupEntry(last_log_miss_.spell_id) : nullptr;

  const bool should_dispatch_unit_combat =
      last_log_miss_.unknown == 0 && spell != nullptr &&
      (spell->attributes & 0x100u) == 0;
  last_log_miss_.allow_client_miss_feedback = should_dispatch_unit_combat;
  last_log_miss_.armor_resistance_mask =
      should_dispatch_unit_combat ? openwow::data::DBClient_GetArmorResistanceMask(dbc_) : 0;
  const bool has_reflect_info = (last_log_miss_.unknown != 0);
  for (std::uint32_t i = 0; i < count; ++i) {
    SpellMissTarget t;
    if (!r.ReadU64(t.target_guid)) return false;
    if (!r.ReadU8(t.miss_info)) return false;
    if (has_reflect_info) {
      if (!r.ReadFloat(t.reflect_info_1)) return false;
      if (!r.ReadFloat(t.reflect_info_2)) return false;
    }
    last_log_miss_.targets.push_back(t);
    if (should_dispatch_unit_combat) {
      DispatchSpellMissUnitCombatEvent(
          ObjectGuid(t.target_guid), t.miss_info, last_log_miss_.armor_resistance_mask);
    }
  }
  return true;
}

bool SpellLogHandler::HandleSpellInstaKillLog(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);
  return HandleSpellInstaKillLog(r);
}

bool SpellLogHandler::HandleSpellInstaKillLog(PacketReader& r) {
  last_instakill_ = {};
  if (!r.ReadU64(last_instakill_.caster_guid)) return false;
  if (!r.ReadU64(last_instakill_.target_guid)) return false;
  if (!r.ReadU32(last_instakill_.spell_id)) return false;
  return true;
}

bool SpellLogHandler::HandleSpellOrDamageImmune(const std::uint8_t* data,
                                                std::size_t len) {
  PacketReader r(data, len);
  return HandleSpellOrDamageImmune(r);
}

bool SpellLogHandler::HandleSpellOrDamageImmune(PacketReader& r) {
  last_immune_ = {};
  if (!r.ReadU64(last_immune_.caster_guid)) return false;
  if (!r.ReadU64(last_immune_.target_guid)) return false;
  if (!r.ReadU32(last_immune_.spell_id)) return false;
  if (!r.ReadU8(last_immune_.is_periodic)) return false;
  return true;
}

bool SpellLogHandler::HandleDispelFailed(const std::uint8_t* data,
                                         std::size_t len) {
  PacketReader r(data, len);
  return HandleDispelFailed(r);
}

bool SpellLogHandler::HandleDispelFailed(PacketReader& r) {
  last_dispel_failed_ = {};
  if (!r.ReadU64(last_dispel_failed_.caster_guid)) return false;
  if (!r.ReadU64(last_dispel_failed_.victim_guid)) return false;
  if (!r.ReadU32(last_dispel_failed_.spell_id)) return false;
  last_dispel_failed_.failed_spells.clear();
  while (r.Remaining() >= 4) {
    std::uint32_t failed_spell = 0;
    if (!r.ReadU32(failed_spell)) break;
    last_dispel_failed_.failed_spells.push_back(failed_spell);
  }
  return true;
}

bool SpellLogHandler::HandleModifyCooldown(const std::uint8_t* data,
                                           std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(last_modify_cooldown_.spell_id)) return false;
  if (!r.ReadU64(last_modify_cooldown_.player_guid)) return false;
  if (!r.ReadI32(last_modify_cooldown_.cooldown_delta_ms)) return false;
  return true;
}

bool SpellLogHandler::HandleSpellLogExecute(WorldSession& session,
                                            const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  return HandleSpellLogExecute(session, r);
}

bool SpellLogHandler::HandleSpellLogExecute(WorldSession& session, PacketReader& r) {
  ObjectGuid caster{ObjectGuid(0)};
  if (!r.ReadPackedGuid(caster)) return false;
  if (!r.ReadU32(last_log_execute_spell_)) return false;
  last_execute_resurrects_.clear();
  last_execute_drains_.clear();
  last_execute_extra_attacks_.clear();
  last_execute_interrupts_.clear();
  last_execute_summons_.clear();
  last_execute_durability_damages_.clear();
  last_execute_durability_damage_alls_.clear();
  std::uint32_t effect_count = 0;
  if (!r.ReadU32(effect_count)) return false;

  for (std::uint32_t effect_index = 0; effect_index < effect_count; ++effect_index) {
    std::uint32_t effect_type = 0;
    std::uint32_t record_count = 0;
    if (!r.ReadU32(effect_type)) return false;
    if (!r.ReadU32(record_count)) return false;

    for (std::uint32_t record_index = 0; record_index < record_count; ++record_index) {
      if (!ConsumeSpellLogExecuteRecord(
              session, r,
              effect_type,
              last_log_execute_spell_,
              caster,
              last_execute_resurrects_,
              last_execute_drains_,
              last_execute_extra_attacks_,
              last_execute_interrupts_,
              last_execute_summons_,
              last_execute_durability_damages_,
              last_execute_durability_damage_alls_)) {
        return false;
      }
    }
  }

  return true;
}

void SpellLogHandler::Clear() {
  dispel_logs_.clear();
  last_damage_shield_ = {};
  last_log_miss_ = {};
  last_instakill_ = {};
  last_immune_ = {};
  last_dispel_failed_ = {};
  last_modify_cooldown_ = {};
  last_execute_resurrects_.clear();
  last_execute_drains_.clear();
  last_execute_extra_attacks_.clear();
  last_execute_interrupts_.clear();
  last_execute_summons_.clear();
  last_execute_durability_damages_.clear();
  last_execute_durability_damage_alls_.clear();
  last_log_execute_spell_ = 0;
}

}

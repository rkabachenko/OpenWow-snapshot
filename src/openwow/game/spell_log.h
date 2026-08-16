
#pragma once

#include <cstdint>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class WorldSession;

struct DispelEntry {
  std::uint32_t spell_id = 0;
  std::uint8_t is_cleansed = 0;
};

struct SpellDispelLog {
  ObjectGuid victim{ObjectGuid(0)};
  ObjectGuid caster{ObjectGuid(0)};
  std::uint32_t spell_id = 0;
  std::vector<DispelEntry> entries;
  bool is_steal = false;
};

struct SpellDamageShield {
  std::uint64_t victim_guid = 0;
  std::uint64_t attacker_guid = 0;
  std::uint32_t spell_id = 0;
  std::uint32_t damage = 0;
  std::uint32_t absorb_amount = 0;
  std::uint32_t resist_amount = 0;
};

struct SpellMissTarget {
  std::uint64_t target_guid = 0;
  std::uint8_t miss_info = 0;

  float reflect_info_1 = 0.0f;
  float reflect_info_2 = 0.0f;
};

struct SpellLogMiss {
  std::uint32_t spell_id = 0;
  std::uint64_t caster_guid = 0;
  std::uint8_t unknown = 0;

  bool allow_client_miss_feedback = false;
  std::uint32_t armor_resistance_mask = 0;
  std::vector<SpellMissTarget> targets;
};

struct SpellInstaKillLog {
  std::uint64_t caster_guid = 0;
  std::uint64_t target_guid = 0;
  std::uint32_t spell_id = 0;
};

struct SpellOrDamageImmune {
  std::uint64_t caster_guid = 0;
  std::uint64_t target_guid = 0;
  std::uint32_t spell_id = 0;
  std::uint8_t is_periodic = 0;
};

struct DispelFailed {
  std::uint64_t caster_guid = 0;
  std::uint64_t victim_guid = 0;
  std::uint32_t spell_id = 0;
  std::vector<std::uint32_t> failed_spells;
};

struct ModifyCooldown {
  std::uint32_t spell_id = 0;
  std::uint64_t player_guid = 0;
  std::int32_t cooldown_delta_ms = 0;
};

struct SpellLogExecuteResurrect {
  std::uint64_t caster_guid = 0;
  std::uint64_t target_guid = 0;
  std::uint32_t spell_id = 0;
};

struct SpellLogExecuteDrain {
  std::uint64_t caster_guid = 0;
  std::uint64_t target_guid = 0;
  std::uint32_t spell_id = 0;
  std::uint32_t power_type = 0;
  std::uint32_t drain_amount = 0;
  float leech_coefficient = 0.0f;
  bool is_periodic = false;
};

struct SpellLogExecuteExtraAttacks {
  std::uint64_t caster_guid = 0;
  std::uint64_t target_guid = 0;
  std::uint32_t spell_id = 0;
  std::uint32_t amount = 0;
};

struct SpellLogExecuteInterrupt {
  std::uint64_t caster_guid = 0;
  std::uint64_t target_guid = 0;
  std::uint32_t spell_id = 0;
  std::uint32_t extra_spell_id = 0;
};

struct SpellLogExecuteSummon {
  std::uint64_t caster_guid = 0;
  std::uint64_t target_guid = 0;
  std::uint32_t spell_id = 0;
};

struct SpellLogExecuteDurabilityDamage {
  std::uint64_t caster_guid = 0;
  std::uint64_t target_guid = 0;
  std::uint32_t damage_spell_id = 0;
  std::uint32_t trigger_spell_id = 0;
};

struct SpellLogExecuteDurabilityDamageAll {
  std::uint64_t caster_guid = 0;
  std::uint64_t target_guid = 0;
  std::uint32_t spell_id = 0;
};

class SpellLogHandler {
 public:
  void SetDbcLoader(const openwow::data::dbc::DbcLoader* dbc) {
    dbc_ = dbc;
  }

  bool HandleSpellDispelLog(const std::uint8_t* data, std::size_t len);
  bool HandleSpellDispelLog(PacketReader& reader);
  bool HandleSpellStealLog(const std::uint8_t* data, std::size_t len);
  bool HandleSpellStealLog(PacketReader& reader);
  bool HandleSpellDamageShield(const std::uint8_t* data, std::size_t len);
  bool HandleSpellDamageShield(PacketReader& reader);
  bool HandleSpellLogMiss(const std::uint8_t* data, std::size_t len);
  bool HandleSpellLogMiss(PacketReader& reader);
  bool HandleSpellInstaKillLog(const std::uint8_t* data, std::size_t len);
  bool HandleSpellInstaKillLog(PacketReader& reader);
  bool HandleSpellOrDamageImmune(const std::uint8_t* data, std::size_t len);
  bool HandleSpellOrDamageImmune(PacketReader& reader);
  bool HandleDispelFailed(const std::uint8_t* data, std::size_t len);
  bool HandleDispelFailed(PacketReader& reader);
  bool HandleModifyCooldown(const std::uint8_t* data, std::size_t len);

  bool HandleSpellLogExecute(WorldSession& session, const std::uint8_t* data,
                             std::size_t len);
  bool HandleSpellLogExecute(WorldSession& session, PacketReader& reader);

  const std::vector<SpellDispelLog>& dispel_logs() const { return dispel_logs_; }
  const SpellDamageShield& last_damage_shield() const { return last_damage_shield_; }
  const SpellLogMiss& last_log_miss() const { return last_log_miss_; }
  const SpellInstaKillLog& last_instakill() const { return last_instakill_; }
  const SpellOrDamageImmune& last_immune() const { return last_immune_; }
  const DispelFailed& last_dispel_failed() const { return last_dispel_failed_; }
  const ModifyCooldown& last_modify_cooldown() const { return last_modify_cooldown_; }
  const std::vector<SpellLogExecuteResurrect>& last_execute_resurrects() const {
    return last_execute_resurrects_;
  }
  const std::vector<SpellLogExecuteDrain>& last_execute_drains() const {
    return last_execute_drains_;
  }
  const std::vector<SpellLogExecuteExtraAttacks>&
  last_execute_extra_attacks() const {
    return last_execute_extra_attacks_;
  }
  const std::vector<SpellLogExecuteInterrupt>& last_execute_interrupts() const {
    return last_execute_interrupts_;
  }
  const std::vector<SpellLogExecuteSummon>& last_execute_summons() const {
    return last_execute_summons_;
  }
  const std::vector<SpellLogExecuteDurabilityDamage>& last_execute_durability_damages() const {
    return last_execute_durability_damages_;
  }
  const std::vector<SpellLogExecuteDurabilityDamageAll>& last_execute_durability_damage_alls() const {
    return last_execute_durability_damage_alls_;
  }
  std::uint32_t last_log_execute_spell() const { return last_log_execute_spell_; }

  void Clear();

 private:
  bool ParseDispelOrSteal(PacketReader& reader, bool is_steal);

  std::vector<SpellDispelLog> dispel_logs_;
  SpellDamageShield last_damage_shield_{};
  SpellLogMiss last_log_miss_{};
  SpellInstaKillLog last_instakill_{};
  SpellOrDamageImmune last_immune_{};
  DispelFailed last_dispel_failed_{};
  ModifyCooldown last_modify_cooldown_{};
  std::vector<SpellLogExecuteResurrect> last_execute_resurrects_;
  std::vector<SpellLogExecuteDrain> last_execute_drains_;
  std::vector<SpellLogExecuteExtraAttacks> last_execute_extra_attacks_;
  std::vector<SpellLogExecuteInterrupt> last_execute_interrupts_;
  std::vector<SpellLogExecuteSummon> last_execute_summons_;
  std::vector<SpellLogExecuteDurabilityDamage> last_execute_durability_damages_;
  std::vector<SpellLogExecuteDurabilityDamageAll> last_execute_durability_damage_alls_;
  std::uint32_t last_log_execute_spell_ = 0;
  const openwow::data::dbc::DbcLoader* dbc_ = nullptr;
};

}

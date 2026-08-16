
#include "openwow/game/proc_manager.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

#include <algorithm>
#include <cstdlib>

namespace openwow::game {

ProcManager& ProcManager::Get() {
  static ProcManager instance;
  return instance;
}

void ProcManager::RegisterSpellProc(
    const std::uint64_t unit_guid,
    const ProcTriggerDescriptor& descriptor) {
  auto& procs = spell_procs_[unit_guid];

  auto it = std::find_if(procs.begin(), procs.end(),
      [&](const ProcTriggerDescriptor& d) {
        return d.source_spell_id == descriptor.source_spell_id &&
               d.source == descriptor.source;
      });
  if (it != procs.end()) {
    *it = descriptor;
  } else {
    procs.push_back(descriptor);
  }
}

void ProcManager::RegisterEnchantmentProc(
    const std::uint64_t unit_guid,
    const EnchantmentProcState& proc) {
  auto& procs = enchantment_procs_[unit_guid];

  auto it = std::find_if(procs.begin(), procs.end(),
      [&](const EnchantmentProcState& p) {
        return p.enchantment_id == proc.enchantment_id &&
               p.item_guid == proc.item_guid;
      });
  if (it != procs.end()) {
    *it = proc;
  } else {
    procs.push_back(proc);
  }
}

void ProcManager::RegisterSetBonus(
    const std::uint64_t unit_guid,
    const SetBonusState& bonus) {
  auto& bonuses = set_bonuses_[unit_guid];

  auto it = std::find_if(bonuses.begin(), bonuses.end(),
      [&](const SetBonusState& b) {
        return b.spell_id == bonus.spell_id;
      });
  if (it != bonuses.end()) {
    *it = bonus;
  } else {
    bonuses.push_back(bonus);
  }
}

void ProcManager::RegisterTrinketEffect(
    const std::uint64_t unit_guid,
    const TrinketEffectState& trinket) {
  auto& trinkets = trinket_effects_[unit_guid];

  auto it = std::find_if(trinkets.begin(), trinkets.end(),
      [&](const TrinketEffectState& t) {
        return t.item_guid == trinket.item_guid;
      });
  if (it != trinkets.end()) {
    *it = trinket;
  } else {
    trinkets.push_back(trinket);
  }
}

void ProcManager::UnregisterSpellProcs(
    const std::uint64_t unit_guid,
    const std::uint32_t spell_id) {
  auto it = spell_procs_.find(unit_guid);
  if (it == spell_procs_.end()) {
    return;
  }

  auto& procs = it->second;
  procs.erase(std::remove_if(procs.begin(), procs.end(),
      [spell_id](const ProcTriggerDescriptor& d) {
        return d.source_spell_id == spell_id;
      }), procs.end());
}

void ProcManager::UnregisterEnchantmentProcs(
    const std::uint64_t unit_guid,
    const std::uint32_t enchantment_id) {
  auto it = enchantment_procs_.find(unit_guid);
  if (it == enchantment_procs_.end()) {
    return;
  }

  auto& procs = it->second;
  procs.erase(std::remove_if(procs.begin(), procs.end(),
      [enchantment_id](const EnchantmentProcState& p) {
        return p.enchantment_id == enchantment_id;
      }), procs.end());
}

void ProcManager::ClearUnitProcs(const std::uint64_t unit_guid) {
  spell_procs_.erase(unit_guid);
  enchantment_procs_.erase(unit_guid);
  set_bonuses_.erase(unit_guid);
  trinket_effects_.erase(unit_guid);
  internal_cooldowns_.erase(unit_guid);
}

bool ProcManager::RollProcChance(const std::uint32_t chance) {

  if (chance >= 100) {
    return true;
  }
  if (chance == 0) {
    return false;
  }
  return (static_cast<std::uint32_t>(std::rand() % 100u) < chance);
}

ProcTriggerFlag ProcManager::TypeToFlag(const ProcTriggerType type) {
  switch (type) {
    case ProcTriggerType::kOnHit:

      return static_cast<ProcTriggerFlag>(
          static_cast<std::uint32_t>(ProcTriggerFlag::kOnSuccessfulMeleeAttack) |
          static_cast<std::uint32_t>(ProcTriggerFlag::kOnSuccessfulRangedAttack) |
          static_cast<std::uint32_t>(ProcTriggerFlag::kOnSpellHitHarmful) |
          static_cast<std::uint32_t>(ProcTriggerFlag::kOnSpellHitHelpful));
    case ProcTriggerType::kOnCrit:
      return static_cast<ProcTriggerFlag>(
          static_cast<std::uint32_t>(ProcTriggerFlag::kOnMeleeAttackCrit) |
          static_cast<std::uint32_t>(ProcTriggerFlag::kOnRangedAttackCrit) |
          static_cast<std::uint32_t>(ProcTriggerFlag::kOnSpellCritHarmful) |
          static_cast<std::uint32_t>(ProcTriggerFlag::kOnSpellCritHelpful));
    case ProcTriggerType::kOnDodge:
      return ProcTriggerFlag::kOnDodge;
    case ProcTriggerType::kOnParry:
      return ProcTriggerFlag::kOnParry;
    case ProcTriggerType::kOnBlock:
      return ProcTriggerFlag::kOnBlock;
    case ProcTriggerType::kOnCast:
      return ProcTriggerFlag::kOnCastSpell;
    case ProcTriggerType::kOnHeal:
      return ProcTriggerFlag::kOnHealDone;
    case ProcTriggerType::kOnDamageTaken:
      return static_cast<ProcTriggerFlag>(
          static_cast<std::uint32_t>(ProcTriggerFlag::kOnTakeDamageFromAny) |
          static_cast<std::uint32_t>(ProcTriggerFlag::kOnTakeDamageFromMeleeAttack));
    case ProcTriggerType::kOnSpellMiss:
      return ProcTriggerFlag::kOnSpellMiss;
    case ProcTriggerType::kOnSpellResist:
      return ProcTriggerFlag::kOnSpellResist;
    default:
      return ProcTriggerFlag::kNone;
  }
}

std::uint32_t ProcManager::TypeToFlagMask(const ProcTriggerType type) {
  return static_cast<std::uint32_t>(TypeToFlag(type));
}

bool ProcManager::MatchesTriggerFlags(
    const ProcTriggerFlag trigger_flags,
    const ProcTriggerType type,
    const bool is_crit) {
  const auto flags_raw = static_cast<std::uint32_t>(trigger_flags);

  if (is_crit) {

    const auto crit_mask = static_cast<std::uint32_t>(
        ProcTriggerFlag::kOnMeleeAttackCrit |
        ProcTriggerFlag::kOnRangedAttackCrit |
        ProcTriggerFlag::kOnSpellCritHarmful |
        ProcTriggerFlag::kOnSpellCritHelpful);
    const auto hit_mask = static_cast<std::uint32_t>(
        ProcTriggerFlag::kOnSuccessfulMeleeAttack |
        ProcTriggerFlag::kOnSuccessfulRangedAttack |
        ProcTriggerFlag::kOnSpellHitHarmful |
        ProcTriggerFlag::kOnSpellHitHelpful);
    return (flags_raw & (crit_mask | hit_mask)) != 0;
  }

  switch (type) {
    case ProcTriggerType::kOnHit:
      return (flags_raw & kProcFlagHitMask) != 0;
    case ProcTriggerType::kOnCrit:
      return (flags_raw & kProcFlagCritMask) != 0;
    case ProcTriggerType::kOnDodge:
      return (flags_raw & static_cast<std::uint32_t>(
          ProcTriggerFlag::kOnDodge)) != 0;
    case ProcTriggerType::kOnParry:
      return (flags_raw & static_cast<std::uint32_t>(
          ProcTriggerFlag::kOnParry)) != 0;
    case ProcTriggerType::kOnBlock:
      return (flags_raw & (static_cast<std::uint32_t>(
          ProcTriggerFlag::kOnBlock) |
          static_cast<std::uint32_t>(
          ProcTriggerFlag::kOnBlockVictim))) != 0;
    case ProcTriggerType::kOnCast:
      return (flags_raw & static_cast<std::uint32_t>(
          ProcTriggerFlag::kOnCastSpell)) != 0;
    case ProcTriggerType::kOnHeal:
      return (flags_raw & static_cast<std::uint32_t>(
          ProcTriggerFlag::kOnHealDone)) != 0;
    case ProcTriggerType::kOnDamageTaken:
      return (flags_raw & kProcFlagDefenseMask) != 0;
    default:
      return false;
  }
}

void ProcManager::StartInternalCooldown(
    const std::uint64_t unit_guid,
    const std::uint32_t spell_id,
    const std::uint32_t duration_ms) {
  auto& cooldowns = internal_cooldowns_[unit_guid];

  const auto now = core::GameClock::GetTickCount64();

  auto it = std::find_if(cooldowns.begin(), cooldowns.end(),
      [spell_id](const ProcCooldownEntry& e) {
        return e.spell_id == spell_id;
      });
  if (it != cooldowns.end()) {
    it->ready_time_ms = now + duration_ms;
    it->cooldown_ms = duration_ms;
  } else {
    ProcCooldownEntry entry;
    entry.spell_id = spell_id;
    entry.cooldown_ms = duration_ms;
    entry.ready_time_ms = now + duration_ms;
    cooldowns.push_back(entry);
  }
}

bool ProcManager::IsInternalCooldownReady(
    const std::uint64_t unit_guid,
    const std::uint32_t spell_id) const {
  const auto it = internal_cooldowns_.find(unit_guid);
  if (it == internal_cooldowns_.end()) {
    return true;
  }

  const auto now = core::GameClock::GetTickCount64();
  for (const auto& entry : it->second) {
    if (entry.spell_id == spell_id) {
      return now >= entry.ready_time_ms;
    }
  }
  return true;
}

std::uint32_t ProcManager::GetInternalCooldownRemaining(
    const std::uint64_t unit_guid,
    const std::uint32_t spell_id) const {
  const auto it = internal_cooldowns_.find(unit_guid);
  if (it == internal_cooldowns_.end()) {
    return 0;
  }

  const auto now = core::GameClock::GetTickCount64();
  for (const auto& entry : it->second) {
    if (entry.spell_id == spell_id) {
      if (now >= entry.ready_time_ms) {
        return 0;
      }
      return static_cast<std::uint32_t>(entry.ready_time_ms - now);
    }
  }
  return 0;
}

void ProcManager::UpdateSetBonusStates(
    const std::uint64_t unit_guid,
    const std::uint32_t set_id,
    const std::uint32_t items_equipped) {
  auto it = set_bonuses_.find(unit_guid);
  if (it == set_bonuses_.end()) {
    return;
  }

  for (auto& bonus : it->second) {
    if (bonus.set_id != set_id) {
      continue;
    }

    bonus.items_equipped = items_equipped;
    const bool should_be_active = items_equipped >= bonus.required_pieces;

    if (should_be_active && !bonus.active) {

      bonus.active = true;
    } else if (!should_be_active && bonus.active) {
      bonus.active = false;

    }
  }
}

bool ProcManager::IsSetBonusActive(
    const std::uint64_t unit_guid,
    const std::uint32_t spell_id) const {
  const auto it = set_bonuses_.find(unit_guid);
  if (it == set_bonuses_.end()) {
    return false;
  }

  for (const auto& bonus : it->second) {
    if (bonus.spell_id == spell_id && bonus.active) {
      return true;
    }
  }
  return false;
}

bool ProcManager::ActivateTrinketOnUse(
    const std::uint64_t unit_guid,
    const ObjectGuid& item_guid) {
  auto it = trinket_effects_.find(unit_guid);
  if (it == trinket_effects_.end()) {
    return false;
  }

  const auto now = core::GameClock::GetTickCount64();
  for (auto& trinket : it->second) {
    if (trinket.item_guid != item_guid || !trinket.on_use) {
      continue;
    }

    if (now < trinket.cooldown_ready_ms) {

      return false;
    }

    trinket.active = true;
    trinket.cooldown_ready_ms = now + trinket.cooldown_ms;

    return true;
  }
  return false;
}

bool ProcManager::IsTrinketEquipEffectActive(
    const std::uint64_t unit_guid,
    const ObjectGuid& item_guid) const {
  const auto it = trinket_effects_.find(unit_guid);
  if (it == trinket_effects_.end()) {
    return false;
  }

  for (const auto& trinket : it->second) {
    if (trinket.item_guid == item_guid && !trinket.on_use) {
      return trinket.active;
    }
  }
  return false;
}

void ProcManager::SetEnchantmentProc(
    const std::uint64_t unit_guid,
    const std::uint32_t enchantment_id,
    const std::uint32_t trigger_spell_id,
    const std::uint32_t proc_chance,
    const ProcTriggerFlag flags,
    const std::uint32_t icd_ms,
    const ObjectGuid& item_guid) {
  EnchantmentProcState proc;
  proc.enchantment_id = enchantment_id;
  proc.trigger_spell_id = trigger_spell_id;
  proc.proc_chance = proc_chance;
  proc.trigger_flags = flags;
  proc.icd_duration_ms = icd_ms;
  proc.icd_ready_ms = 0;
  proc.item_guid = item_guid;

  RegisterEnchantmentProc(unit_guid, proc);
}

void ProcManager::RemoveItemProcs(
    const std::uint64_t unit_guid,
    const ObjectGuid& item_guid) {

  auto enc_it = enchantment_procs_.find(unit_guid);
  if (enc_it != enchantment_procs_.end()) {
    auto& procs = enc_it->second;
    procs.erase(std::remove_if(procs.begin(), procs.end(),
        [&](const EnchantmentProcState& p) {
          return p.item_guid == item_guid;
        }), procs.end());
  }

  auto tri_it = trinket_effects_.find(unit_guid);
  if (tri_it != trinket_effects_.end()) {
    auto& trinkets = tri_it->second;
    trinkets.erase(std::remove_if(trinkets.begin(), trinkets.end(),
        [&](const TrinketEffectState& t) {
          return t.item_guid == item_guid;
        }), trinkets.end());
  }
}

bool ProcManager::FireProcs(
    const std::uint64_t unit_guid,
    const ProcTriggerType trigger,
    const std::uint32_t ,
    const std::uint32_t ,
    const std::uint64_t ) {
  bool any_proc_fired = false;
  const bool is_crit = (trigger == ProcTriggerType::kOnCrit);
  const auto now = core::GameClock::GetTickCount64();

  {
    const auto it = spell_procs_.find(unit_guid);
    if (it != spell_procs_.end()) {
      for (auto& proc : it->second) {

        if (proc.icd_duration_ms > 0 && now < proc.icd_ready_time_ms) {
          continue;
        }

        if (!MatchesTriggerFlags(proc.trigger_flags, trigger, is_crit)) {
          continue;
        }

        if (proc.triggered_spell_id == 0) {
          continue;
        }

        if (proc.proc_charges > 0) {
          --proc.proc_charges;
        }

        if (!RollProcChance(proc.proc_chance)) {
          continue;
        }

        if (proc.icd_duration_ms > 0) {
          proc.icd_ready_time_ms = now + proc.icd_duration_ms;
        }

        any_proc_fired = true;

        if (proc.proc_charges == 0) {

        }
      }
    }
  }

  {
    const auto it = enchantment_procs_.find(unit_guid);
    if (it != enchantment_procs_.end()) {
      for (auto& proc : it->second) {

        if (proc.icd_duration_ms > 0 && now < proc.icd_ready_ms) {
          continue;
        }

        if (!MatchesTriggerFlags(proc.trigger_flags, trigger, is_crit)) {
          continue;
        }

        if (!RollProcChance(proc.proc_chance)) {
          continue;
        }

        if (proc.icd_duration_ms > 0) {
          proc.icd_ready_ms = now + proc.icd_duration_ms;
        }

        any_proc_fired = true;
      }
    }
  }

  return any_proc_fired;
}

void ProcManager::Clear() {
  spell_procs_.clear();
  enchantment_procs_.clear();
  set_bonuses_.clear();
  trinket_effects_.clear();
  internal_cooldowns_.clear();
}

}


#include "openwow/game/unit_combat.h"

#include "openwow/game/combat_log.h"
#include "openwow/game/combat_log_internal.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/spell_failure_names.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace openwow::game {

void CGUnit_C::DisplayPowerGain(std::int32_t amount) const {
  if (amount == 0)
    return;

  if (!IsPlayer())
    return;

  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireCombatTextUpdate("ENERGIZE", amount, PowerTypeToString(State().GetPowerType()));
}

void CGUnit_C::DisplayHealing(std::int32_t amount) const {
  if (amount == 0)
    return;
  if (!IsPlayer())
    return;

  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireCombatTextUpdate("HEAL", amount);
}

void CGUnit_C::ShowMissTypeFCT(std::uint32_t miss_index, bool is_player_target, bool is_pet) const {
  (void)miss_index;
  (void)is_player_target;
  (void)is_pet;
}

int CGUnit_C::CombatLogUpdateHandler(WorldSession &session,
                                     std::int32_t opcode,
                                     const std::uint8_t *packet) {
  if (opcode != 330) {
    return 1;
  }

  if (packet == nullptr) {
    return 1;
  }

  SpellCombatLogData combat_data;
  std::size_t bytes_read = 0;
  if (!SpellCombatLog_ReadFromPacket(combat_data, packet,
                                      std::numeric_limits<std::size_t>::max(),
                                      bytes_read)) {
    return 1;
  }

  auto& combat_log = session.combat_log();

  const bool is_hit = (combat_data.hit_flags &
      static_cast<std::uint32_t>(HitInfo::kAffectsVictim)) != 0;

  const std::uint32_t event_index = is_hit ? 0 : 1;

  CombatLogEntry entry = CombatLog_CreateEntry(
      combat_data.attacker_guid,
      combat_data.victim_guid,
      event_index,
      combat_data.melee_spell_id);

  if (is_hit) {
    entry.amount    = static_cast<std::int32_t>(combat_data.total_damage);
    entry.overkill  = static_cast<std::int32_t>(combat_data.overkill);

    entry.resisted  = 0;
    entry.absorbed  = 0;
    for (int i = 0; i < 2; ++i) {
      entry.resisted += static_cast<std::int32_t>(combat_data.resisted[i]);
      entry.absorbed += static_cast<std::int32_t>(combat_data.absorbed[i]);
    }

    entry.blocked  = static_cast<std::int32_t>(combat_data.blocked);

    entry.critical = (combat_data.hit_flags &
        static_cast<std::uint32_t>(HitInfo::kCriticalHit)) != 0;
    entry.glancing = (combat_data.hit_flags &
        static_cast<std::uint32_t>(HitInfo::kGlancing)) != 0;

    entry.crushing = (combat_data.hit_flags &
        static_cast<std::uint32_t>(HitInfo::kCrushing)) != 0;

    entry.school = 1;

    (void)combat_data.rage_gained;
  } else {
    switch (combat_data.victim_state) {
      case 2:  entry.miss_type = "DODGE";   break;
      case 3:  entry.miss_type = "PARRY";   break;
      case 4:  entry.miss_type = "BLOCK";   break;
      case 5:  entry.miss_type = "EVADE";   break;
      case 6:  entry.miss_type = "IMMUNE";  break;
      case 7:  entry.miss_type = "DEFLECT"; break;
      default: entry.miss_type = "MISS";    break;
    }
  }

  CombatLog_FinalizeEntry(combat_log, entry);

  if (CombatLog_IsActivePlayerTarget(combat_data.attacker_guid) ||
      CombatLog_IsActivePlayerTarget(combat_data.victim_guid)) {

    std::uint32_t fct_index = CombatTextMsgIdx::kDamage;
    if (entry.critical) fct_index = CombatTextMsgIdx::kDamageCrit;

    if (!is_hit) {
      switch (combat_data.victim_state) {
        case 1:  fct_index = CombatTextMsgIdx::kMiss;   break;
        case 2:  fct_index = CombatTextMsgIdx::kDodge;  break;
        case 3:  fct_index = CombatTextMsgIdx::kParry;  break;
        case 4:  fct_index = CombatTextMsgIdx::kBlock;  break;
        case 5:  fct_index = CombatTextMsgIdx::kEvade;  break;
        case 6:  fct_index = CombatTextMsgIdx::kImmune; break;
        case 7:  fct_index = CombatTextMsgIdx::kDeflect;break;
        default: fct_index = CombatTextMsgIdx::kMiss;   break;
      }
    }

    const std::string victim_name = CombatLog_ResolveName(
        combat_data.victim_guid);

    if (entry.critical || is_hit) {
      CombatLog_FireCombatTextSSD(fct_index, victim_name.c_str(),
                                  static_cast<std::int32_t>(combat_data.total_damage));
    } else {
      CombatLog_FireCombatTextSS(fct_index, victim_name.c_str());
    }
  }

  return 1;
}

}

namespace openwow::game::unit_combat {

void* CGUnit_C_GetWeaponVisualForSlot(void* unit, int is_offhand,
                                      bool ensure_model_loaded) {

    (void)unit;
    (void)is_offhand;
    (void)ensure_model_loaded;
    return nullptr;
}

void UnitCombat_C_HandleAttackResultAnim(void* unit, int attackResult,
                                         const void* combatData) {

    (void)unit;
    (void)attackResult;
    (void)combatData;
}

void CombatText_ProcessAttackResult(void* attacker, int edx_unused,
                                    int* combatData) {

    (void)attacker;
    (void)edx_unused;
    (void)combatData;
}

AttackResultDisplayAction
DetermineAttackResultDisplay(const AttackResultContext& ctx) {
    AttackResultDisplayAction action{};

    if (ctx.extra_attacks != 0 &&
        ctx.result_type != AttackResultType::kWound) {
        return action;
    }
    action.should_process = true;

    switch (ctx.result_type) {
    case AttackResultType::kDeflect:
        action.show_miss_text  = true;
        action.miss_display_id = kCombatTextDeflect;
        return action;

    case AttackResultType::kParry:
        action.show_miss_text  = true;
        action.miss_display_id = kCombatTextParry;
        return action;

    case AttackResultType::kEvade:
        action.show_miss_text  = true;
        action.miss_display_id = kCombatTextEvade;
        return action;

    case AttackResultType::kDodge:
        action.play_miss_sound = true;
        action.show_miss_text  = true;
        action.miss_display_id = kCombatTextDodge;
        return action;

    case AttackResultType::kBlock:
        action.show_miss_text  = true;
        action.miss_display_id = kCombatTextBlock;
        return action;

    case AttackResultType::kImmune:
        action.show_miss_text  = true;
        action.miss_display_id = kCombatTextImmune;
        return action;

    default:
        break;
    }

    const bool has_damage =
        ctx.damage != 0 ||
        (ctx.hit_flags & AttackHitFlags::kEnvironmental) != 0;

    if (has_damage) {
        action.play_weapon_impact_sound = true;
        action.show_damage_number       = true;
        action.is_crit =
            (ctx.hit_flags & AttackHitFlags::kCriticalHit) != 0;
    } else {
        if (ctx.active_player_guid != ctx.victim_guid &&
            (ctx.hit_flags & AttackHitFlags::kGlancing) == 0) {
            action.show_miss_text = true;
            if (ctx.hit_flags & AttackHitFlags::kFullAbsorb) {
                action.miss_display_id = kCombatTextAbsorb;
            } else if (ctx.hit_flags & AttackHitFlags::kFullResist) {
                action.miss_display_id = kCombatTextResist;
            } else {
                action.miss_display_id = kCombatTextMiss;
            }
        }
        action.play_miss_sound = true;
    }

    return action;
}

void UnitCombat_ProcessTargetHit(void* unit, int* combatData) {

    (void)unit;
    (void)combatData;
}

void UnitCombat_CreateDeathCameraShake(void* unit) {

    (void)unit;
}

std::optional<float> ComputeWeaponTrailAnimSpeed(
    const AnimationPlaybackState& base_state,
    std::uint32_t weapon_anim_duration_ms) {
    if (base_state.flags != 0) {
        return std::nullopt;
    }

    std::int32_t elapsed = 0;
    if (base_state.playback_speed != 0.0f) {
        elapsed = static_cast<std::int32_t>(
            static_cast<double>(base_state.current_time) /
            static_cast<double>(base_state.playback_speed));
    }

    const std::int32_t remaining =
        static_cast<std::int32_t>(base_state.end_time) - elapsed -
        static_cast<std::int32_t>(base_state.start_time);

    if (remaining <= 0) {
        return std::nullopt;
    }

    return static_cast<float>(
        static_cast<double>(weapon_anim_duration_ms) /
        static_cast<double>(remaining));
}

void UnitCombat_ProcessPendingCombatResult(void* unit, int edx_unused) {

    (void)unit;
    (void)edx_unused;
}

void UnitCombat_HandleAnimEvent(void* unit, std::uint32_t fourCC,
                                int param, const float* position, int flags) {
    auto* const creature = static_cast<CGUnit_C*>(unit);
    if (creature == nullptr) {
        return;
    }

    if (fourCC == kFourCC_DTH) {
        UnitSound_PlayDeathThud(*creature);
        UnitCombat_CreateDeathCameraShake(creature);
        creature->SpellVisuals().CreateFromCreatureInfo();
        return;
    }

    (void)fourCC;
    (void)param;
    (void)position;
    (void)flags;
}

void UnitCombat_ClearAndFace(void* unit, std::uint64_t attackerGUID,
                             int attackerPresent) {

    (void)unit;
    (void)attackerGUID;
    (void)attackerPresent;
}

int CombatLog_HandleAttackOpcodes(const char* edx_unused, int param2,
                                  int opcode, int param4,
                                  void* packetData) {

    (void)edx_unused;
    (void)param2;
    (void)opcode;
    (void)param4;
    (void)packetData;
    return 0;
}

int CombatLog_Initialize() {

    return 0;
}

int CGUnit_CheckCantEmoteFlag(const void* unit) {

    auto fields = static_cast<const std::uint32_t*>(unit);
    return static_cast<int>(fields[22] & 0x4000000u);
}

}


#include "openwow/game/spell_target_validation.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/interaction_range.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/world_session.h"

#include <algorithm>

namespace openwow::game {

const char* SpellTargetResultToString(SpellTargetResult r) {
  switch (r) {
    case SpellTargetResult::kValid:             return "VALID";
    case SpellTargetResult::kInvalidTarget:     return "INVALID_TARGET";
    case SpellTargetResult::kTargetIsDead:      return "TARGET_IS_DEAD";
    case SpellTargetResult::kTargetIsAlive:     return "TARGET_IS_ALIVE";
    case SpellTargetResult::kTargetIsFriendly:  return "TARGET_IS_FRIENDLY";
    case SpellTargetResult::kTargetIsHostile:   return "TARGET_IS_HOSTILE";
    case SpellTargetResult::kTargetNotInParty:  return "TARGET_NOT_IN_PARTY";
    case SpellTargetResult::kTargetNotInRaid:   return "TARGET_NOT_IN_RAID";
    case SpellTargetResult::kTargetNotPlayer:   return "TARGET_NOT_PLAYER";
    case SpellTargetResult::kTargetNotNpc:      return "TARGET_NOT_NPC";
    case SpellTargetResult::kOutOfRange:        return "OUT_OF_RANGE";
    case SpellTargetResult::kTooClose:          return "TOO_CLOSE";
    case SpellTargetResult::kWrongCreatureType: return "WRONG_CREATURE_TYPE";
    case SpellTargetResult::kSelfOnly:          return "SELF_ONLY";
    case SpellTargetResult::kTargetImmune:      return "TARGET_IMMUNE";
    case SpellTargetResult::kTargetNotSelf:     return "TARGET_NOT_SELF";
  }
  return "UNKNOWN";
}

SpellCastResult SpellTargetResultToCastResult(
    const SpellTargetResult result) {
  switch (result) {
    case SpellTargetResult::kValid:
      return SpellCastResult::kSuccess;
    case SpellTargetResult::kTargetIsDead:
      return SpellCastResult::kTargetsDead;
    case SpellTargetResult::kTargetIsAlive:
      return SpellCastResult::kTargetNotDead;
    case SpellTargetResult::kTargetIsFriendly:
      return SpellCastResult::kTargetFriendly;
    case SpellTargetResult::kTargetIsHostile:
      return SpellCastResult::kTargetEnemy;
    case SpellTargetResult::kTargetNotInParty:
      return SpellCastResult::kTargetNotInParty;
    case SpellTargetResult::kTargetNotInRaid:
      return SpellCastResult::kTargetNotInRaid;
    case SpellTargetResult::kTargetNotPlayer:
      return SpellCastResult::kTargetNotPlayer;
    case SpellTargetResult::kTargetNotNpc:
      return SpellCastResult::kTargetIsPlayer;
    case SpellTargetResult::kOutOfRange:
      return SpellCastResult::kOutOfRange;
    case SpellTargetResult::kTooClose:
      return SpellCastResult::kTooClose;
    case SpellTargetResult::kTargetImmune:
      return SpellCastResult::kImmune;
    case SpellTargetResult::kInvalidTarget:
    case SpellTargetResult::kWrongCreatureType:
    case SpellTargetResult::kSelfOnly:
    case SpellTargetResult::kTargetNotSelf:
      return SpellCastResult::kBadTargets;
  }
  return SpellCastResult::kBadTargets;
}

namespace spell_attr {

constexpr std::uint32_t kAttr0OnlyTargetPlayer = 0x00000080;
constexpr std::uint32_t kAttr0RangeAlways100 = 0x00000404;
constexpr std::uint32_t kAttrExSelfCast = 0x00000020;
constexpr std::uint32_t kAttrExPartyOnly = 0x00000040;
constexpr std::uint32_t kAttrExCantTargetSelf = 0x00080000;
constexpr std::uint32_t kAttrEx2AllowDeadUnitState = 0x00000001;
constexpr std::uint32_t kAttrEx4GeneralUnitPlayerException = 0x10000000;
constexpr std::uint32_t kAttrEx5RedirectThroughTarget = 0x00000800;
constexpr std::uint32_t kAttrEx5GeneralUnitException = 0x00200000;
constexpr std::uint32_t kAttrEx6AssistUseAltCheck = 0x00000008;
constexpr std::uint32_t kAttrEx6AllowAssistOnLinkedHostile = 0x00000200;
constexpr std::uint32_t kAttrEx6AllowTapped = 0x01000000;

constexpr std::uint32_t kTargetsFriendly = 0x00000001;
constexpr std::uint32_t kTargetsHostile = 0x00000002;
constexpr std::uint32_t kTargetsParty = 0x00000004;
constexpr std::uint32_t kTargetsRaid = 0x00000008;
constexpr std::uint32_t kTargetsPlayer = 0x00000010;
constexpr std::uint32_t kTargetsNpc = 0x00000020;
constexpr std::uint32_t kTargetsDead = 0x00000200;

}

namespace {

constexpr std::uint32_t kMaskUnit = 0x0002;
constexpr std::uint32_t kMaskDestLocation = 0x0040;
constexpr std::uint32_t kMaskUnitRaid = 0x0004;
constexpr std::uint32_t kMaskUnitParty = 0x0008;
constexpr std::uint32_t kMaskUnitEnemy = 0x0080;
constexpr std::uint32_t kMaskUnitAlly = 0x0100;
constexpr std::uint32_t kMaskCorpseEnemy = 0x0200;
constexpr std::uint32_t kMaskUnitDead = 0x0400;
constexpr std::uint32_t kMaskGameObject = 0x0800;
constexpr std::uint32_t kMaskGoItem = 0x4000;
constexpr std::uint32_t kMaskCorpseAlly = 0x8000;
constexpr std::uint32_t kMaskUnitMinipet = 0x10000;

constexpr std::uint32_t kRangeFlagMelee = 0x0001;
constexpr std::uint32_t kRangeFlagMinUsesReach = 0x0002;
constexpr std::uint32_t kRangeFlagUnlimited = 0x0404;

constexpr std::uint8_t kPvpFlagByte0 = 0x01;
constexpr std::uint8_t kPvpFlagByte1 = 0x02;
constexpr std::uint8_t kPvpFlagByte2 = 0x04;
constexpr std::uint8_t kPvpFlagByte3 = 0x08;
constexpr std::uint32_t kUnitFlagPlayerControlled = 0x00000008;
constexpr std::uint32_t kUnitFlagPcCanAssistBlock = 0x00000100;
constexpr std::uint32_t kUnitFlagNpcCanAssistBlock = 0x00000200;

[[nodiscard]] const GroupMember* FindGroupMember(
    const WorldSession& session,
    ObjectGuid guid) {
  for (const auto& member : session.group().members()) {
    if (member.guid == guid) {
      return &member;
    }
  }
  return nullptr;
}

[[nodiscard]] const CGPlayer_C* ResolveControllingPlayer(
    const WorldSession& session,
    const CGUnit_C& unit) {
  const ObjectGuid controller_guid =
      unit.Interaction().GetControllingPlayerGuid();
  if (controller_guid.IsEmpty()) {
    return nullptr;
  }
  return session.objects().GetPlayer(controller_guid);
}

[[nodiscard]] bool IsActivePlayerOrPartyMember(
    const WorldSession& session,
    ObjectGuid guid) {
  if (guid.IsEmpty()) return false;
  if (guid == session.objects().GetActivePlayerGuid()) {
    return true;
  }
  return FindGroupMember(session, guid) != nullptr;
}

[[nodiscard]] bool IsRaidRosterMember(
    const WorldSession& session,
    ObjectGuid guid) {
  if (guid.IsEmpty() || !session.group().IsRaid()) return false;
  return FindGroupMember(session, guid) != nullptr;
}

[[nodiscard]] bool IsSamePartySubGroup(
    const WorldSession& session,
    ObjectGuid guid) {
  if (guid.IsEmpty()) return false;
  if (guid == session.objects().GetActivePlayerGuid()) {
    return true;
  }

  const auto* member = FindGroupMember(session, guid);
  if (!member) {
    return false;
  }
  if (!session.group().IsRaid()) {
    return true;
  }
  return member->sub_group == session.group().my_sub_group();
}

[[nodiscard]] bool IsPartyControlledTarget(
    const WorldSession& session,
    const CGUnit_C& target) {
  if ((target.State().GetUnitFlags() & kUnitFlagPlayerControlled) == 0) {
    return false;
  }
  if (const auto* player = ResolveControllingPlayer(session, target)) {
    return IsSamePartySubGroup(session, player->GetGuid());
  }
  return false;
}

[[nodiscard]] bool IsRaidControlledTarget(
    const WorldSession& session,
    const CGUnit_C& target) {
  if ((target.State().GetUnitFlags() & kUnitFlagPlayerControlled) == 0) {
    return false;
  }
  if (const auto* player = ResolveControllingPlayer(session, target)) {
    return IsActivePlayerOrPartyMember(session, player->GetGuid()) ||
           IsRaidRosterMember(session, player->GetGuid());
  }
  return false;
}

[[nodiscard]] UnitRelation ResolveRelation(
    const CGUnit_C& caster,
    const CGUnit_C& target,
    const WorldSession& session) {
  if (caster.GetGuid() == target.GetGuid()) {
    return UnitRelation::kFriendly;
  }
  if (session.duel().IsDuelTarget(target.GetGuid())) {
    return UnitRelation::kHostile;
  }
  if (caster.Interaction().IsHostileTo(target) ||
      target.Interaction().IsHostileTo(caster)) {
    return UnitRelation::kHostile;
  }
  if (caster.Interaction().IsFriendlyTo(target) ||
      target.Interaction().IsFriendlyTo(caster)) {
    return UnitRelation::kFriendly;
  }
  return UnitRelation::kNeutral;
}

[[nodiscard]] bool CanAssist(
    const CGUnit_C& caster,
    const CGUnit_C& target,
    const WorldSession& session,
    bool use_alt_check) {
  const auto caster_flags = caster.State().GetUnitFlags();
  const auto target_flags = target.State().GetUnitFlags();
  const bool caster_player_controlled =
      (caster_flags & kUnitFlagPlayerControlled) != 0;
  const bool target_player_controlled =
      (target_flags & kUnitFlagPlayerControlled) != 0;

  if ((target_flags & 0x02000000u) != 0) {
    return false;
  }
  if (!use_alt_check) {
    if (caster_player_controlled &&
        (target_flags & kUnitFlagPcCanAssistBlock) != 0) {
      return false;
    }
    if (!caster_player_controlled &&
        (target_flags & kUnitFlagNpcCanAssistBlock) != 0) {
      return false;
    }
  }

  if (ResolveRelation(caster, target, session) == UnitRelation::kHostile &&
      !IsPartyControlledTarget(session, target) &&
      !IsRaidControlledTarget(session, target)) {
    return false;
  }

  const auto caster_pvp = caster.State().GetPvPFlags();
  const auto target_pvp = target.State().GetPvPFlags();

  if (target_player_controlled) {
    if ((target_pvp & kPvpFlagByte2) != 0 &&
        (caster_pvp & kPvpFlagByte2) == 0) {
      return false;
    }
    return (caster_pvp & kPvpFlagByte3) == 0 ||
           (target_pvp & kPvpFlagByte3) != 0 ||
           (target_pvp & kPvpFlagByte0) == 0;
  }

  if (!caster_player_controlled) {
    return true;
  }

  return use_alt_check ||
         (target_pvp & kPvpFlagByte0) != 0 ||
         target.State().CanBeAssistedByPlayerSpell() ||
         target.State().IsCivilian();
}

[[nodiscard]] bool CanAttack(
    const CGUnit_C& caster,
    const CGUnit_C& target,
    const WorldSession& session) {
  if (caster.GetGuid() == target.GetGuid()) return false;

  const auto caster_flags = caster.State().GetUnitFlags();
  const auto target_flags = target.State().GetUnitFlags();
  const bool caster_player_controlled =
      (caster_flags & kUnitFlagPlayerControlled) != 0;
  const bool target_player_controlled =
      (target_flags & kUnitFlagPlayerControlled) != 0;
  if ((target_flags & 0x00000002u) != 0 ||
      (target_flags & 0x00010000u) != 0 ||
      (target_flags & 0x00000080u) != 0 ||
      (target_flags & 0x00100000u) != 0 ||
      (target_flags & 0x02000000u) != 0) {
    return false;
  }

  if (caster_player_controlled &&
      (target_flags & kUnitFlagPcCanAssistBlock) != 0) {
    return false;
  }
  if (!caster_player_controlled &&
      (target_flags & kUnitFlagNpcCanAssistBlock) != 0) {
    return false;
  }
  if (target_player_controlled &&
      (caster_flags & kUnitFlagPcCanAssistBlock) != 0) {
    return false;
  }
  if (!target_player_controlled &&
      (caster_flags & kUnitFlagNpcCanAssistBlock) != 0) {
    return false;
  }

  if (session.duel().IsDuelTarget(target.GetGuid())) {
    return true;
  }

  const auto caster_pvp = caster.State().GetPvPFlags();
  const auto target_pvp = target.State().GetPvPFlags();

  if (caster_player_controlled || target_player_controlled) {
    if (caster_player_controlled && target_player_controlled) {
      if (caster.Interaction().IsNeutralOrCivilian(target)) {
        return false;
      }

      if ((target_pvp & kPvpFlagByte0) != 0) {
        return (caster_pvp & kPvpFlagByte3) == 0 &&
               (target_pvp & kPvpFlagByte3) == 0;
      }

      if ((caster_pvp & kPvpFlagByte2) != 0 &&
          (target_pvp & kPvpFlagByte2) != 0) {
        return true;
      }

      if (((caster_pvp & kPvpFlagByte1) == 0 &&
           (target_pvp & kPvpFlagByte1) == 0) ||
          (caster_pvp & kPvpFlagByte3) != 0) {
        return false;
      }

      return (target_pvp & kPvpFlagByte3) == 0;
    }

    if ((caster_player_controlled && (target_pvp & kPvpFlagByte3) != 0) ||
        (target_player_controlled && (caster_pvp & kPvpFlagByte3) != 0)) {
      return false;
    }

    return !caster.Interaction().IsNeutralOrCivilian(target);
  }

  return caster.Interaction().IsHostileTo(target) ||
         target.Interaction().IsHostileTo(caster);
}

[[nodiscard]] bool HasLinkedTarget(const CGUnit_C& unit) {
  return !unit.State().GetTarget().IsEmpty();
}

[[nodiscard]] const CGUnit_C* ResolveLinkedTarget(
    const WorldSession& session,
    const CGUnit_C& unit) {
  if (unit.State().GetTarget().IsEmpty()) return nullptr;
  return session.objects().GetUnit(unit.State().GetTarget());
}

[[nodiscard]] bool MatchesCreatureTypeMask(
    const WorldSession& session,
    const CGUnit_C& target,
    std::uint32_t creature_type_mask) {
  if (creature_type_mask == 0) return true;

  const auto creature_type =
      SpellTargetValidator::GetSpellTargetCreatureTypeId(session, target);
  if (creature_type == 0 || creature_type > 32) return false;
  return (creature_type_mask & (1u << (creature_type - 1))) != 0;
}

[[nodiscard]] std::uint32_t ResolveActiveSpellModifierFamily(
    const WorldSession& session) {
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return 0;
  }

  const auto* player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return 0;
  }

  const auto* chr_class = dbc->chr_classes().LookupEntry(player->State().GetClass());
  return chr_class != nullptr ? chr_class->spell_family : 0;
}

void ApplyRangeSpellModifiers(
    const WorldSession& session,
    const data::dbc::SpellEntry& spell,
    float* max_range) {
  (void)session.aura().ApplySpellModifierDeltas(
      ResolveActiveSpellModifierFamily(session),
      spell,
      SpellModOp::kRange,
      max_range);
}

[[nodiscard]] std::uint32_t ResolveExplicitUnitMask(
    const SpellTargetRequirements& req,
    const CGUnit_C& caster,
    const CGUnit_C& target,
    const WorldSession& session) {
  const bool can_assist =
      CanAssist(caster, target, session, false);
  const bool can_assist_alt = CanAssist(
      caster, target, session,
      (req.attributes_ex6 & spell_attr::kAttrEx6AssistUseAltCheck) != 0);
  const bool can_attack = CanAttack(caster, target, session);
  const bool general_unit_allowed =
      !target.IsPlayer() ||
      (req.attributes_ex4 & spell_attr::kAttrEx4GeneralUnitPlayerException) != 0 ||
      (req.attributes_ex5 & spell_attr::kAttrEx5GeneralUnitException) != 0;

  if ((req.target_mask & kMaskUnitParty) != 0 &&
      IsPartyControlledTarget(session, target) &&
      can_assist) {
    return kMaskUnitParty;
  }
  if ((req.target_mask & kMaskUnitRaid) != 0 &&
      IsRaidControlledTarget(session, target) &&
      can_assist) {
    return kMaskUnitRaid;
  }
  if ((req.target_mask & kMaskUnitAlly) != 0 && can_assist_alt) {
    return kMaskUnitAlly;
  }
  if ((req.target_mask & kMaskUnitEnemy) != 0 && can_attack) {
    return kMaskUnitEnemy;
  }
  if ((req.target_mask & kMaskUnit) != 0 && general_unit_allowed) {
    return kMaskUnit;
  }
  if ((req.target_mask & kMaskCorpseAlly) != 0 &&
      target.State().IsDead() &&
      (target.IsPlayer() ||
       SpellTargetValidator::GetSpellTargetCreatureTypeId(session, target) !=
           0) &&
      can_assist) {
    return kMaskCorpseAlly;
  }
  if ((req.target_mask & kMaskCorpseEnemy) != 0 &&
      target.State().IsDead() &&
      target.IsPlayer() &&
      !can_assist) {
    return kMaskCorpseEnemy;
  }
  if ((req.target_mask & kMaskUnitMinipet) != 0 &&
      SpellTargetValidator::GetSpellTargetCreatureTypeId(session, target) ==
          static_cast<std::uint32_t>(CreatureTypeId::kNonCombatPet)) {
    return kMaskUnitMinipet;
  }
  if ((req.target_mask & kMaskUnitAlly) != 0 &&
      (req.attributes_ex6 & spell_attr::kAttrEx6AllowAssistOnLinkedHostile) != 0 &&
      HasLinkedTarget(target) &&
      can_attack) {
    return kMaskUnitAlly;
  }

  return 0;
}

}

SpellTargetResult SpellTargetValidator::ValidateAliveState(
    const SpellTargetRequirements& req,
    const UnitTargetInfo& target) {
  if (target.is_dead) {
    if (req.targets_alive && !req.targets_dead) {
      return SpellTargetResult::kTargetIsDead;
    }
  } else if (req.targets_dead && !req.targets_alive) {
    return SpellTargetResult::kTargetIsAlive;
  }
  return SpellTargetResult::kValid;
}

SpellTargetResult SpellTargetValidator::ValidateRelation(
    const SpellTargetRequirements& req,
    const UnitTargetInfo& target) {
  if (req.self_only && !target.is_self) {
    return SpellTargetResult::kSelfOnly;
  }
  if (req.targets_friendly && !req.targets_hostile &&
      target.relation == UnitRelation::kHostile) {
    return SpellTargetResult::kTargetIsHostile;
  }
  if (req.targets_hostile && !req.targets_friendly &&
      target.relation == UnitRelation::kFriendly) {
    return SpellTargetResult::kTargetIsFriendly;
  }
  if (req.requires_party && !target.is_in_party && !target.is_self) {
    return SpellTargetResult::kTargetNotInParty;
  }
  if (req.requires_raid && !target.is_in_raid && !target.is_in_party &&
      !target.is_self) {
    return SpellTargetResult::kTargetNotInRaid;
  }
  if (req.requires_player && !target.is_player) {
    return SpellTargetResult::kTargetNotPlayer;
  }
  if (req.requires_npc && target.is_player) {
    return SpellTargetResult::kTargetNotNpc;
  }
  return SpellTargetResult::kValid;
}

SpellTargetResult SpellTargetValidator::ValidateRange(
    const SpellTargetRequirements& req,
    const UnitTargetInfo& target) {
  if (req.self_only || (req.min_range == 0.0f && req.max_range == 0.0f)) {
    return SpellTargetResult::kValid;
  }
  if (req.max_range > 0.0f && target.distance > req.max_range) {
    return SpellTargetResult::kOutOfRange;
  }
  if (req.min_range > 0.0f && target.distance < req.min_range) {
    return SpellTargetResult::kTooClose;
  }
  return SpellTargetResult::kValid;
}

SpellTargetResult SpellTargetValidator::ValidateCreatureType(
    const SpellTargetRequirements& req,
    const UnitTargetInfo& target) {
  if (req.creature_type_mask == 0) {
    return SpellTargetResult::kValid;
  }

  std::uint32_t target_flag = target.creature_type;
  if (target.is_player && target_flag == 0) {
    target_flag = static_cast<std::uint32_t>(CreatureTypeId::kHumanoid);
  }

  if (target_flag == 0 || target_flag > 32 ||
      (req.creature_type_mask & (1u << (target_flag - 1))) == 0) {
    return SpellTargetResult::kWrongCreatureType;
  }
  return SpellTargetResult::kValid;
}

std::uint32_t SpellTargetValidator::GetSpellTargetCreatureTypeId(
    const WorldSession& session,
    const CGUnit_C& target) {

  if (!target.IsPlayer()) {
    const auto form_id =
        static_cast<std::uint32_t>(target.Animation().GetShapeshiftForm());
    if (form_id != 0) {
      if (const auto* dbc = session.GetDbcLoader()) {
        if (const auto* form =
                dbc->spell_shapeshift_form().LookupEntry(form_id)) {
          if (form->creature_type > 0) {
            return form->creature_type;
          }
        }
      }
    }
  }

  if (const auto* tpl =
          session.query_cache().GetCreatureTemplate(target.GetEntry())) {
    return tpl->creature_type;
  }

  const auto race_id = static_cast<std::uint32_t>(target.State().GetRace());
  if (race_id != 0) {
    if (const auto* dbc = session.GetDbcLoader()) {
      if (const auto* race = dbc->chr_races().LookupEntry(race_id)) {
        if (race->creature_type > 0) {
          return race->creature_type;
        }
      }
    }
  }

  return 0;
}

SpellTargetRangeWindow SpellTargetValidator::GetTargetRangeWindow(
    const data::dbc::SpellEntry& spell,
    const data::dbc::SpellRangeEntry* range_entry,
    const CGUnit_C& caster,
    const CGUnit_C& target,
    bool use_friendly_range,
    const WorldSession* session) {
  SpellTargetRangeWindow window;
  if ((spell.attributes & spell_attr::kAttr0RangeAlways100) != 0) {
    window.max_range = 100.0f;
    return window;
  }
  if (!range_entry) {
    return window;
  }

  const float base_min = use_friendly_range ? range_entry->range_min_friendly
                                            : range_entry->range_min;
  const float base_max = use_friendly_range ? range_entry->range_max_friendly
                                            : range_entry->range_max;

  if ((range_entry->flags & kRangeFlagUnlimited) != 0) {
    window.max_range = 100.0f;
    return window;
  }

  const float combat_reach_sum =
      caster.State().GetCombatReach() + target.State().GetCombatReach();
  const float min_reach_padding = interaction_range::ComputeUnitInteractionRange(
      caster.State().GetCombatReach(), target.State().GetCombatReach());
  float max_range_padding = 0.0f;

  if ((range_entry->flags & kRangeFlagMelee) != 0) {
    window.min_range = 0.0f;
    max_range_padding = min_reach_padding;
  } else if ((range_entry->flags & kRangeFlagMinUsesReach) != 0) {
    window.min_range = base_min + min_reach_padding;
    window.max_range = base_max;
  } else {
    window.min_range = base_min;
    window.max_range = base_max;
    max_range_padding = combat_reach_sum;
    if (window.min_range > 0.0f) {
      window.min_range += combat_reach_sum;
    }
  }

  if (session) {
    ApplyRangeSpellModifiers(*session, spell, &window.max_range);
  }

  window.max_range += max_range_padding;

  {
    constexpr std::uint32_t kLeewayMoveMask =
        kMoveFlagForward | kMoveFlagStrafeLeft |
        kMoveFlagStrafeRight | kMoveFlagFalling;

    const auto& caster_mi = caster.GetMovementInfo();
    const auto& target_mi = target.GetMovementInfo();
    const bool caster_moving = (caster_mi.flags & kLeewayMoveMask) != 0;
    const bool target_moving = (target_mi.flags & kLeewayMoveMask) != 0;

    const bool caster_at_walk_pace = caster.Movement().IsMovingAtWalkPace();
    const bool target_at_walk_pace = target.Movement().IsMovingAtWalkPace();

    if (caster_moving && target_moving &&
        !caster_at_walk_pace && !target_at_walk_pace &&
        ((range_entry->flags & kRangeFlagMelee) != 0 || target.IsPlayer())) {
      window.max_range += 2.6666667f;
    }
  }

  return window;
}

SpellTargetRangeWindow SpellTargetValidator::GetUntargetedRangeWindow(
    const data::dbc::SpellEntry& spell,
    const data::dbc::SpellRangeEntry* range_entry,
    const CGUnit_C& caster,
    bool use_friendly_range,
    const WorldSession* session) {
  SpellTargetRangeWindow window;
  if ((spell.attributes & spell_attr::kAttr0RangeAlways100) != 0) {
    window.max_range = 100.0f;
    return window;
  }
  if (!range_entry) {
    return window;
  }

  const float base_min = use_friendly_range ? range_entry->range_min_friendly
                                            : range_entry->range_min;
  window.max_range = use_friendly_range ? range_entry->range_max_friendly
                                        : range_entry->range_max;

  if ((range_entry->flags & kRangeFlagUnlimited) != 0) {
    window.max_range = 100.0f;
    return window;
  }

  const float self_padding = interaction_range::ComputeUnitInteractionRange(
      caster.State().GetCombatReach(), caster.State().GetCombatReach());

  if ((range_entry->flags & kRangeFlagMelee) != 0) {
    window.max_range = 0.0f;
  } else if ((range_entry->flags & kRangeFlagMinUsesReach) != 0) {
    window.min_range = base_min + self_padding;
  } else {
    window.min_range = base_min;
  }

  if (session) {
    ApplyRangeSpellModifiers(*session, spell, &window.max_range);
  }

  if ((range_entry->flags & kRangeFlagMelee) != 0) {
    window.max_range += self_padding;
  }

  return window;
}

bool SpellTargetValidator::IsTargetInRange(
    const CGObject_C& caster,
    const CGObject_C& target,
    const SpellTargetRangeWindow& window,
    bool* out_of_range) {
  if (out_of_range) {
    *out_of_range = false;
  }

  const double distance_sq =
      caster.GetSquaredDistanceToPosition(target.GetPosition());
  const double min_range_sq =
      static_cast<double>(window.min_range) * window.min_range;
  if (window.min_range > 0.0f && min_range_sq > distance_sq) {
    return false;
  }

  const double max_range_sq =
      static_cast<double>(window.max_range) * window.max_range;
  if (window.max_range == 0.0f || max_range_sq >= distance_sq) {
    return true;
  }

  if (out_of_range) {
    *out_of_range = true;
  }

  return false;
}

std::uint32_t SpellTargetValidator::BuildTargetMask(
    const data::dbc::SpellEntry& spell,
    const data::dbc::DbcLoader* const dbc) {
  const std::uint32_t targets = spell.targets;

  switch (spell.effect_implicit_target_a.front()) {
    case 0x01:
      return (targets & kMaskUnitDead) != 0 ? targets & ~kMaskUnitDead : targets;
    case 0x05:
      return (targets & kMaskCorpseAlly) != 0 ? targets & ~kMaskCorpseAlly
                                              : targets;
    case 0x06:
    case 0x35:
      return targets | kMaskUnitEnemy;
    case 0x15:
    case 0x2D:
      return targets | kMaskUnitAlly;
    case 0x17:
      return targets | kMaskGameObject;
    case 0x19:
    case 0x3F:
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
    case 0x4A:
    case 0x4B:
      return targets | kMaskUnit;
    case 0x1A:
      return targets | kMaskGoItem;
    case 0x23:
      return targets | kMaskUnitParty;
    case 0x39:
    case 0x3D:
      return targets | kMaskUnitRaid;
    case 0x59: {

      const auto* const missile =
          dbc != nullptr && spell.spell_missile_id != 0
              ? dbc->spell_missile().LookupEntry(spell.spell_missile_id)
              : nullptr;
      return missile != nullptr && (missile->flags & 1u) != 0
                 ? targets & ~kMaskDestLocation
                 : targets;
    }
    case 0x5A:
      return targets | kMaskUnitMinipet;
    default:
      return targets;
  }
}

SpellTargetResult SpellTargetValidator::Validate(
    const SpellTargetRequirements& req,
    const UnitTargetInfo& target) {
  if (target.guid == ObjectGuid(0) && !req.self_only) {
    return SpellTargetResult::kInvalidTarget;
  }
  if (target.is_immune) {
    return SpellTargetResult::kTargetImmune;
  }

  auto result = ValidateRelation(req, target);
  if (result != SpellTargetResult::kValid) return result;

  result = ValidateAliveState(req, target);
  if (result != SpellTargetResult::kValid) return result;

  result = ValidateCreatureType(req, target);
  if (result != SpellTargetResult::kValid) return result;

  return ValidateRange(req, target);
}

SpellTargetRequirements SpellTargetValidator::BuildRequirements(
    const data::dbc::SpellEntry& spell,
    bool use_friendly_range,
    float min_range,
    float max_range) {
  auto req = BuildRequirements(
      spell.id,
      spell.attributes,
      spell.attributes_ex,
      spell.attributes_ex2,
      spell.targets,
      spell.target_creature_type,
      min_range,
      max_range);
  req.target_mask = BuildTargetMask(spell);
  req.attributes_ex = spell.attributes_ex;
  req.attributes_ex2 = spell.attributes_ex2;
  req.attributes_ex4 = spell.attributes_ex4;
  req.attributes_ex5 = spell.attributes_ex5;
  req.attributes_ex6 = spell.attributes_ex6;
  req.targets_friendly = (req.target_mask & kMaskUnitAlly) != 0 ||
                         (req.target_mask & kMaskUnitParty) != 0 ||
                         (req.target_mask & kMaskUnitRaid) != 0 ||
                         (req.target_mask & kMaskCorpseAlly) != 0;
  req.targets_hostile = (req.target_mask & kMaskUnitEnemy) != 0 ||
                        (req.target_mask & kMaskCorpseEnemy) != 0;
  req.requires_party = (req.target_mask & kMaskUnitParty) != 0;
  req.requires_raid = (req.target_mask & kMaskUnitRaid) != 0;
  req.targets_dead = (req.target_mask & (kMaskUnitDead | kMaskCorpseEnemy |
                                         kMaskCorpseAlly)) != 0;
  req.targets_alive = (req.target_mask & kMaskUnitDead) == 0;
  if (use_friendly_range && max_range > 0.0f && min_range == 0.0f) {
    req.targets_friendly = true;
  }
  return req;
}

SpellTargetRequirements SpellTargetValidator::BuildRequirements(
    std::uint32_t spell_id,
    std::uint32_t attributes,
    std::uint32_t attributes_ex,
    std::uint32_t ,
    std::uint32_t targets,
    std::uint32_t target_creature_type,
    float min_range,
    float max_range) {
  SpellTargetRequirements req;
  req.spell_id = spell_id;
  req.target_mask = targets & 0xFFFFu;
  req.attributes_ex = attributes_ex;
  req.self_only = (attributes_ex & spell_attr::kAttrExSelfCast) != 0;
  req.requires_party = (attributes_ex & spell_attr::kAttrExPartyOnly) != 0;
  req.requires_player = (attributes & spell_attr::kAttr0OnlyTargetPlayer) != 0;
  req.targets_friendly = (targets & spell_attr::kTargetsFriendly) != 0;
  req.targets_hostile = (targets & spell_attr::kTargetsHostile) != 0;
  if (!req.targets_friendly && !req.targets_hostile) {
    req.targets_friendly = true;
    req.targets_hostile = true;
  }
  req.targets_dead = (targets & spell_attr::kTargetsDead) != 0;
  req.targets_alive = !req.targets_dead;
  if (targets & spell_attr::kTargetsRaid) req.requires_raid = true;
  if (targets & spell_attr::kTargetsParty) req.requires_party = true;
  if (targets & spell_attr::kTargetsPlayer) req.requires_player = true;
  if (targets & spell_attr::kTargetsNpc) req.requires_npc = true;
  req.creature_type_mask = target_creature_type;
  req.min_range = min_range;
  req.max_range = max_range;
  return req;
}

SpellUnitTargetResolution SpellTargetValidator::ResolveUnitTarget(
    const WorldSession& session,
    const data::dbc::DbcLoader& dbc,
    std::uint32_t spell_id,
    const CGUnit_C& caster,
    const CGUnit_C& input_target,
    const std::uint32_t target_mask,
    bool check_range) {
  SpellUnitTargetResolution resolution;
  const auto* spell = dbc.spell().LookupEntry(spell_id);
  if (!spell) {
    return resolution;
  }

  const auto initial_mask = target_mask;
  const CGUnit_C* target = &input_target;

  if ((spell->attributes_ex5 & spell_attr::kAttrEx5RedirectThroughTarget) != 0) {
    const auto* redirected = ResolveRedirectedTarget(
        spell, initial_mask, input_target, caster, session);
    if (!redirected) {
      return resolution;
    }
    target = redirected;
  }

  if (target->GetGuid() == caster.GetGuid() &&
      (spell->attributes_ex & spell_attr::kAttrExCantTargetSelf) != 0) {
    resolution.result = SpellTargetResult::kTargetNotSelf;
    return resolution;
  }

  if (target->State().IsTappedByOther() &&
      (spell->attributes_ex6 & spell_attr::kAttrEx6AllowTapped) == 0) {
    return resolution;
  }
  if (!MatchesCreatureTypeMask(session, *target, spell->target_creature_type)) {
    resolution.result = SpellTargetResult::kWrongCreatureType;
    return resolution;
  }

  if (target->State().IsDead()) {
    const auto dead_mask = initial_mask &
                           (kMaskCorpseEnemy | kMaskCorpseAlly | kMaskUnitDead);
    if (dead_mask == 0 &&
        (spell->attributes_ex2 & spell_attr::kAttrEx2AllowDeadUnitState) == 0) {
      resolution.result = SpellTargetResult::kTargetIsDead;
      return resolution;
    }
  } else if ((initial_mask & kMaskUnitDead) != 0) {
    resolution.result = SpellTargetResult::kTargetIsAlive;
    return resolution;
  }

  const bool use_friendly_range =
      CanAssist(caster, *target, session, false);
  const auto* range_entry = spell->range_index != 0
                                ? dbc.spell_range().LookupEntry(spell->range_index)
                                : nullptr;
  const auto range_window = GetTargetRangeWindow(
      *spell, range_entry, caster, *target, use_friendly_range, &session);

  auto req = BuildRequirements(
      *spell, use_friendly_range, range_window.min_range,
      range_window.max_range);
  req.target_mask = initial_mask;
  const std::uint32_t matched_mask =
      ResolveExplicitUnitMask(req, caster, *target, session);
  if (matched_mask == 0) {
    return resolution;
  }

  resolution.resolved_target = target->GetGuid();
  resolution.packet_target_mask = matched_mask == kMaskUnitMinipet
                                      ? kMaskUnitMinipet
                                      : kMaskUnit;
  resolution.consumed_target_mask = matched_mask;

  if (target->State().IsDead()) {
    if (matched_mask == kMaskUnit) {
      resolution.consumed_target_mask |=
          kMaskUnitDead | kMaskCorpseEnemy | kMaskCorpseAlly;
    } else if (matched_mask == kMaskUnitParty ||
               matched_mask == kMaskUnitRaid ||
               matched_mask == kMaskUnitAlly ||
               matched_mask == kMaskUnitEnemy) {
      resolution.consumed_target_mask |= kMaskUnitDead;
    }
  }

  if (target != &input_target) {
    if ((initial_mask &
         (kMaskUnitParty | kMaskUnitRaid | kMaskUnitAlly)) != 0) {
      resolution.consumed_target_mask |= kMaskUnitEnemy;
    } else if ((initial_mask & kMaskUnitEnemy) != 0) {
      resolution.consumed_target_mask |= kMaskUnitAlly;
    }
  }

  if (!check_range) {
    resolution.result = SpellTargetResult::kValid;
    return resolution;
  }
  if (req.self_only ||
      (range_window.min_range == 0.0f && range_window.max_range == 0.0f)) {
    resolution.result = SpellTargetResult::kValid;
    return resolution;
  }

  bool out_of_range = false;
  if (IsTargetInRange(caster, *target, range_window, &out_of_range)) {
    resolution.result = SpellTargetResult::kValid;
    return resolution;
  }
  resolution.result = out_of_range ? SpellTargetResult::kOutOfRange
                                   : SpellTargetResult::kTooClose;
  resolution.resolved_target = {};
  resolution.consumed_target_mask = 0;
  resolution.packet_target_mask = 0;
  return resolution;
}

SpellTargetResult SpellTargetValidator::ValidateUnitTarget(
    const WorldSession& session,
    const data::dbc::DbcLoader& dbc,
    std::uint32_t spell_id,
    const CGUnit_C& caster,
    const CGUnit_C& target,
    bool check_range) {
  const auto* const spell = dbc.spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return SpellTargetResult::kInvalidTarget;
  }
  return ResolveUnitTarget(
             session, dbc, spell_id, caster, target,
             BuildTargetMask(*spell, &dbc), check_range)
      .result;
}

SpellTargetResult SpellTargetValidator::ValidateUnitTarget(
    const WorldSession& session,
    const data::dbc::DbcLoader& dbc,
    std::uint32_t spell_id,
    const CGUnit_C& target,
    bool check_range) {
  const auto* caster = session.objects().GetLocalPlayerTyped();
  if (!caster) {
    return SpellTargetResult::kInvalidTarget;
  }

  return ValidateUnitTarget(session, dbc, spell_id, *caster, target, check_range);
}

bool SpellTargetValidator::CanAssistSpellTarget(
    const CGUnit_C& caster,
    const CGUnit_C& target,
    const WorldSession& session,
    bool use_alt_check) {
  return CanAssist(caster, target, session, use_alt_check);
}

bool SpellTargetValidator::CanAttackSpellTarget(
    const CGUnit_C& caster,
    const CGUnit_C& target,
    const WorldSession& session) {
  return CanAttack(caster, target, session);
}

const CGUnit_C* SpellTargetValidator::ResolveRedirectedTarget(
    const data::dbc::SpellEntry* spell,
    std::uint32_t target_mask,
    const CGUnit_C& target,
    const CGUnit_C& caster,
    const WorldSession& session) {
  const CGUnit_C* resolved = &target;
  const bool allow_assist_alt =
      spell && (spell->attributes_ex6 &
                spell_attr::kAttrEx6AssistUseAltCheck) != 0;

  if ((target_mask & 0x10Cu) != 0) {
    if (CanAttack(caster, target, session)) {

      const auto* linked = ResolveLinkedTarget(session, target);
      if (!linked) return nullptr;

      if (!CanAssist(caster, *linked, session, allow_assist_alt)) {
        return nullptr;
      }

      const bool party_ok =
          (target_mask & kMaskUnitParty) != 0 &&
          IsPartyControlledTarget(session, *linked);
      const bool raid_ok =
          (target_mask & kMaskUnitRaid) != 0 &&
          IsRaidControlledTarget(session, *linked);
      const bool ally_ok = (target_mask & kMaskUnitAlly) != 0;

      if (!party_ok && !raid_ok && !ally_ok) {
        return nullptr;
      }
      resolved = linked;
    }
  }

  else if ((target_mask & 0x80u) != 0) {
    if (CanAssist(caster, target, session, allow_assist_alt)) {
      const auto* linked = ResolveLinkedTarget(session, target);
      if (!linked || !CanAttack(caster, *linked, session)) {
        return nullptr;
      }
      resolved = linked;
    }
  }

  return resolved;
}

}

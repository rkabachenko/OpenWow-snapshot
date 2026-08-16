#include "openwow/game/objects/cgunit.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/query_cache.h"
#include "openwow/data/formats/dbc/dbc_loader.h"

namespace openwow::game {

namespace {

constexpr std::uint32_t kPlayerFlagsGhost = 0x00000010u;
constexpr std::uint32_t kNpcFlagQuestGiver = 0x00000002u;
constexpr std::uint32_t kUnitVisStealthMask = 0x02u;
constexpr std::uint32_t kMountedActionCreatureTemplateTypeFlag = 0x00000800u;
constexpr std::uint32_t kPlayerSpellAssistCreatureTemplateTypeFlag = 0x00001000u;
constexpr std::uint32_t kCivilianCreatureTemplateTypeFlag = 0x04000000u;
constexpr std::uint32_t kDeadInteractCreatureTemplateTypeFlag = 0x00000080u;
constexpr std::uint32_t kLinkAllCreatureTemplateTypeFlag = 0x00400000u;
constexpr std::uint32_t kNoShadowBlobCreatureTemplateTypeFlag = 0x02000000u;
constexpr std::uint32_t kHideFactionTooltipCreatureTemplateTypeFlag = 0x00000010u;
constexpr std::uint32_t kDontLogDeathCreatureTemplateTypeFlag = 0x00000400u;

}

bool UnitStateRuntime::IsInCombat() const {
  return HasUnitState(UnitStateFlag::kInCombat);
}

bool UnitStateRuntime::IsDead() const {
  return GetHealth() == 0;
}

bool UnitStateRuntime::IsDeadOrGhost() const {
  return IsDead() ||
         (owner_.IsPlayer() && (descriptor_.PlayerFlags() & kPlayerFlagsGhost) != 0u);
}

bool UnitStateRuntime::IsLootableCorpseAt(const std::uint32_t current_tick_ms) const {
  if (!IsDead() || (GetDynamicFlags() & kUnitDynFlagLootable) == 0u) {
    return false;
  }
  return owner_.Loot().IsCorpseReadyAt(current_tick_ms);
}

bool UnitStateRuntime::IsLootableCorpseNow() const {
  return IsLootableCorpseAt(core::GameClock::GetTickCount32());
}

UnitLootComponent &CGUnit_C::Loot() noexcept { return loot_; }

const UnitLootComponent &CGUnit_C::Loot() const noexcept { return loot_; }

bool UnitStateRuntime::IsFeignDeath() const {
  return HasUnitState(UnitStateFlag::kFeignDeath);
}

bool UnitStateRuntime::HasUnitState(const UnitStateFlag state) const {
  return (GetUnitFlags() & static_cast<std::uint32_t>(state)) != 0u;
}

bool UnitMovementRuntime::IsMoving() const {
  constexpr std::uint32_t kMovingMask =
      kMoveFlagForward | kMoveFlagBackward | kMoveFlagStrafeLeft |
      kMoveFlagStrafeRight | kMoveFlagFalling | kMoveFlagAscending |
      kMoveFlagDescending | kMoveFlagSplineEnabled;
  return (owner_.GetMovementInfo().flags & kMovingMask) != 0u;
}

bool UnitMovementRuntime::IsFlying() const {
  return owner_.GetMovementInfo().HasFlag(kMoveFlagFlying);
}

bool UnitMovementRuntime::IsSwimming() const {
  return owner_.GetMovementInfo().HasFlag(kMoveFlagSwimming);
}

bool UnitMovementRuntime::IsRooted() const {
  return owner_.GetMovementInfo().HasFlag(kMoveFlagRoot);
}

bool UnitMovementRuntime::HasFallingLaunchVelocity() const {
  return owner_.GetMovementInfo().HasFallingLaunchVelocity();
}

bool UnitMovementRuntime::IsMovingAtRunPace() const {
  const float walk_speed = data_.GetSpeed(kSpeedWalk);
  return 2.0f * walk_speed < ComputeCurrentSpeed();
}

bool UnitMovementRuntime::IsMovingAtWalkPace() const {
  const float walk_speed = data_.GetSpeed(kSpeedWalk);
  return 2.0f * walk_speed >= ComputeCurrentSpeed();
}

bool UnitStateRuntime::IsSitting() const {
  return owner_.Animation().GetStandState() == 1u;
}

bool UnitStateRuntime::IsInAnySittingStandState() const {
  const auto stand_state = owner_.Animation().GetStandState();
  return stand_state == 1u || (stand_state >= 4u && stand_state <= 6u);
}

bool UnitStateRuntime::IsStealth() const {
  return (GetVisFlags() & kUnitVisStealthMask) != 0u;
}

bool UnitStateRuntime::IsTapped() const {
  return (GetDynamicFlags() & kUnitDynFlagTapped) != 0u;
}

bool UnitStateRuntime::IsTappedByPlayer() const {
  return (GetDynamicFlags() & kUnitDynFlagTappedByPlayer) != 0u;
}

bool UnitStateRuntime::IsTappedByAllThreatList() const {
  return (GetDynamicFlags() & kUnitDynFlagTappedByAllThreatList) != 0u;
}

bool UnitStateRuntime::IsTappedByOther() const {
  return IsTapped() && !IsTappedByPlayer() && !IsTappedByAllThreatList();
}

bool UnitStateRuntime::IsConfused() const { return HasUnitFlag(0x00400000u); }
bool UnitStateRuntime::IsFleeing() const { return HasUnitFlag(0x00800000u); }
bool UnitStateRuntime::IsPossessed() const { return HasUnitFlag(0x01000000u); }
bool UnitStateRuntime::IsNotSelectable() const { return HasUnitFlag(0x02000000u); }
bool UnitStateRuntime::IsSkinnable() const { return HasUnitFlag(0x04000000u); }
bool UnitStateRuntime::IsDisarmed() const { return HasUnitFlag(0x00200000u); }
bool UnitStateRuntime::IsSilenced() const { return HasUnitFlag(0x00002000u); }
bool UnitStateRuntime::IsPacified() const { return HasUnitFlag(0x00020000u); }
bool UnitStateRuntime::IsStunned() const { return HasUnitFlag(0x00040000u); }
bool UnitStateRuntime::IsTaxiFlight() const { return HasUnitFlag(0x00100000u); }

bool UnitStateRuntime::IsPvPFreeForAll() const {
  return (GetPvPFlags() & 0x04u) != 0u;
}

bool UnitStateRuntime::IsPvP() const {
  return (GetPvPFlags() & 0x01u) != 0u;
}

bool UnitStateRuntime::HasCreatureTemplateTypeFlag(const std::uint32_t flag) const {
  const auto *const objects = owner_.object_manager();
  if (objects == nullptr || owner_.GetEntry() == 0u) {
    return false;
  }
  const auto *const creature =
      objects->query_cache().GetCreatureTemplate(owner_.GetEntry());
  return creature != nullptr && (creature->type_flags & flag) != 0u;
}

bool UnitStateRuntime::CanActWhileMounted() const {
  return HasCreatureTemplateTypeFlag(kMountedActionCreatureTemplateTypeFlag);
}

bool UnitStateRuntime::IsCivilian() const {
  return HasCreatureTemplateTypeFlag(kCivilianCreatureTemplateTypeFlag);
}

bool UnitStateRuntime::CanBeAssistedByPlayerSpell() const {
  return HasCreatureTemplateTypeFlag(
      kPlayerSpellAssistCreatureTemplateTypeFlag);
}

bool UnitStateRuntime::HasDeadInteractTypeFlag() const {
  return HasCreatureTemplateTypeFlag(kDeadInteractCreatureTemplateTypeFlag);
}

bool UnitStateRuntime::IsLinkAll() const {
  return HasCreatureTemplateTypeFlag(kLinkAllCreatureTemplateTypeFlag);
}

bool UnitStateRuntime::HasNoShadowBlob() const {
  return HasCreatureTemplateTypeFlag(kNoShadowBlobCreatureTemplateTypeFlag);
}

bool UnitStateRuntime::HasHideFactionTooltipFlag() const {
  return HasCreatureTemplateTypeFlag(
      kHideFactionTooltipCreatureTemplateTypeFlag);
}

ClassificationRank UnitStateRuntime::GetClassificationRank() const {
  const auto *const objects = owner_.object_manager();
  if (objects == nullptr || owner_.GetEntry() == 0u || GetPetNumber() != 0u) {
    return ClassificationRank::kNormal;
  }
  const auto *const creature =
      objects->query_cache().GetCreatureTemplate(owner_.GetEntry());
  return creature != nullptr ? static_cast<ClassificationRank>(creature->rank)
                             : ClassificationRank::kNormal;
}

bool UnitStateRuntime::ShouldSuppressDeathCombatLog() const {
  if (owner_.IsPlayer()) {
    return false;
  }
  const auto *const objects = owner_.object_manager();
  if (objects == nullptr || owner_.GetEntry() == 0u) {
    return true;
  }
  const auto *const creature =
      objects->query_cache().GetCreatureTemplate(owner_.GetEntry());
  return creature == nullptr ||
         (creature->type_flags & kDontLogDeathCreatureTemplateTypeFlag) != 0u;
}

bool UnitStateRuntime::HasCharmedByGUID() const {
  return !GetCharmedBy().IsEmpty();
}

float UnitStateRuntime::GetCreatureHealthModifier() const {
  if (IsHunterPet()) {
    return 1.0f;
  }
  const auto *const objects = owner_.object_manager();
  if (objects == nullptr || owner_.GetEntry() == 0u) {
    return 0.0f;
  }
  const auto *const creature =
      objects->query_cache().GetCreatureTemplate(owner_.GetEntry());
  return creature != nullptr ? creature->mod_health : 0.0f;
}

float UnitStateRuntime::GetCreaturePowerModifier() const {
  if (IsHunterPet()) {
    return 1.0f;
  }
  const auto *const objects = owner_.object_manager();
  if (objects == nullptr || owner_.GetEntry() == 0u) {
    return 0.0f;
  }
  const auto *const creature =
      objects->query_cache().GetCreatureTemplate(owner_.GetEntry());
  return creature != nullptr ? creature->mod_mana : 0.0f;
}

CreatureTypeId UnitStateRuntime::GetCreatureType() const {

  const auto *const dbc = owner_.dbc_loader();
  if (dbc != nullptr) {
    if (const auto form = owner_.Animation().GetShapeshiftForm(); form != 0u) {
      if (const auto *const row =
              dbc->spell_shapeshift_form().LookupEntry(form);
          row != nullptr && row->creature_type > 0u) {
        return static_cast<CreatureTypeId>(row->creature_type);
      }
    }
  }
  if (const auto *const objects = owner_.object_manager();
      objects != nullptr && owner_.GetEntry() != 0u) {
    if (const auto *const creature =
            objects->query_cache().GetCreatureTemplate(owner_.GetEntry());
        creature != nullptr && creature->creature_type != 0u) {
      return static_cast<CreatureTypeId>(creature->creature_type);
    }
  }
  if (dbc != nullptr) {
    if (const auto *const race = dbc->chr_races().LookupEntry(GetRace());
        race != nullptr && race->creature_type > 0u) {
      return static_cast<CreatureTypeId>(race->creature_type);
    }
  }
  return CreatureTypeId::kNone;
}

bool UnitStateRuntime::IsHunterPet() const {
  if (descriptor_.PetNumber() == 0u) {
    return false;
  }
  const auto *const objects = owner_.object_manager();
  const auto *const owner =
      objects != nullptr ? objects->GetPlayer(GetSummonedBy()) : nullptr;
  return owner != nullptr && owner->State().GetClass() == 3u;
}

bool UnitStateRuntime::HasQuestGiverWithActiveOverlay() const {
  return (GetNpcFlags() & kNpcFlagQuestGiver) != 0u &&
         owner_.GetOverlayDisplayType() >= OverlayDisplayType::kQuestAvailable;
}

bool UnitStateRuntime::IsPvPFlaggedRacialLeader() const {
  const auto *const objects = owner_.object_manager();
  const auto *const creature =
      IsPvP() && objects != nullptr && owner_.GetEntry() != 0u
          ? objects->query_cache().GetCreatureTemplate(owner_.GetEntry())
          : nullptr;
  return creature != nullptr && creature->racial_leader != 0u;
}

const char *UnitStateRuntime::GetCreatureSubnameForDisplay() const {
  const auto *const objects = owner_.object_manager();
  const auto *const creature =
      objects != nullptr && owner_.GetEntry() != 0u
          ? objects->query_cache().GetCreatureTemplate(owner_.GetEntry())
          : nullptr;
  return creature != nullptr && !creature->sub_name.empty() &&
                  owner_.Mount().DisplayId(owner_) == 0u
             ? creature->sub_name.c_str()
             : nullptr;
}

bool UnitStateRuntime::IsAreaInAreaGroup(
    const data::dbc::DbcStore<data::dbc::AreaTableEntry> &areas,
    const data::dbc::DbcStore<data::dbc::AreaGroupEntry> &groups,
    const std::int32_t area_id, const std::int32_t group_id,
    std::uint32_t *const out_count) {
  if (out_count != nullptr) {
    *out_count = 0u;
  }
  const auto *const area =
      areas.LookupEntry(static_cast<std::uint32_t>(area_id));
  if (area == nullptr || group_id == 0) {
    return false;
  }
  auto current_group_id = static_cast<std::uint32_t>(group_id);
  while (current_group_id != 0u) {
    const auto *const group = groups.LookupEntry(current_group_id);
    if (group == nullptr) {
      break;
    }
    for (const auto candidate : group->area_id) {
      if (candidate == 0u) {
        break;
      }
      if (out_count != nullptr) {
        ++*out_count;
      }
      if (candidate == area->id || candidate == area->parent_area) {
        return true;
      }
    }
    current_group_id = group->next_group;
  }
  return false;
}

std::uint32_t UnitStateRuntime::GetPetHappinessLevel() const {
  if (!IsHunterPet()) {
    return 0u;
  }
  const auto *const dbc = owner_.dbc_loader();
  const auto *const personality =
      dbc != nullptr ? dbc->pet_personality().LookupEntry(1u) : nullptr;
  if (personality == nullptr) {
    return 0u;
  }
  const auto happiness = GetPower(4u);
  return happiness < personality->threshold[0]
             ? 0u
         : happiness < personality->threshold[1] ? 1u
                                                  : 2u;
}

bool UnitStateRuntime::IsStaleGuid(const ObjectManager &objects,
                           const ObjectGuid guid) {
  return !guid.IsEmpty() && objects.GetObjectByGUID(guid) == nullptr;
}

bool UnitStateRuntime::HasForcedVehicleTransition() const noexcept {
  return HasSpellStateFlags(0x20000000u);
}

void UnitStateRuntime::SetForcedVehicleTransition(const bool enabled) noexcept {
  enabled ? AddSpellStateFlags(0x20000000u)
          : ClearSpellStateFlags(0x20000000u);
}

void UnitStateRuntime::SetComboPointAuraActive(const bool active) noexcept {
  active ? AddSpellStateFlags(0x00040000u)
         : ClearSpellStateFlags(0x00040000u);
}

void UnitStateRuntime::ResetCreatureMetadata() noexcept {
  creature_metadata_ = {};
}

}

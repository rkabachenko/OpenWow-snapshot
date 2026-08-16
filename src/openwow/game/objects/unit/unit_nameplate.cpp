#include "openwow/game/objects/unit/unit_nameplate.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/objects/unit/unit_relationship_rules.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/world/environment/day_night.h"

#include <array>

namespace openwow::game {

UnitNameplateComponent &CGUnit_C::Nameplate() noexcept { return nameplate_; }

const UnitNameplateComponent &CGUnit_C::Nameplate() const noexcept {
  return nameplate_;
}

namespace {

constexpr std::uint32_t kUnitFlagNotSelectable = 0x02000000u;

constexpr std::uint32_t kUnitFlagImmuneToNpc = 0x00000200u;

constexpr std::uint8_t kUnitVisFlagCreep = 0x02u;

}

bool UnitNameplateComponent::ShouldShow(
    const CGUnit_C &unit, const CGUnit_C &viewer, const ObjectManager &objects,
    const float distance_squared, const bool range_exempt_map) const {
  return PassesHardEligibility(unit, viewer) &&
         PassesCvarVisibility(unit, viewer, objects) &&
         PassesRange(distance_squared, range_exempt_map);
}

bool UnitNameplateComponent::IsFriendlyForNameplate(const CGUnit_C &unit,
                                                    const CGUnit_C &viewer) {
  if (!unit.IsPlayer()) {

    return !viewer.Interaction().CanAttackSpellTarget(unit);
  }

  if (unit.GetGuid() == viewer.GetGuid()) {
    return false;
  }
  if (!viewer.State().GetCharmedBy().IsEmpty() ||
      !unit.State().GetCharmedBy().IsEmpty()) {
    return false;
  }
  const auto *const dbc = viewer.dbc_loader();
  const auto *const viewer_faction =
      dbc != nullptr ? dbc->faction_template().LookupEntry(
                           viewer.State().GetFactionTemplate())
                     : nullptr;
  const auto *const unit_faction =
      dbc != nullptr ? dbc->faction_template().LookupEntry(
                           unit.State().GetFactionTemplate())
                     : nullptr;
  if (viewer_faction != nullptr && unit_faction != nullptr &&
      viewer_faction->faction_group != unit_faction->faction_group) {
    return false;
  }
  return !viewer.Interaction().CanAttackSpellTarget(unit);
}

bool UnitNameplateComponent::PassesHardEligibility(
    const CGUnit_C &unit, const CGUnit_C &viewer) const {
  if (static_cast<std::int32_t>(unit.State().GetHealth()) <= 0 ||
      (unit.State().GetUnitFlags() & kUnitFlagNotSelectable) != 0u ||
      unit.GetGuid() == viewer.GetGuid()) {
    return false;
  }

  if (unit.GetPrimaryM2InstanceId() == 0u) {
    return false;
  }
  const bool friendly = IsFriendlyForNameplate(unit, viewer);
  if (!friendly &&
      (unit.State().GetVisFlags() & kUnitVisFlagCreep) != 0u) {
    return false;
  }

  if (unit.IsPlayer() && !friendly &&
      !viewer.Interaction().CanAttackSpellTarget(unit)) {
    return false;
  }
  const auto creature_type = unit.State().GetCreatureType();
  if (creature_type == CreatureTypeId::kCritter ||
      creature_type == CreatureTypeId::kNonCombatPet ||
      IsPlayerOwnedCritterLootCase(unit)) {
    return false;
  }
  return true;
}

bool UnitNameplateComponent::PassesRange(const float distance_squared,
                                         const bool range_exempt_map) const {

  constexpr float kDefaultRangeSquared = 1681.0f;
  return range_exempt_map || distance_squared <= kDefaultRangeSquared;
}

bool UnitNameplateComponent::PassesCvarVisibility(
    const CGUnit_C &unit, const CGUnit_C &viewer,
    const ObjectManager &objects) const {
  const bool friendly = IsFriendlyForNameplate(unit, viewer);
  const auto &cvars = ui::game::CVarSystem::Instance();
  const auto enabled = [&cvars](const char *const name,
                                const bool fallback) {
    return cvars.Exists(name) ? cvars.GetCVarBool(name) : fallback;
  };

  if (!enabled(friendly ? "nameplateShowFriends" : "nameplateShowEnemies",
               false)) {
    return false;
  }

  const auto owner_guid = unit.State().GetCharmedBy().IsEmpty()
                              ? unit.State().GetCreatedBy()
                              : unit.State().GetCharmedBy();
  const auto *const owner = objects.Get(owner_guid);
  if (owner == nullptr || !owner->IsPlayer()) {
    return true;
  }

  if (!unit.State().GetCreatedBy().IsEmpty() &&
      unit.State().GetSummonedBy().IsEmpty() &&
      (unit.State().GetUnitFlags() & kUnitFlagImmuneToNpc) != 0u) {
    return false;
  }
  const bool guardian = unit.State().GetCharmedBy().IsEmpty() &&
                        unit.State().GetSummonedBy().IsEmpty() &&
                        !unit.State().GetCreatedBy().IsEmpty();
  if (unit.State().GetCreatureType() == CreatureTypeId::kTotem) {
    return enabled(friendly ? "nameplateShowFriendlyTotems"
                            : "nameplateShowEnemyTotems",
                   true);
  }
  if (guardian) {
    return enabled(friendly ? "nameplateShowFriendlyGuardians"
                            : "nameplateShowEnemyGuardians",
                   true);
  }
  return enabled(friendly ? "nameplateShowFriendlyPets"
                          : "nameplateShowEnemyPets",
                 true);
}

namespace {

constexpr float kByteToUnitFloat = 1.0f / 255.0f;
constexpr std::array<float, 4> kSelectionGlowResetColor = {0.0f, 0.0f, 0.0f, 1.0f};

void ApplyPrimaryM2SelectionGlow(CGUnit_C &unit,
                                 const std::array<float, 4> &rgba) {
  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  if (instance_id == 0u) {
    return;
  }

  auto *const m2_system = unit.m2_system();
  if (m2_system == nullptr) {
    return;
  }

  const auto status = m2_system->SetSelectionGlowColor(instance_id, rgba);
  if (render::m2::IsTerminalM2ResultStatus(status)) {
    unit.SetPrimaryM2InstanceId(0u);
  }
}

}

void UnitNameplateComponent::Clear(CGUnit_C &unit,
                                    const std::uint8_t highlight_type) {
  if (!IsValidHighlightType(highlight_type)) {
    return;
  }

  const std::uint32_t bit = 1u << (highlight_type + kHighlightBitBase);
  flags_ &= ~bit;

  const std::uint64_t my_guid =
      unit.GetGuid().IsEmpty() ? 0 : unit.GetGuid().GetRawValue();
  ClearHighlightedGuidIfMatch(my_guid);

  const auto tint = unit.GetModelTintColor();
  const bool has_body_tint_override =
      tint.r != 1.0f || tint.g != 1.0f || tint.b != 1.0f;
  if (has_body_tint_override) {
    return;
  }

  if ((flags_ & kHighlightMask) == 0u) {
    ApplyPrimaryM2SelectionGlow(unit, kSelectionGlowResetColor);
  }
}

void UnitNameplateComponent::Set(CGUnit_C &unit,
                                  const std::uint8_t highlight_type) {
  if (!IsValidHighlightType(highlight_type)) {
    return;
  }

  const std::uint32_t bit = 1u << (highlight_type + kHighlightBitBase);
  flags_ |= bit;

  const auto tint = unit.GetModelTintColor();
  const bool has_body_tint_override =
      tint.r != 1.0f || tint.g != 1.0f || tint.b != 1.0f;
  if (has_body_tint_override) {
    return;
  }

  s_highlighted_guid_ =
      unit.GetGuid().IsEmpty() ? 0 : unit.GetGuid().GetRawValue();

  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  if (instance_id == 0u) {
    return;
  }

  auto *light_env = DayNight_GetLightEnv();
  if (light_env == nullptr) {
    return;
  }

  auto *const m2_system = unit.m2_system();
  if (m2_system == nullptr) {
    return;
  }
  auto &system = *m2_system;
  std::array<float, 4> selection_glow = {
      static_cast<float>(
          light_env->ReadByte(kDayNightNameplateHighlightRedByteOffset)) *
          kByteToUnitFloat,
      static_cast<float>(
          light_env->ReadByte(kDayNightNameplateHighlightGreenByteOffset)) *
          kByteToUnitFloat,
      static_cast<float>(
          light_env->ReadByte(kDayNightNameplateHighlightBlueByteOffset)) *
          kByteToUnitFloat,
      1.0f,
  };

  if ((flags_ & kHighlightMask) == kAlphaPreservingOnlyMask) {
    const auto current_glow = system.QuerySelectionGlowColor(instance_id);
    if (current_glow.status != render::m2::M2ResultStatus::kReady) {
      if (render::m2::IsTerminalM2ResultStatus(current_glow.status)) {
        unit.SetPrimaryM2InstanceId(0u);
      }
      return;
    }
    selection_glow[3] = current_glow.rgba[3];
  }

  const auto status = system.SetSelectionGlowColor(instance_id, selection_glow);
  if (render::m2::IsTerminalM2ResultStatus(status)) {
    unit.SetPrimaryM2InstanceId(0u);
  }
}

}

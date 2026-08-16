#include "openwow/game/targeting/adapters/ui/unit_selection_color_query.h"

#include "openwow/game/battlefield_info.h"
#include "openwow/game/group_system.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/party_stats.h"
#include "openwow/game/unit_defines.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/world_session.h"

#include <algorithm>
#include <array>

namespace openwow::game::targeting::ui {
namespace {

constexpr std::array<UnitSelectionColor, 8> kReactionSelectionColors{
    UnitSelectionColor::FromPackedArgb(0xFFFF0000u),
    UnitSelectionColor::FromPackedArgb(0xFFFF0000u),
    UnitSelectionColor::FromPackedArgb(0xFFFF8000u),
    UnitSelectionColor::FromPackedArgb(0xFFFFFF00u),
    UnitSelectionColor::FromPackedArgb(0xFF00FF00u),
    UnitSelectionColor::FromPackedArgb(0xFF00FF00u),
    UnitSelectionColor::FromPackedArgb(0xFF00FF00u),
    UnitSelectionColor::FromPackedArgb(0xFF00FF00u),
};
constexpr std::array<UnitSelectionColor, 2> kFriendlyPlayerColorsByVariant{
    UnitSelectionColor::FromPackedArgb(0xFF0000FFu),
    UnitSelectionColor::FromPackedArgb(0xFF6060FFu),
};
constexpr auto kNameplatePartyColor =
    UnitSelectionColor::FromPackedArgb(0xFFAAAAFFu);
constexpr auto kNameplatePartyPvpColor =
    UnitSelectionColor::FromPackedArgb(0xFFAAFFAAu);
constexpr auto kNameplateDeadNpcColor =
    UnitSelectionColor::FromPackedArgb(0xFF7F7F7Fu);
constexpr auto kSocialContactColor =
    UnitSelectionColor::FromPackedArgb(0xFF53C9FFu);
constexpr std::uint32_t kPlayerControlledSelectionFlag = 0x00000008u;
constexpr std::uint32_t kVehicleSeatSelectionRedirectFlag = 0x00100000u;

[[nodiscard]] bool UsesNameplatePalette(
    const UnitSelectionColorVariant variant) {
  return variant == UnitSelectionColorVariant::kNameplate;
}

[[nodiscard]] std::size_t PaletteIndex(
    const UnitSelectionColorVariant variant) {
  return UsesNameplatePalette(variant) ? 1u : 0u;
}

[[nodiscard]] UnitSelectionColor ReactionSelectionColor(
    const ReactionType reaction) {
  const auto index = std::min<std::size_t>(
      static_cast<std::size_t>(reaction), kReactionSelectionColors.size() - 1);
  return kReactionSelectionColors[index];
}

[[nodiscard]] const CGUnit_C *ResolveSelectionViewer(
    const WorldSession &session) {
  const auto *active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr) {
    return nullptr;
  }

  const CGUnit_C *viewer = active_player;
  if (const auto *controlled = active_player->GetActiveControlUnit();
      controlled != nullptr) {
    viewer = controlled;
  }

  if (const auto *passenger = viewer->Vehicle().GetVehiclePassengerComponent();
      passenger != nullptr) {
    const auto *seat_entry = passenger->GetSeatEntry();
    if (seat_entry != nullptr &&
        (seat_entry->flags & kVehicleSeatSelectionRedirectFlag) != 0u) {
      if (const auto *vehicle = viewer->Vehicle().GetVehicleUnit(); vehicle != nullptr) {
        viewer = vehicle;
      }
    }
  }

  return viewer;
}

[[nodiscard]] bool HasCachedSelectionPvpFlag(const WorldSession &session,
                                             const ObjectGuid target) {
  if (target.IsEmpty()) {
    return false;
  }

  if (const auto cached =
          session.party_stats().GetCachedMember(target.GetRawValue());
      cached.has_value() &&
      (cached->available_mask & GroupUpdateFlag::kStatus) != 0) {
    return (cached->stats.status & GroupMemberStatus::kPvp) != 0;
  }

  return session.battleground().GetArenaOpponentPvpFlag(
      target.GetRawValue());
}

[[nodiscard]] bool IsArenaBattlefieldActive() {
  const auto &battlefield_info = BattlefieldInfo::Get();
  const auto active_slot = battlefield_info.GetActiveSlotIndex();
  if (active_slot < 0 ||
      static_cast<std::size_t>(active_slot) >= kMaxBGQueueSlots) {
    return false;
  }

  const auto &queue_slot =
      battlefield_info.GetQueueSlot(static_cast<std::size_t>(active_slot));
  return queue_slot.status == 3 && queue_slot.arena_type != 0;
}

[[nodiscard]] UnitSelectionColor ResolveCachedSelectionColor(
    const WorldSession &session, const ObjectGuid target,
    const UnitSelectionColorVariant variant, const bool cached_pvp) {
  if (UsesNameplatePalette(variant)) {
    const auto &group = GroupSystem::Get();
    if (group.IsActivePlayerOrPartyUnitGuid(
            session.objects(), target.GetRawValue()) &&
        group.GetRealPartyMemberCount() > 0) {
      return cached_pvp ? kNameplatePartyPvpColor : kNameplatePartyColor;
    }

    if (session.social().HasContact(target)) {
      return kSocialContactColor;
    }
  }

  if (cached_pvp) {
    return kReactionSelectionColors[
        static_cast<std::size_t>(ReactionType::kFriendly)];
  }

  return kFriendlyPlayerColorsByVariant[PaletteIndex(variant)];
}

}

UnitSelectionColor DefaultUnitSelectionColor(
    const UnitSelectionColorVariant variant) {
  return kFriendlyPlayerColorsByVariant[PaletteIndex(variant)];
}

UnitSelectionColor ResolveUnitSelectionColor(
    const WorldSession &session, const ObjectGuid target,
    const UnitSelectionColorVariant variant) {
  const bool cached_pvp = HasCachedSelectionPvpFlag(session, target);
  if (target.IsEmpty()) {
    return ResolveCachedSelectionColor(session, target, variant, cached_pvp);
  }

  const auto *target_unit = session.objects().GetUnit(target);
  const auto *viewer =
      target_unit != nullptr ? ResolveSelectionViewer(session) : nullptr;
  if (target_unit == nullptr || viewer == nullptr) {
    return ResolveCachedSelectionColor(session, target, variant, cached_pvp);
  }

  if ((target_unit->State().GetUnitFlags() & kPlayerControlledSelectionFlag) != 0) {
    if (target_unit->Interaction().CanAttackSpellTarget(*viewer)) {
      return viewer->Interaction().CanAttackSpellTarget(*target_unit)
                 ? UnitSelectionColor::Hostile()
                 : kFriendlyPlayerColorsByVariant[PaletteIndex(variant)];
    }

    if (viewer->Interaction().CanAttackSpellTarget(*target_unit)) {
      return kReactionSelectionColors[
          static_cast<std::size_t>(ReactionType::kNeutral)];
    }

    const auto target_pvp_flags = target_unit->State().GetPvPFlags();
    const bool use_reaction_color =
        (target_pvp_flags & 0x01u) != 0 &&
        (target_pvp_flags & 0x08u) == 0 &&
        (viewer->State().GetPvPFlags() & 0x08u) == 0 &&
        !IsArenaBattlefieldActive();

    if (UsesNameplatePalette(variant)) {
      const auto &group = GroupSystem::Get();
      if (group.IsActivePlayerOrPartyUnitGuid(
              session.objects(), target.GetRawValue()) &&
          group.GetRealPartyMemberCount() > 0) {
        return use_reaction_color ? kNameplatePartyPvpColor
                                  : kNameplatePartyColor;
      }

      if (session.social().HasContact(target)) {
        return kSocialContactColor;
      }
    }

    return use_reaction_color
               ? ReactionSelectionColor(
                     target_unit->Interaction().GetReaction(*viewer))
               : kFriendlyPlayerColorsByVariant[PaletteIndex(variant)];
  }

  if (UsesNameplatePalette(variant) &&
      (target_unit->State().GetHealth() == 0 ||
       (target_unit->State().GetDynamicFlags() & kUnitDynFlagDead) != 0)) {
    return kNameplateDeadNpcColor;
  }

  return ReactionSelectionColor(
      target_unit->Interaction().GetReaction(*viewer));
}

}

#include "openwow/game/objects/unit/unit_interaction_runtime.h"

#include "openwow/game/objects/cgunit.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/combat/death/adapters/ui/area_spirit_healer_controller.h"
#include "openwow/data/formats/dbc/faction_reaction.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/ceffect_c.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/missile_node.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/unit/unit_relationship_rules.h"
#include "openwow/game/objects/unit/unit_descriptor_view.h"
#include "openwow/game/group_system.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/attack_action_shapeshift.h"
#include "openwow/game/interaction_range.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/objects/unit/unit_movement_runtime.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/petition_frame.h"
#include "openwow/game/quest_dialog_close.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/targeting.h"
#include "openwow/game/taxi_map_frame.h"
#include "openwow/game/trainer_system.h"
#include "openwow/game/world_session.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>

namespace openwow::game {

namespace {

constexpr std::uint32_t kSpellAttrEx4ResumesAutoAttackOnCompletion = 0x4000u;
constexpr std::uint32_t kNpcFlagSpiritHealer = 0x00004000u;
constexpr std::uint32_t kNpcFlagSpiritGuide = 0x00008000u;
constexpr std::uint32_t kNpcFlagStableMaster = 0x00400000u;
constexpr std::uint32_t kNpcFlagGuildBanker = 0x00800000u;
constexpr std::uint32_t kNpcFlagPetitioner = 0x00040000u;
constexpr std::uint32_t kNpcFlagTabardDesigner = 0x00080000u;
constexpr std::uint32_t kNpcFlagBattlemaster = 0x00100000u;

constexpr int kAttackOutOfRangeSystemMessage = 134;

enum class SpeechEmoteSlot : std::size_t {
  kTalk = 0,
  kQuestion = 1,
  kExclamation = 2,
  kYell = 3,
  kLaugh = 4,
  kCount,
};

[[nodiscard]] std::size_t SpeechEmoteSlotIndex(const SpeechEmoteSlot slot) {
  return static_cast<std::size_t>(slot);
}

bool DispatchFriendlyUnitInteraction(WorldSession &session, const CGUnit_C &unit) {
  const auto guid = unit.GetGuid().GetRawValue();
  const auto npc_flags = unit.State().GetNpcFlags();

  if (unit.GetOverlayModelIndexOverride() == kOverlayModelIndexTaxiEnable) {
    session.interaction().SendEnableTaxi(guid);
    if ((npc_flags & UNIT_NPC_FLAG_QUESTGIVER) == 0) {
      return true;
    }
    const auto taxi_gate_overlay_status = unit.GetOverlayDisplayType();
    if (taxi_gate_overlay_status == OverlayDisplayType::kNone ||
        taxi_gate_overlay_status == OverlayDisplayType::kType1) {
      return true;
    }
  }

  if (openwow::diagnostics::IsLogEnabled(
          openwow::diagnostics::LogLevel::kDebug)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kDebug,
        "interaction dispatch npc guid=" + std::to_string(guid) +
            " flags=" + std::to_string(npc_flags));
  }

  if ((npc_flags & UNIT_NPC_FLAG_GOSSIP) != 0) {
    session.interaction().SendGossipHello(guid);
    return true;
  }

  const auto quest_overlay_status = unit.GetOverlayDisplayType();
  if (quest_overlay_status == OverlayDisplayType::kType1) {
    constexpr std::uint32_t kTutorialQuestgiverTooLowLevel = 0x2au;
    TutorialSystem::Instance().TriggerTutorial(kTutorialQuestgiverTooLowLevel);
  }
  if (quest_overlay_status != OverlayDisplayType::kNone &&
      quest_overlay_status != OverlayDisplayType::kType1) {
    session.interaction().SendQuestGiverHello(guid);
    return true;
  }
  if ((npc_flags & UNIT_NPC_FLAG_FLIGHTMASTER) != 0) {
    session.interaction().SendTaxiQueryAvailableNodes(guid);
    return true;
  }
  if ((npc_flags & UNIT_NPC_FLAG_VENDOR) != 0) {
    session.interaction().SendListInventory(guid);
    return true;
  }
  if ((npc_flags & UNIT_NPC_FLAG_TRAINER) != 0) {
    ui::game::RequestTrainerInteraction(session, ObjectGuid(guid));
    return true;
  }
  if ((npc_flags & kNpcFlagSpiritHealer) != 0) {
    session.interaction().SendSpiritHealerActivate(guid);
    return true;
  }
  if ((npc_flags & kNpcFlagSpiritGuide) != 0) {
    combat::death::ui::InteractWithSpiritGuide(session, ObjectGuid(guid));
    return true;
  }
  if ((npc_flags & UNIT_NPC_FLAG_INNKEEPER) != 0) {
    session.interaction().SendGossipHello(guid);
    return true;
  }
  if ((npc_flags & UNIT_NPC_FLAG_BANKER) != 0) {
    session.interaction().SendBankerActivate(guid);
    return true;
  }

  if ((npc_flags & kNpcFlagPetitioner) != 0) {
    session.interaction().SendPetitionShowList(guid);
    return true;
  }
  if ((npc_flags & kNpcFlagTabardDesigner) != 0) {
    (void)PetitionFrame_RequestTabardVendorActivate(session, guid);
    return true;
  }

  if ((npc_flags & kNpcFlagBattlemaster) != 0) {
    session.interaction().SendBattlemasterHello(guid);
    return true;
  }
  if ((npc_flags & UNIT_NPC_FLAG_AUCTIONEER) != 0) {
    session.interaction().SendAuctionHello(guid);
    return true;
  }
  if ((npc_flags & kNpcFlagStableMaster) != 0) {
    session.interaction().SendListStabledPets(guid);
    return true;
  }
  if ((npc_flags & kNpcFlagGuildBanker) != 0) {
    session.interaction().SendGuildBankerActivate(guid);
    return true;
  }

  return false;
}

void StartUnitApproach(WorldSession &session, const CGUnit_C &unit,
                       const CGPlayer_C &active_player) {
  const auto position = unit.GetPosition();
  const float to_target_x = position.x - active_player.GetX();
  const float to_target_y = position.y - active_player.GetY();
  const float to_target_z = position.z - active_player.GetZ();
  const float distance_squared = to_target_x * to_target_x +
                                 to_target_y * to_target_y +
                                 to_target_z * to_target_z;

  constexpr int kLootInteractionActionType = 6;
  if (interaction_range::ExceedsInteractionWarningDistance(
          kLootInteractionActionType, distance_squared)) {
    constexpr int kInteractionTooFarMessageId = 0x139;
    ui::game::DisplaySystemMessage(kInteractionTooFarMessageId);
    return;
  }

  if (static_cast<std::int32_t>(unit.State().GetHealth()) <= 0) {
    session.click_to_move().LootTarget(
        unit.GetGuid(), position.x, position.y, position.z,
        interaction_range::ComputeUnitInteractionApproachStopDistance(
            active_player.State().GetCombatReach(),
            unit.State().GetCombatReach()));
    return;
  }

  session.click_to_move().InteractWith(
      unit.GetGuid(), position.x, position.y, position.z,
      interaction_range::ComputeFriendlyInteractApproachStopDistance(
          unit.State().GetCombatReach()));
}

[[nodiscard]] bool IsFriendlyUnitInteractionInRangeOrStartAutoApproach(
    WorldSession &session, const CGUnit_C &unit, const CGPlayer_C &active_player) {
  if (unit.State().GetNpcFlags() == 0u) {
    return true;
  }

  const double interaction_range_squared =
      ui::game::detail::GetUnitInteractionRangeSquared(active_player, unit);

  const float distance = active_player.GetDistance(unit);
  if (distance * distance <= interaction_range_squared) {
    return true;
  }
  if (static_cast<std::int32_t>(active_player.State().GetHealth()) <= 0 ||
      !active_player.IsActiveMover() || !session.click_to_move().IsEnabled()) {
    return false;
  }

  StartUnitApproach(session, unit, active_player);
  return false;
}

void CloseLootForActiveMovement(WorldSession &session) {
  CloseActiveLootWindow(
      session, CloseLootWindowOptions{.send_release = true,
                                      .skip_item_check = true,
                                      .show_interrupted = false,
                                      .clear_dead_target = true});
}

bool TryResolveGatherInteractionRange(const WorldSession &session,
                                      const std::uint32_t spell_id,
                                      const CGUnit_C &caster,
                                      float *out_max_range) {
  if (spell_id == 0 || out_max_range == nullptr) {
    return false;
  }

  if (const auto query = SpellQueryBridge::Get().Query(spell_id);
      query.has_value() && query->range > 0.0f) {
    *out_max_range = query->range;
    return true;
  }

  const auto *dbc = caster.dbc_loader();
  if (dbc == nullptr) {
    return false;
  }

  const auto *spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return false;
  }

  const auto *range_entry =
      spell->range_index != 0 ? dbc->spell_range().LookupEntry(spell->range_index) : nullptr;
  const auto range_window =
      SpellTargetValidator::GetUntargetedRangeWindow(*spell, range_entry, caster, false, &session);
  if (range_window.max_range <= 0.0f) {
    return false;
  }

  *out_max_range = range_window.max_range;
  return true;
}

ObjectGuid GetImmediateControllerGuid(const CGUnit_C &unit) {
  const auto charmed_by = unit.State().GetCharmedBy();
  return !charmed_by.IsEmpty() ? charmed_by : unit.State().GetCreatedBy();
}

ReactionType ClampReactionLevel(const std::uint32_t raw_level) {
  return static_cast<ReactionType>(std::min<std::uint32_t>(
      raw_level, static_cast<std::uint32_t>(ReactionType::kExalted)));
}

bool IsActivePlayerController(const CGUnit_C *controller) {
  return controller != nullptr &&
         controller->GetGuid() == CGObject_C::GetActivePlayerGuid();
}

[[nodiscard]] ObjectGuid ResolveActivePlayerFactionRefreshExclusionGuid(
    const CGUnit_C &unit) {
  const auto charm_guid = unit.State().GetCharmedUnitGUID();
  return !charm_guid.IsEmpty() ? charm_guid : unit.State().GetCharmedBy();
}

void FireUnitFactionRefresh(const CGUnit_C &unit) {
  ui::game::ScriptEventDispatch::Get().FireUnitFaction(unit.GetGuid().GetRawValue());
}

void RefreshVisibleFactionForActivePlayer(const CGUnit_C &context_unit) {
  const auto* const objects = context_unit.object_manager();
  if (objects == nullptr) {
    return;
  }

  const auto active_player_guid = objects->GetActivePlayerGuid();
  if (active_player_guid.IsEmpty()) {
    return;
  }

  objects->EnumVisibleObjects([objects, active_player_guid](const WorldObject &object) {
    if (!object.IsUnit()) {
      return true;
    }

    const auto *unit = objects->GetUnit(object.GetGuid());
    if (unit == nullptr || unit->GetGuid() == active_player_guid ||
        ResolveActivePlayerFactionRefreshExclusionGuid(*unit) ==
            active_player_guid) {
      return true;
    }

    FireUnitFactionRefresh(*unit);
    return true;
  });
}

void RefreshVisibleFactionLinkedToUnit(const CGUnit_C &context_unit) {
  const auto context_guid = context_unit.GetGuid();
  if (context_guid.IsEmpty()) {
    return;
  }

  const auto* const objects = context_unit.object_manager();
  if (objects == nullptr) {
    return;
  }

  objects->EnumVisibleObjects([objects, context_guid](const WorldObject &object) {
    if (!object.IsUnit()) {
      return true;
    }

    const auto *unit = objects->GetUnit(object.GetGuid());

    if (unit == nullptr || GetImmediateControllerGuid(*unit) != context_guid ||
        (unit->State().GetUnitFlags2() & 0x100u) != 0u) {
      return true;
    }

    FireUnitFactionRefresh(*unit);
    return true;
  });
}

std::optional<ReactionType> ResolveReactionTowardActivePlayerControlledUnit(
    const data::dbc::FactionTemplateEntry &source_entry,
    const CGUnit_C &target) {
  if (!target.Interaction().IsPlayerControlled() ||
      !IsActivePlayerController(target.Interaction().ResolveControllingPlayer())) {
    return std::nullopt;
  }
  const auto *const dbc = target.dbc_loader();
  if (dbc == nullptr) {
    return std::nullopt;
  }
  auto &reputation = ReputationInfo::Get();
  reputation.BindDbc(dbc);
  const auto faction_id = static_cast<std::int32_t>(source_entry.faction);
  if (const auto forced =
          reputation.FindForcedReactionStanding(source_entry.faction);
      forced.has_value()) {
    return ClampReactionLevel(*forced);
  }
  if ((target.State().GetUnitFlags2() & 0x4u) != 0u ||
      !reputation.HasReputationList(faction_id)) {
    return std::nullopt;
  }
  return ClampReactionLevel(
      static_cast<std::uint32_t>(reputation.GetStandingLevel(faction_id)));
}

std::optional<ReactionType> ResolveReactionFromActivePlayerControlledUnit(
    const CGUnit_C &viewer,
    const data::dbc::FactionTemplateEntry &target_entry,
    const CGUnit_C &target) {
  if (!viewer.Interaction().IsPlayerControlled() ||
      !IsActivePlayerController(viewer.Interaction().ResolveControllingPlayer())) {
    return std::nullopt;
  }
  const auto *const dbc = viewer.dbc_loader();
  if (dbc == nullptr) {
    return std::nullopt;
  }
  auto &reputation = ReputationInfo::Get();
  reputation.BindDbc(dbc);
  const auto faction_id = static_cast<std::int32_t>(target_entry.faction);
  if (const auto forced =
          reputation.FindForcedReactionStanding(target_entry.faction);
      forced.has_value()) {
    return ClampReactionLevel(*forced);
  }
  if ((target.State().GetUnitFlags2() & 0x4u) != 0u ||
      !reputation.HasReputationList(faction_id)) {
    return std::nullopt;
  }
  return reputation.IsAtWar(faction_id) ? ReactionType::kHostile
                                         : ReactionType::kFriendly;
}

}

void UnitInteractionRuntime::SetCachedInteractRange(const float range) noexcept {
  cached_interact_range_ = range;
}

std::uint32_t UnitInteractionRuntime::DetermineCursorInteractionBits(
    const std::uint32_t filter) const {
  if (owner_.Movement().IsLocallyControlled()) {
    return filter & 1;
  }

  bool is_player_type = (owner_.GetTypeMask() & 0x10) != 0;
  if (is_player_type) {
    ObjectGuid summoner = owner_.State().GetSummonedBy();
    if (!summoner.IsEmpty()) {
      return (filter >> 7) & 1;
    }
    return (filter >> 4) & 1;
  }

  if (IsPlayerOwnedCritterLootCase(owner_)) {
    return (filter >> 10) & 1;
  }

  ObjectGuid charmer = owner_.State().GetCharmedBy();
  if (!charmer.IsEmpty()) {
    CreatureTypeId ctype = owner_.State().GetCreatureType();
    if (ctype == CreatureTypeId::kCritter || ctype == CreatureTypeId::kNonCombatPet) {
      return (filter >> 10) & 1;
    }

    if (owner_.State().IsDead() && (owner_.State().GetUnitFlags() & 0x200) != 0) {
      return (filter >> 10) & 1;
    }

    if (ctype == CreatureTypeId::kTotem) {
      return (filter >> 6) & 1;
    }

    ObjectGuid my_summoner = owner_.State().GetSummonedBy();
    if (!my_summoner.IsEmpty()) {
      return (filter >> 8) & 1;
    }

    return (filter >> 5) & 1;
  }

  std::uint8_t pvp_byte = owner_.State().GetPvPFlags();
  if ((pvp_byte & 0x02) != 0) {
    return 0;
  }

  CreatureTypeId ctype = owner_.State().GetCreatureType();
  if (ctype == CreatureTypeId::kCritter || ctype == CreatureTypeId::kNonCombatPet) {
    return (filter >> 10) & 1;
  }

  return (filter >> 1) & 1;
}

bool UnitInteractionRuntime::IsSelectableOrOwnedPet(
    const WorldSession &session) const {
  if (!owner_.State().IsNotSelectable()) {
    return true;
  }

  if (owner_.State().GetCreatedBy() != CGObject_C::GetActivePlayerGuid()) {
    return false;
  }

  const auto &pet_guids = session.pet().pet_guids();
  if (pet_guids.empty()) {
    return false;
  }

  return owner_.GetGuid().GetRawValue() == pet_guids[0];
}

void UnitInteractionRuntime::RightClickInteract(
    WorldSession *session, TargetingSystem *targeting) const {
  if (!session) {
    return;
  }

  const auto *player_obj = session->objects().GetActivePlayer();
  if (player_obj == nullptr) {
    return;
  }

  if (owner_.State().IsLootableCorpseNow()) {

    const double loot_range_squared =
        ui::game::detail::GetUnitInteractionRangeSquared(*player_obj, owner_);
    const float loot_distance = player_obj->GetDistance(owner_);
    if (static_cast<double>(loot_distance) * loot_distance >
        loot_range_squared) {
      if (static_cast<std::int32_t>(player_obj->State().GetHealth()) > 0 &&
          player_obj->IsActiveMover() &&
          session->click_to_move().IsEnabled()) {
        StartUnitApproach(*session, owner_, *player_obj);
        return;
      }
    }
    PrepareAutoLootInteraction(
        *session, IsAutoLootEnabled(session->binding_profiles()));
    session->interaction().SendLoot(owner_.GetGuid().GetRawValue());
    return;
  }

  const auto &player = *player_obj;
  const bool is_hostile = player.Interaction().IsHostileTo(owner_) ||
                          IsHostileTo(player);
  if (!is_hostile) {
    if (!IsFriendlyUnitInteractionInRangeOrStartAutoApproach(
            *session, owner_, player)) {
      return;
    }
    CompleteRightClickInteraction(*session);
    return;
  }

  if (targeting != nullptr) {

    const auto outcome = targeting->StartAttack(owner_.GetGuid().GetRawValue());
    if (outcome.result == AttackStartResult::kInvalidTarget) {
      owner_.sound_runtime().PlayAmbientIdleSound(owner_.GetGuid().GetRawValue());
    } else if (outcome.result == AttackStartResult::kRangeRejected) {

      ui::game::DisplaySystemMessage(kAttackOutOfRangeSystemMessage);
    }
  }
}

void UnitInteractionRuntime::CompleteRightClickInteraction(
    WorldSession &session) const {
  const auto *player_object = session.objects().GetLocalPlayer();
  if (player_object == nullptr || !player_object->IsUnit()) {
    return;
  }

  const auto &player = static_cast<const CGUnit_C &>(*player_object);
  if (player.Interaction().IsHostileTo(owner_) || IsHostileTo(player) ||
      !DispatchFriendlyUnitInteraction(session, owner_)) {
    return;
  }

  if (auto *active_player = session.objects().GetActivePlayer();
      active_player != nullptr) {
    active_player->Animation().TryPlaySpeechEmoteSlot(
        static_cast<std::uint32_t>(
            SpeechEmoteSlotIndex(SpeechEmoteSlot::kTalk)));
  }
}

bool UnitInteractionRuntime::GetInteractionRangeSquared(
    const WorldSession &session, const ObjectGuid target_guid,
    const int action_type, float *out_range_sq) const {

  const auto *params =
      interaction_range::LookupInteractionActionParameters(action_type);
  if (params == nullptr || !params->enabled || params->suppress_range_resolution) {
    return true;
  }

  const auto *const objects = owner_.object_manager();

  switch (action_type) {
  case 3: {

    constexpr float kGossipRangeSq = 9.0f;
    *out_range_sq = kGossipRangeSq;
    return true;
  }
  case 4: {

    constexpr float kTaxiRangeSq = 0.25f;
    *out_range_sq = kTaxiRangeSq;
    return true;
  }
  case 5: {

    if (const auto *target = objects != nullptr ? objects->GetUnit(target_guid) : nullptr) {
      const float reach = target->State().GetCombatReach();
      const float range = reach * 0.5f + 2.0f;
      *out_range_sq = range * range;
    } else {

      constexpr float kDefaultMeleeRangeSq = 2.7777779f * 2.7777779f;
      *out_range_sq = kDefaultMeleeRangeSq;
    }
    return true;
  }
  case 6:
  case 10: {

    if (const auto *target = objects != nullptr ? objects->GetUnit(target_guid) : nullptr) {
      *out_range_sq = interaction_range::ComputeSpellInteractionRangeSquared(
          owner_.State().GetCombatReach(), target->State().GetCombatReach());
    } else {
      constexpr float kFallbackRange = 5.0f;
      *out_range_sq = kFallbackRange * kFallbackRange;
    }
    return true;
  }
  case 7: {

    *out_range_sq = cached_interact_range_ * cached_interact_range_;
    return true;
  }
  case 9: {

    const auto *target = objects != nullptr ? objects->Get(target_guid) : nullptr;
    if (target == nullptr) {
      return false;
    }

    if (objects == nullptr) {
      return false;
    }

    const auto spell_id =
        SpellbookSystem::Get().ResolveGatherInteractionSpellId(*target, &objects->query_cache());
    float max_range = 0.0f;
    if (spell_id == 0 ||
        !TryResolveGatherInteractionRange(session, spell_id, owner_, &max_range)) {
      return false;
    }

    const float interaction_range = max_range * 0.9f;
    *out_range_sq = interaction_range * interaction_range;
    return true;
  }
  case 11: {

    const auto *target = objects != nullptr ? objects->GetUnit(target_guid) : nullptr;
    if (target == nullptr) {
      return false;
    }

    constexpr std::uint32_t kDuelSpellId = 7266;
    float max_range = 0.0f;
    if (!TryResolveGatherInteractionRange(session, kDuelSpellId, owner_, &max_range)) {
      return false;
    }

    const float interaction_range = max_range * 0.9f;
    *out_range_sq = interaction_range * interaction_range;
    return true;
  }
  default:
    return false;
  }
}

bool IsActivePlayerControlledGroupLink(const CGUnit_C &left,
                                       const CGUnit_C &right,
                                       const ControlledGroupScope scope,
                                       const ObjectManager &objects) {
  if (!left.Interaction().IsPlayerControlled() ||
      !right.Interaction().IsPlayerControlled()) {
    return false;
  }
  const auto *const left_controller = left.Interaction().ResolveControllingPlayer();
  const auto *const right_controller = right.Interaction().ResolveControllingPlayer();
  if (left_controller == nullptr || right_controller == nullptr) {
    return false;
  }
  const auto active_player_guid = CGObject_C::GetActivePlayerGuid();
  const auto &groups = GroupSystem::Get();
  const auto is_member = [&groups, &objects, scope](const ObjectGuid guid) {
    return scope == ControlledGroupScope::kParty
               ? groups.IsActivePlayerOrPartyUnitGuid(objects,
                                                       guid.GetRawValue())
               : groups.IsActivePlayerPartyOrRaidUnitGuid(
                     objects, guid.GetRawValue());
  };
  if (left_controller->GetGuid() == active_player_guid) {
    return is_member(right_controller->GetGuid());
  }
  if (right_controller->GetGuid() == active_player_guid) {
    return is_member(left_controller->GetGuid());
  }
  return false;
}

bool IsPlayerOwnedCritterLootCase(const CGUnit_C &unit) {
  const UnitDescriptorView descriptor(unit);
  auto relationship_guid = descriptor.CharmedUnit();
  if (relationship_guid.IsEmpty()) {
    relationship_guid = descriptor.Critter();
  }
  const auto *const objects = unit.object_manager();
  const auto *const relationship =
      objects != nullptr ? objects->GetUnit(relationship_guid) : nullptr;
  return !relationship_guid.IsEmpty() && relationship != nullptr &&
         relationship->IsPlayer() && !descriptor.Critter().IsEmpty() &&
         descriptor.Pet().IsEmpty() &&
         (descriptor.UnitFlags() & 0x200u) != 0u;
}

void UnitInteractionRuntime::CompleteAutoAttackInteraction(
    const bool stop_facing, const bool send_stop) {
  auto *const objects = owner_.object_manager();
  if (objects == nullptr) {
    return;
  }
  auto &control = objects->player_control();
  if (owner_.GetGuid().GetRawValue() !=
          control.active_mover_guid ||
      auto_attack_type_ == kAutoAttackTypeIdle) {
    return;
  }

  const bool ctm_type_drives_movement =
      auto_attack_type_ >= 3u && auto_attack_type_ <= 11u;
  const bool target_resolvable =
      !auto_attack_target_.IsEmpty() &&
      objects->GetUnit(auto_attack_target_) != nullptr;
  if (!stop_facing || !target_resolvable) {
    if (send_stop) {
      const std::uint32_t timestamp = core::GameClock::GetTickCount32();
      if (ctm_type_drives_movement) {
        owner_.Movement().InputControlStopForward(timestamp);
      }
      owner_.Movement().InputControlClearClickToMoveFacingFlags(timestamp);
    }
  }
  auto_attack_target_ = ObjectGuid();
  control.movement_interaction_flags = 0u;
  auto_attack_type_ = kAutoAttackTypeIdle;
}

bool UnitInteractionRuntime::MatchesActiveMoverInteractionMask(
    const std::uint32_t interaction_mask) const {
  if (auto_attack_type_ == kAutoAttackTypeIdle || auto_attack_type_ >= 32u) {
    return false;
  }
  const auto *const objects = owner_.object_manager();
  if (objects == nullptr) {
    return false;
  }
  return owner_.GetGuid().GetRawValue() ==
              objects->player_control().active_mover_guid &&
          ((1u << auto_attack_type_) & interaction_mask) != 0u;
}

bool UnitInteractionRuntime::SpellResumesAutoAttackOnCompletion(
    const std::uint32_t spell_id) const {
  if (spell_id == 0u) {
    return false;
  }
  const auto spell = SpellQueryBridge::Get().Query(spell_id);

  return spell.has_value() &&
         (spell->attributesEx4 & kSpellAttrEx4ResumesAutoAttackOnCompletion) != 0u;
}

bool UnitInteractionRuntime::IsAutoAttacking() const noexcept {
  return auto_attack_type_ != kAutoAttackTypeIdle;
}

void UnitInteractionRuntime::HandleMovementArrival() {
  switch (auto_attack_type_) {
  case 4:
    CompleteAutoAttackInteraction(true, true);
    break;
  case 5:
    pending_follow_target_ = auto_attack_target_;
    CompleteAutoAttackInteraction(true, true);
    break;
  case 6:
    pending_spell_target_ = auto_attack_target_;
    CompleteAutoAttackInteraction(true, true);
    break;
  case 7:
    pending_loot_target_ = auto_attack_target_;
    CompleteAutoAttackInteraction(true, true);
    break;
  case 9:
    pending_object_interact_target_ = auto_attack_target_;
    CompleteAutoAttackInteraction(true, true);
    break;
  case 10:
  case 11:
    pending_attack_target_ = auto_attack_target_;
    CompleteAutoAttackInteraction(true, true);
    break;
  default:
    owner_.Movement().InputControlStopForward(core::GameClock::GetTickCount32());
    break;
  }
}

bool UnitInteractionRuntime::IsFriendlyTo(const CGUnit_C &other) const {
  return GetReaction(other) >= ReactionType::kFriendly;
}

ReactionType UnitInteractionRuntime::GetReaction(const CGUnit_C &other,
                                                 ReactionMemo &memo) const {
  if (!memo.resolved) {
    memo.reaction = GetReaction(other);
    memo.resolved = true;
  }
  return memo.reaction;
}

ReactionType UnitInteractionRuntime::GetReaction(const CGUnit_C &other) const {
  if (owner_.GetGuid() == other.GetGuid()) {
    return ReactionType::kFriendly;
  }
  const auto my_faction = owner_.State().GetFactionTemplate();
  const auto their_faction = other.State().GetFactionTemplate();
  if (my_faction == 0u || their_faction == 0u) {
    return ReactionType::kNeutral;
  }
  const auto *const dbc = owner_.dbc_loader();
  if (dbc == nullptr) {
    return ReactionType::kNeutral;
  }
  const auto &faction_template = dbc->faction_template();
  const auto *const my_entry = faction_template.LookupEntry(my_faction);
  const auto *const their_entry = faction_template.LookupEntry(their_faction);
  if (my_entry == nullptr || their_entry == nullptr) {
    return ReactionType::kNeutral;
  }

  if (const auto override =
          ResolveReactionTowardActivePlayerControlledUnit(*my_entry, other);
      override.has_value()) {
    return *override;
  }
  if (const auto override = ResolveReactionFromActivePlayerControlledUnit(
           owner_, *their_entry, other);
      override.has_value()) {
    return *override;
  }

  if (my_faction == their_faction) {

    return ReactionType::kFriendly;
  }
  return data::dbc::ComputeFactionReactionForEntries(*my_entry, *their_entry);
}

bool UnitInteractionRuntime::IsHostileTo(const CGUnit_C &other) const {
  return GetReaction(other) <= ReactionType::kHostile;
}

bool UnitInteractionRuntime::IsNeutralOrCivilian(const CGUnit_C &other) const {
  return GetReaction(other) >= ReactionType::kFriendly || owner_.State().IsCivilian();
}

int UnitInteractionRuntime::GetCorpseReactionLevel(const CGCorpse_C &corpse) const {
  const auto *const dbc = owner_.dbc_loader();
  if (dbc == nullptr) {
    return static_cast<int>(ReactionType::kNeutral);
  }
  return data::dbc::ComputeCorpseReactionLevel(
      owner_.State().GetFactionTemplate(), corpse.GetFactionTemplate(),
      dbc->faction_template());
}

bool UnitInteractionRuntime::IsFriendlyCorpseTarget(const CGCorpse_C &corpse) const {
  return GetCorpseReactionLevel(corpse) >=
         static_cast<int>(ReactionType::kFriendly);
}

bool UnitInteractionRuntime::IsNeutralGameObjectTarget(
    const CGGameObject_C &object) const {
  return object.GetReactionLevel(owner_) >= 3;
}

bool UnitInteractionRuntime::IsPlayerControlled() const {
  return (owner_.State().GetUnitFlags() & 0x8u) != 0u;
}

bool UnitInteractionRuntime::MatchesImmediateControllerGuid(
    const ObjectGuid guid) const {
  if (guid.IsEmpty() || !IsPlayerControlled()) {
    return false;
  }
  const auto controller = GetImmediateControllerGuid(owner_);
  return !controller.IsEmpty() && controller == guid;
}

ObjectGuid UnitInteractionRuntime::GetControllingPlayerGuid() const {
  const auto direct_controller = GetImmediateControllerGuid(owner_);
  if (direct_controller.IsEmpty()) {
    return owner_.IsPlayer() ? owner_.GetGuid() : ObjectGuid();
  }
  const auto *const objects = owner_.object_manager();
  if (objects == nullptr) {
    return ObjectGuid();
  }
  const auto *const controller = objects->GetUnit(direct_controller);
  if (controller == nullptr) {
    return ObjectGuid();
  }
  if (controller->IsPlayer()) {
    return controller->GetGuid();
  }
  const auto nested_controller = GetImmediateControllerGuid(*controller);
  if (nested_controller.IsEmpty()) {
    return ObjectGuid();
  }
  const auto *const nested = objects->GetUnit(nested_controller);
  return nested != nullptr && nested->IsPlayer() ? nested->GetGuid()
                                                  : ObjectGuid();
}

CGUnit_C *UnitInteractionRuntime::ResolveControllingPlayer() const {
  const ObjectGuid controller = GetControllingPlayerGuid();
  if (controller.IsEmpty()) {
    return nullptr;
  }

  if (controller == owner_.GetGuid() && owner_.IsPlayer()) {
    return &owner_;
  }

  auto *const objects = owner_.object_manager();
  auto *controller_unit = objects != nullptr ? objects->GetMutableUnit(controller) : nullptr;
  if (controller_unit != nullptr && controller_unit->IsPlayer()) {
    return controller_unit;
  }

  return nullptr;
}

void UnitInteractionRuntime::RefreshFactionDependentState(
    WorldSession &session, const bool refresh_linked_visible_units) const {
  FireUnitFactionRefresh(owner_);

  if (owner_.GetGuid() == CGObject_C::GetActivePlayerGuid()) {
    session.RequestVisibleQuestgiverStatusRefresh();
    RefreshVisibleFactionForActivePlayer(owner_);
    return;
  }

  if (refresh_linked_visible_units) {
    RefreshVisibleFactionLinkedToUnit(owner_);
  }
}

void UnitInteractionRuntime::RefreshLinkedVisibleUnitFactionState() const {
  RefreshVisibleFactionLinkedToUnit(owner_);
}

void UnitInteractionRuntime::HandleAliveStateTransition(
    WorldSession &session, const bool suppress_player_alive_event) {
  if (!death_state_active_) {
    return;
  }
  death_state_active_ = false;
  if (owner_.GetGuid() == CGObject_C::GetActivePlayerGuid()) {
    (void)owner_.sound_runtime().UpdateLiquidAmbience(0.5);
  }

  owner_.Loot().ClearCorpseReadyTick();
  owner_.Animation().ResetAuraAnimationVisualState(session);
  owner_.Animation().ResetDeathPlaybackForAliveTransition(session);

  if (owner_.GetGuid() != CGObject_C::GetActivePlayerGuid()) {
    return;
  }

  session.HandleActivePlayerAliveTransition(suppress_player_alive_event);
}

void UnitInteractionRuntime::CancelSpellCastsOnUnitDeath(WorldSession &session) {

  if ((owner_.State().GetDynamicFlags() &
       static_cast<std::uint32_t>(UnitDynFlag::kUnitDynFlagDead)) != 0u &&
      GroupSystem::Get().IsActivePlayerPartyOrRaidUnitGuid(
          session.objects(), owner_.GetGuid().GetRawValue())) {
    return;
  }
  session.spells().CancelSpellCastsTargeting(session, owner_.GetGuid());
}

void UnitInteractionRuntime::HandleDeathStateTransition(WorldSession &session) {
  if (death_state_active_) {
    return;
  }
  death_state_active_ = true;
  owner_.Movement().StopLocomotionForDeath(session);
  owner_.Loot().ClearCorpseReadyTick();
  owner_.Animation().ResetAuraAnimationVisualState(session);
  owner_.SpellVisuals().ClearCreatureInfo();
  CompleteAutoAttackInteraction(false, true);
  CancelSpellCastsOnUnitDeath(session);
  if (owner_.GetGuid() == CGObject_C::GetActivePlayerGuid()) {

    session.RefreshActivePlayerReleaseTimerMode();
    ApplyActivePlayerDeathSideEffects(session);
    session.HandleActivePlayerDeadTransition();
  }
  DrainAttachedEffectNodesForDeath();
}

void UnitInteractionRuntime::DrainAttachedEffectNodesForDeath() {
  for (auto *node = *owner_.GetEffectNodeListHeadSlot(); node != nullptr;) {
    auto *const next = node->GetNextAttachedEffect();
    node->ReleaseLoopingLightningHandles();
    node->BeginTeardown();
    node = next;
  }
  CEffect_C::ProcessTeardownList();
}

void UnitInteractionRuntime::ApplyActivePlayerDeathSideEffects(
    WorldSession &session) {
  if (auto *const targeting = session.targeting_system(); targeting != nullptr) {
    targeting->ClearTarget();
  }
  session.objects().SetMouseover(ObjectGuid());
  ui::game::SetNpcInteractionTarget({});
  session.spells().CancelAllLocalPlayerCasts(session);

  constexpr std::uint32_t kUnitFlagTaxiFlight = 0x00100000u;
  if ((owner_.State().GetUnitFlags() & kUnitFlagTaxiFlight) != 0u) {
    session.interaction().SendRepopRequest(false);
  }
}

void UnitInteractionRuntime::OnNPCInteractionFlagsChanged(
    WorldSession &session, const std::uint32_t new_flags) {
  const std::uint32_t old_flags = cached_npc_interaction_flags_;
  const std::uint32_t changed   = old_flags ^ new_flags;
  cached_npc_interaction_flags_  = new_flags;

  if (changed == 0) {
    return;
  }

  const std::uint64_t my_guid = owner_.GetGuid().GetRawValue();

  if ((changed & 0x1) != 0 && (new_flags & 0x1) == 0) {

    if (session.gossip().interaction_guid().GetRawValue() == my_guid) {
      ui::game::CloseGossipInteraction(session);
    }
  }

  if ((changed & 0x2) != 0) {
    if ((new_flags & 0x2) == 0) {
      {
        const auto &qf_state = session.quests().quest_frame_interaction_state();
        if (qf_state.interaction_guid.GetRawValue() == my_guid) {
          auto close_state =
              GetActiveQuestDialogCloseState(session.quests());
          CloseQuestDialogLikeIda58CA70(session, close_state,
                                        false,
                                        false);
        }
      }
    }
  }

  if ((changed & 0x2000) != 0) {
    if ((new_flags & 0x2000) == 0) {
      if (GetTaxiMapFrameNpcGuid(session.taxi()) == my_guid) {
        TaxiMapFrame_Close(session.taxi());
      }
    }
  }

  if ((changed & 0x10) != 0 && (new_flags & 0x10) == 0) {
    if (TrainerSystem::Get().GetTrainerGuid() == my_guid) {
      TrainerSystem::Get().CloseTrainer();
    }
  }

  if ((changed & 0x20000) != 0 && (new_flags & 0x20000) == 0) {
    if (session.bank_npc_guid() == my_guid) {
      ui::game::SetBankInteractionTarget(session, {});
    }
  }

  if ((changed & 0x800000) != 0 && (new_flags & 0x800000) == 0) {
    if (GuildSystem::Get().GetBankerGuid() == my_guid) {
      GuildSystem::Get().CloseBankFrame();
    }
  }

  if ((changed & 0x4000000) != 0 && (new_flags & 0x4000000) == 0) {
    if (session.mail().mailbox_guid() == my_guid) {
      session.mail().CloseMailbox(false);
      if (session.mail().ConsumeNextMailTimeQueryRequest()) {
        session.interaction().SendQueryNextMailTime();
      }
    }
  }
}

bool UnitInteractionRuntime::IsInSamePartyOrControlledParty(
    const CGUnit_C &other) const {
  if (owner_.GetGuid() == other.GetGuid()) {
    return true;
  }
  const auto *const objects = owner_.object_manager();
  return objects != nullptr && IsActivePlayerControlledGroupLink(
                                    owner_, other, ControlledGroupScope::kParty,
                                   *objects);
}

bool UnitInteractionRuntime::IsInSameRaidOrControlledRaid(
    const CGUnit_C &other) const {
  constexpr std::uint32_t kSameRaidCreatureTemplateTypeFlag = 0x04000000u;
  if (owner_.GetGuid() == other.GetGuid() ||
      other.State().HasCreatureTemplateTypeFlag(kSameRaidCreatureTemplateTypeFlag)) {
    return true;
  }
  const auto *const objects = owner_.object_manager();
  return objects != nullptr && IsActivePlayerControlledGroupLink(
                                    owner_, other, ControlledGroupScope::kRaid,
                                   *objects);
}

UnitInteractionRuntime::GroupRelation UnitInteractionRuntime::ResolveGroupRelation(
    const CGUnit_C &other) const {

  constexpr std::uint32_t kSameRaidCreatureTemplateTypeFlag = 0x04000000u;
  if (owner_.GetGuid() == other.GetGuid()) {
    return {.same_party = true, .same_raid = true};
  }
  GroupRelation relation;
  relation.same_raid =
      other.State().HasCreatureTemplateTypeFlag(kSameRaidCreatureTemplateTypeFlag);
  const auto *const objects = owner_.object_manager();
  if (objects == nullptr || !IsPlayerControlled() ||
      !other.Interaction().IsPlayerControlled()) {
    return relation;
  }
  const auto *const owner_controller = ResolveControllingPlayer();
  const auto *const other_controller = other.Interaction().ResolveControllingPlayer();
  if (owner_controller == nullptr || other_controller == nullptr) {
    return relation;
  }
  const auto active_player_guid = CGObject_C::GetActivePlayerGuid();
  const CGUnit_C *probed = nullptr;
  if (owner_controller->GetGuid() == active_player_guid) {
    probed = other_controller;
  } else if (other_controller->GetGuid() == active_player_guid) {
    probed = owner_controller;
  } else {
    return relation;
  }
  const auto &groups = GroupSystem::Get();
  const std::uint64_t probed_guid = probed->GetGuid().GetRawValue();
  relation.same_party = groups.IsActivePlayerOrPartyUnitGuid(*objects, probed_guid);
  relation.same_raid = relation.same_raid || relation.same_party ||
                       groups.IsRaidUnitGuid(*objects, probed_guid);
  return relation;
}

bool UnitInteractionRuntime::CanAssistSpellTarget(
    const CGUnit_C &target, const bool ignore_flag_check) const {
  ReactionMemo memo;
  return CanAssistSpellTarget(target, ignore_flag_check, memo);
}

bool UnitInteractionRuntime::CanAssistSpellTarget(
    const CGUnit_C &target, const bool ignore_flag_check,
    ReactionMemo &memo) const {
  const std::uint32_t target_flags = target.State().GetUnitFlags();
  if ((target_flags & 0x02000000u) != 0u) {
    return false;
  }
  if (!ignore_flag_check &&
       ((IsPlayerControlled() && (target_flags & 0x100u) != 0u) ||
        (!IsPlayerControlled() && (target_flags & 0x200u) != 0u))) {
    return false;
  }
  if (GetReaction(target, memo) < ReactionType::kFriendly &&
      !owner_.State().IsCivilian()) {
    return false;
  }
  if (target.Interaction().IsPlayerControlled()) {
    const auto *const my_controller = ResolveControllingPlayer();
    const auto *const target_controller =
        target.Interaction().ResolveControllingPlayer();
    const auto *const objects = owner_.object_manager();
    const auto &groups = GroupSystem::Get();
    if (my_controller != nullptr && target_controller != nullptr &&
        groups.IsInGroup() && objects != nullptr &&
        groups.IsActivePlayerPartyOrRaidUnitGuid(
            *objects, target_controller->GetGuid().GetRawValue()) &&
        !IsActivePlayerControlledGroupLink(
            *my_controller, *target_controller, ControlledGroupScope::kRaid,
            *objects)) {
      return false;
    }
    const auto target_pvp = target.State().GetPvPFlags();
    const auto my_pvp = owner_.State().GetPvPFlags();
    if ((target_pvp & 0x04u) != 0u && (my_pvp & 0x04u) == 0u) {
      return false;
    }
    if ((my_pvp & 0x08u) != 0u && (target_pvp & 0x08u) == 0u &&
        (target_pvp & 0x01u) != 0u) {
      return false;
    }
  } else if (IsPlayerControlled() && !ignore_flag_check) {
    const auto target_pvp = target.State().GetPvPFlags();
    if ((target_pvp & 0x01u) == 0u &&
        !target.State().CanBeAssistedByPlayerSpell() && !target.State().IsCivilian()) {
      return false;
    }
  }
  return true;
}

bool UnitInteractionRuntime::CanAttackSpellTarget(const CGUnit_C &target) const {
  ReactionMemo memo;
  return CanAttackSpellTarget(target, memo);
}

bool UnitInteractionRuntime::CanAttackSpellTarget(const CGUnit_C &target,
                                                  ReactionMemo &memo) const {

  const auto neutral_or_civilian = [&]() {
    return GetReaction(target, memo) >= ReactionType::kFriendly ||
           owner_.State().IsCivilian();
  };
  constexpr std::uint32_t kPlayerFlagsCannotAttack = 0x00080000u;
  constexpr std::uint32_t kPlayerFlagsGhost = 0x00000010u;
  constexpr std::uint32_t kAttackGhostCreatureTemplateTypeFlag = 0x00000002u;
  if (owner_.IsPlayer() &&
      (owner_.GetUInt32(PLAYER_FLAGS) & kPlayerFlagsCannotAttack) != 0u) {
    return false;
  }
  if (target.IsPlayer() &&
      (target.GetUInt32(PLAYER_FLAGS) & kPlayerFlagsGhost) != 0u &&
      !owner_.State().HasCreatureTemplateTypeFlag(kAttackGhostCreatureTemplateTypeFlag)) {
    return false;
  }
  const std::uint32_t target_flags = target.State().GetUnitFlags();
  if ((target_flags & 0x02u) != 0u ||
      (target_flags & 0x100000u) != 0u ||
      (target_flags & 0x80u) != 0u ||
      (target_flags & 0x10000u) != 0u ||
      (target_flags & 0x02000000u) != 0u) {
    return false;
  }
  const std::uint32_t my_flags = owner_.State().GetUnitFlags();
  if (((my_flags & 0x8u) != 0u && (target_flags & 0x100u) != 0u) ||
      ((my_flags & 0x8u) == 0u && (target_flags & 0x200u) != 0u) ||
      ((target_flags & 0x8u) != 0u && (my_flags & 0x100u) != 0u) ||
      ((target_flags & 0x8u) == 0u && (my_flags & 0x200u) != 0u)) {
    return false;
  }
  if (owner_.Vehicle().IsUsingVehicle() || target.Vehicle().IsUsingVehicle()) {
    const auto *const my_vehicle = owner_.Vehicle().GetVehicleEntry();
    const auto *const target_vehicle = target.Vehicle().GetVehicleEntry();
    if ((my_vehicle != nullptr &&
         (my_vehicle->flags & 0x20000000u) != 0u &&
          target.Vehicle().GetVehicleObject(target) == &owner_) ||
        (target_vehicle != nullptr &&
         (target_vehicle->flags & 0x20000000u) != 0u &&
          owner_.Vehicle().GetVehicleObject(owner_) == &target)) {
      return false;
    }
  }
  const bool caster_pc = IsPlayerControlled();
  const bool target_pc = target.Interaction().IsPlayerControlled();
  if (caster_pc && target_pc) {
    if (neutral_or_civilian()) {
      return false;
    }
    const auto *const self_controller = ResolveControllingPlayer();
    const auto *const target_controller =
        target.Interaction().ResolveControllingPlayer();
    if (self_controller != nullptr && target_controller != nullptr) {
      const auto *const objects = owner_.object_manager();
      if (GroupSystem::Get().IsInGroup() && objects != nullptr &&
          IsActivePlayerControlledGroupLink(
              *self_controller, *target_controller,
              ControlledGroupScope::kRaid, *objects)) {
        return true;
      }
      const auto target_pvp = target.State().GetPvPFlags();
      const auto my_pvp = owner_.State().GetPvPFlags();
      if ((target_pvp & 0x01u) != 0u) {
        return (my_pvp & 0x08u) == 0u && (target_pvp & 0x08u) == 0u;
      }
      if ((my_pvp & 0x04u) != 0u && (target_pvp & 0x04u) != 0u) {
        return true;
      }
      if (((my_pvp & 0x02u) == 0u && (target_pvp & 0x02u) == 0u) ||
          (my_pvp & 0x08u) != 0u) {
        return false;
      }
      return (target_pvp & 0x08u) == 0u;
    }
    if ((owner_.State().GetPvPFlags() & 0x08u) != 0u) {
      return false;
    }
    return (target.State().GetPvPFlags() & 0x08u) == 0u;
  }
  if (caster_pc || target_pc) {
    if ((caster_pc && (target.State().GetPvPFlags() & 0x08u) != 0u) ||
        (target_pc && (owner_.State().GetPvPFlags() & 0x08u) != 0u)) {
      return false;
    }
    return !neutral_or_civilian();
  }
  return GetReaction(target, memo) <= ReactionType::kHostile ||
         target.Interaction().IsHostileTo(owner_);
}

bool UnitInteractionRuntime::IsAttackingOrLatched() const {
  return (owner_.State().GetUnitFlags() & 0x800u) != 0u ||
         !cached_update_target_guid_.IsEmpty();
}

bool UnitInteractionRuntime::IsSpellClickAccessible() const {
  constexpr std::uint32_t kNpcFlagSpellClick = 0x01000000u;
  constexpr std::uint32_t kSpellClickDisabled = 0x2000u;
  constexpr std::uint32_t kSpellClickPartyOnly = 0x1000u;
  const auto flags2 = owner_.State().GetUnitFlags2();
  if ((owner_.State().GetNpcFlags() & kNpcFlagSpellClick) == 0u ||
      (flags2 & kSpellClickDisabled) != 0u) {
    return false;
  }
  if ((flags2 & kSpellClickPartyOnly) == 0u) {
    return true;
  }
  const auto *const objects = owner_.object_manager();
  const auto &groups = GroupSystem::Get();
  const auto guid = owner_.GetGuid().GetRawValue();
  return objects != nullptr &&
         (groups.IsPartyUnitGuid(*objects, guid) ||
          groups.IsRaidUnitGuid(*objects, guid));
}

bool UnitInteractionRuntime::CanInitiateAutoAttack(const CGUnit_C &target) const {
  if (owner_.State().IsDead()) {
    if (!owner_.IsActivePlayer()) {
      return false;
    }
    const auto *const objects = owner_.object_manager();
    const auto *const player =
        objects != nullptr ? objects->GetLocalPlayerTyped() : nullptr;
    if (player == nullptr || player->GetActiveControlUnit() == nullptr) {
      return false;
    }
  }
  constexpr std::uint32_t kMountedStateSuppressed = 0x10000000u;
  if (static_cast<std::int32_t>(owner_.Mount().CachedDisplayForSpell()) > 0 &&
      !owner_.State().HasSpellStateFlags(kMountedStateSuppressed) &&
      !owner_.State().CanActWhileMounted() && !owner_.Movement().CanChangeDirection()) {
    return false;
  }
  constexpr std::uint32_t kDeadDynamicFlag = 0x20u;
  if (static_cast<std::int32_t>(target.State().GetHealth()) <= 0 &&
      (target.State().GetDynamicFlags() & kDeadDynamicFlag) == 0u) {
    return false;
  }
  return CanAttackSpellTarget(target);
}

bool UnitInteractionRuntime::CanInteractWithFriendlyPlayerTarget(
    const CGUnit_C &target) const {
  if (target.GetGuid() == owner_.GetGuid() ||
      (target.GetTypeMask() & 0x10u) == 0u ||
      !target.State().GetCharmedBy().IsEmpty()) {
    return false;
  }
  const auto *const dbc = owner_.dbc_loader();
  const auto *const my_faction =
      dbc != nullptr ? dbc->faction_template().LookupEntry(
                           owner_.State().GetFactionTemplate())
                     : nullptr;
  const auto *const target_faction =
      dbc != nullptr ? dbc->faction_template().LookupEntry(
                           target.State().GetFactionTemplate())
                     : nullptr;
  if (my_faction != nullptr && target_faction != nullptr &&
      my_faction->faction_group != target_faction->faction_group) {
    return false;
  }
  return !CanAttackSpellTarget(target);
}

void UnitInteractionRuntime::ProcessPendingInteraction() {}

bool UnitInteractionRuntime::IsActivePlayerAutoAttacking() const {
  const auto *const objects = owner_.object_manager();
  const auto *const player =
      objects != nullptr ? objects->GetLocalPlayerTyped() : nullptr;
  return player != nullptr && owner_.GetGuid() == player->GetGuid() &&
         IsAutoAttacking();
}

bool UnitInteractionRuntime::CurrentShapeshiftFormRequiresTurnSensitiveUse() const {
  if (owner_.State().SuppressesCurrentFormSpellQueries() ||
      owner_.Animation().GetShapeshiftForm() == 0u) {
    return false;
  }
  const auto *const dbc = owner_.dbc_loader();
  const auto *const form =
      dbc != nullptr
          ? dbc->spell_shapeshift_form().LookupEntry(
                owner_.Animation().GetShapeshiftForm())
          : nullptr;
  return form == nullptr ||
         (form->flags & kTurnSensitiveShapeshiftFlag) == 0u;
}

bool UnitInteractionRuntime::IsInCancelableShapeshiftForm() const {
  if (owner_.State().SuppressesCurrentFormSpellQueries() ||
      owner_.Animation().GetShapeshiftForm() == 0u) {
    return false;
  }
  const auto *const dbc = owner_.dbc_loader();
  const auto *const form =
      dbc != nullptr
          ? dbc->spell_shapeshift_form().LookupEntry(
                owner_.Animation().GetShapeshiftForm())
          : nullptr;
  return form == nullptr ||
         (form->flags & kTurnSensitiveShapeshiftFlag) == 0u ||
         (form->flags & data::dbc::kShapeshiftFormFlagCancelOverride) != 0u;
}

bool UnitInteractionRuntime::CanAutoCancelShapeshiftFormForAction() const {

  if (owner_.State().IsTaxiFlight()) {
    return false;
  }

  const auto active_player_guid = CGObject_C::GetActivePlayerGuid();
  if (active_player_guid.IsEmpty() || owner_.GetGuid() != active_player_guid) {
    return false;
  }

  const auto &cvars = ui::game::CVarSystem::Instance();
  if (!cvars.GetCVarBool("autoUnshift")) {
    return false;
  }
  if (owner_.Movement().IsFlying() && !cvars.GetCVarBool("autoDismountFlying")) {
    return false;
  }

  const auto form_id = owner_.State().SuppressesCurrentFormSpellQueries()
                           ? std::uint8_t{0}
                           : owner_.Animation().GetShapeshiftForm();
  if (form_id == 0u) {
    return true;
  }
  const auto *const dbc = owner_.dbc_loader();
  const auto *const form =
      dbc != nullptr ? dbc->spell_shapeshift_form().LookupEntry(form_id)
                     : nullptr;
  return form == nullptr ||
         (form->flags & data::dbc::kShapeshiftFormFlagBlocksAutoCancel) == 0u;
}

bool UnitInteractionRuntime::SuppressesAttackActionShapeshiftAutoCancel() const {
  if (CurrentShapeshiftFormRequiresTurnSensitiveUse()) {
    return false;
  }
  if (owner_.Presentation().DisplayId() ==
      owner_.Presentation().NativeDisplayId()) {
    return true;
  }
  const auto *const dbc = owner_.dbc_loader();
  return dbc != nullptr &&
         DisplaySuppressesAttackActionShapeshiftAutoCancel(*dbc,
                                                             owner_.Presentation().DisplayId());
}

bool UnitInteractionRuntime::IsInMeleeRange(const CGUnit_C &other) const {
  const auto range = interaction_range::ComputeUnitInteractionRange(
      owner_.State().GetCombatReach(), other.State().GetCombatReach());
  const auto position = owner_.GetPosition();
  const auto other_position = other.GetPosition();
  const auto dx = position.x - other_position.x;
  const auto dy = position.y - other_position.y;
  const auto dz = position.z - other_position.z;
  return dx * dx + dy * dy + dz * dz < range * range;
}

bool UnitInteractionRuntime::IsControlledPet() const {
  const auto *const objects = owner_.object_manager();
  const auto *const player =
      objects != nullptr ? objects->GetLocalPlayerTyped() : nullptr;
  return player != nullptr && owner_.GetGuid() != player->GetGuid() &&
         !owner_.State().GetSummonedBy().IsEmpty() && IsPlayerControlled();
}

bool UnitInteractionRuntime::CanMoveInCurrentForm() const {
  return IsInCancelableShapeshiftForm();
}

void UnitInteractionRuntime::CancelAutoAttackAndCheckLootClose(
    WorldSession &session, const bool cancel_auto_attack,
    const bool skip_loot_check) {
  if (cancel_auto_attack && owner_.IsActiveMover() && auto_attack_type_ != kAutoAttackTypeIdle &&
      (session.player_control_runtime().movement_interaction_flags & 0x1u) ==
          0u) {
    CompleteAutoAttackInteraction(false, true);

    session.click_to_move().Stop();
  }
  if (!skip_loot_check && owner_.IsActiveMover()) {
    const auto *const objects = owner_.object_manager();
    const auto *const player =
        objects != nullptr ? objects->GetActivePlayer() : nullptr;
    if (player != nullptr && !player->Casts().GetComboTarget().IsEmpty()) {
      CloseLootForActiveMovement(session);
    }
  }
}

std::uint32_t UnitInteractionRuntime::AutoAttackType() const noexcept {
  return auto_attack_type_;
}

ObjectGuid UnitInteractionRuntime::AutoAttackTarget() const noexcept {
  return auto_attack_target_;
}

const std::array<float, 3> &
UnitInteractionRuntime::AutoAttackTargetPosition() const noexcept {
  return auto_attack_target_position_;
}

void UnitInteractionRuntime::BeginAutoAttack(
    const std::uint32_t type, const ObjectGuid target,
    const std::array<float, 3> target_position, const float facing) noexcept {
  auto_attack_target_position_ = target_position;
  auto_attack_facing_ = facing;
  auto_attack_type_ = type;
  auto_attack_target_ = target;
}

void UnitInteractionRuntime::SetCachedUpdateTarget(
    const ObjectGuid target) noexcept {
  cached_update_target_guid_ = target;
}

bool UnitInteractionRuntime::HasCachedUpdateTarget() const noexcept {
  return !cached_update_target_guid_.IsEmpty();
}

void UnitInteractionRuntime::RebaseAutoAttackForTransportChange(
    const float *matrix4x4, const float facing_delta) {
  const auto *const objects = owner_.object_manager();
  if (objects == nullptr) {
    return;
  }
  if (owner_.GetGuid().GetRawValue() !=
          objects->player_control().active_mover_guid ||
      auto_attack_type_ == kAutoAttackTypeIdle) {
    return;
  }
  if (auto_attack_type_ == 4u) {
    openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
        auto_attack_target_position_.data(), auto_attack_target_position_.data(),
        matrix4x4);
  }
  if (auto_attack_type_ == 2u || auto_attack_type_ == 8u) {
    auto_attack_facing_ = openwow::game::Movement_NormalizeFacing0ToTau(
        auto_attack_facing_ + facing_delta);
  }
}

void CGUnit_C::OnRightClickInteract(WorldSession *session,
                                    TargetingSystem *targeting) const {
  Interaction().RightClickInteract(session, targeting);
}

}

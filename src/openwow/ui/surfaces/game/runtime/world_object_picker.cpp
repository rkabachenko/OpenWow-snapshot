#include "openwow/ui/surfaces/game/runtime/world_object_picker.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/interaction_predicate.h"
#include "openwow/game/interaction_range.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"

#include <cmath>
#include <cstdint>

namespace openwow::ui::game {
namespace {

namespace game = ::openwow::game;

constexpr float kWorldPickTieEpsilon = 2.3841858e-7f;
constexpr float kNpcHoverRangePadding = 4.0f;
constexpr float kAutoAttackHoverRangeSquared = 109.2025f;
constexpr std::uint32_t kCorpseFlagSpellTargetEnemy = 0x20u;
constexpr float kFacingMouseoverMaxDistanceSquared = 10000.0f;

bool HasBetterWorldPickScore(const WorldObjectPickContext& context,
                             const float dot,
                             const float distance_squared) {
  if (dot > context.best_dot) {
    return true;
  }
  if (std::fabs(dot - context.best_dot) >= kWorldPickTieEpsilon) {
    return false;
  }
  return distance_squared < context.best_distance_squared;
}

bool MatchesComboTargetOverride(const game::CGPlayer_C& active_player,
                                const game::CGObject_C& target) {
  return active_player.Casts().GetComboTarget() == target.GetGuid();
}

bool CanInteractWithWorldPickTarget(
    const game::CGPlayer_C& active_player,
    const game::CGObject_C& target) {
  return game::CanInteractWithTarget(active_player, target);
}

bool CanHoverNpcUnit(const game::CGPlayer_C& active_player,
                     const game::CGUnit_C& unit,
                     const game::QueryCache& query_cache,
                     const float distance_squared) {

  if (!detail::CanActivePlayerInteractWithNpcUnits(active_player) ||
      !detail::PickPathAllowsNpcUnitTarget(active_player, unit, &query_cache) ||
      !detail::IsValidNpcUnitInteractionTarget(
          active_player, unit, &query_cache)) {
    return false;
  }

  const float threshold = unit.State().GetCombatReach() + kNpcHoverRangePadding;
  return distance_squared <= threshold * threshold;
}

bool CanHoverLootableDeadUnit(const game::CGPlayer_C& active_player,
                              const game::CGUnit_C& unit,
                              const std::uint32_t current_tick) {
  return unit.State().IsLootableCorpseAt(current_tick) &&
         (CanInteractWithWorldPickTarget(active_player, unit) ||
          MatchesComboTargetOverride(active_player, unit));
}

bool CanHoverPendingSpellTarget(const game::WorldSession& session,
                                const game::CGPlayer_C& active_player,
                                const game::CGUnit_C& unit) {
  auto& spell_client = session.spells();
  if (!spell_client.GetTargeting().IsTargeting()) {
    return false;
  }

  const std::uint32_t spell_id = spell_client.GetCurrentSpellId();
  const auto* dbc = session.GetDbcLoader();
  return spell_id != 0 && dbc != nullptr &&
         game::SpellTargetValidator::ValidateUnitTarget(
             session, *dbc, spell_id, active_player, unit, true) ==
             game::SpellTargetResult::kValid;
}

bool CanHoverAutoAttackTarget(const game::WorldSession& session,
                              const game::CGPlayer_C& active_player,
                              const game::CGUnit_C& unit,
                              const float distance_squared) {
  if (!session.has_current_map() || active_player.State().IsDeadOrGhost() ||
      active_player.State().IsTaxiFlight() || active_player.State().IsPacified() ||
      distance_squared > kAutoAttackHoverRangeSquared ||
      (unit.State().GetHealth() == 0 && !unit.State().IsSkinnable())) {
    return false;
  }
  return active_player.Interaction().CanAttackSpellTarget(unit);
}

bool CanHoverGameObject(const game::WorldSession& session,
                        const game::CGGameObject_C& game_object) {
  if (!game_object.ShouldHighlight()) {
    return false;
  }
  return const_cast<game::CGGameObject_C&>(game_object)
      .TryUse(session, nullptr, nullptr, nullptr);
}

bool CanHoverInteractableCorpse(const game::CGPlayer_C& active_player,
                                const game::CGCorpse_C& corpse) {
  return (corpse.GetCorpseFlags() & 0x1u) != 0 &&
         (CanInteractWithWorldPickTarget(active_player, corpse) ||
          MatchesComboTargetOverride(active_player, corpse));
}

bool CanHoverSpellCorpse(const game::WorldSession& session,
                         const game::CGPlayer_C& active_player,
                         const game::CGCorpse_C& corpse,
                         const float distance_squared) {
  if ((corpse.GetCorpseFlags() & kCorpseFlagSpellTargetEnemy) == 0) {
    return false;
  }

  const auto spell_id =
      game::SpellbookSystem::Get().ResolveGatherInteractionSpellId(
          corpse, &session.query_cache());
  float max_range = 0.0f;
  return TryResolveGatherInteractionMaxRange(
             session, active_player, spell_id, max_range) &&
         distance_squared <=
             static_cast<double>(max_range * max_range);
}

bool CanHoverGatherableUnit(const game::WorldSession& session,
                            const game::CGPlayer_C& active_player,
                            const game::CGUnit_C& unit,
                            const float distance_squared) {

  if (!unit.State().IsSkinnable()) {
    return false;
  }
  if (unit.IsPlayer() &&
      active_player.Interaction().CanAssistSpellTarget(unit, false)) {
    return false;
  }

  const auto spell_id =
      game::SpellbookSystem::Get().ResolveGatherInteractionSpellId(
          unit, &session.query_cache());
  float max_range = 0.0f;
  return TryResolveGatherInteractionMaxRange(
             session, active_player, spell_id, max_range) &&
         distance_squared <=
             static_cast<double>(max_range * max_range);
}

void CommitWorldPickCandidate(WorldObjectPickContext& context,
                              const game::ObjectGuid candidate,
                              const float dot,
                              const float distance_squared) {
  context.best_target = candidate;
  context.best_dot = dot;
  context.best_distance_squared = distance_squared;
}

}

bool TryResolveGatherInteractionMaxRange(
    const game::WorldSession& session,
    const game::CGPlayer_C& active_player,
    const std::uint32_t spell_id,
    float& max_range) {
  if (spell_id == 0) {
    return false;
  }

  if (const auto query = game::SpellQueryBridge::Get().Query(spell_id);
      query.has_value() && query->range > 0.0f) {
    max_range = query->range;
    return true;
  }

  const auto* dbc = session.GetDbcLoader();
  const auto* spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
  if (spell == nullptr) {
    return false;
  }

  const auto* range_entry =
      spell->range_index != 0
          ? dbc->spell_range().LookupEntry(spell->range_index)
          : nullptr;
  const auto range_window =
      game::SpellTargetValidator::GetUntargetedRangeWindow(
          *spell, range_entry, active_player, false, &session);
  if (range_window.max_range <= 0.0f) {
    return false;
  }

  max_range = range_window.max_range;
  return true;
}

void EvaluateWorldObjectPickCandidate(
    const game::ObjectGuid candidate,
    WorldObjectPickContext& context) {
  const auto* session = context.session;
  if (session == nullptr || context.active_player == nullptr ||
      context.facing_anchor == nullptr) {
    return;
  }

  const auto* object = session->objects().Get(candidate);
  if (object == nullptr || object == context.facing_anchor) {
    return;
  }

  const float delta_x = object->GetX() - context.facing_anchor->GetX();
  const float delta_y = object->GetY() - context.facing_anchor->GetY();
  const float delta_z = object->GetZ() - context.facing_anchor->GetZ();
  const float distance_squared =
      delta_x * delta_x + delta_y * delta_y + delta_z * delta_z;
  if (distance_squared <= 0.0f) {
    return;
  }

  const float inverse_distance = 1.0f / std::sqrt(distance_squared);
  const float dot =
      context.direction_x * (delta_x * inverse_distance) +
      context.direction_y * (delta_y * inverse_distance) +
      context.direction_z * (delta_z * inverse_distance);
  if (!HasBetterWorldPickScore(context, dot, distance_squared)) {
    return;
  }

  if (const auto* unit = dynamic_cast<const game::CGUnit_C*>(object);
      unit != nullptr) {
    const auto& player = *context.active_player;
    if (CanHoverNpcUnit(
            player, *unit, session->query_cache(), distance_squared) ||
        CanHoverLootableDeadUnit(player, *unit, context.client_tick) ||
        CanHoverGatherableUnit(*session, player, *unit, distance_squared) ||
        CanHoverPendingSpellTarget(*session, player, *unit) ||
        CanHoverAutoAttackTarget(*session, player, *unit, distance_squared)) {
      CommitWorldPickCandidate(
          context, object->GetGuid(), dot, distance_squared);
    }
    return;
  }

  if (context.active_player->GetGuid() != context.facing_anchor->GetGuid()) {
    return;
  }

  if (const auto* game_object =
          dynamic_cast<const game::CGGameObject_C*>(object);
      game_object != nullptr) {
    if (CanHoverGameObject(*session, *game_object)) {
      CommitWorldPickCandidate(
          context, object->GetGuid(), dot, distance_squared);
    }
    return;
  }

  if (const auto* corpse = dynamic_cast<const game::CGCorpse_C*>(object);
      corpse != nullptr &&
      (CanHoverInteractableCorpse(*context.active_player, *corpse) ||
       CanHoverSpellCorpse(
           *session, *context.active_player, *corpse, distance_squared))) {
    CommitWorldPickCandidate(
        context, object->GetGuid(), dot, distance_squared);
  }
}

game::ObjectGuid ResolveFacingMouseoverTarget(
    const game::WorldSession& session) {
  const auto* active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr) {
    return {};
  }

  const game::CGUnit_C* facing_unit = active_player;
  if (const auto* controlled = active_player->GetActiveControlUnit();
      controlled != nullptr) {
    facing_unit = controlled;
  }

  WorldObjectPickContext context;
  context.session = &session;
  context.active_player = active_player;
  context.facing_anchor = facing_unit;
  context.direction_x = std::cos(facing_unit->GetOrientation());
  context.direction_y = std::sin(facing_unit->GetOrientation());
  context.client_tick = openwow::core::GameClock::GetTickCount32();
  context.best_distance_squared = kFacingMouseoverMaxDistanceSquared;

  session.objects().EnumVisibleObjects([&](const game::WorldObject& object) {
    EvaluateWorldObjectPickCandidate(object.GetGuid(), context);
    return true;
  });
  return context.best_target;
}

}

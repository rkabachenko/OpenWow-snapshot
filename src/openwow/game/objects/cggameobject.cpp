
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/player_control_runtime.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/client_misc.h"
#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/faction_reaction.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/gameobject_sound_event.h"
#include "openwow/game/group_system.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/object_effect_system.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/passenger_movement.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/readable_text.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/targeting.h"
#include "openwow/game/vehicle.h"
#include "openwow/game/world_session.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/game/callee_wrappers.h"
#include "openwow/game/movement/player_move_event.h"
#include "openwow/foundation/math/packed_quaternion64.h"
#include "openwow/foundation/math/quaternion_xyzw.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>

namespace openwow::game {

namespace {

constexpr std::uint32_t kReadablePageTextFieldId = 15;
constexpr std::uint32_t kLockIdFieldId = 4;
constexpr std::uint32_t kChairSlotsFieldId = 11;
constexpr std::uint32_t kReadableLanguageFieldId = 16;
constexpr std::uint32_t kReadablePageMaterialFieldId = 17;
constexpr std::uint32_t kMountedInteractionFieldId = 91;
constexpr std::uint32_t kPlayerRequirementGateFieldId = 18;
constexpr std::uint32_t kRequiredSkillFieldId = 20;
constexpr std::uint32_t kUseWhileInCombatFieldId = 62;
constexpr std::uint32_t kQuestConditionalOwnerFieldId = 94;
constexpr std::uint32_t kDestructibleModelDataFieldId = 124;
constexpr std::uint32_t kMOTransportTaxiPathFieldId = 35;
constexpr std::uint32_t kMOTransportMaxVelocityFieldId = 36;
constexpr std::uint32_t kMOTransportAccelerationFieldId = 43;
constexpr std::uint32_t kMOTransportPhysicsFieldId = 99;

constexpr std::uint32_t kMOTransportCanBeStoppedFieldId = 128;
constexpr std::uint32_t kDungeonDifficultyMapIdFieldId = 87;
constexpr std::uint32_t kDungeonDifficultyRequiredIndexFieldId = 88;
constexpr std::uint32_t kMeetingStoneMinLevelFieldId = 51;
constexpr std::uint32_t kMeetingStoneMaxLevelFieldId = 52;
constexpr std::uint32_t kMeetingStoneAreaFieldId = 53;
constexpr std::uint32_t kFloatingTooltipFieldId = 19;
constexpr std::uint32_t kCustomAnimFieldId = 22;
constexpr std::uint32_t kMapAllowsPlayerDifficultyFlag = 0x100u;
constexpr std::array<std::uint16_t, 7> kModelAnimationIdByRequestCode = {0u,  0u,  8u, 9u,
                                                                         10u, 11u, 12u};
constexpr std::uint32_t kPlayerUseBlockedUnitFlag = 0x00040000u;
constexpr std::uint32_t kUnitFlagInCombat = 0x00080000u;
constexpr std::uint32_t kPlayerUseRestrictedUnitFlag = 0x80000000u;
constexpr std::uint32_t kSpellEffectOpenLock = 33u;
constexpr std::uint32_t kAuraTypeUseBlocked = 12u;
constexpr float kRightClickAutoApproachRangeScale = 0.9f;
constexpr float kFacingEpsilon = 0.00000023841858f;
constexpr float kPi = 3.1415927f;
constexpr float kHalfPi = 0.5f * kPi;
constexpr float kThreeHalfPi = 1.5f * kPi;
constexpr std::uint16_t kTrapdoorCloseAnimationId = 0x92u;
constexpr std::uint16_t kTrapdoorOpenAnimationId = 0x94u;

void SyncTransportAnimationClock(CGGameObject_C &game_object,
                                 std::uint8_t previous_state_byte,
                                 std::uint8_t current_state_byte);

void ResetTransportHandlerCommittedState(CGGameObject_C &game_object);

[[nodiscard]] bool TryStartRightClickAutoApproach(WorldSession &session,
                                                  const CGGameObject_C &game_object,
                                                  const float interact_distance) {

  auto *const active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr || static_cast<std::int32_t>(active_player->State().GetHealth()) <= 0 ||
      !active_player->IsActiveMover() ||
      !::openwow::ui::game::CVarSystem::Instance().GetCVarBool("autoInteract")) {
    return false;
  }

  const auto position = game_object.GetPosition();
  session.click_to_move().InteractWith(
      game_object.GetGuid(), position.x, position.y, position.z,
      interact_distance * kRightClickAutoApproachRangeScale);
  return true;
}

[[nodiscard]] bool HasCaseInsensitiveSuffix(const std::string_view value,
                                            const std::string_view suffix) {
  if (value.size() < suffix.size()) {
    return false;
  }

  const auto offset = value.size() - suffix.size();
  for (std::size_t index = 0; index < suffix.size(); ++index) {
    const auto lhs = static_cast<unsigned char>(value[offset + index]);
    const auto rhs = static_cast<unsigned char>(suffix[index]);
    if (std::tolower(lhs) != std::tolower(rhs)) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] bool IsWorldModelPath(const std::string_view display_path) {
  return HasCaseInsensitiveSuffix(display_path, ".wmo");
}

[[nodiscard]] WorldModelPlacementMetadata
NormalizeWorldModelPlacementMetadata(const GameObjectModelLoadMetadata &metadata) {
  auto world_model = metadata.world_model;
  world_model.wait_for_loaded_model =
      world_model.wait_for_loaded_model || metadata.loader_arg3 != 0u;
  if (world_model.doodad_set_count > world_model.doodad_sets.size()) {
    world_model.doodad_set_count = static_cast<std::uint8_t>(world_model.doodad_sets.size());
  }
  for (std::size_t index = world_model.doodad_set_count; index < world_model.doodad_sets.size();
       ++index) {
    world_model.doodad_sets[index] = 0u;
  }
  return world_model;
}

void LogMissingGameObjectDisplayRecord(const CGGameObject_C &game_object,
                                       const std::uint32_t display_id) {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                     "Game object id " + std::to_string(game_object.GetEntry()) +
                         " is missing its display record " + std::to_string(display_id));
}

[[nodiscard]] const openwow::data::dbc::GameObjectDisplayInfoEntry *
LookupGameObjectDisplayInfoForModelLoad(const CGGameObject_C &game_object,
                                        const std::uint32_t display_id) {
  const auto *const objects = game_object.object_manager();
  const auto *const dbc = objects != nullptr ? &objects->dbc_loader() : nullptr;
  if (dbc == nullptr) {
    LogMissingGameObjectDisplayRecord(game_object, display_id);
    return nullptr;
  }

  const auto *const entry = dbc->gameobject_display_info().LookupEntry(display_id);
  if (entry == nullptr) {
    LogMissingGameObjectDisplayRecord(game_object, display_id);
    return nullptr;
  }

  return entry;
}

[[nodiscard]] bool IsLockActionApplicable(const CGGameObject_C &game_object,
                                          const std::uint32_t action) {
  const auto state_byte = static_cast<std::uint8_t>(game_object.GetGoState());
  const auto is_locked = (game_object.GetFlags() & GO_FLAG_LOCKED) != 0u;

  if (action == 4u) {
    return state_byte == static_cast<std::uint8_t>(GOState::ActiveAlternative);
  }

  if (state_byte == static_cast<std::uint8_t>(GOState::ActiveAlternative) ||
      (((action < 2u) || action == 3u) &&
       state_byte != static_cast<std::uint8_t>(GOState::Ready))) {
    return false;
  }

  if (action == 0u) {
    return !is_locked;
  }

  if (action == 1u) {
    return is_locked;
  }

  if (action == 2u && state_byte != static_cast<std::uint8_t>(GOState::Active)) {
    return false;
  }

  return true;
}

[[nodiscard]] const openwow::data::dbc::SpellEntry *
LookupSpellEntry(const openwow::data::dbc::DbcLoader &dbc, const std::uint32_t spell_id) {
  if (spell_id == 0u) {
    return nullptr;
  }

  return dbc.spell().LookupEntry(spell_id);
}

[[nodiscard]] std::uint32_t GetSpellEffectMagnitude(const openwow::data::dbc::SpellEntry &spell,
                                                    const std::size_t effect_index) {
  if (effect_index >= spell.effect_base_points.size()) {
    return 0u;
  }

  return static_cast<std::uint32_t>(std::max(0, spell.effect_base_points[effect_index] + 1));
}

[[nodiscard]] bool TryResolveBlockingAuraMechanic(const CGUnit_C &player,
                                                  const openwow::data::dbc::DbcLoader &dbc,
                                                  std::uint32_t *const mechanic_out) {
  if (mechanic_out != nullptr) {
    *mechanic_out = 0u;
  }

  return CGUnit_C__HasCompatibleAuraType(player, dbc, nullptr, kAuraTypeUseBlocked, mechanic_out);
}

[[nodiscard]] bool SpellSatisfiesOpenLockRequirement(const openwow::data::dbc::SpellEntry &spell,
                                                     const std::uint32_t lock_index,
                                                     const std::uint32_t required_value) {
  for (std::size_t effect_index = 0; effect_index < spell.effect.size(); ++effect_index) {
    if (spell.effect[effect_index] != kSpellEffectOpenLock ||
        static_cast<std::uint32_t>(spell.effect_misc_value[effect_index]) != lock_index) {
      continue;
    }

    if (GetSpellEffectMagnitude(spell, effect_index) >= required_value) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool SpellHasAnyOpenLockEffect(const openwow::data::dbc::SpellEntry &spell) {
  for (const auto effect : spell.effect) {
    if (effect == kSpellEffectOpenLock) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool IsZeroQuaternion(const CGGameObject_C::Rotation &rotation) {
  return rotation.x == 0.0f && rotation.y == 0.0f && rotation.z == 0.0f && rotation.w == 0.0f;
}

[[nodiscard]] float
FacingFromQuaternion(const openwow::math::quaternion_xyzw::Quaternion &rotation) {
  const float yaw_cosine = 1.0f - (rotation.z * rotation.z + rotation.y * rotation.y) * 2.0f;
  const float yaw_sine = 2.0f * (rotation.w * rotation.z + rotation.y * rotation.x);

  if (std::fabs(yaw_cosine) >= kFacingEpsilon) {
    if (std::fabs(yaw_sine) >= kFacingEpsilon) {
      return std::atan2(yaw_sine, yaw_cosine);
    }

    return yaw_cosine > 0.0f ? 0.0f : kPi;
  }

  return yaw_sine >= 0.0f ? kHalfPi : kThreeHalfPi;
}

[[nodiscard]] std::array<float, 4>
BuildTransportAnimationBaseRotation(const CGGameObject_C &game_object) {
  const auto descriptor_rotation = game_object.GetParentRotation();
  if (IsZeroQuaternion(descriptor_rotation)) {
    return {0.0f, 0.0f, 0.0f, 1.0f};
  }

  return {
      descriptor_rotation.x,
      descriptor_rotation.y,
      descriptor_rotation.z,
      descriptor_rotation.w,
  };
}

[[nodiscard]] std::optional<TransportPathInfo>
BuildTransportPathInfoFromDbc(const CGGameObject_C &game_object,
                              const openwow::data::dbc::DbcLoader &dbc,
                              const std::uint32_t map_id) {

  const auto transport_key = game_object.GetEntry();
  if (transport_key == 0u) {
    return std::nullopt;
  }

  std::vector<openwow::data::dbc::TransportAnimationEntry> animation_entries;
  for (const auto &entry : dbc.transport_animation().entries()) {
    if (entry.transport_id == transport_key) {
      animation_entries.push_back(entry);
    }
  }

  if (animation_entries.empty()) {
    return std::nullopt;
  }

  std::sort(animation_entries.begin(), animation_entries.end(),
            [](const auto &lhs, const auto &rhs) {
              if (lhs.time_index != rhs.time_index) {
                return lhs.time_index < rhs.time_index;
              }
              return lhs.id < rhs.id;
            });

  TransportPathInfo path;
  path.pathId = transport_key;
  path.totalCycleTime = animation_entries.back().time_index;
  path.uses_local_animation = true;

  const auto base_position = game_object.GetPosition();
  path.base_pose.position = {base_position.x, base_position.y, base_position.z};
  path.base_pose.rotation = BuildTransportAnimationBaseRotation(game_object);

  const auto stopped_duration_ms = game_object.GetLevel();
  path.nodes.reserve(animation_entries.size());
  for (std::size_t index = 0; index < animation_entries.size(); ++index) {
    const auto &entry = animation_entries[index];
    path.nodes.push_back({
        .x = entry.x,
        .y = entry.y,
        .z = entry.z,
        .mapId = map_id,
        .delay = index == 0 ? stopped_duration_ms : 0u,
        .actionFlag = entry.sequence_id,
        .arrivalTime = entry.time_index,
    });
  }

  std::vector<openwow::data::dbc::TransportRotationEntry> rotation_entries;
  for (const auto &entry : dbc.transport_rotation().entries()) {
    if (entry.transport_id == transport_key) {
      rotation_entries.push_back(entry);
    }
  }

  std::sort(rotation_entries.begin(), rotation_entries.end(),
            [](const auto &lhs, const auto &rhs) {
              if (lhs.time_index != rhs.time_index) {
                return lhs.time_index < rhs.time_index;
              }
              return lhs.id < rhs.id;
            });

  path.rotation_keyframes.reserve(rotation_entries.size());
  for (const auto &entry : rotation_entries) {
    path.rotation_keyframes.push_back({
        .timeIndex = entry.time_index,
        .x = entry.x,
        .y = entry.y,
        .z = entry.z,
        .w = entry.w,
    });
  }

  return path;
}

void RefreshTransportRuntimeRegistration(CGGameObject_C &game_object) {
  auto *const objects = game_object.object_manager();
  if (objects == nullptr) {
    return;
  }

  auto &transport_manager = objects->transport_manager();

  if (!game_object.IsTransport()) {
    return;
  }

  const auto path = BuildTransportPathInfoFromDbc(
      game_object, objects->dbc_loader(), objects->GetMapId());
  if (!path.has_value()) {
    transport_manager.OnTransportDestroy(game_object.GetGuid());
    return;
  }

  transport_manager.OnTransportCreate(game_object.GetGuid(), game_object.GetEntry(),
                                      game_object.GetDisplayId(), *path);
  const auto state = static_cast<std::uint8_t>(game_object.GetGoState());
  SyncTransportAnimationClock(game_object, state, state);
}

[[nodiscard]] bool ActivePlayerCanSatisfyOpenLockRequirement(
    const CGGameObject_C &game_object, const openwow::data::dbc::DbcLoader &dbc,
    const SpellTargeting &targeting, const std::uint32_t lock_index,
    const std::uint32_t required_skill,
    std::uint32_t *const matched_spell_id_out = nullptr) {
  const auto required_value = required_skill != 0u ? required_skill : 5u * game_object.GetLevel();

  for (const auto &known_spell : SpellbookSystem::Get().GetKnownSpellList()) {
    const auto *const spell = LookupSpellEntry(dbc, known_spell.spell_id);
    if (spell != nullptr && SpellSatisfiesOpenLockRequirement(*spell, lock_index, required_value)) {
      if (matched_spell_id_out != nullptr) {
        *matched_spell_id_out = known_spell.spell_id;
      }
      return true;
    }
  }

  const auto targeting_state = targeting.GetState();
  if (!targeting_state.isActive || targeting_state.spellId == 0u) {
    return false;
  }

  const auto *const current_spell = LookupSpellEntry(dbc, targeting_state.spellId);
  if (current_spell == nullptr ||
      !SpellSatisfiesOpenLockRequirement(*current_spell, lock_index, required_value)) {
    return false;
  }
  if (matched_spell_id_out != nullptr) {
    *matched_spell_id_out = targeting_state.spellId;
  }
  return true;
}

[[nodiscard]] bool HasUnsatisfiedLockedRequirement(const CGGameObject_C &game_object,
                                                   const PlayerInventoryReplica& inventory,
                                                   const openwow::data::dbc::DbcLoader &dbc,
                                                   const SpellTargeting &targeting) {
  const auto *const lock_entry = game_object.GetLockEntry();
  if (lock_entry == nullptr) {
    return false;
  }

  bool has_known_requirement = false;
  for (std::size_t index = 0; index < lock_entry->type.size(); ++index) {
    switch (lock_entry->type[index]) {
    case 1u:
      has_known_requirement = true;
      if (IsLockActionApplicable(game_object, lock_entry->action[index]) &&
          inventory.FindItemByEntry(lock_entry->index[index]) >= 0) {
        return false;
      }
      break;

    case 2u:
      has_known_requirement = true;
      if (IsLockActionApplicable(game_object, lock_entry->action[index]) &&
          ActivePlayerCanSatisfyOpenLockRequirement(game_object, dbc, targeting,
                                                    lock_entry->index[index],
                                                    lock_entry->skill[index])) {
        return false;
      }
      break;

    case 3u: {
      has_known_requirement = true;
      if (!IsLockActionApplicable(game_object, lock_entry->action[index])) {
        break;
      }

      const auto *const spell = LookupSpellEntry(dbc, lock_entry->index[index]);
      if (spell != nullptr && SpellHasAnyOpenLockEffect(*spell)) {
        return false;
      }
      break;
    }

    default:
      break;
    }
  }

  return has_known_requirement;
}

[[nodiscard]] std::uint32_t ResolveLockOpeningSpellId(const CGGameObject_C &game_object,
                                                      const WorldSession &session) {
  const auto *const lock_entry = game_object.GetLockEntry();
  const auto *const dbc = session.GetDbcLoader();
  if (lock_entry == nullptr || dbc == nullptr) {
    return 0u;
  }

  const auto &targeting = session.spells().GetTargeting();
  for (std::size_t index = 0; index < lock_entry->type.size(); ++index) {
    if (!IsLockActionApplicable(game_object, lock_entry->action[index])) {
      continue;
    }

    switch (lock_entry->type[index]) {
    case 2u: {
      std::uint32_t matched_spell_id = 0u;
      if (ActivePlayerCanSatisfyOpenLockRequirement(game_object, *dbc, targeting,
                                                     lock_entry->index[index],
                                                     lock_entry->skill[index],
                                                     &matched_spell_id)) {
        return matched_spell_id;
      }
      break;
    }

    case 3u: {
      const auto *const spell = LookupSpellEntry(*dbc, lock_entry->index[index]);
      if (spell != nullptr && SpellHasAnyOpenLockEffect(*spell)) {
        return lock_entry->index[index];
      }
      break;
    }

    default:
      break;
    }
  }

  return 0u;
}

[[nodiscard]] bool TryResolveLockSpellMaxRange(const WorldSession &session,
                                                const CGPlayer_C &active_player,
                                                const std::uint32_t spell_id,
                                                float &max_range) {
  if (spell_id == 0u) {
    return false;
  }

  if (const auto query = SpellQueryBridge::Get().Query(spell_id);
      query.has_value() && query->range > 0.0f) {
    max_range = query->range;
    return true;
  }

  const auto *const dbc = session.GetDbcLoader();
  const auto *const spell = dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
  if (spell == nullptr) {
    return false;
  }

  const auto *const range_entry =
      spell->range_index != 0u ? dbc->spell_range().LookupEntry(spell->range_index) : nullptr;
  const auto range_window = SpellTargetValidator::GetUntargetedRangeWindow(
      *spell, range_entry, active_player, false, &session);
  if (range_window.max_range <= 0.0f) {
    return false;
  }

  max_range = range_window.max_range;
  return true;
}

void SyncTransportAnimationClock(CGGameObject_C &game_object,
                                 const std::uint8_t previous_state_byte,
                                 const std::uint8_t current_state_byte) {
  if (!game_object.IsAnyTransport()) {
    return;
  }

  auto *const objects = game_object.object_manager();
  if (objects == nullptr) {
    return;
  }

  auto *transport = objects->transport_manager().GetTransportMutable(game_object.GetGuid());
  if (transport == nullptr) {
    return;
  }

  const auto absolute_tick_ms = openwow::core::GameClock::GetTickCount32();

  if (game_object.IsMOTransport()) {

    return;
  }

  transport->SyncGameObjectAnimationState(absolute_tick_ms, previous_state_byte, current_state_byte,
                                          game_object.GetTransportPathProgress());
}

void ResetTransportHandlerCommittedState(CGGameObject_C &game_object) {
  if (game_object.GetGoType() == GameObjectType::Trapdoor) {
    game_object.ResetTrapdoorHandlerCommittedState();
    return;
  }

  if (!game_object.IsAnyTransport()) {
    return;
  }

  auto *const objects = game_object.object_manager();
  if (objects == nullptr) {
    return;
  }

  auto *transport = objects->transport_manager().GetTransportMutable(game_object.GetGuid());
  if (transport == nullptr) {
    return;
  }

  transport->ResetHandlerCommittedState();
}

constexpr std::array<std::array<std::uint8_t, 24>, kMaxGameObjectType>
    kTemplateFieldIdsByGameObjectType = {{
        {1, 4, 3, 57, 89, 90, 117, 85},
        {1, 4, 3, 30, 57, 48, 89, 90, 55, 85},
        {4, 5, 17, 38, 22, 57, 89, 55, 91, 48, 85},
        {4, 6, 7, 8, 25, 26, 13, 30, 20, 31, 55, 56, 62, 66, 89, 92, 19, 85},
        {},
        {19, 18, 37, 48, 63, 20, 85},
        {4, 31, 24, 10, 9, 23, 3, 32, 37, 65, 48, 58, 89, 90, 123, 85},
        {11, 12, 94, 13, 85},
        {14, 24, 30, 37, 20, 48, 19, 63, 85},
        {15, 16, 17, 91, 85},
        {4, 20, 21, 3, 22, 8, 23, 15, 16, 17, 10, 57, 30, 48, 89, 90, 55, 91, 19, 38,
         127, 63, 85},
        {93, 1, 3, 125, 126, 87},
        {4, 24, 27, 28, 29, 3, 89, 90},
        {4, 33, 21, 89, 85},
        {},
        {35, 36, 43, 44, 45, 99, 87, 67, 128},
        {},
        {},
        {34, 10, 39, 40, 49, 50, 54, 46, 85},
        {85},
        {},
        {42, 9},
        {10, 9, 47, 91, 48, 85},
        {51, 52, 53},
        {4, 59, 24, 60, 61, 57, 89, 55, 85},
        {24, 6, 25, 26, 4},
        {4, 21, 59, 57, 89},
        {64},
        {},
        {24, 10, 67, 68, 70, 71, 72, 73, 74, 75, 76, 77, 78, 69, 79, 80, 81, 82, 48,
         18, 118, 119},
        {1, 24, 83, 85, 84, 86, 37},
        {87, 88},
        {12, 121},
        {95, 97, 103, 109, 103, 96, 103, 103, 103, 110, 103, 103, 103, 103, 111, 103,
         112, 103, 124, 114, 103, 103, 120, 103},
        {85},
        {93, 1, 3},
    }};

std::optional<std::uint32_t> TryResolveTemplateFieldRawIndex(const GameObjectType type,
                                                             const std::uint32_t field_id) {
  const auto type_index = static_cast<std::size_t>(type);
  if (type_index >= kTemplateFieldIdsByGameObjectType.size() || field_id == 0u ||
      field_id > 128u) {
    return std::nullopt;
  }

  const auto &field_ids = kTemplateFieldIdsByGameObjectType[type_index];
  for (std::size_t raw_index = 0; raw_index < field_ids.size(); ++raw_index) {
    const auto candidate = field_ids[raw_index];
    if (candidate == 0u) {
      break;
    }
    if (candidate == field_id) {
      return static_cast<std::uint32_t>(raw_index);
    }
  }
  return std::nullopt;
}

std::uint32_t ResolveTemplateFieldValue(const GameObjectTemplateInfo *const template_info,
                                        const GameObjectType type, const std::uint32_t field_id) {
  if (template_info == nullptr) {
    return 0;
  }

  const auto raw_index = TryResolveTemplateFieldRawIndex(type, field_id);
  if (!raw_index.has_value() || *raw_index >= template_info->raw_data.size()) {
    return 0;
  }

  return template_info->raw_data[*raw_index];
}

[[nodiscard]] WaveOscillationParams BuildMOTransportWaveParams(
    const openwow::data::dbc::TransportPhysicsEntry &physics) {
  return {
      .waveAmplitude = physics.wave_amp,
      .waveTimeScale = physics.wave_time_scale,
      .rollAmplitude = physics.roll_amp,
      .rollTimeScale = physics.roll_time_scale,
      .pitchAmplitude = physics.pitch_amp,
      .pitchTimeScale = physics.pitch_time_scale,
      .maxBank = physics.max_bank,
      .maxBankTurnSpeed = physics.max_bank_turn_speed,
      .speedDampThreshold = physics.speed_damp_thresh,
      .speedDamp = physics.speed_damp,
  };
}

[[nodiscard]] MOTransportTimedPathState BuildMOTransportPathState(
    const CGGameObject_C &game_object, const openwow::data::dbc::DbcLoader &dbc) {
  const std::uint32_t taxi_path_id =
      game_object.GetTemplateFieldValue(kMOTransportTaxiPathFieldId);
  std::vector<TaxiPathNodeRaw> nodes;
  for (const auto &entry : dbc.taxi_path_node().entries()) {
    if (entry.path_id == taxi_path_id) {

      nodes.push_back({
          .id = entry.id,
          .pathId = entry.path_id,
          .sequenceIndex = entry.node_index,
          .mapId = entry.map_id,
          .x = entry.x,
          .y = entry.y,
          .z = entry.z,
          .flags = entry.flags,
          .delaySeconds = entry.delay,
          .arrivalEventId = entry.arrival_event_id,
          .departureEventId = entry.departure_event_id,
      });
    }
  }

  const auto physics_id = game_object.GetTemplateFieldValue(kMOTransportPhysicsFieldId);
  const auto *const physics = dbc.transport_physics().LookupEntry(physics_id);
  const auto wave_params =
      physics != nullptr ? std::optional(BuildMOTransportWaveParams(*physics)) : std::nullopt;

  MOTransportTimedPathState path_state;
  path_state.BuildFromTaxiPathNodes(
      nodes,
      static_cast<float>(game_object.GetTemplateFieldValue(kMOTransportMaxVelocityFieldId)),
      static_cast<float>(game_object.GetTemplateFieldValue(kMOTransportAccelerationFieldId)),
      wave_params.has_value() ? &*wave_params : nullptr);

  path_state.SetCycleDuration(game_object.GetLevel());
  return path_state;
}

[[nodiscard]] std::uint32_t ResolveUseInteractionPointCount(const CGGameObject_C &game_object) {
  switch (game_object.GetGoType()) {
  case GameObjectType::Chair:
    return game_object.GetTemplateFieldValue(kChairSlotsFieldId);
  case GameObjectType::BarberChair:
    return 1u;
  default:
    return 1u;
  }
}

void BuildUseInteractionTransform(const CGGameObject_C &game_object, float *const out_matrix) {
  const auto position = game_object.GetPosition();
  const auto [rotation_x, rotation_y, rotation_z, rotation_w] = game_object.GetWorldRotation();

  auto matrix = openwow::render::BuildRotationMatrix4x4Quaternion(
      openwow::render::RenderVec4{rotation_x, rotation_y, rotation_z, rotation_w});

  const float scale = game_object.GetScale();
  matrix = openwow::render::ScaleMatrix4x4BasisRows(
      matrix, openwow::render::RenderVec3{scale, scale, scale});
  matrix[12] = position.x;
  matrix[13] = position.y;
  matrix[14] = position.z;
  std::copy(matrix.begin(), matrix.end(), out_matrix);
}

[[nodiscard]] std::vector<std::array<float, 3>>
BuildUseInteractionPoints(const CGGameObject_C &game_object) {
  const std::uint32_t point_count = ResolveUseInteractionPointCount(game_object);
  std::vector<std::array<float, 3>> points;
  if (point_count == 0u) {
    return points;
  }

  points.resize(point_count);

  float world_transform[16];
  BuildUseInteractionTransform(game_object, world_transform);

  float local_point[3] = {
      0.0f,
      -((static_cast<float>(point_count) - 1.0f) * 0.5f),
      0.0f,
  };

  for (auto &point : points) {
    openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4Unbuffered(
        point.data(), local_point, world_transform);
    local_point[1] += 1.0f;
  }

  return points;
}

bool EvaluateDungeonDifficultyVisibility(const CGGameObject_C &game_object,
                                         const WorldSession &session) {
  if (game_object.GetGoType() != GameObjectType::DungeonDifficulty) {
    return true;
  }

  const auto required_difficulty = game_object.GetRequiredInstanceDifficultyIndex();
  {
    const auto *const map_entry = session.LookupMapEntry(game_object.GetRequiredInstanceMapId());
    if (map_entry != nullptr &&
        map_entry->map_type == static_cast<std::uint32_t>(openwow::data::dbc::MapType::kRaid)) {
      auto &group_system = GroupSystem::Get();
      if ((map_entry->flags & kMapAllowsPlayerDifficultyFlag) != 0u) {
        return group_system.GetEffectiveRaidMapDifficultyIndex(true) == required_difficulty;
      }

      const auto active_raid_difficulty =
          static_cast<std::uint32_t>(group_system.GetRaidDifficulty());
      if (active_raid_difficulty == required_difficulty) {
        return true;
      }

      const auto *const difficulty_entry =
          session.LookupMapDifficultyEntry(
              game_object.GetRequiredInstanceMapId(),
              static_cast<std::uint8_t>(active_raid_difficulty));
      if (difficulty_entry == nullptr) {
        return true;
      }

      return false;
    }
  }

  return static_cast<std::uint32_t>(GroupSystem::Get().GetDungeonDifficulty()) ==
         required_difficulty;
}

[[nodiscard]] std::uint8_t ResolveDestructibleStateIndex(const std::uint32_t flags) {
  return static_cast<std::uint8_t>(((flags >> 8u) & 0xEu) >> 1u);
}

[[nodiscard]] bool HasRenderableGameObjectDisplay(const openwow::data::dbc::DbcLoader *const dbc,
                                                  const std::uint32_t display_id) {
  if (display_id == 0u) {
    return false;
  }

  if (dbc == nullptr) {
    return true;
  }

  const auto *const entry = dbc->gameobject_display_info().LookupEntry(display_id);
  return entry != nullptr && !entry->filename.empty();
}

}

float GetDefaultInteractDistance(GameObjectType type) {

  return GetTypeHandlerInfo(static_cast<std::uint8_t>(type)).interact_dist;
}

CGGameObject_C::CGGameObject_C(PlayerInventoryReplica& inventory)
    : CGObject_C(TypeID::kGameObject), inventory_(inventory) {}

CGGameObject_C::CGGameObject_C(PlayerInventoryReplica& inventory, ObjectGuid guid)
    : CGObject_C(guid, TypeID::kGameObject), inventory_(inventory) {}

CGGameObject_C::CGGameObject_C(ObjectManager& objects,
                               PlayerInventoryReplica& inventory,
                               ObjectGuid guid)
    : CGObject_C(objects, guid, TypeID::kGameObject), inventory_(inventory) {}

CGGameObject_C::~CGGameObject_C() {
  ReleasePlayerNameDescriptor();
  ClearLoadedModelState();
}

float CGGameObject_C::GetModelOpacity() const {
  if (GetGoType() == GameObjectType::DungeonDifficulty) {
    return GetDungeonDifficultyModelOpacity(*this);
  }
  return 1.0f;
}

void CGGameObject_C::PrepareForWorldRemoval() {
  CGObject_C::PrepareForWorldRemoval();

  ClearAttachmentsForWorldRemoval();

  ClearLoadedModelState();

  ClearTrackedPositionalSound();
  ReleasePlayerNameDescriptor();
}

std::vector<std::uint16_t> CGGameObject_C::ApplyCreateUpdate(const CreateObjectUpdate &upd) {
  auto changed = CGObject_C::ApplyCreateUpdate(upd);

  if (upd.defer_post_init) {
    return changed;
  }
  FinalizeGameObjectCreateState(upd);
  RefreshPlayerNameDescriptor();
  return changed;
}

void CGGameObject_C::FinalizeCreateUpdate(const CreateObjectUpdate &upd) {
  CGObject_C::FinalizeCreateUpdate(upd);
  FinalizeGameObjectCreateState(upd);
  if (template_info_ != nullptr) {
    RefreshPlayerNameDescriptor();
  }
}

void CGGameObject_C::FinalizeWorldPublication() {
  CGObject_C::FinalizeWorldPublication();

  if (template_info_ == nullptr) {
    return;
  }

  switch (GetGoType()) {
  case GameObjectType::MapObject:
  case GameObjectType::Transport:
  case GameObjectType::MOTransport:
  case GameObjectType::Trapdoor:
    InitModelByType();
    RefreshTransportRuntimeRegistration(*this);
    break;

  case GameObjectType::DestructibleBuilding:

    RefreshDestructibleVisualControlState();
    break;

  default:
    break;
  }

  if (IsMOTransport()) {
    const auto state = static_cast<std::uint8_t>(GetGoState());
    RefreshMOTransportAnimationControl(state, state);
    SyncTransportAnimationClock(*this, state, state);
  } else if (GetGoType() == GameObjectType::Trapdoor) {
    const auto state = static_cast<std::uint8_t>(GetGoState());
    OnTrapdoorStateByteChanged(state, state);
  }
}

void CGGameObject_C::FinalizePacketUpdatePromotion() {

  CGObject_C::FinalizePacketUpdatePromotion();
  if (template_info_ != nullptr) {
    ResetTransportHandlerCommittedState(*this);
  }
  RefreshPlayerNameDescriptor(true);
}

void CGGameObject_C::SynchronizeModelSpatialBounds(
    const std::array<float, 6>& world_bounds) {
  const bool has_strict_bounds =
      std::isfinite(world_bounds[0]) && std::isfinite(world_bounds[1]) &&
      std::isfinite(world_bounds[2]) && std::isfinite(world_bounds[3]) &&
      std::isfinite(world_bounds[4]) && std::isfinite(world_bounds[5]) &&
      world_bounds[0] < world_bounds[3] && world_bounds[1] < world_bounds[4] &&
      world_bounds[2] < world_bounds[5];

  const bool model_bounds_became_ready = has_strict_bounds && !HasObjectBoundingBox();

  if (has_strict_bounds) {
    SetObjectBoundingBox(world_bounds.data());
  } else {
    ClearObjectBoundingBox();
  }

  if (!model_bounds_became_ready) {
    return;
  }

  switch (GetGoType()) {
  case GameObjectType::Transport:
  case GameObjectType::MapObject:
  case GameObjectType::MOTransport:
  case GameObjectType::DestructibleBuilding:
  case GameObjectType::Trapdoor:

    return;

  case GameObjectType::Door:

    if (m2_go_animation_control_.state_index != kGoAnimStateDoorSolid) {
      return;
    }
    break;

  default:
    break;
  }

  cached_interaction_value_ = 1u;
}

void CGGameObject_C::SynchronizeRenderAssetReadiness(const bool ready) {
  if (!loaded_model_state_) {
    render_readiness_state_ = {};
    return;
  }

  render_readiness_state_ = {};
  if (loaded_model_state_->is_wmo) {
    render_readiness_state_.kind = render::RuntimeRenderAssetKind::kAreaScene;
    render_readiness_state_.area_scene_primary_ready = ready;
    render_readiness_state_.area_scene_dependencies_ready = ready;
  } else {
    render_readiness_state_.kind = render::RuntimeRenderAssetKind::kM2;
    render_readiness_state_.m2_payload_bound = true;
    render_readiness_state_.m2_payload_ready = ready;
  }
}

void CGGameObject_C::SynchronizeMOTransportModelReadiness(const bool ready) {
  if (ready && loaded_model_state_) {
    mo_transport_model_ready_latched_ = true;
  }
}

void CGGameObject_C::SynchronizeModelLocalBounds(
    const std::optional<std::array<float, 6>> &local_bounds) {
  model_local_bounds_ = local_bounds;
}

void CGGameObject_C::SynchronizeModelConvexVolumePlanes(
    std::optional<std::vector<std::array<float, 4>>> planes) {
  model_convex_volume_planes_ = std::move(planes);
}

bool CGGameObject_C::ContainsLocalPoint(
    const std::array<float, 3> &local_point) const {
  switch (GetGoType()) {
  case GameObjectType::Transport:
  case GameObjectType::MOTransport:
  case GameObjectType::Trapdoor:
    break;
  default:

    return false;
  }

  if (!loaded_model_state_.has_value()) {

    return true;
  }

  if (loaded_model_state_->is_wmo) {
    if (!model_convex_volume_planes_.has_value()) {
      return true;
    }
    for (const std::array<float, 4> &plane : *model_convex_volume_planes_) {
      const float signed_distance = plane[0] * local_point[0] +
                                    plane[1] * local_point[1] +
                                    plane[2] * local_point[2] + plane[3];
      if (signed_distance > 0.0f) {
        return false;
      }
    }
    return true;
  }

  if (!model_local_bounds_.has_value()) {
    return true;
  }

  const auto &bounds = *model_local_bounds_;

  constexpr float kTransportContainmentTopExtension = 1.6404124f + 1.0f / 72.0f;

  const float x = local_point[0];
  const float y = local_point[1];
  const float z = local_point[2];
  return x <= bounds[3] && x >= bounds[0] && y <= bounds[4] && y >= bounds[1] &&
         z >= bounds[2] && z <= bounds[5] + kTransportContainmentTopExtension;
}

void CGGameObject_C::FinalizeGameObjectCreateState(
    const CreateObjectUpdate &upd) {

  InitModelByType();

  flag_visual_control_.current_flags = GetFlags();
  art_kit_visual_control_.art_kit = GetGoArtKit();
  transport_path_progress_control_.path_progress = GetTransportPathProgress();
  RefreshDestructibleVisualControlState();

  if (upd.fields.HasField(GAMEOBJECT_BYTES_1)) {
    OnGoStateByteChanged(0);
  }

  TransitionM2GoAnimationState(ResolveGoAnimationStateIndex());

  RefreshTransportRuntimeRegistration(*this);

  RefreshLootArtVisualControlState();
  (void)RefreshTrackedPositionalSound();

}

bool CGGameObject_C::ApplyMovementUpdate(const MovementOnlyUpdate &upd) {
  (void)CGObject_C::ApplyMovementUpdate(upd);
  RefreshTransportRuntimeRegistration(*this);
  (void)RefreshTrackedPositionalSound();
  return true;
}

bool CGGameObject_C::UpdateModelNodeTransform(const float dt,
                                              const std::uint32_t current_tick_ms) {

  (void)AdvanceCompletedModelAnimState(current_tick_ms);

  if (IsMOTransport()) {

    AdvanceMOTransportPathStateForFrame(
        openwow::core::GameClock::Instance().FrameCount());
    return true;
  }
  return CGObject_C::UpdateModelNodeTransform(dt, current_tick_ms);
}

void CGGameObject_C::AdvanceMOTransportPathStateForFrame(
    const std::uint64_t frame_stamp) {
  if (mo_transport_advance_frame_stamp_.has_value() &&
      *mo_transport_advance_frame_stamp_ == frame_stamp) {
    return;
  }
  mo_transport_advance_frame_stamp_ = frame_stamp;
  AdvanceMOTransportPathState();
}

bool CGGameObject_C::AttachTrackedPositionalSound(const std::uint32_t handle_id) {
  tracked_positional_sound_handle_ = handle_id;
  return RefreshTrackedPositionalSound();
}

void CGGameObject_C::ClearTrackedPositionalSound() {
  if (tracked_positional_sound_handle_ != 0u) {

    (void)sound_runtime().StopActiveSoundHandle(
        tracked_positional_sound_handle_, true, -1.0f, true);
  }
  tracked_positional_sound_handle_ = 0;
}

bool CGGameObject_C::RefreshTrackedPositionalSound() {
  if (tracked_positional_sound_handle_ == 0) {
    return false;
  }

  auto &sound_interface = sound_runtime();
  if (!sound_interface.GetSoundHandle(tracked_positional_sound_handle_).has_value()) {
    tracked_positional_sound_handle_ = 0;
    return false;
  }

  const auto position = GetPosition();
  const std::array<float, 3> sound_position = {position.x, position.y, position.z};
  if (!sound_interface.SetSoundHandlePosition(tracked_positional_sound_handle_,
                                              sound_position.data())) {
    tracked_positional_sound_handle_ = 0;
    return false;
  }

  return true;
}

bool CGGameObject_C::HandlePrimaryModelSoundEvent(
    openwow::world::WorldCamera* const camera,
    const std::uint32_t event_fourcc, const std::uint32_t data,
    const float* const position) {
  if (GetGoType() == GameObjectType::DestructibleBuilding) {
    return HandleDestructibleBuildingSoundEvent(
        *this, camera, event_fourcc, data, position,
        &tracked_positional_sound_handle_);
  }
  return HandleGameObjectSoundEvent(
      *this, camera, event_fourcc, data, position,
      &tracked_positional_sound_handle_);
}

std::vector<std::uint16_t> CGGameObject_C::ApplyValuesUpdate(const ValuesUpdate &upd) {
  const auto previous_state_byte = static_cast<std::uint8_t>(GetGoState());
  const auto previous_art_kit = GetGoArtKit();
  const auto previous_flags = GetFlags();
  const auto previous_path_progress = GetTransportPathProgress();
  auto changed = CGObject_C::ApplyValuesUpdate(upd);

  if (template_info_) {
    ResetTransportHandlerCommittedState(*this);
  }

  const bool refresh_transport_runtime =
      std::find(changed.begin(), changed.end(), GAMEOBJECT_DISPLAYID) != changed.end() ||
      std::find(changed.begin(), changed.end(), GAMEOBJECT_LEVEL) != changed.end() ||
      std::find(changed.begin(), changed.end(), GAMEOBJECT_PARENTROTATION + 0) != changed.end() ||
      std::find(changed.begin(), changed.end(), GAMEOBJECT_PARENTROTATION + 1) != changed.end() ||
      std::find(changed.begin(), changed.end(), GAMEOBJECT_PARENTROTATION + 2) != changed.end() ||
      std::find(changed.begin(), changed.end(), GAMEOBJECT_PARENTROTATION + 3) != changed.end();
  if (refresh_transport_runtime) {
    if (IsMOTransport()) {
      InitVisuals();
    } else {
      RefreshTransportRuntimeRegistration(*this);
    }
  }

  if (std::find(changed.begin(), changed.end(), GAMEOBJECT_BYTES_1) != changed.end()) {
    OnGoStateByteChanged(previous_state_byte);
    if (GetGoArtKit() != previous_art_kit) {
      RefreshArtKitVisualControlState(previous_art_kit);
    }
  }

  const bool flags_changed =
      std::find(changed.begin(), changed.end(), GAMEOBJECT_FLAGS) != changed.end();
  if (flags_changed) {
    RefreshFlagVisualControlState(previous_flags);
  } else {
    flag_visual_control_.current_flags = GetFlags();
    flag_visual_control_.changed_mask = 0u;
  }

  if (std::find(changed.begin(), changed.end(), GAMEOBJECT_DYNAMIC) != changed.end()) {
    if (GetTransportPathProgress() != previous_path_progress) {
      RefreshTransportPathProgressControlState(previous_path_progress);
    } else {
      transport_path_progress_control_.path_progress = GetTransportPathProgress();
    }
  }

  RefreshDestructibleVisualControlState(
      flags_changed ? flag_visual_control_.changed_mask : 0u);
  if (template_info_ != nullptr) {
    RefreshPlayerNameDescriptor();
  }

  RefreshLootArtVisualControlState();
  if (std::find(changed.begin(), changed.end(), GAMEOBJECT_BYTES_1) == changed.end() ||
      GetGoArtKit() == previous_art_kit) {
    art_kit_visual_control_.art_kit = GetGoArtKit();
  }

  return changed;
}

ObjectGuid CGGameObject_C::GetCreatedBy() const {
  return GetGuidField(GAMEOBJECT_FIELD_CREATED_BY);
}

std::uint32_t CGGameObject_C::GetDisplayId() const {
  return GetUInt32(GAMEOBJECT_DISPLAYID);
}

std::uint32_t CGGameObject_C::GetRenderDisplayId() const {
  if (IsDestructibleBuilding()) {
    return destructible_visual_control_.active_render_display_id;
  }

  return GetDisplayId();
}

std::uint32_t CGGameObject_C::GetFlags() const {
  return GetUInt32(GAMEOBJECT_FLAGS);
}

std::uint32_t CGGameObject_C::GetFaction() const {
  return GetUInt32(GAMEOBJECT_FACTION);
}

int CGGameObject_C::GetReactionLevel(const CGUnit_C &unit) const {

  const ObjectGuid creator = GetCreatedBy();
  if (!creator.IsEmpty()) {
    const auto *creator_unit = object_manager() != nullptr
                                   ? object_manager()->GetUnit(creator)
                                   : nullptr;
    if (creator_unit != nullptr) {
      return static_cast<int>(creator_unit->Interaction().GetReaction(unit));
    }
  }

  const std::uint32_t go_faction = GetFaction();
  if (go_faction != 0 && object_manager() != nullptr) {
    const std::uint32_t unit_faction = unit.State().GetFactionTemplate();
    if (unit_faction != 0) {
      return static_cast<int>(data::dbc::ComputeFactionReaction(
          go_faction, unit_faction,
          object_manager()->dbc_loader().faction_template()));
    }
  }

  return 3;
}

std::uint32_t CGGameObject_C::GetLevel() const {
  return GetUInt32(GAMEOBJECT_LEVEL);
}

std::uint32_t CGGameObject_C::GetDynamic() const {
  return GetUInt32(GAMEOBJECT_DYNAMIC);
}

std::uint32_t CGGameObject_C::GetGoBytes1() const {
  return GetUInt32(GAMEOBJECT_BYTES_1);
}

GOState CGGameObject_C::GetGoState() const {
  return static_cast<GOState>(GetGoBytes1() & 0xFF);
}

GameObjectType CGGameObject_C::GetGoType() const {
  return static_cast<GameObjectType>((GetGoBytes1() >> 8) & 0xFF);
}

std::uint8_t CGGameObject_C::GetGoArtKit() const {
  return static_cast<std::uint8_t>((GetGoBytes1() >> 16) & 0xFF);
}

std::uint8_t CGGameObject_C::GetGoAnimProgress() const {
  return static_cast<std::uint8_t>((GetGoBytes1() >> 24) & 0xFF);
}

void CGGameObject_C::SetGoState(GOState state) {
  const auto previous_state_byte = static_cast<std::uint8_t>(GetGoState());
  std::uint32_t bytes1 = GetGoBytes1();
  bytes1 = (bytes1 & 0xFFFFFF00u) | static_cast<std::uint8_t>(state);

  if (GAMEOBJECT_BYTES_1 < fields_.size()) {
    fields_[GAMEOBJECT_BYTES_1] = bytes1;
    OnGoStateByteChanged(previous_state_byte);
  }
}

void CGGameObject_C::SetGoAnimProgress(std::uint8_t progress) {
  const auto previous_state_byte = static_cast<std::uint8_t>(GetGoState());
  std::uint32_t bytes1 = GetGoBytes1();
  bytes1 = (bytes1 & 0x00FFFFFFu) | (static_cast<std::uint32_t>(progress) << 24);
  if (GAMEOBJECT_BYTES_1 < fields_.size()) {
    fields_[GAMEOBJECT_BYTES_1] = bytes1;
    OnGoStateByteChanged(previous_state_byte);
  }
}

void CGGameObject_C::ApplyTransientGoStateByte(const std::uint8_t state_byte) {
  const auto previous_state_byte = cached_go_state_byte_;
  if (state_byte != previous_state_byte || IsMOTransport()) {
    DispatchGoStateByteCallback(previous_state_byte, state_byte);
  }
}

void CGGameObject_C::HandleResetStatePacket() {
  OnGoStateByteChanged(cached_go_state_byte_);
}

void CGGameObject_C::HandleServerCustomAnimation(const std::uint32_t anim_id) {
  if (anim_id >= 4u) {
    return;
  }

  QueueModelAnimationRequest(static_cast<std::uint8_t>(anim_id + 2u));
}

void CGGameObject_C::HandleServerDespawnAnimation() {
  QueueModelAnimationRequest(6u);
}

bool CGGameObject_C::HasFlag(GameObjectFlags flag) const {
  return (GetFlags() & flag) != 0;
}

void CGGameObject_C::SetFlag(GameObjectFlags flag) {
  if (GAMEOBJECT_FLAGS < fields_.size()) {
    const auto previous_flags = GetFlags();
    fields_[GAMEOBJECT_FLAGS] |= flag;
    RefreshFlagVisualControlState(previous_flags);
  }
}

void CGGameObject_C::RemoveFlag(GameObjectFlags flag) {
  if (GAMEOBJECT_FLAGS < fields_.size()) {
    const auto previous_flags = GetFlags();
    fields_[GAMEOBJECT_FLAGS] &= ~static_cast<std::uint32_t>(flag);
    RefreshFlagVisualControlState(previous_flags);
  }
}

std::uint16_t CGGameObject_C::GetDynFlags() const {
  return static_cast<std::uint16_t>(GetDynamic() & 0xFFFF);
}

Position CGGameObject_C::GetRawPosition() const {
  return {GetX(), GetY(), GetZ(), GetOrientation()};
}

Position CGGameObject_C::GetNamePlatePosition() const {
  if (GetGoType() == GameObjectType::DestructibleBuilding) {
    auto pos = GetPosition();
    if (destructible_nameplate_model_height_.has_value()) {

      pos.z += *destructible_nameplate_model_height_;
      return pos;
    }
    pos.z += GetModelBoundingBoxHeight() * GetNativeScale() * 1.25f;
    return pos;
  }

  auto pos = GetPosition();
  pos.z += GetModelBoundingBoxHeight() * GetNativeScale() * 1.25f;
  return pos;
}

void CGGameObject_C::SynchronizeDestructibleNameplateModelHeight(
    std::optional<float> height) noexcept {
  if (!height.has_value() || !std::isfinite(*height)) {
    destructible_nameplate_model_height_.reset();
    return;
  }
  destructible_nameplate_model_height_ = *height;
}

float CGGameObject_C::GetFacing() const {
  const auto [x, y, z, w] = GetWorldRotation();
  return FacingFromQuaternion({x, y, z, w});
}

std::tuple<float, float, float, float> CGGameObject_C::GetWorldRotation() const {
  openwow::math::quaternion_xyzw::Quaternion local_rotation{};

  if (position_.HasUpdateFlag(kUpdateFlagRotation)) {
    openwow::math::packed_quaternion64::Decompress(position_.go_rotation, local_rotation.x,
                                                   local_rotation.y, local_rotation.z,
                                                   local_rotation.w);
  } else {
    const Rotation descriptor_rotation = GetParentRotation();
    if (!IsZeroQuaternion(descriptor_rotation)) {
      local_rotation = {
          descriptor_rotation.x,
          descriptor_rotation.y,
          descriptor_rotation.z,
          descriptor_rotation.w,
      };
    } else {
      const float half_facing = GetOrientation() * 0.5f;
      local_rotation = {
          0.0f,
          0.0f,
          std::sin(half_facing),
          std::cos(half_facing),
      };
    }
  }

  ObjectGuid parent_guid;
  std::array<float, 3> local_position{};
  if (!TryGetRelativePosition(parent_guid, local_position) || parent_guid.IsEmpty()) {
    return {local_rotation.x, local_rotation.y, local_rotation.z, local_rotation.w};
  }

  const auto* const objects = object_manager();
  if (objects == nullptr) {
    return {local_rotation.x, local_rotation.y, local_rotation.z,
            local_rotation.w};
  }
  float parent_rotation_xyzw[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  Movement_GetObjectWorldRotation(
      *objects, parent_guid.GetRawValue(), parent_rotation_xyzw);
  const auto world_rotation =
      openwow::math::quaternion_xyzw::Multiply(local_rotation, {
                                                                   parent_rotation_xyzw[0],
                                                                   parent_rotation_xyzw[1],
                                                                   parent_rotation_xyzw[2],
                                                                   parent_rotation_xyzw[3],
                                                               });
  return {world_rotation.x, world_rotation.y, world_rotation.z, world_rotation.w};
}

bool CGGameObject_C::IsActivated() const {
  return (GetDynFlags() & GO_DYNFLAG_LO_ACTIVATE) != 0;
}

bool CGGameObject_C::HasQuestSparkle() const {
  return (GetDynFlags() & GO_DYNFLAG_LO_SPARKLE) != 0;
}

CGGameObject_C::Rotation CGGameObject_C::GetParentRotation() const {
  Rotation rot;
  rot.x = GetFloat(GAMEOBJECT_PARENTROTATION + 0);
  rot.y = GetFloat(GAMEOBJECT_PARENTROTATION + 1);
  rot.z = GetFloat(GAMEOBJECT_PARENTROTATION + 2);
  rot.w = GetFloat(GAMEOBJECT_PARENTROTATION + 3);
  return rot;
}

bool CGGameObject_C::IsDoor() const {
  return GetGoType() == GameObjectType::Door;
}

bool CGGameObject_C::IsButton() const {
  return GetGoType() == GameObjectType::Button;
}

bool CGGameObject_C::IsQuestGiver() const {
  return GetGoType() == GameObjectType::QuestGiver;
}

bool CGGameObject_C::IsChest() const {
  return GetGoType() == GameObjectType::Chest;
}

bool CGGameObject_C::IsTrap() const {
  return GetGoType() == GameObjectType::Trap;
}

bool CGGameObject_C::IsChair() const {
  return GetGoType() == GameObjectType::Chair;
}

bool CGGameObject_C::IsSpellFocus() const {
  return GetGoType() == GameObjectType::SpellFocus;
}

bool CGGameObject_C::IsGoober() const {
  return GetGoType() == GameObjectType::Goober;
}

bool CGGameObject_C::IsTransport() const {
  return GetGoType() == GameObjectType::Transport;
}

bool CGGameObject_C::IsMOTransport() const {
  return GetGoType() == GameObjectType::MOTransport;
}

bool CGGameObject_C::IsAnyTransport() const {
  return IsTransport() || IsMOTransport();
}

bool CGGameObject_C::CanBeTransportParent() const {
  return HasFlag(GO_FLAG_TRANSPORT);
}

void CGGameObject_C::AttachOverlayModelToBone() {
  CGObject_C::AttachOverlayModelToBone();
  if (!overlay_bone_attached_) {
    return;
  }

  overlay_bone_rotation_compensated_ = true;
}

bool CGGameObject_C::IsFishingNode() const {
  return GetGoType() == GameObjectType::FishingNode;
}

bool CGGameObject_C::IsMailbox() const {
  return GetGoType() == GameObjectType::Mailbox;
}

bool CGGameObject_C::IsMeetingStone() const {
  return GetGoType() == GameObjectType::MeetingStone;
}

bool CGGameObject_C::IsFlagStand() const {
  return GetGoType() == GameObjectType::FlagStand;
}

bool CGGameObject_C::IsCapturePoint() const {
  return GetGoType() == GameObjectType::CapturePoint;
}

bool CGGameObject_C::IsDestructibleBuilding() const {
  return GetGoType() == GameObjectType::DestructibleBuilding;
}

bool CGGameObject_C::IsBarberChair() const {
  return GetGoType() == GameObjectType::BarberChair;
}

bool CGGameObject_C::IsGuildBank() const {
  return GetGoType() == GameObjectType::GuildBank;
}

float CGGameObject_C::GetInteractDistance() const {

  return GetDefaultInteractDistance(GetGoType());
}

bool CGGameObject_C::IsHighlightableBaseHandler() const {
  const auto *const objects = object_manager();
  const auto *const active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  if (active_player == nullptr) {

    return false;
  }

  if (GetGoType() == GameObjectType::Trap) {

    if (LookupLockEntry() == nullptr) {
      return false;
    }
    if (GetReactionLevel(*active_player) > 1) {
      return false;
    }
    const ObjectGuid created_by = GetCreatedBy();
    if (!created_by.IsEmpty()) {

      const auto *const owner = objects->GetPlayer(created_by);
      if (owner == nullptr) {
        return false;
      }

      constexpr std::uint8_t kPvpFlagPvP = 0x01u;
      constexpr std::uint8_t kPvpFlagFfaPvP = 0x04u;
      const auto owner_pvp_flags = owner->State().GetPvPFlags();

      auto &groups = GroupSystem::Get();
      const auto owner_guid = created_by.GetRawValue();
      const bool ffa_reveals =
          (owner_pvp_flags & kPvpFlagFfaPvP) != 0u &&
          (!groups.IsInGroup() ||
           (!groups.IsActivePlayerOrPartyMemberGuid(owner_guid) &&
            !groups.IsRaidMemberGuid(owner_guid)));

      if ((owner_pvp_flags & kPvpFlagPvP) == 0u && !ffa_reveals) {

        return false;
      }
    }
  } else if (GetReactionLevel(*active_player) < 2) {

    return false;
  }

  if (IsPendingRemoval()) {

    return false;
  }
  return PassesInteractionFlagGate();
}

bool CGGameObject_C::IsSeatedOnThisChair() const {
  const auto *const objects = object_manager();
  const auto *const active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  if (active_player == nullptr) {
    return false;
  }

  constexpr std::uint8_t kFirstSittingStandState = 4u;
  constexpr std::uint8_t kLastSittingStandState = 6u;
  const auto stand_state = active_player->GetPlayerStandState();
  if (stand_state < kFirstSittingStandState ||
      stand_state > kLastSittingStandState) {
    return false;
  }

  constexpr float kOccupiedSeatDistanceSquared = 1.0f / 6.0f;
  const auto player_position = active_player->GetPosition();
  for (const auto &point : BuildUseInteractionPoints(*this)) {
    const float dx = point[0] - player_position.x;
    const float dy = point[1] - player_position.y;
    const float dz = point[2] - player_position.z;
    if (dx * dx + dy * dy + dz * dz < kOccupiedSeatDistanceSquared) {
      return true;
    }
  }
  return false;
}

bool CGGameObject_C::ShouldHighlight() const {
  switch (GetGoType()) {

  case GameObjectType::Generic:
  case GameObjectType::SpellFocus:
  case GameObjectType::Transport:
  case GameObjectType::MapObject:
  case GameObjectType::MOTransport:
  case GameObjectType::DuelArbiter:
  case GameObjectType::FishingHole:
  case GameObjectType::CapturePoint:
  case GameObjectType::AuraGenerator:
  case GameObjectType::DungeonDifficulty:
  case GameObjectType::DestructibleBuilding:
  case GameObjectType::Trapdoor:
  case GameObjectType::DoNotUse:
  case GameObjectType::GuardPost:
  case GameObjectType::DoNotUse2:
    return false;

  case GameObjectType::FishingNode: {
    const auto *const objects = object_manager();
    const auto *const active_player =
        objects != nullptr ? objects->GetActivePlayer() : nullptr;
    if (active_player == nullptr ||
        active_player->Casts().GetChannelObject(*active_player) != GetGuid()) {
      return false;
    }
    return IsHighlightableBaseHandler();
  }

  case GameObjectType::Chair:
  case GameObjectType::BarberChair:
    return IsHighlightableBaseHandler() && !IsSeatedOnThisChair();

  default:
    return IsHighlightableBaseHandler();
  }
}

bool CGGameObject_C::IsLocked() const {
  return HasFlag(GO_FLAG_LOCKED);
}

void CGGameObject_C::Interact(WorldSession *session) const {
  if (!session)
    return;
  session->interaction().SendGameObjectUse(GetGuid().IsEmpty() ? 0 : GetGuid().GetRawValue());
}

void CGGameObject_C::OnRightClickInteract(WorldSession *session, TargetingSystem *) const {
  if (!session) {
    return;
  }

  CloseActiveLootWindow(*session, CloseLootWindowOptions{
                                      .send_release = true,
                                      .skip_item_check = false,
                                      .show_interrupted = false,
                                      .clear_dead_target = true,
                                  });
  if (session->objects().GetActivePlayer() != nullptr) {
    PrepareAutoLootInteraction(
        *session, IsAutoLootEnabled(session->binding_profiles()));
  }

  auto &mutable_object = const_cast<CGGameObject_C &>(*this);
  std::uint32_t error = 0u;
  std::uint32_t spell = 0u;
  float interact_distance = 0.0f;
  if (mutable_object.CheckUseRange(*session, &error, &interact_distance, &spell)) {
    mutable_object.OnActivation(*session);

    session->interaction().SendGameObjectReportUse(
        GetGuid().IsEmpty() ? 0 : GetGuid().GetRawValue());
    return;
  }

  if (error == 240u && !IsFishingNode() &&
      TryStartRightClickAutoApproach(*session, *this, interact_distance)) {
    return;
  }

  if (error == 0u) {
    return;
  }

  if (spell != 0u) {
    openwow::ui::game::DisplaySystemMessage(static_cast<int>(error), spell);
  } else {
    openwow::ui::game::DisplaySystemMessage(static_cast<int>(error));
  }
}

const char *CGGameObject_C::GetTypeName(GameObjectType type) {
  switch (type) {
  case GameObjectType::Door:
    return "Door";
  case GameObjectType::Button:
    return "Button";
  case GameObjectType::QuestGiver:
    return "Quest Giver";
  case GameObjectType::Chest:
    return "Chest";
  case GameObjectType::Binder:
    return "Binder";
  case GameObjectType::Generic:
    return "Generic";
  case GameObjectType::Trap:
    return "Trap";
  case GameObjectType::Chair:
    return "Chair";
  case GameObjectType::SpellFocus:
    return "Spell Focus";
  case GameObjectType::Text:
    return "Text";
  case GameObjectType::Goober:
    return "Goober";
  case GameObjectType::Transport:
    return "Transport";
  case GameObjectType::AreaDamage:
    return "Area Damage";
  case GameObjectType::Camera:
    return "Camera";
  case GameObjectType::MapObject:
    return "Map Object";
  case GameObjectType::MOTransport:
    return "MO Transport";
  case GameObjectType::DuelArbiter:
    return "Duel Arbiter";
  case GameObjectType::FishingNode:
    return "Fishing Bobber";
  case GameObjectType::Ritual:
    return "Ritual";
  case GameObjectType::Mailbox:
    return "Mailbox";
  case GameObjectType::DoNotUse:
    return "DO_NOT_USE";
  case GameObjectType::GuardPost:
    return "Guard Post";
  case GameObjectType::SpellCaster:
    return "Spell Caster";
  case GameObjectType::MeetingStone:
    return "Meeting Stone";
  case GameObjectType::FlagStand:
    return "Flag Stand";
  case GameObjectType::FishingHole:
    return "Fishing Hole";
  case GameObjectType::FlagDrop:
    return "Flag Drop";
  case GameObjectType::MiniGame:
    return "Mini Game";
  case GameObjectType::DoNotUse2:
    return "DO_NOT_USE_2";
  case GameObjectType::CapturePoint:
    return "Capture Point";
  case GameObjectType::AuraGenerator:
    return "Aura Generator";
  case GameObjectType::DungeonDifficulty:
    return "Dungeon Difficulty";
  case GameObjectType::BarberChair:
    return "Barber Chair";
  case GameObjectType::DestructibleBuilding:
    return "Destructible Building";
  case GameObjectType::GuildBank:
    return "Guild Bank";
  case GameObjectType::Trapdoor:
    return "Trapdoor";
  }
  return "Unknown";
}

std::string CGGameObject_C::GetTooltipText() const {
  const std::string &obj_name = name_;
  if (!obj_name.empty())
    return obj_name;

  return GetTypeName(GetGoType());
}

std::uint16_t CGGameObject_C::GetTransportPathProgress() const {
  return static_cast<std::uint16_t>((GetDynamic() >> 16) & 0xFFFF);
}

float CGGameObject_C::GetTypeHandlerAnimTime() const {
  return type_handler_anim_time_;
}

void CGGameObject_C::SetTypeHandlerAnimTime(const float value) {
  const bool changed = value != type_handler_anim_time_;
  type_handler_anim_time_ = value;
  if (!changed) {
    return;
  }
  if (auto *const effect = GetObjectEffect(); effect != nullptr) {
    effect->RefreshModifierInputType(kObjectEffectModifierInputTypeAnimTime);
  }
}

bool CGGameObject_C::UpdateTrapdoorRenderSync(const bool asset_ready) {

  if (!loaded_model_state_) {
    return false;
  }

  if (trapdoor_render_sync_.initialized || !asset_ready) {

    return false;
  }

  trapdoor_render_sync_.initialized = true;
  return true;
}

void CGGameObject_C::OnTrapdoorStateByteChanged(
    const std::uint8_t ,
    const std::uint8_t current_state_byte) {
  if (!loaded_model_state_) {
    return;
  }

  const auto publish_model_request = [this](const std::uint32_t request_id) {
    if (m2_go_animation_control_.uses_direct_animation_id &&
        m2_go_animation_control_.direct_animation_id == request_id) {
      return;
    }
    m2_go_animation_control_.uses_direct_animation_id = true;
    m2_go_animation_control_.direct_animation_id = request_id;
    m2_go_animation_control_.looping = false;
    m2_go_animation_control_.use_sequence_repeat_count = false;
    m2_go_animation_control_.playback_speed = 1.0f;
    ++m2_go_animation_control_.sync_serial;
  };

  if (current_state_byte != 0) {

    loaded_model_state_->animation_request_id = kTrapdoorCloseAnimationId;
    publish_model_request(kTrapdoorCloseAnimationId);
    cached_interaction_value_ = 1;
    loaded_model_state_->destructible_proxy_current_state_enabled = true;
    return;
  }

  auto* const objects = object_manager();
  if (objects != nullptr) {
    objects->NotifyTransportOpened(GetGuid());
  }

  loaded_model_state_->animation_request_id = kTrapdoorOpenAnimationId;
  publish_model_request(kTrapdoorOpenAnimationId);
  cached_interaction_value_ = 0;
  loaded_model_state_->destructible_proxy_current_state_enabled = false;
}

std::uint16_t CGGameObject_C::ResolveModelAnimationId(const std::uint8_t request_code) {
  if (request_code >= kModelAnimationIdByRequestCode.size()) {
    return 0;
  }

  return kModelAnimationIdByRequestCode[request_code];
}

void CGGameObject_C::QueueModelAnimationRequest(const std::uint8_t request_code) {
  if (request_code == 0u || request_code >= kModelAnimationIdByRequestCode.size()) {
    return;
  }

  model_animation_control_.request_code = request_code;
  model_animation_control_.animation_id = ResolveModelAnimationId(request_code);
  ++model_animation_control_.sync_serial;
}

std::int8_t CGGameObject_C::ResolveGoAnimationStateIndex() const {
  const auto state_byte = cached_go_state_byte_;

  const bool has_override = HasDynamicAnimProgressOverride();

  switch (state_byte) {
  case 0:
    return has_override ? 2 : 3;
  case 1:
    return has_override ? 4 : 1;
  case 2:
    return has_override ? 5 : 6;
  default:
    return kGoAnimStateInvalid;
  }
}

std::int8_t CGGameObject_C::ResolveGoAnimStateForTransition(
    const std::uint8_t old_go_state,
    const std::uint8_t new_go_state) const {
  const bool prog_override = HasDynamicAnimProgressOverride();

  switch (new_go_state) {
  case 0: {

    return (old_go_state == 1 || prog_override) ? 2 : 3;
  }
  case 1: {

    const std::uint8_t effective_old =
        prog_override ? 0 : old_go_state;
    if (effective_old == 0) {
      return 4;
    }
    if (effective_old == 2) {
      return 7;
    }
    return 1;
  }
  case 2: {

    return (old_go_state == 1 || prog_override) ? 5 : 6;
  }
  default:
    return kGoAnimStateInvalid;
  }
}

bool CGGameObject_C::HasDynamicAnimProgressOverride() const {
  return GetTransportPathProgress() != kDynamicAnimProgressNoOverride;
}

void CGGameObject_C::ClearDynamicAnimProgressOverride() {
  if (GAMEOBJECT_DYNAMIC >= fields_.size()) {
    return;
  }
  fields_[GAMEOBJECT_DYNAMIC] =
      (fields_[GAMEOBJECT_DYNAMIC] & 0x0000FFFFu) |
      (static_cast<std::uint32_t>(kDynamicAnimProgressNoOverride) << 16u);
}

void CGGameObject_C::TransitionM2GoAnimationState(const std::int8_t new_state_index) {
  if (new_state_index == m2_go_animation_control_.state_index) {
    return;
  }

  m2_go_animation_control_.previous_state_index = m2_go_animation_control_.state_index;
  m2_go_animation_control_.state_index = new_state_index;
  m2_go_animation_control_.uses_direct_animation_id = false;
  m2_go_animation_control_.direct_animation_id = 0;
  m2_go_animation_control_.looping = IsGoAnimStateLooping(new_state_index);
  m2_go_animation_control_.playback_speed = 1.0f;

  if (new_state_index >= 0 &&
      static_cast<std::size_t>(new_state_index) < kGoAnimIdByStateIndex.size()) {
    m2_go_animation_control_.animation_id =
        kGoAnimIdByStateIndex[static_cast<std::size_t>(new_state_index)];
  } else {
    m2_go_animation_control_.animation_id = 0;
  }

  const bool is_auto_advance_state =
      new_state_index == 2 || new_state_index == 4 ||
      new_state_index == 5 || new_state_index == 7;
  m2_go_animation_control_.use_sequence_repeat_count =
      is_auto_advance_state &&
      (GetFlags() & GO_FLAG_ANIM_CUSTOM_REPEAT) != 0;

  const auto type = GetGoType();
  const bool uses_base_set_anim_state =
      type != GameObjectType::MapObject && type != GameObjectType::Transport &&
      type != GameObjectType::MOTransport && type != GameObjectType::Trapdoor;
  const bool consumes_progress_override = is_auto_advance_state &&
                                          uses_base_set_anim_state &&
                                          HasDynamicAnimProgressOverride();
  m2_go_animation_control_.has_progress_override =
      is_auto_advance_state && HasDynamicAnimProgressOverride();
  if (consumes_progress_override) {
    ClearDynamicAnimProgressOverride();
  }

  ++m2_go_animation_control_.sync_serial;

  if (GetGoType() == GameObjectType::Door) {
    cached_interaction_value_ =
        (new_state_index == kGoAnimStateDoorSolid) ? 1u : 0u;
  }
}

bool CGGameObject_C::TryPlayGoAnimState(const std::int8_t state_index) {
  if (state_index < 0 ||
      static_cast<std::size_t>(state_index) >=
          kGoAnimIdByStateIndex.size()) {
    return false;
  }

  TransitionM2GoAnimationState(state_index);

  if (state_index == 0) {
    SetOpacityTarget(GetModelOpacity(), 0);
  }

  return true;
}

std::int8_t CGGameObject_C::ResolveGoAnimAutoAdvanceTarget(
    const std::int8_t current_state_index) {
  switch (current_state_index) {
  case 2:  return 3;
  case 4:  return 1;
  case 5:  return 6;
  case 7:  return 1;
  default: return kGoAnimStateInvalid;
  }
}

bool CGGameObject_C::IsGoAnimStateLooping(const std::int8_t state_index) {
  return state_index == 1 || state_index == 3 || state_index == 6;
}

bool CGGameObject_C::PollAnimCompletionForLifetimeRelease(
    const std::uint32_t current_tick) {

  if (!IsPendingRemoval()) {
    return true;
  }

  if (!loaded_model_state_) {
    return true;
  }

  if (!m2_go_animation_control_.use_sequence_repeat_count) {

    if (animation_completion_tick_ == kInvalidAnimCompletionTick) {
      return true;
    }

    const auto delta = static_cast<std::int32_t>(
        current_tick - animation_completion_tick_);
    if (delta < 0) {
      return true;
    }
  }

  return HandleAnimCompletionHoldRelease();
}

bool CGGameObject_C::HandleAnimCompletionHoldRelease() {
  const auto applied = applied_model_anim_state_;
  if (applied == kGoAnimStateInvalid || IsGoAnimStateLooping(applied)) {
    applied_model_anim_state_ = kGoAnimStateInvalid;
    return true;
  }

  if (auto *const objects = object_manager(); objects != nullptr) {
    (void)objects->ReleaseObjectLifetimeHold(GetGuid());
  }
  return false;
}

void CGGameObject_C::SetAppliedModelAnimState(const std::int8_t state_index) {
  applied_model_anim_state_ = state_index;
}

void CGGameObject_C::SetAnimCompletionTick(const std::uint32_t tick) {
  animation_completion_tick_ = tick;
}

void CGGameObject_C::SynchronizeModelAnimationCompletion(
    const std::optional<std::uint32_t> duration_ms, const std::uint32_t current_tick) {
  const auto state = m2_go_animation_control_.state_index;
  if (state == kGoAnimStateInvalid || !duration_ms.has_value()) {
    return;
  }
  if (applied_model_anim_state_ == state) {
    return;
  }
  applied_model_anim_state_ = state;
  animation_completion_tick_ = current_tick + *duration_ms;
}

bool CGGameObject_C::AdvanceCompletedModelAnimState(const std::uint32_t current_tick) {
  const auto state = m2_go_animation_control_.state_index;
  if (state == kGoAnimStateInvalid ||
      applied_model_anim_state_ != state ||
      animation_completion_tick_ == kInvalidAnimCompletionTick) {
    return false;
  }

  if (static_cast<std::int32_t>(current_tick - animation_completion_tick_) < 0) {
    return false;
  }

  const auto next = ResolveGoAnimAutoAdvanceTarget(state);
  if (next == kGoAnimStateInvalid) {

    animation_completion_tick_ = kInvalidAnimCompletionTick;
    return false;
  }

  animation_completion_tick_ = kInvalidAnimCompletionTick;
  applied_model_anim_state_ = kGoAnimStateInvalid;
  TransitionM2GoAnimationState(next);
  return true;
}

void CGGameObject_C::OnNpcInteractionClosed() {

  const auto go_type = GetGoType();

  if (go_type == GameObjectType::Text) {

    TransitionM2GoAnimationState(4);
    return;
  }

  if (go_type != GameObjectType::QuestGiver) {
    return;
  }

  if (HasTemplateFieldValue(kCustomAnimFieldId)) {
    return;
  }

  const auto state = m2_go_animation_control_.state_index;
  if (state == 2 || state == 3) {
    TransitionM2GoAnimationState(4);
  }
}

void CGGameObject_C::RefreshFlagVisualControlState(const std::uint32_t previous_flags) {
  const auto current_flags = GetFlags();
  flag_visual_control_.current_flags = current_flags;
  flag_visual_control_.changed_mask = previous_flags ^ current_flags;
  if (flag_visual_control_.changed_mask != 0u) {
    ++flag_visual_control_.sync_serial;
  }
}

void CGGameObject_C::RefreshArtKitVisualControlState(const std::uint8_t previous_art_kit) {
  const auto current_art_kit = GetGoArtKit();
  art_kit_visual_control_.art_kit = current_art_kit;
  if (current_art_kit != previous_art_kit) {
    ++art_kit_visual_control_.sync_serial;
  }
}

void CGGameObject_C::RefreshDestructibleVisualControlState(
    const std::uint32_t flags_changed_mask) {
  if (!IsDestructibleBuilding()) {
    ReleasePlayerNameDescriptor();
    destructible_visual_control_ = {};
    return;
  }

  const auto previous_visual = destructible_visual_control_;
  DestructibleVisualControlState next{};
  next.initialized = true;
  next.active_state_index = ResolveDestructibleStateIndex(GetFlags());
  const bool state_changed = previous_visual.initialized &&
                             previous_visual.active_state_index != next.active_state_index;
  next.previous_active_state_index = previous_visual.previous_active_state_index;
  if (state_changed) {

    next.previous_active_state_index =
        static_cast<std::int8_t>(previous_visual.active_state_index);
  }
  if (!state_changed) {
    next.impact_effect_enabled = previous_visual.impact_effect_enabled;
  }

  const auto *const objects = object_manager();
  const auto *const dbc = objects != nullptr ? &objects->dbc_loader() : nullptr;

  next.states[0].source_display_id = GetDisplayId();
  if (HasRenderableGameObjectDisplay(dbc, next.states[0].source_display_id)) {
    next.states[0].render_display_id = next.states[0].source_display_id;
  }

  const auto destructible_model_data_id = GetTemplateFieldValue(kDestructibleModelDataFieldId);
  const auto *const model_data =
      dbc != nullptr ? dbc->destructible_model_data().LookupEntry(destructible_model_data_id)
                     : nullptr;
  if (model_data != nullptr) {
    next.states[0].impact_effect_doodad_set =
        static_cast<std::uint16_t>(model_data->state0_impact_effect_doodad_set);
    next.states[0].ambient_doodad_set =
        static_cast<std::uint16_t>(model_data->state0_ambient_doodad_set);

    next.states[1].source_display_id = model_data->state1_wmo_display_id;
    next.states[1].destruction_or_init_doodad_set =
        static_cast<std::uint16_t>(model_data->state1_destruction_doodad_set);
    next.states[1].impact_effect_doodad_set =
        static_cast<std::uint16_t>(model_data->state1_impact_effect_doodad_set);
    next.states[1].ambient_doodad_set =
        static_cast<std::uint16_t>(model_data->state1_ambient_doodad_set);

    next.states[2].source_display_id = model_data->state2_wmo_display_id;
    next.states[2].destruction_or_init_doodad_set =
        static_cast<std::uint16_t>(model_data->state2_destruction_doodad_set);
    next.states[2].impact_effect_doodad_set =
        static_cast<std::uint16_t>(model_data->state2_impact_effect_doodad_set);
    next.states[2].ambient_doodad_set =
        static_cast<std::uint16_t>(model_data->state2_ambient_doodad_set);

    next.states[3].source_display_id = model_data->state3_wmo_display_id;
    next.states[3].destruction_or_init_doodad_set =
        static_cast<std::uint16_t>(model_data->state3_init_doodad_set);
    next.states[3].ambient_doodad_set =
        static_cast<std::uint16_t>(model_data->state3_ambient_doodad_set);

    if (HasRenderableGameObjectDisplay(dbc, model_data->rebuild_effect_display_id)) {
      next.rebuild_effect_display_id = model_data->rebuild_effect_display_id;
    }
    next.rebuild_transition_mode = model_data->rebuild_transition_mode;
    next.rebuild_transition_speed = model_data->rebuild_transition_speed;
  }

  for (std::size_t index = 1; index < next.states.size(); ++index) {
    if (HasRenderableGameObjectDisplay(dbc, next.states[index].source_display_id)) {
      next.states[index].render_display_id = next.states[index].source_display_id;
    } else {

      next.states[index].render_display_id =
          next.states[index - 1].render_display_id;
    }
  }

  if (next.active_state_index < next.states.size()) {
    next.active_render_display_id = next.states[next.active_state_index].render_display_id;
  }

  if ((flags_changed_mask & 0x100u) != 0u && next.active_state_index < 3u) {
    next.impact_effect_enabled =
        next.states[next.active_state_index].impact_effect_doodad_set != 0u;
  }

  const auto unchanged =
      previous_visual.initialized == next.initialized &&
      previous_visual.active_state_index == next.active_state_index &&
      previous_visual.previous_active_state_index == next.previous_active_state_index &&
      previous_visual.active_render_display_id == next.active_render_display_id &&
      previous_visual.rebuild_effect_display_id == next.rebuild_effect_display_id &&
      previous_visual.rebuild_transition_mode == next.rebuild_transition_mode &&
      previous_visual.rebuild_transition_speed == next.rebuild_transition_speed &&
      previous_visual.impact_effect_enabled == next.impact_effect_enabled &&
      previous_visual.states == next.states;

  const auto previous_serial = previous_visual.sync_serial;
  destructible_visual_control_ = next;
  destructible_visual_control_.transition_serial =
      previous_visual.transition_serial + (state_changed ? 1u : 0u);
  destructible_visual_control_.sync_serial = unchanged ? previous_serial : previous_serial + 1u;
}

void CGGameObject_C::RefreshPlayerNameDescriptor(const bool allow_without_template) {
  if (!IsDestructibleBuilding()) {
    ReleasePlayerNameDescriptor();
    return;
  }

  if (!allow_without_template && template_info_ == nullptr) {
    ReleasePlayerNameDescriptor();
    return;
  }

  auto *const objects = object_manager();
  if (objects == nullptr || objects->Get(GetGuid()) == nullptr) {
    ReleasePlayerNameDescriptor();
    return;
  }

  ReleasePlayerNameDescriptor();
  player_name_desc_ = PlayerNameDesc_CreateForGuid(GetGuid().GetRawValue(), 0u);
}

void CGGameObject_C::ReleasePlayerNameDescriptor() {
  if (player_name_desc_ == nullptr) {
    return;
  }

  PlayerNameDesc_Destroy(player_name_desc_);
  player_name_desc_ = nullptr;
}

void CGGameObject_C::RefreshTransportPathProgressControlState(
    const std::uint16_t previous_path_progress) {
  const auto current_path_progress = GetTransportPathProgress();
  transport_path_progress_control_.path_progress = current_path_progress;
  if (current_path_progress != previous_path_progress) {
    ++transport_path_progress_control_.sync_serial;
    const auto state = static_cast<std::uint8_t>(GetGoState());
    if (IsMOTransport()) {

      SynchronizeMOTransportPathState(true,
                                      false);
    } else {
      SyncTransportAnimationClock(*this, state, state);
    }
  }
}

bool CGGameObject_C::HasMOTransportAnimationData() const {

  return HasTemplateFieldValue(kMOTransportCanBeStoppedFieldId);
}

void CGGameObject_C::RefreshMOTransportAnimationControl(const std::uint8_t previous_state_byte,
                                                        const std::uint8_t current_state_byte) {
  if (!IsMOTransport()) {
    mo_transport_animation_control_ = {};
    return;
  }

  mo_transport_animation_control_.enabled = HasMOTransportAnimationData();
  mo_transport_animation_control_.ready_state =
      current_state_byte == static_cast<std::uint8_t>(GOState::Ready);

  if (!mo_transport_animation_control_.enabled) {
    return;
  }

  ++mo_transport_animation_control_.sync_serial;
  (void)previous_state_byte;
}

void CGGameObject_C::SynchronizeMOTransportPathState(
    const bool reseed_from_progress, const bool force_state_reapply) {
  if (!mo_transport_path_state_.has_value() || !HasMOTransportAnimationData()) {
    return;
  }

  const std::uint32_t absolute_time_ms =
      openwow::core::GameClock::GetTickCount32() + GetObjectTimeOffsetMs();
  if (reseed_from_progress) {
    mo_transport_path_state_->SeedFromPackedProgress(
        absolute_time_ms,
        static_cast<float>(GetTransportPathProgress()) / 65535.0f);

    if ((GetDynFlags() & 0x10u) != 0u) {
      (void)mo_transport_path_state_->LatchReadySegment(absolute_time_ms);
    } else {
      mo_transport_path_state_->ApplyReadyState(
          absolute_time_ms, GetGoState() == GOState::Ready);
    }
    return;
  }

  const bool requested_ready = GetGoState() == GOState::Ready;
  if (force_state_reapply) {

    mo_transport_path_state_->ApplyReadyState(absolute_time_ms, !requested_ready);
  }
  mo_transport_path_state_->ApplyReadyState(absolute_time_ms, requested_ready);

  mo_transport_previous_phase_.reset();
}

void CGGameObject_C::AdvanceMOTransportPathState() {
  if (!mo_transport_path_state_.has_value()) {
    return;
  }

  const std::uint32_t current_tick_ms = openwow::core::GameClock::GetTickCount32();
  const std::uint32_t adjusted_absolute_time_ms =
      current_tick_ms + GetObjectTimeOffsetMs();
  const std::uint32_t elapsed_ms =
      mo_transport_has_update_tick_ ? current_tick_ms - mo_transport_last_update_tick_ms_ : 0u;
  mo_transport_last_update_tick_ms_ = current_tick_ms;
  mo_transport_has_update_tick_ = true;

  const auto sample = mo_transport_path_state_->EvaluatePathAtTime(
      adjusted_absolute_time_ms, elapsed_ms,
      false, true);

  auto* const objects = object_manager();
  if (objects == nullptr) {
    return;
  }

  if (sample.mapId != objects->GetMapId()) {
    const auto* const active_player = objects->GetActivePlayer();
    if (active_player == nullptr ||
        active_player->GetTransportGUID() != GetGuid()) {
      return;
    }
    openwow::core::CMovementRuntime_SetMovementTimestamp(
        mo_transport_handler_timestamp_ms_);
    return;
  }

  mo_transport_handler_timestamp_ms_ =
      mo_transport_path_state_->GetCurrentPathTime(adjusted_absolute_time_ms);

  if (mo_transport_previous_position_.has_value()) {
    const float delta_x = sample.position[0] - (*mo_transport_previous_position_)[0];
    const float delta_y = sample.position[1] - (*mo_transport_previous_position_)[1];
    const float delta_z = sample.position[2] - (*mo_transport_previous_position_)[2];
    const float distance = std::sqrt(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
    const float elapsed_milliseconds =
        static_cast<float>(elapsed_ms >> 16u) * 65536.0f +
        static_cast<float>(elapsed_ms & 0xFFFFu);
    const float speed = elapsed_milliseconds == 0.0f
                            ? ((std::isnan(distance) || distance == 0.0f)
                                   ? std::numeric_limits<float>::quiet_NaN()
                                   : std::numeric_limits<float>::infinity())
                            : distance / (elapsed_milliseconds * 0.001f);
    SetTypeHandlerAnimTime(speed);
  }
  mo_transport_previous_position_ = sample.position;

  auto matrix = openwow::render::kRenderIdentityMatrix4x4;
  matrix[12] = sample.position[0];
  matrix[13] = sample.position[1];
  matrix[14] = sample.position[2];

  matrix = openwow::render::PrependRotationMatrix4x4Z(matrix, sample.facing);
  matrix = openwow::render::PrependRotationMatrix4x4X(matrix, sample.roll);
  matrix = openwow::render::PrependRotationMatrix4x4Y(matrix, sample.pitch);
  SetVisualModelWorldTransform(matrix.data());

  if (objects != nullptr) {
    const auto rotation =
        openwow::game::Passenger_QuaternionFromTransform(matrix.data());
    TransportPathInfo live_pose_path;
    live_pose_path.pathId = GetDisplayId();
    live_pose_path.live_pose = true;
    live_pose_path.nodes.push_back({
        .x = sample.position[0],
        .y = sample.position[1],
        .z = sample.position[2],
        .mapId = objects->GetMapId(),
    });
    live_pose_path.rotation_keyframes.push_back({
        .timeIndex = 0u,
        .x = rotation.x,
        .y = rotation.y,
        .z = rotation.z,
        .w = rotation.w,
    });

    if (auto *const existing = objects->transport_manager().GetTransportMutable(GetGuid());
        existing != nullptr) {
      existing->RefreshLivePosePath(live_pose_path);
    } else {
      objects->transport_manager().OnTransportCreate(GetGuid(), GetEntry(), GetDisplayId(),
                                                      live_pose_path);
      BackfillMOTransportPassengers(*objects);
    }

    if (mo_transport_previous_path_position_.has_value() && elapsed_ms != 0u) {
      if (auto *const transport = objects->transport_manager().GetTransportMutable(GetGuid());
          transport != nullptr) {

        constexpr std::uint32_t kPassengerCollisionWindowMs = 250u;
        auto start_position = *mo_transport_previous_path_position_;
        std::uint32_t collision_elapsed_ms = elapsed_ms;
        if (elapsed_ms > kPassengerCollisionWindowMs) {
          const std::uint32_t rewound_absolute_ms =
              current_tick_ms + GetObjectTimeOffsetMs() - kPassengerCollisionWindowMs;
          const std::uint32_t rewound_path_time_ms =
              mo_transport_path_state_->GetCurrentPathTime(rewound_absolute_ms);

          const auto rewound = mo_transport_path_state_->EvaluatePathAtTime(
              rewound_path_time_ms, 0u, true,
              true);
          start_position = rewound.pathPosition;
          collision_elapsed_ms = kPassengerCollisionWindowMs;
        }
        transport->SetLivePoseMotion(
            {start_position[0], start_position[1], start_position[2]},
            {sample.pathPosition[0], sample.pathPosition[1], sample.pathPosition[2]},
            collision_elapsed_ms);
      }
    }
  }

  mo_transport_previous_path_position_ = sample.pathPosition;

  if (mo_transport_model_ready_latched_ &&
      (!mo_transport_previous_phase_.has_value() ||
       *mo_transport_previous_phase_ != sample.movePhase)) {
    RequestMOTransportMovePhaseAnimation(sample.movePhase);
    MOTransport_SetEffectForMovePhase(GetObjectEffect(), sample.movePhase);
    mo_transport_previous_phase_ = sample.movePhase;
  }

  PropagateMOTransportAttachments(matrix.data());
}

std::uint32_t CGGameObject_C::MapMOTransportMovementTimestamp(
    const std::uint32_t absolute_timestamp_ms) const {
  if (!mo_transport_path_state_.has_value()) {
    return absolute_timestamp_ms;
  }
  return mo_transport_path_state_->GetCurrentPathTime(absolute_timestamp_ms);
}

void CGGameObject_C::BackfillMOTransportPassengers(ObjectManager& objects) {
  const std::uint64_t carrier_guid = GetGuid().GetRawValue();

  const MovementInfo no_previous_attachment{};

  objects.ForEachUnit([&](const ObjectGuid&, CGUnit_C& unit) {
    if (unit.Movement().Data().GetTransportGuid() != carrier_guid) {
      return;
    }
    objects.SynchronizeUnitTransportPassengerMembership(unit,
                                                        no_previous_attachment);
  });
}

void CGGameObject_C::PropagateMOTransportAttachments(float* const parent_matrix) {
  if (parent_matrix == nullptr) {
    return;
  }

  auto* const objects = object_manager();
  if (objects == nullptr) {
    return;
  }

  const bool parent_is_wmo = loaded_model_state_ && loaded_model_state_->is_wmo;

  const auto* const active_player = objects->GetActivePlayer();
  for (auto* const node : active_attachment_list_) {
    if (node == nullptr || node->target_guid == 0u) {
      continue;
    }

    if (mo_transport_model_ready_latched_ && node->HasAutoPlayParticle() &&
        node->HasParticleStopFlag()) {
      node->ClearParticleStopFlag();
    }

    auto* const target = static_cast<CGObject_C*>(
        objects->GetMutable(ObjectGuid(node->target_guid)));
    if (target == nullptr) {
      continue;
    }

    if (auto* const target_unit = dynamic_cast<CGUnit_C*>(target);
        target_unit != nullptr) {
      auto* const vehicle_data = target_unit->Vehicle().GetVehicleData();
      if (vehicle_data != nullptr && target_unit->Vehicle().GetVehicleEntry() != nullptr) {
        vehicle::Vehicle_C_UpdateTransformHierarchy(vehicle_data, parent_matrix);
      }
    }

    target->Show(target != active_player && parent_is_wmo);
    target->ApplyModelParentTransform(parent_matrix);
  }

  if (active_player != nullptr &&
      active_player->GetTransportGUID() == GetGuid()) {
    openwow::core::CMovementRuntime_SetMovementTimestamp(
        mo_transport_handler_timestamp_ms_);
  }
}

void CGGameObject_C::RequestMOTransportMovePhaseAnimation(
    const MOTransportMovePhase phase) {
  const auto animation_id = static_cast<std::uint32_t>(phase);

  mo_transport_published_move_phase_ = phase;

  if (m2_go_animation_control_.uses_direct_animation_id &&
      m2_go_animation_control_.direct_animation_id == animation_id) {
    return;
  }

  m2_go_animation_control_.uses_direct_animation_id = true;
  m2_go_animation_control_.direct_animation_id = animation_id;
  m2_go_animation_control_.looping = false;
  m2_go_animation_control_.use_sequence_repeat_count = false;
  m2_go_animation_control_.playback_speed = 1.0f;
  ++m2_go_animation_control_.sync_serial;
}

void CGGameObject_C::ApplyTransportSequenceEffect(const std::uint32_t sequence_id) {
  if (!m2_go_animation_control_.uses_direct_animation_id ||
      m2_go_animation_control_.direct_animation_id != sequence_id) {
    m2_go_animation_control_.uses_direct_animation_id = true;
    m2_go_animation_control_.direct_animation_id = sequence_id;
    m2_go_animation_control_.looping = false;
    m2_go_animation_control_.use_sequence_repeat_count = false;
    m2_go_animation_control_.playback_speed = 1.0f;
    ++m2_go_animation_control_.sync_serial;
  }

  Transport_UpdateSequenceEffectState(GetObjectEffect(), sequence_id,
                                      cached_transport_sequence_effect_state_);
}

void CGGameObject_C::SetTemplateInfo(const GameObjectTemplateInfo *info) {

  if (template_info_ == info && info != nullptr) {
    return;
  }

  template_info_ = info;

  if (template_info_ != nullptr) {
    RefreshTemplateBoundTypeHandlerState();
  }

  if (info != nullptr && info->display_id != 0u) {
    const auto *const display_info =
        LookupGameObjectDisplayInfoForModelLoad(*this, info->display_id);
    (void)BindObjectEffectPackage(
        display_info != nullptr ? display_info->object_effect_package_id : 0u);
  } else {
    ClearObjectEffectPackage();
  }

  RefreshDestructibleVisualControlState();
  RefreshPlayerNameDescriptor();
  RefreshLootArtVisualControlState();
  if (IsMOTransport()) {
    const auto state = static_cast<std::uint8_t>(GetGoState());
    RefreshMOTransportAnimationControl(state, state);
    SyncTransportAnimationClock(*this, state, state);
  }
}

void CGGameObject_C::RefreshTemplateBoundTypeHandlerState() {
  switch (GetGoType()) {
  case GameObjectType::Chair:
  case GameObjectType::BarberChair:

    return;

  case GameObjectType::Text:

    return;

  case GameObjectType::Transport:
  case GameObjectType::MapObject:
  case GameObjectType::Trapdoor:

    InitModelByType();
    return;

  case GameObjectType::MOTransport:

    InitVisuals();
    return;

  case GameObjectType::CapturePoint:

    return;

  case GameObjectType::DestructibleBuilding:

    RefreshDestructibleVisualControlState();
    return;

  default:

    return;
  }
}

void CGGameObject_C::DispatchGoStateByteCallback(const std::uint8_t previous_state_byte,
                                                 const std::uint8_t current_state_byte) {
  cached_go_state_byte_ = current_state_byte;
  RefreshMOTransportAnimationControl(previous_state_byte, current_state_byte);
  if (IsMOTransport()) {
    SynchronizeMOTransportPathState(
        false,
        previous_state_byte == current_state_byte);
  } else {
    SyncTransportAnimationClock(*this, previous_state_byte, current_state_byte);
  }

  if (GetGoType() == GameObjectType::Trapdoor) {
    OnTrapdoorStateByteChanged(previous_state_byte, current_state_byte);
  }

  if (m2_go_animation_control_.state_index != kGoAnimStateInvalid) {
    const auto new_state_index =
        ResolveGoAnimStateForTransition(previous_state_byte, current_state_byte);
    if (new_state_index != kGoAnimStateInvalid) {
      TransitionM2GoAnimationState(new_state_index);
    }
  }
}

const char *CGGameObject_C::GetStatsName() const {
  if (template_info_) {
    return template_info_->name.c_str();
  }
  return "";

}

const char *CGGameObject_C::GetStatsCastBarCaption() const {
  if (template_info_ && !template_info_->cast_bar_caption.empty()) {
    return template_info_->cast_bar_caption.c_str();
  }
  return nullptr;
}

const char *CGGameObject_C::GetStatsUnk1Text() const {
  if (template_info_ && !template_info_->unk1.empty()) {
    return template_info_->unk1.c_str();
  }
  return nullptr;
}

std::uint32_t CGGameObject_C::GetTypeSpecificData(std::uint32_t index) const {
  if (!template_info_ || index >= 24) {
    return 0;
  }
  return template_info_->raw_data[index];
}

std::uint32_t CGGameObject_C::GetReadablePageTextId() const {
  return GetTemplateFieldValue(kReadablePageTextFieldId);
}

std::uint32_t CGGameObject_C::GetReadableLanguageId() const {
  return GetTemplateFieldValue(kReadableLanguageFieldId);
}

std::uint32_t CGGameObject_C::GetReadablePageMaterialId() const {
  return GetTemplateFieldValue(kReadablePageMaterialFieldId);
}

std::uint32_t CGGameObject_C::GetRequiredInstanceMapId() const {
  return GetTemplateFieldValue(kDungeonDifficultyMapIdFieldId);
}

std::uint32_t CGGameObject_C::GetRequiredInstanceDifficultyIndex() const {
  return GetTemplateFieldValue(kDungeonDifficultyRequiredIndexFieldId);
}

std::uint32_t CGGameObject_C::GetMeetingStoneAreaId() const {
  return GetTemplateFieldValue(kMeetingStoneAreaFieldId);
}

std::uint32_t CGGameObject_C::GetTemplateFieldValue(const std::uint32_t field_id) const {
  return ResolveTemplateFieldValue(template_info_, GetGoType(), field_id);
}

bool CGGameObject_C::IsTemplateFieldZero(const std::uint32_t field_id) const {
  return GetTemplateFieldValue(field_id) == 0;
}

bool CGGameObject_C::HasTemplateFieldValue(const std::uint32_t field_id) const {
  return GetTemplateFieldValue(field_id) != 0;
}

bool CGGameObject_C::AllowsMountedInteraction() const {
  switch (GetGoType()) {
  case GameObjectType::Mailbox:
    return true;
  case GameObjectType::BarberChair:
    return false;
  default:
    return HasTemplateFieldValue(kMountedInteractionFieldId);
  }
}

bool CGGameObject_C::AllowsUseWhileInCombat() const {
  return IsTemplateFieldZero(kUseWhileInCombatFieldId);
}

bool CGGameObject_C::HasQuestConditionalOwner() const {
  return HasTemplateFieldValue(kQuestConditionalOwnerFieldId);
}

bool CGGameObject_C::HasFloatingTooltip() const {
  return HasTemplateFieldValue(kFloatingTooltipFieldId);
}

bool CGGameObject_C::IsLevelInRange(const std::uint32_t level) const {
  const auto min_level = GetTemplateFieldValue(kMeetingStoneMinLevelFieldId);
  if (level < min_level) {
    return false;
  }
  const auto max_level = GetTemplateFieldValue(kMeetingStoneMaxLevelFieldId);
  return level <= max_level;
}

bool CGGameObject_C::PassesMeetingStoneUseGates(const WorldSession &session,
                                                const CGPlayer_C &active_player) const {

  constexpr int kErrMeetingStoneNeedParty = 0x1d6;

  constexpr int kErrGenericNoTarget = 199;

  constexpr int kErrMeetingStoneInvalidLevel = 0x1d1;

  constexpr int kErrMeetingStoneTargetNotInParty = 0x1d2;

  constexpr int kErrMeetingStoneNotFound = 0x1d7;

  constexpr int kErrMeetingStoneTargetInvalidLevel = 0x1d3;

  auto &groups = GroupSystem::Get();
  if (!groups.IsInGroup()) {
    openwow::ui::game::DisplaySystemMessage(kErrMeetingStoneNeedParty);
    return false;
  }

  const auto *const targeting = session.targeting_system();
  const std::uint64_t target_guid = targeting != nullptr ? targeting->target_guid() : 0u;
  if (target_guid == 0u) {
    openwow::ui::game::DisplaySystemMessage(kErrGenericNoTarget);
    return false;
  }

  if (!IsLevelInRange(active_player.GetLevel())) {
    openwow::ui::game::DisplaySystemMessage(kErrMeetingStoneInvalidLevel);
    return false;
  }

  if (!groups.IsActivePlayerOrPartyMemberGuid(target_guid) &&
      !groups.IsRaidMemberGuid(target_guid)) {
    openwow::ui::game::DisplaySystemMessage(kErrMeetingStoneTargetNotInParty);
    return false;
  }

  std::uint32_t target_level = 0u;
  if (const auto *const target_unit = session.objects().GetUnit(ObjectGuid(target_guid));
      target_unit != nullptr) {
    target_level = target_unit->GetLevel();
  } else {
    const auto cached = session.party_stats().GetCachedMember(target_guid);
    if (!cached.has_value() || cached->stats.level == 0u ||
        (cached->stats.status & GroupMemberStatus::kOnline) == 0u) {
      openwow::ui::game::DisplaySystemMessage(kErrMeetingStoneNotFound);
      return false;
    }
    target_level = cached->stats.level;
  }

  if (!IsLevelInRange(target_level)) {
    openwow::ui::game::DisplaySystemMessage(kErrMeetingStoneTargetInvalidLevel);
    return false;
  }

  return true;
}

bool CGGameObject_C::IsVisibleForCurrentInstanceDifficulty(
    const WorldSession &session) const {
  return EvaluateDungeonDifficultyVisibility(*this, session);
}

void CGGameObject_C::RefreshDifficultyVisibilityControlState(
    const WorldSession &session) {
  const bool visible = EvaluateDungeonDifficultyVisibility(*this, session);

  if (!visible && tracked_positional_sound_handle_ != 0u) {
    auto &sound_interface = sound_runtime();
    if (!sound_interface.GetSoundHandle(tracked_positional_sound_handle_).has_value()) {
      tracked_positional_sound_handle_ = 0u;
    } else if (sound_interface.IsSoundHandlePlaying(tracked_positional_sound_handle_)) {
      (void)sound_interface.RequestStopSoundHandle(tracked_positional_sound_handle_, -1.0f);
      tracked_positional_sound_handle_ = 0u;
    }
  }

  if (difficulty_visibility_control_.visible != visible) {
    difficulty_visibility_control_.visible = visible;
    ++difficulty_visibility_control_.sync_serial;
  }
}

std::uint32_t CGGameObject_C::GetLockId() const {
  return GetTemplateFieldValue(kLockIdFieldId);
}

const openwow::data::dbc::LockEntry *CGGameObject_C::LookupLockEntry() const {
  const auto *const objects = object_manager();
  if (objects == nullptr) {
    return nullptr;
  }

  const auto lock_id = GetLockId();
  if (lock_id == 0) {
    return nullptr;
  }

  return objects->dbc_loader().lock().LookupEntry(lock_id);
}

const openwow::data::dbc::LockEntry *CGGameObject_C::GetLockEntry() const {
  return LookupLockEntry();
}

bool CGGameObject_C::GetLockInteractionInfo(SpellCastRuntime &spells,
                                            LockInteractionInfo *info) const {
  const auto *const objects = object_manager();
  if (objects == nullptr) {
    return false;
  }

  const auto *const active_player = objects->GetActivePlayer();
  if (active_player == nullptr) {
    return false;
  }

  const auto *const lock_entry = LookupLockEntry();
  if (lock_entry == nullptr) {
    return false;
  }

  const auto *const dbc = &objects->dbc_loader();

  bool has_known_requirement = false;

  for (std::size_t slot = 0; slot < lock_entry->type.size(); ++slot) {
    const auto type = lock_entry->type[slot];
    if (type == 0u) {
      continue;
    }

    switch (type) {
    case 2u: {

      has_known_requirement = true;
      if (!IsLockActionApplicable(*this, lock_entry->action[slot])) {
        break;
      }

      const auto lock_index = lock_entry->index[slot];
      const auto required_value =
          lock_entry->skill[slot] != 0u ? lock_entry->skill[slot] : 5u * GetLevel();

      for (const auto &known_spell : SpellbookSystem::Get().GetKnownSpellList()) {
        const auto *const spell = LookupSpellEntry(*dbc, known_spell.spell_id);
        if (spell == nullptr) {
          continue;
        }

        for (std::size_t ei = 0; ei < spell->effect.size(); ++ei) {
          if (spell->effect[ei] != kSpellEffectOpenLock ||
              static_cast<std::uint32_t>(spell->effect_misc_value[ei]) != lock_index) {
            continue;
          }

          const auto current_value = GetSpellEffectMagnitude(*spell, ei);
          if (info != nullptr) {
            info->spell_id = known_spell.spell_id;
            info->current_skill = current_value;
            info->required_skill = required_value;
          }

          if (current_value >= required_value) {
            if (info != nullptr) {
              info->lock_slot = static_cast<std::uint32_t>(slot);
            }
            return false;
          }
        }
      }

      const auto targeting_state = spells.GetTargeting().GetState();
      if (targeting_state.isActive && targeting_state.spellId != 0u) {
        const auto *const current_spell =
            LookupSpellEntry(*dbc, targeting_state.spellId);
        if (current_spell != nullptr) {
          for (std::size_t ei = 0; ei < current_spell->effect.size(); ++ei) {
            if (current_spell->effect[ei] != kSpellEffectOpenLock ||
                static_cast<std::uint32_t>(current_spell->effect_misc_value[ei]) != lock_index) {
              continue;
            }

            const auto current_value = GetSpellEffectMagnitude(*current_spell, ei);
            if (info != nullptr) {
              info->spell_id = targeting_state.spellId;
              info->current_skill = current_value;
              info->required_skill = required_value;
            }

            if (current_value >= required_value) {
              if (info != nullptr) {
                info->lock_slot = static_cast<std::uint32_t>(slot);
              }
              return false;
            }
          }
        }
      }
      break;
    }

    case 3u: {

      has_known_requirement = true;
      if (!IsLockActionApplicable(*this, lock_entry->action[slot])) {
        break;
      }

      const auto *const spell = LookupSpellEntry(*dbc, lock_entry->index[slot]);
      if (spell != nullptr && SpellHasAnyOpenLockEffect(*spell)) {
        if (info != nullptr) {
          info->spell_id = spell->id;
          info->lock_slot = static_cast<std::uint32_t>(slot);
        }
        return false;
      }
      break;
    }

    case 1u: {

      has_known_requirement = true;
      if (!IsLockActionApplicable(*this, lock_entry->action[slot])) {
        break;
      }

      const auto bag_slot =
          inventory_.FindItemByEntry(lock_entry->index[slot]);
      if (bag_slot >= 0) {
        if (info != nullptr) {
          info->item_bag_slot = bag_slot;
          info->lock_slot = static_cast<std::uint32_t>(slot);
        }
        return false;
      }
      break;
    }

    default:
      break;
    }
  }

  return has_known_requirement;
}

bool CGGameObject_C::MeetsTrackedLootArtEligibilityGate(const CGPlayer_C &active_player) const {
  switch (GetGoType()) {
  case GameObjectType::FishingHole:
    return true;

  case GameObjectType::AreaDamage:
    return !GetCreatedBy().IsEmpty() && GetCreatedBy() == active_player.GetGuid() && PassesInteractionFlagGate();

  default:
    return PassesInteractionFlagGate();
  }
}

bool CGGameObject_C::LockEntryMatchesTrackResources(const openwow::data::dbc::LockEntry &lock_entry,
                                                    const std::uint32_t track_resources) {
  for (std::size_t index = 0; index < lock_entry.type.size(); ++index) {
    if (lock_entry.type[index] != 2) {
      continue;
    }

    const auto track_index = lock_entry.index[index];
    if (track_index == 0 || track_index > 32) {
      continue;
    }

    if ((track_resources & (1u << (track_index - 1u))) != 0u) {
      return true;
    }
  }

  return false;
}

bool CGGameObject_C::EvaluateLootArtVisualRequest(bool *const quest_sparkle,
                                                  bool *const tracked_resource_match) const {
  const bool has_quest_sparkle = HasQuestSparkle();
  bool matches_track_resources = false;

  if (!has_quest_sparkle) {
    const auto *const objects = object_manager();
    const auto *const active_player =
        objects != nullptr ? objects->GetActivePlayer() : nullptr;
    const auto *const lock_entry = LookupLockEntry();
    if (active_player != nullptr && !active_player->State().IsDeadOrGhost() && lock_entry != nullptr &&
        MeetsTrackedLootArtEligibilityGate(*active_player)) {
      matches_track_resources =
          LockEntryMatchesTrackResources(*lock_entry, active_player->GetTrackResources());
    }
  }

  if (quest_sparkle != nullptr) {
    *quest_sparkle = has_quest_sparkle;
  }
  if (tracked_resource_match != nullptr) {
    *tracked_resource_match = matches_track_resources;
  }

  return has_quest_sparkle || matches_track_resources;
}

void CGGameObject_C::RefreshLootArtVisualControlState() {
  bool quest_sparkle = false;
  bool tracked_resource_match = false;
  const bool requested = EvaluateLootArtVisualRequest(&quest_sparkle, &tracked_resource_match);
  const std::uint32_t effect_id =
      requested ? HardcodedEffectIdTable::GetEffectId(HardcodedEffectId::kLootArt) : 0u;

  if (loot_art_visual_control_.requested == requested &&
      loot_art_visual_control_.quest_sparkle == quest_sparkle &&
      loot_art_visual_control_.tracked_resource_match == tracked_resource_match &&
      loot_art_visual_control_.effect_id == effect_id) {
    return;
  }

  loot_art_visual_control_.requested = requested;
  loot_art_visual_control_.quest_sparkle = quest_sparkle;
  loot_art_visual_control_.tracked_resource_match = tracked_resource_match;
  loot_art_visual_control_.effect_id = effect_id;
  ++loot_art_visual_control_.sync_serial;
}

bool CGGameObject_C::IsQuestGiverType() const {
  return GetGoType() == GameObjectType::QuestGiver;
}

bool CGGameObject_C::IsSpellFocusTargetEligible(
    const SpellCastRuntime &spell_cast_runtime) const {
  constexpr std::uint32_t game_object_target_flags =
      static_cast<std::uint32_t>(SpellTargetFlag::kGameobject) |
      static_cast<std::uint32_t>(SpellTargetFlag::kGoItem);
  const auto &targeting = spell_cast_runtime.GetTargeting();
  if ((targeting.GetTargetMask() & game_object_target_flags) == 0u ||
      object_manager() == nullptr) {
    return false;
  }

  std::uint32_t spell_id = spell_cast_runtime.GetCurrentSpellId();
  if (spell_id == 0u) {
    spell_id = targeting.GetSpellId();
  }
  if (spell_id == 0u) {
    return false;
  }

  const auto *const spell = object_manager()->dbc_loader().spell().LookupEntry(spell_id);
  return spell != nullptr && spell->requires_spell_focus != 0u &&
         MeetsSpellFocusConditions(spell_id, spell->requires_spell_focus);
}

bool CGGameObject_C::MeetsSpellFocusConditions(std::uint32_t ,
                                               std::uint32_t focus_id) const {

  if (GetGoType() != GameObjectType::SpellFocus) {
    return false;
  }

  if (!template_info_) {
    return false;
  }

  const std::uint32_t spell_focus_type = template_info_->raw_data[0];
  if (spell_focus_type == 0) {
    return false;
  }

  return spell_focus_type == focus_id;
}

bool CGGameObject_C::PassesPlayerRequirementTypeGate(
    const WorldSession &session) const {
  switch (GetGoType()) {
  case GameObjectType::SpellFocus:
  case GameObjectType::DuelArbiter:
  case GameObjectType::FishingHole:
  case GameObjectType::AuraGenerator:
    return true;

  case GameObjectType::Transport:
  case GameObjectType::MapObject:
  case GameObjectType::MOTransport:
  case GameObjectType::Trapdoor:
    return false;

  case GameObjectType::Generic:
  case GameObjectType::CapturePoint:
    return HasTemplateFieldValue(kPlayerRequirementGateFieldId);

  case GameObjectType::DestructibleBuilding:

    return loaded_model_state_.has_value();

  default:

    (void)session;
    return ShouldHighlight();
  }
}

bool CGGameObject_C::PlayerMeetsRequirements(const WorldSession &session) const {
  if (!PassesPlayerRequirementTypeGate(session)) {
    return false;
  }

  const auto required_skill_raw = GetTemplateFieldValue(kRequiredSkillFieldId);
  if (static_cast<std::int32_t>(required_skill_raw) <= 0) {
    return true;
  }

  const auto *const active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr) {
    return false;
  }

  constexpr std::uint16_t kRetailRequirementSkillSlotCount = 25u;
  for (std::uint16_t slot = 0; slot < kRetailRequirementSkillSlotCount;
       ++slot) {
    if (static_cast<std::uint32_t>(active_player->GetSkill(slot).skill_id) ==
        required_skill_raw) {
      return true;
    }
  }
  return false;
}

int CGGameObject_C::GetCursorType(const WorldSession &session) const {
  const bool can_use = TryUse(session, nullptr, nullptr, nullptr);

  if (IsQuestGiverType()) {
    if (const auto status = session.quests().FindQuestGiverStatus(GetGuid()); status.has_value()) {
      if (const auto retail_type = ResolveQuestGiverRetailCursorType(*status, !can_use);
          retail_type != 0u) {
        return static_cast<int>(retail_type);
      }
    }
  }

  if (IsMailbox()) {
    return can_use ? 15 : 41;
  }

  if (GetGoType() == GameObjectType::Text) {
    return can_use ? 7 : 33;
  }

  if (IsGuildBank()) {
    return can_use ? 3 : 29;
  }

  const auto fallback_cursor = [can_use]() { return can_use ? 5 : 31; };
  const auto *const lock_entry = LookupLockEntry();
  if (lock_entry == nullptr || object_manager() == nullptr) {
    return fallback_cursor();
  }

  const auto lock_type_id = lock_entry->index[0];
  const auto *const lock_type =
      object_manager()->dbc_loader().lock_type().LookupEntry(lock_type_id);
  if (lock_type == nullptr || lock_type->cursor.empty()) {
    return fallback_cursor();
  }

  std::string cursor_stem(lock_type->cursor);
  if (lock_type->id != 1u && !can_use) {
    cursor_stem.insert(0u, "Unable");
  }

  const auto retail_type = FindRetailCursorTypeByStem(cursor_stem);
  return retail_type != 0u ? static_cast<int>(retail_type) : fallback_cursor();
}

std::string_view CGGameObject_C::GetLockTypeCursorStem() const {
  const auto *const objects = object_manager();
  if (objects == nullptr) {
    return {};
  }

  const auto *const lock_entry = LookupLockEntry();
  if (lock_entry == nullptr) {
    return {};
  }

  const auto *const lock_type =
      objects->dbc_loader().lock_type().LookupEntry(lock_entry->index[0]);
  if (lock_type == nullptr || lock_type->cursor.empty()) {
    return {};
  }

  return lock_type->cursor;
}

bool CGGameObject_C::GetCustomCursorPath(const WorldSession &session,
                                          char *buffer,
                                          std::uint32_t buffer_size) const {
  const auto stem = GetLockTypeCursorStem();
  if (stem.empty() || buffer == nullptr || buffer_size == 0) {
    return false;
  }

  const bool can_use = TryUse(session, nullptr, nullptr, nullptr);
  if (can_use) {
    std::snprintf(buffer, buffer_size, "Interface\\Cursor\\%.*s.blp",
                  static_cast<int>(stem.size()), stem.data());
  } else {
    std::snprintf(buffer, buffer_size, "Interface\\Cursor\\Unable%.*s.blp",
                  static_cast<int>(stem.size()), stem.data());
  }
  return true;
}

std::uint32_t CGGameObject_C::GetInteractionValue(std::uint16_t flags) const {
  if ((flags & 0x8000) == 0 || static_cast<std::uint8_t>(GetGoType()) != 0) {
    return cached_interaction_value_;
  }
  return 0;
}

void CGGameObject_C::OnGoStateByteChanged(const std::uint8_t previous_state_byte) {
  const auto current_state_byte = static_cast<std::uint8_t>(GetGoState());

  if (current_state_byte != previous_state_byte || IsMOTransport()) {
    DispatchGoStateByteCallback(previous_state_byte, current_state_byte);
  }
}

bool CGGameObject_C::LoadModel(GameObjectModelLoadMetadata metadata) {
  const std::uint32_t display_id = GetDisplayId();

  if (display_id == 0u) {
    return false;
  }
  const auto *const display_info = LookupGameObjectDisplayInfoForModelLoad(*this, display_id);
  if (display_info == nullptr) {
    return false;
  }

  const auto world_position = GetPosition();
  GameObjectLoadedModelState state;
  state.display_path = std::string(display_info->filename);
  state.world_position = {world_position.x, world_position.y, world_position.z};
  state.facing = GetFacing();
  state.metadata = metadata;
  state.is_wmo = IsWorldModelPath(display_info->filename);
  if (state.is_wmo) {
    state.world_model = NormalizeWorldModelPlacementMetadata(metadata);
  }

  mo_transport_model_ready_latched_ = false;
  loaded_model_state_ = state;
  return true;
}

void CGGameObject_C::ClearLoadedModelState() {
  mo_transport_model_ready_latched_ = false;
  render_readiness_state_ = {};
  loaded_model_state_.reset();
}

bool CGGameObject_C::PassesInteractionFlagGate() const {

  const ObjectGuid player_guid = GetActivePlayerGuid();
  if (player_guid.IsEmpty()) {
    return false;
  }

  if (GetGoType() == GameObjectType::FishingNode) {
    if (GetCreatedBy() != player_guid) {
      return false;
    }
  }

  if (GetGoType() == GameObjectType::Trap && !template_info_) {
    return false;
  }

  if ((GetDynFlags() & GO_DYNFLAG_LO_NO_INTERACT) != 0) {
    return false;
  }

  const std::uint32_t flags = GetFlags();
  const std::uint16_t dyn_flags = GetDynFlags();

  if ((flags & (GO_FLAG_IN_USE | GO_FLAG_NOT_SELECTABLE)) != 0) {
    return false;
  }
  if ((flags & GO_FLAG_INTERACT_COND) != 0 &&
      (dyn_flags & GO_DYNFLAG_LO_ACTIVATE) == 0) {
    return false;
  }

  if (HasQuestConditionalOwner()) {
    const ObjectGuid created_by = GetCreatedBy();
    if (created_by.IsEmpty() || created_by != player_guid) {
      return false;
    }
  }

  return true;
}

bool CGGameObject_C::TryUse(const WorldSession &session,
                            std::uint32_t *error_out,
                             float *range_out,
                             std::uint32_t *spell_out) const {

  const auto *const active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr || active_player->State().IsDeadOrGhost()) {
    if (error_out)
      *error_out = 135;
    return false;
  }

  if (!session.IsInWorld()) {
    if (error_out)
      *error_out = 136;
    return false;
  }

  const auto player_unit_flags = active_player->State().GetUnitFlags();
  if ((player_unit_flags & kPlayerUseBlockedUnitFlag) != 0u) {
    if (error_out == nullptr) {
      return false;
    }

    std::uint32_t blocking_mechanic = 0u;
    const auto *const dbc = session.GetDbcLoader();
    if (dbc != nullptr &&
        TryResolveBlockingAuraMechanic(*active_player, *dbc, &blocking_mechanic) &&
        spell_out != nullptr) {
      *spell_out = blocking_mechanic;
      *error_out = 512u;
      return false;
    }

    *error_out = 434u;
    return false;
  }

  const auto *const lock_entry = LookupLockEntry();
  if (lock_entry == nullptr) {
    if (active_player->Mount().IsMountedStateActive(*active_player) &&
        !AllowsMountedInteraction() &&
        !active_player->Movement().CanChangeDirection()) {
      if (error_out != nullptr) {
        *error_out = 448u;
      }
      return false;
    }

    const auto shapeshift_form = active_player->Animation().GetShapeshiftForm();
    if (shapeshift_form != 0u) {
      const auto *const dbc = session.GetDbcLoader();
      const auto *const form_entry =
          dbc != nullptr ? dbc->spell_shapeshift_form().LookupEntry(shapeshift_form) : nullptr;
      const bool requires_turn_sensitive_use =
          active_player->Interaction()
              .CurrentShapeshiftFormRequiresTurnSensitiveUse();
      const bool allows_form_based_use = form_entry != nullptr && (form_entry->flags & 0x8u) != 0u;
      if (requires_turn_sensitive_use && !allows_form_based_use &&
          !active_player->Movement().CanTurn()) {
        if (error_out != nullptr) {
          *error_out = 449u;
        }
        return false;
      }
    }
  }

  if ((player_unit_flags & kUnitFlagInCombat) != 0u && !AllowsUseWhileInCombat()) {
    if (error_out)
      *error_out = 450;
    return false;
  }

  if ((player_unit_flags & kPlayerUseRestrictedUnitFlag) != 0u &&
      (IsChest() || HasTemplateFieldValue(57u))) {
    if (error_out != nullptr) {
      *error_out = 385u;
    }
    return false;
  }

  if ((GetFlags() & GO_FLAG_LOCKED) != 0u) {
    const auto *const dbc = session.GetDbcLoader();
    if (dbc != nullptr &&
        HasUnsatisfiedLockedRequirement(*this, inventory_, *dbc,
                                        session.spells().GetTargeting())) {
      if (error_out != nullptr) {

        *error_out = GetTypeHandlerInfo(static_cast<std::uint8_t>(GetGoType()))
                         .event_id;
      }
      return false;
    }
  }

  if (range_out) {
    *range_out = GetInteractDistance();
  }

  if (!PassesInteractionPointRangeTest(session)) {
    if (error_out)
      *error_out = 240;
    return false;
  }

  if (GetGoType() == GameObjectType::Chair ||
      GetGoType() == GameObjectType::BarberChair) {
    if (!PassesSeatPointRangeTest(*active_player)) {
      if (error_out != nullptr) {
        *error_out = 240u;
      }
      return false;
    }
  }

  if (GetGoType() == GameObjectType::BarberChair &&
      (active_player->Presentation().DisplayId() !=
           active_player->Presentation().NativeDisplayId() ||
       active_player->Interaction()
           .CurrentShapeshiftFormRequiresTurnSensitiveUse())) {
    if (error_out != nullptr) {
      *error_out = 449u;
    }
    return false;
  }

  (void)spell_out;
  return true;
}

bool CGGameObject_C::PassesSeatPointRangeTest(const CGPlayer_C &active_player) const {

  const float interact_dist = GetInteractDistance();
  const float interact_dist_sq = interact_dist * interact_dist;
  const auto player_position = active_player.GetPosition();
  for (const auto &point : BuildUseInteractionPoints(*this)) {
    const float dx = player_position.x - point[0];
    const float dy = player_position.y - point[1];
    const float dz = player_position.z - point[2];
    if (dx * dx + dy * dy + dz * dz < interact_dist_sq) {
      return true;
    }
  }
  return false;
}

bool CGGameObject_C::OnActivation(WorldSession &session) {

  const auto *const active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr) {
    return false;
  }

  if (IsGuildBank()) {
    session.interaction().SendGuildBankerActivate(GetGuid().GetRawValue());
    return true;
  }

  if (GetGoType() == GameObjectType::Text) {
    if (ToggleOrBeginReadableObjectInteraction(session, GetGuid().GetRawValue())) {
      LoadCurrentReadableTextPage(session, true);
    }
    return true;
  }

  if (IsMailbox()) {
    session.mail().set_mailbox_guid(GetGuid().GetRawValue());
    ui::game::SetNpcInteractionTarget(GetGuid());
    ui::game::ShowMailbox(session.mail());
    return true;
  }

  if (IsMeetingStone() && !PassesMeetingStoneUseGates(session, *active_player)) {
    return false;
  }

  if (active_player->Mount().IsMounted(*active_player) &&
      !active_player->State().HasForcedVehicleTransition() &&
      !AllowsMountedInteraction()) {
    session.interaction().SendCancelMountAura();
  }

  if (IsChest()) {
    session.loot().ExpectServerLootResponse(GetGuid());
  }

  Interact(&session);
  return true;
}

void CGGameObject_C::InitModelByType() {

  ClearLoadedModelState();

  const auto type = GetGoType();

  switch (type) {
  case GameObjectType::Transport:
  case GameObjectType::MOTransport:
  case GameObjectType::Trapdoor: {
    const auto guid = GetGuid().GetRawValue();
    GameObjectModelLoadMetadata metadata;
    metadata.owner_guid_low = static_cast<std::uint32_t>(guid & 0xFFFFFFFFu);
    metadata.owner_guid_high = static_cast<std::uint32_t>(guid >> 32u);
    if (LoadModel(metadata) && type == GameObjectType::Transport) {
      loaded_model_state_->transport_flag_enabled = true;
    }
    break;
  }
  case GameObjectType::DestructibleBuilding:
    return;

  default:
    (void)LoadModel();
    break;
  }
}

void GameObjectAttachmentNode::ResetParentToWorld(
    const ObjectManager& objects) {
  if (parent_guid == 0u) {
    return;
  }

  float parent_transform[16];
  (void)Movement_GetObjectTransform(objects, parent_guid, parent_transform);
  const float parent_facing =
      Movement_GetObjectOrientation(objects, parent_guid);
  float parent_rotation_xyzw[4];
  Movement_GetObjectWorldRotation(objects, parent_guid,
                                  parent_rotation_xyzw);
  const PassengerQuaternion parent_rotation{
      parent_rotation_xyzw[0], parent_rotation_xyzw[1],
      parent_rotation_xyzw[2], parent_rotation_xyzw[3]};

  pose.uses_packed_orientation = (flags & 0x2u) != 0u;
  Passenger_ApplyParentTransform(pose, parent_transform, parent_facing,
                                 parent_rotation);
  parent_guid = 0u;
}

void CGGameObject_C::RemoveAttachment(GameObjectAttachmentNode* node,
                                       bool stop_particles) {
  if (!node)
    return;

  active_attachment_list_.remove(node);

  free_attachment_list_.push_front(node);

  if (stop_particles && node->HasAutoPlayParticle()) {
    const bool model_ready =
        loaded_model_state_ &&
        render::IsRuntimeRenderAssetReady(render_readiness_state_);
    if (!model_ready) {
      node->SetParticleStopFlag();
    }
  }

  const auto* const objects = object_manager();
  const auto* const active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  if (active_player != nullptr &&
      node->target_guid == active_player->GetGuid().GetRawValue()) {

    openwow::core::CMovementRuntime_PushPreviousTransportContext(transport_context_id_);
  }
}

void CGGameObject_C::ClearAttachmentsForWorldRemoval() {

  while (!active_attachment_list_.empty()) {
    auto* const node = active_attachment_list_.front();
    active_attachment_list_.pop_front();
    if (node == nullptr) {
      continue;
    }
    if (node->HasAutoPlayParticle()) {
      node->ClearParticleStopFlag();
    }
    free_attachment_list_.push_front(node);
  }
}

void CGGameObject_C::AddAttachmentNode(GameObjectAttachmentNode* node) {
  if (!node)
    return;

  active_attachment_list_.remove(node);
  free_attachment_list_.remove(node);
  active_attachment_list_.push_front(node);

  const auto* const objects = object_manager();
  const auto* const active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  if (active_player != nullptr &&
      node->target_guid == active_player->GetGuid().GetRawValue()) {
    openwow::core::CMovementRuntime_SetMovementTimestamp(
        mo_transport_handler_timestamp_ms_);
  }

}

void CGGameObject_C::UpsertTransportPassengerAttachment(
    const std::uint64_t target_guid) {
  if (target_guid == 0u) {
    return;
  }

  for (auto &owned_node : transport_attachment_nodes_) {
    if (owned_node != nullptr && owned_node->target_guid == target_guid) {
      AddAttachmentNode(owned_node.get());
      return;
    }
  }

  auto node = std::make_unique<GameObjectAttachmentNode>();
  node->target_guid = target_guid;

  node->flags |= GameObjectAttachmentNode::kAutoPlayParticleBit;
  node->is_transport_passenger = true;
  auto *const node_pointer = node.get();
  transport_attachment_nodes_.push_back(std::move(node));
  AddAttachmentNode(node_pointer);
}

void CGGameObject_C::RemoveTransportPassengerAttachment(
    const std::uint64_t target_guid) {
  if (target_guid == 0u) {
    return;
  }

  for (auto it = transport_attachment_nodes_.begin();
       it != transport_attachment_nodes_.end(); ++it) {
    if (*it == nullptr || (*it)->target_guid != target_guid) {
      continue;
    }
    auto *const node = it->get();
    active_attachment_list_.remove(node);
    free_attachment_list_.remove(node);
    if (transport_attachment_destruction_in_progress_) {
      free_attachment_list_.push_front(node);
      return;
    }
    transport_attachment_nodes_.erase(it);
    return;
  }
}

void CGGameObject_C::InitVisuals() {

  InitModelByType();

  if (!IsMOTransport()) {
    return;
  }

  if (!template_info_ || object_manager() == nullptr) {
    mo_transport_path_state_.reset();
    mo_transport_handler_timestamp_ms_ = 0u;
    mo_transport_previous_phase_.reset();
    mo_transport_previous_position_.reset();
    mo_transport_previous_path_position_.reset();
    mo_transport_has_update_tick_ = false;
    return;
  }

  mo_transport_path_state_ = BuildMOTransportPathState(*this, object_manager()->dbc_loader());
  mo_transport_handler_timestamp_ms_ = 0u;
  mo_transport_previous_phase_.reset();
  const auto initial_position = GetPosition();
  mo_transport_previous_position_ =
      std::array<float, 3>{initial_position.x, initial_position.y, initial_position.z};
  mo_transport_previous_path_position_.reset();
  mo_transport_has_update_tick_ = false;

  const auto state = static_cast<std::uint8_t>(GetGoState());
  RefreshMOTransportAnimationControl(state, state);
  SynchronizeMOTransportPathState(true);
}

bool CGGameObject_C::CheckUseRange(const WorldSession &session,
                                   std::uint32_t *error_out,
                                   float *range_out,
                                   std::uint32_t *spell_out) {

  return TryUse(session, error_out, range_out, spell_out);
}

bool CGGameObject_C::PassesInteractionPointRangeTest(
    const WorldSession &session) const {
  const auto *const objects = object_manager();
  const auto *const active_player =
      objects != nullptr ? objects->GetActivePlayer() : nullptr;
  if (active_player == nullptr) {
    return false;
  }

  float interact_dist = GetInteractDistance();
  if (const auto lock_spell_id = ResolveLockOpeningSpellId(*this, session);
      lock_spell_id != 0u) {
    if (float spell_max_range = 0.0f;
        TryResolveLockSpellMaxRange(session, *active_player, lock_spell_id,
                                    spell_max_range)) {
      interact_dist = spell_max_range;
    }
  }

  const auto player_position = active_player->GetPosition();

  if (GetGoType() == GameObjectType::SpellFocus) {
    const auto object_position = GetPosition();
    const double dx = static_cast<double>(object_position.x) - player_position.x;
    const double dy = static_cast<double>(object_position.y) - player_position.y;
    const double dz = static_cast<double>(object_position.z) - player_position.z;
    return dx * dx + dy * dy + dz * dz <=
           static_cast<double>(interact_dist) * static_cast<double>(interact_dist);
  }

  const auto *const display =
      objects->dbc_loader().gameobject_display_info().LookupEntry(GetDisplayId());
  if (display == nullptr) {

    const auto object_position = GetPosition();
    const double dx = static_cast<double>(object_position.x) - player_position.x;
    const double dy = static_cast<double>(object_position.y) - player_position.y;
    const double dz = static_cast<double>(object_position.z) - player_position.z;
    return dx * dx + dy * dy + dz * dz <=
           static_cast<double>(interact_dist) * static_cast<double>(interact_dist);
  }

  float world_transform[16];
  BuildUseInteractionTransform(*this, world_transform);

  const float scale = GetScale();
  if (!(scale > 0.0f)) {
    return false;
  }
  const float inverse_scale = 1.0f / scale;

  const float offset[3] = {
      player_position.x - world_transform[12],
      player_position.y - world_transform[13],
      player_position.z - world_transform[14],
  };

  float local_point[3];
  for (int axis = 0; axis < 3; ++axis) {
    const float row_x = world_transform[axis * 4 + 0] * inverse_scale;
    const float row_y = world_transform[axis * 4 + 1] * inverse_scale;
    const float row_z = world_transform[axis * 4 + 2] * inverse_scale;
    local_point[axis] = offset[0] * row_x + offset[1] * row_y + offset[2] * row_z;
  }

  const float box_min[3] = {display->min_x * scale - interact_dist,
                            display->min_y * scale - interact_dist,
                            display->min_z * scale - interact_dist};
  const float box_max[3] = {display->max_x * scale + interact_dist,
                            display->max_y * scale + interact_dist,
                            display->max_z * scale + interact_dist};

  for (int axis = 0; axis < 3; ++axis) {
    if (local_point[axis] <= box_min[axis] || box_max[axis] <= local_point[axis]) {
      return false;
    }
  }
  return true;
}

CGGameObject_C *CGGameObject_OnCreate_LinkToObjectMgr(ObjectManager &objects,
                                                       const std::uint64_t guid) {
  return objects.GetMutableGameObject(ObjectGuid(guid));
}

}

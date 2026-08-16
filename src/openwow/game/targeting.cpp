#include "openwow/game/targeting.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/commentator_state.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/interaction_range.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/objects/unit/unit_movement_runtime.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/spell_cast_lifecycle.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/spell_validation.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_session.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/foundation/math/quadratic_roots.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/render/scene/world_frame.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace openwow::game {

static uint64_t NowMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

constexpr std::uint32_t kDefaultAutoAttackSpellId = 6603u;

constexpr std::uint32_t kSpellAttributesExBlocksAttackWhileChanneling = 0x4000u;

std::uint32_t ResolveAutoAttackSpellId(const SpellCastRuntime& spells,
                                       const std::uint32_t spell_id) {
  if (spell_id != 0) {
    return spell_id;
  }

  const auto current_spell_id = spells.GetAutoAttackSpellId();
  if (current_spell_id != 0) {
    return current_spell_id;
  }

  return kDefaultAutoAttackSpellId;
}

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kTabFrontConeHalfAngle = 0.52359879f;
constexpr float kTabTargetDistanceSqInFront = 1681.0f;
constexpr float kTabTargetDistanceSqOffAngle = 100.0f;
constexpr std::uint32_t kCreatureTypeTabTargetExcludeFlag = 0x1u;
constexpr std::uint32_t kUnitFlagNotSelectable = static_cast<std::uint32_t>(UnitStateFlag::kNotSelectable);
constexpr std::uint32_t kPlayerFlags2Spectator = 0x00080000u;
constexpr std::uint32_t kPlayerFlags2CommentatorAdmin = 0x00400000u;

constexpr std::uint32_t kUnitFlags2AttackTargetLockout = 0x01000000u;
constexpr int kFollowInteractionActionType = 3;
constexpr int kAttackFollowInteractionActionType = 10;
constexpr int kAutofollowTooFarMessageId = 313;

constexpr int kAttackSwingBadFacingMessageId = 230;
constexpr int kAttackSwingNotInRangeMessageId = 231;
constexpr float kAttackStopDistancePadding = 1.3333334f;

constexpr float kMinAttackStopCombinedReach = 5.0f;

float NormalizePositiveRadians(float angle) {
  while (angle < 0.0f) {
    angle += kTwoPi;
  }
  while (angle >= kTwoPi) {
    angle -= kTwoPi;
  }
  return angle;
}

float ComputePositiveFacingDelta(float facing, float angle_to_target) {
  return NormalizePositiveRadians(angle_to_target - facing);
}

bool IsPartyMemberTarget(const WorldSession& session, const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return false;
  }

  const auto local_guid = session.objects().GetLocalPlayerGuid();
  if (guid == local_guid) {
    return true;
  }

  const auto& members = session.group().members();
  if (!session.group().IsRaid()) {
    return std::any_of(members.begin(), members.end(),
                       [&](const GroupMember& member) { return member.guid == guid; });
  }

  for (const auto& member : members) {
    if (member.guid == guid) {
      return member.sub_group == session.group().my_sub_group();
    }
  }
  return false;
}

bool IsRaidMemberTarget(const WorldSession& session, const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return false;
  }

  if (guid == session.objects().GetLocalPlayerGuid()) {
    return true;
  }

  const auto& members = session.group().members();
  return std::any_of(members.begin(), members.end(),
                     [&](const GroupMember& member) { return member.guid == guid; });
}

bool IsStoredTargetGuidRetargetable(const WorldSession& session,
                                    const std::uint64_t guid) {
  if (guid == 0) {
    return false;
  }

  const auto* object = session.objects().Get(ObjectGuid(guid));
  if (object != nullptr && object->IsUnit()) {
    return true;
  }

  return IsPartyMemberTarget(session, ObjectGuid(guid)) ||
         IsRaidMemberTarget(session, ObjectGuid(guid));
}

bool IsAssistTargetGuidRetargetable(const WorldSession& session,
                                    const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return false;
  }

  const auto* object = session.objects().Get(guid);
  if (object != nullptr && object->IsUnit()) {
    return true;
  }

  return IsPartyMemberTarget(session, guid) || IsRaidMemberTarget(session, guid);
}

std::uint32_t ResolveSpellTargetCreatureTypeId(const WorldSession* session,
                                               const CGUnit_C& target) {
  if (session != nullptr) {
    return SpellTargetValidator::GetSpellTargetCreatureTypeId(*session, target);
  }

  if (target.IsPlayer()) {
    return static_cast<std::uint32_t>(CreatureTypeId::kHumanoid);
  }

  return static_cast<std::uint32_t>(target.State().GetCreatureType());
}

bool IsTabTargetCreatureTypeExcluded(const WorldSession* session,
                                     const CGUnit_C& target) {
  if (session == nullptr) {
    return false;
  }

  const auto* dbc = session->GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  const auto creature_type_id = ResolveSpellTargetCreatureTypeId(session, target);
  if (creature_type_id == 0) {
    return false;
  }

  const auto* entry = dbc->creature_type().LookupEntry(creature_type_id);
  return entry != nullptr &&
         (entry->flags & kCreatureTypeTabTargetExcludeFlag) != 0;
}

bool IsCommentatorMapTypeSelected(const CommentatorState& commentator,
                                  const std::uint32_t map_type) {
  const auto* selected_instance = commentator.GetSelectedInstance();
  if (selected_instance == nullptr) {
    return false;
  }

  for (std::size_t i = 0; i < commentator.GetMapCount(); ++i) {
    const auto* map = commentator.GetMapInfo(i);
    if (map == nullptr) {
      continue;
    }

    const auto match = std::find_if(
        map->instances.begin(), map->instances.end(),
        [&](const CommentatorInstanceInfo& info) { return info.key == selected_instance->key; });
    if (match != map->instances.end()) {
      return map->field2 == map_type;
    }
  }

  return false;
}

bool IsCommentatorOnlyTabTarget(const CGUnit_C& target,
                                const CommentatorState& commentator) {
  if (!target.IsPlayer()) {
    return false;
  }

  const auto flags2 = target.State().GetUnitFlags2();
  if ((flags2 & kPlayerFlags2Spectator) == 0) {
    return false;
  }

  return (flags2 & kPlayerFlags2CommentatorAdmin) != 0 ||
          IsCommentatorMapTypeSelected(commentator, 4);
}

Vec3 NormalizeFlatVec3(const Vec3& vector) {
  const float length = std::sqrt(vector.x * vector.x + vector.y * vector.y);
  if (length <= 1.0e-6f) {
    return {};
  }

  const float inverse_length = 1.0f / length;
  return {vector.x * inverse_length, vector.y * inverse_length, 0.0f};
}

Vec3 RotateFlatClockwise(const Vec3& vector, float radians) {
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  return {vector.x * cosine + vector.y * sine,
          -vector.x * sine + vector.y * cosine, 0.0f};
}

float Dot2(const Vec3& lhs, const Vec3& rhs) {
  return lhs.x * rhs.x + lhs.y * rhs.y;
}

float LengthSq2(const Vec3& vector) {
  return vector.x * vector.x + vector.y * vector.y;
}

float LengthSq3(const Vec3& vector) {
  return vector.x * vector.x + vector.y * vector.y + vector.z * vector.z;
}

Vec3 GetWorldPosition(const WorldObject& object) {
  const auto position = object.GetPosition();
  return {position.x, position.y, position.z};
}

bool RejectInteractionIfWarningDistanceExceeded(const CGUnit_C& source,
                                                const WorldObject& target,
                                                const int action_type) {
  const Vec3 source_position = GetWorldPosition(source);
  const Vec3 target_position = GetWorldPosition(target);
  const float dx = target_position.x - source_position.x;
  const float dy = target_position.y - source_position.y;
  const float dz = target_position.z - source_position.z;
  const float distance_sq = dx * dx + dy * dy + dz * dz;

  if (!interaction_range::ExceedsInteractionWarningDistance(action_type, distance_sq)) {
    return false;
  }

  ui::game::DisplaySystemMessage(kAutofollowTooFarMessageId);
  return true;
}

bool ExceedsAttackStopRange(const CGUnit_C& attacker, const CGUnit_C& victim) {
  float combined_reach =
      attacker.State().GetCombatReach() + victim.State().GetCombatReach() + kAttackStopDistancePadding;
  if (combined_reach < kMinAttackStopCombinedReach) {
    combined_reach = kMinAttackStopCombinedReach;
  }
  const float max_distance = combined_reach * 4.0f;
  const Vec3 attacker_position = GetWorldPosition(attacker);
  const Vec3 victim_position = GetWorldPosition(victim);
  const Vec3 delta{
      attacker_position.x - victim_position.x,
      attacker_position.y - victim_position.y,
      attacker_position.z - victim_position.z,
  };
  return LengthSq3(delta) >= max_distance * max_distance;
}

bool TryProjectWorldPosition(const render::WorldFrame& world_frame,
                             const Vec3& world_position,
                             Vec3& screen_position) {
  const render::RenderVec3 world_point{world_position.x, world_position.y, world_position.z};
  const auto projected = world_frame.WorldToScreen(
      render::RenderVec3View{world_point});
  if (!projected.on_screen) {
    return false;
  }

  screen_position = {projected.position[0], projected.position[1], projected.position[2]};
  return true;
}

bool TryResolveDirectionTargetAnchor(const WorldObject& source,
                                     const WorldObject* preferred_anchor,
                                     const render::WorldFrame& world_frame,
                                     const WorldObject*& anchor,
                                     Vec3& anchor_world,
                                     Vec3& anchor_screen) {
  anchor = preferred_anchor != nullptr ? preferred_anchor : &source;
  anchor_world = GetWorldPosition(*anchor);
  if (TryProjectWorldPosition(world_frame, anchor_world, anchor_screen)) {
    return true;
  }

  anchor = &source;
  anchor_world = GetWorldPosition(source);
  return TryProjectWorldPosition(world_frame, anchor_world, anchor_screen);
}

bool PassesTargetAcquisitionFilter(const CGUnit_C& source,
                                   const WorldObject& object,
                                   const TargetFilter filter,
                                   const WorldSession* session) {
  if (!object.IsUnit()) {
    return false;
  }

  const auto unit_flags = object.GetUInt32(UNIT_FIELD_FLAGS);
  if ((unit_flags & kUnitFlagNotSelectable) != 0) {
    return false;
  }

  const auto& target = static_cast<const CGUnit_C&>(object);
  switch (filter) {
    case TargetFilter::kAny:
    case TargetFilter::kEnemy:
    case TargetFilter::kEnemyPlayer:
    case TargetFilter::kFriend:
    case TargetFilter::kFriendPlayer:
      return MatchesTargetFilterAction(source, target, filter);
    case TargetFilter::kParty:
      return session != nullptr && IsPartyMemberTarget(*session, target.GetGuid());
    case TargetFilter::kRaid:
      return session != nullptr && IsRaidMemberTarget(*session, target.GetGuid());
  }

  return false;
}

void TargetingSystem::Initialize(WorldSession* session) {
  session_ = session;
  direction_target_guid_ = 0;
}

void TargetingSystem::ResetForWorldLeave() {
  target_guid_ = 0;
  last_target_guid_ = 0;
  last_enemy_guid_ = 0;
  last_friend_guid_ = 0;
  direction_target_guid_ = 0;

  tab_candidates_.clear();
  tab_index_ = -1;
  tab_list_built_ms_ = 0;
  tab_last_selection_ms_ = 0;

  attack_swing_active_ = false;
  attack_swing_target_guid_ = 0;
  attack_stop_pending_ = false;
  attack_follow_active_ = false;
  attack_keep_follow_ = false;
  follow_active_ = false;
  follow_guid_ = 0;
  owns_auto_follow_forward_ = false;
}

bool TargetingSystem::IsHostile(const WorldObject& unit) const {
  const ObjectManager* mgr = Objects();
  if (!mgr) return false;
  const auto* player = mgr->GetLocalPlayer();
  if (!player || !player->IsUnit() || !unit.IsUnit()) return false;

  const auto& player_unit = static_cast<const CGUnit_C&>(*player);
  const auto& target_unit = static_cast<const CGUnit_C&>(unit);
  return player_unit.Interaction().IsHostileTo(target_unit);
}

bool TargetingSystem::IsFriendly(const WorldObject& unit) const {
  const ObjectManager* mgr = Objects();
  if (!mgr) return false;
  const auto* player = mgr->GetLocalPlayer();
  if (!player || !player->IsUnit() || !unit.IsUnit()) return false;

  const auto& player_unit = static_cast<const CGUnit_C&>(*player);
  const auto& target_unit = static_cast<const CGUnit_C&>(unit);
  return player_unit.Interaction().IsFriendlyTo(target_unit);
}

bool TargetingSystem::IsSelectable(const WorldObject& unit) const {
  uint32_t flags = unit.GetUInt32(UNIT_FIELD_FLAGS);
  return (flags & kUnitFlagNotSelectable) == 0;
}

bool MatchesTargetFilterAction(const CGUnit_C& player,
                               const CGUnit_C& target,
                               const TargetFilter filter) {
  switch (filter) {
    case TargetFilter::kAny:
      return true;
    case TargetFilter::kEnemy:
      return player.Interaction().CanAttackSpellTarget(target) && !target.State().IsDead() &&
             (target.State().GetDynamicFlags() & kUnitDynFlagDead) == 0 &&
             target.State().GetCreatureType() != CreatureTypeId::kCritter;
    case TargetFilter::kEnemyPlayer:
      return target.IsPlayer() && player.Interaction().CanAttackSpellTarget(target) &&
             !target.State().IsDead() &&
             (target.State().GetDynamicFlags() & kUnitDynFlagDead) == 0 &&
             target.State().GetCreatureType() != CreatureTypeId::kCritter;
    case TargetFilter::kFriend:
      return player.Interaction().CanAssistSpellTarget(target, false) && target.State().GetHealth() > 0;
    case TargetFilter::kFriendPlayer:
      return target.IsPlayer() && player.Interaction().CanAssistSpellTarget(target, false) &&
             target.State().GetHealth() > 0;
    case TargetFilter::kParty:
    case TargetFilter::kRaid:
      return false;
  }

  return false;
}

bool TargetingSystem::CanShowTutorialPopup(std::uint32_t tutorial_index) const {
  return tutorials_.IsSeenBitsInitialized() &&
         !tutorials_.IsTutorialSeen(tutorial_index);
}

const ObjectManager* TargetingSystem::Objects() const {
  return session_ ? &session_->objects() : nullptr;
}

void TargetingSystem::TriggerPlayerTargetTutorialIfNeeded(
    const CGUnit_C& player,
    const CGUnit_C& target) {
  if (!target.IsPlayer()) {
    return;
  }
  if (target.GetGuid() == player.GetGuid()) {
    return;
  }
  if (!CanShowTutorialPopup(0x12u)) {
    return;
  }

  tutorials_.TriggerTutorial(0x12u);
}

void TargetingSystem::TabTarget(bool reverse, TargetFilter filter) {
  const ObjectManager* obj_mgr = Objects();
  if (obj_mgr == nullptr) {
    return;
  }

  const auto* active_player = obj_mgr->GetLocalPlayerTyped();
  if (active_player == nullptr) {
    return;
  }

  const CGUnit_C* source = active_player->GetActiveControlUnit();
  if (source == nullptr) {
    source = active_player;
  }

  const auto rebuild_candidates = [&]() {
    tab_candidates_.clear();

    const auto source_position = source->GetPosition();
    const auto source_facing = NormalizePositiveRadians(source->GetOrientation());

    obj_mgr->EnumVisibleObjects([&](const WorldObject& obj) {
      if (obj.GetGuid() == source->GetGuid() ||
          !PassesTargetAcquisitionFilter(*source, obj, filter, session_)) {
        return;
      }

      const auto& unit = static_cast<const CGUnit_C&>(obj);
      const float dx = unit.GetX() - source_position.x;
      const float dy = unit.GetY() - source_position.y;
      const float angle_to_target = NormalizePositiveRadians(std::atan2(dy, dx));
      const float facing_delta = ComputePositiveFacingDelta(source_facing, angle_to_target);
      const bool in_front_arc =
          facing_delta < kTabFrontConeHalfAngle ||
          facing_delta > (kTwoPi - kTabFrontConeHalfAngle);

      const float distance_sq =
          static_cast<float>(source->GetSquaredDistanceToPosition(unit.GetPosition()));
      const float max_distance_sq =
          in_front_arc ? kTabTargetDistanceSqInFront : kTabTargetDistanceSqOffAngle;
      if (distance_sq > max_distance_sq) {
        return;
      }

      if (IsTabTargetCreatureTypeExcluded(session_, unit)) {
        return;
      }
      if (unit.Presentation().DisplayId() == 0) {
        return;
      }
      if (IsCommentatorOnlyTabTarget(unit, commentator_)) {
        return;
      }

      tab_candidates_.push_back(
          TabTargetCandidate{unit.GetGuid().GetRawValue(), distance_sq, in_front_arc});
    });

    std::sort(tab_candidates_.begin(), tab_candidates_.end(),
              [](const TabTargetCandidate& lhs, const TabTargetCandidate& rhs) {
                if (lhs.in_front_arc != rhs.in_front_arc) {
                  return lhs.in_front_arc && !rhs.in_front_arc;
                }
                return lhs.distance_sq < rhs.distance_sq;
              });

    tab_index_ = -1;
  };

  while (true) {
    const auto now = NowMs();
    const bool needs_rebuild =
        tab_list_built_ms_ == 0 || tab_last_selection_ms_ == 0 ||
        (now - tab_last_selection_ms_) >= kTabCacheTimeoutMs ||
        tab_candidates_.empty() || filter != tab_cache_filter_;

    if (needs_rebuild) {
      rebuild_candidates();
      tab_list_built_ms_ = now != 0 ? now : 1;
      tab_cache_filter_ = filter;
      if (tab_candidates_.empty()) {
        return;
      }
    }

    int32_t active_index = tab_index_;
    if (active_index >= 0 && active_index < static_cast<int32_t>(tab_candidates_.size()) &&
        tab_candidates_[active_index].guid == target_guid_) {
      if (reverse) {
        active_index = active_index > 0
                           ? (active_index - 1)
                           : (static_cast<int32_t>(tab_candidates_.size()) - 1);
      } else {
        ++active_index;
        if (active_index >= static_cast<int32_t>(tab_candidates_.size())) {
          active_index = 0;
          if ((now - tab_list_built_ms_) >= 1000) {
            tab_list_built_ms_ = 0;
            continue;
          }
        }
      }
    } else if (active_index < 0 || active_index >= static_cast<int32_t>(tab_candidates_.size())) {
      active_index = 0;
    }

    tab_last_selection_ms_ = now;
    const int32_t starting_index = active_index;

    while (true) {
      const auto candidate_guid = tab_candidates_[active_index].guid;
      const auto* candidate_object = obj_mgr->Get(ObjectGuid(candidate_guid));
      if (candidate_object != nullptr &&
          PassesTargetAcquisitionFilter(*source, *candidate_object, filter, session_)) {
        tab_index_ = active_index;
        SetTarget(candidate_guid);
        return;
      }

      ++active_index;
      if (active_index >= static_cast<int32_t>(tab_candidates_.size())) {
        active_index = 0;
        if ((now - tab_list_built_ms_) >= 1000) {
          tab_list_built_ms_ = 0;
          break;
        }
      }

      if (active_index == starting_index) {
        tab_candidates_.clear();
        tab_index_ = -1;
        return;
      }
    }
  }
}

void TargetingSystem::TargetNearest(TargetFilter filter, bool reverse) {

  TabTarget(reverse, filter);
}

void TargetingSystem::TargetDirection(float facing_radians, float cone_radians,
                                      TargetFilter filter) {
  const ObjectManager* mgr = Objects();
  if (mgr == nullptr || session_ == nullptr) {
    return;
  }

  if (direction_target_guid_ != 0 &&
      mgr->Get(ObjectGuid(direction_target_guid_)) == nullptr) {
    direction_target_guid_ = 0;
  }
  if (direction_target_guid_ != 0) {
    return;
  }

  const auto* active_player = mgr->GetLocalPlayerTyped();
  if (active_player == nullptr) {
    return;
  }

  const WorldObject* source = active_player;
  if (const auto* controlled = active_player->GetActiveControlUnit();
      controlled != nullptr) {
    source = controlled;
  }
  if (source == nullptr || !source->IsUnit()) {
    return;
  }
  const auto& source_unit = static_cast<const CGUnit_C&>(*source);

  const WorldObject* preferred_anchor = nullptr;
  if (target_guid_ != 0) {
    if (const auto* current_target = mgr->Get(ObjectGuid(target_guid_));
        current_target != nullptr && current_target->IsUnit()) {
      preferred_anchor = current_target;
    }
  }
  const float fallback_facing = source->GetOrientation();

  const WorldObject* anchor = nullptr;
  Vec3 anchor_position{};
  Vec3 anchor_screen{};
  if (!TryResolveDirectionTargetAnchor(*source, preferred_anchor, world_frame_,
                                       anchor, anchor_position, anchor_screen)) {
    return;
  }

  const auto* const camera = session_->world_camera();
  const float camera_yaw =
      camera != nullptr ? camera->yaw() : fallback_facing;
  Vec3 desired_world_direction = NormalizeFlatVec3(RotateFlatClockwise(
      {std::cos(camera_yaw), std::sin(camera_yaw), 0.0f}, facing_radians));
  if (LengthSq2(desired_world_direction) <= 1.0e-6f) {
    desired_world_direction = {std::cos(fallback_facing), std::sin(fallback_facing), 0.0f};
  }

  const Vec3 desired_screen_direction{
      std::sin(facing_radians), std::cos(facing_radians), 0.0f};
  const float cone_threshold =
      std::cos(std::max(cone_radians, kDirectionMinCone) * 0.5f);

  bool found_in_cone = false;
  float best_score = std::numeric_limits<float>::max();
  std::uint64_t best_guid = 0;

  mgr->EnumVisibleObjects([&](const WorldObject& object) {
    if (object.GetGuid() == source->GetGuid() || object.GetGuid() == anchor->GetGuid()) {
      return;
    }
    if (!PassesTargetAcquisitionFilter(source_unit, object, filter, session_) ||
        object.GetDisplayId() == 0) {
      return;
    }

    const auto& unit = static_cast<const CGUnit_C&>(object);
    if (IsTabTargetCreatureTypeExcluded(session_, unit)) {
      return;
    }

    const Vec3 candidate_position = GetWorldPosition(object);
    const Vec3 origin_position = GetWorldPosition(*source);
    const Vec3 source_delta{candidate_position.x - origin_position.x,
                            candidate_position.y - origin_position.y,
                            candidate_position.z - origin_position.z};
    if (source_delta.x * source_delta.x + source_delta.y * source_delta.y +
            source_delta.z * source_delta.z >
        kDirectionTargetRangeSq) {
      return;
    }

    const Vec3 anchor_delta{object.GetX() - anchor_position.x,
                            object.GetY() - anchor_position.y,
                            object.GetZ() - anchor_position.z};
    Vec3 anchor_flat = NormalizeFlatVec3(anchor_delta);
    if (LengthSq2(anchor_flat) <= 1.0e-6f) {
      return;
    }

    Vec3 candidate_screen{};
    if (!TryProjectWorldPosition(world_frame_, GetWorldPosition(object),
                                 candidate_screen)) {
      return;
    }

    const Vec3 screen_delta{candidate_screen.x - anchor_screen.x,
                            candidate_screen.y - anchor_screen.y,
                            candidate_screen.z - anchor_screen.z};
    const float screen_length_sq = LengthSq3(screen_delta);
    if (screen_length_sq <= 1.0e-6f) {
      return;
    }

    const Vec3 normalized_screen_delta = NormalizeFlatVec3(screen_delta);
    if (Dot2(normalized_screen_delta, desired_screen_direction) < 0.0f) {
      return;
    }

    const float world_dot = Dot2(anchor_flat, desired_world_direction);
    const bool in_cone = world_dot >= cone_threshold;
    if (!in_cone && found_in_cone) {
      return;
    }

    float score = screen_length_sq;
    if (!in_cone) {
      const float cone_penalty = 1.0f - world_dot;
      score *= cone_penalty * cone_penalty;
    } else if (!found_in_cone) {
      best_score = std::numeric_limits<float>::max();
      found_in_cone = true;
    }

    if (score > best_score) {
      return;
    }

    best_score = score;
    best_guid = object.GetGuid().GetRawValue();
  });

  if (best_guid == 0) {
    return;
  }

  SetTarget(best_guid);
  if (target_guid_ == best_guid) {
    direction_target_guid_ = best_guid;
  }
}

void TargetingSystem::FinishDirectionTarget() {
  direction_target_guid_ = 0;
}

void TargetingSystem::TargetLastTarget() {
  if (last_target_guid_ != 0) {
    SetTarget(last_target_guid_);
    return;
  }

  if (target_guid_ != 0) {
    ClearTarget();
  }
}

void TargetingSystem::TargetLastEnemy() {
  if (session_ != nullptr &&
      IsStoredTargetGuidRetargetable(*session_, last_enemy_guid_)) {
    SetTarget(last_enemy_guid_);
  }
}

void TargetingSystem::TargetLastFriend() {
  if (session_ != nullptr &&
      IsStoredTargetGuidRetargetable(*session_, last_friend_guid_)) {
    SetTarget(last_friend_guid_);
  }
}

void TargetingSystem::SetTarget(uint64_t guid) {
  if (guid == 0) {
    ClearTarget();
    return;
  }
  if (!HasWorldMapContext()) {
    return;
  }

  if (const auto* const local_player =
          session_ != nullptr ? session_->objects().GetLocalPlayerTyped()
                              : nullptr;
      local_player != nullptr &&
      !local_player->State().GetCharmedBy().IsEmpty()) {
    return;
  }

  if (session_ != nullptr &&
      session_->spells().GetTargeting().GetSpellId() != 0u) {
    const auto* const clicked = session_->objects().Get(ObjectGuid(guid));
    if (clicked != nullptr) {
      (void)SpellAction_TryAssignTargetByGuid(*session_, guid);
      return;
    }
  }

  if (guid == target_guid_) return;

  if (session_ != nullptr) {
    session_->click_to_move().CancelInteraction();
  }

  const bool should_reengage = attack_follow_active_ || attack_swing_active_;
  const bool keep_follow = attack_keep_follow_;
  const ObjectManager* const object_manager = Objects();
  const auto* const player_object =
      object_manager ? object_manager->GetLocalPlayer() : nullptr;
  const auto* const player_unit =
      (player_object != nullptr && player_object->IsUnit())
          ? static_cast<const CGUnit_C*>(player_object)
          : nullptr;
  const auto* const selected_object =
      object_manager ? object_manager->Get(ObjectGuid(guid)) : nullptr;
  const auto* const selected_unit =
      (selected_object != nullptr && selected_object->IsUnit())
          ? static_cast<const CGUnit_C*>(selected_object)
          : nullptr;

  if (target_guid_ != 0) {
    last_target_guid_ = target_guid_;
    ClearTargetInternal(target_guid_, false);
  }

  target_guid_ = guid;

  if (selected_unit != nullptr) {
    if (player_unit != nullptr) {
      if (player_unit->Interaction().CanAttackSpellTarget(*selected_unit)) {
        last_enemy_guid_ = guid;
      } else if (player_unit->Interaction().IsFriendlyTo(*selected_unit)) {
        last_friend_guid_ = guid;
      }
      TriggerPlayerTargetTutorialIfNeeded(*player_unit, *selected_unit);
    }
  }

  tab_index_ = -1;
  for (int32_t i = 0; i < static_cast<int32_t>(tab_candidates_.size()); ++i) {
    if (tab_candidates_[i].guid == guid) {
      tab_index_ = i;
      break;
    }
  }

  if (session_) {
    session_->objects().SetTarget(ObjectGuid(guid));
  }

  SendSetSelection(guid);

  if (target_changed_fn_) {
    target_changed_fn_();
  }

  RetargetAttackIfNeeded(guid, should_reengage, keep_follow);

}

void TargetingSystem::SetTargetIfNone(uint64_t guid) {
  if (!HasTarget()) {
    SetTarget(guid);
  }
}

void TargetingSystem::ClearTarget(uint64_t expected_guid, bool send_packet) {
  const bool had_matching_target =
      target_guid_ != 0 && (expected_guid == 0 || expected_guid == target_guid_);
  if (had_matching_target) {
    last_target_guid_ = target_guid_;
    InvalidateTabTargetBuildTimestamp();
  }
  ClearTargetInternal(expected_guid, send_packet);
  if (had_matching_target && send_packet) {
    CancelAutoRepeatSpellIfActive();
  }
}

void TargetingSystem::InvalidateTrackedGuidReferences(const std::uint64_t guid) {
  if (guid == 0) {
    return;
  }

  if (session_ != nullptr) {
    session_->click_to_move().CancelInteraction(ObjectGuid(guid));
  }

  if (guid == target_guid_) {
    if (session_ != nullptr) {
      session_->spells().CancelSpell(*session_, SpellSlotType::kCurrent);
    }
    ClearTargetInternal(guid, true);
  }

  if (last_target_guid_ == guid) {
    last_target_guid_ = 0;
  }
  if (last_enemy_guid_ == guid) {
    last_enemy_guid_ = 0;
  }
  if (last_friend_guid_ == guid) {
    last_friend_guid_ = 0;
  }

  if (session_ != nullptr &&
      session_->objects().GetMouseoverGuid().GetRawValue() == guid) {
    session_->objects().SetMouseover(ObjectGuid());
  }

  if (world_frame_.GetMouseoverGuid().GetRawValue() == guid) {
    world_frame_.SetMouseoverGuid(ObjectGuid());
  }

  if (focus_guid() == guid) {
    ClearFocus();
  }

  if (direction_target_guid_ == guid) {
    direction_target_guid_ = 0;
  }
}

bool TargetingSystem::HasWorldMapContext() const {
  return session_ && session_->has_current_map();
}

void TargetingSystem::InvalidateTabTargetBuildTimestamp() {
  tab_list_built_ms_ = 0;
}

void TargetingSystem::CancelAutoRepeatSpellIfActive() {
  if (session_ == nullptr) {
    return;
  }
  auto& spell_client = session_->spells();
  if (spell_client.GetAutoRepeatSpellId() == 0) {
    return;
  }

  if (session_ != nullptr) {
    spell_client.CancelSpell(*session_, SpellSlotType::kAutoRepeat);
  }
}

void TargetingSystem::ClearTargetInternal(uint64_t expected_guid,
                                          bool send_packet) {
  if (target_guid_ == 0) return;
  if (expected_guid != 0 && expected_guid != target_guid_) return;

  if (session_ != nullptr) {
    session_->click_to_move().CancelInteraction(ObjectGuid(target_guid_));
  }

  CloseTargetLootWindowIfNeeded(target_guid_);
  if (target_guid_ == 0) return;
  if (expected_guid != 0 && expected_guid != target_guid_) return;
  StopFollow();
  StopAttackInternal(false);

  target_guid_ = 0;
  tab_index_ = -1;

  if (session_) {
    session_->objects().SetTarget(ObjectGuid(0));
    const auto* local_player = session_->objects().GetLocalPlayerTyped();
    if (local_player != nullptr && !session_->pet().GetPrimaryPetGuid().IsEmpty()) {
      session_->pet().StopAttackIfActive(session_->interaction());
    }
  }

  if (send_packet && HasWorldMapContext()) {
    SendSetSelection(0);
  }

  if (send_packet && HasWorldMapContext() && target_changed_fn_) {
    target_changed_fn_();
  }

}

void TargetingSystem::SetFocus(uint64_t guid) {
  if (session_) {
    session_->objects().SetFocusTarget(ObjectGuid(guid));
  }

  if (focus_changed_fn_) {
    focus_changed_fn_();
  }

}

void TargetingSystem::ClearFocus() {
  if (session_) {
    session_->objects().SetFocusTarget(ObjectGuid(0));
  }

  if (focus_changed_fn_) {
    focus_changed_fn_();
  }

}

uint64_t TargetingSystem::focus_guid() const {
  return session_ != nullptr
             ? session_->objects().GetFocusTargetGuid().GetRawValue()
             : 0;
}

bool TargetingSystem::HasFocus() const {
  return focus_guid() != 0;
}

bool TargetingSystem::IsAttackActive() const {
  return session_ != nullptr && session_->spells().GetAutoAttackSpellId() != 0;
}

bool TargetingSystem::HasMeleeAttackState() const {
  return IsAttackActive() || attack_follow_active_ || attack_keep_follow_ ||
         attack_swing_active_;
}

void TargetingSystem::AssistUnit(uint64_t guid) {
  const ObjectManager* mgr = Objects();
  if (!mgr) return;

  const auto* unit = mgr->Get(ObjectGuid(guid));
  if (!unit || !unit->IsUnit()) return;

  const auto assist_target_guid = unit->GetGuidField(UNIT_FIELD_TARGET);
  if (assist_target_guid.IsEmpty()) {
    return;
  }

  const auto* assist_target_object = mgr->Get(assist_target_guid);
  const bool can_retarget =
      (assist_target_object != nullptr && assist_target_object->IsUnit()) ||
      (session_ != nullptr && IsAssistTargetGuidRetargetable(*session_, assist_target_guid));
  if (can_retarget) {
    SetTarget(assist_target_guid.GetRawValue());
  }

  if (!cvars_.GetCVarBool("assistAttack")) {
    return;
  }

  if (!HasWorldMapContext() || assist_target_object == nullptr || !assist_target_object->IsUnit()) {
    return;
  }

  const auto* player = mgr->GetLocalPlayer();
  if (player == nullptr || !player->IsPlayer()) {
    return;
  }

  StartAttack(assist_target_guid.GetRawValue(), false, false);
}

bool TargetingSystem::ValidateFollowTarget(std::uint64_t guid) const {
  const ObjectManager* mgr = Objects();
  if (guid == 0 || !mgr) {
    return false;
  }

  const auto* player_obj = mgr->GetLocalPlayer();
  const auto* target_obj = mgr->Get(ObjectGuid(guid));
  if (!player_obj || !player_obj->IsUnit() || !target_obj || !target_obj->IsUnit()) {
    return false;
  }

  const auto& player = static_cast<const CGUnit_C&>(*player_obj);
  const auto& target = static_cast<const CGUnit_C&>(*target_obj);
  if (!target.IsPlayer()) {
    return false;
  }
  if (!target.State().GetCharmedBy().IsEmpty()) {
    return false;
  }
  if (player.GetGuid() == target.GetGuid()) {
    return false;
  }
  if (player.State().GetHealth() == 0 || target.State().GetHealth() == 0) {
    return false;
  }
  if (player.State().IsStunned()) {
    return false;
  }
  if (!player.GetGuidField(UNIT_FIELD_CHANNEL_OBJECT).IsEmpty()) {
    return false;
  }
  if (!player.Interaction().IsFriendlyTo(target) ||
      player.Interaction().IsHostileTo(target)) {
    return false;
  }

  return true;
}

void TargetingSystem::StartFollow(std::uint64_t guid) {
  const ObjectManager* mgr = Objects();
  if (!ValidateFollowTarget(guid) || !mgr) {
    return;
  }

  const auto* target = mgr->Get(ObjectGuid(guid));
  if (!target) {
    return;
  }
  const auto* player = mgr->GetLocalPlayer();
  if (player == nullptr) {
    return;
  }

  SetTarget(guid);
  if (target_guid_ != guid) {
    return;
  }

  if (RejectInteractionIfWarningDistanceExceeded(static_cast<const CGUnit_C&>(*player), *target,
                                                 kFollowInteractionActionType)) {
    return;
  }

  const bool changed = !follow_active_ || follow_guid_ != guid;
  follow_guid_ = guid;
  follow_active_ = true;

  DriveAutoFollowTowards(*target, !IsInFollowRange(*target));

  if (changed) {
    NotifyAutoFollowChanged();
  }
}

void TargetingSystem::StopFollow() {
  if (!follow_active_ && follow_guid_ == 0) {
    return;
  }

  follow_active_ = false;
  follow_guid_ = 0;

  if (!attack_follow_active_) {
    StopOwnedAutoFollowMovement();
  }

  NotifyAutoFollowChanged();
}

AttackStartOutcome TargetingSystem::ValidateAttackStart() const {
  const auto no_action = AttackStartOutcome{AttackStartResult::kNoAction};
  const ObjectManager* mgr = Objects();
  if (mgr == nullptr) {
    return no_action;
  }
  const auto* player = mgr->GetLocalPlayerTyped();
  if (!player) {
    return no_action;
  }
  if (player->State().GetHealth() == 0) {
    return player->GetActiveControlUnit() == nullptr
               ? AttackStartOutcome{AttackStartResult::kDead}
               : no_action;
  }
  if ((player->State().GetUnitFlags2() & kUnitFlags2AttackTargetLockout) != 0u) {
    return {AttackStartResult::kClientLockedOut};
  }
  const auto* const dbc = session_ != nullptr ? session_->GetDbcLoader() : nullptr;
  const auto blocked_by_aura = [dbc, player](const auto has_compatible_aura,
                                             const AttackStartResult fixed_result) {
    std::uint32_t mechanic = 0;
    if (dbc != nullptr && has_compatible_aura(*player, *dbc, nullptr, &mechanic)) {
      return AttackStartOutcome{fixed_result, mechanic};
    }
    return AttackStartOutcome{fixed_result};
  };
  if (player->State().IsStunned()) {
    return blocked_by_aura(CGUnit_C__HasCompatibleStunAura, AttackStartResult::kStunned);
  }
  if (player->State().IsPacified()) {
    return blocked_by_aura(CGUnit_C__HasCompatiblePacifyAura, AttackStartResult::kPacified);
  }
  if (player->State().IsFleeing()) {
    return blocked_by_aura(CGUnit_C__HasCompatibleFearAura, AttackStartResult::kFleeing);
  }
  if (player->State().IsConfused()) {
    return blocked_by_aura(CGUnit_C__HasCompatibleConfuseAura, AttackStartResult::kConfused);
  }
  if (const auto charmer = player->State().GetCharmedBy();
      !charmer.IsEmpty() && charmer != player->GetGuid()) {
    return blocked_by_aura(CGUnit_C__HasCompatibleCharmAura, AttackStartResult::kCharmed);
  }
  if (const auto channel_spell_id = player->Casts().GetChannelCast().spell_id;
      channel_spell_id != 0u && dbc != nullptr) {
    if (const auto* const channel_spell = dbc->spell().LookupEntry(channel_spell_id);
        channel_spell != nullptr &&
        (channel_spell->attributes_ex &
         kSpellAttributesExBlocksAttackWhileChanneling) != 0u) {
      return {AttackStartResult::kChanneling};
    }
  }
  if (player->Mount().IsMounted(*player) &&
      !player->State().CanActWhileMounted() &&
      !player->Movement().CanChangeDirection()) {
    return {AttackStartResult::kMounted};
  }
  return no_action;
}

AttackStartOutcome TargetingSystem::StartAttack(std::uint64_t guid, bool keep_follow,
                                                bool suppress_range_error,
                                                std::uint32_t spell_id) {
  const auto no_action = AttackStartOutcome{AttackStartResult::kNoAction};
  if (guid == 0) guid = target_guid_;
  const ObjectManager* mgr = Objects();
  if (guid == 0 || !mgr) return no_action;

  CancelAutoRepeatSpellIfActive();
  if (const auto* const player_for_stand = mgr->GetLocalPlayerTyped();
      player_for_stand != nullptr && player_for_stand->GetPlayerStandState() != 0u) {
    if (auto* const mutable_player = session_->objects().GetActivePlayer();
        mutable_player != nullptr) {
      mutable_player->Animation().MaybeStandUpIfPlayer(*session_, 0u);
    }
  }

  SetTarget(guid);
  if (target_guid_ != guid) return no_action;

  const auto attack_precondition = ValidateAttackStart();
  if (attack_precondition.result != AttackStartResult::kNoAction) {
    return attack_precondition;
  }

  const auto* player = mgr->GetLocalPlayerTyped();
  if (!player) {
    StopAttackInternal(false);
    return no_action;
  }

  const auto* target = mgr->Get(ObjectGuid(guid));

  if (!target || !target->IsUnit() || !IsSelectable(*target) ||
      target->GetHealth() == 0 ||
      !player->Interaction().CanAttackSpellTarget(
          static_cast<const CGUnit_C&>(*target))) {
    StopAttackInternal(false);
    return {AttackStartResult::kInvalidTarget};
  }

  if (RejectInteractionIfWarningDistanceExceeded(static_cast<const CGUnit_C&>(*player), *target,
                                                 kAttackFollowInteractionActionType)) {
    return {AttackStartResult::kRangeRejected};
  }

  const auto& target_unit = static_cast<const CGUnit_C&>(*target);
  if (!IsInAttackRange(target_unit)) {
    StopAttackFollow();
    return suppress_range_error ? no_action
                                : AttackStartOutcome{AttackStartResult::kRangeRejected};
  }

  const bool was_active = IsAttackActive();
  session_->spells().SetAutoAttackSpellId(
      ResolveAutoAttackSpellId(session_->spells(), spell_id));
  SpellC_ResetMeleeAttackCastFailureReason();

  const bool state_changed = !was_active || attack_keep_follow_ != keep_follow;
  attack_keep_follow_ = keep_follow;
  if (state_changed) {
    NotifyAttackStateChanged();
  }

  StopAttackFollow();
  if (!attack_swing_active_ || attack_swing_target_guid_ != guid ||
      attack_stop_pending_) {
    SendAttackSwing(guid);
    attack_swing_active_ = true;
    attack_swing_target_guid_ = guid;
  }
  return {AttackStartResult::kStarted};
}

void TargetingSystem::StopAttack(bool send_packet) {
  StopAttackInternal(send_packet);
}

void TargetingSystem::HandleServerAttackerStateUpdate(
    const std::uint64_t attacker_guid, const std::uint64_t victim_guid) {
  if (session_ == nullptr || attacker_guid == 0 || victim_guid == 0) {
    return;
  }

  const auto local_player_guid = session_->objects().GetLocalPlayerGuid().GetRawValue();
  const ObjectManager* const object_manager = Objects();

  if (attacker_guid == local_player_guid) {

    session_->combat().ClearSwingError(session_->CurrentClientTimeMs());

    const auto* const attacker =
        object_manager ? object_manager->GetLocalPlayerTyped() : nullptr;
    const auto* const victim_object =
        object_manager ? object_manager->Get(ObjectGuid(victim_guid)) : nullptr;
    const auto* const victim =
        (victim_object != nullptr && victim_object->IsUnit())
            ? static_cast<const CGUnit_C*>(victim_object)
            : nullptr;
    if (attacker != nullptr && victim != nullptr &&
        ExceedsAttackStopRange(*attacker, *victim)) {
      SendAttackStopRequest();
    }
  }

  if (victim_guid == local_player_guid && attacker_guid != local_player_guid) {
    SetTargetIfNone(attacker_guid);
  }
}

void TargetingSystem::HandleServerSpellStart(const std::uint64_t caster_guid,
                                             const bool target_is_unit,
                                             const std::uint64_t target_guid) {
  if (session_ == nullptr || caster_guid == 0) {
    return;
  }
  const auto local_player_guid = session_->objects().GetLocalPlayerGuid().GetRawValue();

  if (caster_guid == local_player_guid) {
    return;
  }

  if (!target_is_unit || target_guid == 0 || target_guid != local_player_guid) {
    return;
  }
  const ObjectManager* const object_manager = Objects();
  if (object_manager == nullptr) {
    return;
  }

  const auto* const player = object_manager->GetLocalPlayerTyped();
  const auto* const caster = object_manager->GetUnit(ObjectGuid(caster_guid));
  if (player == nullptr || caster == nullptr) {
    return;
  }

  if (caster->Interaction().IsFriendlyTo(*player)) {
    return;
  }

  SetTargetIfNone(caster_guid);
}

void TargetingSystem::HandleServerAttackStop(const std::uint64_t attacker_guid,
                                             const std::uint64_t ) {
  if (session_ == nullptr || attacker_guid == 0) {
    return;
  }

  const auto local_player_guid = session_->objects().GetLocalPlayerGuid().GetRawValue();
  if (attacker_guid != local_player_guid) {
    return;
  }

  attack_stop_pending_ = false;
  attack_swing_target_guid_ = 0;
  StopAttackInternal(false);
}

void TargetingSystem::Update(float ) {
  const ObjectManager* mgr = Objects();
  if (!mgr) return;

  if (follow_active_) {
    const auto* follow_target = mgr->Get(ObjectGuid(follow_guid_));
    if (!follow_target || !ValidateFollowTarget(follow_guid_)) {
      StopFollow();
    } else {
      if (!attack_follow_active_) {
        DriveAutoFollowTowards(*follow_target,
                               !IsInFollowRange(*follow_target));
      }
    }
  }

  if (IsAttackActive()) {
    DisplaySwingErrorIfDue();

    if (attack_target_change_seen_guid_ != target_guid_) {
      attack_target_change_time_ms_ = core::GameClock::GetTickCount32();
      attack_target_change_seen_guid_ = target_guid_;
    }
  }

  if (!IsAttackActive() || !attack_follow_active_) return;

  const auto* player = mgr->GetLocalPlayerTyped();
  const auto* target = mgr->Get(ObjectGuid(target_guid_));
  if (!player || !target || !target->IsUnit() || !IsSelectable(*target) ||
      !player->Interaction().CanAttackSpellTarget(
          static_cast<const CGUnit_C&>(*target))) {
    StopAttackInternal(false);
    return;
  }

  const auto& target_unit = static_cast<const CGUnit_C&>(*target);
  if (IsInAttackRange(target_unit, false)) {
    StopAttackFollow();
    if (!attack_swing_active_ || attack_swing_target_guid_ != target_guid_ ||
        attack_stop_pending_) {
      SendAttackSwing(target_guid_);
      attack_swing_active_ = true;
      attack_swing_target_guid_ = target_guid_;
    }
    return;
  }

  if (attack_keep_follow_) {
    attack_keep_follow_ = false;
    StopAttackFollow();
    return;
  }

  StartAttackFollow();
}

bool TargetingSystem::InteractWith(std::uint64_t guid) {
  const ObjectManager* mgr = Objects();
  if (!mgr) {
    return false;
  }

  if (!mgr->GetLocalPlayer()) {

    return true;
  }

  if (guid == 0) {
    return false;
  }

  const auto* object = mgr->Get(ObjectGuid(guid));
  if (!object) {
    return false;
  }

  if (object->IsUnit()) {
    SetTarget(guid);

    object = mgr->Get(ObjectGuid(guid));
    if (!object) {
      return false;
    }
  }

  session_->click_to_move().CancelInteraction(ObjectGuid(guid));
  object->OnRightClickInteract(session_, this);
  return true;
}

std::uint32_t TargetingSystem::GetAutoRangedCombatSpellId() const {
  if (!cvars_.GetCVarBool("autoRangedCombat")) {
    return 0;
  }

  return spellbook_.GetAutoRangedCombatSpellId();
}

bool TargetingSystem::IsInAttackRange(const CGUnit_C& unit) const {
  return IsInAttackRange(unit, true);
}

bool TargetingSystem::IsInAttackRange(
    const CGUnit_C& unit, bool allow_auto_ranged_substitution) const {
  float player_x = 0.0f;
  float player_y = 0.0f;
  float player_z = 0.0f;
  float attack_range = 0.0f;

  const ObjectManager* mgr = Objects();
  const CGUnit_C* moving_unit =
      session_ != nullptr
          ? ResolveEffectiveMovingUnit(*session_)
          : (mgr != nullptr ? mgr->GetLocalPlayerTyped() : nullptr);
  if (moving_unit != nullptr) {
    player_x = moving_unit->GetX();
    player_y = moving_unit->GetY();
    player_z = moving_unit->GetZ();
  }

  if (mgr) {
    if (const auto* player = mgr->GetLocalPlayerTyped()) {
      attack_range = interaction_range::ComputeUnitInteractionRange(
          player->GetFloat(UNIT_FIELD_COMBATREACH),
          unit.GetFloat(UNIT_FIELD_COMBATREACH));

      if (const auto spell_id = allow_auto_ranged_substitution
                                     ? GetAutoRangedCombatSpellId()
                                     : 0;
          spell_id != 0) {
        const auto* dbc = session_ != nullptr ? session_->GetDbcLoader() : nullptr;
        const auto* spell =
            dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
        if (spell != nullptr) {
          const auto* range_entry =
              spell->range_index != 0
                  ? dbc->spell_range().LookupEntry(spell->range_index)
                  : nullptr;
          const bool use_friendly_range =
              player->Interaction().CanAssistSpellTarget(unit, false);
          const auto range = SpellTargetValidator::GetTargetRangeWindow(
              *spell, range_entry, *player, unit, use_friendly_range, session_);
          if (range.max_range > 0.0f) {
            attack_range = range.max_range;
          }
        }
      }
    }
  }

  const double dx = static_cast<double>(player_x) - unit.GetX();
  const double dy = static_cast<double>(player_y) - unit.GetY();
  const double dz = static_cast<double>(player_z) - unit.GetZ();
  return dx * dx + dy * dy + dz * dz <
         static_cast<double>(attack_range * attack_range);
}

void TargetingSystem::CloseTargetLootWindowIfNeeded(std::uint64_t guid) {
  if (!session_ || guid == 0) return;

  auto& loot = session_->loot();
  if (!loot.is_looting()) return;
  if (loot.loot_window().source_guid.GetRawValue() != guid) return;

  CloseActiveLootWindow(*session_,
                        CloseLootWindowOptions{
                            .send_release = true,
                            .skip_item_check = false,
                            .show_interrupted = false,
                            .clear_dead_target = false,
                        });
}

void TargetingSystem::NotifyAttackStateChanged() {
  if (attack_state_changed_fn_) {
    attack_state_changed_fn_();
  }
}

void TargetingSystem::NotifyAutoFollowChanged() {
  if (auto_follow_changed_fn_) {
    auto_follow_changed_fn_(follow_active_);
  }
}

bool TargetingSystem::IsInFollowRange(const WorldObject& unit) const {
  float player_x = 0.0f;
  float player_y = 0.0f;
  float player_z = 0.0f;

  const ObjectManager* const mgr = Objects();
  const CGUnit_C* const moving_unit =
      session_ != nullptr
          ? ResolveEffectiveMovingUnit(*session_)
          : (mgr != nullptr ? mgr->GetLocalPlayerTyped() : nullptr);
  if (moving_unit != nullptr) {
    player_x = moving_unit->GetX();
    player_y = moving_unit->GetY();
    player_z = moving_unit->GetZ();
  }

  const double dx = static_cast<double>(player_x) - unit.GetX();
  const double dy = static_cast<double>(player_y) - unit.GetY();
  const double dz = static_cast<double>(player_z) - unit.GetZ();
  return dx * dx + dy * dy + dz * dz < static_cast<double>(kFollowRangeSquared);
}

void TargetingSystem::StartAttackFollow() {
  attack_follow_active_ = true;
}

void TargetingSystem::StopAttackFollow() {
  if (!attack_follow_active_) {
    return;
  }
  attack_follow_active_ = false;
  if (!follow_active_) {
    StopOwnedAutoFollowMovement();
  }
}

void TargetingSystem::DriveAutoFollowTowards(const WorldObject& target,
                                             const bool move_forward) {
  if (session_ == nullptr) {
    return;
  }

  auto* const mover = ResolveEffectiveMovingUnit(*session_);
  if (mover == nullptr) {
    owns_auto_follow_forward_ = false;
    return;
  }

  const float dx = target.GetX() - mover->GetX();
  const float dy = target.GetY() - mover->GetY();
  if (std::fabs(dx) >= 1.0e-6f || std::fabs(dy) >= 1.0e-6f) {
    const float facing = std::atan2(dy, dx);
    const float delta = std::remainder(
        facing - mover->GetOrientation(), kTwoPi);
    if (std::fabs(delta) >= 1.0e-4f) {
      mover->Movement().SendSetFacing(
          *session_, session_->CurrentClientTimeMs(), facing);
    }
  }

  const bool already_forward =
      (mover->GetMovementInfo().flags & kMoveFlagForward) != 0u;
  if (move_forward) {
    if (!already_forward) {
      mover->Movement().SendForward(
          *session_, session_->CurrentClientTimeMs(), true);
      owns_auto_follow_forward_ = true;
    }
    return;
  }

  StopOwnedAutoFollowMovement();
}

void TargetingSystem::StopOwnedAutoFollowMovement() {
  if (!owns_auto_follow_forward_) {
    return;
  }

  if (session_ != nullptr) {
    if (auto* const mover = ResolveEffectiveMovingUnit(*session_);
        mover != nullptr) {
      const std::uint32_t timestamp = session_->CurrentClientTimeMs();
      mover->Movement().StopForward(timestamp);
      mover->Movement().QueueHeartbeat(timestamp);
    }
  }
  owns_auto_follow_forward_ = false;
}

void TargetingSystem::StopAttackInternal(bool send_packet) {
  const bool was_active = IsAttackActive();
  const bool had_keep_follow = attack_keep_follow_;
  const bool had_swing = attack_swing_active_;

  attack_swing_active_ = false;
  attack_swing_target_guid_ = 0;
  attack_keep_follow_ = false;
  StopAttackFollow();

  if (was_active || had_keep_follow || had_swing) {
    SpellC_ResetMeleeAttackCastFailureReason();
    if (session_ != nullptr) {
      session_->spells().SetAutoAttackSpellId(0);
    }
    NotifyAttackStateChanged();
  }

  if (send_packet && was_active) {
    SendAttackStopRequest();
  }
}

void TargetingSystem::SendAttackStopRequest() {
  if (session_ == nullptr) {
    return;
  }
  SendAttackStop();
  attack_stop_pending_ = true;
}

void TargetingSystem::RetargetAttackIfNeeded(std::uint64_t guid,
                                             bool should_reengage,
                                             bool keep_follow) {
  if (!should_reengage || guid == 0) return;

  if (session_ == nullptr) return;

  if (cvars_.GetCVarBool("stopAutoAttackOnTargetChange")) {
    return;
  }

  auto& spell_client = session_->spells();
  if (spell_client.IsCasting() || spell_client.IsChanneling()) return;

  StartAttack(guid, keep_follow, true);
}

void TargetingSystem::SendSetSelection(uint64_t guid) {
  if (session_ != nullptr) {
    session_->Send(net::wotlk::PacketSender::BuildSetSelection(guid));
  }
}

void TargetingSystem::SendAttackSwing(std::uint64_t guid) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_ATTACKSWING);
  pkt.AppendU64(guid);
  if (session_ != nullptr) {
    session_->Send(pkt);
  }
}

void TargetingSystem::SendAttackStop() {
  if (session_ != nullptr) {
    session_->Send(net::wotlk::WorldPacket(net::wotlk::Opcode::CMSG_ATTACKSTOP));
  }
}

void TargetingSystem::DisplaySwingErrorIfDue() {
  if (session_ == nullptr) return;

  AttackSwingError error{};
  if (!session_->combat().TryConsumeSwingErrorForDisplay(
          session_->CurrentClientTimeMs(), error)) {
    return;
  }

  switch (error) {
    case AttackSwingError::kBadFacing:
      ui::game::DisplaySystemMessage(kAttackSwingBadFacingMessageId);
      break;
    case AttackSwingError::kNotInRange:
      ui::game::DisplaySystemMessage(kAttackSwingNotInRangeMessageId);
      break;
    default:
      break;
  }
}

bool TargetingSystem::Unproject(float screen_x, float screen_y,
                                float screen_w, float screen_h,
                                const float* view_mtx, const float* proj_mtx,
                                float& ray_ox, float& ray_oy, float& ray_oz,
                                float& ray_dx, float& ray_dy, float& ray_dz) {
  if (screen_w <= 0.0f || screen_h <= 0.0f) return false;

  const float ndc_x = (2.0f * screen_x / screen_w) - 1.0f;
  const float ndc_y = 1.0f - (2.0f * screen_y / screen_h);

  float vp[16];

  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      vp[r * 4 + c] = 0.0f;
      for (int k = 0; k < 4; ++k) {
        vp[r * 4 + c] += view_mtx[r * 4 + k] * proj_mtx[k * 4 + c];
      }
    }
  }

  float inv[16];
  float aug[16][8];
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      aug[r][c] = vp[r * 4 + c];
      aug[r][c + 4] = (r == c) ? 1.0f : 0.0f;
    }
  }

  for (int col = 0; col < 4; ++col) {

    int pivot = col;
    float max_val = std::fabs(aug[col][col]);
    for (int row = col + 1; row < 4; ++row) {
      if (std::fabs(aug[row][col]) > max_val) {
        max_val = std::fabs(aug[row][col]);
        pivot = row;
      }
    }
    if (max_val < 1e-8f) return false;

    if (pivot != col) {
      for (int j = 0; j < 8; ++j) {
        std::swap(aug[col][j], aug[pivot][j]);
      }
    }

    const float scale = 1.0f / aug[col][col];
    for (int j = 0; j < 8; ++j) {
      aug[col][j] *= scale;
    }

    for (int row = 0; row < 4; ++row) {
      if (row == col) continue;
      const float factor = aug[row][col];
      for (int j = 0; j < 8; ++j) {
        aug[row][j] -= factor * aug[col][j];
      }
    }
  }

  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      inv[r * 4 + c] = aug[r][c + 4];
    }
  }

  auto unproj = [&](float nx, float ny, float nz, float& wx, float& wy, float& wz) {
    const float x = inv[0] * nx + inv[1] * ny + inv[2] * nz + inv[3];
    const float y = inv[4] * nx + inv[5] * ny + inv[6] * nz + inv[7];
    const float z = inv[8] * nx + inv[9] * ny + inv[10] * nz + inv[11];
    const float w = inv[12] * nx + inv[13] * ny + inv[14] * nz + inv[15];
    if (std::fabs(w) < 1e-8f) return false;
    wx = x / w;
    wy = y / w;
    wz = z / w;
    return true;
  };

  float near_x, near_y, near_z;
  float far_x, far_y, far_z;
  if (!unproj(ndc_x, ndc_y, -1.0f, near_x, near_y, near_z)) return false;
  if (!unproj(ndc_x, ndc_y, 1.0f, far_x, far_y, far_z)) return false;

  ray_ox = near_x;
  ray_oy = near_y;
  ray_oz = near_z;

  float dx = far_x - near_x;
  float dy = far_y - near_y;
  float dz = far_z - near_z;
  const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (len < 1e-8f) return false;

  ray_dx = dx / len;
  ray_dy = dy / len;
  ray_dz = dz / len;
  return true;
}

float TargetingSystem::IntersectSphere(float ox, float oy, float oz,
                                       float dx, float dy, float dz,
                                       float cx, float cy, float cz,
                                       float radius) {
  const float ex = ox - cx;
  const float ey = oy - cy;
  const float ez = oz - cz;

  const float a = dx * dx + dy * dy + dz * dz;
  const float b = 2.0f * (ex * dx + ey * dy + ez * dz);
  const float c = ex * ex + ey * ey + ez * ez - radius * radius;

  openwow::math::quadratic_roots::OrderedRoots roots;
  if (!openwow::math::quadratic_roots::SolveOrderedStable(a, b, c, &roots)) {
    return -1.0f;
  }

  if (roots.low >= 0.0f) return roots.low;
  if (roots.high >= 0.0f) return roots.high;
  return -1.0f;
}

}

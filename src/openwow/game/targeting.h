#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/object_manager.h"
#include "openwow/runtime/time/game_clock.h"
#include <cstdint>
#include <functional>
#include <vector>

#include "openwow/game/world_session_fwd.h"

namespace openwow::render {
class WorldFrame;
}

namespace openwow::ui::game {
class CVarSystem;
}

namespace openwow::game {

class CGUnit_C;
class CommentatorState;
class SpellbookSystem;
class TutorialSystem;

using TargetEventFn = std::function<void()>;

enum class TargetFilter : std::uint8_t {
  kAny = 0,
  kEnemy = 1,
  kEnemyPlayer = 2,
  kFriend = 3,
  kFriendPlayer = 4,
  kParty = 5,
  kRaid = 6,
};

enum class AttackStartResult : std::uint8_t {
  kStarted,
  kNoAction,
  kInvalidTarget,
  kStunned,
  kPacified,
  kMounted,
  kFleeing,
  kConfused,
  kCharmed,
  kChanneling,
  kDead,
  kClientLockedOut,
  kRangeRejected,
};

struct AttackStartOutcome {
  AttackStartResult result = AttackStartResult::kNoAction;

  std::uint32_t blocking_mechanic = 0;
};

[[nodiscard]] bool MatchesTargetFilterAction(const CGUnit_C& player,
                                             const CGUnit_C& target,
                                             TargetFilter filter);

[[nodiscard]] std::uint32_t ResolveSpellTargetCreatureTypeId(
    const WorldSession* session, const CGUnit_C& target);

class TargetingSystem {
 public:
  TargetingSystem(openwow::render::WorldFrame& world_frame,
                  const CommentatorState& commentator,
                  TutorialSystem& tutorials,
                  const SpellbookSystem& spellbook,
                  const openwow::ui::game::CVarSystem& cvars) noexcept
      : world_frame_(world_frame),
        commentator_(commentator),
        tutorials_(tutorials),
        spellbook_(spellbook),
        cvars_(cvars),

        attack_target_change_time_ms_(core::GameClock::GetTickCount32() -
                                      kTargetAcquireFlashDurationMs) {}
  ~TargetingSystem() = default;

  void Initialize(WorldSession* session);

  void SetTargetChangedCallback(TargetEventFn fn) {
    target_changed_fn_ = std::move(fn);
  }
  void SetFocusChangedCallback(TargetEventFn fn) {
    focus_changed_fn_ = std::move(fn);
  }
  void SetAttackStateChangedCallback(TargetEventFn fn) {
    attack_state_changed_fn_ = std::move(fn);
  }
  void SetAutoFollowChangedCallback(std::function<void(bool)> fn) {
    auto_follow_changed_fn_ = std::move(fn);
  }

  void TabTarget(bool reverse = false, TargetFilter filter = TargetFilter::kEnemy);

  void TargetNearest(TargetFilter filter, bool reverse = false);

  void TargetDirection(float facing_radians, float cone_radians,
                       TargetFilter filter);
  void FinishDirectionTarget();

  void TargetLastTarget();
  void TargetLastEnemy();
  void TargetLastFriend();

  void SetTarget(uint64_t guid);

  void SetTargetIfNone(uint64_t guid);

  void ClearTarget(uint64_t expected_guid = 0, bool send_packet = true);

  void InvalidateTrackedGuidReferences(std::uint64_t guid);

  void SetFocus(uint64_t guid);
  void ClearFocus();

  void AssistUnit(uint64_t guid);

  void StartFollow(std::uint64_t guid);
  [[nodiscard]] bool ValidateFollowTarget(std::uint64_t guid) const;

  void StopFollow();

  AttackStartOutcome StartAttack(std::uint64_t guid = 0, bool keep_follow = false,
                                 bool suppress_range_error = false,
                                 std::uint32_t spell_id = 0);

  [[nodiscard]] AttackStartOutcome ValidateAttackStart() const;

  void StopAttack(bool send_packet = true);

  void HandleServerAttackerStateUpdate(std::uint64_t attacker_guid,
                                       std::uint64_t victim_guid);

  void HandleServerAttackStop(std::uint64_t attacker_guid,
                              std::uint64_t victim_guid);

  void HandleServerSpellStart(std::uint64_t caster_guid, bool target_is_unit,
                              std::uint64_t target_guid);

  void ResetForWorldLeave();

  void Update(float dt_seconds);
  [[nodiscard]] bool InteractWith(std::uint64_t guid);

  [[nodiscard]] uint64_t target_guid() const { return target_guid_; }
  [[nodiscard]] uint64_t focus_guid() const;
  [[nodiscard]] uint64_t last_target_guid() const { return last_target_guid_; }
  [[nodiscard]] uint64_t last_enemy_guid() const { return last_enemy_guid_; }
  [[nodiscard]] uint64_t last_friend_guid() const { return last_friend_guid_; }
  [[nodiscard]] uint64_t direction_target_guid() const {
    return direction_target_guid_;
  }
  [[nodiscard]] bool HasTarget() const { return target_guid_ != 0; }
  [[nodiscard]] bool HasFocus() const;
  [[nodiscard]] bool IsAttackActive() const;
  [[nodiscard]] bool IsAttackSwingActive() const { return attack_swing_active_; }

  [[nodiscard]] std::uint32_t attack_target_change_time_ms() const {
    return attack_target_change_time_ms_;
  }

  static constexpr std::uint32_t kTargetAcquireFlashDurationMs = 500u;
  [[nodiscard]] bool IsAttackFollowing() const { return attack_follow_active_; }
  [[nodiscard]] bool HasMeleeAttackState() const;
  [[nodiscard]] bool IsFollowing() const { return follow_active_; }
  [[nodiscard]] std::uint64_t follow_guid() const { return follow_guid_; }

  bool IsHostile(const WorldObject& unit) const;
  bool IsFriendly(const WorldObject& unit) const;

 private:

  void SendSetSelection(uint64_t guid);

  bool IsSelectable(const WorldObject& unit) const;

  static bool Unproject(float screen_x, float screen_y,
                        float screen_w, float screen_h,
                        const float* view_mtx, const float* proj_mtx,
                        float& ray_ox, float& ray_oy, float& ray_oz,
                        float& ray_dx, float& ray_dy, float& ray_dz);

  static float IntersectSphere(float ox, float oy, float oz,
                               float dx, float dy, float dz,
                               float cx, float cy, float cz, float radius);

  [[nodiscard]] bool CanShowTutorialPopup(std::uint32_t tutorial_index) const;
  [[nodiscard]] const ObjectManager* Objects() const;
  void TriggerPlayerTargetTutorialIfNeeded(const CGUnit_C& player,
                                           const CGUnit_C& target);

  [[nodiscard]] bool IsInAttackRange(const CGUnit_C& unit) const;

  [[nodiscard]] bool IsInAttackRange(const CGUnit_C& unit,
                                     bool allow_auto_ranged_substitution) const;
  [[nodiscard]] std::uint32_t GetAutoRangedCombatSpellId() const;

  void CloseTargetLootWindowIfNeeded(std::uint64_t guid);

  [[nodiscard]] bool HasWorldMapContext() const;
  void InvalidateTabTargetBuildTimestamp();
  void CancelAutoRepeatSpellIfActive();
  void ClearTargetInternal(uint64_t expected_guid, bool send_packet);
  void NotifyAttackStateChanged();
  void NotifyAutoFollowChanged();
  [[nodiscard]] bool IsInFollowRange(const WorldObject& unit) const;
  void StartAttackFollow();
  void StopAttackFollow();
  void DriveAutoFollowTowards(const WorldObject& target, bool move_forward);
  void StopOwnedAutoFollowMovement();
  void StopAttackInternal(bool send_packet);
  void SendAttackStopRequest();
  void RetargetAttackIfNeeded(std::uint64_t guid, bool should_reengage,
                              bool keep_follow);
  void SendAttackSwing(std::uint64_t guid);
  void SendAttackStop();
  void DisplaySwingErrorIfDue();

  WorldSession* session_ = nullptr;
  TargetEventFn target_changed_fn_;
  TargetEventFn focus_changed_fn_;
  TargetEventFn attack_state_changed_fn_;
  std::function<void(bool)> auto_follow_changed_fn_;

  uint64_t target_guid_ = 0;

  uint64_t last_target_guid_ = 0;
  uint64_t last_enemy_guid_ = 0;
  uint64_t last_friend_guid_ = 0;
  uint64_t direction_target_guid_ = 0;

  struct TabTargetCandidate {
    uint64_t guid;
    float distance_sq;
    bool in_front_arc;
  };
  int32_t tab_index_ = -1;

  std::vector<TabTargetCandidate> tab_candidates_;
  uint64_t tab_list_built_ms_ = 0;
  uint64_t tab_last_selection_ms_ = 0;
  TargetFilter tab_cache_filter_ = TargetFilter::kEnemy;
  static constexpr uint64_t kTabCacheTimeoutMs = 3000;

  openwow::render::WorldFrame& world_frame_;
  const CommentatorState& commentator_;
  TutorialSystem& tutorials_;
  const SpellbookSystem& spellbook_;
  const openwow::ui::game::CVarSystem& cvars_;

  bool attack_swing_active_ = false;

  std::uint64_t attack_swing_target_guid_ = 0;

  std::uint64_t attack_target_change_seen_guid_ = 0;

  std::uint32_t attack_target_change_time_ms_ = 0;

  bool attack_stop_pending_ = false;

  bool attack_follow_active_ = false;
  bool attack_keep_follow_ = false;
  bool follow_active_ = false;
  std::uint64_t follow_guid_ = 0;
  bool owns_auto_follow_forward_ = false;

  static constexpr float kClickTargetRange = 100.0f;
  static constexpr float kDefaultBoundRadius = 2.0f;
  static constexpr float kFollowRangeSquared = 9.0f;

  static constexpr float kDirectionTargetRangeSq = 1681.0f;
  static constexpr float kDirectionMinCone = 0.39269909f;

  friend struct TargetingSystemTestAccess;
};

}

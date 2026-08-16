#pragma once

#include "openwow/game/object_guid.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <vector>

namespace openwow::data::dbc {
template <typename Entry> class DbcStore;
struct SpellMissileMotionEntry;
}

namespace openwow::game {
namespace simple_script {
struct SimpleScript;
}

struct MissileVec3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;

  [[nodiscard]] float DistanceTo(const MissileVec3 &other) const {
    const float dx = x - other.x;
    const float dy = y - other.y;
    const float dz = z - other.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  [[nodiscard]] float HorizontalDistanceTo(const MissileVec3 &other) const {
    const float dx = x - other.x;
    const float dy = y - other.y;
    return std::sqrt(dx * dx + dy * dy);
  }

  [[nodiscard]] MissileVec3 Lerp(const MissileVec3 &other, float t) const {
    return {
        x + (other.x - x) * t,
        y + (other.y - y) * t,
        z + (other.z - z) * t,
    };
  }

  MissileVec3 &operator+=(const MissileVec3 &other) noexcept {
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
  }
};

struct MissileTrackingSpring {
  bool initialized = false;
  MissileVec3 previous_target{};
  MissileVec3 offset{};
  MissileVec3 velocity{};

  void Reset() noexcept;
  void Apply(const MissileVec3 &target_position, float elapsed_seconds,
             MissileVec3 &position) noexcept;
};

namespace missile_math {

enum class PitchSolveResult : std::uint8_t {
  kFailed = 0,
  kDegenerate = 1,
  kSolved = 2,
};

struct PitchSolveSolution {
  PitchSolveResult result = PitchSolveResult::kFailed;
  float required_speed = 0.0f;
  float travel_time = 0.0f;
  MissileVec3 initial_velocity{};
};

[[nodiscard]] float ComputeBallisticTravelTime(float launch_pitch, float speed,
                                               const MissileVec3 &delta, float gravity);
[[nodiscard]] PitchSolveSolution
SolveBallisticLaunchAtPitch(float launch_pitch, const MissileVec3 &delta, float gravity);

}

enum class MissileMotionType : std::uint8_t {
  kLinear = 0,
  kParabolicArc = 1,
  kHoming = 2,
};

enum class MissilePhase : std::uint8_t {
  kLaunching = 0,
  kTraveling = 1,
  kImpacting = 2,
  kExpired = 3,
};

enum class MissileImpactResult : std::uint8_t {
  kNone = 0,
  kMiss = 1,
  kResist = 2,
  kDodge = 3,
  kParry = 4,
  kBlock = 5,
  kEvade = 6,
  kImmune = 7,
  kImmune2 = 8,
  kDeflect = 9,
  kAbsorb = 10,
  kReflect = 11,
};

enum class MissileCreateImpactBehavior : std::uint8_t {
  kNone = 0,
  kCleanupOnly,
  kBlockReaction,
  kImmuneReaction,
  kBounce,
  kReflect,
};

struct MissileImpactResolutionState {
  bool has_indexed_results = false;
  bool force_single_impact_result = false;
  bool suppress_scalar_create_impact_resolution = false;
  bool reflect_uses_bounce_behavior = false;
  std::size_t current_target_index = 0;

  MissileImpactResult impact_result = MissileImpactResult::kNone;
  MissileImpactResult reflect_result = MissileImpactResult::kNone;
  std::vector<MissileImpactResult> indexed_impact_results;
  std::vector<MissileImpactResult> indexed_reflect_results;
  MissileCreateImpactBehavior create_impact_behavior = MissileCreateImpactBehavior::kNone;

  [[nodiscard]] MissileImpactResult GetImpactResult() const;
  [[nodiscard]] MissileImpactResult GetReflectResult() const;

  void SetImpactResult(MissileImpactResult result);
  void SetReflectResult(MissileImpactResult result);
  void ResolveCreateImpactBehavior();
};

struct MissileVisualInfo {
  std::uint32_t spell_visual_id = 0;
  std::uint32_t missile_kit_id = 0;
  std::uint32_t model_file_id = 0;
  std::string model_path;
  std::uint32_t trail_visual_id = 0;
};

struct MissileInfo {
  std::uint32_t missile_id = 0;
  std::uint32_t spell_id = 0;
  std::uint8_t cast_count = 0;

  ObjectGuid caster_guid;
  ObjectGuid target_guid;

  MissileVec3 source;
  MissileVec3 target;
  MissileVec3 current;
  MissileVec3 last_reported_position;
  MissileVec3 initial_velocity;
  ObjectGuid position_parent_guid;

  float speed = 24.0f;
  MissileMotionType motion = MissileMotionType::kParabolicArc;
  MissilePhase phase = MissilePhase::kLaunching;

  float arc_height_ratio = 0.15f;
  float arc_peak_height = 0.0f;
  float launch_pitch = 0.0f;
  float gravity = 0.0f;
  bool use_ballistic_solver = false;
  bool has_initial_velocity = false;

  float progress = 0.0f;
  float travel_time = 0.0f;
  float elapsed = 0.0f;

  std::uint32_t impact_delay_deadline_tick_ms = 0;
  std::uint32_t launch_tick_ms = 0;
  std::uint32_t ballistic_deadline_tick_ms = 0;
  std::uint32_t ballistic_adjust_start_tick_ms = 0;
  std::uint32_t ballistic_adjust_duration_ms = 0;
  bool ballistic_timing_adjusted = false;

  bool wait_for_impact_visual = false;
  bool impact_visual_complete = false;

  MissileVisualInfo visual;

  std::uint32_t motion_id = 0;
  MissileVec3 random_motion{};
  MissileVec3 script_translation{};
  MissileVec3 model_rotation_radians{};
  float model_scale = 1.0f;
  float base_speed = 0.0f;

  std::uint32_t salvo_index = 0;
  std::uint32_t salvo_count = 1;

  bool homing_target_valid = true;

  bool has_collision_position = false;
  MissileVec3 collision_position;

  MissileImpactResolutionState impact_resolution;

  [[nodiscard]] MissileImpactResult GetImpactResult() const;
  [[nodiscard]] MissileImpactResult GetReflectResult() const;

  void SetImpactResult(MissileImpactResult result);
  void SetReflectResult(MissileImpactResult result);
  void ResolveCreateImpactBehavior();
};

struct SpellMissileMotionInputs {
  double progress = 0.0;
  double time = 0.0;
  double missile_index = 0.0;
  double missile_count = 0.0;
  double distance_to_fire_position = 0.0;
  double distance_to_impact_position = 0.0;
  double start_distance = 0.0;
  double total_distance = 0.0;
  double random_value_1 = 0.0;
  double random_value_2 = 0.0;
  double random_value_3 = 0.0;
  double spell_id = 0.0;
};

struct SpellMissileMotionOutputs {
  double trans_angle = 0.0;
  double trans_magnitude = 0.0;
  double trans_right = 0.0;
  double trans_front = 0.0;
  double trans_up = 0.0;
  double model_yaw = 0.0;
  double model_pitch = 0.0;
  double model_roll = 0.0;
  double speed_absolute = 0.0;
  double speed_scalar = 0.0;
  double speed_offset = 0.0;
  double model_scale = 0.0;
};

struct SpellMissileMotionSpeedOverride {
  bool has_override = false;
  float speed = 0.0f;
};

[[nodiscard]] SpellMissileMotionSpeedOverride
ResolveSpellMissileMotionSpeed(float base_speed, const SpellMissileMotionOutputs &outputs);

class SpellMissileMotionRegistry {
public:
  static SpellMissileMotionRegistry &Get();

  void
  BindStore(const openwow::data::dbc::DbcStore<openwow::data::dbc::SpellMissileMotionEntry> *store);
  void BindScript(simple_script::SimpleScript* script) noexcept {
    script_ = script;
  }
  void Shutdown();

  [[nodiscard]] int EnsureFunctionHandle(std::uint32_t motion_id) const;
  [[nodiscard]] std::uint32_t ResolveInstanceCount(std::uint32_t motion_id) const;
  [[nodiscard]] bool Evaluate(std::uint32_t motion_id, const SpellMissileMotionInputs &inputs,
                              SpellMissileMotionOutputs &outputs) const;

private:
  SpellMissileMotionRegistry() = default;

  [[nodiscard]] const openwow::data::dbc::SpellMissileMotionEntry *
  LookupEntry(std::uint32_t motion_id) const;

  const openwow::data::dbc::DbcStore<openwow::data::dbc::SpellMissileMotionEntry> *store_{nullptr};
  simple_script::SimpleScript* script_{nullptr};
};

class MissileManager {
public:
  static MissileManager &Get();

  [[nodiscard]] std::uint32_t Launch(MissileInfo info);

  void Update(float dt);

  void Remove(std::uint32_t missile_id);

  void RemoveForCaster(const ObjectGuid &caster);

  void ClearAll();

  [[nodiscard]] std::optional<MissileInfo> GetMissile(std::uint32_t missile_id) const;

  [[nodiscard]] MissileInfo *FindMissile(std::uint32_t missile_id);
  [[nodiscard]] const MissileInfo *FindMissile(std::uint32_t missile_id) const;

  [[nodiscard]] std::vector<MissileInfo> GetActiveMissiles() const;

  [[nodiscard]] std::vector<MissileInfo> GetPendingImpacts() const;

  [[nodiscard]] std::vector<MissileInfo> GetMissilesForCaster(const ObjectGuid &caster) const;

  [[nodiscard]] std::vector<MissileInfo> GetMissilesForTarget(const ObjectGuid &target) const;

  [[nodiscard]] std::uint32_t GetActiveCount() const;
  [[nodiscard]] std::uint32_t GetPendingImpactCount() const;

  [[nodiscard]] bool CompletePendingImpact(std::uint32_t missile_id);

  [[nodiscard]] std::vector<std::uint32_t> DrainImpacted();

  bool SetCollisionPosition(const ObjectGuid &caster, std::uint8_t cast_count,
                            const MissileVec3 &position);

  [[nodiscard]] std::size_t SetImpactDelay(const ObjectGuid &caster,
                                           std::uint8_t cast_count,
                                           std::uint32_t spell_id,
                                           std::uint32_t current_tick_ms);

  [[nodiscard]] std::size_t RescaleBallisticDuration(
      std::uint8_t cast_count, std::uint32_t requested_duration_ms,
      std::uint32_t current_tick_ms);

  using ImpactCallback = std::function<void(const MissileInfo &)>;

  void SetOnImpact(ImpactCallback cb);

  void Reset();

private:
  MissileManager() = default;

  static float ComputeTravelTime(const MissileInfo &m);

  static void UpdateLinear(MissileInfo &m, float t);

  static void UpdateParabolicArc(MissileInfo &m, float t);

  static void UpdateHoming(MissileInfo &m, float t);

  using MissileList = std::list<MissileInfo>;

  struct LocatedMissile {
    MissileList *owner = nullptr;
    MissileList::iterator iterator{};
  };
  struct ConstLocatedMissile {
    const MissileList *owner = nullptr;
    MissileList::const_iterator iterator{};
  };

  [[nodiscard]] LocatedMissile FindLocated(std::uint32_t missile_id);
  [[nodiscard]] ConstLocatedMissile FindLocated(std::uint32_t missile_id) const;
  [[nodiscard]] std::uint32_t AllocateRuntimeId();

  MissileList active_missiles_;
  MissileList pending_impacts_;
  std::vector<std::uint32_t> impacted_;
  ImpactCallback on_impact_;
  std::uint32_t next_id_ = 1;
};

void CMissile_RegisterTypeAndLifecycleCallback();

void CMissile_UnregisterTypeAndLifecycleCallback();

int CMissile_LifecycleCallback(int phase);

void CMissile_ReleaseAll();

[[nodiscard]] std::uint32_t* CMissile_GetTypeHandle();

[[nodiscard]] bool CMissile_C_SendUpdateProjectilePositionAndImpact(
    MissileInfo& missile);

struct MissileCreateState {
  std::int32_t miss_anim_id = -1;
  MissileVec3 random_motion{};
  float total_distance = 0.0f;
  std::uint32_t flight_start_tick_ms = 0;
  std::uint32_t travel_time_ms = 0;
  std::uint32_t emote_delay_tick_ms = 0;
  std::uint32_t visual_create_tick_ms = 0;
  std::uint32_t launch_sound_kit_id = 0;
  std::uint32_t delay_offset_ms = 0;
  bool is_clone = false;
  bool source_unit_resolved = false;
  bool no_target_guid = false;
};

[[nodiscard]] std::int32_t CMissile_C_ResolveMissAnimSequence(
    const std::function<bool(std::uint32_t)>& has_anim_record,
    std::int32_t requested);

void CMissile_C_CreateInner(
    MissileInfo& info,
    const MissileVec3& source_pos,
    MissileCreateState& state,
    std::uint32_t current_tick_ms,
    const std::function<MissileVec3()>& target_pos_fn,
    const std::function<float()>& rng_fn,
    const std::function<bool(std::uint32_t)>* has_anim_fn = nullptr);

}

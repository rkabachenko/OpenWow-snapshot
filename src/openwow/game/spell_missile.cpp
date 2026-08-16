
#include "openwow/game/spell_missile.h"

#include "openwow/core/cobject_heap.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_store.h"
#include "openwow/game/simple_script.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <new>
#include <span>

namespace openwow::game {

namespace {

constexpr float kBallisticSolveEpsilon = 0.000099999997f;
constexpr float kBallisticSolveMinTimeSquared = 0.0099999998f;

constexpr std::array<const char *, 12> kSpellMissileMotionInputNames = {
    "progress",
    "time",
    "missileIndex",
    "missileCount",
    "distanceToFirePos",
    "distanceToImpactPos",
    "startDistance",
    "totalDistance",
    "rand1",
    "rand2",
    "rand3",
    "spellID",
};

constexpr std::array<const char *, 12> kSpellMissileMotionOutputNames = {
    "transAngle", "transMag",  "transRight", "transFront",  "transUp",     "modelYaw",
    "modelPitch", "modelRoll", "speedAbs",   "speedScalar", "speedOffset", "modelScale",
};

[[nodiscard]] std::array<double, 12> ToNumericInputArray(const SpellMissileMotionInputs &inputs) {
  return {
      inputs.progress,
      inputs.time,
      inputs.missile_index,
      inputs.missile_count,
      inputs.distance_to_fire_position,
      inputs.distance_to_impact_position,
      inputs.start_distance,
      inputs.total_distance,
      inputs.random_value_1,
      inputs.random_value_2,
      inputs.random_value_3,
      inputs.spell_id,
  };
}

void AssignNumericOutputs(const std::array<double, 12> &values,
                          SpellMissileMotionOutputs &outputs) {
  outputs.trans_angle = values[0];
  outputs.trans_magnitude = values[1];
  outputs.trans_right = values[2];
  outputs.trans_front = values[3];
  outputs.trans_up = values[4];
  outputs.model_yaw = values[5];
  outputs.model_pitch = values[6];
  outputs.model_roll = values[7];
  outputs.speed_absolute = values[8];
  outputs.speed_scalar = values[9];
  outputs.speed_offset = values[10];
  outputs.model_scale = values[11];
}

void SanitizeNumericOutputs(std::array<double, 12> &values) {
  for (double &value : values) {
    if (!std::isfinite(value)) {
      value = 0.0;
    }
  }
}

[[nodiscard]] bool UsesIndexedImpactResultStorage(const MissileImpactResolutionState &state) {
  return state.has_indexed_results && !state.force_single_impact_result;
}

[[nodiscard]] MissileVec3 ComputeDelta(const MissileInfo &missile) {
  return {
      missile.target.x - missile.source.x,
      missile.target.y - missile.source.y,
      missile.target.z - missile.source.z,
  };
}

}

void MissileTrackingSpring::Reset() noexcept {
  initialized = false;
  previous_target = {};
  offset = {};
  velocity = {};
}

void MissileTrackingSpring::Apply(const MissileVec3 &target_position,
                                  const float elapsed_seconds,
                                  MissileVec3 &position) noexcept {
  if (!initialized) {
    initialized = true;
    previous_target = target_position;
    offset = {};
    velocity = {};
    return;
  }

  const float scaled_time = elapsed_seconds * 5.0f;
  const float scaled_time_squared = scaled_time * scaled_time;
  const float decay =
      1.0f / (scaled_time + 1.0f + scaled_time_squared * 0.48f +
              scaled_time_squared * scaled_time * 0.235f);

  const auto apply_axis = [elapsed_seconds, decay](const float target,
                                                    const float previous,
                                                    float &axis_offset,
                                                    float &axis_velocity) {
    const float target_delta = target - previous;
    const float displacement = axis_offset - target_delta;
    const float step = (displacement * 5.0f + axis_velocity) * elapsed_seconds;
    axis_offset = target_delta + (displacement + step) * decay;
    axis_velocity = (axis_velocity - step * 5.0f) * decay;
  };

  apply_axis(target_position.x, previous_target.x, offset.x, velocity.x);
  apply_axis(target_position.y, previous_target.y, offset.y, velocity.y);
  apply_axis(target_position.z, previous_target.z, offset.z, velocity.z);
  position += offset;
  previous_target = target_position;
}

float missile_math::ComputeBallisticTravelTime(const float launch_pitch, const float speed,
                                               const MissileVec3 &delta, const float gravity) {
  const double gravity_magnitude = gravity;
  const bool has_gravity = std::fabs(gravity_magnitude) > kBallisticSolveEpsilon;
  const double horizontal_speed = std::cos(static_cast<double>(launch_pitch)) * speed;
  const double horizontal_distance =
      std::sqrt(static_cast<double>(delta.x) * delta.x + static_cast<double>(delta.y) * delta.y);

  if (horizontal_speed > kBallisticSolveEpsilon) {
    return static_cast<float>(horizontal_distance / horizontal_speed);
  }

  if (speed <= kBallisticSolveEpsilon) {
    if (!has_gravity) {
      return -1.0f;
    }
  } else if (!has_gravity) {
    return static_cast<float>(std::fabs(static_cast<double>(delta.z)) / speed);
  }

  const double vertical_speed = speed * std::sin(static_cast<double>(launch_pitch));
  const double discriminant =
      (vertical_speed * vertical_speed) - (2.0 * static_cast<double>(delta.z) * gravity_magnitude);
  if (discriminant < 0.0) {
    return -1.0f;
  }

  const double root = std::sqrt(discriminant);
  const double inv_negative_gravity = -1.0 / gravity_magnitude;
  const double first_time = (root - vertical_speed) * inv_negative_gravity;
  const double second_time = (-vertical_speed - root) * inv_negative_gravity;
  if (first_time >= 0.0 && (second_time < 0.0 || first_time < second_time)) {
    return static_cast<float>(first_time);
  }

  return static_cast<float>(second_time);
}

missile_math::PitchSolveSolution missile_math::SolveBallisticLaunchAtPitch(const float launch_pitch,
                                                                           const MissileVec3 &delta,
                                                                           const float gravity) {
  const double cos_pitch = std::cos(static_cast<double>(launch_pitch));
  const double sin_pitch = std::sin(static_cast<double>(launch_pitch));
  const double horizontal_distance =
      std::sqrt(static_cast<double>(delta.x) * delta.x + static_cast<double>(delta.y) * delta.y);

  if (std::fabs(cos_pitch) < kBallisticSolveEpsilon ||
      std::fabs(horizontal_distance) < kBallisticSolveEpsilon ||
      std::fabs(static_cast<double>(gravity)) < kBallisticSolveEpsilon) {
    return {.result = missile_math::PitchSolveResult::kDegenerate};
  }

  const double time_term =
      (((1.0 / cos_pitch) * horizontal_distance * sin_pitch) - delta.z) / gravity;
  const double doubled_time_term = time_term + time_term;
  if (doubled_time_term <= kBallisticSolveMinTimeSquared) {
    return {.result = missile_math::PitchSolveResult::kFailed};
  }

  const double travel_time = std::sqrt(doubled_time_term);
  const double required_speed = (horizontal_distance / travel_time) / cos_pitch;

  missile_math::PitchSolveSolution solution;
  solution.result = missile_math::PitchSolveResult::kSolved;
  solution.required_speed = static_cast<float>(required_speed);
  solution.travel_time = static_cast<float>(travel_time);

  const double inverse_horizontal_distance = 1.0 / horizontal_distance;
  solution.initial_velocity.x =
      static_cast<float>(required_speed * delta.x * inverse_horizontal_distance * cos_pitch);
  solution.initial_velocity.y =
      static_cast<float>(required_speed * delta.y * inverse_horizontal_distance * cos_pitch);
  solution.initial_velocity.z = static_cast<float>(sin_pitch * required_speed);
  return solution;
}

MissileImpactResult MissileImpactResolutionState::GetImpactResult() const {
  if (!has_indexed_results || force_single_impact_result) {
    return impact_result;
  }
  if (current_target_index >= indexed_impact_results.size()) {
    return MissileImpactResult::kNone;
  }
  return indexed_impact_results[current_target_index];
}

MissileImpactResult MissileImpactResolutionState::GetReflectResult() const {
  if (!has_indexed_results) {
    return reflect_result;
  }
  if (current_target_index >= indexed_reflect_results.size()) {
    return MissileImpactResult::kNone;
  }
  return indexed_reflect_results[current_target_index];
}

void MissileImpactResolutionState::SetImpactResult(const MissileImpactResult result) {
  if (!has_indexed_results || force_single_impact_result ||
      current_target_index >= indexed_impact_results.size()) {
    impact_result = result;
    return;
  }
  indexed_impact_results[current_target_index] = result;
}

void MissileImpactResolutionState::SetReflectResult(const MissileImpactResult result) {
  if (!has_indexed_results || current_target_index >= indexed_reflect_results.size()) {
    reflect_result = result;
    return;
  }
  indexed_reflect_results[current_target_index] = result;
}

void MissileImpactResolutionState::ResolveCreateImpactBehavior() {
  create_impact_behavior = MissileCreateImpactBehavior::kNone;

  if (!UsesIndexedImpactResultStorage(*this)) {
    if (suppress_scalar_create_impact_resolution) {
      return;
    }
  } else if (current_target_index < indexed_impact_results.size() &&
             indexed_impact_results[current_target_index] == MissileImpactResult::kNone) {
    return;
  }

  switch (GetImpactResult()) {
  case MissileImpactResult::kMiss:
  case MissileImpactResult::kDodge:
  case MissileImpactResult::kEvade:
    create_impact_behavior = MissileCreateImpactBehavior::kCleanupOnly;
    return;

  case MissileImpactResult::kResist:
  case MissileImpactResult::kImmune:
  case MissileImpactResult::kImmune2:
    create_impact_behavior = MissileCreateImpactBehavior::kImmuneReaction;
    return;

  case MissileImpactResult::kParry:
    create_impact_behavior = MissileCreateImpactBehavior::kBounce;
    SetImpactResult(MissileImpactResult::kDeflect);
    return;

  case MissileImpactResult::kBlock:
    create_impact_behavior = MissileCreateImpactBehavior::kBlockReaction;
    return;

  case MissileImpactResult::kReflect:
    if (reflect_uses_bounce_behavior) {
      reflect_uses_bounce_behavior = false;
      create_impact_behavior = MissileCreateImpactBehavior::kBounce;
    } else {
      create_impact_behavior = MissileCreateImpactBehavior::kReflect;
    }
    return;

  case MissileImpactResult::kNone:
  case MissileImpactResult::kDeflect:
  case MissileImpactResult::kAbsorb:
    create_impact_behavior = MissileCreateImpactBehavior::kBounce;

    impact_result = MissileImpactResult::kDeflect;
    return;
  }
}

MissileImpactResult MissileInfo::GetImpactResult() const {
  return impact_resolution.GetImpactResult();
}

MissileImpactResult MissileInfo::GetReflectResult() const {
  return impact_resolution.GetReflectResult();
}

void MissileInfo::SetImpactResult(const MissileImpactResult result) {
  impact_resolution.SetImpactResult(result);
}

void MissileInfo::SetReflectResult(const MissileImpactResult result) {
  impact_resolution.SetReflectResult(result);
}

void MissileInfo::ResolveCreateImpactBehavior() {
  impact_resolution.ResolveCreateImpactBehavior();
}

SpellMissileMotionSpeedOverride
ResolveSpellMissileMotionSpeed(const float base_speed, const SpellMissileMotionOutputs &outputs) {
  SpellMissileMotionSpeedOverride result;
  if (outputs.speed_absolute > 0.0) {
    result.has_override = true;
    result.speed = static_cast<float>(outputs.speed_absolute);
  }

  if (outputs.speed_scalar > 0.0) {
    result.has_override = true;
    result.speed = static_cast<float>(outputs.speed_scalar) * base_speed;
  }

  if (outputs.speed_offset != 0.0) {
    result.has_override = true;
    const float resolved_speed = base_speed + static_cast<float>(outputs.speed_offset);
    result.speed = resolved_speed > 0.0099999998f ? resolved_speed : 0.0099999998f;
  }

  return result;
}

SpellMissileMotionRegistry &SpellMissileMotionRegistry::Get() {
  static SpellMissileMotionRegistry instance;
  return instance;
}

void SpellMissileMotionRegistry::BindStore(
    const openwow::data::dbc::DbcStore<openwow::data::dbc::SpellMissileMotionEntry> *store) {
  store_ = store;
}

void SpellMissileMotionRegistry::Shutdown() {
  store_ = nullptr;
  script_ = nullptr;
}

const openwow::data::dbc::SpellMissileMotionEntry *
SpellMissileMotionRegistry::LookupEntry(const std::uint32_t motion_id) const {
  if (store_ == nullptr || motion_id == 0u) {
    return nullptr;
  }

  return store_->LookupEntry(motion_id);
}

int SpellMissileMotionRegistry::EnsureFunctionHandle(const std::uint32_t motion_id) const {
  const auto *const entry = LookupEntry(motion_id);
  if (entry == nullptr || entry->script_name.empty() || entry->script_body.empty()) {
    return -1;
  }

  if (script_ == nullptr) {
    return -1;
  }

  const simple_script::SimpleScriptNumericFunctionDescriptor descriptor{
      .chunk_name = entry->script_name.data(),
      .source = entry->script_body.data(),
      .numeric_input_names = std::span<const char *const>(kSpellMissileMotionInputNames),
      .numeric_output_names = std::span<const char *const>(kSpellMissileMotionOutputNames),
  };
  return simple_script::SimpleScript_EnsureNumericFunction(
      script_, motion_id, descriptor);
}

std::uint32_t
SpellMissileMotionRegistry::ResolveInstanceCount(const std::uint32_t motion_id) const {
  const auto *const entry = LookupEntry(motion_id);
  if (entry == nullptr || entry->instance_count < 1u) {
    return 1u;
  }

  return entry->instance_count;
}

bool SpellMissileMotionRegistry::Evaluate(const std::uint32_t motion_id,
                                          const SpellMissileMotionInputs &inputs,
                                          SpellMissileMotionOutputs &outputs) const {
  std::array<double, 12> numeric_outputs{};
  const int function_handle = EnsureFunctionHandle(motion_id);
  if (function_handle == -1) {
    outputs = {};
    return false;
  }

  if (script_ == nullptr) {
    outputs = {};
    return false;
  }

  const std::array<double, 12> numeric_inputs = ToNumericInputArray(inputs);
  if (!simple_script::SimpleScript_ExecuteNumericFunction(
          script_, static_cast<std::uint32_t>(function_handle),
          std::span<const double>(numeric_inputs), std::span<double>(numeric_outputs))) {
    outputs = {};
    return false;
  }

  SanitizeNumericOutputs(numeric_outputs);
  AssignNumericOutputs(numeric_outputs, outputs);
  return true;
}

MissileManager &MissileManager::Get() {
  static MissileManager instance;
  return instance;
}

float MissileManager::ComputeTravelTime(const MissileInfo &m) {
  if (m.motion == MissileMotionType::kParabolicArc && m.use_ballistic_solver) {
    return missile_math::ComputeBallisticTravelTime(m.launch_pitch, m.speed, ComputeDelta(m),
                                                    m.gravity);
  }

  const float dist = m.source.DistanceTo(m.target);
  if (m.speed <= 0.0f)
    return 0.0f;
  return dist / m.speed;
}

void MissileManager::UpdateLinear(MissileInfo &m, float t) {
  m.current = m.source.Lerp(m.target, t);
}

void MissileManager::UpdateParabolicArc(MissileInfo &m, float t) {
  if (m.use_ballistic_solver) {
    if (!m.has_initial_velocity) {
      m.current = m.source.Lerp(m.target, t);
      return;
    }

    const float ballistic_time = std::clamp(m.elapsed, 0.0f, m.travel_time);
    m.current.x = m.source.x + m.initial_velocity.x * ballistic_time;
    m.current.y = m.source.y + m.initial_velocity.y * ballistic_time;
    m.current.z = m.source.z + m.initial_velocity.z * ballistic_time -
                  (0.5f * m.gravity * ballistic_time * ballistic_time);
    return;
  }

  m.current = m.source.Lerp(m.target, t);

  const float arc_offset = 4.0f * m.arc_peak_height * t * (1.0f - t);
  m.current.z += arc_offset;
}

void MissileManager::UpdateHoming(MissileInfo &m, float t) {

  m.current = m.source.Lerp(m.target, t);
}

MissileManager::LocatedMissile
MissileManager::FindLocated(const std::uint32_t missile_id) {
  const auto matches = [missile_id](const MissileInfo &missile) {
    return missile.missile_id == missile_id;
  };
  auto active = std::find_if(active_missiles_.begin(), active_missiles_.end(), matches);
  if (active != active_missiles_.end()) {
    return {&active_missiles_, active};
  }
  auto pending = std::find_if(pending_impacts_.begin(), pending_impacts_.end(), matches);
  if (pending != pending_impacts_.end()) {
    return {&pending_impacts_, pending};
  }
  return {};
}

MissileManager::ConstLocatedMissile
MissileManager::FindLocated(const std::uint32_t missile_id) const {
  const auto matches = [missile_id](const MissileInfo &missile) {
    return missile.missile_id == missile_id;
  };
  const auto active =
      std::find_if(active_missiles_.cbegin(), active_missiles_.cend(), matches);
  if (active != active_missiles_.cend()) {
    return {&active_missiles_, active};
  }
  const auto pending =
      std::find_if(pending_impacts_.cbegin(), pending_impacts_.cend(), matches);
  if (pending != pending_impacts_.cend()) {
    return {&pending_impacts_, pending};
  }
  return {};
}

std::uint32_t MissileManager::AllocateRuntimeId() {

  for (;;) {
    const std::uint32_t candidate = next_id_++;
    if (candidate != 0 && FindLocated(candidate).owner == nullptr) {
      return candidate;
    }
  }
}

std::uint32_t MissileManager::Launch(MissileInfo info) {
  const std::uint32_t id = AllocateRuntimeId();
  info.missile_id = id;
  info.current = info.source;
  info.elapsed = 0.0f;
  info.progress = 0.0f;
  info.phase = MissilePhase::kLaunching;
  info.initial_velocity = {};
  info.has_initial_velocity = false;
  if (info.base_speed <= 0.0f) {
    info.base_speed = info.speed;
  }
  info.wait_for_impact_visual =
      info.wait_for_impact_visual || info.visual.model_file_id != 0 ||
      !info.visual.model_path.empty();
  info.ResolveCreateImpactBehavior();

  if (info.motion == MissileMotionType::kParabolicArc && info.use_ballistic_solver) {
    const auto solution = missile_math::SolveBallisticLaunchAtPitch(
        info.launch_pitch, ComputeDelta(info), info.gravity);
    if (solution.result == missile_math::PitchSolveResult::kSolved) {
      info.speed = solution.required_speed;
      info.travel_time = solution.travel_time;
      info.initial_velocity = solution.initial_velocity;
      info.has_initial_velocity = true;
    } else {
      info.travel_time = ComputeTravelTime(info);
    }
  } else {
    info.travel_time = ComputeTravelTime(info);
  }

  if (info.motion == MissileMotionType::kParabolicArc && !info.use_ballistic_solver) {
    const float horiz = info.source.HorizontalDistanceTo(info.target);
    info.arc_peak_height = horiz * info.arc_height_ratio;
  }

  if (info.travel_time < kBallisticSolveEpsilon) {
    info.phase = MissilePhase::kExpired;
  }

  if (info.use_ballistic_solver && info.ballistic_deadline_tick_ms == 0) {
    const auto duration_ms = static_cast<std::uint32_t>(
        std::max(0.0f, info.travel_time) * 1000.0f);
    const std::uint32_t deadline = info.launch_tick_ms + duration_ms;
    info.ballistic_deadline_tick_ms = deadline == 0 ? 1u : deadline;
  }

  active_missiles_.push_front(std::move(info));
  return id;
}

void MissileManager::Update(float dt) {
  for (auto it = active_missiles_.begin(); it != active_missiles_.end();) {
    MissileInfo &m = *it;
    if (m.phase == MissilePhase::kExpired) {
      it = active_missiles_.erase(it);
      continue;
    }

    if (m.phase == MissilePhase::kLaunching) {
      m.phase = MissilePhase::kTraveling;
    }

    if (m.phase == MissilePhase::kTraveling) {
      m.elapsed += dt;

      if (m.travel_time <= 0.0f) {

        m.progress = 1.0f;
      } else {
        m.progress = std::clamp(m.elapsed / m.travel_time, 0.0f, 1.0f);
      }

      switch (m.motion) {
      case MissileMotionType::kLinear:
        UpdateLinear(m, m.progress);
        break;
      case MissileMotionType::kParabolicArc:
        UpdateParabolicArc(m, m.progress);
        break;
      case MissileMotionType::kHoming:
        UpdateHoming(m, m.progress);
        break;
      }

      if (m.motion_id != 0) {
        SpellMissileMotionInputs inputs;
        inputs.progress = m.progress;
        inputs.time = m.elapsed;
        inputs.missile_index = m.salvo_index;
        inputs.missile_count = m.salvo_count;
        inputs.distance_to_fire_position = m.current.DistanceTo(m.source);
        inputs.distance_to_impact_position = m.current.DistanceTo(m.target);
        inputs.start_distance = m.source.DistanceTo(m.current);
        inputs.total_distance = m.source.DistanceTo(m.target);
        inputs.random_value_1 = m.random_motion.x;
        inputs.random_value_2 = m.random_motion.y;
        inputs.random_value_3 = m.random_motion.z;
        inputs.spell_id = m.spell_id;

        SpellMissileMotionOutputs outputs;
        if (SpellMissileMotionRegistry::Get().Evaluate(m.motion_id, inputs, outputs)) {
          const auto speed_override =
              ResolveSpellMissileMotionSpeed(m.base_speed, outputs);
          if (speed_override.has_override) {
            m.speed = speed_override.speed;
          }

          constexpr float kDegreesToRadians = 0.017453292f;
          m.model_rotation_radians = {
              static_cast<float>(outputs.model_yaw) * kDegreesToRadians,
              static_cast<float>(outputs.model_pitch) * kDegreesToRadians,
              static_cast<float>(outputs.model_roll) * kDegreesToRadians,
          };
          if (outputs.model_scale > 0.0) {
            m.model_scale = static_cast<float>(outputs.model_scale);
          }

          const float angle = static_cast<float>(outputs.trans_angle);
          const float magnitude = static_cast<float>(outputs.trans_magnitude);
          m.script_translation = {
              static_cast<float>(outputs.trans_front),
              static_cast<float>(outputs.trans_right),
              static_cast<float>(outputs.trans_up),
          };
          m.script_translation.y += std::cos(angle) * magnitude;
          m.script_translation.z += std::sin(angle) * magnitude;
        }
      }

      if (m.progress >= 1.0f) {
        m.current = m.has_collision_position ? m.collision_position : m.target;
        if (m.has_collision_position &&
            !CMissile_C_SendUpdateProjectilePositionAndImpact(m)) {
          ++it;
          continue;
        }
        m.phase = MissilePhase::kImpacting;
        impacted_.push_back(m.missile_id);

        if (on_impact_) {
          on_impact_(m);
        }

        auto arrived = it++;
        pending_impacts_.splice(pending_impacts_.begin(), active_missiles_, arrived);
        continue;
      }
    }
    ++it;
  }

  for (auto it = pending_impacts_.begin(); it != pending_impacts_.end();) {
    if (!it->wait_for_impact_visual || it->impact_visual_complete) {
      it->phase = MissilePhase::kExpired;
      it = pending_impacts_.erase(it);
    } else {
      ++it;
    }
  }
}

void MissileManager::Remove(std::uint32_t missile_id) {
  const auto located = FindLocated(missile_id);
  if (located.owner != nullptr) {
    located.owner->erase(located.iterator);
  }
}

void MissileManager::RemoveForCaster(const ObjectGuid &caster) {
  for (auto it = active_missiles_.begin(); it != active_missiles_.end();) {

    if (it->caster_guid == caster) {
      it = active_missiles_.erase(it);
    } else {
      ++it;
    }
  }
}

void MissileManager::ClearAll() {
  active_missiles_.clear();
  pending_impacts_.clear();
  impacted_.clear();
}

std::optional<MissileInfo> MissileManager::GetMissile(std::uint32_t missile_id) const {
  const auto located = FindLocated(missile_id);
  if (located.owner != nullptr)
    return *located.iterator;
  return std::nullopt;
}

MissileInfo *MissileManager::FindMissile(const std::uint32_t missile_id) {
  const auto located = FindLocated(missile_id);
  if (located.owner == nullptr) {
    return nullptr;
  }
  return &*located.iterator;
}

const MissileInfo *MissileManager::FindMissile(const std::uint32_t missile_id) const {
  const auto located = FindLocated(missile_id);
  if (located.owner == nullptr) {
    return nullptr;
  }
  return &*located.iterator;
}

std::vector<MissileInfo> MissileManager::GetActiveMissiles() const {
  std::vector<MissileInfo> result;
  result.reserve(active_missiles_.size());
  for (const MissileInfo &m : active_missiles_) {
    if (m.phase != MissilePhase::kExpired) {
      result.push_back(m);
    }
  }
  return result;
}

std::vector<MissileInfo> MissileManager::GetPendingImpacts() const {
  return {pending_impacts_.begin(), pending_impacts_.end()};
}

std::vector<MissileInfo> MissileManager::GetMissilesForCaster(const ObjectGuid &caster) const {
  std::vector<MissileInfo> result;
  for (const MissileInfo &m : active_missiles_) {
    if (m.phase != MissilePhase::kExpired && m.caster_guid == caster) {
      result.push_back(m);
    }
  }
  return result;
}

std::vector<MissileInfo> MissileManager::GetMissilesForTarget(const ObjectGuid &target) const {
  std::vector<MissileInfo> result;
  for (const MissileInfo &m : active_missiles_) {
    if (m.phase != MissilePhase::kExpired && m.target_guid == target) {
      result.push_back(m);
    }
  }
  return result;
}

std::uint32_t MissileManager::GetActiveCount() const {
  return static_cast<std::uint32_t>(std::count_if(
      active_missiles_.begin(), active_missiles_.end(),
      [](const MissileInfo &missile) { return missile.phase != MissilePhase::kExpired; }));
}

std::uint32_t MissileManager::GetPendingImpactCount() const {
  return static_cast<std::uint32_t>(pending_impacts_.size());
}

bool MissileManager::CompletePendingImpact(const std::uint32_t missile_id) {
  const auto it = std::find_if(
      pending_impacts_.begin(), pending_impacts_.end(),
      [missile_id](const MissileInfo &missile) { return missile.missile_id == missile_id; });
  if (it == pending_impacts_.end()) {
    return false;
  }
  it->impact_visual_complete = true;
  return true;
}

std::vector<std::uint32_t> MissileManager::DrainImpacted() {
  std::vector<std::uint32_t> result;
  std::swap(result, impacted_);
  return result;
}

bool MissileManager::SetCollisionPosition(const ObjectGuid &caster,
                                           std::uint8_t cast_count,
                                           const MissileVec3 &position) {
  bool matched = false;
  for (MissileInfo &m : active_missiles_) {
    if (m.caster_guid == caster && m.cast_count == cast_count) {
      m.has_collision_position = true;
      m.collision_position = position;
      matched = true;
    }
  }
  return matched;
}

std::size_t MissileManager::SetImpactDelay(const ObjectGuid &caster,
                                           const std::uint8_t cast_count,
                                           const std::uint32_t spell_id,
                                           const std::uint32_t current_tick_ms) {
  std::size_t matched = 0;
  for (MissileInfo &missile : active_missiles_) {
    if (missile.caster_guid != caster || missile.cast_count != cast_count ||
        missile.spell_id != spell_id ||
        missile.impact_delay_deadline_tick_ms != 0) {
      continue;
    }

    const std::uint32_t deadline = current_tick_ms + 500u;
    missile.impact_delay_deadline_tick_ms = deadline == 0 ? 1u : deadline;
    ++matched;
  }
  return matched;
}

std::size_t MissileManager::RescaleBallisticDuration(
    const std::uint8_t cast_count, const std::uint32_t requested_duration_ms,
    const std::uint32_t current_tick_ms) {
  std::size_t matched = 0;
  for (MissileInfo &missile : active_missiles_) {
    if (missile.cast_count != cast_count || !missile.use_ballistic_solver) {
      continue;
    }

    missile.ballistic_timing_adjusted = true;
    missile.ballistic_adjust_start_tick_ms = current_tick_ms;
    const auto remaining = static_cast<std::int32_t>(
        missile.ballistic_deadline_tick_ms - current_tick_ms);
    if (remaining <= 0) {
      missile.ballistic_adjust_duration_ms = 1;
      ++matched;
      continue;
    }

    const float remaining_ms = static_cast<float>(remaining);
    const float requested_ms = requested_duration_ms == 0
                                   ? 1.0f
                                   : static_cast<float>(requested_duration_ms);
    const float ratio = std::clamp(requested_ms / remaining_ms, 0.5f, 2.0f);
    const auto adjusted = static_cast<std::uint32_t>(ratio * remaining_ms);
    missile.ballistic_adjust_duration_ms = adjusted == 0 ? 1u : adjusted;
    ++matched;
  }
  return matched;
}

void MissileManager::SetOnImpact(ImpactCallback cb) {
  on_impact_ = std::move(cb);
}

void MissileManager::Reset() {
  active_missiles_.clear();
  pending_impacts_.clear();
  impacted_.clear();
  on_impact_ = nullptr;
  next_id_ = 1;
}

namespace {

std::uint32_t* s_missile_type_handle = nullptr;

}

void CMissile_RegisterTypeAndLifecycleCallback() {
  if (s_missile_type_handle != nullptr) {
    return;
  }

  auto* handle = new (std::nothrow) std::uint32_t{0};
  if (handle) {

    *handle = openwow::core::CObjectHeapList::Instance().RegisterType(
        448, 64, "CMissile", true);
    s_missile_type_handle = handle;
  } else {
    s_missile_type_handle = nullptr;
  }

  simple_script::SimpleScript_RegisterLifecycleCallback(
      CMissile_LifecycleCallback);
}

void CMissile_UnregisterTypeAndLifecycleCallback() {
  static_cast<void>(simple_script::SimpleScript_UnregisterLifecycleCallback(
      CMissile_LifecycleCallback));

  if (s_missile_type_handle) {
    delete s_missile_type_handle;
  }
  s_missile_type_handle = nullptr;
}

int CMissile_LifecycleCallback(int phase) {
  if (phase == 1) {
    CMissile_ReleaseAll();
  }
  return 0;
}

void CMissile_ReleaseAll() {
  MissileManager::Get().ClearAll();
}

std::uint32_t* CMissile_GetTypeHandle() {
  return s_missile_type_handle;
}

bool CMissile_C_SendUpdateProjectilePositionAndImpact(MissileInfo& missile) {
  using openwow::net::wotlk::PacketSender;
  using openwow::net::ClientServices__SendPacket;

  auto pkt = PacketSender::BuildUpdateProjectilePosition(
      missile.caster_guid.GetRawValue(),
      missile.spell_id,
      missile.cast_count,
      missile.current.x,
      missile.current.y,
      missile.current.z);

  if (!ClientServices__SendPacket(pkt)) {
    return false;
  }

  missile.position_parent_guid = {};
  missile.last_reported_position = missile.current;
  missile.phase = MissilePhase::kImpacting;
  return true;
}

std::int32_t CMissile_C_ResolveMissAnimSequence(
    const std::function<bool(std::uint32_t)>& has_anim_record,
    const std::int32_t requested) {

  if (requested >= 0 &&
      has_anim_record(static_cast<std::uint32_t>(requested))) {
    return requested;
  }

  if (has_anim_record(15u)) {
    return 15;
  }
  return has_anim_record(19u) ? 19 : -1;
}

void CMissile_C_CreateInner(
    MissileInfo& info,
    const MissileVec3& source_pos,
    MissileCreateState& state,
    const std::uint32_t current_tick_ms,
    const std::function<MissileVec3()>& target_pos_fn,
    const std::function<float()>& rng_fn,
    const std::function<bool(std::uint32_t)>* has_anim_fn) {

  info.ResolveCreateImpactBehavior();

  info.target = source_pos;
  info.source = source_pos;

  if (has_anim_fn) {
    state.miss_anim_id = CMissile_C_ResolveMissAnimSequence(
        *has_anim_fn, state.miss_anim_id);
    state.source_unit_resolved = true;
  } else {

    state.miss_anim_id = -1;
  }

  if (info.target_guid.IsEmpty()) {
    state.no_target_guid = true;
  }

  info.progress = 0.0f;
  info.elapsed = 0.0f;

  const MissileVec3 target_pos = target_pos_fn();
  info.target = target_pos;

  const float dx = target_pos.x - info.source.x;
  const float dy = target_pos.y - info.source.y;
  const float dz = target_pos.z - info.source.z;

  if (!state.is_clone) {
    state.flight_start_tick_ms = current_tick_ms;
  }
  state.total_distance =
      std::sqrt(dx * dx + dy * dy + dz * dz);

  state.random_motion.x = rng_fn();
  state.random_motion.y = rng_fn();
  state.random_motion.z = rng_fn();

  if (info.speed > 0.0f && !state.is_clone) {
    const float max_speed = info.speed + info.speed;
    const float flight_dist = state.total_distance;

    if (flight_dist > 0.0f) {
      const float base_travel = flight_dist / info.speed;
      const float elapsed_so_far =
          static_cast<float>(state.delay_offset_ms) * 0.001f;
      float remaining = base_travel - elapsed_so_far;

      float effective_speed;
      if (remaining > 0.0f) {
        effective_speed = flight_dist / remaining;
        if (max_speed < effective_speed) {
          effective_speed = max_speed;
        }
      } else {
        remaining = 0.0f;
        effective_speed = max_speed;
      }

      info.speed = effective_speed;

      info.travel_time = flight_dist / info.speed;
      state.travel_time_ms =
          static_cast<std::uint32_t>(info.travel_time * 1000.0f);
    } else {
      info.travel_time = 0.0f;
      state.travel_time_ms = 0;
    }
  } else if (state.is_clone) {

    if (info.speed > 0.0f && state.total_distance > 0.0f) {
      info.travel_time = state.total_distance / info.speed;
      state.travel_time_ms =
          static_cast<std::uint32_t>(info.travel_time * 1000.0f);
    }
  }

  info.phase = MissilePhase::kLaunching;
  info.current = info.source;
}

}

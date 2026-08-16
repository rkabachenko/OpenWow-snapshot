
#include "openwow/game/missile_trajectory.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/foundation/math/fast_trig_approx.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace openwow::game {
namespace {

constexpr std::uint32_t kOpaqueWhite = 0x00FFFFFF;
constexpr std::uint32_t kMissileTrajectoryPreviewFlagMask = 0x0F800000u;
constexpr std::uint32_t kMissileTrajectoryWorldIntersectFlags = 0x00100111u;
constexpr int kRibbonPreviewTextureFilterFlags = 0x209;
constexpr int kEndpointPreviewTextureFilterFlags = 0x201;
constexpr float kTrajectoryStepSeconds = 0.1f;
constexpr float kMinimumSegmentLength = 1.0e-5f;
constexpr float kMinimumTargetRadius = 0.0001f;
constexpr std::uint32_t kMissileAimEvent = 0x4D494124u;

[[nodiscard]] std::uint32_t PackOpaqueWhiteWithAlpha(std::uint8_t alpha) {
  return kOpaqueWhite | (static_cast<std::uint32_t>(alpha) << 24);
}

[[nodiscard]] std::uint8_t QuantizeAlphaByte(float value) {
  return static_cast<std::uint8_t>(static_cast<int>(value + 0.5f));
}

[[nodiscard]] std::array<float, 16> BuildZRotationMatrix(float radians) {
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  return {
      cosine, sine, 0.0f, 0.0f, -sine, cosine, 0.0f, 0.0f,
      0.0f,   0.0f, 1.0f, 0.0f, 0.0f,  0.0f,   0.0f, 1.0f,
  };
}

void AppendRibbonVertex(MissileArcRibbonSnapshot &ribbon, float x, float y, float z,
                        std::uint32_t color_argb, float u, float v) {
  MissileArcVertex vertex;
  vertex.x = x;
  vertex.y = y;
  vertex.z = z;
  vertex.color_argb = color_argb;
  vertex.u = u;
  vertex.v = v;
  ribbon.vertices.push_back(vertex);
}

[[nodiscard]] bool HasMissileTrajectoryPreviewResources(const std::uint32_t spell_visual_flags) {
  return (spell_visual_flags & kMissileTrajectoryPreviewFlagMask) != 0;
}

[[nodiscard]] bool HasLegacySignedTickReachedOrPassed(
    const std::uint32_t current_tick_ms,
    const std::uint32_t expiry_tick_ms) {
  return current_tick_ms - expiry_tick_ms < 0x80000000u;
}

[[nodiscard]] TrajectoryPoint ResolveAppliedUnitPosition(const CGUnit_C &unit) {
  std::array<float, 16> transform{};
  if (unit.GetVisualModelWorldTransform(transform.data())) {
    return {transform[12], transform[13], transform[14]};
  }

  const Position position = unit.GetPosition();
  return {position.x, position.y, position.z};
}

[[nodiscard]] const data::dbc::CreatureModelDataEntry *
ResolveUnitCreatureModelData(const CGUnit_C &unit) {
  const auto* const dbc = unit.dbc_loader();
  if (dbc == nullptr) {
    return nullptr;
  }
  const auto *display = dbc->creature_display_info().LookupEntry(
      unit.Presentation().CreatureModelLookupDisplayId());
  return display != nullptr
             ? dbc->creature_model_data().LookupEntry(display->model_id)
             : nullptr;
}

[[nodiscard]] std::optional<float> ResolveUnitModelMaximumExtent(
    const CGUnit_C &unit) {
  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  if (instance_id == 0) {
    return std::nullopt;
  }

  auto* const m2_system = unit.m2_system();
  if (m2_system == nullptr) {
    return std::nullopt;
  }
  const auto spatial = m2_system->QueryInstanceSpatialInfo(instance_id);
  if (spatial.status != render::m2::M2ResultStatus::kReady) {
    return std::nullopt;
  }

  const auto &bounds = spatial.spatial.local_bounds;
  return std::max({bounds[3], bounds[4], bounds[5]});
}

[[nodiscard]] bool IsExcludedVehicleBranch(const CGUnit_C &candidate,
                                            const CGUnit_C *excluded) {
  if (excluded == nullptr) {
    return false;
  }
  if (&candidate == excluded) {
    return true;
  }
  return candidate.Vehicle().GetVehicleObject(candidate) == excluded;
}

[[nodiscard]] bool CanUseMissileTrajectoryPreview(const CGUnit_C &unit) {

  return !unit.State().IsDead() && !unit.State().IsStunned() && !unit.State().IsTaxiFlight() &&
         unit.Movement().CanControlCharacter();
}

[[nodiscard]] float RetailHorizontalMissileSpeed(float pitch_radians,
                                                 float speed) noexcept {

  float half_turns = pitch_radians * 0.31830987f;
  std::int32_t interval = 0;
  if (half_turns <= 0.0f) {
    interval = ~static_cast<std::int32_t>(std::max(-half_turns, 0.0f));
  } else {
    interval = static_cast<std::int32_t>(half_turns);
  }
  half_turns -= static_cast<float>(interval);
  float cosine =
      1.0f - half_turns * half_turns * (half_turns * -4.0f + 6.0f);
  if ((interval & 1) != 0) {
    cosine = -cosine;
  }

  const float horizontal_speed = cosine * speed;
  return horizontal_speed >= kMinimumTargetRadius || std::isnan(horizontal_speed)
             ? horizontal_speed
             : kMinimumTargetRadius;
}

[[nodiscard]] TrajectoryPoint ResolveMissileLaunchPosition(
    const CGUnit_C &unit, const data::dbc::SpellEntry &spell,
    const data::dbc::DbcLoader &dbc) {
  const std::uint32_t instance_id = unit.GetPrimaryM2InstanceId();
  if (instance_id != 0) {
    auto* const m2 = unit.m2_system();
    if (m2 != nullptr) {
      const auto animation = m2->QueryInstanceAnimationInfo(instance_id);
      if (animation.status == render::m2::M2ResultStatus::kReady) {
        const auto event = m2->QueryInstanceEvent(
            instance_id, animation.info.resolved_animation_id, kMissileAimEvent);
        if (event.status == render::m2::M2ResultStatus::kReady &&
            event.has_event) {
          return {event.event.world_position[0], event.event.world_position[1],
                  event.event.world_position[2]};
        }
      }
    }
  }

  const data::dbc::SpellVisualEntry *visual = nullptr;
  for (const std::uint32_t visual_id : spell.spell_visual) {
    if (visual_id != 0) {
      visual = dbc.spell_visual().LookupEntry(visual_id);
      if (visual != nullptr) {
        break;
      }
    }
  }

  std::uint32_t attachment_id = std::numeric_limits<std::uint32_t>::max();
  float offset[3]{};
  bool use_raw_attachment_index = false;
  if (visual != nullptr) {
    if (visual->missile_attachment_id >= 0) {
      attachment_id = static_cast<std::uint32_t>(visual->missile_attachment_id);
    }
    offset[0] = visual->missile_cast_offset_x;
    offset[1] = visual->missile_cast_offset_y;
    offset[2] = visual->missile_cast_offset_z;
    use_raw_attachment_index = (visual->flags & 0x200u) != 0;
  }

  float launch[3]{};
  SpellVisual_GetAttachSourcePosition(
      launch, reinterpret_cast<std::uintptr_t>(&unit), attachment_id, offset,
      use_raw_attachment_index);
  return {launch[0], launch[1], launch[2]};
}

[[nodiscard]] MissileTrajectoryPreviewResourceLoader MakeDefaultPreviewResourceLoader(
    render::m2::M2System& m2_system) {
  MissileTrajectoryPreviewResourceLoader loader;
  loader.load_model_instance = [&m2_system](const std::string_view path) -> MissileTrajectoryPreviewModelLoadResult {
    if (path.empty()) {
      return {.status = render::m2::M2ResultStatus::kFailed,
              .reason = render::m2::M2ResultReason::kInvalidHandle,
              .detail = "empty model path"};
    }
    const auto instance_result = m2_system.LoadModelInstance(std::string(path));
    return {.status = instance_result.status,
            .reason = instance_result.reason,
            .detail = instance_result.detail,
            .instance_id = instance_result.instance_id};
  };
  loader.release_model_instance = [&m2_system](const std::uint32_t handle) {
    if (handle == 0) {
      return;
    }
    const auto destroy_status = m2_system.DestroyInstance(handle);
    if (render::m2::IsTerminalM2ResultStatus(destroy_status)) {
      return;
    }
  };
  return loader;
}

}

SpellMissileCastTrajectoryResult
UnitMissileTrajectory_C::PrepareSpellCastTrajectory(
    const CGUnit_C& caster, const std::uint32_t spell_id,
    const ObjectManager& object_manager,
    foundation::hashing::AdlerSeedState& random_state) const {
  SpellMissileCastTrajectoryResult result;
  const auto* const dbc = caster.dbc_loader();
  const auto* const spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
  const auto* const missile =
      spell != nullptr
          ? dbc->spell_missile().LookupEntry(spell->spell_missile_id)
          : nullptr;
  if (missile == nullptr || (missile->flags & 1u) == 0u) {
    return result;
  }

  float pitch =
      (missile->default_pitch_min + missile->default_pitch_max) * 0.5f;
  float speed =
      (missile->default_speed_min + missile->default_speed_max) * 0.5f;
  if (const auto* const vehicle = caster.Vehicle().GetVehicleEntry();
      vehicle != nullptr) {
    if ((vehicle->flags & 0x10u) != 0u) {
      pitch = caster.GetFlyHeight();
      if ((vehicle->flags & 0x40u) != 0u) {
        pitch = std::clamp(pitch, vehicle->pitch_min, vehicle->pitch_max);
      }
    }
    if ((vehicle->flags & 0x800u) != 0u) {
      speed = missile->default_speed_min +
              InputControl_GetVehicleAimNormalizedPower() *
                  (missile->default_speed_max - missile->default_speed_min);
    }
  }

  pitch += foundation::hashing::AdlerSeedNextRangeFloat(
      missile->randomize_pitch_min, missile->randomize_pitch_max,
      random_state);
  speed += foundation::hashing::AdlerSeedNextRangeFloat(
      missile->randomize_speed_min, missile->randomize_speed_max,
      random_state);
  float facing = caster.GetWorldFacing() +
                 foundation::hashing::AdlerSeedNextRangeFloat(
                     missile->randomize_facing_min,
                     missile->randomize_facing_max, random_state);
  pitch += foundation::hashing::AdlerSeedNextRangeFloat(
      -0.01f, 0.01f, random_state);
  speed += foundation::hashing::AdlerSeedNextRangeFloat(
      -0.01f, 0.01f, random_state);

  TrajectoryPoint source = ResolveMissileLaunchPosition(caster, *spell, *dbc);
  source.x += foundation::hashing::AdlerSeedNextRangeFloat(
      -0.1f, 0.1f, random_state);
  source.y += foundation::hashing::AdlerSeedNextRangeFloat(
      -0.1f, 0.1f, random_state);
  source.z += foundation::hashing::AdlerSeedNextRangeFloat(
      -0.01f, 0.01f, random_state);

  const int step_count =
      missile->max_duration > 0.0001f
          ? std::min(static_cast<int>(missile->max_duration /
                                      kTrajectoryStepSeconds),
                     200)
          : 200;
  const float horizontal_step =
      speed * kTrajectoryStepSeconds * std::cos(pitch);
  const TrajectoryPoint nominal_destination{
      source.x + static_cast<float>(step_count) * horizontal_step *
                     std::cos(facing),
      source.y + static_cast<float>(step_count) * horizontal_step *
                     std::sin(facing),
      source.z + static_cast<float>(step_count) * speed *
                     kTrajectoryStepSeconds * std::sin(pitch),
  };

  MissileCollisionTrajectoryNode collision_node;
  const MissileCollisionTrajectoryNode* collision = nullptr;
  if ((missile->flags & 0x3Cu) != 0u) {
    collision_node.Configure({
        .source = source,
        .destination = nominal_destination,
        .pitch_radians = pitch,
        .speed = speed,
        .spell_missile = missile,
        .excluded_unit = &caster,
    });

    collision_node.PopulateTargets(const_cast<ObjectManager&>(object_manager));
    collision = &collision_node;
  }

  const auto solved = CalculateMissileTrajectoryArc(
      {
          .origin = source,
          .facing_radians = facing,
          .pitch_radians = pitch,
          .speed = speed,
          .gravity = missile->gravity,
          .max_duration_seconds = missile->max_duration,
          .collision_node = collision,
      },
      world_intersection_);
  if (!solved.valid) {
    result.status = SpellMissileCastTrajectoryStatus::kFailed;
    return result;
  }

  result.status = SpellMissileCastTrajectoryStatus::kReady;
  result.source = solved.origin;
  result.destination = solved.impact;
  result.pitch_radians = solved.pitch_radians;
  result.speed = solved.speed;
  return result;
}

bool MissileTrajectory_HasAOETargetFlags(std::uint32_t flags) {
  return (flags & 0x300) != 0 && (flags & 0x2000001) == 0;
}

MissileArcRenderSnapshot BuildMissileArcRenderSnapshot(const MissileArcRenderInputs &inputs) {
  MissileArcRenderSnapshot snapshot{};

  if (inputs.alpha <= 0.0f || inputs.source_unit_guid == 0) {
    return snapshot;
  }

  if ((inputs.ribbon_texture_handle != 0 || !inputs.ribbon_texture_path.empty()) &&
      !inputs.trajectory_points.empty()) {
    MissileArcRibbonSnapshot ribbon{};
    ribbon.texture_handle = inputs.ribbon_texture_handle;
    ribbon.texture_path = inputs.ribbon_texture_path;

    const float half_width = inputs.style.ribbon_width * 0.5f;
    const openwow::math::FastTrigSample facing =
        openwow::math::FastSinCosApprox(inputs.source_facing_radians);
    const float offset_x = -facing.sine * half_width;
    const float offset_y = facing.cosine * half_width;
    const float base_alpha = inputs.alpha * 255.0f;
    const float tip_alpha = 255.0f * (inputs.alpha * inputs.style.ribbon_tip_alpha_scale);
    const float alpha_step =
        (tip_alpha - base_alpha) / static_cast<float>(inputs.trajectory_points.size());
    const std::uint32_t elapsed_ms = inputs.current_time_ms - inputs.trajectory_create_time_ms;
    const float scroll_u =
        1.0f -
        static_cast<float>(std::fmod(static_cast<double>(elapsed_ms) * 0.001 *
                                         static_cast<double>(inputs.style.ribbon_scroll_speed),
                                     1.0));

    ribbon.vertices.reserve(inputs.trajectory_points.size() * 2 + 2);
    ribbon.indices.reserve(inputs.trajectory_points.size() * 2 + 2);

    AppendRibbonVertex(ribbon, inputs.trajectory_origin.x + offset_x,
                       inputs.trajectory_origin.y + offset_y, inputs.trajectory_origin.z,
                       kOpaqueWhite, scroll_u, 0.0f);
    AppendRibbonVertex(ribbon, inputs.trajectory_origin.x - offset_x,
                       inputs.trajectory_origin.y - offset_y, inputs.trajectory_origin.z,
                       kOpaqueWhite, scroll_u, 1.0f);

    float current_alpha = base_alpha + alpha_step;
    float current_u = scroll_u + inputs.style.ribbon_u_step;

    for (const TrajectoryPoint &point : inputs.trajectory_points) {
      if (current_alpha < 0.0f) {
        current_alpha = 0.0f;
      }

      const std::uint8_t alpha_byte = QuantizeAlphaByte(current_alpha);
      const std::uint32_t color_argb = PackOpaqueWhiteWithAlpha(alpha_byte);

      AppendRibbonVertex(ribbon, point.x + offset_x, point.y + offset_y, point.z, color_argb,
                         current_u, 0.0f);
      AppendRibbonVertex(ribbon, point.x - offset_x, point.y - offset_y, point.z, color_argb,
                         current_u, 1.0f);

      current_alpha += alpha_step;
      current_u += inputs.style.ribbon_u_step;
    }

    for (std::uint16_t index = 0; index < static_cast<std::uint16_t>(ribbon.vertices.size());
         ++index) {
      ribbon.indices.push_back(index);
    }

    snapshot.ribbon = std::move(ribbon);
  }

  if ((inputs.endpoint_texture_handle != 0 || !inputs.endpoint_texture_path.empty()) &&
      !inputs.trajectory_points.empty()) {
    const TrajectoryPoint &last_point = inputs.trajectory_points.back();
    const float half_extent = inputs.style.endpoint_half_extent;

    MissileArcEndpointProjectionSnapshot endpoint;
    endpoint.texture_handle = inputs.endpoint_texture_handle;
    endpoint.texture_path = inputs.endpoint_texture_path;
    endpoint.min_corner = {
        last_point.x - half_extent,
        last_point.y - half_extent,
        last_point.z - half_extent,
    };
    endpoint.max_corner = {
        last_point.x + half_extent,
        last_point.y + half_extent,
        last_point.z + half_extent,
    };
    endpoint.color_argb = PackOpaqueWhiteWithAlpha(QuantizeAlphaByte(inputs.alpha * 255.0f));
    endpoint.rotation_matrix = BuildZRotationMatrix(-inputs.source_facing_radians);
    snapshot.endpoint = endpoint;
  }

  snapshot.active = snapshot.ribbon.has_value() || snapshot.endpoint.has_value();
  return snapshot;
}

void DelayedMissileTrajectoryState::Begin(
    const std::uint32_t new_spell_id,
    const std::uint32_t new_cast_start_tick_ms) noexcept {
  spell_id = new_spell_id;
  cast_start_tick_ms = new_cast_start_tick_ms;
}

void DelayedMissileTrajectoryState::Complete(
    const std::uint32_t completed_spell_id,
    const std::uint32_t new_spell_go_tick_ms,
    const std::uint32_t new_server_delay_ms) noexcept {
  if (spell_id != completed_spell_id) {
    return;
  }
  spell_go_tick_ms = new_spell_go_tick_ms;
  server_delay_ms = new_server_delay_ms;
}

void DelayedMissileTrajectoryState::Cancel(
    const std::uint32_t cancelled_spell_id) noexcept {
  if (spell_id != cancelled_spell_id) {
    return;
  }
  spell_id = 0;
  cast_start_tick_ms = 0;
  spell_go_tick_ms = 0;
  active_missile_cast_count = 0;

}

bool DelayedMissileTrajectoryState::HasPayload() const noexcept {
  return cast_start_tick_ms != 0 && spell_go_tick_ms != 0;
}

bool DelayedMissileTrajectoryState::HasActiveMissile() const noexcept {
  return HasPayload() && active_missile_cast_count != 0;
}

DelayedMissileTrajectoryGateResult EvaluateDelayedMissileTrajectoryPayloadGate(
    const DelayedMissileTrajectoryState &state,
    const std::uint32_t current_tick_ms) noexcept {
  DelayedMissileTrajectoryGateResult result;
  result.deadline_tick_ms =
      state.cast_start_tick_ms + state.server_delay_ms;

  result.ready = state.HasPayload() &&
                 state.active_missile_cast_count == 0 &&
                 HasLegacySignedTickReachedOrPassed(current_tick_ms, result.deadline_tick_ms);
  return result;
}

DelayedMissileTrajectoryPayloadDecodeResult DecodeDelayedMissileTrajectoryPayload(
    const std::span<const std::uint8_t> encoded,
    const DelayedMissileTrajectoryPayloadDecodeMode mode) {
  DelayedMissileTrajectoryPayloadDecodeResult result;

  if (mode == DelayedMissileTrajectoryPayloadDecodeMode::Raw) {
    const std::size_t copy_size = std::min(encoded.size(), result.payload.size());
    std::copy_n(encoded.begin(), copy_size, result.payload.begin());
    result.bytes_read = copy_size;
    result.bytes_written = copy_size;
    result.complete = copy_size == result.payload.size();
    return result;
  }

  if (encoded.empty()) {
    return result;
  }

  result.payload[0] = encoded[0];
  result.bytes_written = 1;
  std::size_t input_index = 0;

  while (result.bytes_written < result.payload.size()) {
    const std::size_t previous_index = input_index;
    const std::size_t current_index = previous_index + 1;
    if (current_index >= encoded.size()) {
      result.bytes_read = encoded.size();
      return result;
    }

    const std::uint8_t previous = encoded[previous_index];
    const std::uint8_t current = encoded[current_index];
    result.payload[result.bytes_written++] = current;
    input_index = current_index;

    if (current != previous) {
      continue;
    }

    const std::size_t run_count_index = previous_index + 2;
    if (run_count_index >= encoded.size()) {
      result.bytes_read = encoded.size();
      return result;
    }

    const std::uint8_t run_count = encoded[run_count_index];
    for (std::uint8_t repeat = 0;
         repeat < run_count && result.bytes_written < result.payload.size();
         ++repeat) {
      result.payload[result.bytes_written++] = current;
    }

    input_index = previous_index + 3;
    if (result.bytes_written >= result.payload.size()) {
      break;
    }
    if (input_index >= encoded.size()) {
      result.bytes_read = encoded.size();
      return result;
    }

    result.payload[result.bytes_written++] = encoded[input_index];
  }

  result.bytes_read = std::min(input_index + 1, encoded.size());
  result.complete = true;
  return result;
}

void SetDelayedMissileTrajectoryActiveCastCount(
    DelayedMissileTrajectoryState &state,
    const std::uint8_t cast_count) noexcept {
  state.active_missile_cast_count = cast_count;
}

UnitMissileTrajectory_C::~UnitMissileTrajectory_C() {
  Cleanup();
}

void UnitMissileTrajectory_C::Initialize() {
  if (initialized_) {
    Cleanup();
  }

  trajectory_points_.fill({});
  trajectory_point_count_ = 0;

  visual_objects_.fill(0);
  visual_particles_.fill(0);

  collision_nodes_.clear();

  ribbon_texture_ = 0;
  endpoint_texture_ = 0;
  ribbon_texture_path_.clear();
  endpoint_texture_path_.clear();
  preview_texture_release_ = nullptr;
  preview_model_release_ = nullptr;

  alpha_ = 0.0f;
  current_mode_ = MissileTargetMode::Direct;
  mode_start_time_ = 0;
  last_spell_id_ = 0;
  input_refresh_latched_ = false;
  trajectory_create_time_ = 0;
  current_time_ms_ = 0;
  source_unit_guid_ = 0;
  source_facing_radians_ = 0.0f;
  trajectory_origin_ = {};
  render_style_ = {};
  render_snapshot_ = {};
  world_intersection_ = {};

  initialized_ = true;
}

void UnitMissileTrajectory_C::Cleanup() {
  if (!initialized_)
    return;

  trajectory_points_.fill({});
  trajectory_point_count_ = 0;

  ClearPreviewResources();

  collision_nodes_.clear();

  alpha_ = 0.0f;
  current_mode_ = MissileTargetMode::Direct;
  mode_start_time_ = 0;
  last_spell_id_ = 0;
  input_refresh_latched_ = false;
  trajectory_create_time_ = 0;
  current_time_ms_ = 0;
  source_unit_guid_ = 0;
  source_facing_radians_ = 0.0f;
  trajectory_origin_ = {};
  render_style_ = {};
  render_snapshot_ = {};
  world_intersection_ = {};
  initialized_ = false;
}

void UnitMissileTrajectory_C::ClearPreviewResources() {
  MissileTrajectoryPreviewResourceLoader loader;
  loader.release_texture = preview_texture_release_;
  loader.release_model_instance = preview_model_release_;
  ClearPreviewResources(loader);
}

void UnitMissileTrajectory_C::ClearPreviewResources(
    const MissileTrajectoryPreviewResourceLoader &loader) {
  if (ribbon_texture_ != 0 && loader.release_texture) {
    loader.release_texture(ribbon_texture_);
  }
  ribbon_texture_ = 0;
  ribbon_texture_path_.clear();

  if (endpoint_texture_ != 0 && loader.release_texture) {
    loader.release_texture(endpoint_texture_);
  }
  endpoint_texture_ = 0;
  endpoint_texture_path_.clear();

  for (std::uint32_t &visual_object : visual_objects_) {
    if (visual_object != 0 && loader.release_model_instance) {
      loader.release_model_instance(visual_object);
    }
    visual_object = 0;
  }

  visual_particles_.fill(0);
  source_unit_guid_ = 0;
  preview_texture_release_ = nullptr;
  preview_model_release_ = nullptr;
}

void UnitMissileTrajectory_C::LoadPreviewResources(
    const MissileTrajectoryPreviewResourceConfig &config,
    render::m2::M2System& m2_system) {
  LoadPreviewResources(config, MakeDefaultPreviewResourceLoader(m2_system));
}

void UnitMissileTrajectory_C::LoadPreviewResources(
    const MissileTrajectoryPreviewResourceConfig &config,
    const MissileTrajectoryPreviewResourceLoader &loader) {
  if (!initialized_) {
    Initialize();
  }
  ClearPreviewResources(loader);

  if (!HasMissileTrajectoryPreviewResources(config.spell_visual_flags)) {
    return;
  }

  preview_texture_release_ = loader.release_texture;
  preview_model_release_ = loader.release_model_instance;
  ribbon_texture_path_ = config.ribbon_texture_path;
  endpoint_texture_path_ = config.endpoint_texture_path;

  if (!config.ribbon_texture_path.empty() && loader.load_texture) {
    ribbon_texture_ = static_cast<std::uint32_t>(
        loader.load_texture(config.ribbon_texture_path, kRibbonPreviewTextureFilterFlags));
  } else {
    ribbon_texture_ = 0;
  }

  if (!config.endpoint_texture_path.empty() && loader.load_texture) {
    endpoint_texture_ = static_cast<std::uint32_t>(
        loader.load_texture(config.endpoint_texture_path, kEndpointPreviewTextureFilterFlags));
  } else {
    endpoint_texture_ = 0;
  }

  for (std::size_t slot = 0; slot < visual_objects_.size(); ++slot) {
    if (config.model_paths[slot].empty() || !loader.load_model_instance) {
      visual_objects_[slot] = 0;
      continue;
    }
    const MissileTrajectoryPreviewModelLoadResult model_result =
        loader.load_model_instance(config.model_paths[slot]);
    if (model_result.status != render::m2::M2ResultStatus::kReady ||
        model_result.instance_id == 0u) {
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                "MissileTrajectory: preview model load failed for " +
                    config.model_paths[slot] + " status=" +
                    render::m2::M2ResultStatusName(model_result.status) +
                    " reason=" + render::m2::M2ResultReasonName(model_result.reason) +
                    (model_result.detail.empty() ? std::string()
                                                 : " detail=" + model_result.detail));
      visual_objects_[slot] = 0;
      continue;
    }
    visual_objects_[slot] = model_result.instance_id;
  }

  source_unit_guid_ = config.source_unit_guid;
  last_spell_id_ = 0;
  input_refresh_latched_ = false;
  mode_start_time_ = 0;
  trajectory_create_time_ = config.current_time_ms;
}

MissileCollisionTrajectoryNode &UnitMissileTrajectory_C::CreateCollisionTrajectory(
    const MissileCollisionTrajectoryConfig &config,
    ObjectManager &object_manager) {
  if (!initialized_) {
    Initialize();
  }
  auto node = std::make_unique<MissileCollisionTrajectoryNode>();
  node->Configure(config);
  node->PopulateTargets(object_manager);
  collision_nodes_.insert(collision_nodes_.begin(), std::move(node));
  return *collision_nodes_.front();
}

bool UnitMissileTrajectory_C::DestroyCollisionTrajectory(
    const MissileCollisionTrajectoryNode *node) {
  const auto found = std::find_if(
      collision_nodes_.begin(), collision_nodes_.end(),
      [node](const auto &candidate) { return candidate.get() == node; });
  if (found == collision_nodes_.end()) {
    return false;
  }
  collision_nodes_.erase(found);
  return true;
}

void UnitMissileTrajectory_C::RemoveCollisionTarget(const CGUnit_C *unit) {
  for (const auto &node : collision_nodes_) {
    node->RemoveTarget(unit);
  }
}

void UnitMissileTrajectory_C::NotifyUnitCreated(CGUnit_C *unit) {
  if (unit == nullptr || unit->object_manager() == nullptr) {
    return;
  }
  const auto &object_manager = *unit->object_manager();
  for (const auto &node : collision_nodes_) {
    node->AddTargetIfEligible(object_manager, unit);
  }
}

std::size_t UnitMissileTrajectory_C::GetCollisionTrajectoryCount() const noexcept {
  return collision_nodes_.size();
}

bool UnitMissileTrajectory_C::PreviewParticleVisibilityCallback(std::size_t slot) {
  if (slot >= visual_particles_.size()) {
    return false;
  }

  if (alpha_ <= 0.0f && visual_particles_[slot] != 0) {
    if (render_node_destroy_fn_) {
      render_node_destroy_fn_(visual_particles_[slot]);
    }
    visual_particles_[slot] = 0;
  }

  return visual_particles_[slot] != 0;
}

void UnitMissileTrajectory_C::UpdateTrajectoryPreview(
    ObjectManager &object_manager) {
  const bool refresh_latched = input_refresh_latched_;
  input_refresh_latched_ = false;
  alpha_ = 0.0f;
  current_time_ms_ = core::GameClock::GetTickCount32();

  if (source_unit_guid_ == 0) {
    return;
  }

  CGUnit_C *source =
      object_manager.GetMutableUnit(ObjectGuid(source_unit_guid_));
  if (source == nullptr) {
    source_unit_guid_ = 0;
    return;
  }

  const auto *vehicle = source->Vehicle().GetVehicleEntry();
  const auto *dbc = source->dbc_loader();
  if (vehicle == nullptr || dbc == nullptr ||
      (vehicle->flags & kMissileTrajectoryPreviewFlagMask) == 0) {
    return;
  }

  std::uint32_t active_spell_id = source->Casts().GetCurrentCast().spell_id;
  if (active_spell_id == 0) {
    active_spell_id = source->Casts().GetChannelCast().spell_id;
  }
  const auto *active_spell =
      active_spell_id != 0 ? dbc->spell().LookupEntry(active_spell_id) : nullptr;
  const auto *active_missile =
      active_spell != nullptr
          ? dbc->spell_missile().LookupEntry(active_spell->spell_missile_id)
          : nullptr;

  bool mode_active = false;
  MissileTargetMode requested_mode = MissileTargetMode::Direct;
  if (active_spell != nullptr) {
    if ((vehicle->flags & 0x00800000u) != 0) {
      mode_active = true;
    } else if ((vehicle->flags & 0x01000000u) != 0 &&
               MissileTrajectory_HasAOETargetFlags(active_spell->attributes)) {
      requested_mode = MissileTargetMode::AOE;
      mode_active = true;
    } else if ((vehicle->flags & 0x02000000u) != 0 &&
               (refresh_latched || (active_spell->attributes & 0x0C00u) != 0)) {
      requested_mode = MissileTargetMode::Ranged;
      mode_active = true;
    } else if ((vehicle->flags & 0x04000000u) != 0 &&
               CanUseMissileTrajectoryPreview(*source)) {
      requested_mode = MissileTargetMode::Channeled;
      mode_active = true;
    } else if ((vehicle->flags & 0x08000000u) != 0 &&
               active_missile != nullptr) {
      mode_active = true;
    }
  }

  if (mode_active) {
    current_mode_ = requested_mode;
    mode_start_time_ = 0;
    alpha_ = 1.0f;
  } else {
    if (current_mode_ == MissileTargetMode::Direct) {
      mode_start_time_ = 0;
      return;
    }
    if (mode_start_time_ == 0) {
      if (last_spell_id_ == 0) {
        current_mode_ = MissileTargetMode::Direct;
        return;
      }
      mode_start_time_ = current_time_ms_ != 0 ? current_time_ms_ : 1u;
      alpha_ = 1.0f;
    } else {
      float lingering_seconds = vehicle->mssl_trgt_mouse_lingering;
      if (current_mode_ == MissileTargetMode::AOE) {
        lingering_seconds = vehicle->mssl_trgt_turn_lingering;
      } else if (current_mode_ == MissileTargetMode::Ranged) {
        lingering_seconds = vehicle->mssl_trgt_pitch_lingering;
      }
      const float fade_seconds =
          std::min(lingering_seconds * 0.5f, 0.3f);
      const auto hold_ms = static_cast<std::uint32_t>(std::lround(
          (lingering_seconds - fade_seconds) * 1000.0f));
      const auto fade_ms = static_cast<std::uint32_t>(
          std::lround(fade_seconds * 1000.0f));
      const std::uint32_t fade_start = mode_start_time_ + hold_ms;
      if (!HasLegacySignedTickReachedOrPassed(current_time_ms_, fade_start)) {
        alpha_ = 1.0f;
      } else {
        const std::uint32_t elapsed = current_time_ms_ - fade_start;
        alpha_ = fade_ms != 0
                     ? 1.0f - static_cast<float>(elapsed) /
                                  static_cast<float>(fade_ms)
                     : 0.0f;
        if (alpha_ <= 0.0f) {
          alpha_ = 0.0f;
          mode_start_time_ = 0;
          current_mode_ = MissileTargetMode::Direct;
          return;
        }
      }
    }
  }

  const std::uint32_t solve_spell_id =
      active_missile != nullptr ? active_spell_id : last_spell_id_;
  const auto *spell =
      solve_spell_id != 0 ? dbc->spell().LookupEntry(solve_spell_id) : nullptr;
  const auto *missile =
      spell != nullptr
          ? dbc->spell_missile().LookupEntry(spell->spell_missile_id)
          : nullptr;
  if (spell == nullptr || missile == nullptr) {
    alpha_ = 0.0f;
    return;
  }

  float pitch =
      (missile->default_pitch_min + missile->default_pitch_max) * 0.5f;
  float speed =
      (missile->default_speed_min + missile->default_speed_max) * 0.5f;
  if ((vehicle->flags & 0x10u) != 0) {
    pitch = source->GetFlyHeight();
    if ((vehicle->flags & 0x40u) != 0) {
      pitch = std::max(vehicle->pitch_min,
                       std::min(vehicle->pitch_max, pitch));
    }
  }

  pitch +=
      (missile->randomize_pitch_min + missile->randomize_pitch_max) * 0.5f;
  speed +=
      (missile->randomize_speed_min + missile->randomize_speed_max) * 0.5f;
  const float facing =
      source->GetWorldFacing() +
      (missile->randomize_facing_min + missile->randomize_facing_max) * 0.5f;
  const TrajectoryPoint origin = ResolveMissileLaunchPosition(*source, *spell, *dbc);

  const int step_count =
      missile->max_duration > 0.0001f
          ? std::min(static_cast<int>(missile->max_duration /
                                      kTrajectoryStepSeconds),
                     200)
          : 200;
  const float horizontal_step =
      speed * kTrajectoryStepSeconds * std::cos(pitch);
  const TrajectoryPoint nominal_destination{
      origin.x + static_cast<float>(step_count) * horizontal_step *
                     std::cos(facing),
      origin.y + static_cast<float>(step_count) * horizontal_step *
                     std::sin(facing),
      origin.z + static_cast<float>(step_count) * speed *
                     kTrajectoryStepSeconds * std::sin(pitch),
  };

  MissileCollisionTrajectoryNode collision_node;
  const MissileCollisionTrajectoryNode *collision = nullptr;
  if ((missile->flags & 0x3Cu) != 0) {
    collision_node.Configure({
        .source = origin,
        .destination = nominal_destination,
        .pitch_radians = pitch,
        .speed = speed,
        .spell_missile = missile,
        .excluded_unit = source,
    });
    collision_node.PopulateTargets(object_manager);
    collision = &collision_node;
  }

  const MissileTrajectorySolveResult solved = CalculateMissileTrajectoryArc({
      .origin = origin,
      .facing_radians = facing,
      .pitch_radians = pitch,
      .speed = speed,
      .gravity = missile->gravity,
      .max_duration_seconds = missile->max_duration,
      .collision_node = collision,
  }, world_intersection_);
  if (!solved.valid) {
    alpha_ = 0.0f;
    return;
  }

  trajectory_origin_ = solved.origin;
  SetTrajectoryPoints(solved.points);
  source_facing_radians_ = source->GetWorldFacing();
  render_style_ = {
      .ribbon_tip_alpha_scale = vehicle->mssl_trgt_end_opacity,
      .ribbon_scroll_speed = vehicle->mssl_trgt_arc_speed,
      .ribbon_u_step = vehicle->mssl_trgt_arc_repeat,
      .ribbon_width = vehicle->mssl_trgt_arc_width,
      .endpoint_half_extent = vehicle->mssl_trgt_impact_tex_radius,
  };

  auto* const m2 = source->m2_system();
  if (m2 == nullptr) {
    alpha_ = 0.0f;
    return;
  }
  constexpr float kRadiansToDegrees = 57.29577951308232f;
  for (std::size_t slot = 0; slot < visual_objects_.size(); ++slot) {
    const std::uint32_t instance_id = visual_objects_[slot];
    if (instance_id == 0) {
      continue;
    }

    float scale = vehicle->mssl_trgt_impact_radius[slot];
    const auto spatial = m2->QueryInstanceSpatialInfo(instance_id);
    if (spatial.status == render::m2::M2ResultStatus::kReady &&
        spatial.spatial.local_bounding_sphere[3] > 0.0f) {
      scale /= spatial.spatial.local_bounding_sphere[3];
    }
    (void)m2->SetTransform(
        instance_id,
        render::RenderVec3{solved.impact.x, solved.impact.y, solved.impact.z},
        render::RenderVec3{0.0f, 0.0f, source_facing_radians_ * kRadiansToDegrees},
        scale);
    (void)m2->SetAlpha(instance_id, alpha_);
    (void)m2->SetEffectEmittersEnabled(instance_id, alpha_ > 0.0f);
  }

  last_spell_id_ = solve_spell_id;
}

void UnitMissileTrajectory_C::SetTrajectoryPoints(const std::vector<TrajectoryPoint> &points) {
  const std::size_t count = std::min(points.size(), trajectory_points_.size());
  std::fill(trajectory_points_.begin(), trajectory_points_.end(), TrajectoryPoint{});
  std::copy_n(points.begin(), count, trajectory_points_.begin());
  trajectory_point_count_ = static_cast<std::uint32_t>(count);
}

void UnitMissileTrajectory_C::RenderMissileArc() {
  MissileArcRenderInputs inputs{};
  inputs.alpha = alpha_;
  inputs.source_unit_guid = source_unit_guid_;
  inputs.source_facing_radians = source_facing_radians_;
  inputs.current_time_ms = current_time_ms_;
  inputs.trajectory_create_time_ms = trajectory_create_time_;
  inputs.trajectory_origin = trajectory_origin_;
  inputs.ribbon_texture_handle = ribbon_texture_;
  inputs.endpoint_texture_handle = endpoint_texture_;
  inputs.ribbon_texture_path = ribbon_texture_path_;
  inputs.endpoint_texture_path = endpoint_texture_path_;
  inputs.style = render_style_;
  inputs.trajectory_points.assign(trajectory_points_.begin(),
                                  trajectory_points_.begin() + trajectory_point_count_);

  render_snapshot_ = BuildMissileArcRenderSnapshot(inputs);
}

MissileCollisionSphere BuildMissileCollisionSphere(
    const TrajectoryPoint &unit_position, const float unit_scale,
    const data::dbc::CreatureModelDataEntry *model,
    const std::array<float, 2> &trajectory_direction,
    const float spell_collision_radius) noexcept {
  MissileCollisionSphere result{
      .x = unit_position.x,
      .y = unit_position.y,
      .z = unit_position.z,
      .radius = 1.0f,
  };

  if (model != nullptr) {
    result.radius = model->missile_collision_radius <= 0.0f
                        ? model->collision_height * 0.5f
                        : model->missile_collision_radius * unit_scale;
    const float push = model->missile_collision_push * unit_scale;
    result.x -= push * trajectory_direction[0];
    result.y -= push * trajectory_direction[1];
    result.z += model->missile_collision_raise * unit_scale;
  }

  result.z += result.radius;
  result.radius += spell_collision_radius;
  return result;
}

MissileCollisionSphere BuildUnitMissileCollisionSphere(
    const CGUnit_C &unit,
    const std::array<float, 2> &trajectory_direction,
    const float spell_collision_radius) {
  return BuildMissileCollisionSphere(
      ResolveAppliedUnitPosition(unit), unit.GetScale(),
      ResolveUnitCreatureModelData(unit), trajectory_direction,
      spell_collision_radius);
}

float IntersectMissileCollisionSpheres(
    const TrajectoryPoint &segment_start,
    const TrajectoryPoint &segment_delta,
    const std::span<const MissileCollisionSphere> spheres) noexcept {
  const float segment_length = std::sqrt(
      segment_delta.x * segment_delta.x + segment_delta.y * segment_delta.y +
      segment_delta.z * segment_delta.z);
  if (segment_length < kMinimumSegmentLength) {
    return -1.0f;
  }

  const float inverse_length = 1.0f / segment_length;
  for (const MissileCollisionSphere &sphere : spheres) {
    if (!(sphere.radius > 0.0f)) {
      break;
    }

    const float offset_x = sphere.x - segment_start.x;
    const float offset_y = sphere.y - segment_start.y;
    const float offset_z = sphere.z - segment_start.z;
    const float projected_distance =
        (offset_x * segment_delta.x + offset_y * segment_delta.y +
         offset_z * segment_delta.z) *
        inverse_length;
    const float perpendicular_distance_squared =
        offset_x * offset_x + offset_y * offset_y + offset_z * offset_z -
        projected_distance * projected_distance;
    if (projected_distance >= 0.0f && projected_distance <= segment_length &&
        perpendicular_distance_squared <= sphere.radius * sphere.radius) {
      return projected_distance / segment_length;
    }
  }
  return -1.0f;
}

float ScoreMissileCollisionTarget(
    const MissileCollisionTargetScoreInputs &inputs) noexcept {
  if (inputs.target.radius < kMinimumTargetRadius ||
      inputs.horizontal_speed <= inputs.model_max_extent) {
    return -1.0f;
  }

  const float destination_offset_x =
      inputs.destination.x - inputs.target.x;
  const float destination_offset_y =
      inputs.destination.y - inputs.target.y;
  const float perpendicular_distance = std::fabs(
      destination_offset_x * inputs.perpendicular[0] +
      destination_offset_y * inputs.perpendicular[1]);
  const float projection =
      (perpendicular_distance * inputs.perpendicular[0] + inputs.target.x -
       inputs.source.x) *
          inputs.direction[0] +
      (perpendicular_distance * inputs.perpendicular[1] + inputs.target.y -
       inputs.source.y) *
          inputs.direction[1];

  float cone_score = -1.0f;
  if (projection < kMinimumTargetRadius) {
    const float source_offset_x = inputs.source.x - inputs.target.x;
    const float source_offset_y = inputs.source.y - inputs.target.y;
    cone_score =
        (inputs.target.radius * inputs.target.radius -
         (source_offset_x * source_offset_x + source_offset_y * source_offset_y)) /
        kMinimumTargetRadius;
  } else if (projection < inputs.distance) {
    const float widened_radius =
        (projection / inputs.distance) * inputs.time_to_target *
            inputs.model_max_extent +
        inputs.target.radius;
    cone_score =
        (widened_radius * widened_radius -
         perpendicular_distance * perpendicular_distance) /
        projection;
  }

  const float outer_radius =
      inputs.model_max_extent * inputs.time_to_target + inputs.target.radius;
  const float outer_score =
      (outer_radius * outer_radius -
       (destination_offset_x * destination_offset_x +
        destination_offset_y * destination_offset_y)) /
      inputs.distance;
  if (outer_score > 0.0f && outer_score > cone_score) {
    return outer_score;
  }
  return cone_score > 0.0f ? cone_score : -1.0f;
}

void MissileCollisionTrajectoryNode::Configure(
    const MissileCollisionTrajectoryConfig &config) {
  targets_.fill(nullptr);
  source_x_ = config.source.x;
  source_y_ = config.source.y;
  dest_x_ = config.destination.x;
  dest_y_ = config.destination.y;
  direction_x_ = dest_x_ - source_x_;
  direction_y_ = dest_y_ - source_y_;

  if (std::fabs(direction_x_) >= 0.001f || std::isnan(direction_x_) ||
      std::fabs(direction_y_) >= 0.001f) {
    distance_ = std::sqrt(direction_x_ * direction_x_ +
                          direction_y_ * direction_y_);
    const float inverse_distance = 1.0f / distance_;
    direction_x_ *= inverse_distance;
    direction_y_ *= inverse_distance;
    horizontal_speed_ =
        RetailHorizontalMissileSpeed(config.pitch_radians, config.speed);
  } else {
    const float distance_3d = std::sqrt(
        (config.destination.x - config.source.x) *
            (config.destination.x - config.source.x) +
        (config.destination.y - config.source.y) *
            (config.destination.y - config.source.y) +
        (config.destination.z - config.source.z) *
            (config.destination.z - config.source.z));
    horizontal_speed_ = config.speed > kMinimumTargetRadius
                            ? 0.01f / (distance_3d / config.speed)
                            : 0.01f;
    direction_x_ = 1.0f;
    direction_y_ = 0.0f;
    distance_ = 1.0f;
    source_x_ -= 0.01f;
  }

  perp_x_ = -direction_y_;
  perp_y_ = direction_x_;
  time_to_target_ = distance_ / horizontal_speed_;
  spell_missile_ = config.spell_missile;
  excluded_unit_ = config.excluded_unit;
}

float MissileCollisionTrajectoryNode::EvaluateTargetPriority(
    const ObjectManager &object_manager, const CGUnit_C *target) const {
  if (target == nullptr || spell_missile_ == nullptr ||
      IsExcludedVehicleBranch(*target, excluded_unit_)) {
    return -1.0f;
  }

  const bool is_player = target->IsPlayer();
  const std::uint32_t relationship_mask =
      spell_missile_->flags & (is_player ? 0x0Cu : 0x30u);
  const std::uint32_t both_relationships = is_player ? 0x0Cu : 0x30u;
  const std::uint32_t attackable_bit = is_player ? 0x04u : 0x10u;
  if (relationship_mask == 0) {
    return -1.0f;
  }

  if (relationship_mask != both_relationships) {
    const CGUnit_C *active_player = object_manager.GetActivePlayer();
    if (active_player == nullptr ||
        active_player->Interaction().CanAttackSpellTarget(*target) !=
            ((relationship_mask & attackable_bit) != 0)) {
      return -1.0f;
    }
  }

  const std::optional<float> maximum_extent =
      ResolveUnitModelMaximumExtent(*target);
  if (!maximum_extent.has_value()) {
    return -1.0f;
  }

  const MissileCollisionSphere sphere = BuildUnitMissileCollisionSphere(
      *target, {direction_x_, direction_y_}, spell_missile_->collision_radius);

  return ScoreMissileCollisionTarget({
      .source = {source_x_, source_y_, 0.0f},
      .destination = {dest_x_, dest_y_, 0.0f},
      .direction = {direction_x_, direction_y_},
      .perpendicular = {perp_x_, perp_y_},
      .distance = distance_,
      .time_to_target = time_to_target_,
      .horizontal_speed = horizontal_speed_,
      .target = sphere,
      .model_max_extent = *maximum_extent,
  });
}

void MissileCollisionTrajectoryNode::PopulateTargets(
    ObjectManager &object_manager) {
  struct Candidate {
    CGUnit_C *unit = nullptr;
    float priority = -1.0f;
    std::uint64_t guid = 0;
  };

  std::vector<Candidate> candidates;
  candidates.reserve(kMaxEnumeratedTargets);
  object_manager.ForEachUnit(
      [this, &object_manager, &candidates](const ObjectGuid &guid,
                                           CGUnit_C &unit) {
        if (candidates.size() >= kMaxEnumeratedTargets) {
          return;
        }
        const float priority = EvaluateTargetPriority(object_manager, &unit);
        if (priority != -1.0f) {
          candidates.push_back({&unit, priority, guid.GetRawValue()});
        }
      });

  if (candidates.size() >= kMaxCollisionTargets + 1u) {
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &left, const Candidate &right) {
                if (left.priority != right.priority) {
                  return left.priority > right.priority;
                }
                return left.guid < right.guid;
              });
  }

  targets_.fill(nullptr);
  const std::size_t count =
      std::min(candidates.size(), kMaxCollisionTargets);
  for (std::size_t index = 0; index < count; ++index) {
    targets_[index] = candidates[index].unit;
  }
}

bool MissileCollisionTrajectoryNode::AddTargetIfEligible(
    const ObjectManager &object_manager, CGUnit_C *target) {
  if (target == nullptr ||
      EvaluateTargetPriority(object_manager, target) == -1.0f) {
    return false;
  }
  const std::size_t count = GetTargetCount();
  if (count >= kMaxCollisionTargets) {
    return false;
  }
  targets_[count] = target;
  targets_[count + 1] = nullptr;
  return true;
}

void MissileCollisionTrajectoryNode::RemoveTarget(const CGUnit_C *target) {
  const std::size_t count = GetTargetCount();
  const auto found =
      std::find(targets_.begin(), targets_.begin() + count, target);
  if (found == targets_.begin() + count) {
    return;
  }
  std::move(found + 1, targets_.begin() + count + 1, found);
}

CGUnit_C *MissileCollisionTrajectoryNode::GetTarget(
    const std::size_t index) const {
  return index <= kMaxCollisionTargets ? targets_[index] : nullptr;
}

std::size_t MissileCollisionTrajectoryNode::GetTargetCount() const noexcept {
  return static_cast<std::size_t>(
      std::find(targets_.begin(), targets_.begin() + kMaxCollisionTargets,
                nullptr) -
      targets_.begin());
}

std::size_t MissileCollisionTrajectoryNode::BuildTargetSpheres(
    const std::span<MissileCollisionSphere> out) const {
  if (out.empty()) {
    return 0;
  }

  const std::size_t count =
      std::min(GetTargetCount(), out.size() - 1);
  const float collision_radius =
      spell_missile_ != nullptr ? spell_missile_->collision_radius : 0.0f;
  for (std::size_t index = 0; index < count; ++index) {
    out[index] = BuildUnitMissileCollisionSphere(
        *targets_[index], {direction_x_, direction_y_}, collision_radius);
  }
  out[count] = {};
  return count;
}

float MissileCollisionTrajectoryNode::IntersectSegment(
    const TrajectoryPoint &start, const TrajectoryPoint &end) const {
  std::array<MissileCollisionSphere, kMaxCollisionTargets + 1> spheres{};
  const std::size_t count = BuildTargetSpheres(spheres);
  return IntersectMissileCollisionSpheres(
      start, {end.x - start.x, end.y - start.y, end.z - start.z},
      std::span<const MissileCollisionSphere>(spheres.data(), count + 1));
}

MissileTrajectorySolveResult CalculateMissileTrajectoryArc(
    const MissileTrajectorySolveInputs &inputs,
    const MissileWorldIntersectionFn &world_intersection) {
  MissileTrajectorySolveResult result{
      .origin = inputs.origin,
      .impact = inputs.origin,
      .pitch_radians = inputs.pitch_radians,
      .speed = inputs.speed,
  };

  if (!std::isfinite(inputs.origin.x) || !std::isfinite(inputs.origin.y) ||
      !std::isfinite(inputs.origin.z) ||
      !std::isfinite(inputs.facing_radians) ||
      !std::isfinite(inputs.pitch_radians) || !std::isfinite(inputs.speed) ||
      !std::isfinite(inputs.gravity) ||
      !std::isfinite(inputs.max_duration_seconds)) {
    return result;
  }

  const float pitch_cosine = std::cos(inputs.pitch_radians);
  TrajectoryPoint step{
      .x = inputs.speed * kTrajectoryStepSeconds * pitch_cosine *
           std::cos(inputs.facing_radians),
      .y = inputs.speed * kTrajectoryStepSeconds * pitch_cosine *
           std::sin(inputs.facing_radians),
      .z = inputs.speed * kTrajectoryStepSeconds *
               std::sin(inputs.pitch_radians) -
           inputs.gravity * kTrajectoryStepSeconds * kTrajectoryStepSeconds *
               0.5f,
  };
  const float gravity_step =
      inputs.gravity * kTrajectoryStepSeconds * kTrajectoryStepSeconds;

  int retail_step_count = 200;
  if (inputs.max_duration_seconds > 0.0001f) {
    retail_step_count = std::min(
        static_cast<int>(inputs.max_duration_seconds / kTrajectoryStepSeconds),
        200);
  }
  const int iteration_count = std::max(retail_step_count, 1);
  result.points.reserve(static_cast<std::size_t>(iteration_count));

  std::array<MissileCollisionSphere,
             MissileCollisionTrajectoryNode::kMaxCollisionTargets + 1>
      collision_spheres{};
  std::size_t collision_sphere_count = 0;
  if (inputs.collision_node != nullptr) {
    collision_sphere_count =
        inputs.collision_node->BuildTargetSpheres(collision_spheres);
  }

  TrajectoryPoint current = inputs.origin;
  for (int index = 0; index < iteration_count; ++index) {
    const TrajectoryPoint next{
        current.x + step.x,
        current.y + step.y,
        current.z + step.z,
    };

    const auto world_hit =
        world_intersection
            ? world_intersection(current, next,
                                 kMissileTrajectoryWorldIntersectFlags)
            : std::nullopt;
    float hit_fraction = -1.0f;
    TrajectoryPoint hit{};
    if (world_hit.has_value()) {
      hit = *world_hit;
      const float segment_length = std::sqrt(
          step.x * step.x + step.y * step.y + step.z * step.z);
      const float hit_distance = std::sqrt(
          (hit.x - current.x) * (hit.x - current.x) +
          (hit.y - current.y) * (hit.y - current.y) +
          (hit.z - current.z) * (hit.z - current.z));
      hit_fraction = segment_length > 0.0f
                         ? std::clamp(hit_distance / segment_length, 0.0f, 1.0f)
                         : 0.0f;
    } else if (collision_sphere_count != 0) {
      hit_fraction = IntersectMissileCollisionSpheres(
          current, step,
          std::span<const MissileCollisionSphere>(
              collision_spheres.data(), collision_sphere_count + 1));
      if (hit_fraction != -1.0f) {
        hit = {
            current.x + hit_fraction * step.x,
            current.y + hit_fraction * step.y,
            current.z + hit_fraction * step.z,
        };
      }
    }

    if (hit_fraction != -1.0f) {
      result.impact = hit;
      result.travel_time_seconds =
          (static_cast<float>(index) + hit_fraction) *
          kTrajectoryStepSeconds;
      result.points.push_back(hit);
      result.valid = true;
      return result;
    }

    if (index + 1 == iteration_count) {
      result.impact = next;
      result.travel_time_seconds =
          static_cast<float>(retail_step_count) * kTrajectoryStepSeconds;
      result.points.push_back(next);
      result.valid = true;
      return result;
    }

    result.points.push_back(next);
    current = next;
    step.z -= gravity_step;
  }
  return result;
}

}

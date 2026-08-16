
#pragma once

#include "openwow/game/objects/unit/unit_cast_runtime.h"
#include "openwow/foundation/hashing/retail_adler_seed.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/render/m2/m2_public_types.h"

namespace openwow::render::m2 {
class M2System;
}

namespace openwow::game {

class CGUnit_C;
class MissileCollisionTrajectoryNode;
class ObjectManager;
struct MissileCollisionTrajectoryConfig;
struct SpellVisualRecord;

}

namespace openwow::data::dbc {

struct CreatureModelDataEntry;
struct SpellEntry;
struct SpellMissileEntry;

}

namespace openwow::game {

enum class MissileTargetMode : std::uint32_t {
  Direct = 0,

  AOE = 1,

  Ranged = 2,

  Channeled = 3,

};

struct MissileTargetEntry {
  CGUnit_C *unit = nullptr;
  float priority = -1.0f;
};

struct TrajectoryPoint {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct MissileArcRenderStyle {
  float ribbon_tip_alpha_scale = 1.0f;
  float ribbon_scroll_speed = 0.0f;
  float ribbon_u_step = 0.0f;
  float ribbon_width = 0.0f;
  float endpoint_half_extent = 0.0f;
};

struct MissileArcVertex {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  std::uint32_t color_argb = 0;
  float u = 0.0f;
  float v = 0.0f;
};

struct MissileArcRibbonSnapshot {
  std::uint32_t texture_handle = 0;
  std::string texture_path;
  std::vector<MissileArcVertex> vertices;
  std::vector<std::uint16_t> indices;
};

struct MissileArcEndpointProjectionSnapshot {
  std::uint32_t texture_handle = 0;
  std::string texture_path;
  std::array<float, 3> min_corner{};
  std::array<float, 3> max_corner{};
  std::uint32_t color_argb = 0;
  std::array<float, 16> rotation_matrix{};
};

struct MissileArcRenderSnapshot {
  bool active = false;
  std::optional<MissileArcRibbonSnapshot> ribbon;
  std::optional<MissileArcEndpointProjectionSnapshot> endpoint;
};

struct MissileArcRenderInputs {
  float alpha = 0.0f;
  std::uint64_t source_unit_guid = 0;
  float source_facing_radians = 0.0f;
  std::uint32_t current_time_ms = 0;
  std::uint32_t trajectory_create_time_ms = 0;
  TrajectoryPoint trajectory_origin{};
  std::vector<TrajectoryPoint> trajectory_points;
  std::uint32_t ribbon_texture_handle = 0;
  std::uint32_t endpoint_texture_handle = 0;
  std::string ribbon_texture_path;
  std::string endpoint_texture_path;
  MissileArcRenderStyle style{};
};

[[nodiscard]] MissileArcRenderSnapshot
BuildMissileArcRenderSnapshot(const MissileArcRenderInputs &inputs);

inline constexpr std::size_t kDelayedMissileTrajectoryPayloadSize = 0x2A8u;

using DelayedMissileTrajectoryPayload =
    std::array<std::uint8_t, kDelayedMissileTrajectoryPayloadSize>;

struct DelayedMissileTrajectoryGateResult {
  bool ready = false;
  std::uint32_t deadline_tick_ms = 0;
};

enum class DelayedMissileTrajectoryPayloadDecodeMode : std::uint8_t {
  Raw,
  LegacyRunLength,
};

struct DelayedMissileTrajectoryPayloadDecodeResult {
  DelayedMissileTrajectoryPayload payload{};
  bool complete = false;
  std::size_t bytes_read = 0;
  std::size_t bytes_written = 0;
};

[[nodiscard]] DelayedMissileTrajectoryGateResult
EvaluateDelayedMissileTrajectoryPayloadGate(
    const DelayedMissileTrajectoryState &state,
    std::uint32_t current_tick_ms) noexcept;

[[nodiscard]] DelayedMissileTrajectoryPayloadDecodeResult
DecodeDelayedMissileTrajectoryPayload(
    std::span<const std::uint8_t> encoded,
    DelayedMissileTrajectoryPayloadDecodeMode mode);

void SetDelayedMissileTrajectoryActiveCastCount(
    DelayedMissileTrajectoryState &state,
    std::uint8_t cast_count) noexcept;

struct MissileTrajectoryPreviewResourceConfig {
  std::uint32_t spell_visual_flags = 0;
  std::uint64_t source_unit_guid = 0;
  std::uint32_t current_time_ms = 0;
  std::string ribbon_texture_path;
  std::string endpoint_texture_path;
  std::array<std::string, 2> model_paths{};
};

enum class SpellMissileCastTrajectoryStatus : std::uint8_t {
  kNotRequired,
  kReady,
  kFailed,
};

struct SpellMissileCastTrajectoryResult {
  SpellMissileCastTrajectoryStatus status =
      SpellMissileCastTrajectoryStatus::kNotRequired;
  TrajectoryPoint source{};
  TrajectoryPoint destination{};
  float pitch_radians = 0.0f;
  float speed = 0.0f;
};

struct MissileTrajectoryPreviewModelLoadResult {
  render::m2::M2ResultStatus status = render::m2::M2ResultStatus::kNotReady;
  render::m2::M2ResultReason reason = render::m2::M2ResultReason::kNone;
  std::string detail;
  std::uint32_t instance_id = 0;
};

struct MissileTrajectoryPreviewResourceLoader {
  using TextureLoadFn = std::function<std::uintptr_t(std::string_view path, int filter_flags)>;
  using TextureReleaseFn = std::function<void(std::uintptr_t handle)>;
  using ModelInstanceLoadFn =
      std::function<MissileTrajectoryPreviewModelLoadResult(std::string_view path)>;
  using ModelInstanceReleaseFn = std::function<void(std::uint32_t handle)>;

  TextureLoadFn load_texture;
  TextureReleaseFn release_texture;
  ModelInstanceLoadFn load_model_instance;
  ModelInstanceReleaseFn release_model_instance;
};

using MissileWorldIntersectionFn = std::function<std::optional<TrajectoryPoint>(
    const TrajectoryPoint &start, const TrajectoryPoint &end,
    std::uint32_t flags)>;

class UnitMissileTrajectory_C {
public:
  static constexpr std::size_t kMaxTrajectoryPoints = 200;

  UnitMissileTrajectory_C() = default;
  ~UnitMissileTrajectory_C();
  UnitMissileTrajectory_C(const UnitMissileTrajectory_C &) = delete;
  UnitMissileTrajectory_C &operator=(const UnitMissileTrajectory_C &) = delete;

  void Initialize();

  void Cleanup();

  void ClearPreviewResources();
  void ClearPreviewResources(const MissileTrajectoryPreviewResourceLoader &loader);
  void LoadPreviewResources(
      const MissileTrajectoryPreviewResourceConfig &config,
      openwow::render::m2::M2System& m2_system);
  void LoadPreviewResources(const MissileTrajectoryPreviewResourceConfig &config,
                            const MissileTrajectoryPreviewResourceLoader &loader);
  void BindWorldIntersection(MissileWorldIntersectionFn world_intersection) {
    world_intersection_ = std::move(world_intersection);
  }

  [[nodiscard]] SpellMissileCastTrajectoryResult PrepareSpellCastTrajectory(
      const CGUnit_C& caster, std::uint32_t spell_id,
      const ObjectManager& object_manager,
      foundation::hashing::AdlerSeedState& random_state) const;

  MissileCollisionTrajectoryNode &
  CreateCollisionTrajectory(const MissileCollisionTrajectoryConfig &config,
                            ObjectManager &object_manager);
  bool DestroyCollisionTrajectory(const MissileCollisionTrajectoryNode *node);
  void RemoveCollisionTarget(const CGUnit_C *unit);
  void NotifyUnitCreated(CGUnit_C *unit);
  [[nodiscard]] std::size_t GetCollisionTrajectoryCount() const noexcept;

  void UpdateTrajectoryPreview(ObjectManager &object_manager);

  [[nodiscard]] bool PreviewParticleVisibilityCallback(std::size_t slot);

  void RenderMissileArc();

  [[nodiscard]] bool IsActive() const {
    return alpha_ > 0.0f;
  }
  [[nodiscard]] float GetAlpha() const {
    return alpha_;
  }
  [[nodiscard]] MissileTargetMode GetCurrentMode() const {
    return current_mode_;
  }
  [[nodiscard]] std::uint32_t GetLastSpellId() const {
    return last_spell_id_;
  }
  void SetLastSpellId(std::uint32_t spell_id) { last_spell_id_ = spell_id; }
  [[nodiscard]] bool IsInputRefreshLatched() const {
    return input_refresh_latched_;
  }
  [[nodiscard]] std::uint32_t GetTrajectoryPointCount() const {
    return trajectory_point_count_;
  }
  [[nodiscard]] std::uint32_t GetTrajectoryCreateTime() const {
    return trajectory_create_time_;
  }
  [[nodiscard]] std::uint32_t GetRibbonTextureHandle() const {
    return ribbon_texture_;
  }
  [[nodiscard]] std::uint32_t GetEndpointTextureHandle() const {
    return endpoint_texture_;
  }
  [[nodiscard]] const std::string& GetRibbonTexturePath() const noexcept {
    return ribbon_texture_path_;
  }
  [[nodiscard]] const std::string& GetEndpointTexturePath() const noexcept {
    return endpoint_texture_path_;
  }
  [[nodiscard]] std::uint32_t GetVisualObjectHandle(std::size_t slot) const {
    return visual_objects_.at(slot);
  }
  [[nodiscard]] std::uint32_t GetVisualParticleHandle(std::size_t slot) const {
    return visual_particles_.at(slot);
  }
  [[nodiscard]] const MissileArcRenderSnapshot &GetRenderSnapshot() const {
    return render_snapshot_;
  }

  void SetSourceUnit(std::uint64_t guid) {
    source_unit_guid_ = guid;
  }
  [[nodiscard]] std::uint64_t GetSourceUnit() const {
    return source_unit_guid_;
  }
  void ClearSourceUnit() {
    source_unit_guid_ = 0;
  }
  void LatchInputRefresh() {
    input_refresh_latched_ = true;
  }

  void SetAlpha(float alpha) {
    alpha_ = alpha;
  }
  void SetTrajectoryCreateTime(std::uint32_t time_ms) {
    trajectory_create_time_ = time_ms;
  }
  void SetCurrentTime(std::uint32_t time_ms) {
    current_time_ms_ = time_ms;
  }
  void SetSourceFacingRadians(float radians) {
    source_facing_radians_ = radians;
  }
  void SetTrajectoryOrigin(const TrajectoryPoint &origin) {
    trajectory_origin_ = origin;
  }
  void SetTrajectoryPoints(const std::vector<TrajectoryPoint> &points);
  void SetRenderStyle(const MissileArcRenderStyle &style) {
    render_style_ = style;
  }
  void SetRibbonTextureHandle(std::uint32_t handle) {
    ribbon_texture_ = handle;
    preview_texture_release_ = nullptr;
  }
  void SetEndpointTextureHandle(std::uint32_t handle) {
    endpoint_texture_ = handle;
    preview_texture_release_ = nullptr;
  }

  using RenderNodeDestroyFn = std::function<void(std::uint32_t handle)>;
  void SetRenderNodeDestroyFn(RenderNodeDestroyFn fn) {
    render_node_destroy_fn_ = std::move(fn);
  }

private:

  std::uint64_t source_unit_guid_{0};

  float alpha_{0.0f};

  MissileTargetMode current_mode_{MissileTargetMode::Direct};

  std::uint32_t mode_start_time_{0};

  std::uint32_t last_spell_id_{0};

  bool input_refresh_latched_{false};

  std::uint32_t trajectory_create_time_{0};

  std::uint32_t current_time_ms_{0};

  std::array<TrajectoryPoint, kMaxTrajectoryPoints> trajectory_points_{};
  std::uint32_t trajectory_point_count_{0};

  TrajectoryPoint trajectory_origin_{};

  std::array<std::uint32_t, 2> visual_objects_{};
  std::array<std::uint32_t, 2> visual_particles_{};

  std::uint32_t ribbon_texture_{0};
  std::uint32_t endpoint_texture_{0};
  std::string ribbon_texture_path_;
  std::string endpoint_texture_path_;
  MissileTrajectoryPreviewResourceLoader::TextureReleaseFn preview_texture_release_{};
  MissileTrajectoryPreviewResourceLoader::ModelInstanceReleaseFn preview_model_release_{};

  float source_facing_radians_{0.0f};
  MissileArcRenderStyle render_style_{};
  MissileArcRenderSnapshot render_snapshot_{};

  std::vector<std::unique_ptr<MissileCollisionTrajectoryNode>> collision_nodes_;
  MissileWorldIntersectionFn world_intersection_;

  bool initialized_{false};

  RenderNodeDestroyFn render_node_destroy_fn_{};
};

struct MissileCollisionSphere {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float radius = 0.0f;
};

struct MissileCollisionTargetScoreInputs {
  TrajectoryPoint source{};
  TrajectoryPoint destination{};
  std::array<float, 2> direction{};
  std::array<float, 2> perpendicular{};
  float distance = 0.0f;
  float time_to_target = 0.0f;
  float horizontal_speed = 0.0f;
  MissileCollisionSphere target{};
  float model_max_extent = 0.0f;
};

struct MissileCollisionTrajectoryConfig {
  TrajectoryPoint source{};
  TrajectoryPoint destination{};
  float pitch_radians = 0.0f;
  float speed = 0.0f;
  const data::dbc::SpellMissileEntry *spell_missile = nullptr;
  const CGUnit_C *excluded_unit = nullptr;
};

[[nodiscard]] MissileCollisionSphere BuildMissileCollisionSphere(
    const TrajectoryPoint &unit_position, float unit_scale,
    const data::dbc::CreatureModelDataEntry *model,
    const std::array<float, 2> &trajectory_direction,
    float spell_collision_radius) noexcept;

[[nodiscard]] MissileCollisionSphere
BuildUnitMissileCollisionSphere(
    const CGUnit_C &unit,
    const std::array<float, 2> &trajectory_direction,
    float spell_collision_radius);

[[nodiscard]] float IntersectMissileCollisionSpheres(
    const TrajectoryPoint &segment_start,
    const TrajectoryPoint &segment_delta,
    std::span<const MissileCollisionSphere> spheres) noexcept;

[[nodiscard]] float ScoreMissileCollisionTarget(
    const MissileCollisionTargetScoreInputs &inputs) noexcept;

class MissileCollisionTrajectoryNode {
public:
  static constexpr std::size_t kMaxEnumeratedTargets = 128;
  static constexpr std::size_t kMaxCollisionTargets = 15;

  MissileCollisionTrajectoryNode() = default;

  void Configure(const MissileCollisionTrajectoryConfig &config);
  void PopulateTargets(ObjectManager &object_manager);

  [[nodiscard]] float EvaluateTargetPriority(
      const ObjectManager &object_manager, const CGUnit_C *target) const;
  bool AddTargetIfEligible(const ObjectManager &object_manager,
                           CGUnit_C *target);
  void RemoveTarget(const CGUnit_C *target);

  [[nodiscard]] std::size_t BuildTargetSpheres(
      std::span<MissileCollisionSphere> out) const;
  [[nodiscard]] float IntersectSegment(
      const TrajectoryPoint &start, const TrajectoryPoint &end) const;

  [[nodiscard]] CGUnit_C *GetTarget(std::size_t index) const;
  [[nodiscard]] std::size_t GetTargetCount() const noexcept;
  [[nodiscard]] float GetDirectionX() const { return direction_x_; }
  [[nodiscard]] float GetDirectionY() const { return direction_y_; }
  [[nodiscard]] float GetDistance() const { return distance_; }
  [[nodiscard]] float GetTimeToTarget() const { return time_to_target_; }
  [[nodiscard]] float GetHorizontalSpeed() const { return horizontal_speed_; }
  [[nodiscard]] const data::dbc::SpellMissileEntry *GetSpellMissileRecord() const {
    return spell_missile_;
  }

private:
  std::array<CGUnit_C *, kMaxCollisionTargets + 1> targets_{};
  float source_x_ = 0.0f;
  float source_y_ = 0.0f;
  float dest_x_ = 0.0f;
  float dest_y_ = 0.0f;

  float direction_x_ = 0.0f;
  float direction_y_ = 0.0f;

  float perp_x_ = 0.0f;
  float perp_y_ = 0.0f;
  float distance_ = 0.0f;
  float time_to_target_ = 0.0f;
  float horizontal_speed_ = 0.0f;
  const data::dbc::SpellMissileEntry *spell_missile_ = nullptr;
  const CGUnit_C *excluded_unit_ = nullptr;
};

struct MissileTrajectorySolveInputs {
  TrajectoryPoint origin{};
  float facing_radians = 0.0f;
  float pitch_radians = 0.0f;
  float speed = 0.0f;
  float gravity = 0.0f;
  float max_duration_seconds = 0.0f;
  const MissileCollisionTrajectoryNode *collision_node = nullptr;
};

struct MissileTrajectorySolveResult {
  bool valid = false;
  TrajectoryPoint origin{};
  TrajectoryPoint impact{};
  float pitch_radians = 0.0f;
  float speed = 0.0f;
  float travel_time_seconds = 0.0f;
  std::vector<TrajectoryPoint> points;
};

[[nodiscard]] MissileTrajectorySolveResult CalculateMissileTrajectoryArc(
    const MissileTrajectorySolveInputs &inputs,
    const MissileWorldIntersectionFn &world_intersection = {});

[[nodiscard]] bool MissileTrajectory_HasAOETargetFlags(std::uint32_t flags);

}

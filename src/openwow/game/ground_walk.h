#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace openwow::game {

struct Float2 {
  float x{0.0f};
  float y{0.0f};
};

struct C3Vector {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

struct CollisionAabb {
  C3Vector min{};
  C3Vector max{};

  [[nodiscard]] bool Contains(const CollisionAabb& other) const;
};

struct MovementCollisionFacet {
  std::array<C3Vector, 3> vertices{};
  C3Vector normal{0.0f, 0.0f, 1.0f};
  float plane_offset{0.0f};
  std::uint64_t owner_id{0};
  std::uint64_t facet_id{0};

  std::uint64_t owner_guid{0};
  bool secondary{false};
};

enum class MovementCollisionFacetCompleteness : std::uint8_t {
  kComplete,
  kPending,
};

inline constexpr std::uint32_t kMovementIncludeGameObjectGeometry = 0x100000u;

struct MovementCollisionFacetBatch {
  std::vector<MovementCollisionFacet> facets;
  std::uint64_t revision{0};
  MovementCollisionFacetCompleteness completeness{
      MovementCollisionFacetCompleteness::kComplete};
};

enum class MovementCollisionLayer : std::uint8_t {
  kPrimary,
  kSecondary,
};

struct MovementParentTransform {
  std::array<float, 9> parent_to_world{
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 1.0f};
  std::array<float, 9> world_to_parent{
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 1.0f};
  C3Vector parent_origin_world{};
  std::uint64_t revision{0};

  [[nodiscard]] C3Vector ToWorldPoint(const C3Vector& point) const;
  [[nodiscard]] C3Vector ToWorldVector(const C3Vector& vector) const;
  [[nodiscard]] C3Vector ToParentPoint(const C3Vector& point) const;
  [[nodiscard]] C3Vector ToParentVector(const C3Vector& vector) const;
  [[nodiscard]] C3Vector WorldNormalToParent(const C3Vector& normal) const;
};

enum class MovementCollisionMode : std::uint8_t {
  kGround,
  kFalling,
  kFlying,
  kSwimming,
  kSpecial,
  kSimpleCollision,
};

enum class MovementCollisionStatus : std::uint8_t {
  kNoCollision,
  kCollided,
  kBlocked,
  kQueryFailed,
  kCancelled,
  kInvalidInput,
};

struct MovementCollisionBody {
  C3Vector position{};
  float radius{0.5f};
  float height{2.0f};

  float step_height{2.0f};
  float hover_height{0.0f};
  MovementCollisionMode mode{MovementCollisionMode::kGround};
  std::uint32_t collision_mask{0};
  bool include_secondary_facets{false};

  bool allow_secondary_pass{true};

  bool primary_contact_seen{false};

  bool allow_fall_transition{true};

  bool trigger_ascent_jump_on_liquid_contact{false};
  bool permissive_walkable_slope{false};
  bool allow_vertical_stall_recovery{true};

  bool allow_special_fall_transition{true};

  bool stepping{false};
  float step_reference_z{0.0f};
  std::optional<MovementParentTransform> parent;
};

struct MovementCollisionStep {
  C3Vector displacement{};
  std::uint32_t duration_ms{0};

  float ground_probe_distance{0.0f};

  float movement_speed{0.0f};

  float vertical_speed{0.0f};

  std::uint32_t fall_time_ms{0};

  C3Vector initial_direction{};

  float fall_start_z{0.0f};

  bool safe_fall{false};

  bool falling{false};

  bool directional_input{false};
};

struct MovementCollisionContact {

  C3Vector normal{0.0f, 0.0f, 1.0f};
  C3Vector surface_normal{0.0f, 0.0f, 1.0f};
  float distance{0.0f};

  std::array<C3Vector, 3> vertices{};
  std::uint64_t owner_id{0};
  std::uint64_t facet_id{0};

  std::uint64_t owner_guid{0};
  bool secondary{false};

  std::array<C3Vector, 7> generated_normals{};
  std::uint8_t generated_normal_count{0};
};

class MovementCollisionContactSet {
 public:
  static constexpr std::size_t kCapacity = 7;
  using iterator = MovementCollisionContact*;
  using const_iterator = const MovementCollisionContact*;

  [[nodiscard]] bool empty() const { return size_ == 0; }
  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] iterator begin() { return contacts_.data(); }
  [[nodiscard]] iterator end() { return contacts_.data() + size_; }
  [[nodiscard]] const_iterator begin() const { return contacts_.data(); }
  [[nodiscard]] const_iterator end() const { return contacts_.data() + size_; }
  [[nodiscard]] MovementCollisionContact& front() { return contacts_.front(); }
  [[nodiscard]] const MovementCollisionContact& front() const {
    return contacts_.front();
  }
  void clear() { size_ = 0; }
  void push_back(const MovementCollisionContact& contact) {
    if (size_ < kCapacity) {
      contacts_[size_++] = contact;
    }
  }

 private:
  std::array<MovementCollisionContact, kCapacity> contacts_{};
  std::size_t size_{0};
};

struct MovementCollisionTrace {
  bool hit{false};
  float distance{0.0f};
  MovementCollisionContactSet contacts;
};

struct MovementCollisionResult {
  MovementCollisionStatus status{MovementCollisionStatus::kNoCollision};
  std::uint32_t consumed_ms{0};
  bool transitioned_to_falling{false};
  bool landed{false};

  bool ascent_jump_contact{false};

  bool falling_far_contact{false};
  bool reset_requested{false};

  bool state_snapshot_required{false};

  std::optional<float> current_speed_update;

  std::optional<C3Vector> horizontal_direction_update;
  std::optional<MovementCollisionContact> last_contact;
};

using MovementFacetQuery = std::function<std::optional<MovementCollisionFacetBatch>(
    const CollisionAabb&, std::uint32_t, MovementCollisionLayer)>;

struct MovementCollisionCallbacks {
  MovementFacetQuery query_facets;

  std::function<std::uint64_t()> facet_revision;
  std::function<bool()> cancelled;
  std::function<void(const MovementCollisionContact&)> contact;
  std::function<void(MovementCollisionMode, MovementCollisionMode)> mode_changed;
  std::function<void()> reset_movement;
};

struct MovementCollisionConstants {
  static constexpr float kPlaneEpsilon = 1.0f / 720.0f;
  static constexpr float kTiny = 0.00000023841858f;
  static constexpr float kTraceEpsilon = 0.00000095367432f;
  static constexpr float kMinimumHullSweepDistance = 0.027777778f;
  static constexpr float kMinDistance = 0.001f;
  static constexpr float kContactPush = 0.001f;
  static constexpr float kWalkableNormalZ = 0.64278764f;
  static constexpr float kPermissiveNormalZ = 0.17364818f;
  static constexpr float kLowerRingScale = 1.849399f;
  static constexpr float kFallingLateralRecovery = 1.1866661f;

  static constexpr float kSwimLiquidHullHeightScale = 0.75f;

  static constexpr float kStepProbeLateralScale = 1.1917536f;

  static constexpr float kStepFallAcceptCos = 0.9848077f;

  static constexpr float kGravity = 19.291103f;

  static constexpr float kSteepOpposeEpsilon = 1.0e-05f;
  static constexpr float kHoverVerticalSpeed = 3.5f;
  static constexpr float kLowerPlaneHorizontal = 0.8796419f;
  static constexpr float kLowerPlaneVertical = 0.4756366f;
  static constexpr float kWorldHalfSize = 17066.666f;
  static constexpr float kWorldEdgePadding = 71.375595f;
  static constexpr std::uint32_t kStalledIterationLimit = 6;
};

class MovementCollisionSolver {
 public:

  MovementCollisionSolver() = default;
  explicit MovementCollisionSolver(MovementCollisionCallbacks callbacks);
  MovementCollisionSolver(const MovementCollisionSolver&) = delete;
  MovementCollisionSolver& operator=(const MovementCollisionSolver&) = delete;
  MovementCollisionSolver(MovementCollisionSolver&&) noexcept = default;
  MovementCollisionSolver& operator=(MovementCollisionSolver&&) noexcept = default;

  void SetCallbacks(MovementCollisionCallbacks callbacks);
  [[nodiscard]] bool IsBound() const;

  void InvalidateFacets();
  void Reset();

  [[nodiscard]] std::shared_ptr<MovementCollisionSolver>
  CreateIndependentSolver() const;

  [[nodiscard]] MovementCollisionTrace SweepHull(
      const MovementCollisionBody& body, const C3Vector& displacement,
      bool* query_ok = nullptr);

  [[nodiscard]] MovementCollisionTrace QueryStaticOverlap(
      const MovementCollisionBody& body, bool* query_ok = nullptr);

  MovementCollisionResult Solve(MovementCollisionBody& body,
                                const MovementCollisionStep& step);

  [[nodiscard]] std::size_t CachedFacetCount() const;
  [[nodiscard]] std::optional<CollisionAabb> CachedBounds() const;

 private:
  struct FacetCache {
    CollisionAabb world_bounds{};

    std::vector<MovementCollisionFacet> world_facets;
    std::vector<MovementCollisionFacet> facets;
    std::uint32_t collision_mask{0};
    std::uint64_t parent_revision{0};
    std::uint64_t primary_revision{0};
    std::uint64_t secondary_revision{0};
    std::uint64_t source_revision{0};
    std::size_t primary_facet_count{0};
    bool has_secondary{false};
    bool complete{true};
    bool valid{false};
  };

  MovementCollisionCallbacks callbacks_;
  FacetCache cache_;

  [[nodiscard]] bool RefreshFacets(const MovementCollisionBody& body,
                                   const C3Vector& displacement);
  [[nodiscard]] MovementCollisionTrace SweepPrepared(
      const MovementCollisionBody& body,
      const C3Vector& displacement,
      float requested_distance) const;
  [[nodiscard]] MovementCollisionResult SolveGround(
      MovementCollisionBody& body, const MovementCollisionStep& step);
  [[nodiscard]] MovementCollisionResult SolveSimpleCollision(
      MovementCollisionBody& body, const MovementCollisionStep& step);
  [[nodiscard]] MovementCollisionResult SolveAirborne(
      MovementCollisionBody& body, const MovementCollisionStep& step);
  [[nodiscard]] MovementCollisionResult SolveSpecial(
      MovementCollisionBody& body, const MovementCollisionStep& step);

  [[nodiscard]] bool TryTerrainStep(MovementCollisionBody& body,
                                    const C3Vector& displacement,
                                    const MovementCollisionContact& trigger,
                                    const MovementCollisionStep& step);

  [[nodiscard]] bool StepFallCarriesForward(const MovementCollisionBody& body,
                                            const C3Vector& trial_position,
                                            const C3Vector& direction,
                                            float free_height,
                                            float movement_speed,
                                            bool safe_fall);
};

}

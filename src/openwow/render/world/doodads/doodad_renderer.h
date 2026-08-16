#pragma once

#include "openwow/data/terrain/adt_file.h"
#include "openwow/data/wmo/wmo_file.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/m2/m2_resource_streamer.h"
#include "openwow/render/m2/m2_transparent_draw_order.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/world/environment/spatial_point_light.h"
#include "openwow/render/world/environment/world_model_lighting.h"
#include "openwow/world/coordinates/frustum.h"
#include "openwow/world/environment/environment_detail.h"
#include "openwow/world/presentation/world_presentation_commands.h"
#include "openwow/world/wmo/wmo_visibility.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::core {
class FrameJobSystem;
}

namespace openwow::render {

struct DoodadAdmission {
  bool visible{false};
  float distance_alpha{0.0f};
  std::uint8_t depth_bucket{0u};
};

inline constexpr double kInertDoodadAnimationClockMark = -1.0;

struct DoodadInstance {
  std::string model_path;
  RenderVec3 position{};
  RenderVec3 rotation{};
  float scale{1.0f};
  RenderMatrix4x4 model_matrix{};

  std::uint64_t model_matrix_revision{0u};
  RenderVec4 tint_color{1.0f, 1.0f, 1.0f, 1.0f};

  bool wmo_color_is_ambient_substitute{false};
  float alpha{1.0f};
  RenderVec3 bounding_center{};
  float bounding_radius{0.0f};
  RenderAabb bounding_bounds{};

  RenderMatrix4x4 wmo_local_model_matrix{kRenderIdentityMatrix4x4};

  std::vector<std::uint16_t> wmo_group_indices;
  std::uint64_t render_owner_key{0u};

  std::uint64_t object_guid{0u};
  std::uint32_t render_placement_index{0u};

  std::uint32_t mddf_unique_id{0u};

  std::uint16_t wmo_doodad_set_index{0u};

  std::uint16_t authored_wmo_doodad_set_index{0u};

  bool is_destructible_transfer{false};
  std::uint8_t distance_class{4u};
  bool is_wmo_owned{false};
  std::uint32_t m2_model_id{0};
  std::uint32_t m2_instance_id{0};

  std::int8_t static_instancing_state{-1};

  std::int8_t scene_point_light_state{-1};

  mutable std::int32_t shadow_class_memo{-1};
  m2::M2StreamTicket m2_stream_ticket;
  std::shared_ptr<const m2::M2ModelCollisionGeometry> collision_geometry;
  bool m2_load_attempted{false};
  bool has_bounding_radius{false};
  bool has_bounding_bounds{false};

  RenderAabb header_local_bounds{};

  bool collision_ready{false};

  world::WmoDoodadAnimation destructible_animation{
      world::WmoDoodadAnimation::kNone};

  world::WmoDoodadAnimation transport_animation{
      world::WmoDoodadAnimation::kNone};

  bool transport_animation_callback_installed{false};

  bool render_ready_latched{false};

  mutable std::uint64_t admission_cache_epoch{0u};
  mutable DoodadAdmission admission_cache{};

  float deferred_animation_seconds{0.0f};

  double animation_clock_mark{kInertDoodadAnimationClockMark};
};

struct WmoDoodadM2PresentationEvent {
  std::uint64_t owner{};
  m2::M2TriggeredEvent event;
};

struct DoodadCollisionTriangle {
  std::array<RenderVec3, 3> vertices{};
  std::uint64_t owner_id{0u};
  std::uint64_t facet_id{0u};

  std::uint64_t owner_guid{0u};
};

[[nodiscard]] float ResolveRetailDoodadDistanceAlpha(float raw_alpha) noexcept;

[[nodiscard]] bool IsDoodadAuthoredBefore(const DoodadInstance &lhs,
                                          const DoodadInstance &rhs) noexcept;

[[nodiscard]] std::array<std::uint8_t, 64>
BuildDoodadDepthBucketOrder(m2::M2RenderPassScope pass_scope) noexcept;

[[nodiscard]] DoodadAdmission
EvaluateDoodadAdmission(const DoodadInstance &instance, const world::Frustum *frustum,
                        float camera_x, float camera_y, float camera_z,
                        const RenderVec3 &camera_forward,
                        const world::EnvironmentDetailDistances &detail) noexcept;

class DoodadRenderer {
public:
  using WmoOwnerId = std::uint64_t;
  explicit DoodadRenderer(m2::M2System &m2_system) : m2_system_(m2_system) {}
  ~DoodadRenderer();

  DoodadRenderer(const DoodadRenderer &) = delete;
  DoodadRenderer &operator=(const DoodadRenderer &) = delete;

  bool Initialize();
  void Shutdown();

  using LoadFileCallback = std::function<std::vector<std::uint8_t>(const std::string &)>;

  void SetFileLoader(LoadFileCallback callback) {
    load_file_ = std::move(callback);
    m2_system_.SetFileLoader(load_file_);
    m2_system_.SetAsyncFileLoader(load_file_);
  }

  using PrefixLoadFileCallback = m2::M2StreamPrefixFileLoader;
  void SetPrefixFileLoader(PrefixLoadFileCallback callback) {
    load_file_prefix_ = std::move(callback);
    m2_system_.SetAsyncPrefixFileLoader(load_file_prefix_);
  }

  void LoadFromAdt(const data::terrain::AdtFile &adt, std::int32_t tile_x, std::int32_t tile_y);

  void UnloadTile(std::int32_t tile_x, std::int32_t tile_y);

  void ClearWmoInstances();

  void LoadFromWmo(WmoOwnerId owner, const data::wmo::WmoRoot &root,
                   const std::vector<data::wmo::WmoGroup> &groups,
                   const RenderMatrix4x4 &wmo_model_matrix, std::uint16_t active_doodad_set,
                   std::array<std::uint16_t, 3> additional_active_doodad_sets = {},
                   std::uint64_t object_guid = 0u);

  void BeginStreamingWmoInstance(
      WmoOwnerId owner, std::size_t group_count, std::uint16_t active_doodad_set,
      std::array<std::uint16_t, 3> additional_active_doodad_sets = {},
      std::uint64_t object_guid = 0u);
  void PublishStreamingWmoGroup(WmoOwnerId owner, const data::wmo::WmoRoot &root,
                                const data::wmo::WmoGroup &group, std::uint16_t group_index,
                                const RenderMatrix4x4 &wmo_model_matrix);
  void UnloadWmoInstance(WmoOwnerId owner);

  void SetWmoInstanceDoodadSets(
      WmoOwnerId owner, std::uint16_t active_doodad_set,
      std::array<std::uint16_t, 3> additional_active_doodad_sets = {});

  void SetWmoInstanceDoodadAnimations(
      WmoOwnerId owner,
      std::array<world::WmoDoodadAnimationControl, 2> controls);

  void SetWmoInstanceTransferDestinationGroups(
      WmoOwnerId owner, const data::wmo::WmoRoot& root);

  void TransferWmoDoodadSet(WmoOwnerId source_owner,
                            WmoOwnerId destination_owner,
                            std::uint16_t source_doodad_set,
                            std::uint16_t destination_doodad_set);

  void SetWmoInstanceEnabled(WmoOwnerId owner, bool enabled);
  void BindWmoDoodadM2EventSink(
      std::function<void(const WmoDoodadM2PresentationEvent&)> sink) {
    wmo_doodad_m2_event_sink_ = std::move(sink);
  }

  void SetWmoInstanceTransform(WmoOwnerId owner,
                               const RenderMatrix4x4& wmo_model_matrix);

  void Clear();

  void SetLoadingFocus(float x, float y, float z);

  void UpdateLoading(int max_per_frame = 64);

  void Update(float dt);

  void Render(std::uint8_t view_id, const float *view_mtx, const float *proj_mtx,
              const world::Frustum *frustum, float camera_x, float camera_y, float camera_z,
              const RenderVec3 &camera_forward,
              m2::M2RenderPassScope pass_scope = m2::M2RenderPassScope::kAll,
              m2::M2TransparentDrawOrder *transparent_draw_order = nullptr);

  [[nodiscard]] const std::vector<SpatialPointLight> &scene_point_lights() const noexcept {
    return scene_point_lights_;
  }

  void SetWorldM2SceneState(const WorldM2SceneState &scene_state) {
    world_m2_scene_state_ = scene_state;
  }
  void SetEnvironmentDetail(float scale) {

    environment_detail_ = world::MakeEnvironmentDetailDistances(scale);
  }

  void VisitInstances(const std::function<void(const DoodadInstance &)> &visitor) const;

  template <typename Visitor>
  void VisitVisibleInstances(const world::Frustum &frustum, const float camera_x,
                             const float camera_y, const float camera_z,
                             const RenderVec3 &camera_forward, Visitor &&visitor) {
    CollectAdmittedWalk(frustum, camera_x, camera_y, camera_z, camera_forward);
    for (const DoodadInstance *const instance : admitted_walk_scratch_) {
      visitor(*instance);
    }
  }

  void VisitCollisionTriangles(
      const std::array<float, 6>& world_bounds,
      const std::function<void(const DoodadCollisionTriangle&)>& visitor,
      bool include_object_owned = true) const;
  [[nodiscard]] std::uint64_t CollisionRevision() const noexcept {
    return collision_revision_;
  }

private:
  struct OwnedDoodads;

  m2::M2System &m2_system_;

  static std::uint64_t MakeTileKey(std::int32_t tx, std::int32_t ty) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tx)) << 32) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(ty));
  }

  void ClearM2Instance(DoodadInstance &inst);
  void SynchronizeDestructibleAnimation(DoodadInstance& inst,
                                        const OwnedDoodads& owner);

  void SynchronizeTransportAnimation(DoodadInstance& inst,
                                     const OwnedDoodads& owner);

  void PrepareAdmissionCache(const world::Frustum *frustum, float camera_x, float camera_y,
                             float camera_z, const RenderVec3 &camera_forward) const;

  void CollectAdmittedWalk(const world::Frustum &frustum, float camera_x, float camera_y,
                           float camera_z, const RenderVec3 &camera_forward);

  [[nodiscard]] DoodadAdmission EvaluateCachedAdmission(
      const DoodadInstance &instance, const world::Frustum *frustum, float camera_x,
      float camera_y, float camera_z, const RenderVec3 &camera_forward) const;
  void AdvanceCollisionRevision() noexcept;
  void RefreshSpatialBounds(DoodadInstance& inst);

  void EnsureOwnerSpatialIndex(const OwnedDoodads &owner) const;

  template <typename OwnerRef, typename Visitor>
  void ForEachAdmissionCandidate(OwnerRef &owner, const world::Frustum *frustum,
                                 float camera_x, float camera_y, float camera_z,
                                 const world::EnvironmentDetailDistances &detail,
                                 const Visitor &visit) const;

  void ApplyAnimationEligibility(DoodadInstance &inst, bool is_eligible) const noexcept;

  void AdoptEarlyCollisionData(DoodadInstance& inst, const m2::M2StreamQuery& streamed);
  [[nodiscard]] bool PublishPreparedM2Instance(DoodadInstance &inst, std::uint32_t model_id,
                                               std::uint32_t instance_id);
  void ComputeModelMatrix(DoodadInstance &inst, const data::terrain::DoodadPlacement &placement);
  void ComputeWmoModelMatrix(DoodadInstance &inst, const data::wmo::WmoDoodadDef &def,
                             const RenderMatrix4x4 &wmo_model_matrix);
  enum class OwnerKind : std::uint8_t { Tile, Wmo };

  struct DoodadAdmissionGroup {

    RenderAabb volume_bounds{};

    RenderAabb center_bounds{};

    std::uint32_t first_member{0u};
    std::uint32_t member_count{0u};

    std::uint8_t distance_class{0u};
  };

  struct DoodadAdmissionBuildEntry {
    RenderAabb volume{};
    RenderAabb center_bounds{};
    RenderVec3 center{};
    std::uint32_t group_slot{0u};
    std::uint8_t distance_class{0u};
  };

  struct OwnedDoodads {
    std::uint64_t generation{0};

    std::uint64_t object_guid{0};
    std::vector<DoodadInstance> instances;

    std::size_t group_count{0u};

    std::array<std::uint16_t, 4> active_wmo_doodad_sets{};
    std::array<world::WmoDoodadAnimationControl, 2>
        doodad_animation_controls{};

    std::vector<std::uint16_t> transfer_destination_group_indices;
    bool enabled{true};
    std::unordered_map<std::uint16_t, std::size_t> wmo_instance_by_doodad_ref;

    mutable RenderAabb combined_world_bounds{};
    mutable std::vector<DoodadAdmissionGroup> admission_groups;
    mutable std::vector<std::uint32_t> admission_group_members;
    mutable bool spatial_index_valid{false};
  };

  struct PendingLoadHandle {
    float distance_squared{0.0f};
    std::uint64_t sequence{0};
    std::uint64_t owner{0};
    std::uint64_t generation{0};
    std::uint32_t instance_index{0};
    OwnerKind kind{OwnerKind::Tile};
  };

  struct PendingLoadFarther {
    bool operator()(const PendingLoadHandle &lhs, const PendingLoadHandle &rhs) const noexcept {
      if (lhs.distance_squared != rhs.distance_squared) {
        return lhs.distance_squared > rhs.distance_squared;
      }
      return lhs.sequence > rhs.sequence;
    }
  };

  void PushPendingOwner(OwnerKind kind, std::uint64_t owner, const OwnedDoodads &doodads);

 public:

  [[nodiscard]] bool IsWorldEntryLoadDrained() const;

 private:

  void TransferTileDoodadInstance(OwnedDoodads &source, std::uint32_t unique_id,
                                  std::uint64_t destination_tile_key);
  void RebuildLoadingQueue();
  [[nodiscard]] DoodadInstance *ResolvePending(const PendingLoadHandle &handle);

  struct QueuedDoodadDraw {
    DoodadInstance *instance{nullptr};
    float distance_alpha{1.0f};
    bool render_state_prepared{false};

    bool routed_to_instanced{false};
  };

  struct PendingDoodadFrameState {
    QueuedDoodadDraw *target{nullptr};

    RenderVec4 material_tint{1.0f, 1.0f, 1.0f, 1.0f};

    std::uint32_t request_index{0};
  };
  static constexpr std::uint32_t kNoDoodadFrameRequest =
      std::numeric_limits<std::uint32_t>::max();

  struct InstancedDoodadGroup {
    std::uint32_t exemplar_instance_id{0};
    std::vector<m2::M2InstancedDrawRecord> records;
    std::vector<QueuedDoodadDraw*> members;
  };

  std::unordered_map<std::uint64_t, OwnedDoodads> tile_doodads_;

  std::map<WmoOwnerId, OwnedDoodads> wmo_doodads_;

  LoadFileCallback load_file_;

  PrefixLoadFileCallback load_file_prefix_;

  WorldM2SceneState world_m2_scene_state_{};
  world::EnvironmentDetailDistances environment_detail_{};

  struct AdmissionCacheKey {
    bool has_frustum{false};
    std::array<std::array<float, 4>, 6> frustum_planes{};
    float camera_x{0.0f};
    float camera_y{0.0f};
    float camera_z{0.0f};
    RenderVec3 camera_forward{};
    world::EnvironmentDetailDistances environment_detail{};

    [[nodiscard]] bool operator==(const AdmissionCacheKey &) const noexcept = default;
  };
  mutable AdmissionCacheKey admission_cache_key_{};

  mutable std::uint64_t admission_cache_epoch_{0u};

  std::vector<DoodadInstance *> admitted_walk_scratch_;
  std::uint64_t admitted_walk_epoch_{0u};
  bool admitted_walk_consumable_{false};

  std::priority_queue<PendingLoadHandle, std::vector<PendingLoadHandle>, PendingLoadFarther>
      pending_load_queue_;
  std::deque<PendingLoadHandle> pending_publication_queue_;
  RenderVec3 loading_focus_{};
  bool has_loading_focus_{false};
  std::uint64_t next_owner_generation_{1};
  std::uint64_t next_pending_sequence_{1};

  std::uint64_t next_model_matrix_revision_{1};

  std::unordered_map<std::uint32_t, std::vector<std::uint64_t>>
      tile_doodad_uid_refs_;

  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>>
      tile_referenced_uids_;
  std::uint64_t collision_revision_{1};
  std::function<void(const WmoDoodadM2PresentationEvent&)>
      wmo_doodad_m2_event_sink_;
  std::array<std::vector<QueuedDoodadDraw>, 64> render_buckets_;

  std::vector<std::uint32_t> animation_update_ids_scratch_;
  std::vector<DoodadInstance*> animation_update_instances_scratch_;
  std::vector<std::uint32_t> animation_update_missing_scratch_;

  std::vector<DoodadInstance*> scene_light_candidates_scratch_;
  std::vector<SpatialPointLight> scene_point_lights_;
  void CollectScenePointLights();

  std::vector<std::uint32_t> render_batch_ids_scratch_;
  std::vector<QueuedDoodadDraw*> render_batch_targets_scratch_;
  std::vector<m2::M2RenderInstanceResult> render_batch_results_scratch_;

  std::vector<std::uint32_t> render_batch_draw_ordinals_scratch_;

  std::vector<PendingDoodadFrameState> frame_state_work_scratch_;
  std::vector<m2::M2DoodadFrameRenderRequest> frame_state_requests_scratch_;
  std::vector<m2::M2ResultStatus> frame_state_statuses_scratch_;
  std::vector<m2::M2BatchUniforms> frame_state_uniforms_scratch_;

  std::vector<std::uint32_t> transparent_effect_ids_scratch_;

  mutable std::vector<DoodadAdmissionBuildEntry> admission_build_scratch_;
  mutable std::vector<std::uint32_t> admission_slot_count_scratch_;

  double animation_clock_seconds_{0.0};

  std::unordered_map<std::uint64_t, InstancedDoodadGroup> instanced_groups_scratch_;

  bool render_queue_ready_for_transparent_{false};
  bool initialized_{false};
};

}

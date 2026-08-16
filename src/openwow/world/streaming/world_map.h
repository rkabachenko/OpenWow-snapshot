#pragma once
#include "openwow/runtime/scheduling/thread_pool_system.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/terrain/adt_file.h"
#include "openwow/data/terrain/wdl_file.h"
#include "openwow/data/terrain/wdt_file.h"
#include "openwow/world/coordinates/frustum.h"
#include "openwow/world/environment/lighting.h"
#include "openwow/world/environment/zone_skybox.h"
#include "openwow/world/coordinates/world_geometry.h"
#include "openwow/world/wmo/wmo_visibility.h"
#include "openwow/world/streaming/terrain_streamer.h"
#include "openwow/world/streaming/stream_identity.h"
#include "openwow/world/presentation/world_presentation_snapshot.h"
#include "openwow/world/presentation/world_presentation_commands.h"
#include "openwow/world/collision/collision.h"
#include "openwow/world/environment/chunk_ambient_audio.h"
#include "openwow/world/environment/weather.h"
#include "openwow/world/wmo/wmo_area_acceleration.h"
#include "openwow/world/liquid/wmo_liquid_surface.h"
#include "openwow/world/liquid/water_heightfield.h"
#include "openwow/world/streaming/world_streaming_ownership.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::world {

struct LoadedTile {
  TileCoord coord{};
  data::terrain::AdtFile adt{};
  std::array<std::array<std::uint32_t, 64>, 256> terrain_cell_ground_effect_ids{};
  std::vector<WaterHeightfield> water_surfaces;
  std::array<ChunkSoundInstanceSet, data::terrain::kTotalChunks> ambient_sounds;
};

struct WmoInstance {
  std::string wmo_path;
  Matrix4 model_matrix{};
  Matrix4 inverse_model_matrix{};
  std::array<float, 6> placement_world_bounds{};
  float uniform_scale{1.0f};
  std::uint16_t active_wmo_doodad_set_id{0};
  std::array<std::uint16_t, 3> additional_active_wmo_doodad_sets{};
  std::array<WmoDoodadAnimationControl, 2> doodad_animation_controls{};
  bool visible{true};
  std::uint16_t name_set{0};
  std::vector<std::array<float, 6>> group_world_bounds;
  std::vector<std::optional<WaterHeightfield>> group_liquid_surfaces;
  world::WmoVisibilityMask critical_spawn_groups;
  mutable world::WmoVisibilityWorkspace visibility_workspace;
  std::uint64_t collision_owner_id{0};
  std::uint64_t doodad_owner_id{0};
  std::uint64_t placement_stable_id{0};

  std::uint32_t placement_unique_id{0};
  std::uint16_t placement_flags{0};

  std::uint64_t object_guid{0};
};

struct ObjectWmoPlacement {
  std::string path;
  Matrix4 model_matrix{};
  Bounds world_bounds{};
  float uniform_scale{1.0f};
  std::array<std::uint16_t, 3> additional_doodad_sets{};
  std::array<WmoDoodadAnimationControl, 2> doodad_animation_controls{};
  bool visible{true};

  std::uint64_t object_guid{0};

  [[nodiscard]] bool operator==(const ObjectWmoPlacement&) const = default;
};

enum class WmoGroupResidency : std::uint8_t {
  kUnrequested,
  kPending,
  kReady,
  kResident,
};

[[nodiscard]] inline constexpr std::uint64_t WmoCpuLoadRetryDelay(
    const std::uint8_t retry_count) noexcept {
  return std::uint64_t{1u} << std::min<std::uint8_t>(retry_count, 6u);
}

struct WmoCpuLoadRetryState {
  std::uint64_t retry_after_pump{};
  std::uint8_t retry_count{};
  float priority{};

  [[nodiscard]] constexpr bool HasFailure() const noexcept {
    return retry_count != 0u;
  }

  [[nodiscard]] constexpr bool CanQueue(
      const std::uint64_t pump_sequence) const noexcept {
    return pump_sequence >= retry_after_pump;
  }

  constexpr void RecordFailure(const std::uint64_t pump_sequence,
                               const float request_priority) noexcept {
    priority = request_priority;
    retry_after_pump = pump_sequence + WmoCpuLoadRetryDelay(retry_count);
    retry_count = std::min<std::uint8_t>(
        static_cast<std::uint8_t>(retry_count + 1u), 7u);
  }

  constexpr void Reset() noexcept {
    retry_after_pump = 0u;
    retry_count = 0u;
    priority = 0.0f;
  }
};

enum class MovementWmoCollisionCompleteness : std::uint8_t {
  kComplete,
  kPending,
};

struct AreaEnvironmentContext {
  std::uint32_t area_id{0u};
  bool outdoors{true};
  bool has_wmo_context{false};
  float depth{0.0f};
};

enum class AreaEnvironmentProbe : std::uint8_t {
  kUnitSurface,
  kCameraRoom,
};

struct WorldLiquidSoundSource {
  std::uint32_t liquid_type_id{0u};
  std::array<float, 3> position{};
};

struct CachedWmo {
  world::WmoVisibilityData visibility;
  data::wmo::WmoRoot root;
  std::uint32_t root_wmo_id{0};
  std::vector<data::wmo::WmoGroup> groups;
  std::vector<std::shared_ptr<const data::wmo::WmoGroup>>
      group_presentation_payloads;
  std::vector<std::vector<std::uint32_t>> group_material_indices;
  std::vector<std::vector<std::array<float, 3>>> collision_vertices;
  std::vector<WmoGroupResidency> group_residency;
  std::vector<WmoCpuLoadRetryState> group_cpu_retry;
  std::vector<bool> group_gpu_queued;
  std::vector<WmoGroupPublicationStatus> group_gpu_publication;
  std::vector<WmoGroupPublicationRetryState> group_gpu_retry;
  std::vector<WmoAreaTriangleIndex> area_triangle_indices;
};

struct StagedWorldTile {
  TileCoord coord{};
  MapGeneration generation{};
  StreamOwnerHandle owner{};
  std::uint64_t request_id{0};
  std::unique_ptr<LoadedTile> tile;
  std::string error;
};

struct StagedWorldWmo {
  std::string path;
  MapGeneration generation{};
  StreamOwnerHandle owner{};
  std::uint64_t request_id{0};
  std::unique_ptr<CachedWmo> cached;
  std::string error;
};

struct WmoGroupRequestKey {
  std::string path;
  std::uint32_t group_index{0};

  [[nodiscard]] friend bool operator<(const WmoGroupRequestKey& lhs,
                                      const WmoGroupRequestKey& rhs) noexcept {
    return lhs.path < rhs.path ||
           (lhs.path == rhs.path && lhs.group_index < rhs.group_index);
  }
};

struct StagedWorldWmoGroup {
  WmoGroupRequestKey key;
  MapGeneration generation{};
  StreamOwnerHandle owner{};
  std::uint64_t request_id{0};
  data::wmo::WmoGroup group;
  std::vector<std::uint32_t> material_indices;
  std::vector<std::array<float, 3>> collision_vertices;
  WmoAreaTriangleIndex area_triangle_index;
  std::uint64_t publication_bytes{0u};
  std::string error;
};

struct WorldStagingMailbox {
  std::mutex mutex;
  std::vector<StagedWorldTile> tiles;
  std::vector<StagedWorldWmo> wmos;
  std::vector<StagedWorldWmoGroup> wmo_groups;
};

class WorldMap {
public:
  explicit WorldMap(audio::SoundRuntime* ambient_audio);
  ~WorldMap();

  WorldMap(const WorldMap &) = delete;
  WorldMap &operator=(const WorldMap &) = delete;

  bool Initialize();

  void Shutdown();

  void UnloadMap();

  void SetMap(uint32_t map_id, const std::string &map_internal_name);

  void UpdatePlayerPosition(float x, float y, float z);

  void UpdateStreamingPosition(float x, float y);

  void SetViewDistance(int32_t tiles) {
    view_distance_ = tiles;
  }

  void SetTimeOfDay(float normalized_time);

  void SetScreenEffectLightParamSlotOverride(std::optional<std::uint8_t> light_param_slot_override);
  void SetScreenEffectFogOverride(
      std::optional<WorldFogOverride> fog_override);

  void SetSuppressLocalLighting(bool suppress_local_lighting);

  void SetWeather(std::uint32_t type, float intensity, bool smooth);

  void BindDbc(const openwow::data::dbc::DbcLoader *dbc);

  void Update(float dt);

  void SynchronizeObjectWmoPlacement(std::uint64_t owner,
                                     const ObjectWmoPlacement &placement);

  void TransferObjectWmoDoodadSet(std::uint64_t source_owner,
                                  std::uint64_t destination_owner,
                                  std::uint16_t source_doodad_set,
                                  std::uint16_t destination_doodad_set);
  void RemoveObjectWmoPlacement(std::uint64_t owner);

  [[nodiscard]] std::optional<Bounds> EnsureObjectWmoLocalBounds(
      std::uint64_t owner);

  [[nodiscard]] std::optional<Bounds> QueryObjectWmoLocalBounds(
      std::uint64_t owner) const;

  [[nodiscard]] std::optional<std::vector<std::array<float, 4>>>
  QueryObjectWmoConvexVolumePlanes(std::uint64_t owner) const;

  [[nodiscard]] bool IsObjectWmoPlacementRenderReady(std::uint64_t owner) const;

  [[nodiscard]] WorldPresentationSnapshot PublishPresentationSnapshot(
      CameraSnapshot camera, float far_clip);
  [[nodiscard]] WorldPresentationCommandBatch DrainPresentationCommands();
  void QueueWaterRipplePresentation(SpawnWaterRippleCommand command);
  void AcknowledgePresentation(
      const WorldPresentationAcknowledgment& acknowledgment);

  void QueueFullPresentationReplay();

  bool SetScreenSize(uint16_t width, uint16_t height);

  using LoadFileCallback = std::function<std::vector<uint8_t>(const std::string &path)>;

  using BlockingLoadProgressCallback = std::function<void(float progress)>;

  using TileLoadedCallback =
      std::function<void(const TileCoord &coord, const data::terrain::AdtFile &adt)>;
  using TileUnloadedCallback = std::function<void(const TileCoord &coord)>;

  void SetFileLoader(LoadFileCallback callback);

  void SetBlockingLoadProgressCallback(BlockingLoadProgressCallback callback) {
    blocking_load_progress_callback_ = std::move(callback);
  }

  void SetTileLifecycleCallbacks(TileLoadedCallback on_tile_loaded,
                                 TileUnloadedCallback on_tile_unloaded) {
    tile_loaded_callback_ = std::move(on_tile_loaded);
    tile_unloaded_callback_ = std::move(on_tile_unloaded);
  }

  [[nodiscard]] uint32_t map_id() const {
    return map_id_;
  }
  [[nodiscard]] const std::string &map_name() const {
    return map_name_;
  }
  [[nodiscard]] std::size_t loaded_tile_count() const {
    return loaded_tiles_.size();
  }
  [[nodiscard]] MapGeneration map_generation() const noexcept {
    return world_staging_generation_;
  }
  [[nodiscard]] WorldPresentationSnapshot BuildPresentationSnapshot(
      const CameraSnapshot& camera) const;

  [[nodiscard]] bool IsCriticalSpawnSurfaceReady() const;

  [[nodiscard]] bool IsWorldEntryStreamingComplete() const;

  void SetWorldEntryStreamingMode(const bool enabled) noexcept {
    world_entry_streaming_mode_ = enabled;
  }

  [[nodiscard]] bool IsAreaResolutionSettledAt(float x, float y) const;

  [[nodiscard]] bool AreExistingTerrainTilesLoaded(float min_x, float max_x,
                                                   float min_y,
                                                   float max_y) const;
  [[nodiscard]] TileCoord player_tile() const {
    return player_tile_;
  }

  [[nodiscard]] std::optional<WmoLiquidPointQueryResult>
  QueryWmoLiquidAtPosition(float x, float y, float z) const;

  [[nodiscard]] std::optional<WmoLiquidPointQueryResult>
  QueryWmoInteriorLiquidAtPosition(float x, float y, float z) const;

  [[nodiscard]] std::optional<WmoLiquidPointQueryResult>
  QueryAnyWmoLiquidAtPosition(float x, float y, float z) const;
  [[nodiscard]] std::uint32_t GetUnderwaterLiquidTypeId(float x, float y, float z) const;
  [[nodiscard]] std::optional<float> GetLiquidSurfaceHeightAtPosition(
      float x, float y, float z) const;

  [[nodiscard]] std::optional<WmoLiquidPointQueryResult>
  QueryLiquidSurfaceAtPosition(float x, float y, float z) const;
  [[nodiscard]] std::vector<WorldLiquidSoundSource>
  QueryLiquidSoundSources(float x, float y, float z, float radius) const;

  [[nodiscard]] bool IsPositionInTerrainHole(float x, float y) const;

  [[nodiscard]] std::uint32_t ResolveTerrainGroundTypeAtPosition(float x, float y) const;
  [[nodiscard]] AreaEnvironmentContext ResolveAreaEnvironmentContextAtPosition(
      float x, float y, float z) const;
  [[nodiscard]] std::uint32_t ResolveAreaIdAtPosition(float x, float y, float z) const;

  [[nodiscard]] bool IsPositionInSnowArea(float x, float y, float z) const;
  [[nodiscard]] bool IsOutdoorsAtPosition(float x, float y, float z) const;

  MovementWmoCollisionCompleteness VisitMovementCollisionFacets(
      const std::array<float, 6>& world_bounds,
      const CollisionFacetVisitor& visitor,
      bool include_object_placements = true);

  void VisitMovementLiquidFacets(
      const std::array<float, 6>& world_bounds,
      std::uint32_t collision_mask,
      const CollisionFacetVisitor& visitor) const;
  [[nodiscard]] std::uint64_t MovementCollisionFacetRevision() const noexcept {
    return area_environment_generation_ * 0x9e3779b97f4a7c15ull ^
           (0x517cc1b727220a95ull +
            (area_environment_generation_ << 6u) +
            (area_environment_generation_ >> 2u));
  }

  [[nodiscard]] const TerrainStreamer &terrain_streamer() const {
    return terrain_streamer_;
  }

private:
  static std::unique_ptr<CachedWmo> PrepareWmoCpuBundle(
      const LoadFileCallback& load_file, const std::string& wmo_path,
      std::string* error);

  audio::SoundRuntime* ambient_audio_{nullptr};
  enum class LifecycleState : std::uint8_t {
    kStopped,
    kStarting,
    kRunning,
    kStopping,
  };

  struct AreaEnvironmentQueryCache {
    std::uint64_t generation{0u};
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    AreaEnvironmentProbe probe{AreaEnvironmentProbe::kUnitSurface};
    WmoAreaSpatialIndex::Cell cell{};
    AreaEnvironmentContext resolution{};
    std::optional<WmoAreaGroupRef> containing_group;
    std::optional<WmoAreaGroupRef> alternate_group;
    std::optional<WmoAreaGroupRef> secondary_group;
    std::optional<WmoAreaGroupRef> secondary_alternate_group;
    bool valid{false};
  };

  struct WmoAreaLookupValue {
    std::uint32_t area_id{0u};
    std::uint32_t flags{0u};
  };

  struct CameraWmoFogResolution {
    FogState state;
    float portal_blend_progress{1.0f};
  };

  [[nodiscard]] AreaEnvironmentContext ResolveAreaEnvironmentAtPosition(
      float x, float y, float z, AreaEnvironmentProbe probe) const;
  [[nodiscard]] std::optional<CameraWmoFogResolution> ResolveCameraWmoFog(
      float x, float y, float z, float far_clip) const;
  void AddWmoAreaGroupToIndex(const WmoPlacementKey &placement_key,
                              const WmoInstance &instance,
                              std::size_t group_index);
  void UpdateResidentObjectWmoPresentation(
      std::uint64_t owner, const ObjectWmoPlacement& placement);
  void InvalidateAreaEnvironmentCache() noexcept;

  void InvalidateAreaEnvironmentCacheInBounds(
      const std::array<float, 6>& world_bounds) noexcept;

  std::unordered_set<TileCoord, TileCoordHash> GetRequiredTiles() const;

  static TileCoord WorldToTile(float x, float y);

  void QueueTileLoad(const TileCoord &coord);

  void QueueWmoLoad(const std::string &wmo_path,
                    float publication_priority = 0.0f);
  void QueueWmoGroupLoad(const std::string& wmo_path,
                         std::uint32_t group_index, float priority);
  void QueueInitialWmoGroups(const std::string& wmo_path);

  void QueueCameraRoomWmoGroups(float camera_x, float camera_y,
                                float camera_z);
  void QueueVisibleWmoGroups(const std::string& wmo_path,
                             const WmoInstance& instance,
                             const world::Frustum& frustum, float camera_x,
                             float camera_y, float camera_z);

  void PumpWorldStaging(std::size_t tile_budget, std::size_t wmo_budget);
  void DrainWorldStagingMailbox();
  void QueueDueWmoRetries();
  void CancelWorldStaging();
  void InitializeWorldStagingWorkers(std::uint32_t worker_count);
  void RebuildPublicationOrder(float focus_x, float focus_y);
  std::size_t PumpReadyWmoGroups(std::size_t group_budget,
                                 std::uint64_t* byte_budget,
                                 std::chrono::steady_clock::time_point deadline);
  void PublishWmoGroup(StagedWorldWmoGroup completion);
  void CancelWmoGroupLoadsForPath(const std::string& path);

  void UnloadTile(const TileCoord &coord);

  void RegisterMapWmoPlacementOwners();
  void RegisterTileWmoPlacementOwner(const LoadedTile &tile);
  void RegisterWmoPlacement(PendingWmoPlacement placement,
                            float publication_priority);
  void MaterializeWmoPlacement(const PendingWmoPlacement &placement);

  [[nodiscard]] bool TryTransferWmoPlacementInstance(const WmoPlacementKey &from,
                                                     const WmoPlacementKey &to);
  void RemoveWmoPlacementOwner(std::uint64_t owner);
  void PublishWmoPlacementsForPath(const std::string &path);
  void MaybeReleaseWmoCache(const std::string &path);

  [[nodiscard]] std::uint32_t ResolveWmoLiquidVertexFormat(
      std::uint32_t liquid_type, std::uint32_t group_flags,
      std::uint32_t liquid_type_flags) const;

  [[nodiscard]] std::optional<world::WorldLighting::LiquidDarkeningState>
  BuildLiquidDarkeningState() const;
  [[nodiscard]] std::optional<WaterHeightfieldSample>
  QueryAdtLiquidAtPosition(float x, float y) const;
  void RefreshLightingEnvironment();
  void UpdateDayNightLightEnvironmentForFrame(
      float camera_x, float camera_y, float camera_z,
      const std::array<float, 3> &camera_forward, float far_clip);

  [[nodiscard]] bool MapUsesAdvancedFog() const;

  void PublishAdvancedFogInputs();

  std::string GetAdtPath(const TileCoord &coord) const;

  static void ComputeWmoModelMatrix(const data::terrain::WmoPlacement &placement,
                                    float out_mtx[16]);

  [[nodiscard]] static std::uint64_t TileOwnerId(const TileCoord &coord) noexcept;

  std::unordered_map<std::string, CachedWmo> wmo_cache_;
  std::vector<WorldPresentationCommand> presentation_commands_;

  std::map<WmoPlacementKey, WmoInstance> wmo_instances_;
  WmoAreaSpatialIndex wmo_area_spatial_index_;
  std::uint64_t area_environment_generation_{1u};

  static constexpr std::size_t kAreaEnvironmentQueryCacheSize = 64u;
  static_assert((kAreaEnvironmentQueryCacheSize &
                 (kAreaEnvironmentQueryCacheSize - 1u)) == 0u,
                "area-environment memo is indexed with a power-of-two mask");
  mutable std::array<AreaEnvironmentQueryCache, kAreaEnvironmentQueryCacheSize>
      area_environment_query_cache_{};

  mutable AreaEnvironmentQueryCache last_area_environment_resolution_{};
  std::vector<std::pair<float, std::uint32_t>> wmo_group_priority_scratch_;
  std::optional<WmoAreaGroupRef> last_presentation_room_;
  bool last_presentation_outdoors_{true};
  std::uint64_t wmo_camera_room_transition_{0};

  WorldStreamingOwnership streaming_ownership_;
  std::unordered_set<std::uint64_t> active_wdl_wmo_owners_;
  std::unordered_map<std::uint64_t, ObjectWmoPlacement> object_wmo_placements_;

  uint32_t map_id_ = 0;
  std::string map_name_;
  data::terrain::WdtFile wdt_;
  bool wdt_loaded_ = false;

  bool world_entry_streaming_mode_ = false;
  std::shared_ptr<const data::terrain::WdlFile> wdl_;
  bool wdl_loaded_ = false;
  float player_x_ = 0.0f;
  float player_y_ = 0.0f;
  float player_z_ = 0.0f;

  float camera_x_ = 0.0f;
  float camera_y_ = 0.0f;
  float camera_z_ = 0.0f;
  bool has_camera_position_ = false;
  bool player_is_outdoors_ = true;
  bool has_player_streaming_focus_ = false;
  TileCoord player_tile_{0, 0};
  float streaming_x_ = 0.0f;
  float streaming_y_ = 0.0f;
  bool has_camera_streaming_focus_ = false;
  TileCoord streaming_tile_{0, 0};
  int32_t view_distance_ = 2;
  float time_of_day_ = 0.5f;
  WeatherKind active_weather_type_{WeatherKind::kNone};
  float active_weather_density_{0.0f};
  std::optional<data::dbc::WeatherEntry> active_weather_row_;
  LifecycleState lifecycle_state_{LifecycleState::kStopped};
  bool initialization_succeeded_{false};
  float last_frame_delta_seconds_ = 0.0f;
  float light_env_elapsed_seconds_ = 0.0f;
  std::optional<std::uint8_t> screen_effect_light_param_slot_override_;
  bool suppress_local_lighting_ = false;

  float camera_far_clip_ = 0.0f;
  LightFogParams outdoor_fog_{};
  uint16_t screen_width_ = 1280;
  uint16_t screen_height_ = 720;

  std::unordered_map<TileCoord, std::unique_ptr<LoadedTile>, TileCoordHash> loaded_tiles_;

  struct StagingRequest {
    MapGeneration generation{};
    StreamOwnerHandle owner{};
    std::uint64_t request_id{0};
    std::uint32_t task_id{0};
    float publication_priority{0.0f};
  };

  struct TilePublicationCandidate {
    float priority{0.0f};
    std::uint64_t request_id{0};
    TileCoord coord{};
  };
  struct TilePublicationFarther {
    bool operator()(const TilePublicationCandidate &lhs,
                    const TilePublicationCandidate &rhs) const noexcept {
      if (lhs.priority != rhs.priority) {
        return lhs.priority > rhs.priority;
      }
      return lhs.request_id > rhs.request_id;
    }
  };
  struct WmoPublicationCandidate {
    float priority{0.0f};
    std::uint64_t request_id{0};
    std::string path;
  };
  struct WmoGroupPublicationCandidate {
    float priority{0.0f};
    std::uint64_t request_id{0};
    WmoGroupRequestKey key;
  };
  struct WmoGroupPublicationFarther {
    bool operator()(const WmoGroupPublicationCandidate& lhs,
                    const WmoGroupPublicationCandidate& rhs) const noexcept {
      if (lhs.priority != rhs.priority) {
        return lhs.priority > rhs.priority;
      }
      return lhs.request_id > rhs.request_id;
    }
  };
  struct WmoPublicationFarther {
    bool operator()(const WmoPublicationCandidate &lhs,
                    const WmoPublicationCandidate &rhs) const noexcept {
      if (lhs.priority != rhs.priority) {
        return lhs.priority > rhs.priority;
      }
      return lhs.request_id > rhs.request_id;
    }
  };

  core::ThreadPoolSystem world_staging_workers_;
  std::shared_ptr<WorldStagingMailbox> world_staging_mailbox_ =
      std::make_shared<WorldStagingMailbox>();
  MapGeneration world_staging_generation_{};
  std::uint64_t next_world_staging_request_id_{1};
  std::uint64_t world_staging_pump_sequence_{0};
  std::uint64_t presentation_drain_sequence_{0};
  std::uint64_t next_wmo_collision_owner_id_{1};
  std::unordered_map<TileCoord, StagingRequest, TileCoordHash> pending_tile_loads_;
  std::unordered_map<TileCoord, StagedWorldTile, TileCoordHash> ready_tile_loads_;
  std::priority_queue<TilePublicationCandidate,
                      std::vector<TilePublicationCandidate>,
                      TilePublicationFarther>
      ready_tile_publications_;
  std::unordered_map<std::string, StagingRequest> pending_wmo_loads_;
  std::unordered_map<std::string, StagedWorldWmo> ready_wmo_loads_;
  std::priority_queue<WmoPublicationCandidate,
                      std::vector<WmoPublicationCandidate>,
                      WmoPublicationFarther>
      ready_wmo_publications_;
  std::unordered_map<std::string, WmoCpuLoadRetryState> wmo_cpu_retry_;
  std::map<WmoGroupRequestKey, StagingRequest> pending_wmo_group_loads_;
  std::map<WmoGroupRequestKey, StagedWorldWmoGroup> ready_wmo_group_loads_;
  std::priority_queue<WmoGroupPublicationCandidate,
                      std::vector<WmoGroupPublicationCandidate>,
                      WmoGroupPublicationFarther>
      ready_wmo_group_publications_;
  std::size_t stream_progress_total_{0};
  std::size_t stream_progress_completed_{0};
  BlockingLoadProgressCallback active_stream_progress_callback_;

  static constexpr std::size_t kTilePublicationBudgetPerFrame = 2u;
  static constexpr std::size_t kWmoPublicationBudgetPerFrame = 8u;
  static constexpr std::uint64_t kWmoPublicationByteBudgetPerFrame =
      8u * 1024u * 1024u;
  static constexpr auto kWmoPublicationTimeBudgetPerFrame =
      std::chrono::milliseconds(2);

  LoadFileCallback load_file_;

  BlockingLoadProgressCallback blocking_load_progress_callback_;

  TileLoadedCallback tile_loaded_callback_;
  TileUnloadedCallback tile_unloaded_callback_;

  const openwow::data::dbc::DbcLoader *dbc_{nullptr};

  world::WorldLighting lighting_;
  world::SkyColors active_sky_colors_{};
  std::optional<world::ZoneSkyboxEntry> active_primary_skybox_;
  WorldEnvironmentPresentation environment_{};

  TerrainStreamer terrain_streamer_;

  world::Frustum frustum_;

  float fog_start_{300.0f};
  float fog_end_{800.0f};
  float fog_density_{0.0f};
  Vec4 fog_color_{0.6f, 0.7f, 0.85f, 1.0f};

  static constexpr float kTileSize = 533.33333f;

  static constexpr float kMapMidPoint = 32.0f;

  static constexpr int32_t kMinTile = 0;
  static constexpr int32_t kMaxTile = 63;
};

}

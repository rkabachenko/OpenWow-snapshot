
#pragma once

namespace openwow::audio { class SoundRuntime; }

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
struct TaxiPathNodeEntry;
struct WorldMapContinentEntry;
}

namespace openwow::core {

struct LoadingScreenElementCatalog;

struct RetailDebugActivePlayerState {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float orientation = 0.0f;
};

struct RetailDebugObjectManagerStatus {
  std::uint32_t tracked_count = 0;
  std::uint32_t pending_free_count = 0;
};

struct RetailDebugCommandBindings {
  std::function<std::optional<RetailDebugActivePlayerState>()> get_active_player_state;
  std::function<std::optional<RetailDebugObjectManagerStatus>()>
      get_object_manager_status;
  std::function<const openwow::data::dbc::DbcLoader *()> get_dbc_loader;
  std::function<bool(std::uint32_t)> is_valid_map_id;
};

void fn_delete_array(void **thisPtr);

void MoveLogFile_ref();

bool TryGetMovementRuntimeTimestampFloor(std::uint32_t &timestamp_floor_ms);

void CMovementRuntime_PushPreviousTransportContext(std::uint32_t transport_id);

void CMovementRuntime_SetMovementTimestamp(std::uint32_t timestamp_ms);

std::uint32_t CMovementRuntime_GetMovementTimestamp();

void CMovementRuntime_RestoreMovementTimestampState(
    std::uint32_t current_timestamp_ms,
    std::uint32_t previous_timestamp_ms);

void CMovementRuntime_MarkTransportTimestampTransition();

bool CMovementRuntime_TakePendingTransportTime2(std::uint32_t &time2_ms);

int AsyncIO_RegisterCVars();

int GameCleanup();

int fn_timingMethod();

int CompareFunction(const char **a1, const char **a2);

char *AppendRealmInfoToCrashDump(char *buf, int buf_size);

int LaunchWowError(int a1, int a2, int a3, int a4, int a5);

char *AppendLocalZoneInfoToCrashDump(void *obj, char *buf, int buf_size);

void SetRetailDebugCommandBindings(RetailDebugCommandBindings bindings);
[[nodiscard]] std::optional<RetailDebugActivePlayerState>
GetRetailDebugActivePlayerState();
[[nodiscard]] std::optional<RetailDebugObjectManagerStatus>
GetRetailDebugObjectManagerStatus();
[[nodiscard]] const openwow::data::dbc::DbcLoader *GetRetailDebugDbcLoader();
void Console_RegisterDebugCommands();
void Console_UnregisterDebugCommands();

struct StormInitParamsBlob {
  std::array<std::uint32_t, 6> words{};
};

int Storm_StoreInitParams(void *event_data, int param);
void LoadingScreen_RegisterStormInitHandlers();
bool Storm_ClearInitHandlersAndReplayCachedParams();

struct LoadingScreenWorldBackgroundVertex {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct LoadingScreenWorldBackgroundTexCoord {
  float u = 0.0f;
  float v = 0.0f;
};

inline constexpr std::size_t kLoadingScreenWorldBackgroundTileCount = 12;
inline constexpr std::size_t kLoadingScreenWorldBackgroundVerticesPerTile = 4;
inline constexpr std::size_t kLoadingScreenWorldBackgroundVertexCount =
    kLoadingScreenWorldBackgroundTileCount * kLoadingScreenWorldBackgroundVerticesPerTile;

struct LoadingScreenWorldBackgroundGeometry {
  std::array<LoadingScreenWorldBackgroundVertex, kLoadingScreenWorldBackgroundVertexCount>
      positions{};
  std::array<LoadingScreenWorldBackgroundTexCoord, kLoadingScreenWorldBackgroundVertexCount>
      texcoords{};
};

LoadingScreenWorldBackgroundGeometry LoadingScreen_InitWorldBackgroundQuads();

void InitGameSubsystems_InitializeUiShaders(const openwow::data::dbc::DbcLoader &dbc_loader);

[[nodiscard]] const LoadingScreenElementCatalog *GetLoadingScreenTaxiPathCatalog();
[[nodiscard]] const LoadingScreenWorldBackgroundGeometry &GetLoadingScreenWorldBackgroundGeometry();

const char *LoadingScreen_SetTextSource(const char *text);

const char *fn_TRIAL_LOADING_MESSAGE(char enable);

bool LoadingScreen_HasRenderLayer();

int GxDrawEnabled();

int GxRenderEnabled();

void DynamicElementVert_Resize(void **thisPtr, uint32_t new_count);

void LoadingScreenTaxiPathInfo_Resize(void **thisPtr, uint32_t new_count);

struct DynamicElementVert {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float u = 0.0f;
  float v = 0.0f;
  std::uint32_t color = 0;
};

static_assert(sizeof(DynamicElementVert) == 24);

struct LoadingScreenDynamicOverlayVertices {
  std::uint32_t capacity = 0;
  std::uint32_t count = 0;
  DynamicElementVert *vertices = nullptr;
  std::uint32_t growth_quantum = 0;
};

struct LoadingScreenDynamicMapChangeAssets {
  bool dynamic_elements_loaded = false;
  std::uint32_t world_tile_texture_count = 0;
  LoadingScreenDynamicOverlayVertices overlay_vertices{};
};

void LoadingScreen_DynamicOverlayVertices_EnsureCount(LoadingScreenDynamicOverlayVertices *thisPtr,
                                                      std::uint32_t new_count);

bool LoadingScreen_BuildMapChangeOverlay(std::uint32_t path_segment_index,
                                         std::uint32_t loading_path_id);

bool LoadingScreen_BuildMapChangeOverlayForPreviousMap(
    std::uint32_t loading_path_id, std::uint32_t previous_map_id);

[[nodiscard]] LoadingScreenDynamicMapChangeAssets &LoadingScreen_GetDynamicMapChangeAssets();

void LoadingScreen_CleanupDynamicMapChangeAssets();

struct LoadingScreenTaxiPathInfo {
  std::int32_t start_element_id = 0;
  std::int32_t end_element_id = 0;
};

struct LoadingScreenCatalogSourceElement {
  std::int32_t element_id = 0;
  std::uint32_t group_index = 0;
  std::int32_t boundary_key = 0;
  std::uint32_t flags = 0;
};

struct LoadingScreenElementGroup {
  std::uint32_t reserved_0 = 0;
  std::uint32_t segment_count = 0;
  const std::uint32_t *segment_indices = nullptr;
  std::uint32_t reserved_c = 0;
};

struct LoadingScreenElementCatalog {
  std::uint32_t reserved_0 = 0;
  std::uint32_t group_count = 0;
  const LoadingScreenElementGroup *groups = nullptr;
  std::uint32_t reserved_c = 0;
  std::uint32_t reserved_10 = 0;
  std::uint32_t reserved_14 = 0;
  const LoadingScreenTaxiPathInfo *taxi_path_infos = nullptr;
};

struct LoadingScreenElementCatalogStorage {
  struct GroupStorage {
    std::vector<std::uint32_t> segment_indices;
  };

  void Reset(std::uint32_t group_count);
  [[nodiscard]] LoadingScreenElementCatalog AsCatalog() const;

  std::vector<GroupStorage> groups;
  std::vector<LoadingScreenTaxiPathInfo> taxi_path_infos;

private:
  mutable std::vector<LoadingScreenElementGroup> group_views_;
};

std::uint32_t LoadingScreen_BuildTaxiPathCatalog(
    LoadingScreenElementCatalogStorage &catalog,
    std::span<const LoadingScreenCatalogSourceElement> source_elements, std::uint32_t group_count);

std::uint32_t LoadingScreen_BuildTaxiPathCatalogFromTaxiPathNodes(
    LoadingScreenElementCatalogStorage &catalog,
    std::span<const openwow::data::dbc::TaxiPathNodeEntry> taxi_path_nodes,
    std::uint32_t group_count);

bool LoadingScreen_TryGetTaxiPathInfo(const LoadingScreenElementCatalog *thisPtr,
                                      std::uint32_t group_idx, std::uint32_t segment_idx,
                                      std::int32_t *out_start_element_id,
                                      std::int32_t *out_end_element_id);

int LoadingScreen_FindElementByTexture(const LoadingScreenElementCatalog *thisPtr, void *context,
                                       std::uint32_t group_idx, int texture_id);

struct C3VectorNTempestOwnedBuffers {
  void *slot_79_buffer = nullptr;

  void *slot_109_buffer = nullptr;

};

void C3VectorNTempest_Destructor(C3VectorNTempestOwnedBuffers *thisPtr);

void LoadingScreen_CleanupResources(openwow::audio::SoundRuntime& sound_runtime);

void GxRenderTarget_Cleanup();

}

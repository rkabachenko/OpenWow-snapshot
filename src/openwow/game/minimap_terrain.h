#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui {
class MinimapSystem;
}

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

struct MinimapTerrainTile {
    float   boundsMinX;
    float   boundsMinY;
    float   boundsMaxX;
    float   boundsMaxY;
    float   boundsMinZ;
    float   boundsMaxZ;
    void*   renderNext;
    float   sortDistance;
    void*   textureHandle;
    uint32_t flags;
    uint32_t mapIdY;
    uint32_t mapIdX;
    int32_t  mapId;
};

inline constexpr uint32_t kPartyMaxSlots = 4;

inline constexpr uint32_t kBGMaxSlots = 40;

struct BGRosterEntry {
    uint64_t guid;
    uint32_t memberId;
};

struct MinimapUnitBlip {
    uint64_t guid;
    char     name[48];
    uint32_t outOfRange;
    uint32_t inRange;
    float    worldX;
    float    worldY;
    float    facing;
    uint32_t creatureType;
};

struct PartyMemberMiniState {
    uint16_t flags;
    uint16_t areaId;
    int16_t  posX;
    int16_t  posY;
};

struct MinimapBlipCallbacks {

    uint64_t activePlayerGuid = 0;

    bool (*resolveWorldUnit)(uint64_t guid,
                             float& outX, float& outY, float& outZ,
                             uint8_t& outCreatureType) = nullptr;

    const PartyMemberMiniState* (*getPartyMemberState)(uint32_t slot) = nullptr;

    const PartyMemberMiniState* (*getRaidMemberState)(uint64_t guid) = nullptr;

    int32_t (*getAreaMapId)(uint16_t areaId) = nullptr;

    int32_t currentMapId = -1;

    bool (*lookupName)(uint64_t guid,
                       char* outName, uint32_t nameCapacity,
                       uint32_t* outCreatureType) = nullptr;

    bool (*isCharmedByBattlefieldVehicle)(uint64_t guid) = nullptr;
};

struct POIDirectionEntry {
    char     name[64];
    float    distance;
    uint32_t isSpecial;
    int32_t  poiIndex;
};

struct MinimapAreaPOI {
    float       priority{0.0f};
    float       worldX{0.0f};
    float       worldY{0.0f};
    uint8_t     flags{0};
    const char* name{nullptr};
    uint32_t    worldStateId{0};
};

inline constexpr uint32_t kMaxNearestPOIArrows = 3;

inline constexpr uint32_t kSpecialLandmarkCount = 4;

inline constexpr float kPOIDistanceCullFraction = 0.80000001f;

inline constexpr float kPOIArrowMaxDistance = 694.44446f;

inline constexpr uint32_t kCorpseSpecialLandmarkIndex = 2;

inline constexpr uint32_t kSpiritHealerSpecialLandmarkIndex = 3;

inline constexpr uint32_t kOutdoorZoomSizes[] = {14, 12, 10, 8, 6, 4};
inline constexpr float kMinimapChunkTileSize = 533.33331f;
inline constexpr float kMinimapChunkHalfTileSpan = 266.66666f;
inline constexpr float kMinimapChunkGridMaxCoord = 17066.666f;
inline constexpr std::int32_t kMinimapChunkGridDimension = 64;

struct MinimapChunkCoords {
  std::int32_t row = 0;
  std::int32_t column = 0;
};

struct MinimapChunkWorldBounds {
  float min_x = 0.0f;
  float max_x = 0.0f;
  float min_y = 0.0f;
  float max_y = 0.0f;
};

struct MinimapChunkWindowSlot {
  std::string texture_path;
  std::int32_t row = -1;
  std::int32_t column = -1;
};

[[nodiscard]] MinimapChunkCoords Minimap_WorldToChunkCoords(float world_x,
                                                            float world_y) noexcept;
[[nodiscard]] MinimapChunkWorldBounds Minimap_ChunkCoordsToWorldBounds(
    std::int32_t row, std::int32_t column) noexcept;

[[nodiscard]] MinimapChunkCoords Minimap_ComputeChunkWindowOrigin(
    float world_x, float world_y) noexcept;

void Minimap_RegisterViolenceLevelCVar();

void Minimap_StripMapPathToBaseName(void* areaInfo);

int Minimap_SetZoomLevel(openwow::ui::MinimapSystem& minimap,
                         uint32_t level);

int Minimap_GetZoomLevel(const openwow::ui::MinimapSystem& minimap);

void* Minimap_GetAndClearDirtyFlag(uint32_t* outDirtyFlag);

void* WorldMap_GetSpecialLandmarkSource();

float Minimap_GetVisibleRadius();

void Minimap_UpdateNearestPOIDirections();

using WorldStateQueryFn = int (*)(uint32_t worldStateId);

void Minimap_SetWorldStateQueryCallback(WorldStateQueryFn fn);

[[nodiscard]] uint32_t Minimap_GetNearestPOICount();

[[nodiscard]] float Minimap_GetNearestPOIAngle(uint32_t index);

[[nodiscard]] uint32_t Minimap_GetNearestPOISlotIndex(uint32_t index);

[[nodiscard]] const MinimapAreaPOI* Minimap_GetNearestPOIRecord(uint32_t index);

void Minimap_TickPOIDirections(const openwow::ui::MinimapSystem& minimap_system,
                               int32_t map_id, float player_x, float player_y);

[[nodiscard]] bool Minimap_IsPOIRenderDirty();

[[nodiscard]] bool Minimap_IsPOINeedsRebuild();

[[nodiscard]] const std::vector<const MinimapAreaPOI*>&
Minimap_GetVisiblePOIList();

void Minimap_SetAreaPOIList(const std::vector<MinimapAreaPOI*>& area_pois);

[[nodiscard]] MinimapAreaPOI& Minimap_GetSpecialLandmark(uint32_t index);

void Minimap_SetCurrentMapId(int32_t mapId);

void Minimap_ResetPOIDirectionState();

void Minimap_UpdateUnitBlip(uint64_t guid, MinimapUnitBlip* blip,
                            const MinimapBlipCallbacks* callbacks,
                            uint32_t slotIndex,
                            bool isBGEntry);

void Minimap_LoadPartyMemberBlips(void* minimapData);

void Minimap_ValidateBGBlips(void* minimapData);

void Minimap_SetCorpseMarker(float x, float y);

void Minimap_SetSpiritHealerMarker(float x, float y);

int Minimap_UpdatePOIArrows(void* arrowArray);

bool Minimap_UpdateTerrainTiles(void* worldObj, int mapId,
                                 float* playerPos, void* cameraData,
                                 void* tileGrid, void* outputState,
                                 int forceUpdate);

int Minimap_InitRenderTarget(
    const openwow::ui::MinimapSystem& minimap, int mapId);

using AreaPOIProviderFn = std::vector<MinimapAreaPOI> (*)(int32_t mapId);

void Minimap_SetAreaPOIProviderForTests(AreaPOIProviderFn provider);

void Minimap_BindAreaPOIDbcLoader(const openwow::data::dbc::DbcLoader* dbc);

using MinimapEventFireFn = void (*)(const char* event_name);
void Minimap_SetEventFireCallbackForTests(MinimapEventFireFn callback);

[[nodiscard]] bool Minimap_IsInitRenderTargetCalled();

void POIDIRECTIONDATA_SetCapacity(uint32_t newCapacity);

void POIDIRECTIONDATA_SetCountUninitialized(uint32_t newCount);

[[nodiscard]] uint32_t POIDIRECTIONDATA_GetCapacityForTests();

[[nodiscard]] uint32_t POIDIRECTIONDATA_GetCountForTests();

[[nodiscard]] const POIDirectionEntry* POIDIRECTIONDATA_GetEntryForTests(
    uint32_t index);

void POIDIRECTIONDATA_ResetForTests();

void* Minimap_FindExistingTerrainTile(void* tileArray, const int* tileKey,
                                       int currentMap, float playerDist);

void Minimap_BuildSortedRttTileSlotList(void* tile_array,
                                        std::uint8_t** out_sorted_head);

uint32_t Minimap_LoadSingleTerrainTile(const int* tileData, void* tileArray,
                                        int currentMap, float playerDist,
                                        const char* mapBaseName);

void Minimap_ResetTerrainTextureTranslations();
bool Minimap_LoadTerrainTextureTranslations();

bool Minimap_LoadTerrainTextureTranslationsFromText(std::string_view text);
bool Minimap_ResolveTerrainTexturePath(const int* tileData,
                                       const char* mapBaseName,
                                       char* outPath,
                                       std::size_t outPathChars);
bool Minimap_LoadTerrainTextureTranslationsFromTextForTests(
    std::string_view text);
void Minimap_SetUnitBlipRuntimeForTesting(float player_x,
                                          float player_y,
                                          bool is_indoor,
                                          std::uint32_t zoom_level);

void Minimap_SetBGRosterForTesting(const BGRosterEntry* entries,
                                   uint32_t count,
                                   uint32_t selfMemberId);

void Minimap_SetPartyMembersForTesting(const uint64_t* guids, uint32_t count);

void Minimap_SetBlipCallbacksForTesting(const MinimapBlipCallbacks* cb);

bool Minimap_ResolveChunkTexturePath(const char* continent_name,
                                     std::int32_t row,
                                     std::int32_t column,
                                     char* out_path,
                                     std::size_t out_path_chars);

void Minimap_UpdateChunkTextureWindow(
    const MinimapChunkCoords& origin,
    bool continent_changed,
    const char* continent_name,
    std::array<MinimapChunkWindowSlot, 4>& slots);

void* MINIMAPMD5NAME_Create(void* list, int extraSize, uint8_t flags);

}

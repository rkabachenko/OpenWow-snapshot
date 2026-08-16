
#include "openwow/game/minimap_terrain.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/client_config.h"
#include "openwow/game/localization.h"
#include "openwow/core/storm_ref_counted.h"
#include "openwow/core/storm_containers.h"
#include "openwow/core/storm_string.h"
#include "openwow/world/environment/day_night.h"
#include "openwow/game/violence_level.h"
#include "openwow/foundation/math/planar_facing_angle.h"
#include "openwow/render/resources/textures/texture_asset.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/vfs/sfile_core.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace openwow::game {

using openwow::core::TSGrowableArray;

namespace {

constexpr std::string_view kOutdoorZoomCVarName = "minimapZoom";
constexpr std::string_view kIndoorZoomCVarName = "minimapInsideZoom";
constexpr std::uint32_t kMinimapTerrainTileCount = 256u;
constexpr std::size_t kMinimapTerrainTileStride = 164u;
constexpr std::size_t kMinimapTerrainTileSortDistanceOffset = 140u;
constexpr std::size_t kMinimapTerrainTileTextureHandleOffset = 144u;
constexpr std::size_t kMinimapTerrainTileFlagsOffset = 148u;
constexpr std::size_t kMinimapTerrainTileGridYOffset = 152u;
constexpr std::size_t kMinimapTerrainTileGridXOffset = 156u;
constexpr std::size_t kMinimapTerrainTileMapIdOffset = 160u;
constexpr std::size_t kMinimapTerrainTileBoundsOffset = 112u;
constexpr std::size_t kMinimapTerrainTileRenderNextOffset = 136u;
constexpr std::uint32_t kMinimapTerrainTileResidentFlag = 0x2u;
constexpr std::uint32_t kMinimapTerrainTileLoadingFlag = 0x4u;
constexpr std::size_t kMinimapTranslatedTextureNameCapacity = 40u;
constexpr std::size_t kMinimapTerrainPathBufferChars = 260u;
constexpr std::string_view kMinimapTextureRoot = "Textures\\Minimap";
constexpr std::string_view kMinimapMd5TranslateFilename = "Textures\\Minimap\\md5translate.trs";
constexpr std::string_view kMinimapTranslationDirectoryPrefix = "dir:";
constexpr std::string_view kMinimapChunkLookupFormat = "%s\\map%d_%02d.blp";
constexpr std::array<std::int32_t, 4> kMinimapChunkRowOffsets = {0, 1, 1, 0};
constexpr std::array<std::int32_t, 4> kMinimapChunkColumnOffsets = {0, 0, 1, 1};

struct MinimapTerrainTranslationEntry {
    std::string translated_path;
};

struct MinimapTerrainTranslationTable {
    std::unordered_map<std::string, MinimapTerrainTranslationEntry> entries;
};
bool s_minimapTerrainTranslationsLoaded = false;

struct MinimapTerrainTileKey {
    std::int32_t map_id = -1;
    std::int32_t grid_y = -1;
    std::int32_t grid_x = -1;

    [[nodiscard]] bool operator==(const MinimapTerrainTileKey& other) const = default;
};

struct MinimapTerrainTileCacheState {
    std::array<std::uint16_t, kMinimapTerrainTileCount> mru_slots{};
    std::array<std::int16_t, kMinimapTerrainTileCount> positions{};
    std::array<MinimapTerrainTileKey, kMinimapTerrainTileCount> keys{};
    std::array<openwow::render::TextureAssetPtr,
               kMinimapTerrainTileCount> textures{};
    std::size_t active_count = 0;
};

[[nodiscard]] std::uint8_t* GetMinimapTerrainTileSlot(void* tile_array,
                                                      const std::uint32_t slot) {
    return static_cast<std::uint8_t*>(tile_array) +
           static_cast<std::size_t>(slot) * kMinimapTerrainTileStride;
}

[[nodiscard]] MinimapTerrainTranslationTable&
GetMinimapTerrainTranslationTable() {
    static MinimapTerrainTranslationTable table;
    return table;
}

[[nodiscard]] std::string NormalizeMinimapTranslationKey(
    std::string_view raw_key) {
    std::string normalized;
    normalized.reserve(raw_key.size());
    for (const char raw_ch : raw_key) {
        unsigned char ch = static_cast<unsigned char>(raw_ch);
        if (ch == '/') {
            ch = '\\';
        }
        normalized.push_back(
            static_cast<char>(std::tolower(ch)));
    }
    return normalized;
}

void InsertMinimapTerrainTranslation(std::string_view raw_key,
                                     std::string_view raw_value) {
    auto& table = GetMinimapTerrainTranslationTable();
    const std::string normalized_key = NormalizeMinimapTranslationKey(raw_key);
    if (table.entries.contains(normalized_key)) {
        return;
    }

    char translated_path[kMinimapTranslatedTextureNameCapacity]{};
    openwow::core::SStrCopy(
        translated_path, std::string(raw_value).c_str(),
        kMinimapTranslatedTextureNameCapacity);
    table.entries.emplace(normalized_key,
                          MinimapTerrainTranslationEntry{translated_path});
}

void LoadMinimapTerrainTranslationsFromText(std::string_view text) {
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const std::size_t line_begin = cursor;
        while (cursor < text.size() && text[cursor] != '\r' &&
               text[cursor] != '\n' && text[cursor] != '\0') {
            ++cursor;
        }

        const std::string_view line = text.substr(line_begin, cursor - line_begin);
        while (cursor < text.size() &&
               (text[cursor] == '\r' || text[cursor] == '\n')) {
            ++cursor;
        }

        if (line.empty()) {
            break;
        }

        if (line.size() >= kMinimapTranslationDirectoryPrefix.size() &&
            openwow::core::SStrCmpNoCase(
                std::string(line.substr(0, kMinimapTranslationDirectoryPrefix.size())).c_str(),
                kMinimapTranslationDirectoryPrefix.data(),
                kMinimapTranslationDirectoryPrefix.size()) == 0) {
            continue;
        }

        const std::size_t tab_pos = line.find('\t');
        if (tab_pos == std::string_view::npos) {
            continue;
        }

        InsertMinimapTerrainTranslation(line.substr(0, tab_pos),
                                        line.substr(tab_pos + 1u));
    }
}

[[nodiscard]] bool TryResolveMinimapTranslatedTexturePath(
    std::string_view lookup_key, char* out_path,
    std::size_t out_path_chars);

[[nodiscard]] bool TryResolveMinimapTerrainTexturePath(const int* tileData,
                                                       const char* mapBaseName,
                                                       char* outPath,
                                                       const std::size_t outPathChars) {
    if (tileData == nullptr || mapBaseName == nullptr || *mapBaseName == '\0' ||
        outPath == nullptr || outPathChars == 0u) {
        if (outPath != nullptr && outPathChars != 0u) {
            outPath[0] = '\0';
        }
        return false;
    }

    char lookup_key[kMinimapTerrainPathBufferChars]{};
    openwow::core::SStrPrintf(lookup_key, sizeof(lookup_key),
                              "%s_%03d_%02d_%02d.blp", mapBaseName,
                              tileData[0], tileData[1], tileData[2]);

    return TryResolveMinimapTranslatedTexturePath(lookup_key, outPath,
                                                  outPathChars);
}

[[nodiscard]] bool TryResolveMinimapTranslatedTexturePath(
    const std::string_view lookup_key, char* outPath,
    const std::size_t outPathChars) {
    if (outPath == nullptr || outPathChars == 0u) {
        return false;
    }

    const auto& table = GetMinimapTerrainTranslationTable();
    const auto it =
        table.entries.find(NormalizeMinimapTranslationKey(lookup_key));
    if (it == table.entries.end()) {
        outPath[0] = '\0';
        return false;
    }

    openwow::core::SStrPrintf(outPath, outPathChars, "%s\\%s",
                              kMinimapTextureRoot.data(),
                              it->second.translated_path.c_str());
    return true;
}

[[nodiscard]] bool TryResolveMinimapChunkTexturePath(
    const char* continent_name, const std::int32_t row,
    const std::int32_t column, char* out_path,
    const std::size_t out_path_chars) {
    if (continent_name == nullptr || *continent_name == '\0' ||
        out_path == nullptr || out_path_chars == 0u) {
        if (out_path != nullptr && out_path_chars != 0u) {
            out_path[0] = '\0';
        }
        return false;
    }

    char lookup_key[kMinimapTerrainPathBufferChars]{};
    openwow::core::SStrPrintf(lookup_key, sizeof(lookup_key),
                              kMinimapChunkLookupFormat.data(),
                              continent_name, row, column);
    return TryResolveMinimapTranslatedTexturePath(lookup_key, out_path,
                                                  out_path_chars);
}

template <typename T>
[[nodiscard]] T ReadMinimapTerrainTileField(const std::uint8_t* tile_slot,
                                            const std::size_t offset) {
    T value{};
    std::memcpy(&value, tile_slot + offset, sizeof(T));
    return value;
}

template <typename T>
void WriteMinimapTerrainTileField(std::uint8_t* tile_slot,
                                  const std::size_t offset,
                                  const T& value) {
    std::memcpy(tile_slot + offset, &value, sizeof(T));
}

[[nodiscard]] MinimapTerrainTileKey ReadMinimapTerrainTileKey(
    const std::uint8_t* tile_slot) {
    return {
        .map_id = ReadMinimapTerrainTileField<std::int32_t>(
            tile_slot, kMinimapTerrainTileMapIdOffset),
        .grid_y = ReadMinimapTerrainTileField<std::int32_t>(
            tile_slot, kMinimapTerrainTileGridYOffset),
        .grid_x = ReadMinimapTerrainTileField<std::int32_t>(
            tile_slot, kMinimapTerrainTileGridXOffset),
    };
}

[[nodiscard]] bool MinimapTerrainTileIsResident(const std::uint8_t* tile_slot) {
    return (ReadMinimapTerrainTileField<std::uint32_t>(
                tile_slot, kMinimapTerrainTileFlagsOffset) &
            kMinimapTerrainTileResidentFlag) != 0u;
}

void SetMinimapTerrainTileResident(std::uint8_t* tile_slot) {
    const auto flags = ReadMinimapTerrainTileField<std::uint32_t>(
        tile_slot, kMinimapTerrainTileFlagsOffset);
    WriteMinimapTerrainTileField(
        tile_slot, kMinimapTerrainTileFlagsOffset,
        flags | kMinimapTerrainTileResidentFlag);
}

void UpdateMinimapTerrainTileSortDistance(std::uint8_t* tile_slot,
                                          const std::int32_t current_map,
                                          const float player_dist) {
    const auto tile_key = ReadMinimapTerrainTileKey(tile_slot);
    float sort_distance = 3.4028235e38f;
    if (tile_key.map_id != current_map) {
        const float bounds_min_z = ReadMinimapTerrainTileField<float>(
            tile_slot, kMinimapTerrainTileBoundsOffset + sizeof(float) * 2u);
        const float bounds_max_z = ReadMinimapTerrainTileField<float>(
            tile_slot, kMinimapTerrainTileBoundsOffset + sizeof(float) * 5u);
        sort_distance = (bounds_max_z + bounds_min_z) * 0.5f - player_dist;
    }

    WriteMinimapTerrainTileField(tile_slot, kMinimapTerrainTileSortDistanceOffset,
                                 sort_distance);
}

void ResetMinimapTerrainTileCacheState(MinimapTerrainTileCacheState& state) {
    state.active_count = 0;
    state.positions.fill(-1);
    state.keys.fill({});
    state.textures.fill({});
}

void RemoveMinimapTerrainTileFromCacheOrder(MinimapTerrainTileCacheState& state,
                                            const std::uint16_t slot) {
    const std::int16_t position = state.positions[slot];
    if (position < 0) {
        return;
    }

    for (std::size_t index = static_cast<std::size_t>(position) + 1u;
         index < state.active_count; ++index) {
        state.mru_slots[index - 1u] = state.mru_slots[index];
        state.positions[state.mru_slots[index - 1u]] =
            static_cast<std::int16_t>(index - 1u);
    }

    --state.active_count;
    state.positions[slot] = -1;
}

void AppendMinimapTerrainTileToCacheOrder(MinimapTerrainTileCacheState& state,
                                          const std::uint16_t slot,
                                          const MinimapTerrainTileKey& key) {
    if (state.positions[slot] >= 0) {
        state.keys[slot] = key;
        return;
    }

    state.mru_slots[state.active_count] = slot;
    state.positions[slot] = static_cast<std::int16_t>(state.active_count);
    state.keys[slot] = key;
    ++state.active_count;
}

void TouchMinimapTerrainTileCacheEntry(MinimapTerrainTileCacheState& state,
                                       const std::uint16_t slot,
                                       const MinimapTerrainTileKey& key) {
    RemoveMinimapTerrainTileFromCacheOrder(state, slot);

    for (std::size_t index = state.active_count; index > 0; --index) {
        state.mru_slots[index] = state.mru_slots[index - 1u];
        state.positions[state.mru_slots[index]] =
            static_cast<std::int16_t>(index);
    }

    state.mru_slots[0] = slot;
    state.positions[slot] = 0;
    state.keys[slot] = key;
    ++state.active_count;
}

[[nodiscard]] MinimapTerrainTileCacheState& GetMinimapTerrainTileCacheState(
    void* tile_array) {
    static std::unordered_map<std::uintptr_t, MinimapTerrainTileCacheState>
        cache_states;

    auto& state = cache_states[reinterpret_cast<std::uintptr_t>(tile_array)];
    if (state.active_count > kMinimapTerrainTileCount) {
        ResetMinimapTerrainTileCacheState(state);
    }

    for (std::size_t index = 0; index < state.active_count;) {
        const auto slot = state.mru_slots[index];
        const auto* tile_slot = GetMinimapTerrainTileSlot(tile_array, slot);
        if (!MinimapTerrainTileIsResident(tile_slot) ||
            state.keys[slot] != ReadMinimapTerrainTileKey(tile_slot)) {
            RemoveMinimapTerrainTileFromCacheOrder(state, slot);
            continue;
        }
        ++index;
    }

    for (std::uint32_t slot = 0; slot < kMinimapTerrainTileCount; ++slot) {
        const auto* tile_slot = GetMinimapTerrainTileSlot(tile_array, slot);
        if (!MinimapTerrainTileIsResident(tile_slot)) {
            continue;
        }

        AppendMinimapTerrainTileToCacheOrder(state, static_cast<std::uint16_t>(slot),
                                             ReadMinimapTerrainTileKey(tile_slot));
    }

    return state;
}

}

static float s_playerX = 0.0f;
static float s_playerY = 0.0f;
static float s_playerZ = 0.0f;
static bool  s_isIndoor = false;
static uint32_t s_outdoorZoom = 0;
static uint32_t s_indoorZoom = 0;
static uint32_t s_dirtyFlags = 0;
static uint32_t s_currentMapIdX = 0xFFFFFFFF;
static bool  s_poiNeedsRebuild = false;
static float s_waypointX = 0.0f;
static float s_waypointY = 0.0f;
static uint32_t s_waypointTimeout = 0;

static int32_t s_poiMapId = -1;

static std::vector<MinimapAreaPOI*> s_poiRecords;
static std::vector<uint32_t> s_poiActive;
static std::vector<uint32_t> s_poiOutOfRange;

static std::vector<const MinimapAreaPOI*> s_visiblePOIs;

static bool s_poiRenderDirty = false;

static uint32_t s_nearestPOICount = 0;
static std::array<uint32_t, kMaxNearestPOIArrows>
    s_nearestPOIIndices = {~0u, ~0u, ~0u};
static std::array<float, kMaxNearestPOIArrows>
    s_nearestPOIAngles = {-1.0f, -1.0f, -1.0f};

static std::array<MinimapAreaPOI, kSpecialLandmarkCount> s_specialLandmarks{};

static std::array<std::array<char, 64>, kSpecialLandmarkCount>
    s_specialLandmarkNames{};

static WorldStateQueryFn s_worldStateQueryFn = nullptr;

static AreaPOIProviderFn s_areaPOIProvider = nullptr;

static const openwow::data::dbc::DbcLoader* s_areaPOIDbcLoader = nullptr;

static MinimapEventFireFn s_eventFireCallback = nullptr;

static std::vector<MinimapAreaPOI> s_ownedAreaPOIs;

static bool s_initRenderTargetCalled = false;

static std::array<BGRosterEntry, kBGMaxSlots> s_bgRosterEntries{};
static uint32_t s_bgRosterCount = 0;
static uint32_t s_selfBGMemberId = 0;
static bool     s_selfBGMemberValid = false;

static std::array<uint64_t, kPartyMaxSlots> s_partyMemberGuids{};
static uint32_t s_partyMemberCount = 0;

static const MinimapBlipCallbacks* s_blipCallbacks = nullptr;

namespace {

void SyncStoredZoomStateFromUi(
    const openwow::ui::MinimapSystem& minimap_system) {
    s_isIndoor = minimap_system.IsIndoorMinimapActive();
    s_outdoorZoom = minimap_system.GetOutdoorZoomLevel();
    s_indoorZoom = minimap_system.GetIndoorZoomLevel();
}

const char* GetActiveZoomCVarName() {
    return s_isIndoor ? kIndoorZoomCVarName.data() : kOutdoorZoomCVarName.data();
}

}

void Minimap_RegisterViolenceLevelCVar() {
    auto& cvars = openwow::ui::game::CVarSystem::Instance();
    const std::string default_value =
        std::to_string(ClientViolenceLevelLocaleMaximum());

    cvars.RegisterCVar("violenceLevel", default_value,
                       openwow::ui::game::CVarFlags::Account,
                       "Sets the violence level of the game",
                       0.0f, 0.0f, 4);

}

void Minimap_StripMapPathToBaseName(void* areaInfo) {

    (void)areaInfo;
}

int Minimap_SetZoomLevel(openwow::ui::MinimapSystem& minimap_system,
                         uint32_t level) {
    SyncStoredZoomStateFromUi(minimap_system);

    const uint32_t oldZoom = s_isIndoor ? s_indoorZoom : s_outdoorZoom;
    const uint32_t newZoom = std::min(level, 5u);

    minimap_system.SetZoomLevel(newZoom);
    SyncStoredZoomStateFromUi(minimap_system);

    if (oldZoom != newZoom) {
        s_dirtyFlags |= 1u;
        (void)openwow::ui::game::CVarSystem::Instance().SetCVar(
            GetActiveZoomCVarName(), std::to_string(newZoom));
    }
    return static_cast<int>(newZoom);
}

int Minimap_GetZoomLevel(
    const openwow::ui::MinimapSystem& minimap_system) {
    SyncStoredZoomStateFromUi(minimap_system);
    return s_isIndoor ? static_cast<int>(s_indoorZoom)
                      : static_cast<int>(s_outdoorZoom);
}

void* Minimap_GetAndClearDirtyFlag(uint32_t* outDirtyFlag) {

    *outDirtyFlag = s_poiRenderDirty ? 1u : 0u;
    s_poiRenderDirty = false;
    return &s_visiblePOIs;
}

void* WorldMap_GetSpecialLandmarkSource() {
    return nullptr;
}

float Minimap_GetVisibleRadius() {
    if (s_isIndoor) {
        return kIndoorZoomRadii[std::min<uint32_t>(s_indoorZoom, 5u)];
    }
    return static_cast<float>(
               kOutdoorZoomSizes[std::min<uint32_t>(s_outdoorZoom, 5u)])
           * 0.5f * 33.333332f;
}

void Minimap_UpdateNearestPOIDirections() {

    if (s_poiMapId == -1) return;

    const uint32_t poiCount = static_cast<uint32_t>(s_poiRecords.size());

    s_visiblePOIs.clear();

    const float visibleRadius = Minimap_GetVisibleRadius();

    for (uint32_t i = 0; i < poiCount; ++i) {
        const MinimapAreaPOI* poi = s_poiRecords[i];

        if (poi->worldX == 0.0f && poi->worldY == 0.0f) {
            if (s_poiActive[i] == 0 && s_poiOutOfRange[i] == 0) continue;
            s_poiActive[i] = 0;
            s_poiOutOfRange[i] = 0;
            s_poiRenderDirty = true;
            continue;
        }

        if (poi->worldStateId != 0) {
            const int wsValue = s_worldStateQueryFn
                                    ? s_worldStateQueryFn(poi->worldStateId)
                                    : 0;
            if (!wsValue) {
                if (s_poiActive[i] == 0 && s_poiOutOfRange[i] == 0) continue;
                s_poiActive[i] = 0;
                s_poiOutOfRange[i] = 0;
                s_poiRenderDirty = true;
                continue;
            }
        }

        const double dx = static_cast<double>(s_playerX)
                          - static_cast<double>(poi->worldX);
        const double dy = static_cast<double>(s_playerY)
                          - static_cast<double>(poi->worldY);
        const double dist = std::sqrt(dx * dx + dy * dy);

        if (dist / static_cast<double>(visibleRadius) > kPOIDistanceCullFraction) {
            if (s_poiActive[i] == 0 && s_poiOutOfRange[i] == 1) continue;
            s_poiActive[i] = 0;
            s_poiOutOfRange[i] = 1;
            s_poiRenderDirty = true;
            continue;
        }

        if ((poi->flags & 0x02) == 0) {
            if (s_poiActive[i] == 0 && s_poiOutOfRange[i] == 0) continue;
            s_poiActive[i] = 0;
            s_poiOutOfRange[i] = 0;
            s_poiRenderDirty = true;
            continue;
        }

        s_visiblePOIs.push_back(poi);

        if (s_poiActive[i] == 1 && s_poiOutOfRange[i] == 0) continue;
        s_poiActive[i] = 1;
        s_poiOutOfRange[i] = 0;
        s_poiRenderDirty = true;
    }

    struct Candidate {
        float distance;
        float priority;
        uint32_t index;
    };

    std::array<Candidate, kMaxNearestPOIArrows> slots{};
    uint32_t filled = 0;
    int worstSlot = -1;

    const MinimapAreaPOI* corpseMarker =
        (poiCount > 0 && kCorpseSpecialLandmarkIndex < kSpecialLandmarkCount)
            ? &s_specialLandmarks[kCorpseSpecialLandmarkIndex]
            : nullptr;

    for (uint32_t i = 0; i < poiCount; ++i) {
        if (s_poiOutOfRange[i] == 0) continue;

        const MinimapAreaPOI* poi = s_poiRecords[i];
        const double dx = static_cast<double>(poi->worldX)
                          - static_cast<double>(s_playerX);
        const double dy = static_cast<double>(poi->worldY)
                          - static_cast<double>(s_playerY);
        const float dist = static_cast<float>(std::sqrt(dx * dx + dy * dy));

        if (dist > kPOIArrowMaxDistance && poi != corpseMarker) continue;

        const float pri = poi->priority;

        if (worstSlot == -1) {

            slots[filled] = {dist, pri, i};
            ++filled;

            if (filled == kMaxNearestPOIArrows) {

                worstSlot = 0;
                for (uint32_t s = 1; s < kMaxNearestPOIArrows; ++s) {
                    const auto& ws = slots[static_cast<uint32_t>(worstSlot)];
                    const auto& cs = slots[s];

                    uint32_t wBits, cBits;
                    std::memcpy(&wBits, &ws.priority, 4);
                    std::memcpy(&cBits, &cs.priority, 4);
                    const auto wSigned = static_cast<int32_t>(wBits);
                    const auto cSigned = static_cast<int32_t>(cBits);
                    if (cSigned > wSigned) {
                        worstSlot = static_cast<int>(s);
                    } else if (cSigned == wSigned &&
                               ws.distance < cs.distance) {
                        worstSlot = static_cast<int>(s);
                    }
                }
            }
        } else {

            auto& ws = slots[static_cast<uint32_t>(worstSlot)];
            uint32_t wBits, cBits;
            std::memcpy(&wBits, &ws.priority, 4);
            std::memcpy(&cBits, &pri, 4);
            const auto wSigned = static_cast<int32_t>(wBits);
            const auto cSigned = static_cast<int32_t>(cBits);

            if (cSigned < wSigned) {

                ws = {dist, pri, i};
            } else if (cSigned == wSigned && dist < ws.distance) {

                ws = {dist, pri, i};
            } else {
                continue;
            }

            worstSlot = 0;
            for (uint32_t s = 1; s < kMaxNearestPOIArrows; ++s) {
                const auto& w2 = slots[static_cast<uint32_t>(worstSlot)];
                const auto& c2 = slots[s];
                uint32_t w2Bits, c2Bits;
                std::memcpy(&w2Bits, &w2.priority, 4);
                std::memcpy(&c2Bits, &c2.priority, 4);
                const auto w2Signed = static_cast<int32_t>(w2Bits);
                const auto c2Signed = static_cast<int32_t>(c2Bits);
                if (c2Signed > w2Signed) {
                    worstSlot = static_cast<int>(s);
                } else if (c2Signed == w2Signed &&
                           w2.distance < c2.distance) {
                    worstSlot = static_cast<int>(s);
                }
            }
        }
    }

    if (s_nearestPOICount != filled) {
        s_nearestPOICount = filled;
        s_poiNeedsRebuild = true;
    } else {
        for (uint32_t j = 0; j < filled; ++j) {
            if (slots[j].index != s_nearestPOIIndices[j]) {
                s_poiNeedsRebuild = true;
                break;
            }
        }
    }

    for (uint32_t j = 0; j < filled; ++j) {
        const uint32_t poiIdx = slots[j].index;
        s_nearestPOIIndices[j] = poiIdx;

        const MinimapAreaPOI* poi = s_poiRecords[poiIdx];
        s_nearestPOIAngles[j] =
            openwow::math::ComputeRetailPlanarFacingAngle(
                openwow::math::PlanarPoint{s_playerX, s_playerY},
                openwow::math::PlanarPoint{poi->worldX, poi->worldY});
    }
}

void Minimap_SetWorldStateQueryCallback(WorldStateQueryFn fn) {
    s_worldStateQueryFn = fn;
}

uint32_t Minimap_GetNearestPOICount() { return s_nearestPOICount; }

float Minimap_GetNearestPOIAngle(uint32_t index) {
    return (index < kMaxNearestPOIArrows) ? s_nearestPOIAngles[index] : -1.0f;
}

uint32_t Minimap_GetNearestPOISlotIndex(uint32_t index) {
    return (index < kMaxNearestPOIArrows) ? s_nearestPOIIndices[index] : ~0u;
}

const MinimapAreaPOI* Minimap_GetNearestPOIRecord(uint32_t index) {
    if (index >= s_nearestPOICount) {
        return nullptr;
    }
    const uint32_t record_index = s_nearestPOIIndices[index];
    if (record_index >= s_poiRecords.size()) {
        return nullptr;
    }
    return s_poiRecords[record_index];
}

void Minimap_TickPOIDirections(
    const openwow::ui::MinimapSystem& minimap_system, const int32_t map_id,
    const float player_x, const float player_y) {

    SyncStoredZoomStateFromUi(minimap_system);

    if (s_poiMapId != map_id || s_poiRecords.empty()) {
        s_poiMapId = map_id;
        std::vector<MinimapAreaPOI*> records;
        if (s_areaPOIProvider != nullptr) {
            s_ownedAreaPOIs = s_areaPOIProvider(map_id);
            records.reserve(s_ownedAreaPOIs.size());
            for (auto& poi : s_ownedAreaPOIs) {
                records.push_back(&poi);
            }
        }
        Minimap_SetAreaPOIList(records);
    }
    s_playerX = player_x;
    s_playerY = player_y;
    Minimap_UpdateNearestPOIDirections();
}

bool Minimap_IsPOIRenderDirty() { return s_poiRenderDirty; }

bool Minimap_IsPOINeedsRebuild() { return s_poiNeedsRebuild; }

const std::vector<const MinimapAreaPOI*>& Minimap_GetVisiblePOIList() {
    return s_visiblePOIs;
}

void Minimap_SetAreaPOIList(const std::vector<MinimapAreaPOI*>& area_pois) {
    s_poiRecords.clear();
    s_poiRecords.reserve(area_pois.size() + kSpecialLandmarkCount);
    for (auto* p : area_pois) s_poiRecords.push_back(p);

    for (uint32_t i = 0; i < kSpecialLandmarkCount; ++i) {
        s_specialLandmarks[i].name = s_specialLandmarkNames[i].data();
        s_poiRecords.push_back(&s_specialLandmarks[i]);
    }

    const auto n = s_poiRecords.size();
    s_poiActive.assign(n, 0);
    s_poiOutOfRange.assign(n, 0);
}

MinimapAreaPOI& Minimap_GetSpecialLandmark(uint32_t index) {
    return s_specialLandmarks[std::min(index, kSpecialLandmarkCount - 1)];
}

void Minimap_SetCurrentMapId(int32_t mapId) { s_poiMapId = mapId; }

void Minimap_ResetPOIDirectionState() {
    s_poiMapId = -1;
    s_poiRecords.clear();
    s_poiActive.clear();
    s_poiOutOfRange.clear();
    s_visiblePOIs.clear();
    s_poiRenderDirty = false;
    s_poiNeedsRebuild = false;
    s_nearestPOICount = 0;
    s_nearestPOIIndices = {~0u, ~0u, ~0u};
    s_nearestPOIAngles = {-1.0f, -1.0f, -1.0f};
    for (auto& lm : s_specialLandmarks) lm = {};
    for (auto& nm : s_specialLandmarkNames) nm = {};
    s_worldStateQueryFn = nullptr;
    s_areaPOIProvider = nullptr;
    s_eventFireCallback = nullptr;
    s_ownedAreaPOIs.clear();
    s_initRenderTargetCalled = false;
}

void Minimap_SetAreaPOIProviderForTests(AreaPOIProviderFn provider) {
    s_areaPOIProvider = provider;
}

namespace {

std::vector<MinimapAreaPOI> MinimapAreaPOIProviderFromDbc(int32_t mapId) {
    std::vector<MinimapAreaPOI> result;
    if (s_areaPOIDbcLoader == nullptr) {
        return result;
    }

    constexpr std::uint32_t kAreaPOIShownOnMapFlag = 0x01u;
    const auto& entries = s_areaPOIDbcLoader->area_poi().entries();
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        if (static_cast<int32_t>(entry.map_id) != mapId ||
            (entry.flags & kAreaPOIShownOnMapFlag) == 0u) {
            continue;
        }

        MinimapAreaPOI poi;
        poi.priority = static_cast<float>(entry.importance);
        poi.worldX = entry.x;
        poi.worldY = entry.y;
        poi.flags = static_cast<uint8_t>(entry.flags);

        poi.name = entry.name.data();
        poi.worldStateId = entry.world_state_id;
        result.push_back(poi);
    }
    return result;
}

}

void Minimap_BindAreaPOIDbcLoader(const openwow::data::dbc::DbcLoader* dbc) {
    s_areaPOIDbcLoader = dbc;
    s_areaPOIProvider = dbc != nullptr ? &MinimapAreaPOIProviderFromDbc : nullptr;
}

void Minimap_SetEventFireCallbackForTests(MinimapEventFireFn callback) {
    s_eventFireCallback = callback;
}

bool Minimap_IsInitRenderTargetCalled() {
    return s_initRenderTargetCalled;
}

void Minimap_UpdateUnitBlip(uint64_t guid, MinimapUnitBlip* blip,
                            const MinimapBlipCallbacks* callbacks,
                            uint32_t slotIndex,
                            bool isBGEntry) {
    if (!blip) return;

    float dx = s_playerX;
    float dy = s_playerY;
    float unitX = 0.0f, unitY = 0.0f, unitZ = 0.0f;

    const auto* cb = callbacks ? callbacks : s_blipCallbacks;

    auto clearAndReturn = [&]() {
        blip->outOfRange = 0;
        blip->inRange    = 0;
    };

    bool isWorldUnit = false;
    uint8_t creatureType = 0;
    if (cb && cb->resolveWorldUnit) {
        isWorldUnit = cb->resolveWorldUnit(guid, unitX, unitY, unitZ,
                                           creatureType);
    }

    if (isWorldUnit) {
        if (cb && cb->isCharmedByBattlefieldVehicle
            && cb->isCharmedByBattlefieldVehicle(guid))
        {
            clearAndReturn();
            return;
        }

        dx -= unitX;
        dy -= unitY;
        blip->creatureType = creatureType;
    } else {

        if (cb && guid == cb->activePlayerGuid) {
            clearAndReturn();
            return;
        }

        const PartyMemberMiniState* member = nullptr;
        if (isBGEntry) {

            if (cb && cb->getRaidMemberState) {
                member = cb->getRaidMemberState(guid);
            }
        } else {
            if (slotIndex >= kPartyMaxSlots) {

            } else if (cb && cb->getPartyMemberState) {
                member = cb->getPartyMemberState(slotIndex);
            }
        }

        if (!member) {
            clearAndReturn();
            return;
        }

        if (cb && cb->getAreaMapId) {
            int32_t memberMapId = cb->getAreaMapId(member->areaId);
            if (memberMapId != cb->currentMapId) {
                clearAndReturn();
                return;
            }
        }

        if (!(member->flags & 1u) || (member->flags & 0x20u)) {
            clearAndReturn();
            return;
        }

        unitX = static_cast<float>(member->posX);
        unitY = static_cast<float>(member->posY);
        dx -= unitX;
        dy -= unitY;

        if (cb && cb->isCharmedByBattlefieldVehicle
            && cb->isCharmedByBattlefieldVehicle(guid))
        {
            clearAndReturn();
            return;
        }
    }

    blip->guid = guid;

    if (cb && cb->lookupName) {
        uint32_t nameCreatureType = 0;
        if (cb->lookupName(guid, blip->name, sizeof(blip->name),
                           &nameCreatureType))
        {
            blip->creatureType = nameCreatureType;
        } else {
            blip->name[0] = '\0';
        }
    }

    float radius;
    if (s_isIndoor) {
        radius = kIndoorZoomRadii[std::min<uint32_t>(s_indoorZoom, 5u)];
    } else {
        radius = static_cast<float>(
                     kOutdoorZoomSizes[std::min<uint32_t>(s_outdoorZoom, 5u)])
                 * 0.5f * 33.333332f;
    }

    float dist = std::sqrt(dy * dy + dx * dx);
    if (dist / radius <= 0.80000001f) {

        blip->outOfRange = 0;
        blip->worldX     = unitX;
        blip->inRange    = 1;
        blip->worldY     = unitY;
    } else {

        blip->inRange    = 0;
        blip->outOfRange = 1;
        blip->facing     = openwow::math::ComputeRetailPlanarFacingAngle(
            openwow::math::PlanarPoint{s_playerX, s_playerY},
            openwow::math::PlanarPoint{unitX, unitY});
    }
}

void Minimap_LoadPartyMemberBlips(void* minimapData) {

    if (!minimapData) return;

    auto* blips = static_cast<MinimapUnitBlip*>(minimapData);
    for (uint32_t i = 0; i < kPartyMaxSlots; ++i) {
        const uint64_t guid =
            (i < s_partyMemberCount) ? s_partyMemberGuids[i] : 0u;
        if (guid != 0) {
            Minimap_UpdateUnitBlip(guid, &blips[i], s_blipCallbacks, i, false);
        } else {
            blips[i].outOfRange = 0;
            blips[i].inRange    = 0;
        }
    }
}

void Minimap_ValidateBGBlips(void* minimapData) {

    if (!minimapData) return;

    auto* blips = static_cast<MinimapUnitBlip*>(minimapData);
    for (uint32_t i = 0; i < kBGMaxSlots; ++i) {
        if (i < s_bgRosterCount
            && s_bgRosterEntries[i].guid != 0
            && s_selfBGMemberValid
            && s_bgRosterEntries[i].memberId != s_selfBGMemberId)
        {
            Minimap_UpdateUnitBlip(s_bgRosterEntries[i].guid,
                                   &blips[i], s_blipCallbacks, i, true);
        } else {
            blips[i].outOfRange = 0;
            blips[i].inRange    = 0;
        }
    }
}

void Minimap_SetCorpseMarker(float x, float y) {

    const std::string label =
        openwow::game::Localization::Get().GetString("CORPSE_RED", "Corpse");

    MinimapAreaPOI& lm = s_specialLandmarks[kCorpseSpecialLandmarkIndex];
    lm.worldX    = x;
    lm.worldY    = y;
    lm.flags     = 2;
    lm.priority  = 0.0f;

    lm.worldStateId = 0;

    openwow::core::SStrCopy(
        s_specialLandmarkNames[kCorpseSpecialLandmarkIndex].data(),
        label.c_str(),
        s_specialLandmarkNames[kCorpseSpecialLandmarkIndex].size());
    lm.name = s_specialLandmarkNames[kCorpseSpecialLandmarkIndex].data();

    Minimap_UpdateNearestPOIDirections();
    s_poiNeedsRebuild = true;
}

void Minimap_SetSpiritHealerMarker(float x, float y) {

    const std::string label =
        openwow::game::Localization::Get().GetString(
            "SPIRIT_HEALER_RELEASE_RED", "Spirit Healer");

    MinimapAreaPOI& lm = s_specialLandmarks[kSpiritHealerSpecialLandmarkIndex];
    lm.worldX    = x;
    lm.worldY    = y;
    lm.flags     = 2;
    lm.priority  = 0.0f;
    lm.worldStateId = 0;

    openwow::core::SStrCopy(
        s_specialLandmarkNames[kSpiritHealerSpecialLandmarkIndex].data(),
        label.c_str(),
        s_specialLandmarkNames[kSpiritHealerSpecialLandmarkIndex].size());
    lm.name = s_specialLandmarkNames[kSpiritHealerSpecialLandmarkIndex].data();

    Minimap_UpdateNearestPOIDirections();
    s_poiNeedsRebuild = true;
}

int Minimap_UpdatePOIArrows(void* arrowArray) {

    (void)arrowArray;
    return 0;
}

bool Minimap_UpdateTerrainTiles(void* worldObj, int mapId,
                                 float* playerPos, void* cameraData,
                                 void* tileGrid, void* outputState,
                                 int forceUpdate) {
    bool dirty = (s_dirtyFlags & 3) != 0;
    bool zoomChanged = (s_dirtyFlags & 1) != 0;

    if (s_waypointTimeout != 0 ) {
        s_waypointTimeout = 0;
        s_waypointX = 0.0f;
        s_waypointY = 0.0f;
        dirty = true;
    }

    if (!playerPos) return dirty;

    bool posChanged = (s_playerX != playerPos[0] ||
                       s_playerY != playerPos[1] ||
                       mapId != static_cast<int>(s_currentMapIdX));

    if (posChanged) {
        dirty = true;
        s_playerX = playerPos[0];
        s_playerY = playerPos[1];
        s_playerZ = playerPos[2];

    }

    if (!dirty && !forceUpdate) return false;

    (void)zoomChanged;
    (void)worldObj;
    (void)cameraData;
    (void)tileGrid;
    (void)outputState;

    return dirty;
}

int Minimap_InitRenderTarget(
    const openwow::ui::MinimapSystem& minimap_system, int mapId) {
    s_initRenderTargetCalled = true;
    s_poiMapId = mapId;

    s_ownedAreaPOIs.clear();
    if (s_areaPOIProvider) {
        s_ownedAreaPOIs = s_areaPOIProvider(mapId);
    }

    s_poiRecords.clear();
    s_poiRecords.reserve(s_ownedAreaPOIs.size() + kSpecialLandmarkCount);
    for (auto& poi : s_ownedAreaPOIs) {
        s_poiRecords.push_back(&poi);
    }

    for (uint32_t i = 0; i < kSpecialLandmarkCount; ++i) {
        s_specialLandmarks[i] = {};
        s_specialLandmarkNames[i] = {};
        s_specialLandmarks[i].name = s_specialLandmarkNames[i].data();
        s_poiRecords.push_back(&s_specialLandmarks[i]);
    }

    const auto n = s_poiRecords.size();
    s_poiActive.assign(n, 0);
    s_poiOutOfRange.assign(n, 0);

    s_nearestPOIAngles = {-1.0f, -1.0f, -1.0f};
    s_nearestPOIIndices = {~0u, ~0u, ~0u};
    s_nearestPOICount = 0;

    SyncStoredZoomStateFromUi(minimap_system);

    if (s_eventFireCallback) {
        s_eventFireCallback("MINIMAP_UPDATE_ZOOM");
    } else {
        auto& dispatch = openwow::ui::game::ScriptEventDispatch::Get();
        if (dispatch.IsInitialized()) {
            dispatch.FireEvent(openwow::ui::game::events::MINIMAP_UPDATE_ZOOM);
        }
    }

    (void)Minimap_LoadTerrainTextureTranslations();

    return 1;
}

static TSGrowableArray<POIDirectionEntry> s_poiDirectionEntries;

static_assert(sizeof(POIDirectionEntry) == 76,
              "POIDIRECTIONDATA element size must be 76 bytes "
              "(matches 0x4C stride in binary 0x7f3d70)");
static_assert(std::is_trivially_copyable_v<POIDirectionEntry>,
              "POIDirectionEntry must be trivially copyable for "
              "SetCountUninitialized / memcpy-based resize");

void POIDIRECTIONDATA_SetCapacity(uint32_t newCapacity) {
    s_poiDirectionEntries.SetCapacity(newCapacity);
}

void POIDIRECTIONDATA_SetCountUninitialized(uint32_t newCount) {
    s_poiDirectionEntries.SetCountUninitialized(newCount);
}

uint32_t POIDIRECTIONDATA_GetCapacityForTests() {
    return s_poiDirectionEntries.GetCapacity();
}

uint32_t POIDIRECTIONDATA_GetCountForTests() {
    return s_poiDirectionEntries.GetCount();
}

const POIDirectionEntry* POIDIRECTIONDATA_GetEntryForTests(uint32_t index) {
    if (index >= s_poiDirectionEntries.GetCount()) {
        return nullptr;
    }
    return &s_poiDirectionEntries[index];
}

void POIDIRECTIONDATA_ResetForTests() {
    s_poiDirectionEntries.Clear();
    s_poiDirectionEntries.SetCapacity(0);
}

void* Minimap_FindExistingTerrainTile(void* tileArray, const int* tileKey,
                                       int currentMap, float playerDist) {
    if (!tileArray || !tileKey) return nullptr;

    auto& cache_state = GetMinimapTerrainTileCacheState(tileArray);
    for (std::uint32_t slot = 0; slot < kMinimapTerrainTileCount; ++slot) {
        auto* tile_slot = GetMinimapTerrainTileSlot(tileArray, slot);
        if (!MinimapTerrainTileIsResident(tile_slot)) {
            continue;
        }

        const auto key = ReadMinimapTerrainTileKey(tile_slot);
        if (key.map_id != tileKey[0] ||
            key.grid_y != tileKey[1] ||
            key.grid_x != tileKey[2]) {
            continue;
        }

        SetMinimapTerrainTileResident(tile_slot);
        UpdateMinimapTerrainTileSortDistance(tile_slot, currentMap, playerDist);
        TouchMinimapTerrainTileCacheEntry(
            cache_state, static_cast<std::uint16_t>(slot), key);
        return tile_slot;
    }

    return nullptr;
}

uint32_t Minimap_LoadSingleTerrainTile(const int* tileData, void* tileArray,
                                        int currentMap, float playerDist,
                                        const char* mapBaseName) {
    if (!tileArray || !tileData) return 0;

    auto& cache_state = GetMinimapTerrainTileCacheState(tileArray);

    std::uint32_t slot = 0;
    std::uint8_t* tile_slot = nullptr;
    while (slot < kMinimapTerrainTileCount) {
        tile_slot = GetMinimapTerrainTileSlot(tileArray, slot);
        if (!MinimapTerrainTileIsResident(tile_slot)) {
            break;
        }
        ++slot;
    }

    if (slot >= kMinimapTerrainTileCount) {
        if (cache_state.active_count == 0) {
            slot = 0;
        } else {
            slot = cache_state.mru_slots[cache_state.active_count - 1u];
            RemoveMinimapTerrainTileFromCacheOrder(
                cache_state, static_cast<std::uint16_t>(slot));
        }
        tile_slot = GetMinimapTerrainTileSlot(tileArray, slot);
    }

    auto flags = ReadMinimapTerrainTileField<std::uint32_t>(
        tile_slot, kMinimapTerrainTileFlagsOffset);
    flags |= kMinimapTerrainTileResidentFlag;
    WriteMinimapTerrainTileField(tile_slot, kMinimapTerrainTileFlagsOffset, flags);

    cache_state.textures[slot].reset();

    char texture_path[kMinimapTerrainPathBufferChars]{};
    const bool have_texture_path = TryResolveMinimapTerrainTexturePath(
        tileData, mapBaseName, texture_path, sizeof(texture_path));
    if (have_texture_path) {
        cache_state.textures[slot] =
            openwow::render::TextureAsset::File(texture_path);
        flags |= kMinimapTerrainTileLoadingFlag;
        WriteMinimapTerrainTileField(
            tile_slot, kMinimapTerrainTileTextureHandleOffset,
            cache_state.textures[slot].get());
    } else {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "No minimap texture translation for requested tile");
        flags &= ~kMinimapTerrainTileResidentFlag;
        flags &= ~kMinimapTerrainTileLoadingFlag;
        WriteMinimapTerrainTileField<void*>(
            tile_slot, kMinimapTerrainTileTextureHandleOffset, nullptr);
    }
    WriteMinimapTerrainTileField(tile_slot, kMinimapTerrainTileFlagsOffset, flags);

    WriteMinimapTerrainTileField(
        tile_slot, kMinimapTerrainTileGridYOffset,
        static_cast<std::int32_t>(tileData[1]));
    WriteMinimapTerrainTileField(
        tile_slot, kMinimapTerrainTileGridXOffset,
        static_cast<std::int32_t>(tileData[2]));
    WriteMinimapTerrainTileField(
        tile_slot, kMinimapTerrainTileMapIdOffset,
        static_cast<std::int32_t>(tileData[0]));
    std::memcpy(tile_slot + kMinimapTerrainTileBoundsOffset, tileData + 3,
                sizeof(float) * 6u);

    UpdateMinimapTerrainTileSortDistance(tile_slot, currentMap, playerDist);
    if ((flags & kMinimapTerrainTileResidentFlag) != 0u) {
        TouchMinimapTerrainTileCacheEntry(
            cache_state, static_cast<std::uint16_t>(slot),
            ReadMinimapTerrainTileKey(tile_slot));
    }

    return slot;
}

MinimapChunkCoords Minimap_WorldToChunkCoords(const float world_x,
                                              const float world_y) noexcept {
    return {
        .row = static_cast<std::int32_t>(
            std::floor((kMinimapChunkGridMaxCoord - world_y) /
                       kMinimapChunkTileSize)),
        .column = static_cast<std::int32_t>(
            std::floor((kMinimapChunkGridMaxCoord - world_x) /
                       kMinimapChunkTileSize)),
    };
}

MinimapChunkWorldBounds Minimap_ChunkCoordsToWorldBounds(
    const std::int32_t row, const std::int32_t column) noexcept {
    const float max_y =
        kMinimapChunkGridMaxCoord - static_cast<float>(row) * kMinimapChunkTileSize;
    const float min_y = max_y - kMinimapChunkTileSize;
    const float max_x = kMinimapChunkGridMaxCoord -
                        static_cast<float>(column) * kMinimapChunkTileSize;
    const float min_x = max_x - kMinimapChunkTileSize;
    return {min_x, max_x, min_y, max_y};
}

MinimapChunkCoords Minimap_ComputeChunkWindowOrigin(const float world_x,
                                                    const float world_y) noexcept {
    auto first = Minimap_WorldToChunkCoords(world_x + kMinimapChunkHalfTileSpan,
                                            world_y + kMinimapChunkHalfTileSpan);
    auto second = Minimap_WorldToChunkCoords(world_x - kMinimapChunkHalfTileSpan,
                                             world_y - kMinimapChunkHalfTileSpan);

    if (first.row == second.row) {
        if (second.row + 1 >= kMinimapChunkGridDimension) {
            --first.row;
        } else {
            ++second.row;
        }
    }

    if (first.column == second.column) {
        if (second.column + 1 >= kMinimapChunkGridDimension) {
            --first.column;
        } else {
            ++second.column;
        }
    }

    return first;
}

bool Minimap_ResolveChunkTexturePath(const char* continent_name,
                                     const std::int32_t row,
                                     const std::int32_t column,
                                     char* out_path,
                                     const std::size_t out_path_chars) {
    return TryResolveMinimapChunkTexturePath(continent_name, row, column,
                                             out_path, out_path_chars);
}

void Minimap_UpdateChunkTextureWindow(
    const MinimapChunkCoords& origin, const bool continent_changed,
    const char* continent_name,
    std::array<MinimapChunkWindowSlot, 4>& slots) {
    std::array<bool, 4> matched{};
    matched.fill(false);

    if (!continent_changed) {
        for (std::size_t desired_index = 0; desired_index < slots.size();
             ++desired_index) {
            const std::int32_t desired_row =
                origin.row + kMinimapChunkRowOffsets[desired_index];
            const std::int32_t desired_column =
                origin.column + kMinimapChunkColumnOffsets[desired_index];

            for (std::size_t source_index = 0; source_index < slots.size();
                 ++source_index) {
                if (slots[source_index].row != desired_row ||
                    slots[source_index].column != desired_column ||
                    slots[source_index].texture_path.empty()) {
                    continue;
                }

                if (source_index != desired_index) {
                    slots[desired_index].texture_path =
                        std::move(slots[source_index].texture_path);
                    slots[desired_index].row = desired_row;
                    slots[desired_index].column = desired_column;
                    slots[source_index].row = -1;
                    slots[source_index].column = -1;
                }

                matched[desired_index] = true;
                break;
            }
        }
    }

    for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
        if (matched[slot_index]) {
            continue;
        }

        char texture_path[kMinimapTerrainPathBufferChars]{};
        const std::int32_t desired_row =
            origin.row + kMinimapChunkRowOffsets[slot_index];
        const std::int32_t desired_column =
            origin.column + kMinimapChunkColumnOffsets[slot_index];
        if (!TryResolveMinimapChunkTexturePath(continent_name, desired_row,
                                               desired_column, texture_path,
                                               sizeof(texture_path))) {
            slots[slot_index].texture_path.clear();
            continue;
        }

        slots[slot_index].texture_path = texture_path;
        slots[slot_index].row = desired_row;
        slots[slot_index].column = desired_column;
    }
}

void Minimap_ResetTerrainTextureTranslations() {
    GetMinimapTerrainTranslationTable().entries.clear();
    s_minimapTerrainTranslationsLoaded = false;
}

bool Minimap_LoadTerrainTextureTranslations() {
    if (s_minimapTerrainTranslationsLoaded &&
        !GetMinimapTerrainTranslationTable().entries.empty()) {
        return true;
    }

    void* loaded_data = nullptr;
    std::size_t loaded_size = 0;
    if (!openwow::vfs::SFileReadFileToBuffer_Wrapper(
            kMinimapMd5TranslateFilename.data(), &loaded_data, &loaded_size,
            1u, 0)) {
        return false;
    }

    const std::string_view loaded_text(
        static_cast<const char*>(loaded_data), loaded_size);
    LoadMinimapTerrainTranslationsFromText(loaded_text);
    (void)openwow::vfs::SFileFreeLoadedData(loaded_data);
    s_minimapTerrainTranslationsLoaded = true;
    return true;
}

bool Minimap_LoadTerrainTextureTranslationsFromText(
    const std::string_view text) {
    if (text.empty()) {
        return false;
    }

    auto& table = GetMinimapTerrainTranslationTable();
    table.entries.clear();
    LoadMinimapTerrainTranslationsFromText(text);
    s_minimapTerrainTranslationsLoaded = !table.entries.empty();
    return s_minimapTerrainTranslationsLoaded;
}

bool Minimap_ResolveTerrainTexturePath(const int* tileData,
                                       const char* mapBaseName,
                                       char* outPath,
                                       const std::size_t outPathChars) {
    return TryResolveMinimapTerrainTexturePath(
        tileData, mapBaseName, outPath, outPathChars);
}

bool Minimap_LoadTerrainTextureTranslationsFromTextForTests(
    const std::string_view text) {
    return Minimap_LoadTerrainTextureTranslationsFromText(text);
}

void Minimap_SetUnitBlipRuntimeForTesting(const float player_x,
                                          const float player_y,
                                          const bool is_indoor,
                                          const std::uint32_t zoom_level) {
    s_playerX = player_x;
    s_playerY = player_y;
    s_playerZ = 0.0f;
    s_isIndoor = is_indoor;
    if (is_indoor) {
        s_indoorZoom = std::min<std::uint32_t>(zoom_level, 5u);
    } else {
        s_outdoorZoom = std::min<std::uint32_t>(zoom_level, 5u);
    }
}

void Minimap_SetBGRosterForTesting(const BGRosterEntry* entries,
                                   const uint32_t count,
                                   const uint32_t selfMemberId) {
    s_bgRosterCount = std::min(count, kBGMaxSlots);
    s_bgRosterEntries = {};
    for (uint32_t i = 0; i < s_bgRosterCount; ++i) {
        s_bgRosterEntries[i] = entries[i];
    }
    s_selfBGMemberId = selfMemberId;
    s_selfBGMemberValid = true;
}

void Minimap_SetPartyMembersForTesting(const uint64_t* guids,
                                       const uint32_t count) {
    s_partyMemberCount = std::min(count, kPartyMaxSlots);
    s_partyMemberGuids = {};
    for (uint32_t i = 0; i < s_partyMemberCount; ++i) {
        s_partyMemberGuids[i] = guids[i];
    }
}

void Minimap_SetBlipCallbacksForTesting(const MinimapBlipCallbacks* cb) {
    s_blipCallbacks = cb;
}

void* MINIMAPMD5NAME_Create(void* , int extraSize, uint8_t ) {

    auto* block = new char[static_cast<std::size_t>(extraSize + 64)]();
    return block;
}

void Minimap_BuildSortedRttTileSlotList(void* tile_array,
                                        std::uint8_t** out_sorted_head) {
    for (std::uint32_t slot = 0; slot < kMinimapTerrainTileCount; ++slot) {
        auto* tile = GetMinimapTerrainTileSlot(tile_array, slot);

        const auto flags = ReadMinimapTerrainTileField<std::uint32_t>(
            tile, kMinimapTerrainTileFlagsOffset);
        const auto texture = ReadMinimapTerrainTileField<std::uintptr_t>(
            tile, kMinimapTerrainTileTextureHandleOffset);

        if ((flags & kMinimapTerrainTileResidentFlag) == 0 || texture == 0)
            continue;

        const float tile_dist = ReadMinimapTerrainTileField<float>(
            tile, kMinimapTerrainTileSortDistanceOffset);

        std::uint8_t* prev = nullptr;
        std::uint8_t* cur = *out_sorted_head;
        while (cur) {
            const float cur_dist = ReadMinimapTerrainTileField<float>(
                cur, kMinimapTerrainTileSortDistanceOffset);
            if (cur_dist >= tile_dist)
                break;
            prev = cur;
            cur = ReadMinimapTerrainTileField<std::uint8_t*>(
                cur, kMinimapTerrainTileRenderNextOffset);
        }

        WriteMinimapTerrainTileField(
            tile, kMinimapTerrainTileRenderNextOffset, cur);
        if (prev) {
            WriteMinimapTerrainTileField(
                prev, kMinimapTerrainTileRenderNextOffset, tile);
        } else {
            *out_sorted_head = tile;
        }
    }
}

}

#include "openwow/game/world_state/maps/adapters/lua/map_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaGetMinimapZoneText(lua_State* L);
int LuaGetRealZoneText(lua_State* L);
int LuaGetSubZoneText(lua_State* L);
int LuaGetZoneText(lua_State* L);
int LuaGetZonePVPInfo(lua_State* L);
int LuaGetCurrentMapAreaID(lua_State* L);
int LuaGetCurrentMapContinent(lua_State* L);
int LuaGetCurrentMapZone(lua_State* L);
int LuaSetMapToCurrentZone(lua_State* L);
int LuaSetMapByID(lua_State* L);
int LuaGetMapZones(lua_State* L);
int LuaGetMapContinents(lua_State* L);
int LuaGetNumMapOverlays(lua_State* L);
int LuaGetMapOverlayInfo(lua_State* L);
int LuaIsIndoors(lua_State* L);
int LuaGetBindLocation(lua_State* L);
int LuaCreateWorldMapArrowFrame(lua_State* L);
int LuaCreateMiniWorldMapArrowFrame(lua_State* L);
int LuaInitWorldMapPing(lua_State* L);
int LuaPositionWorldMapArrowFrame(lua_State* L);
int LuaPositionMiniWorldMapArrowFrame(lua_State* L);
int LuaGetMapInfo(lua_State* L);
int LuaGetPlayerMapPosition(lua_State* L);
int LuaGetMapLandmarkInfo(lua_State* L);
int LuaGetNumMapLandmarks(lua_State* L);
int LuaProcessMapClick(lua_State* L);
int LuaApi_CanHearthAndResurrectFromArea(lua_State* L);
int LuaApi_CannotBeResurrected(lua_State* L);
int LuaApi_ClickLandmark(lua_State* L);
int LuaApi_HearthAndResurrectFromArea(lua_State* L);
int LuaApi_IsFlyableArea(lua_State* L);
int LuaApi_IsSubZonePVPPOI(lua_State* L);
int LuaApi_QuestPOIUpdateTexture(lua_State* L);
int LuaApi_QuestPOIUpdateIcons(lua_State* L);
int LuaSetMapZoom(lua_State* L);
int LuaApi_SetPOIIconOverlapDistance(lua_State* L);
int LuaApi_SetPOIIconOverlapPushDistance(lua_State* L);
int LuaIsOutdoors(lua_State* L);
int LuaGetCurrentMapDungeonLevel(lua_State* L);
int LuaDungeonUsesTerrainMap(lua_State* L);
int LuaGetCorpseMapPosition(lua_State* L);
int LuaGetDeathReleasePosition(lua_State* L);
int LuaGetNumMapDebugObjects(lua_State* L);
int LuaGetMapDebugObjectInfo(lua_State* L);
int LuaTeleportToDebugObject(lua_State* L);
int LuaHasDebugZoneMap(lua_State* L);
int LuaGetDebugZoneMap(lua_State* L);
int LuaGetNumDungeonMapLevels(lua_State* L);
int LuaSetDungeonMapLevel(lua_State* L);
int LuaUpdateMapHighlight(lua_State* L);
int LuaShowWorldMapArrowFrame(lua_State* L);
int LuaShowMiniWorldMapArrowFrame(lua_State* L);
int LuaUpdateWorldMapArrowFrames(lua_State* L);
int LuaGetWintergraspWaitTime(lua_State* L);
int LuaCanQueueForWintergrasp(lua_State* L);
int LuaIsZoomOutAvailable(lua_State* L);
int LuaZoomOut(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kMapLuaBindings[] = {
    {"GetMinimapZoneText", LuaGetMinimapZoneText},
    {"GetRealZoneText", LuaGetRealZoneText},
    {"GetSubZoneText", LuaGetSubZoneText},
    {"GetZoneText", LuaGetZoneText},
    {"GetZonePVPInfo", LuaGetZonePVPInfo},
    {"GetCurrentMapAreaID", LuaGetCurrentMapAreaID},
    {"GetCurrentMapContinent", LuaGetCurrentMapContinent},
    {"GetCurrentMapZone", LuaGetCurrentMapZone},
    {"SetMapToCurrentZone", LuaSetMapToCurrentZone},
    {"SetMapByID", LuaSetMapByID},
    {"GetMapZones", LuaGetMapZones},
    {"GetMapContinents", LuaGetMapContinents},
    {"GetNumMapOverlays", LuaGetNumMapOverlays},
    {"GetMapOverlayInfo", LuaGetMapOverlayInfo},
    {"IsIndoors", LuaIsIndoors},
    {"GetBindLocation", LuaGetBindLocation},
    {"CreateWorldMapArrowFrame", LuaCreateWorldMapArrowFrame},
    {"CreateMiniWorldMapArrowFrame", LuaCreateMiniWorldMapArrowFrame},
    {"InitWorldMapPing", LuaInitWorldMapPing},
    {"PositionWorldMapArrowFrame", LuaPositionWorldMapArrowFrame},
    {"PositionMiniWorldMapArrowFrame", LuaPositionMiniWorldMapArrowFrame},
    {"GetMapInfo", LuaGetMapInfo},
    {"GetPlayerMapPosition", LuaGetPlayerMapPosition},
    {"GetMapLandmarkInfo", LuaGetMapLandmarkInfo},
    {"GetNumMapLandmarks", LuaGetNumMapLandmarks},
    {"ProcessMapClick", LuaProcessMapClick},
    {"CanHearthAndResurrectFromArea", LuaApi_CanHearthAndResurrectFromArea},
    {"CannotBeResurrected", LuaApi_CannotBeResurrected},
    {"ClickLandmark", LuaApi_ClickLandmark},
    {"HearthAndResurrectFromArea", LuaApi_HearthAndResurrectFromArea},
    {"IsFlyableArea", LuaApi_IsFlyableArea},
    {"IsSubZonePVPPOI", LuaApi_IsSubZonePVPPOI},
    {"QuestPOIUpdateTexture", LuaApi_QuestPOIUpdateTexture},
    {"QuestPOIUpdateIcons", LuaApi_QuestPOIUpdateIcons},
    {"SetMapZoom", LuaSetMapZoom},
    {"SetPOIIconOverlapDistance", LuaApi_SetPOIIconOverlapDistance},
    {"SetPOIIconOverlapPushDistance", LuaApi_SetPOIIconOverlapPushDistance},
    {"IsOutdoors", LuaIsOutdoors},
    {"GetCurrentMapDungeonLevel", LuaGetCurrentMapDungeonLevel},
    {"DungeonUsesTerrainMap", LuaDungeonUsesTerrainMap},
    {"GetCorpseMapPosition", LuaGetCorpseMapPosition},
    {"GetDeathReleasePosition", LuaGetDeathReleasePosition},
    {"GetNumMapDebugObjects", LuaGetNumMapDebugObjects},
    {"GetMapDebugObjectInfo", LuaGetMapDebugObjectInfo},
    {"TeleportToDebugObject", LuaTeleportToDebugObject},
    {"HasDebugZoneMap", LuaHasDebugZoneMap},
    {"GetDebugZoneMap", LuaGetDebugZoneMap},
    {"GetNumDungeonMapLevels", LuaGetNumDungeonMapLevels},
    {"SetDungeonMapLevel", LuaSetDungeonMapLevel},
    {"UpdateMapHighlight", LuaUpdateMapHighlight},
    {"ShowWorldMapArrowFrame", LuaShowWorldMapArrowFrame},
    {"ShowMiniWorldMapArrowFrame", LuaShowMiniWorldMapArrowFrame},
    {"UpdateWorldMapArrowFrames", LuaUpdateWorldMapArrowFrames},
    {"GetWintergraspWaitTime", LuaGetWintergraspWaitTime},
    {"CanQueueForWintergrasp", LuaCanQueueForWintergrasp},
    {"IsZoomOutAvailable", LuaIsZoomOutAvailable},
    {"ZoomOut", LuaZoomOut},
};

}

openwow::ui::lua::NativeBindingCatalog MapNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.world_state.maps", openwow::ui::lua::BindingScope::kWorld, kMapLuaBindings);
}

}


#pragma once

struct lua_State;

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

int LuaDungeonUsesTerrainMap(lua_State* L);
int LuaGetNumMapDebugObjects(lua_State* L);
int LuaGetMapDebugObjectInfo(lua_State* L);
int LuaTeleportToDebugObject(lua_State* L);
int LuaHasDebugZoneMap(lua_State* L);
int LuaGetDebugZoneMap(lua_State* L);
int LuaGetNumDungeonMapLevels(lua_State* L);
int LuaSetDungeonMapLevel(lua_State* L);
int LuaUpdateMapHighlight(lua_State* L);
int LuaIsZoomOutAvailable(lua_State* L);
int LuaZoomOut(lua_State* L);

int LuaCreateWorldMapArrowFrame(lua_State* L);
int LuaCreateMiniWorldMapArrowFrame(lua_State* L);
int LuaInitWorldMapPing(lua_State* L);
int LuaPositionWorldMapArrowFrame(lua_State* L);
int LuaPositionMiniWorldMapArrowFrame(lua_State* L);
int LuaShowWorldMapArrowFrame(lua_State* L);
int LuaShowMiniWorldMapArrowFrame(lua_State* L);
int LuaUpdateWorldMapArrowFrames(lua_State* L);
void DestroyWorldMapArrowFrames(lua_State* L);

int LuaGetCurrentMapDungeonLevel(lua_State* L);
int LuaApi_CanHearthAndResurrectFromArea(lua_State* L);
int LuaApi_CannotBeResurrected(lua_State* L);
int LuaApi_ClickLandmark(lua_State* L);
int LuaApi_HearthAndResurrectFromArea(lua_State* L);
int LuaApi_IsFlyableArea(lua_State* L);
int LuaApi_IsSubZonePVPPOI(lua_State* L);
int LuaApi_QuestPOIUpdateTexture(lua_State* L);
int LuaApi_QuestPOIUpdateIcons(lua_State* L);
int LuaApi_SetPOIIconOverlapDistance(lua_State* L);
int LuaApi_SetPOIIconOverlapPushDistance(lua_State* L);

}

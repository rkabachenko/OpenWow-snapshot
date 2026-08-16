#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_map.h"
#include "openwow/game/active_player_environment.h"
#include "openwow/game/contested_area.h"
#include "openwow/game/flyable_area.h"
#include "openwow/game/localization.h"
#include "openwow/game/minimap_terrain.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/quest_poi.h"
#include "openwow/game/update_fields.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/ui/game/world_map_system.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/net/client_services.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/world/camera/world_camera.h"

#include <array>

namespace openwow::ui::game::detail {

static constexpr const char* kGameUiMgrKeyMap = "openwow.world_ui_runtime_context";

static runtime::WorldUiRuntimeContext* GetMapUiManager(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kGameUiMgrKeyMap);
  auto* mgr = static_cast<runtime::WorldUiRuntimeContext*>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return mgr;
}

constexpr std::uint32_t kAreaFlagPvPPoi = 0x00008000u;

const openwow::game::WorldSession *ResolveAreaQuerySession(lua_State *const L) {
  return GetWorldSession(L);
}

const openwow::data::dbc::AreaTableEntry *
LookupCurrentSubZoneAreaEntry(const openwow::game::WorldSession &session) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto sub_zone_id = session.world_states().area_id();
  if (sub_zone_id <= 0) {
    return nullptr;
  }

  return dbc->area_table().LookupEntry(static_cast<std::uint32_t>(sub_zone_id));
}

lua_Number ReadRequiredPoiOverlapNumber(lua_State *L, const char *usage) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "%s", usage);
    return 0.0;
  }
  return lua_tonumber(L, 1);
}

static std::string ResolveBindLocationText(lua_State* L) {
  const auto* session = GetWorldSession(L);
  const auto* dbc = GetDbcLoader(L);
  if (session != nullptr && dbc != nullptr) {
    const auto area_id = session->misc().bind_point().area_id;
    if (const auto* area = dbc->area_table().LookupEntry(area_id); area != nullptr) {
      return std::string(area->name);
    }
  }

  return ::openwow::game::Localization::Get().GetString("HOME_INN");
}

static const char* GetLuaTableStringField(lua_State* L, int index,
                                          const char* field) {
  index = lua_absindex(L, index);
  lua_getfield(L, index, field);
  const char* value = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
  lua_pop(L, 1);
  return value;
}

static float GetLuaFrameNumericField(lua_State* L, int index,
                                     const char* field) {
  index = lua_absindex(L, index);
  lua_getfield(L, index, field);
  const float value = lua_isnumber(L, -1)
                          ? static_cast<float>(lua_tonumber(L, -1))
                          : 0.0f;
  lua_pop(L, 1);
  return value;
}

static bool TryPushNamedChildFrame(lua_State* L, int parent_index,
                                   const char* child_name) {
  parent_index = lua_absindex(L, parent_index);
  lua_getglobal(L, child_name);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return false;
  }

  lua_getfield(L, -1, "__ow_parent");
  bool same_parent = lua_istable(L, -1) != 0 &&
                     lua_rawequal(L, parent_index, -1) != 0;
  if (!same_parent && lua_istable(L, -1) != 0) {
    const char* expected_parent_name =
        GetLuaTableStringField(L, parent_index, "__ow_name");
    const char* actual_parent_name =
        GetLuaTableStringField(L, -1, "__ow_name");
    same_parent = expected_parent_name != nullptr &&
                  actual_parent_name != nullptr &&
                  std::string_view(expected_parent_name) ==
                      std::string_view(actual_parent_name);
  }
  lua_pop(L, 1);

  if (same_parent) {
    return true;
  }

  lua_pop(L, 1);
  return false;
}

struct ArrowFramePair {
  const char* primary_name;
  const char* effect_name;
  float model_scale_divisor;
};

constexpr ArrowFramePair kWorldMapArrowFrames{
    "PlayerArrowFrame",
    "PlayerArrowEffectFrame",
    0.6f,
};

constexpr ArrowFramePair kMiniWorldMapArrowFrames{
    "PlayerMiniArrowFrame",
    "PlayerMiniArrowEffectFrame",
    0.9f,
};

constexpr std::array<const char*, 4> kAllArrowFrameNames{
    kWorldMapArrowFrames.primary_name,
    kWorldMapArrowFrames.effect_name,
    kMiniWorldMapArrowFrames.primary_name,
    kMiniWorldMapArrowFrames.effect_name,
};

constexpr const char* kArrowModelPath = "Interface\\Minimap\\MinimapArrow.mdx";

static bool PushNamedGlobalFrame(lua_State* L, const char* frame_name) {
  lua_getglobal(L, frame_name);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return false;
  }
  return true;
}

struct ModelWidgetNativeSize {
  float width{0.0f};
  float height{0.0f};
};

static const openwow::ui::framexml::UiFrame* FindModelWidgetFrameRecord(
    lua_State* L, int frame_index) {
  const auto* const context = runtime::WorldUiRuntimeContext::FromLua(L);
  if (context == nullptr) {
    return nullptr;
  }

  const char* const key = frame_api::GetFrameRuntimeKeyOrName(L, frame_index);
  if (key == nullptr || *key == '\0') {
    return nullptr;
  }
  return context->frame_store().FindFrame(std::string_view(key));
}

static bool ModelWidgetModelIsLoaded(lua_State* L, int frame_index) {
  const auto* const frame = FindModelWidgetFrameRecord(L, frame_index);
  return frame != nullptr && !frame->model_natural_size_path.empty();
}

static ModelWidgetNativeSize ResolveModelWidgetNativeSize(lua_State* L,
                                                          int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  ModelWidgetNativeSize size{
      .width = openwow::ui::PixelUiHorizontalCoordinateToStored(
          GetLuaFrameNumericField(L, frame_index, "__ow_width")),
      .height = openwow::ui::PixelUiHorizontalCoordinateToStored(
          GetLuaFrameNumericField(L, frame_index, "__ow_height")),
  };
  const auto* const frame = FindModelWidgetFrameRecord(L, frame_index);
  if (frame == nullptr || frame->model_natural_size_path.empty()) {
    return size;
  }
  if (size.width == 0.0f && frame->model_natural_width.has_value()) {
    size.width = *frame->model_natural_width;
  }
  if (size.height == 0.0f && frame->model_natural_height.has_value()) {
    size.height = *frame->model_natural_height;
  }
  return size;
}

static void WriteModelWidgetCenteredPosition(lua_State* L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  const ModelWidgetNativeSize size = ResolveModelWidgetNativeSize(L, frame_index);
  lua_pushnumber(L, static_cast<lua_Number>(size.width * 0.5f));
  lua_setfield(L, frame_index, "__ow_model_x");
  lua_pushnumber(L, static_cast<lua_Number>(size.height * 0.5f));
  lua_setfield(L, frame_index, "__ow_model_y");
  lua_pushnumber(L, 0.0);
  lua_setfield(L, frame_index, "__ow_model_z");
}

static void SyncArrowModelPosition(lua_State* L, int frame_index) {
  if (!ModelWidgetModelIsLoaded(L, frame_index)) {
    return;
  }
  WriteModelWidgetCenteredPosition(L, frame_index);
}

static int CreateFallbackNamedModelFrame(lua_State* L, const char* frame_name,
                                         int parent_index) {
  lua_newtable(L);
  const int frame_index = lua_absindex(L, -1);

  lua_pushstring(L, "Model");
  lua_setfield(L, frame_index, "__ow_type");
  AttachLuaScriptObjectThis(L, frame_index);
  lua_pushstring(L, frame_name);
  lua_setfield(L, frame_index, "__ow_name");
  lua_pushboolean(L, 1);
  lua_setfield(L, frame_index, "__ow_visible");
  if (lua_istable(L, parent_index) != 0) {
    lua_pushvalue(L, parent_index);
    lua_setfield(L, frame_index, "__ow_parent");
  }

  openwow::ui::game::frame_api::RegisterFrameScriptMethods(L);
  openwow::ui::game::frame_api::ApplyRegisteredFrameMethods(L);
  openwow::ui::game::frame_api::ApplyFrameTypeMethods(L, "Model");

  openwow::ui::ReplaceLuaGlobalValue(L, frame_name, frame_index);
  return frame_index;
}

static int CreateManagedNamedModelFrame(lua_State* L, const char* frame_name,
                                        int parent_index) {
  lua_getglobal(L, "CreateFrame");
  if (lua_isfunction(L, -1) == 0) {
    lua_pop(L, 1);
    return CreateFallbackNamedModelFrame(L, frame_name, parent_index);
  }

  lua_pushstring(L, "Model");
  lua_pushstring(L, frame_name);
  lua_pushvalue(L, parent_index);
  if (lua_pcall(L, 3, 1, 0) != 0) {
    lua_error(L);
  }
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return CreateFallbackNamedModelFrame(L, frame_name, parent_index);
  }
  return lua_absindex(L, -1);
}

static void InitializeArrowFrame(lua_State* L, int frame_index,
                                 const char* frame_name,
                                 float model_scale_divisor) {

  const float model_scale =
      openwow::ui::ApplyCachedUiVerticalScale(1.0f) / model_scale_divisor;
  lua_pushstring(L, frame_name);
  lua_setfield(L, frame_index, "__ow_name");
  lua_pushstring(L, kArrowModelPath);
  lua_setfield(L, frame_index, "__ow_model_path");
  lua_pushnumber(L, static_cast<lua_Number>(model_scale));
  lua_setfield(L, frame_index, "__ow_model_scale");
  SyncArrowModelPosition(L, frame_index);

  frame_api::NotifyFrameInputMutation(L, frame_index, false);
}

static void EnsureArrowFrame(lua_State* L, int parent_index,
                             const char* frame_name, float model_scale_divisor) {
  const int top = lua_gettop(L);
  if (PushNamedGlobalFrame(L, frame_name)) {
    lua_settop(L, top);
    return;
  }

  const int frame_index =
      CreateManagedNamedModelFrame(L, frame_name, parent_index);
  InitializeArrowFrame(L, frame_index, frame_name, model_scale_divisor);
  lua_settop(L, top);
}

static void EnsureArrowFramePair(lua_State* L, int parent_index,
                                 const ArrowFramePair& pair) {
  EnsureArrowFrame(L, parent_index, pair.primary_name, pair.model_scale_divisor);
  EnsureArrowFrame(L, parent_index, pair.effect_name, pair.model_scale_divisor);
}

static int RequireArrowParentFrame(lua_State* L, int index,
                                   const char* function_name) {
  if (lua_type(L, index) != LUA_TTABLE) {
    return luaL_error(L, "Usage: %s(parent)", function_name);
  }

  if (!HasLuaScriptObjectThis(L, index)) {
    return luaL_error(L, "%s(): Couldn't find 'this' in parent object",
                      function_name);
  }

  if (!LuaScriptObjectIsKindOfCanonicalType(
          L, index, openwow::ui::widgets::ScriptObjectType::Frame)) {
    return luaL_error(L, "%s(): Wrong object type, expected frame",
                      function_name);
  }
  return lua_absindex(L, index);
}

static bool LoadArrowFramePair(lua_State* L, const ArrowFramePair& pair,
                               int* primary_index, int* effect_index) {
  const int top = lua_gettop(L);
  if (!PushNamedGlobalFrame(L, pair.primary_name)) {
    lua_settop(L, top);
    return false;
  }
  *primary_index = lua_absindex(L, -1);
  if (!PushNamedGlobalFrame(L, pair.effect_name)) {
    lua_settop(L, top);
    return false;
  }
  *effect_index = lua_absindex(L, -1);
  return true;
}

static void StoreSingleAnchor(lua_State* L, int frame_index, const char* point,
                              const char* relative_to,
                              const char* relative_point, float offset_x,
                              float offset_y) {
  lua_pushboolean(L, 0);
  lua_setfield(L, frame_index, "__ow_setAllPoints");

  lua_newtable(L);
  const int anchors_index = lua_absindex(L, -1);
  lua_newtable(L);
  lua_pushstring(L, point);
  lua_setfield(L, -2, "point");
  lua_pushstring(L, relative_point);
  lua_setfield(L, -2, "relativePoint");
  lua_pushstring(L, relative_to);
  lua_setfield(L, -2, "relativeTo");
  lua_pushnumber(L, static_cast<lua_Number>(offset_x));
  lua_setfield(L, -2, "x");
  lua_pushnumber(L, static_cast<lua_Number>(offset_y));
  lua_setfield(L, -2, "y");
  lua_rawseti(L, anchors_index, 1);
  lua_setfield(L, frame_index, "__ow_anchors");
  openwow::ui::game::detail::ReindexLuaAnchorDependents(L, frame_index);

  frame_api::NotifyFrameInputMutation(L, frame_index, false);
}

static int PositionArrowFramePair(lua_State* L, const ArrowFramePair& pair,
                                  const char* usage_name) {
  const int top = lua_gettop(L);
  int primary_index = 0;
  int effect_index = 0;
  if (!LoadArrowFramePair(L, pair, &primary_index, &effect_index)) {
    return 0;
  }

  if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(
        L,
        "Usage: %s(\"point\" \"frame\" [, relativePoint] [, offsetX, offsetY])",
        usage_name);
  }

  const char* point_name = lua_tostring(L, 1);
  int point_id = 0;
  if (!openwow::ui::StringToFramePoint(point_name, &point_id)) {
    return luaL_error(L, "Unknown frame point");
  }
  (void)point_id;

  const char* target_name = lua_tostring(L, 2);
  if (!PushNamedGlobalFrame(L, target_name)) {
    lua_settop(L, top);
    return luaL_error(L, "Couldn't find frame named '%s'", target_name);
  }

  const int target_index = lua_absindex(L, -1);
  if (lua_rawequal(L, target_index, primary_index) != 0) {
    lua_settop(L, top);
    return luaL_error(L, "Error: %s is anchored to itself", target_name);
  }

  const char* relative_point_name = point_name;
  float offset_x = 0.0f;
  float offset_y = 0.0f;

  if (lua_isstring(L, 3) != 0) {
    relative_point_name = lua_tostring(L, 3);
    int relative_point_id = 0;
    if (!openwow::ui::StringToFramePoint(relative_point_name,
                                         &relative_point_id)) {
      lua_settop(L, top);
      return luaL_error(L, "Unknown frame point");
    }
    (void)relative_point_id;
    if (lua_isnumber(L, 4) != 0 && lua_isnumber(L, 5) != 0) {
      offset_x = static_cast<float>(lua_tonumber(L, 4));
      offset_y = static_cast<float>(lua_tonumber(L, 5));
    }
  }

  StoreSingleAnchor(L, primary_index, point_name, target_name,
                    relative_point_name, offset_x, offset_y);
  StoreSingleAnchor(L, effect_index, point_name, target_name,
                    relative_point_name, offset_x, offset_y);
  lua_settop(L, top);
  return 0;
}

static int ShowArrowFramePair(lua_State* L, const ArrowFramePair& pair) {
  const int top = lua_gettop(L);
  int primary_index = 0;
  int effect_index = 0;
  if (!LoadArrowFramePair(L, pair, &primary_index, &effect_index)) {
    return 0;
  }

  const bool visible = ScriptReadBoolArgOrDefault(L, 1, true);
  const auto apply_visibility = [&](const int frame_index) {
    if (!AllowLuaFrameProtectedMutation(L, frame_index)) {
      return;
    }

    lua_pushboolean(L, visible ? 1 : 0);
    lua_setfield(L, frame_index, "__ow_visible");
    frame_api::NotifyFrameInputMutation(L, frame_index, false);
  };

  apply_visibility(primary_index);
  apply_visibility(effect_index);
  lua_settop(L, top);
  return 0;
}

static void DestroyArrowFrame(lua_State* L, const char* frame_name) {
  if (auto* manager = GetMapUiManager(L); manager != nullptr) {
    manager->DestroyNamedFrame(frame_name);
    return;
  }

  lua_getglobal(L, frame_name);
  if (lua_istable(L, -1) != 0) {
    lua_getfield(L, -1, "__ow_ref");
    const int ref = lua_isinteger(L, -1) != 0
                        ? static_cast<int>(lua_tointeger(L, -1))
                        : LUA_NOREF;
    lua_pop(L, 1);
    openwow::ui::UnregisterLuaGlobal(L, frame_name);
    if (ref != LUA_NOREF) {
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
  }
  lua_pop(L, 1);
}

static void RefreshArrowFrameFacing(lua_State* L, const char* frame_name,
                                    float facing) {
  const int top = lua_gettop(L);
  if (!PushNamedGlobalFrame(L, frame_name)) {
    return;
  }

  const int frame_index = lua_absindex(L, -1);
  lua_pushnumber(L, static_cast<lua_Number>(facing));
  lua_setfield(L, frame_index, "__ow_model_facing");
  lua_settop(L, top);
}

static void RefreshArrowFramePosition(lua_State* L, const char* frame_name) {
  const int top = lua_gettop(L);
  if (!PushNamedGlobalFrame(L, frame_name)) {
    return;
  }

  const int frame_index = lua_absindex(L, -1);
  SyncArrowModelPosition(L, frame_index);
  lua_settop(L, top);
}

static std::optional<float> ResolveCameraBoundObjectFacing(lua_State* state) {
  const auto* const manager = runtime::WorldUiRuntimeContext::FromLua(state);
  if (manager == nullptr) {
    return std::nullopt;
  }

  const auto bound_guid = manager->world_camera().bound_object();
  auto* const session = GetWorldSession(state);
  if (bound_guid == 0u || session == nullptr) {
    return std::nullopt;
  }
  const auto* const object =
      openwow::game::CGObject_HasFlags(session->objects(), bound_guid, 1);
  if (object == nullptr) {
    return std::nullopt;
  }
  return object->GetWorldFacing();
}

int LuaGetMinimapZoneText(lua_State* L) {
  const auto* session = GetWorldSession(L);
  const auto text = session != nullptr
                        ? session->scene_state().GetMinimapZoneText()
                        : std::string{};
  lua_pushstring(L, text.empty() ? "" : text.c_str());
  return 1;
}

int LuaGetRealZoneText(lua_State* L) {
  const auto* session = GetWorldSession(L);
  const auto text = session != nullptr
                        ? session->scene_state().GetRealZoneText()
                        : std::string{};
  lua_pushstring(L, text.empty() ? "" : text.c_str());
  return 1;
}

int LuaGetSubZoneText(lua_State* L) {
  const auto* session = GetWorldSession(L);
  const auto text = session != nullptr
                        ? session->scene_state().GetSubZoneText()
                        : std::string{};
  lua_pushstring(L, text.empty() ? "" : text.c_str());
  return 1;
}

int LuaGetZoneText(lua_State* L) {
  const auto* session = GetWorldSession(L);
  const auto text = session != nullptr
                        ? session->scene_state().GetZoneText()
                        : std::string{};
  lua_pushstring(L, text.empty() ? "" : text.c_str());
  return 1;
}

int LuaGetZonePVPInfo(lua_State* L) {
  const auto* const dbc = GetDbcLoader(L);
  auto* const session = GetWorldSession(L);
  if (dbc == nullptr || session == nullptr) {
    return 0;
  }

  const auto selected_realm =
      openwow::net::ClientServices::Instance().GetSelectedRealmScriptMetadata();
  const auto result = openwow::game::ResolveRetailZonePvpInfo(
      *dbc,
      session->objects().GetActivePlayer(),
      session->objects().GetZoneId(),
      session->objects().GetAreaId(),
      selected_realm.has_value() && selected_realm->is_pvp_flag);
  if (!result.available) {
    return 0;
  }

  const char* type = "contested";
  switch (result.type) {
    case openwow::game::ZonePvPType::Friendly: type = "friendly"; break;
    case openwow::game::ZonePvPType::Hostile: type = "hostile"; break;
    case openwow::game::ZonePvPType::Contested: break;
    case openwow::game::ZonePvPType::Sanctuary: type = "sanctuary"; break;
    case openwow::game::ZonePvPType::FFA: type = "arena"; break;
    case openwow::game::ZonePvPType::Combat: type = "combat"; break;
  }

  lua_pushstring(L, type);
  lua_pushwowbool(L, result.is_sub_zone_pvp);
  if (!result.has_faction_name) {
    return 2;
  }

  lua_pushlstring(L, result.faction_name.data(), result.faction_name.size());
  return 3;
}

int LuaGetCurrentMapAreaID(lua_State* L) {
  const auto& wm = GetMapUiManager(L)->world_map();
  lua_pushnumber(L, static_cast<lua_Number>(wm.GetCurrentMapAreaIdForLua()));
  return 1;
}

int LuaGetCurrentMapContinent(lua_State* L) {
  const auto& wm = GetMapUiManager(L)->world_map();
  lua_pushnumber(L, static_cast<lua_Number>(wm.GetCurrentMapContinentForLua()));
  return 1;
}

int LuaGetCurrentMapZone(lua_State* L) {
  const auto& wm = GetMapUiManager(L)->world_map();
  lua_pushnumber(L, static_cast<lua_Number>(wm.GetCurrentMapZoneForLua()));
  return 1;
}

int LuaSetMapToCurrentZone(lua_State* L) {
  auto* session = GetWorldSession(L);
  auto& wm = GetMapUiManager(L)->world_map();

  if (!session || !wm.IsDbcInitialized()) {
    return 0;
  }
  (void)wm.UpdatePlayerPosition(*session);
  return 0;
}

int LuaSetMapByID(lua_State* L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: SetMapByID(mapID)");
  }

  const auto signed_area_id =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1));
  if (signed_area_id < 0) {
    return 0;
  }

  auto& wm = GetMapUiManager(L)->world_map();
  if (!wm.SetMapByWorldMapAreaId(
          static_cast<std::uint32_t>(signed_area_id))) {
    return 0;
  }
  lua_pushboolean(L, 1);
  return 1;
}

int LuaGetMapZones(lua_State* L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetMapZones(continentIndex)");
  }

  const auto& wm = GetMapUiManager(L)->world_map();
  const auto continent_token =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  if (continent_token >= wm.GetContinentCount()) {
    return 0;
  }
  const int continent_1based = static_cast<int>(continent_token + 1u);
  const auto zone_count = wm.GetZoneCountForContinent(continent_1based);
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, zone_count, "map zones");

  for (std::size_t i = 0; i < zone_count; ++i) {
    const auto zone_name =
        wm.GetZoneNameForContinent(continent_1based, static_cast<int>(i + 1));
    if (zone_name.empty()) {
      lua_pushnil(L);
    } else {
      lua_pushstring(L, zone_name.c_str());
    }
  }

  return result_count;
}

int LuaGetMapContinents(lua_State* L) {

  const auto& wm = GetMapUiManager(L)->world_map();

  auto count = wm.GetContinentCount();
  if (count == 0) {
    return 0;
  }

  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, count, "map continents");
  for (std::size_t i = 0; i < count; ++i) {
    const auto* ci = wm.GetContinentInfo(i);
    if (ci != nullptr && !ci->name.empty()) {
      lua_pushstring(L, ci->name.c_str());
    } else {
      lua_pushnil(L);
    }
  }

  return result_count;
}

int LuaGetNumMapOverlays(lua_State* L) {

  const auto& wm = GetMapUiManager(L)->world_map();
  lua_pushnumber(L, static_cast<lua_Number>(wm.GetNumOverlays()));
  return 1;
}

int LuaGetMapOverlayInfo(lua_State* L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetMapOverlayInfo(index)");
  }

  const auto& wm = GetMapUiManager(L)->world_map();
  const auto one_based_index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  const auto overlay =
      one_based_index == 0 ? std::nullopt
                           : wm.GetVisibleOverlaySnapshot(
                                 static_cast<std::size_t>(one_based_index - 1));
  if (!overlay) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 7;
  }

  lua_pushstring(L, overlay->texture_path.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(overlay->texture_w));
  lua_pushnumber(L, static_cast<lua_Number>(overlay->texture_h));
  lua_pushnumber(L, static_cast<lua_Number>(overlay->offset_x));
  lua_pushnumber(L, static_cast<lua_Number>(overlay->offset_y));
  lua_pushnumber(L, static_cast<lua_Number>(overlay->x));
  lua_pushnumber(L, static_cast<lua_Number>(overlay->y));
  return 7;
}

int LuaIsIndoors(lua_State* L) {
  const auto* const session = GetWorldSession(L);
  const auto* const player =
      session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  lua_pushwowbool(
      L, player != nullptr && openwow::game::IsPlayerIndoors(*player));
  return 1;
}

int LuaGetBindLocation(lua_State* L) {
  const std::string location = detail::ResolveBindLocationText(L);
  lua_pushstring(L, location.c_str());
  return 1;
}

int LuaDungeonUsesTerrainMap(lua_State* L) {
  if (GetMapUiManager(L)->world_map().CurrentSelectionUsesTerrainMap()) {
    FrameScript_PushNumber(L, 1.0);
  } else {
    FrameScript_PushNil(L);
  }
  return 1;
}

int LuaGetNumMapDebugObjects(lua_State* L) {
  FrameScript_PushNumber(L, 0.0);
  return 1;
}

int LuaGetMapDebugObjectInfo([[maybe_unused]] lua_State* L) {
  return 0;
}

int LuaTeleportToDebugObject([[maybe_unused]] lua_State* L) {
  return 0;
}

int LuaHasDebugZoneMap([[maybe_unused]] lua_State* L) {
  return 0;
}

int LuaGetDebugZoneMap([[maybe_unused]] lua_State* L) {
  return 0;
}

int LuaGetNumDungeonMapLevels(lua_State* L) {
  const auto& wm = GetMapUiManager(L)->world_map();
  lua_pushnumber(L, static_cast<lua_Number>(wm.GetNumDungeonMapLevels()));
  return 1;
}

int LuaSetDungeonMapLevel(lua_State* L) {
  auto& wm = GetMapUiManager(L)->world_map();
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: SetDungeonMapLevel(level)");
  }

  const int level = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  if (!wm.SetDungeonMapLevel(level)) {
    return 0;
  }

  return 0;
}

int LuaUpdateMapHighlight(lua_State* L) {
  if (lua_isnumber(L, 1) == 0 || lua_isnumber(L, 2) == 0) {
    return luaL_error(L, "Usage: UpdateMapHighlight(x, y)");
  }

  const float norm_x = static_cast<float>(lua_tonumber(L, 1));
  const float norm_y = static_cast<float>(lua_tonumber(L, 2));
  const auto highlight = GetMapUiManager(L)->world_map().ResolveMapHighlight(norm_x, norm_y);

  if (highlight.display_name.has_value()) {
    lua_pushstring(L, highlight.display_name->c_str());
  } else {
    lua_pushnil(L);
  }

  if (highlight.file_name.has_value()) {
    lua_pushstring(L, highlight.file_name->c_str());
  } else {
    lua_pushnil(L);
  }

  lua_pushnumber(L, static_cast<lua_Number>(highlight.map_width_scale));
  lua_pushnumber(L, static_cast<lua_Number>(highlight.map_height_scale));
  lua_pushnumber(L, static_cast<lua_Number>(highlight.texture_width_scale));
  lua_pushnumber(L, static_cast<lua_Number>(highlight.texture_height_scale));
  lua_pushnumber(L, static_cast<lua_Number>(highlight.texture_offset_x));
  lua_pushnumber(L, static_cast<lua_Number>(highlight.texture_offset_y));
  return 8;
}

int LuaZoomOut(lua_State* L) {
  (void)GetMapUiManager(L)->world_map().ZoomOut();
  return 0;
}

int LuaIsZoomOutAvailable(lua_State* L) {
  lua_pushwowbool(L, GetMapUiManager(L)->world_map().IsZoomOutAvailable());
  return 1;
}

int LuaCreateWorldMapArrowFrame(lua_State* L) {
  const int parent_index =
      RequireArrowParentFrame(L, 1, "CreateWorldMapArrowFrame");
  EnsureArrowFramePair(L, parent_index, kWorldMapArrowFrames);
  return 0;
}

int LuaCreateMiniWorldMapArrowFrame(lua_State* L) {
  const int parent_index =
      RequireArrowParentFrame(L, 1, "CreateMiniWorldMapArrowFrame");
  EnsureArrowFramePair(L, parent_index, kMiniWorldMapArrowFrames);
  return 0;
}

int LuaInitWorldMapPing(lua_State* L) {
  if (lua_type(L, 1) != LUA_TTABLE) {
    return luaL_error(L, "Usage: InitWorldMapPing(parent)");
  }

  if (!HasLuaScriptObjectThis(L, 1)) {
    return luaL_error(
        L, "InitWorldMapPing(): Couldn't find 'this' in parent object");
  }

  if (!LuaScriptObjectIsKindOfCanonicalType(
          L, 1, openwow::ui::widgets::ScriptObjectType::Frame)) {
    return luaL_error(L,
                      "InitWorldMapPing(): Wrong object type, expected frame");
  }

  if (!TryPushNamedChildFrame(L, 1, "WorldMapPing")) {
    return 0;
  }

  WriteModelWidgetCenteredPosition(L, -1);
  lua_pop(L, 1);
  return 0;
}

int LuaPositionWorldMapArrowFrame(lua_State* L) {
  return PositionArrowFramePair(L, kWorldMapArrowFrames,
                                "PositionWorldMapArrowFrame");
}

int LuaPositionMiniWorldMapArrowFrame(lua_State* L) {
  return PositionArrowFramePair(L, kMiniWorldMapArrowFrames,
                                "PositionMiniWorldMapArrowFrame");
}

int LuaShowWorldMapArrowFrame(lua_State* L) {
  return ShowArrowFramePair(L, kWorldMapArrowFrames);
}

int LuaShowMiniWorldMapArrowFrame(lua_State* L) {
  return ShowArrowFramePair(L, kMiniWorldMapArrowFrames);
}

int LuaUpdateWorldMapArrowFrames(lua_State* L) {
  for (const char* frame_name : kAllArrowFrameNames) {
    RefreshArrowFramePosition(L, frame_name);
  }

  const std::optional<float> facing = ResolveCameraBoundObjectFacing(L);
  if (!facing.has_value()) {
    return 0;
  }

  for (const char* frame_name : kAllArrowFrameNames) {
    RefreshArrowFrameFacing(L, frame_name, *facing);
  }
  return 0;
}

void DestroyWorldMapArrowFrames(lua_State* L) {
  if (!L) {
    return;
  }

  for (const char* frame_name : kAllArrowFrameNames) {
    DestroyArrowFrame(L, frame_name);
  }
}

int LuaGetCurrentMapDungeonLevel(lua_State* L) {
  const auto& wm = GetMapUiManager(L)->world_map();
  lua_pushnumber(L, static_cast<lua_Number>(wm.GetCurrentMapDungeonLevelForLua()));
  return 1;
}

int LuaApi_CanHearthAndResurrectFromArea(lua_State *L) {
  const auto *session = ResolveAreaQuerySession(L);
  const auto *area = session != nullptr ? LookupCurrentSubZoneAreaEntry(*session) : nullptr;
  if (area != nullptr &&
      (area->flags & openwow::data::dbc::kAreaFlagCanHearthAndResurrect) != 0) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaApi_CannotBeResurrected(lua_State *L) {
  const auto *const session = GetWorldSession(L);
  const auto *const player =
      session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (player != nullptr && player->CannotBeResurrected()) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaApi_ClickLandmark(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: ClickLandmark(mapLinkID)");
  }

  const auto map_link_id =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  (void)GetMapUiManager(L)->world_map().ClickLandmark(map_link_id);
  return 0;
}

int LuaApi_HearthAndResurrectFromArea(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  session->interaction().SendHearthAndResurrect();
  return 0;
}

int LuaApi_IsFlyableArea(lua_State *L) {
  const auto *session = ResolveAreaQuerySession(L);
  if (session != nullptr && openwow::game::IsFlyableAreaForActivePlayer(*session)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaApi_IsSubZonePVPPOI(lua_State *L) {
  const auto *session = ResolveAreaQuerySession(L);
  const auto *area = session != nullptr ? LookupCurrentSubZoneAreaEntry(*session) : nullptr;
  if (area != nullptr && (area->flags & kAreaFlagPvPPoi) != 0u) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaApi_QuestPOIUpdateTexture(lua_State *L) {
  lua_getglobal(L, "QuestPOIFrame");
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "UpdateQuestPOI");
    if (lua_isfunction(L, -1)) {
      lua_pushvalue(L, -2);
      lua_call(L, 1, 0);
      lua_pop(L, 1);
    } else {
      lua_pop(L, 2);
    }
  } else {
    lua_pop(L, 1);
  }
  return 0;
}

int LuaApi_QuestPOIUpdateIcons(lua_State *L) {
  return LuaApi_QuestPOIUpdateTexture(L);
}

int LuaApi_SetPOIIconOverlapDistance(lua_State *L) {
  const auto distance = static_cast<float>(
      ReadRequiredPoiOverlapNumber(L, "Usage: SetPOIIconOverlapDistance(index)"));
  openwow::game::QuestPOIData::Get().SetIconOverlapDistance(distance);
  return 0;
}

int LuaApi_SetPOIIconOverlapPushDistance(lua_State *L) {
  const auto distance = static_cast<float>(
      ReadRequiredPoiOverlapNumber(L, "Usage: SetPOIIconOverlapPushDistance(index)"));
  openwow::game::QuestPOIData::Get().SetIconOverlapPushDistance(distance);
  return 0;
}

}

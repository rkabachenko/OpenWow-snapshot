#include "openwow/world/presentation/adapters/lua/camera_lua_bindings.h"

#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaCameraZoomIn(lua_State* L);
int LuaCameraZoomOut(lua_State* L);
int LuaMoveViewInStart(lua_State* L);
int LuaMoveViewInStop(lua_State* L);
int LuaMoveViewOutStart(lua_State* L);
int LuaMoveViewOutStop(lua_State* L);
int LuaMoveViewLeftStart(lua_State* L);
int LuaMoveViewLeftStop(lua_State* L);
int LuaMoveViewRightStart(lua_State* L);
int LuaMoveViewRightStop(lua_State* L);
int LuaMoveViewUpStart(lua_State* L);
int LuaMoveViewUpStop(lua_State* L);
int LuaMoveViewDownStart(lua_State* L);
int LuaMoveViewDownStop(lua_State* L);
int LuaSetView(lua_State* L);
int LuaSaveView(lua_State* L);
int LuaResetView(lua_State* L);
int LuaApi_NextView(lua_State* L);
int LuaPrevView(lua_State* L);
int LuaFlipCameraYaw(lua_State* L);
int LuaApi_VehicleCameraZoomIn(lua_State* L);
int LuaApi_VehicleCameraZoomOut(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kCameraLuaBindings[] = {
    {"CameraZoomIn", LuaCameraZoomIn},
    {"CameraZoomOut", LuaCameraZoomOut},
    {"MoveViewInStart", LuaMoveViewInStart},
    {"MoveViewInStop", LuaMoveViewInStop},
    {"MoveViewOutStart", LuaMoveViewOutStart},
    {"MoveViewOutStop", LuaMoveViewOutStop},
    {"MoveViewLeftStart", LuaMoveViewLeftStart},
    {"MoveViewLeftStop", LuaMoveViewLeftStop},
    {"MoveViewRightStart", LuaMoveViewRightStart},
    {"MoveViewRightStop", LuaMoveViewRightStop},
    {"MoveViewUpStart", LuaMoveViewUpStart},
    {"MoveViewUpStop", LuaMoveViewUpStop},
    {"MoveViewDownStart", LuaMoveViewDownStart},
    {"MoveViewDownStop", LuaMoveViewDownStop},
    {"SetView", LuaSetView},
    {"SaveView", LuaSaveView},
    {"ResetView", LuaResetView},
    {"NextView", LuaApi_NextView},
    {"PrevView", LuaPrevView},
    {"FlipCameraYaw", LuaFlipCameraYaw},
    {"VehicleCameraZoomIn", LuaApi_VehicleCameraZoomIn},
    {"VehicleCameraZoomOut", LuaApi_VehicleCameraZoomOut},
};

}

openwow::ui::lua::NativeBindingCatalog CameraNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "world.presentation.cameras", openwow::ui::lua::BindingScope::kWorld, kCameraLuaBindings);
}

}

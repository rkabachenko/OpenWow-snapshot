#pragma once

struct lua_State;

namespace openwow::ui::glue::detail {

int LuaScriptFileAccessDenied(lua_State* state);
int LuaConsoleExec(lua_State* state);
int LuaGetMovieResolution(lua_State* state);
int LuaRestoreVideoEffectsDefaults(lua_State* state);
int LuaRestoreVideoResolutionDefaults(lua_State* state);
int LuaRestoreVideoStereoDefaults(lua_State* state);

}

#pragma once

struct lua_State;

namespace openwow::ui {

enum class FrameScriptGlobalProfile {
  Game,
  Glue,
};

void OpenFrameScriptRetailLibraries(lua_State* state);

void RegisterFrameScriptStandardGlobals(lua_State* state, FrameScriptGlobalProfile profile);

}

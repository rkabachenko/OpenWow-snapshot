#pragma once

#include "openwow/ui/lua_binding_registry.h"

#include <span>

struct lua_State;

namespace openwow::audio {

std::span<const char* const> GetSoundVoiceChatLuaGlobalNames();

std::span<const openwow::ui::LuaGlobalBinding> GetSoundVoiceChatLuaBindings();

void RegisterSoundVoiceChatScriptFunctions(lua_State* L);

void UnregisterSoundVoiceChatScriptFunctions(lua_State* L);

}

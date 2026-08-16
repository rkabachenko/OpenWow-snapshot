#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/game/framescript/core/frame_event_methods.h"

#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/script_event_catalog.h"

extern "C" {
#include <lua.hpp>
}

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace openwow::ui::game::frame_api {

namespace {

constexpr const char* kGameUiManagerRegistryKey = "openwow.world_ui_runtime_context";

bool IsWrongFrameReceiverType(std::string_view type_name) {
  return type_name == "FontString" || type_name == "Texture" ||
         type_name == "Line" || type_name == "Region" ||
         type_name == "Font" || type_name == "Animation" ||
         type_name == "AnimationGroup";
}

void RequireFrameReceiver(lua_State* L) {
  if (lua_istable(L, 1) == 0) {
    luaL_error(
        L,
        "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
  }

  lua_getfield(L, 1, "__ow_type");
  const char* type_name = lua_tostring(L, -1);
  const std::string_view type_view = type_name != nullptr
                                         ? std::string_view(type_name)
                                         : std::string_view();
  lua_pop(L, 1);

  if (type_view.empty()) {
    luaL_error(L, "Attempt to find 'this' in non-framescript object");
  }
  if (IsWrongFrameReceiverType(type_view)) {
    luaL_error(L, "Wrong object type for member function");
  }
}

const ScriptEventDescriptor* ResolveWorldFrameScriptEvent(
    const std::string_view event_name) {
  return ScriptEventCatalog::Instance().Find(event_name);
}

const std::vector<std::string_view>& WorldFrameScriptEventNames() {
  static const std::vector<std::string_view> names = [] {
    std::vector<std::string_view> result;
    const auto& events = ScriptEventCatalog::Instance().events();
    result.reserve(events.size());
    std::unordered_set<std::string_view> seen;
    seen.reserve(events.size());
    for (const auto& event : events) {
      if (event.registerable_by_frame && seen.insert(event.name).second) {
        result.push_back(event.name);
      }
    }
    return result;
  }();
  return names;
}

runtime::WorldUiRuntimeContext* GetGameUIManager(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kGameUiManagerRegistryKey);
  auto* manager = static_cast<runtime::WorldUiRuntimeContext*>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return manager;
}

int GetStoredFrameRef(lua_State* L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  lua_getfield(L, frame_index, "__ow_ref");
  const int ref =
      lua_isinteger(L, -1) != 0 ? static_cast<int>(lua_tointeger(L, -1))
                                : LUA_NOREF;
  lua_pop(L, 1);
  return ref;
}

int EnsureFrameRef(lua_State* L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  const int existing_ref = GetStoredFrameRef(L, frame_index);
  if (existing_ref != LUA_NOREF) {
    return existing_ref;
  }

  lua_pushvalue(L, frame_index);
  const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_pushinteger(L, ref);
  lua_setfield(L, frame_index, "__ow_ref");
  return ref;
}

int EnsureEventTable(lua_State* L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  lua_getfield(L, frame_index, "__ow_events");
  if (lua_istable(L, -1) != 0) {
    return lua_absindex(L, -1);
  }

  lua_pop(L, 1);
  lua_newtable(L);
  lua_pushvalue(L, -1);
  lua_setfield(L, frame_index, "__ow_events");
  return lua_absindex(L, -1);
}

int FindEventTable(lua_State* L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  lua_getfield(L, frame_index, "__ow_events");
  if (lua_istable(L, -1) != 0) {
    return lua_absindex(L, -1);
  }
  lua_pop(L, 1);
  return 0;
}

void StoreEventRegistration(lua_State* L, int frame_index,
                            std::string_view event_name, bool registered) {
  const int events_table = EnsureEventTable(L, frame_index);
  lua_pushlstring(L, event_name.data(), event_name.size());
  if (registered) {
    lua_pushboolean(L, 1);
  } else {
    lua_pushnil(L);
  }
  lua_settable(L, events_table);
  lua_pop(L, 1);
}

void ClearAllEventRegistrations(lua_State* L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  lua_newtable(L);
  lua_setfield(L, frame_index, "__ow_events");
}

void PushIsEventRegisteredResult(lua_State* L, int frame_index,
                                 std::string_view event_name) {
  if (runtime::WorldUiRuntimeContext* manager = GetGameUIManager(L);
      manager != nullptr) {
    const int ref = GetStoredFrameRef(L, frame_index);
    if (ref != LUA_NOREF &&
        manager->frame_events().dispatcher().IsEventRegistered(
            std::string(event_name), ref)) {
      lua_pushnumber(L, 1);
    } else {
      lua_pushnil(L);
    }
    return;
  }

  const int events_table = FindEventTable(L, frame_index);
  if (events_table == 0) {
    lua_pushnil(L);
    return;
  }
  lua_pushlstring(L, event_name.data(), event_name.size());
  lua_gettable(L, events_table);
  const bool registered = lua_isnil(L, -1) == 0;
  lua_pop(L, 2);
  if (registered) {
    lua_pushnumber(L, 1);
  } else {
    lua_pushnil(L);
  }
}

}

int LuaFrame_RegisterEvent(lua_State* L) {
  RequireFrameReceiver(L);
  const char* usage_name = lua_adapter::ScriptObjectDisplayName(L, 1);
  if (lua_isstring(L, 2) == 0) {
    return luaL_error(L, "Usage: %s:RegisterEvent(\"event\")",
                      usage_name);
  }

  const char* event_name = lua_tostring(L, 2);
  const std::string_view event_name_view =
      event_name != nullptr ? std::string_view(event_name) : std::string_view();
  const ScriptEventDescriptor* descriptor =
      event_name != nullptr ? ResolveWorldFrameScriptEvent(event_name_view)
                            : nullptr;
  if (descriptor == nullptr || !descriptor->registerable_by_frame) {
    return 0;
  }

  StoreEventRegistration(L, 1, descriptor->name, true);
  if (runtime::WorldUiRuntimeContext* manager = GetGameUIManager(L);
      manager != nullptr) {
    manager->frame_events().dispatcher().RegisterEvent(
        std::string(descriptor->name), EnsureFrameRef(L, 1));
  }
  return 0;
}

int LuaFrame_UnregisterEvent(lua_State* L) {
  RequireFrameReceiver(L);
  const char* usage_name = lua_adapter::ScriptObjectDisplayName(L, 1);
  if (lua_isstring(L, 2) == 0) {
    return luaL_error(L, "Usage: %s:UnregisterEvent(\"event\")",
                      usage_name);
  }

  const char* event_name = lua_tostring(L, 2);
  const std::string_view event_name_view =
      event_name != nullptr ? std::string_view(event_name) : std::string_view();
  const ScriptEventDescriptor* descriptor =
      event_name != nullptr ? ResolveWorldFrameScriptEvent(event_name_view)
                            : nullptr;
  if (descriptor == nullptr || !descriptor->registerable_by_frame) {
    return 0;
  }

  StoreEventRegistration(L, 1, descriptor->name, false);
  if (runtime::WorldUiRuntimeContext* manager = GetGameUIManager(L);
      manager != nullptr) {
    const int ref = GetStoredFrameRef(L, 1);
    if (ref != LUA_NOREF) {
      manager->frame_events().dispatcher().UnregisterEvent(
          std::string(descriptor->name), ref);
    }
  }
  return 0;
}

int LuaFrame_RegisterAllEvents(lua_State* L) {
  RequireFrameReceiver(L);
  runtime::WorldUiRuntimeContext* manager = GetGameUIManager(L);
  const int ref = manager != nullptr ? EnsureFrameRef(L, 1) : LUA_NOREF;
  for (const std::string_view event_name : WorldFrameScriptEventNames()) {
    StoreEventRegistration(L, 1, event_name, true);
    if (manager != nullptr) {
      manager->frame_events().dispatcher().RegisterEvent(
          std::string(event_name), ref);
    }
  }
  return 0;
}

int LuaFrame_UnregisterAllEvents(lua_State* L) {
  RequireFrameReceiver(L);
  ClearAllEventRegistrations(L, 1);
  if (runtime::WorldUiRuntimeContext* manager = GetGameUIManager(L);
      manager != nullptr) {
    const int ref = GetStoredFrameRef(L, 1);
    if (ref != LUA_NOREF) {
      manager->frame_events().dispatcher().UnregisterAllForFrame(ref);
    }
  }
  return 0;
}

int LuaFrame_IsEventRegistered(lua_State* L) {
  RequireFrameReceiver(L);
  const char* usage_name = lua_adapter::ScriptObjectDisplayName(L, 1);
  if (lua_isstring(L, 2) == 0) {
    return luaL_error(L, "Usage: %s:IsEventRegistered(\"event\")",
                      usage_name);
  }

  const char* event_name = lua_tostring(L, 2);
  const std::string_view event_name_view =
      event_name != nullptr ? std::string_view(event_name) : std::string_view();
  const ScriptEventDescriptor* descriptor =
      event_name != nullptr ? ResolveWorldFrameScriptEvent(event_name_view)
                            : nullptr;
  if (descriptor == nullptr || !descriptor->registerable_by_frame) {
    lua_pushnil(L);
    return 1;
  }

  PushIsEventRegisteredResult(L, 1, descriptor->name);
  return 1;
}

}

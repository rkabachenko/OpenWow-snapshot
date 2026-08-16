
#include "openwow/ui/frame_script_events.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

#include "openwow/ui/lua_c_api_convenience.h"

extern "C" {
#include <lua.hpp>
}

#include "openwow/runtime/time/game_clock.h"
#include "openwow/ui/game/event_dispatcher.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/lua_taint_api.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/widgets/script_object.h"

namespace openwow::ui::frame_script_events {

static int s_scriptErrorHandlerRef = LUA_NOREF;

static lua_State* s_luaState = nullptr;

static bool s_profilingEnabled = false;

static int64_t s_totalScriptTicks = 0;

static int s_scriptNestingDepth = 0;

static int s_errorSuppression = 0;

static int s_currentEventIdValue = 0;
static int* s_currentEventIdPtr = &s_currentEventIdValue;

static char s_argNameBuf[8] = "arg0";

static void FormatArgName(int n) {
    if (n >= 10) {
        std::snprintf(s_argNameBuf + 3, sizeof(s_argNameBuf) - 3, "%d", n);
    } else {
        s_argNameBuf[3] = static_cast<char>('0' + n);
        s_argNameBuf[4] = '\0';
    }
}

static constexpr int kFrameScriptMaxEventId = 1024;
static const char* s_eventNames[kFrameScriptMaxEventId] = {nullptr};

void FrameScript_RegisterEventName(int eventId, const char* name) {
    if (eventId >= 0 && eventId < kFrameScriptMaxEventId && name) {
        s_eventNames[eventId] = name;
    }
}

static const char* FrameScript_LookupEventName(int eventId) {
    if (eventId >= 0 && eventId < kFrameScriptMaxEventId) {
        return s_eventNames[eventId];
    }
    return nullptr;
}

static void AcquireLuaBindingIfNeeded(
    lua_State* L, widgets::CScriptObject* obj) {
    if (!obj || !L) return;
    if (obj->GetLuaRef() != LUA_NOREF) return;

    lua_newtable(L);

    lua_pushnumber(L, 0.0);
    lua_pushlightuserdata(L, static_cast<void*>(obj));
    lua_rawset(L, -3);

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    obj->SetLuaRef(ref);

    const auto& name = obj->GetName();
    if (!name.empty()) {
        lua_getglobal(L, name.c_str());
        bool occupied = (lua_type(L, -1) != LUA_TNIL);
        lua_pop(L, 1);
        if (!occupied) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            openwow::ui::ReplaceLuaGlobalValue(L, name.c_str(), -1);
            lua_pop(L, 1);
        }
    }
}

int FrameScript_WrapOnEventHandler(void* thisPtr, const char* handlerName,
                                   std::uintptr_t* outTemplate) {

#ifdef _WIN32
    if (_stricmp(handlerName, "OnEvent") != 0) {
#else
    if (strcasecmp(handlerName, "OnEvent") != 0) {
#endif
        return 0;
    }
    *outTemplate = reinterpret_cast<std::uintptr_t>(
        "return function(self,event,...) %s end");
    return static_cast<int>(reinterpret_cast<std::intptr_t>(static_cast<char*>(thisPtr) + 12));
}

void FrameScript_SetLuaStateTyped(lua_State* L) {
    s_luaState = L;
}

lua_State* FrameScript_GetLuaStateTyped() {
    return s_luaState;
}

void FrameScript_SetProfilingEnabled(bool enabled) {
    s_profilingEnabled = enabled;
}
bool FrameScript_GetProfilingEnabled() { return s_profilingEnabled; }

int64_t FrameScript_GetTotalScriptTicks() { return s_totalScriptTicks; }
void FrameScript_ResetTotalScriptTicks() { s_totalScriptTicks = 0; }

int FrameScript_GetScriptNestingDepth() { return s_scriptNestingDepth; }

void FrameScript_SetErrorSuppression(int value) {
    s_errorSuppression = value;
}
int FrameScript_GetErrorSuppression() { return s_errorSuppression; }

void FrameScript_SetCurrentEventIdPtr(int* ptr) {
    s_currentEventIdPtr = ptr ? ptr : &s_currentEventIdValue;
}
int* FrameScript_GetCurrentEventIdPtr() { return s_currentEventIdPtr; }

int FrameScript_GetErrorHandlerRef() {
    return s_scriptErrorHandlerRef;
}

void FrameScript_SetErrorHandlerRef(int ref) {
    s_scriptErrorHandlerRef = ref;
}

int FrameScript_RunScript(int scriptRef, int* frameObj, int argCount,
                          int eventId, int eventObj) {
    lua_State* L = s_luaState;
    if (!L) return 0;

    int64_t startTime = 0;
    if (s_profilingEnabled)
        startTime = static_cast<int64_t>(
            core::GameClock::GetRawTimingCounter());

    int baseIndex = lua_gettop(L) - argCount + 1;

    int nArgs = argCount;

    int savedEventId = *s_currentEventIdPtr;

    if (s_scriptNestingDepth && !s_errorSuppression)
        *s_currentEventIdPtr = 0;

    int savedNestingDepth = s_scriptNestingDepth;
    s_scriptNestingDepth = 0;

    lua_checkstack(L, argCount + 2);

    auto* obj = reinterpret_cast<widgets::CScriptObject*>(frameObj);

    openwow::ui::ScopedNeutralLuaExecutionTaint neutral_bookkeeping(L);

    if (frameObj) {
        lua_pushstring(L, "this");
        lua_rawget(L, LUA_GLOBALSINDEX);

        AcquireLuaBindingIfNeeded(L, obj);

        int luaRef = obj->GetLuaRef();
        if (luaRef != LUA_NOREF) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef);

            lua_pushstring(L, "this");
            lua_insert(L, -2);
            lua_rawset(L, LUA_GLOBALSINDEX);
        }
    }

    if (eventObj) {
        lua_pushstring(L, "event");
        lua_rawget(L, LUA_GLOBALSINDEX);

        lua_pushvalue(L, baseIndex);
        lua_pushstring(L, "event");
        lua_insert(L, -2);
        lua_rawset(L, LUA_GLOBALSINDEX);
    }

    int argIdx = 0;
    int stackOffset = (eventObj != 0) ? 1 : 0;

    for (int i = stackOffset; i < argCount; ++i) {
        ++argIdx;
        FormatArgName(argIdx);

        lua_pushstring(L, s_argNameBuf);
        lua_rawget(L, LUA_GLOBALSINDEX);

        lua_pushvalue(L, i + baseIndex);
        lua_pushstring(L, s_argNameBuf);
        lua_insert(L, -2);
        lua_rawset(L, LUA_GLOBALSINDEX);
    }

    if (savedNestingDepth && !s_errorSuppression)
        *s_currentEventIdPtr = savedEventId;

    savedEventId = *s_currentEventIdPtr;

    s_scriptNestingDepth = savedNestingDepth + 1;

    if (savedNestingDepth != -1 && !s_errorSuppression)
        *s_currentEventIdPtr = eventId;

    lua_checkstack(L, argCount + 3);

    lua_rawgeti(L, LUA_REGISTRYINDEX, s_scriptErrorHandlerRef);

    lua_rawgeti(L, LUA_REGISTRYINDEX, scriptRef);

    const int handler_taint = openwow::ui::lua_get_taint(L, lua_gettop(L));

    if (frameObj) {
        AcquireLuaBindingIfNeeded(L, obj);

        int luaRef = obj->GetLuaRef();
        if (luaRef != LUA_NOREF) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef);
            nArgs = argCount + 1;
        }
    }

    for (int i = 0; i < argCount; ++i)
        lua_pushvalue(L, i + baseIndex);

    {
        const openwow::ui::game::SecureExecution::TaintScope handler_scope(
            L, handler_taint);
        if (lua_pcall(L, nArgs, 0, -2 - nArgs) != 0)
            lua_settop(L, -2);
    }

    lua_settop(L, -2);

    if (s_scriptNestingDepth && !s_errorSuppression)
        *s_currentEventIdPtr = savedEventId;

    s_scriptNestingDepth = 0;
    for (int i = argIdx; i > 0; --i) {
        FormatArgName(i);

        lua_pushstring(L, s_argNameBuf);
        lua_insert(L, -2);
        lua_rawset(L, LUA_GLOBALSINDEX);
    }

    if (eventObj) {
        lua_pushstring(L, "event");
        lua_insert(L, -2);
        lua_rawset(L, LUA_GLOBALSINDEX);
    }

    if (frameObj) {
        lua_pushstring(L, "this");
        lua_insert(L, -2);
        lua_rawset(L, LUA_GLOBALSINDEX);
    }

    s_scriptNestingDepth = savedNestingDepth;

    lua_settop(L, lua_gettop(L) - argCount);

    int64_t elapsed = 0;
    if (s_profilingEnabled) {
        elapsed = static_cast<int64_t>(
            core::GameClock::GetRawTimingCounter()) - startTime;

        s_totalScriptTicks += elapsed;
    }

    return static_cast<int>(elapsed);
}

int CSimpleAnim_FireScriptHandler(int* thisObj, ScriptHandlerRef* handlerSlot,
                                  int argCount, int eventIdOverride) {
    int eventId = eventIdOverride != 0 ? eventIdOverride : handlerSlot->eventId;
    return FrameScript_RunScript(handlerSlot->scriptRef, thisObj, argCount,
                                 eventId, 0);
}

void FrameScript_SignalEvent(int eventId, lua_State* L, int argCount) {

    if (!L) return;

    const int top = lua_gettop(L);
    if (top < 1) return;

    const int baseIndex = top - argCount + 1;
    if (baseIndex < 1) return;

    const char* eventName = lua_tostring(L, baseIndex);
    if (!eventName || eventName[0] == '\0') return;

    FrameScript_RegisterEventName(eventId, eventName);

    using openwow::ui::game::EventArg;
    std::vector<EventArg> args;
    for (int i = 1; i < argCount; ++i) {
        const int idx = baseIndex + i;
        if (idx > top) break;

        switch (lua_type(L, idx)) {
            case LUA_TSTRING:
                args.emplace_back(std::string(lua_tostring(L, idx)));
                break;
            case LUA_TNUMBER:
                args.emplace_back(lua_tonumber(L, idx));
                break;
            case LUA_TBOOLEAN:
                args.emplace_back(lua_toboolean(L, idx) != 0);
                break;
            default:
                args.emplace_back(std::monostate{});
                break;
        }
    }

    auto* manager = openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(L);
    if (manager) {
        manager->frame_events().dispatcher().FireEventV(eventName, args);
    } else {
        openwow::ui::game::ScriptEventDispatch::Get().FireEventV(eventName, args);
    }
}

static void PushFormattedEventArgs(lua_State* L, const char* fmt, va_list& args,
                                   int* argCount) {
    if (!fmt) return;

    for (const char* cursor = fmt; *cursor != '\0'; ++cursor) {
        if (*cursor != '%') {
            continue;
        }

        ++cursor;
        if (*cursor == '\0') {
            break;
        }

        switch (*cursor) {
            case 'b':
                lua_pushboolean(L, va_arg(args, int) != 0);
                ++(*argCount);
                break;
            case 'd':
                lua_pushnumber(L, static_cast<lua_Number>(va_arg(args, int)));
                ++(*argCount);
                break;
            case 'u':
                lua_pushnumber(
                    L, static_cast<lua_Number>(va_arg(args, unsigned int)));
                ++(*argCount);
                break;
            case 'f':
                lua_pushnumber(L, static_cast<lua_Number>(va_arg(args, double)));
                ++(*argCount);
                break;
            case 's':
                lua_pushstring(L, va_arg(args, const char*));
                ++(*argCount);
                break;
            default:
                break;
        }
    }
}

static void FireEventFmtWithArgs(int eventId, const char* fmt, va_list& args) {
    auto* L = s_luaState;
    if (!L) return;

    const char* eventName = FrameScript_LookupEventName(eventId);
    if (!eventName) return;

    const int savedTop = lua_gettop(L);
    lua_pushstring(L, eventName);
    int argCount = 1;
    PushFormattedEventArgs(L, fmt, args, &argCount);
    FrameScript_SignalEvent(eventId, L, argCount);
    lua_settop(L, savedTop);
}

void FrameScript_FireEventFmtV(int eventId, const char* fmt,
                               const void* va_args) {
    if (va_args == nullptr) {
        auto* L = s_luaState;
        if (!L) return;

        const char* eventName = FrameScript_LookupEventName(eventId);
        if (!eventName) return;

        const int savedTop = lua_gettop(L);
        lua_pushstring(L, eventName);
        FrameScript_SignalEvent(eventId, L, 1);
        lua_settop(L, savedTop);
        return;
    }

    va_list& caller_args = *static_cast<va_list*>(const_cast<void*>(va_args));
    va_list args;
    va_copy(args, caller_args);
    FireEventFmtWithArgs(eventId, fmt, args);
    va_end(args);
}

void FrameScript_FireEventFmt(int eventId, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    FireEventFmtWithArgs(eventId, fmt, ap);
    va_end(ap);
}

}

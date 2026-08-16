#pragma once

#include <cstdint>

struct lua_State;

namespace openwow::ui::frame_script_events {

void FrameScript_SetLuaStateTyped(lua_State* state);
lua_State* FrameScript_GetLuaStateTyped();

void FrameScript_SetProfilingEnabled(bool enabled);
bool FrameScript_GetProfilingEnabled();

int64_t FrameScript_GetTotalScriptTicks();
void FrameScript_ResetTotalScriptTicks();

int FrameScript_GetScriptNestingDepth();

void FrameScript_SetErrorSuppression(int value);
int FrameScript_GetErrorSuppression();

void FrameScript_SetCurrentEventIdPtr(int* ptr);
int* FrameScript_GetCurrentEventIdPtr();

int FrameScript_WrapOnEventHandler(void* thisPtr, const char* handlerName,
                                   std::uintptr_t* outTemplate);

int FrameScript_GetErrorHandlerRef();
void FrameScript_SetErrorHandlerRef(int ref);

int FrameScript_RunScript(int scriptRef, int* frameObj, int argCount,
                          int eventId, int eventObj);

struct ScriptHandlerRef {
  int scriptRef;
  int eventId;
};

int CSimpleAnim_FireScriptHandler(int* thisObj, ScriptHandlerRef* handlerSlot,
                                  int argCount, int eventIdOverride);

void FrameScript_RegisterEventName(int eventId, const char* name);
void FrameScript_SignalEvent(int eventId, lua_State* state, int argCount);

void FrameScript_FireEventFmtV(int eventId, const char* fmt,
                               const void* va_args);
void FrameScript_FireEventFmt(int eventId, const char* fmt, ...);

}

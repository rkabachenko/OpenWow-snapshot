#pragma once

#include <cstdint>
#include <span>
#include <string>

struct lua_State;

namespace openwow::game::simple_script {

struct SimpleScript;
using SimpleScriptLifecycleCallback = int (*)(int);

struct SimpleScriptBucketDebugState {
  std::uint32_t capacity = 0;
  std::uint32_t count = 0;
  std::uint32_t grow_quantum = 0;
};

void BindFrameScriptLuaState(lua_State* state);

SimpleScript* SimpleScript_Allocate();

void SimpleScript_Free(SimpleScript* script);

void SimpleScript_ImportGlobal(SimpleScript* scriptObj, const char* name);

int SimpleScript_CompileAndRun(SimpleScript* scriptObj, const char* source,
                               const char* chunkName);

void SimpleScriptFunctionArray_SetCapacity(void* funcTable,
                                           std::uint32_t newCount);

int SimpleScript_RegisterLifecycleCallback(
    SimpleScriptLifecycleCallback callback);

[[nodiscard]] std::uint32_t SimpleScript_UnregisterLifecycleCallback(
    SimpleScriptLifecycleCallback callback);

int SimpleScript_Init(SimpleScript* scriptObj, unsigned int size);

SimpleScript* SimpleScript_Create();

void SimpleScript_Destroy(SimpleScript* script);

struct SimpleScriptFunctionDescriptor {
  const char* chunk_name{nullptr};
  const char* source{nullptr};
  std::span<const char* const> numeric_input_names{};
  std::span<const char* const> string_input_names{};
  std::span<const char* const> numeric_output_names{};
  std::span<const char* const> string_output_names{};
};

struct SimpleScriptFunctionInputs {
  std::span<const double> numeric_values{};
  std::span<const char* const> string_values{};
};

struct SimpleScriptFunctionOutputs {
  std::span<double> numeric_values{};
  std::span<std::string> string_values{};
};

struct SimpleScriptNumericFunctionDescriptor {
  const char* chunk_name{nullptr};
  const char* source{nullptr};
  std::span<const char* const> numeric_input_names{};
  std::span<const char* const> numeric_output_names{};
};

[[nodiscard]] int SimpleScript_EnsureFunction(
    SimpleScript* scriptObj, std::uint32_t function_id,
    const SimpleScriptFunctionDescriptor& descriptor);

[[nodiscard]] int SimpleScript_EnsureNumericFunction(
    SimpleScript* scriptObj, std::uint32_t function_id,
    const SimpleScriptNumericFunctionDescriptor& descriptor);

[[nodiscard]] bool SimpleScript_ExecuteFunction(
    SimpleScript* scriptObj, std::uint32_t function_handle,
    const SimpleScriptFunctionInputs& inputs,
    const SimpleScriptFunctionOutputs& outputs);

[[nodiscard]] bool SimpleScript_ExecuteNumericFunction(
    SimpleScript* scriptObj, std::uint32_t function_handle,
    std::span<const double> numeric_inputs,
    std::span<double> numeric_outputs);

[[nodiscard]] bool SimpleScript_Debug_GetBucketState(
    const SimpleScript* scriptObj, std::uint32_t function_id,
    SimpleScriptBucketDebugState* out_state);

void Sound_PlayArmorFoley(void* unit);

void* StartupPendingString_Get();

void* StartupPendingString_Set(void* value);

int IsConsoleActive();

void ConsoleClient_GrowLineBuffer(int textLen, void* consoleLine);

int ConsoleClient_ReplaceEditableText(void* consoleLine, const char* text);

void ConsoleClient_DeleteCharBeforeCursor(void* consoleLine);

void ConsoleClient_DeleteCharAtCursor(void* consoleLine);

void ConsoleClient_LoadOlderHistoryEntry(void* consoleLine);

void ConsoleClient_LoadNewerHistoryEntry(void* consoleLine);

void ConsoleClient_DestroyLine(void* consoleLine);

void ConsoleClient_ClearAllLines(void* consoleClient);

void ConsoleClient_InsertText(void* consoleLine, const char* text);

}


#include "openwow/game/simple_script.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/console.h"
#include "openwow/core/gxcvar.h"
#include "openwow/core/storm_containers.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/ui/game/lua_addon_memory_tracker.h"
#include "openwow/ui/lua_base_overrides.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::game::simple_script {

namespace {

std::atomic<void*> g_startup_pending_string{nullptr};
std::atomic<lua_State*> g_frame_script_lua_state{nullptr};

constexpr const char kOpenWowErrorHandlerRegistryKey[] = "openwow.error_handler";
constexpr int kInvalidFunctionRef = -2;
constexpr std::size_t kMaxSimpleScriptLifecycleCallbacks = 16u;

struct SimpleScriptFunctionBinding {
  std::uint32_t function_id = 0;
  int function_ref = LUA_NOREF;
  std::uint32_t numeric_input_count = 0;
  std::uint32_t string_input_count = 0;
  std::uint32_t numeric_output_count = 0;
  std::uint32_t string_output_count = 0;
};

static_assert(sizeof(SimpleScriptFunctionBinding) == 24);
static_assert(std::is_trivially_copyable_v<SimpleScriptFunctionBinding>);

struct SimpleScriptFunctionBindingArray {

  static constexpr std::uint32_t kAutoGrowthQuantumLock = 10u;

  [[nodiscard]] SimpleScriptFunctionBinding* Data() { return storage.get(); }
  [[nodiscard]] const SimpleScriptFunctionBinding* Data() const {
    return storage.get();
  }

  [[nodiscard]] bool SetCapacity(const std::uint32_t new_capacity) {
    if (new_capacity == capacity) {
      return true;
    }

    std::unique_ptr<SimpleScriptFunctionBinding[]> rebuilt;
    if (new_capacity != 0u) {
      rebuilt.reset(new (std::nothrow) SimpleScriptFunctionBinding[new_capacity]);
      if (!rebuilt) {
        return false;
      }

      const auto preserved_count = std::min(count, new_capacity);
      if (preserved_count != 0u) {
        std::memcpy(rebuilt.get(), storage.get(),
                    preserved_count * sizeof(SimpleScriptFunctionBinding));
      }
    }

    storage = std::move(rebuilt);
    capacity = new_capacity;
    count = std::min(count, new_capacity);
    return true;
  }

  [[nodiscard]] std::uint32_t AppendRange(
      const SimpleScriptFunctionBinding* const bindings_to_append,
      const std::uint32_t binding_count) {
    if (binding_count == 0u) {
      return count;
    }

    assert(bindings_to_append != nullptr);

    const std::uint32_t previous_count = count;
    const std::uint32_t new_count = previous_count + binding_count;
    if (new_count > capacity) {
      std::uint32_t growth_quantum = grow_quantum;
      if (growth_quantum == 0u) {
        growth_quantum =
            openwow::core::ResolveTSGrowableArrayAutoGrowQuantum<
                kAutoGrowthQuantumLock>(new_count);
        if (new_count >= kAutoGrowthQuantumLock) {
          grow_quantum = growth_quantum;
        }
      }

      std::uint32_t aligned_capacity = new_count;
      if (const std::uint32_t remainder = new_count % growth_quantum;
          remainder != 0u) {
        aligned_capacity += growth_quantum - remainder;
      }

      SimpleScriptFunctionArray_SetCapacity(this, aligned_capacity);
      if (new_count > capacity || storage == nullptr) {
        return count;
      }
    }

    std::memcpy(storage.get() + previous_count, bindings_to_append,
                static_cast<std::size_t>(binding_count) *
                    sizeof(SimpleScriptFunctionBinding));
    count = new_count;
    return previous_count;
  }

  [[nodiscard]] SimpleScriptFunctionBinding* Append(
      const SimpleScriptFunctionBinding& binding,
      std::uint32_t* out_index = nullptr) {
    const std::uint32_t previous_count = count;
    if (AppendRange(&binding, 1u) != previous_count ||
        count == previous_count) {
      return nullptr;
    }

    if (out_index != nullptr) {
      *out_index = previous_count;
    }
    return storage.get() + previous_count;
  }

  void Reset() {
    storage.reset();
    capacity = 0;
    count = 0;
    grow_quantum = 0;
  }

  std::uint32_t capacity = 0;
  std::uint32_t count = 0;
  std::unique_ptr<SimpleScriptFunctionBinding[]> storage;
  std::uint32_t grow_quantum = 0;
};

struct SimpleScriptBucket {
  SimpleScriptFunctionBindingArray bindings;
};

struct ConsoleLineLayout {
  void* previous = nullptr;
  void* next = nullptr;
  char* text_buffer = nullptr;
  std::uint32_t text_length = 0;
  std::uint32_t buffer_capacity = 0;
  std::uint32_t cursor_offset = 0;
  std::uint32_t prompt_offset = 0;
  std::uint32_t line_flags = 0;
  void* rendered_string = nullptr;
};

#if INTPTR_MAX == INT32_MAX
static_assert(sizeof(ConsoleLineLayout) == 36);
#endif

}

struct SimpleScript {
  static constexpr std::uint32_t kUninitializedMask = 0xFFFFFFFFu;

  std::uint32_t hash_mask = kUninitializedMask;
  std::uint32_t bucket_count = 0;
  std::unique_ptr<SimpleScriptBucket[]> buckets;
  int environment_ref = LUA_NOREF;
};

namespace {

void ResetSimpleScript(SimpleScript& script);

struct SimpleScriptDeleter {
  void operator()(SimpleScript* script) const {
    if (script == nullptr) {
      return;
    }

    ResetSimpleScript(*script);
    delete script;
  }
};

std::vector<SimpleScriptLifecycleCallback> g_simple_script_callbacks;

lua_State* GetBoundFrameScriptLuaState() {
  return g_frame_script_lua_state.load(std::memory_order_acquire);
}

int NoOpSimpleScriptErrorHandler(lua_State*) {
  return 0;
}

void PushSimpleScriptErrorHandler(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kOpenWowErrorHandlerRegistryKey);
  if (lua_isfunction(state, -1) == 0) {
    lua_pop(state, 1);
    lua_pushcfunction(state, NoOpSimpleScriptErrorHandler);
  }
}

void ResetSimpleScript(SimpleScript& script) {
  if (script.hash_mask == SimpleScript::kUninitializedMask) {
    return;
  }

  if (lua_State* state = GetBoundFrameScriptLuaState(); state != nullptr) {
    if (script.buckets) {
      for (std::uint32_t bucket_index = 0; bucket_index < script.bucket_count; ++bucket_index) {
        auto& bindings = script.buckets[bucket_index].bindings;
        for (std::uint32_t binding_index = 0; binding_index < bindings.count;
             ++binding_index) {
          const SimpleScriptFunctionBinding& binding =
              bindings.Data()[binding_index];
          if (binding.function_ref != LUA_NOREF) {
            luaL_unref(state, LUA_REGISTRYINDEX, binding.function_ref);
          }
        }
        bindings.Reset();
      }
    }

    if (script.environment_ref != LUA_NOREF) {
      luaL_unref(state, LUA_REGISTRYINDEX, script.environment_ref);
    }
  }

  script.buckets.reset();
  script.bucket_count = 0;
  script.environment_ref = LUA_NOREF;
  script.hash_mask = SimpleScript::kUninitializedMask;
}

void InvokeSimpleScriptLifecycleCallbacks(int phase) {

  std::size_t index = 0;
  while (index < g_simple_script_callbacks.size()) {
    const SimpleScriptLifecycleCallback callback =
        g_simple_script_callbacks[index];
    ++index;
    callback(phase);
  }
}

ConsoleLineLayout& RequireConsoleLine(void* console_line) {
  assert(console_line != nullptr);
  return *static_cast<ConsoleLineLayout*>(console_line);
}

[[nodiscard]] bool IsSimpleScriptInitialized(const SimpleScript& script) {
  return script.hash_mask != SimpleScript::kUninitializedMask &&
         script.environment_ref != LUA_NOREF && script.buckets != nullptr &&
         script.bucket_count != 0;
}

[[nodiscard]] std::uint32_t GetSimpleScriptBucketIndex(const SimpleScript& script,
                                                       const std::uint32_t function_id) {
  return function_id & script.hash_mask;
}

[[nodiscard]] std::uint32_t MakeSimpleScriptHandle(
    const std::uint32_t bucket_index, const std::uint32_t binding_index) {
  return binding_index | (bucket_index << 16);
}

[[nodiscard]] SimpleScriptFunctionBinding* FindSimpleScriptBinding(
    SimpleScript& script, const std::uint32_t function_id,
    std::uint32_t* out_bucket_index = nullptr,
    std::uint32_t* out_binding_index = nullptr) {
  if (!IsSimpleScriptInitialized(script)) {
    return nullptr;
  }

  const std::uint32_t bucket_index =
      GetSimpleScriptBucketIndex(script, function_id);
  auto& bindings = script.buckets[bucket_index].bindings;
  auto* const binding_data = bindings.Data();
  for (std::uint32_t binding_index = 0; binding_index < bindings.count;
       ++binding_index) {
    if (binding_data[binding_index].function_id != function_id) {
      continue;
    }

    if (out_bucket_index != nullptr) {
      *out_bucket_index = bucket_index;
    }
    if (out_binding_index != nullptr) {
      *out_binding_index = binding_index;
    }
    return binding_data + binding_index;
  }

  return nullptr;
}

[[nodiscard]] SimpleScriptFunctionBinding* ResolveSimpleScriptBindingHandle(
    SimpleScript& script, const std::uint32_t function_handle) {
  if (!IsSimpleScriptInitialized(script) || function_handle == 0xFFFFFFFFu) {
    return nullptr;
  }

  const std::uint32_t bucket_index = function_handle >> 16;
  const std::uint32_t binding_index = function_handle & 0xFFFFu;
  if (bucket_index >= script.bucket_count) {
    return nullptr;
  }

  auto& bindings = script.buckets[bucket_index].bindings;
  if (binding_index >= bindings.count) {
    return nullptr;
  }

  return bindings.Data() + binding_index;
}

void AppendSimpleScriptNameList(
    std::string& wrapper,
    const std::span<const char* const> first_names,
    const std::span<const char* const> second_names) {
  bool needs_separator = false;
  const auto append_names = [&](const std::span<const char* const> names) {
    for (const char* name : names) {
      if (needs_separator) {
        wrapper += ", ";
      }
      wrapper += name;
      needs_separator = true;
    }
  };

  append_names(first_names);
  append_names(second_names);
}

void AppendSimpleScriptLocals(std::string& wrapper,
                              const std::span<const char* const> names,
                              const std::string_view initializer) {
  for (const char* name : names) {
    wrapper += "local ";
    wrapper += name;
    wrapper += initializer;
  }
}

[[nodiscard]] std::string BuildSimpleScriptWrapper(
    const SimpleScriptFunctionDescriptor& descriptor) {
  std::string wrapper;
  wrapper.reserve(std::string_view(descriptor.source).size() + 256u);
  wrapper += "return function(";
  AppendSimpleScriptNameList(wrapper, descriptor.numeric_input_names,
                             descriptor.string_input_names);
  wrapper += ")\n";

  AppendSimpleScriptLocals(wrapper, descriptor.numeric_output_names, " = 0.0\n");
  AppendSimpleScriptLocals(wrapper, descriptor.string_output_names, " = \"\"\n");

  wrapper += descriptor.source;
  wrapper += "\nreturn ";
  AppendSimpleScriptNameList(wrapper, descriptor.numeric_output_names,
                             descriptor.string_output_names);
  wrapper += "\nend\n";
  return wrapper;
}

[[nodiscard]] int CompileSimpleScriptFunction(
    SimpleScript& script, const SimpleScriptFunctionDescriptor& descriptor) {
  lua_State* const state = GetBoundFrameScriptLuaState();
  if (state == nullptr || descriptor.chunk_name == nullptr ||
      descriptor.source == nullptr || script.environment_ref == LUA_NOREF) {
    return LUA_NOREF;
  }

  const std::string wrapper = BuildSimpleScriptWrapper(descriptor);
  const int base_top = lua_gettop(state);
  PushSimpleScriptErrorHandler(state);
  const int error_handler_index = base_top + 1;

  if (openwow::ui::LoadClientLuaChunk(state, std::string_view(wrapper),
                                      descriptor.chunk_name) != 0) {
    (void)lua_pcall(state, 1, 0, 0);
    lua_settop(state, base_top);
    return LUA_NOREF;
  }

  if (lua_pcall(state, 0, 1, error_handler_index) != 0) {
    lua_settop(state, base_top);
    return LUA_NOREF;
  }

  lua_rawgeti(state, LUA_REGISTRYINDEX, script.environment_ref);
  lua_setfenv(state, -2);
  const int function_ref = luaL_ref(state, LUA_REGISTRYINDEX);
  lua_settop(state, base_top);
  return function_ref;
}

}

static const char kMathAliases[] =
    "local math = math\n"
    "abs = math.abs\n"
    "acos = function (x) return math.deg(math.acos(x)) end\n"
    "asin = function (x) return math.deg(math.asin(x)) end\n"
    "atan = function (x) return math.deg(math.atan(x)) end\n"
    "atan2 = function (x,y) return math.deg(math.atan2(x,y)) end\n"
    "ceil = math.ceil\n"
    "cos = function (x) return math.cos(math.rad(x)) end\n"
    "deg = math.deg\n"
    "exp = math.exp\n"
    "floor = math.floor\n"
    "frexp = math.frexp\n"
    "ldexp = math.ldexp\n"
    "log = math.log\n"
    "log10 = math.log10\n"
    "max = math.max\n"
    "min = math.min\n"
    "mod = math.fmod\n"
    "PI = math.pi\n"
    "rad = math.rad\n"
    "random = math.random\n"
    "randomseed = math.randomseed\n"
    "sin = function (x) return math.sin(math.rad(x)) end\n"
    "sqrt = math.sqrt\n"
    "tan = function (x) return math.tan(math.rad(x)) end\n";

void BindFrameScriptLuaState(lua_State* state) {
  g_frame_script_lua_state.store(state, std::memory_order_release);
}

SimpleScript* SimpleScript_Allocate() {
  return new (std::nothrow) SimpleScript();
}

void SimpleScript_Free(SimpleScript* script) {
  if (script == nullptr) {
    return;
  }

  ResetSimpleScript(*script);
  delete script;
}

void SimpleScript_ImportGlobal(SimpleScript* script, const char* name) {
  lua_State* state = GetBoundFrameScriptLuaState();
  if (script == nullptr || state == nullptr || name == nullptr || script->environment_ref == LUA_NOREF) {
    return;
  }

  lua_rawgeti(state, LUA_REGISTRYINDEX, script->environment_ref);
  lua_pushstring(state, name);
  lua_pushvalue(state, LUA_GLOBALSINDEX);
  lua_pushstring(state, name);
  lua_rawget(state, -2);
  lua_remove(state, -2);
  lua_rawset(state, -3);
  lua_pop(state, 1);
}

int SimpleScript_CompileAndRun(SimpleScript* script, const char* source,
                               const char* chunkName) {
  lua_State* state = GetBoundFrameScriptLuaState();
  if (script == nullptr || state == nullptr || source == nullptr || chunkName == nullptr ||
      script->environment_ref == LUA_NOREF) {
    return 0;
  }

  const int base_top = lua_gettop(state);
  PushSimpleScriptErrorHandler(state);
  const int error_handler_index = base_top + 1;

  if (openwow::ui::LoadClientLuaChunk(state, std::string_view(source), chunkName) != 0) {
    (void)lua_pcall(state, 1, 0, 0);
    lua_settop(state, base_top);
    return 0;
  }

  lua_rawgeti(state, LUA_REGISTRYINDEX, script->environment_ref);
  lua_setfenv(state, -2);

  if (lua_pcall(state, 0, 0, error_handler_index) != 0) {
    lua_settop(state, base_top);
    return 0;
  }

  lua_settop(state, base_top);
  return 1;
}

void SimpleScriptFunctionArray_SetCapacity(void* funcTable,
                                           const std::uint32_t newCount) {
  if (funcTable == nullptr) {
    return;
  }

  auto& bindings =
      *static_cast<SimpleScriptFunctionBindingArray*>(funcTable);
  (void)bindings.SetCapacity(newCount);
}

int SimpleScript_Init(SimpleScript* script, unsigned int size) {
  if (script == nullptr) {
    return 0;
  }

  ResetSimpleScript(*script);
  if (size > 16) {
    return 0;
  }

  lua_State* state = GetBoundFrameScriptLuaState();
  if (state == nullptr) {
    return 0;
  }

  lua_createtable(state, 0, 0);
  script->environment_ref = luaL_ref(state, LUA_REGISTRYINDEX);

  script->bucket_count = 1u << size;
  script->hash_mask = script->bucket_count - 1;
  script->buckets.reset(new (std::nothrow) SimpleScriptBucket[script->bucket_count]);
  if (!script->buckets) {
    script->bucket_count = 0;
  }

  return 1;
}

SimpleScript* SimpleScript_Create() {
  std::unique_ptr<SimpleScript, SimpleScriptDeleter> script(SimpleScript_Allocate());
  if (script != nullptr) {
    if (SimpleScript_Init(script.get(), 8u) != 0) {
      SimpleScript_ImportGlobal(script.get(), "math");
      (void)SimpleScript_CompileAndRun(script.get(), kMathAliases, "MathAliases");
    } else {
      script.reset();
    }
  }

  InvokeSimpleScriptLifecycleCallbacks(0);
  return script.release();
}

void SimpleScript_Destroy(SimpleScript* script) {
  InvokeSimpleScriptLifecycleCallbacks(1);
  SimpleScriptDeleter{}(script);
}

int SimpleScript_RegisterLifecycleCallback(
    const SimpleScriptLifecycleCallback callback) {
  if (g_simple_script_callbacks.size() >= kMaxSimpleScriptLifecycleCallbacks) {
    return 0;
  }

  g_simple_script_callbacks.push_back(callback);
  return 1;
}

std::uint32_t SimpleScript_UnregisterLifecycleCallback(
    const SimpleScriptLifecycleCallback callback) {
  std::uint32_t index = 0;
  const auto count =
      static_cast<std::uint32_t>(g_simple_script_callbacks.size());
  while (index < count && g_simple_script_callbacks[index] != callback) {
    ++index;
  }

  if (index == count) {
    return index;
  }

  g_simple_script_callbacks.erase(g_simple_script_callbacks.begin() + index);
  return index;
}

bool SimpleScript_ExecuteFunction(
    SimpleScript* script, const std::uint32_t function_handle,
    const SimpleScriptFunctionInputs& inputs,
    const SimpleScriptFunctionOutputs& outputs) {
  if (script == nullptr) {
    return false;
  }

  auto* const binding =
      ResolveSimpleScriptBindingHandle(*script, function_handle);
  if (binding == nullptr || binding->function_ref == kInvalidFunctionRef ||
      inputs.numeric_values.size() != binding->numeric_input_count ||
      inputs.string_values.size() != binding->string_input_count ||
      outputs.numeric_values.size() != binding->numeric_output_count ||
      outputs.string_values.size() != binding->string_output_count) {
    return false;
  }

  lua_State* const state = GetBoundFrameScriptLuaState();
  if (state == nullptr) {
    return false;
  }

  const int base_top = lua_gettop(state);
  PushSimpleScriptErrorHandler(state);
  const int error_handler_index = base_top + 1;
  lua_rawgeti(state, LUA_REGISTRYINDEX, binding->function_ref);

  for (const double value : inputs.numeric_values) {
    lua_pushnumber(state, value);
  }
  for (const char* value : inputs.string_values) {
    lua_pushstring(state, value);
  }

  if (lua_pcall(state,
                static_cast<int>(inputs.numeric_values.size() +
                                 inputs.string_values.size()),
                static_cast<int>(outputs.numeric_values.size() +
                                 outputs.string_values.size()),
                error_handler_index) != 0) {
    luaL_unref(state, LUA_REGISTRYINDEX, binding->function_ref);
    binding->function_ref = kInvalidFunctionRef;
    lua_settop(state, base_top);
    return false;
  }

  for (std::size_t index = outputs.string_values.size(); index > 0u; --index) {
    const char* const value = lua_tostring(state, -1);
    outputs.string_values[index - 1u] = value != nullptr ? value : "";
    lua_pop(state, 1);
  }

  for (std::size_t index = outputs.numeric_values.size(); index > 0u; --index) {
    outputs.numeric_values[index - 1u] = lua_tonumber(state, -1);
    lua_pop(state, 1);
  }

  lua_settop(state, base_top);
  return true;
}

bool SimpleScript_ExecuteNumericFunction(
    SimpleScript* script, const std::uint32_t function_handle,
    const std::span<const double> numeric_inputs,
    const std::span<double> numeric_outputs) {
  return SimpleScript_ExecuteFunction(
      script, function_handle,
      {.numeric_values = numeric_inputs, .string_values = {}},
      {.numeric_values = numeric_outputs, .string_values = {}});
}

bool SimpleScript_Debug_GetBucketState(
    const SimpleScript* script, const std::uint32_t function_id,
    SimpleScriptBucketDebugState* out_state) {
  if (script == nullptr || out_state == nullptr ||
      !IsSimpleScriptInitialized(*script)) {
    return false;
  }

  const std::uint32_t bucket_index =
      GetSimpleScriptBucketIndex(*script, function_id);
  const auto& bindings = script->buckets[bucket_index].bindings;
  *out_state = {
      .capacity = bindings.capacity,
      .count = bindings.count,
      .grow_quantum = bindings.grow_quantum,
  };
  return true;
}

int SimpleScript_EnsureFunction(
    SimpleScript* script, const std::uint32_t function_id,
    const SimpleScriptFunctionDescriptor& descriptor) {
  if (script == nullptr || descriptor.chunk_name == nullptr ||
      descriptor.source == nullptr) {
    return -1;
  }

  std::uint32_t bucket_index = 0;
  std::uint32_t binding_index = 0;
  if (FindSimpleScriptBinding(*script, function_id, &bucket_index,
                              &binding_index) != nullptr) {
    return static_cast<int>(
        MakeSimpleScriptHandle(bucket_index, binding_index));
  }

  if (!IsSimpleScriptInitialized(*script)) {
    return -1;
  }

  const int function_ref =
      CompileSimpleScriptFunction(*script, descriptor);
  if (function_ref == LUA_NOREF) {
    return -1;
  }

  bucket_index = GetSimpleScriptBucketIndex(*script, function_id);
  auto& bindings = script->buckets[bucket_index].bindings;
  const SimpleScriptFunctionBinding binding{
      .function_id = function_id,
      .function_ref = function_ref,
      .numeric_input_count =
          static_cast<std::uint32_t>(descriptor.numeric_input_names.size()),
      .string_input_count =
          static_cast<std::uint32_t>(descriptor.string_input_names.size()),
      .numeric_output_count =
          static_cast<std::uint32_t>(descriptor.numeric_output_names.size()),
      .string_output_count =
          static_cast<std::uint32_t>(descriptor.string_output_names.size()),
  };
  if (bindings.Append(binding, &binding_index) == nullptr) {
    if (lua_State* const state = GetBoundFrameScriptLuaState();
        state != nullptr) {
      luaL_unref(state, LUA_REGISTRYINDEX, function_ref);
    }
    return -1;
  }

  return static_cast<int>(MakeSimpleScriptHandle(bucket_index, binding_index));
}

int SimpleScript_EnsureNumericFunction(
    SimpleScript* script, const std::uint32_t function_id,
    const SimpleScriptNumericFunctionDescriptor& descriptor) {
  return SimpleScript_EnsureFunction(
      script, function_id,
      {.chunk_name = descriptor.chunk_name,
       .source = descriptor.source,
       .numeric_input_names = descriptor.numeric_input_names,
       .string_input_names = {},
       .numeric_output_names = descriptor.numeric_output_names,
       .string_output_names = {}});
}

void Sound_PlayArmorFoley(void* unit) {
  if (unit == nullptr) {
    return;
  }

  const auto& object = *static_cast<const CGObject_C*>(unit);
  const auto* const objects = object.object_manager();
  if (objects == nullptr) {
    return;
  }

  audio::PlayArmorFoleySound(
      object.sound_runtime(),
      object.GetGuid().GetRawValue(),
      objects->GetActivePlayerGuid().GetRawValue());
}

void* StartupPendingString_Get() {
    return g_startup_pending_string.load(std::memory_order_acquire);
}

void* StartupPendingString_Set(void* value) {
    g_startup_pending_string.store(value, std::memory_order_release);
    return value;
}

int IsConsoleActive() {
    return openwow::core::ida::IsConsoleVisible() ? 1 : 0;
}

void ConsoleClient_GrowLineBuffer(int textLen, void* consoleLine) {
  ConsoleLineLayout& line = RequireConsoleLine(consoleLine);
  std::uint32_t required_length = static_cast<std::uint32_t>(textLen) + line.text_length;
  if (required_length < line.buffer_capacity) {
    return;
  }

  std::uint32_t new_capacity = line.buffer_capacity;
  do {
    new_capacity += 16;
  } while (required_length >= new_capacity);

  std::unique_ptr<char[]> resized_buffer(new char[new_capacity]);
  if (line.text_buffer != nullptr) {
    std::memcpy(resized_buffer.get(), line.text_buffer, static_cast<std::size_t>(line.text_length) + 1u);
    delete[] line.text_buffer;
  } else {
    resized_buffer[0] = '\0';
  }

  line.text_buffer = resized_buffer.release();
  line.buffer_capacity = new_capacity;
}

int ConsoleClient_ReplaceEditableText(void* consoleLine, const char* text) {
  ConsoleLineLayout& line = RequireConsoleLine(consoleLine);
  assert(text != nullptr);
  assert(line.text_buffer != nullptr);

  line.cursor_offset = line.prompt_offset;
  line.text_length = line.prompt_offset;
  line.text_buffer[line.prompt_offset] = '\0';

  const std::size_t replacement_length = std::strlen(text);
  ConsoleClient_GrowLineBuffer(static_cast<int>(replacement_length), consoleLine);

  std::memcpy(line.text_buffer + line.cursor_offset, text, replacement_length + 1u);
  line.cursor_offset += static_cast<std::uint32_t>(replacement_length);
  line.text_length = line.cursor_offset;
  return static_cast<int>(line.cursor_offset);
}

void ConsoleClient_DeleteCharBeforeCursor(void* consoleLine) {
  ConsoleLineLayout& line = RequireConsoleLine(consoleLine);
  assert(line.text_buffer != nullptr);

  if (line.cursor_offset <= line.prompt_offset) {
    return;
  }

  if (line.text_length <= line.cursor_offset) {
    line.text_buffer[line.cursor_offset - 1u] = '\0';
  } else {
    std::memmove(line.text_buffer + line.cursor_offset - 1u,
                 line.text_buffer + line.cursor_offset,
                 static_cast<std::size_t>(line.text_length - line.cursor_offset) + 1u);
  }

  --line.cursor_offset;
  --line.text_length;
}

void ConsoleClient_DeleteCharAtCursor(void* consoleLine) {
  ConsoleLineLayout& line = RequireConsoleLine(consoleLine);
  assert(line.text_buffer != nullptr);

  if (line.cursor_offset > line.text_length) {
    return;
  }

  std::memmove(line.text_buffer + line.cursor_offset,
               line.text_buffer + line.cursor_offset + 1u,
               static_cast<std::size_t>(line.text_length - line.cursor_offset));
  --line.text_length;
}

void ConsoleClient_LoadOlderHistoryEntry(void* consoleLine) {
  const char* history_entry = nullptr;
  if (!openwow::core::ida::CommandHistoryBrowseOlder(&history_entry)) {
    return;
  }

  ConsoleClient_ReplaceEditableText(consoleLine, history_entry);
}

void ConsoleClient_LoadNewerHistoryEntry(void* consoleLine) {
  const char* history_entry = nullptr;
  if (!openwow::core::ida::CommandHistoryBrowseNewer(&history_entry)) {
    return;
  }

  ConsoleClient_ReplaceEditableText(consoleLine, history_entry);
}

void ConsoleClient_DestroyLine(void* consoleLine) {

    (void)consoleLine;
}

void ConsoleClient_ClearAllLines(void* consoleClient) {

    (void)consoleClient;
}

void ConsoleClient_InsertText(void* consoleLine, const char* text) {
  ConsoleLineLayout& line = RequireConsoleLine(consoleLine);
  assert(text != nullptr);
  assert(line.text_buffer != nullptr);

  const std::size_t insert_length = std::strlen(text);
  if (insert_length == 0u) {
    return;
  }

  ConsoleClient_GrowLineBuffer(static_cast<int>(insert_length), consoleLine);

  if (line.cursor_offset >= line.text_length) {

    std::memcpy(line.text_buffer + line.cursor_offset, text, insert_length);
    line.cursor_offset += static_cast<std::uint32_t>(insert_length);
    line.text_buffer[line.cursor_offset] = '\0';
    line.text_length = line.cursor_offset;
  } else {

    const std::uint32_t tail_length = line.text_length - line.cursor_offset;
    std::memmove(line.text_buffer + line.cursor_offset + insert_length,
                 line.text_buffer + line.cursor_offset,
                 static_cast<std::size_t>(tail_length) + 1u);
    std::memcpy(line.text_buffer + line.cursor_offset, text, insert_length);
    line.cursor_offset += static_cast<std::uint32_t>(insert_length);
    line.text_length += static_cast<std::uint32_t>(insert_length);
  }
}

}

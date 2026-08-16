#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/game/lua_cpu_profiler.h"

#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/lua_addon_memory_tracker.h"
#include "openwow/ui/game/lua_table_graph_worklist.h"
#include "openwow/foundation/text/ascii.h"

extern "C" {
#include <lua.hpp>
#include <lstate.h>
}

#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::ui::game {

namespace {

struct FunctionCpuStats {
  std::string addon_key;
  double self_seconds{0.0};
  double total_seconds{0.0};
  std::uint32_t call_count{0};
};

struct ActiveCall {
  std::uint64_t function_id{0};
  std::chrono::steady_clock::time_point start_time{};
  double child_seconds{0.0};
};

struct StateProfiler {
  std::uint64_t next_function_id{1};
  double total_seconds{0.0};
  std::unordered_map<std::string, LuaCpuUsageSample> event_stats;
  std::unordered_map<const void*, std::uint64_t> function_id_by_identity;
  std::unordered_map<std::uint64_t, FunctionCpuStats> function_stats;
  std::unordered_map<std::string, double> addon_cpu_live_milliseconds;
  std::unordered_map<std::string, double> addon_cpu_snapshot_milliseconds;
  std::vector<ActiveCall> active_calls;
  LuaCpuProfilerCounters counters;
  std::uint32_t script_profile_callback{0};
  bool installed{false};
};

lua_State* RootLuaState(lua_State* L) {
  return L != nullptr ? G(L)->mainthread : nullptr;
}

template <typename Callback>
void ForEachLuaThread(lua_State* L, Callback&& callback) {
  if (L == nullptr) {
    return;
  }
  for (GCObject* object = G(L)->rootgc; object != nullptr;
       object = object->gch.next) {
    if (object->gch.tt == LUA_TTHREAD) {
      callback(gco2th(object));
    }
  }
}

StateProfiler* GetStateProfiler(lua_State* L) {
  L = RootLuaState(L);
  return L != nullptr ? static_cast<StateProfiler*>(G(L)->clientcpuprofiler)
                      : nullptr;
}

StateProfiler& EnsureStateProfiler(lua_State* L) {
  L = RootLuaState(L);
  auto* state = GetStateProfiler(L);
  if (state == nullptr) {
    state = new StateProfiler();
    G(L)->clientcpuprofiler = state;
  }
  return *state;
}

std::string CanonicalizeAddonKey(std::string_view addon_name) {
  return openwow::text::ToLowerAscii(std::string(addon_name));
}

void SaturatingAddCalls(std::uint32_t* target, const std::uint32_t count) {
  *target = count > std::numeric_limits<std::uint32_t>::max() - *target
                ? std::numeric_limits<std::uint32_t>::max()
                : *target + count;
}

std::string GetFunctionAddonKey(lua_State* L, const Closure* closure) {
  if (closure == nullptr) {
    return {};
  }

  return GetLuaAddonKeyForOwnerToken(L, closure->c.owner);
}

std::optional<std::uint64_t> GetFunctionId(lua_State* L,
                                           const Closure* closure,
                                           const bool create_if_missing) {
  if (closure == nullptr) {
    return std::nullopt;
  }

  auto* state = GetStateProfiler(L);
  if (state != nullptr) {
    if (const auto existing = state->function_id_by_identity.find(closure);
        existing != state->function_id_by_identity.end()) {
      return existing->second;
    }
  }

  if (!create_if_missing) {
    return std::nullopt;
  }

  state = &EnsureStateProfiler(L);
  if (state->next_function_id == std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  const auto id = state->next_function_id++;
  state->function_id_by_identity.emplace(closure, id);
  const std::string addon_key = GetFunctionAddonKey(L, closure);
  if (!addon_key.empty()) {
    state->function_stats[id].addon_key = addon_key;
    state->addon_cpu_live_milliseconds.try_emplace(addon_key, 0.0);
  }
  return id;
}

const Closure* StackClosureAt(lua_State* L, const int function_index) {
  const int absolute = lua_absindex(L, function_index);
  return lua_isfunction(L, absolute) != 0
             ? static_cast<const Closure*>(lua_topointer(L, absolute))
             : nullptr;
}

std::optional<FunctionCpuStats> LookupFunctionStats(lua_State* L,
                                                    int function_index) {
  const auto function_id =
      GetFunctionId(L, StackClosureAt(L, function_index), false);
  if (!function_id.has_value()) {
    return std::nullopt;
  }

  const auto* state = GetStateProfiler(L);
  if (state == nullptr) {
    return std::nullopt;
  }

  const auto stats_it = state->function_stats.find(*function_id);
  if (stats_it == state->function_stats.end()) {
    return std::nullopt;
  }

  return stats_it->second;
}

void FinalizeActiveCall(StateProfiler& state,
                        const ActiveCall& finished_call,
                        const std::chrono::steady_clock::time_point end_time) {
  const double elapsed_seconds = std::chrono::duration<double>(
      end_time - finished_call.start_time).count();
  const double self_seconds =
      std::max(0.0, elapsed_seconds - finished_call.child_seconds);

  auto& stats = state.function_stats[finished_call.function_id];
  stats.self_seconds += self_seconds;
  stats.total_seconds += elapsed_seconds;
  if (!stats.addon_key.empty()) {
    state.addon_cpu_live_milliseconds[stats.addon_key] +=
        self_seconds * 1000.0;
  }
  if (!state.active_calls.empty()) {
    state.active_calls.back().child_seconds += elapsed_seconds;
  } else {
    state.total_seconds += elapsed_seconds;
  }
}

void FinalizeLeakedCalls(lua_State* L,
                         const std::size_t preserved_depth,
                         const std::chrono::steady_clock::time_point end_time) {
  auto* state = GetStateProfiler(L);
  if (state == nullptr) {
    return;
  }

  while (state->active_calls.size() > preserved_depth) {
    const ActiveCall finished_call = state->active_calls.back();
    state->active_calls.pop_back();
    FinalizeActiveCall(*state, finished_call, end_time);
  }
}

void ResetTrackedLuaCpuCounters(StateProfiler& state) {
  for (auto& [function_id, stats] : state.function_stats) {
    (void)function_id;
    stats.self_seconds = 0.0;
    stats.total_seconds = 0.0;
    stats.call_count = 0;
  }
  state.addon_cpu_live_milliseconds.clear();
  state.addon_cpu_snapshot_milliseconds.clear();
}

bool StartsWith(std::string_view value, std::string_view prefix) {
  return value.substr(0, prefix.size()) == prefix;
}

void AddFunctionUsage(lua_State* L, int function_index,
                      LuaCpuUsageSample* out_usage) {
  if (out_usage == nullptr) {
    return;
  }

  const auto stats = LookupFunctionStats(L, function_index);
  if (!stats.has_value()) {
    return;
  }

  out_usage->total_seconds += stats->total_seconds;
  SaturatingAddCalls(&out_usage->call_count, stats->call_count);
}

void AccumulateFrameHandlers(lua_State* L, int frame_index,
                             LuaCpuUsageSample* out_usage) {
  const int abs_frame_index = lua_absindex(L, frame_index);
  std::unordered_set<std::string> counted_handlers;

  lua_pushnil(L);
  while (lua_next(L, abs_frame_index) != 0) {
    if (lua_type(L, -2) == LUA_TSTRING && lua_isfunction(L, -1) != 0) {
      const char* raw_key = lua_tostring(L, -2);
      const std::string_view key = raw_key != nullptr ? raw_key : "";
      if (StartsWith(key, "On")) {
        counted_handlers.emplace(key);
        AddFunctionUsage(L, -1, out_usage);
      } else if (StartsWith(key, "__ow_script_")) {
        counted_handlers.emplace(std::string(key.substr(12)));
        AddFunctionUsage(L, -1, out_usage);
      }
    }
    lua_pop(L, 1);
  }

  lua_getfield(L, abs_frame_index, "__ow_scripts");
  if (lua_istable(L, -1) != 0) {
    const int scripts_index = lua_absindex(L, -1);
    lua_pushnil(L);
    while (lua_next(L, scripts_index) != 0) {
      if (lua_type(L, -2) == LUA_TSTRING && lua_isfunction(L, -1) != 0) {
        const char* raw_key = lua_tostring(L, -2);
        const std::string key = raw_key != nullptr ? raw_key : "";
        if (!counted_handlers.contains(key)) {
          AddFunctionUsage(L, -1, out_usage);
        }
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
}

void AccumulateFrameCpuUsageIterative(lua_State* L, const int frame_index,
                                      const bool include_children,
                                      LuaCpuUsageSample* out_usage) {
  if (lua_istable(L, frame_index) == 0 || out_usage == nullptr) {
    return;
  }

  if (!include_children) {
    AccumulateFrameHandlers(L, frame_index, out_usage);
    return;
  }

  const int original_top = lua_gettop(L);
  detail::LuaTableGraphWorklist worklist(L);
  (void)worklist.Enqueue(frame_index);

  while (worklist.PushNext()) {
    const int current_index = lua_absindex(L, -1);
    AccumulateFrameHandlers(L, current_index, out_usage);

    lua_getfield(L, current_index, "__ow_children");
    if (lua_istable(L, -1) != 0) {
      const int children_index = lua_absindex(L, -1);
      lua_pushnil(L);
      while (lua_next(L, children_index) != 0) {
        (void)worklist.Enqueue(-1);
        lua_pop(L, 1);
      }
    }
    lua_settop(L, current_index - 1);
  }

  lua_settop(L, original_top);
}

void SetProfilerHookEnabled(lua_State* L, const bool enabled) {
  L = RootLuaState(L);
  if (L == nullptr) {
    return;
  }
  if (enabled) {
    ForEachLuaThread(L, [](lua_State* thread) {
      lua_sethook(thread, OnLuaCpuProfilerHook,
                  LUA_MASKCALL | LUA_MASKRET, 0);
    });
    return;
  }

  FinalizeLeakedCalls(L, 0, std::chrono::steady_clock::now());
  ForEachLuaThread(L, [](lua_State* thread) {
    if (lua_gethook(thread) == OnLuaCpuProfilerHook) {
      lua_sethook(thread, nullptr, 0, 0);
    }
  });
}

}

void InstallLuaCpuProfiler(lua_State* L) {
  L = RootLuaState(L);
  if (L == nullptr) {
    return;
  }

  auto& state = EnsureStateProfiler(L);
  if (state.installed) {
    return;
  }
  state.installed = true;

  auto& cvars = CVarSystem::Instance();
  const std::uint32_t callback = cvars.AddCallback(
      "scriptProfile", [L](const std::string&, const std::string&) {
        SetProfilerHookEnabled(
            L, CVarSystem::Instance().GetCVarBool("scriptProfile"));
      });
  state.script_profile_callback = callback;
  SetProfilerHookEnabled(L, cvars.GetCVarBool("scriptProfile"));
}

void UninstallLuaCpuProfiler(lua_State* L) {
  L = RootLuaState(L);
  if (L == nullptr) {
    return;
  }

  SetProfilerHookEnabled(L, false);
  auto* state = GetStateProfiler(L);
  const std::uint32_t callback =
      state != nullptr ? state->script_profile_callback : 0;
  if (callback != 0) {
    CVarSystem::Instance().RemoveCallback("scriptProfile", callback);
  }
  delete state;
  G(L)->clientcpuprofiler = nullptr;
}

bool IsLuaCpuProfilerEnabled(lua_State* L) {
  if (L == nullptr || lua_gethook(L) != OnLuaCpuProfilerHook) {
    return false;
  }
  constexpr int kRequiredMask = LUA_MASKCALL | LUA_MASKRET;
  return (lua_gethookmask(L) & kRequiredMask) == kRequiredMask;
}

int ProfiledPCall(lua_State* L, int nargs, int nresults, int errfunc) {
  if (!IsLuaCpuProfilerEnabled(L)) {
    return lua_pcall(L, nargs, nresults, errfunc);
  }

  auto& state = EnsureStateProfiler(L);
  const std::size_t stack_depth = state.active_calls.size();
  const int status = lua_pcall(L, nargs, nresults, errfunc);
  FinalizeLeakedCalls(L, stack_depth, std::chrono::steady_clock::now());
  return status;
}

double GetTotalLuaCpuUsageMilliseconds(lua_State* L) {
  const auto* state = GetStateProfiler(L);
  return state != nullptr ? state->total_seconds * 1000.0 : 0.0;
}

LuaCpuUsageSample GetEventCpuUsage(lua_State* L) {
  LuaCpuUsageSample usage{};

  const auto* state = GetStateProfiler(L);
  if (state == nullptr) {
    return usage;
  }

  for (const auto& [event_name, sample] : state->event_stats) {
    (void)event_name;
    usage.total_seconds += sample.total_seconds;
    SaturatingAddCalls(&usage.call_count, sample.call_count);
  }
  return usage;
}

void ResetLuaCpuUsage(lua_State* L) {
  auto* state = GetStateProfiler(L);
  if (state == nullptr) {
    return;
  }

  state->total_seconds = 0.0;
  state->event_stats.clear();

  if (!IsLuaCpuProfilerEnabled(L)) {

    return;
  }

  ResetTrackedLuaCpuCounters(*state);
}

void UpdateLuaAddonCpuUsage(lua_State* L) {
  if (!IsLuaCpuProfilerEnabled(L)) {
    return;
  }

  auto* state = GetStateProfiler(L);
  if (state == nullptr) {
    return;
  }

  state->addon_cpu_snapshot_milliseconds =
      state->addon_cpu_live_milliseconds;
  state->counters.last_snapshot_addon_count =
      state->addon_cpu_live_milliseconds.size();
}

double GetLuaAddonCpuUsageMilliseconds(lua_State* L, std::string_view addon_name) {
  const auto* state = GetStateProfiler(L);
  if (state == nullptr) {
    return 0.0;
  }

  const auto snapshot_it = state->addon_cpu_snapshot_milliseconds.find(
      CanonicalizeAddonKey(addon_name));
  if (snapshot_it == state->addon_cpu_snapshot_milliseconds.end()) {
    return 0.0;
  }

  return snapshot_it->second;
}

LuaCpuUsageSample GetFrameCpuUsage(lua_State* L, int frame_index,
                                   bool include_children) {
  LuaCpuUsageSample usage{};
  AccumulateFrameCpuUsageIterative(L, frame_index, include_children, &usage);
  return usage;
}

LuaFunctionCpuUsage GetFunctionCpuUsage(lua_State* L, int function_index) {
  LuaFunctionCpuUsage usage{};
  if (!IsLuaCpuProfilerEnabled(L)) {
    return usage;
  }

  const auto stats = LookupFunctionStats(L, function_index);
  if (!stats.has_value()) {
    return usage;
  }

  usage.self_seconds = stats->self_seconds;
  usage.total_seconds = stats->total_seconds;
  usage.call_count = stats->call_count;
  return usage;
}

void RecordLuaEventCpuUsage(lua_State* L, std::string_view event_name,
                            const double elapsed_seconds) {
  if (L == nullptr || !IsLuaCpuProfilerEnabled(L)) {
    return;
  }

  auto& state = EnsureStateProfiler(L);
  auto& sample = state.event_stats[std::string(event_name)];
  sample.total_seconds += std::max(0.0, elapsed_seconds);
  if (sample.call_count != std::numeric_limits<std::uint32_t>::max()) {
    sample.call_count += 1;
  }
}

void OnLuaCpuProfilerHook(lua_State* L, lua_Debug* ar) {
  if (L == nullptr || ar == nullptr) {
    return;
  }
  auto* state = GetStateProfiler(L);
  if (state == nullptr) {
    return;
  }
  if (state->counters.hook_events !=
      std::numeric_limits<std::uint64_t>::max()) {
    state->counters.hook_events += 1;
  }

  switch (ar->event) {
    case LUA_HOOKCALL: {
      if (L->ci == nullptr || !ttisfunction(L->ci->func)) {
        return;
      }

      const auto function_id = GetFunctionId(L, clvalue(L->ci->func), true);
      if (!function_id.has_value()) {
        return;
      }

      auto& stats = state->function_stats[*function_id];
      if (stats.call_count != std::numeric_limits<std::uint32_t>::max()) {
        stats.call_count += 1;
      }
      state->active_calls.push_back(ActiveCall{
          .function_id = *function_id,
          .start_time = std::chrono::steady_clock::now(),
      });
      return;
    }
    case LUA_HOOKRET:
#ifdef LUA_HOOKTAILRET
    case LUA_HOOKTAILRET:
#endif
    {
      if (state->active_calls.empty()) {
        return;
      }

      const ActiveCall finished_call = state->active_calls.back();
      state->active_calls.pop_back();
      FinalizeActiveCall(*state, finished_call,
                         std::chrono::steady_clock::now());
      return;
    }
    default:
      return;
  }
}

void ForgetLuaCpuProfilerObject(lua_State* L, const void* object) {
  auto* state = GetStateProfiler(L);
  if (state == nullptr || object == nullptr) {
    return;
  }
  const auto identity = state->function_id_by_identity.find(object);
  if (identity == state->function_id_by_identity.end()) {
    return;
  }
  const auto stats = state->function_stats.find(identity->second);
  if (stats != state->function_stats.end()) {
    if (!stats->second.addon_key.empty()) {
      const auto live =
          state->addon_cpu_live_milliseconds.find(stats->second.addon_key);
      if (live != state->addon_cpu_live_milliseconds.end()) {
        live->second = std::max(
            0.0, live->second - stats->second.self_seconds * 1000.0);
      }
    }
    state->function_stats.erase(stats);
  }
  state->function_id_by_identity.erase(identity);
}

LuaCpuProfilerCounters GetLuaCpuProfilerCounters(lua_State* L) {
  const auto* state = GetStateProfiler(L);
  return state != nullptr ? state->counters : LuaCpuProfilerCounters{};
}

}

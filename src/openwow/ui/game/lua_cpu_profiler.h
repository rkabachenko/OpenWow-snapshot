#pragma once

extern "C" {
#include <lua.hpp>
}

#include <cstdint>
#include <string_view>

namespace openwow::ui::game {

struct LuaCpuUsageSample {
  double total_seconds{0.0};
  std::uint32_t call_count{0};
};

struct LuaFunctionCpuUsage {
  double self_seconds{0.0};
  double total_seconds{0.0};
  std::uint32_t call_count{0};
};

struct LuaCpuProfilerCounters {
  std::uint64_t hook_events{0};
  std::uint64_t last_snapshot_addon_count{0};
};

int ProfiledPCall(lua_State* L, int nargs, int nresults, int errfunc);
void InstallLuaCpuProfiler(lua_State* L);
void UninstallLuaCpuProfiler(lua_State* L);
bool IsLuaCpuProfilerEnabled(lua_State* L);
double GetTotalLuaCpuUsageMilliseconds(lua_State* L);
LuaCpuUsageSample GetEventCpuUsage(lua_State* L);
void ResetLuaCpuUsage(lua_State* L);
void UpdateLuaAddonCpuUsage(lua_State* L);
double GetLuaAddonCpuUsageMilliseconds(lua_State* L, std::string_view addon_name);
LuaCpuUsageSample GetFrameCpuUsage(lua_State* L, int frame_index,
                                   bool include_children);
LuaFunctionCpuUsage GetFunctionCpuUsage(lua_State* L, int function_index);
void RecordLuaEventCpuUsage(lua_State* L, std::string_view event_name,
                            double elapsed_seconds);
void OnLuaCpuProfilerHook(lua_State* L, lua_Debug* ar);
void ForgetLuaCpuProfilerObject(lua_State* L, const void* object);
[[nodiscard]] LuaCpuProfilerCounters GetLuaCpuProfilerCounters(lua_State* L);

}

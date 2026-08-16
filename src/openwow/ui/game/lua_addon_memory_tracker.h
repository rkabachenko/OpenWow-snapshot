#pragma once

extern "C" {
#include <lua.hpp>
}

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::ui::game {

namespace detail {

struct LuaAddonMemoryObjectLayout {
  std::uint8_t type_tag{0};
  std::size_t string_length{0};
  std::size_t table_array_size{0};
  std::uint8_t table_hash_log2{0};
  bool closure_is_c{false};
  std::uint8_t closure_upvalue_count{0};
  bool closure_has_environment_sidecar{false};
  std::size_t userdata_length{0};
  std::size_t thread_stack_size{0};
  std::size_t thread_callinfo_capacity{0};
  std::size_t proto_sizek{0};
  std::size_t proto_sizecode{0};
  std::size_t proto_sizep{0};
  std::size_t proto_sizelineinfo{0};
  std::size_t proto_sizelocvars{0};
  std::size_t proto_sizeupvalues{0};
};

[[nodiscard]] std::uint64_t ComputeLuaAddonMemoryObjectBytes(
    const LuaAddonMemoryObjectLayout &object);

}

void InstallLuaAddonMemoryTracker(lua_State *L);
void UninstallLuaAddonMemoryTracker(lua_State *L);
bool HasLuaAddonMemoryTracker(lua_State *L);
[[nodiscard]] std::uint32_t GetLuaAllocationOwnerToken(
    lua_State *L, const void *allocation);
[[nodiscard]] std::uint32_t GetLuaObjectMemoryOwnerToken(
    lua_State *L, int stack_index);
[[nodiscard]] std::string GetLuaAddonKeyForOwnerToken(
    lua_State *L, std::uint32_t owner);
void MarkLuaClosureEnvironmentSidecar(lua_State *L, const void *function_identity);

struct LuaAddonMemoryTrackerCounters {
  std::uint64_t object_delta_events{0};
  std::uint64_t last_snapshot_owner_count{0};
};

[[nodiscard]] LuaAddonMemoryTrackerCounters GetLuaAddonMemoryTrackerCounters(
    lua_State *L);

class ScopedLuaAddonMemoryOwner {
 public:
  ScopedLuaAddonMemoryOwner(lua_State *L, std::string_view addon_name);
  ~ScopedLuaAddonMemoryOwner();

  ScopedLuaAddonMemoryOwner(const ScopedLuaAddonMemoryOwner &) = delete;
  ScopedLuaAddonMemoryOwner &operator=(const ScopedLuaAddonMemoryOwner &) = delete;

 private:
  lua_State *state_{nullptr};
  std::uint32_t previous_owner_{0};
  bool active_{false};
};

void RefreshLuaAddonMemoryUsage(lua_State *L);
std::uint64_t GetLuaAddonMemoryUsageBytes(lua_State *L, std::string_view addon_name);
std::string ExtractAddonNameFromLuaSource(std::string_view source);
std::string ExtractAddonDisplayNameFromLuaSource(std::string_view source);

}

#include "openwow/ui/game/lua_addon_memory_tracker.h"

#include "openwow/ui/game/lua_cpu_profiler.h"
#include "openwow/foundation/text/ascii.h"

extern "C" {
#include <lobject.h>
#include <lstate.h>
}

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace openwow::ui::game {
namespace {

struct LuaAddonMemoryTracker {
  std::uint32_t next_owner{1};
  std::unordered_map<std::string, std::uint32_t> owner_by_addon;
  std::unordered_map<std::uint32_t, std::string> addon_by_owner;
  std::unordered_map<std::uint32_t, std::uint64_t> live_bytes_by_owner;
  std::unordered_map<std::string, std::uint64_t> snapshot_bytes_by_owner;
  std::unordered_set<const void*> lua_closures_with_environment_sidecar;
  LuaAddonMemoryTrackerCounters counters;
};

void OnLuaObjectMemoryChanged(lua_State* L, const void* object,
                              unsigned int owner, std::size_t old_size,
                              std::size_t new_size, void* userdata);

std::string CanonicalizeAddonKey(const std::string_view addon_name) {
  return openwow::text::ToLowerAscii(std::string(addon_name));
}

std::string ExtractAddonSegmentFromLuaSource(const std::string_view source,
                                             const bool preserve_case) {
  if (source.empty()) {
    return {};
  }

  std::string normalized(source);
  if (normalized.front() == '@') {
    normalized.erase(normalized.begin());
  }
  std::replace(normalized.begin(), normalized.end(), '\\', '/');

  const std::string lower = openwow::text::ToLowerAscii(normalized);
  constexpr std::string_view kAddonPrefix = "/interface/addons/";
  constexpr std::string_view kAddonPrefixNoSlash = "interface/addons/";
  std::size_t prefix = lower.find(kAddonPrefix);
  std::size_t prefix_size = kAddonPrefix.size();
  if (prefix == std::string::npos) {
    prefix = lower.find(kAddonPrefixNoSlash);
    prefix_size = kAddonPrefixNoSlash.size();
  }
  if (prefix != std::string::npos) {
    const std::size_t begin = prefix + prefix_size;
    const std::size_t end = lower.find('/', begin);
    if (end != std::string::npos && end > begin) {
      const std::string_view name =
          std::string_view(normalized).substr(begin, end - begin);
      return preserve_case ? std::string(name) : CanonicalizeAddonKey(name);
    }
  }

  constexpr std::string_view kSavedVariablesToken = "/savedvariables/";
  const std::size_t saved_variables = lower.find(kSavedVariablesToken);
  if (saved_variables == std::string::npos) {
    return {};
  }

  const std::size_t begin = saved_variables + kSavedVariablesToken.size();
  const std::size_t extension = lower.find(".lua", begin);
  const std::size_t nested_slash = lower.find('/', begin);
  if (extension == std::string::npos || extension <= begin ||
      (nested_slash != std::string::npos && nested_slash < extension)) {
    return {};
  }

  const std::string_view name =
      std::string_view(normalized).substr(begin, extension - begin);
  return preserve_case ? std::string(name) : CanonicalizeAddonKey(name);
}

LuaAddonMemoryTracker* GetTracker(lua_State* L) {
  if (L == nullptr) {
    return nullptr;
  }
  return G(L)->objectmemoryhook == OnLuaObjectMemoryChanged
             ? static_cast<LuaAddonMemoryTracker*>(G(L)->objectmemoryhookud)
             : nullptr;
}

std::uint32_t GetAllocationOwner(const void* allocation) {
  if (allocation == nullptr) {
    return 0;
  }
  return static_cast<const GCObject*>(allocation)->gch.owner;
}

constexpr std::uint64_t kLuaClosureEnvironmentSidecarBytes = 32;

void SaturatingAdd(std::uint64_t* value, const std::uint64_t increment) {
  if (increment > std::numeric_limits<std::uint64_t>::max() - *value) {
    *value = std::numeric_limits<std::uint64_t>::max();
    return;
  }
  *value += increment;
}

void SaturatingSubtract(std::uint64_t* value, const std::uint64_t decrement) {
  *value = decrement >= *value ? 0 : *value - decrement;
}

void OnLuaObjectMemoryChanged(lua_State* L, const void* object,
                              const unsigned int owner,
                              const std::size_t old_size,
                              const std::size_t new_size, void* userdata) {
  auto* tracker = static_cast<LuaAddonMemoryTracker*>(userdata);
  if (new_size == 0 && object != nullptr &&
      static_cast<const GCObject*>(object)->gch.tt == LUA_TFUNCTION) {
    ForgetLuaCpuProfilerObject(L, object);
  }
  if (tracker == nullptr || owner == 0) {
    return;
  }

  const auto live = tracker->live_bytes_by_owner.find(owner);
  if (live == tracker->live_bytes_by_owner.end()) {
    return;
  }
  if (new_size >= old_size) {
    SaturatingAdd(&live->second,
                  static_cast<std::uint64_t>(new_size - old_size));
  } else {
    SaturatingSubtract(&live->second,
                       static_cast<std::uint64_t>(old_size - new_size));
  }

  if (new_size == 0 && object != nullptr &&
      tracker->lua_closures_with_environment_sidecar.erase(object) != 0) {
    SaturatingSubtract(&live->second, kLuaClosureEnvironmentSidecarBytes);
  }
  SaturatingAdd(&tracker->counters.object_delta_events, 1);
}

const TValue* StackValueAt(lua_State* L, const int index) {
  if (L == nullptr || index == 0 || index <= LUA_REGISTRYINDEX) {
    return nullptr;
  }
  if (index > 0) {
    const TValue* value = L->base + (index - 1);
    return value < L->top ? value : nullptr;
  }
  return -index <= L->top - L->base ? L->top + index : nullptr;
}

const GCObject* CollectableStackObjectAt(lua_State* L, const int index) {
  const TValue* value = StackValueAt(L, index);
  return value != nullptr && iscollectable(value) ? gcvalue(value) : nullptr;
}

std::uint32_t ResolveOwner(LuaAddonMemoryTracker& tracker,
                           const std::string_view addon_name) {
  const std::string key = CanonicalizeAddonKey(addon_name);
  if (key.empty()) {
    return 0;
  }

  if (const auto existing = tracker.owner_by_addon.find(key);
      existing != tracker.owner_by_addon.end()) {
    return existing->second;
  }

  if (tracker.next_owner == 0) {
    return 0;
  }
  const std::uint32_t owner = tracker.next_owner++;
  tracker.owner_by_addon.emplace(key, owner);
  tracker.addon_by_owner.emplace(owner, key);
  tracker.live_bytes_by_owner.emplace(owner, 0);
  return owner;
}

void RefreshSnapshotFromLuaVm(LuaAddonMemoryTracker& tracker, lua_State* L) {
  (void)L;

  tracker.snapshot_bytes_by_owner.clear();
  tracker.counters.last_snapshot_owner_count = 0;
  for (const auto& [owner, addon_key] : tracker.addon_by_owner) {
    const auto live = tracker.live_bytes_by_owner.find(owner);
    tracker.snapshot_bytes_by_owner.emplace(
        addon_key, live != tracker.live_bytes_by_owner.end() ? live->second : 0);
    SaturatingAdd(&tracker.counters.last_snapshot_owner_count, 1);
  }
}

void OnLuaStateClosing(lua_State* L, void* userdata) {
  if (GetTracker(L) == userdata) {
    UninstallLuaAddonMemoryTracker(L);
  }
}

}

namespace detail {

std::uint64_t ComputeLuaAddonMemoryObjectBytes(
    const LuaAddonMemoryObjectLayout& object) {
  const auto add = [](const std::uint64_t lhs, const std::uint64_t rhs) {
    return rhs > std::numeric_limits<std::uint64_t>::max() - lhs
               ? std::numeric_limits<std::uint64_t>::max()
               : lhs + rhs;
  };
  const auto multiply = [](const std::uint64_t lhs, const std::uint64_t rhs) {
    return lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs
               ? std::numeric_limits<std::uint64_t>::max()
               : lhs * rhs;
  };
  switch (object.type_tag) {
    case LUA_TSTRING:
      return add(static_cast<std::uint64_t>(object.string_length), 21);
    case LUA_TTABLE: {
      const std::uint64_t hash_capacity =
          object.table_hash_log2 < 64
              ? (std::uint64_t{1} << object.table_hash_log2)
              : std::numeric_limits<std::uint64_t>::max();
      return add(add(multiply(16, object.table_array_size),
                     multiply(40, hash_capacity)), 36);
    }
    case LUA_TFUNCTION:
      if (object.closure_is_c) {
        return add(multiply(16, object.closure_upvalue_count), 28);
      }
      return add(add(28, multiply(4, object.closure_upvalue_count)),
                 object.closure_has_environment_sidecar ? 32 : 0);
    case LUA_TUSERDATA:
      return add(static_cast<std::uint64_t>(object.userdata_length), 24);
    case LUA_TTHREAD:
      return add(add(multiply(16, object.thread_stack_size), 124),
                 multiply(24, object.thread_callinfo_capacity));
    case LUA_TPROTO: {
      std::uint64_t inner = add(object.proto_sizek, object.proto_sizep);
      inner = add(inner, object.proto_sizelineinfo);
      inner = add(inner, object.proto_sizelocvars);
      inner = add(inner, multiply(3, object.proto_sizeupvalues));
      inner = add(inner, multiply(4, object.proto_sizecode));
      return multiply(4, add(inner, 20));
    }
    case LUA_TUPVAL:
      return 32u;
    default:
      return 0u;
  }
}

}

std::string ExtractAddonNameFromLuaSource(const std::string_view source) {
  return ExtractAddonSegmentFromLuaSource(source, false);
}

std::string ExtractAddonDisplayNameFromLuaSource(const std::string_view source) {
  return ExtractAddonSegmentFromLuaSource(source, true);
}

void InstallLuaAddonMemoryTracker(lua_State* L) {
  if (L == nullptr || HasLuaAddonMemoryTracker(L)) {
    return;
  }

  auto* tracker = new LuaAddonMemoryTracker();
  lua_setobjectmemoryhook(L, OnLuaObjectMemoryChanged, tracker);
  lua_setstateclosehook(L, OnLuaStateClosing, tracker);
  InstallLuaCpuProfiler(L);
}

void UninstallLuaAddonMemoryTracker(lua_State* L) {
  auto* tracker = GetTracker(L);
  if (tracker == nullptr) {
    return;
  }

  UninstallLuaCpuProfiler(L);
  lua_setstateclosehook(L, nullptr, nullptr);
  lua_setobjectmemoryhook(L, nullptr, nullptr);
  static_cast<void>(lua_setallocationowner(L, 0));
  delete tracker;
}

bool HasLuaAddonMemoryTracker(lua_State* L) {
  return GetTracker(L) != nullptr;
}

std::uint32_t GetLuaAllocationOwnerToken(lua_State* L, const void* allocation) {
  return GetTracker(L) != nullptr ? GetAllocationOwner(allocation) : 0;
}

std::uint32_t GetLuaObjectMemoryOwnerToken(lua_State* L, const int stack_index) {
  return GetLuaAllocationOwnerToken(L, CollectableStackObjectAt(L, stack_index));
}

std::string GetLuaAddonKeyForOwnerToken(lua_State* L,
                                        const std::uint32_t owner) {
  const auto* tracker = GetTracker(L);
  if (tracker == nullptr || owner == 0) {
    return {};
  }
  const auto addon = tracker->addon_by_owner.find(owner);
  return addon != tracker->addon_by_owner.end() ? addon->second : std::string{};
}

void MarkLuaClosureEnvironmentSidecar(lua_State* L, const void* function_identity) {
  auto* tracker = GetTracker(L);
  if (tracker == nullptr || function_identity == nullptr) {
    return;
  }

  const std::uint32_t owner = GetAllocationOwner(function_identity);
  const auto live = tracker->live_bytes_by_owner.find(owner);
  if (live == tracker->live_bytes_by_owner.end() ||
      !tracker->lua_closures_with_environment_sidecar
           .insert(function_identity).second) {
    return;
  }
  SaturatingAdd(&live->second, kLuaClosureEnvironmentSidecarBytes);
}

LuaAddonMemoryTrackerCounters GetLuaAddonMemoryTrackerCounters(lua_State* L) {
  const auto* tracker = GetTracker(L);
  return tracker != nullptr ? tracker->counters
                            : LuaAddonMemoryTrackerCounters{};
}

ScopedLuaAddonMemoryOwner::ScopedLuaAddonMemoryOwner(
    lua_State* L, const std::string_view addon_name)
    : state_(L) {
  auto* tracker = GetTracker(L);
  if (tracker == nullptr) {
    return;
  }

  const std::uint32_t owner = ResolveOwner(*tracker, addon_name);
  if (owner == 0) {
    return;
  }
  previous_owner_ = lua_setallocationowner(L, owner);
  active_ = true;
}

ScopedLuaAddonMemoryOwner::~ScopedLuaAddonMemoryOwner() {
  if (active_ && HasLuaAddonMemoryTracker(state_)) {
    static_cast<void>(lua_setallocationowner(state_, previous_owner_));
  }
}

void RefreshLuaAddonMemoryUsage(lua_State* L) {
  if (auto* tracker = GetTracker(L); tracker != nullptr) {
    RefreshSnapshotFromLuaVm(*tracker, L);
  }
}

std::uint64_t GetLuaAddonMemoryUsageBytes(lua_State* L,
                                          const std::string_view addon_name) {
  const auto* tracker = GetTracker(L);
  if (tracker == nullptr) {
    return 0;
  }

  const auto usage =
      tracker->snapshot_bytes_by_owner.find(CanonicalizeAddonKey(addon_name));
  if (usage == tracker->snapshot_bytes_by_owner.end()) {
    return 0;
  }
  return usage->second;
}

}

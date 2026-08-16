#pragma once

#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/runtime/lua_interned_field_key.h"

extern "C" {
#include <lua.hpp>
}

#include <cstddef>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openwow::ui::game::detail {

inline bool LuaWidgetLocallyVisible(lua_State* state, const int index) {
  const int absolute_index = lua_absindex(state, index);
  openwow::ui::game::runtime::GetInternedLuaField(state, absolute_index, "__ow_draw_layer_enabled");
  const bool layer_enabled =
      lua_isboolean(state, -1) == 0 || lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  if (!layer_enabled) {
    return false;
  }

  openwow::ui::game::runtime::GetInternedLuaField(state, absolute_index, "__ow_visible");
  const bool shown = lua_isboolean(state, -1) == 0 || lua_toboolean(state, -1) != 0;
  lua_pop(state, 1);
  return shown;
}

inline bool ComputeLuaWidgetEffectiveVisibility(lua_State* state, int index) {
  const int original_top = lua_gettop(state);
  lua_pushvalue(state, lua_absindex(state, index));

  constexpr std::size_t kMaximumParentDepth = 1024;
  std::size_t depth = 0;
  while (lua_istable(state, -1) != 0 && depth++ < kMaximumParentDepth) {
    if (!LuaWidgetLocallyVisible(state, -1)) {
      lua_settop(state, original_top);
      return false;
    }

    openwow::ui::game::runtime::GetInternedLuaField(state, -1, "__ow_parent");
    lua_remove(state, -2);
  }

  const bool reached_root = lua_istable(state, -1) == 0;
  lua_settop(state, original_top);
  return reached_root;
}

inline bool IsLuaWidgetEffectivelyVisibleIterative(lua_State* state, int index) {
  index = lua_absindex(state, index);
  openwow::ui::game::runtime::GetInternedLuaField(state, index, "__ow_effective_visible");
  if (lua_isboolean(state, -1) != 0) {
    const bool visible = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return visible;
  }
  lua_pop(state, 1);
  return ComputeLuaWidgetEffectiveVisibility(state, index);
}

inline void StoreLuaEffectiveVisibility(lua_State* state, const int index,
                                        const bool visible) {
  const int absolute_index = lua_absindex(state, index);
  lua_pushboolean(state, visible ? 1 : 0);
  lua_setfield(state, absolute_index, "__ow_effective_visible");
}

inline lua_Integer ReadLuaVisibilityGeneration(lua_State* state, const int index) {
  openwow::ui::game::runtime::GetInternedLuaField(
      state, lua_absindex(state, index), "__ow_visibility_generation");
  const lua_Integer generation =
      lua_isnumber(state, -1) != 0 ? lua_tointeger(state, -1) : 0;
  lua_pop(state, 1);
  return generation;
}

class LuaVisibilitySnapshot {
 public:
  LuaVisibilitySnapshot() = default;
  LuaVisibilitySnapshot(const LuaVisibilitySnapshot&) = delete;
  LuaVisibilitySnapshot& operator=(const LuaVisibilitySnapshot&) = delete;

  LuaVisibilitySnapshot(LuaVisibilitySnapshot&& other) noexcept
      : state_(std::exchange(other.state_, nullptr)),
        entries_(std::move(other.entries_)),
        steps_(std::move(other.steps_)) {}

  LuaVisibilitySnapshot& operator=(LuaVisibilitySnapshot&& other) noexcept {
    if (this != &other) {
      Reset();
      state_ = std::exchange(other.state_, nullptr);
      entries_ = std::move(other.entries_);
      steps_ = std::move(other.steps_);
    }
    return *this;
  }

  ~LuaVisibilitySnapshot() { Reset(); }

  [[nodiscard]] bool RootWasVisible() const noexcept {
    return !entries_.empty() && entries_.front().was_visible;
  }

 private:
  struct Entry {
    int registry_ref{LUA_NOREF};
    bool was_visible{false};
    lua_Integer generation{0};
    std::size_t parent_entry{std::numeric_limits<std::size_t>::max()};
  };

  struct Step {
    std::size_t entry_index{0};
    bool entering{false};
  };

  void Reset() noexcept {
    if (state_ != nullptr) {
      for (const Entry& entry : entries_) {
        if (entry.registry_ref != LUA_NOREF) {
          luaL_unref(state_, LUA_REGISTRYINDEX, entry.registry_ref);
        }
      }
    }
    state_ = nullptr;
    entries_.clear();
    steps_.clear();
  }

  lua_State* state_{nullptr};
  std::vector<Entry> entries_;
  std::vector<Step> steps_;

  friend LuaVisibilitySnapshot CaptureLuaVisibilitySubtree(lua_State*, int);
  friend bool LuaVisibilitySnapshotParentMatches(
      lua_State*, const LuaVisibilitySnapshot&, std::size_t, int);
  friend void DispatchLuaVisibilityTransitionSteps(LuaVisibilitySnapshot*, bool);
  friend void DispatchLuaVisibilityTransitions(LuaVisibilitySnapshot*);
  friend void DispatchForcedLuaVisibilityTransition(LuaVisibilitySnapshot*, bool);
  friend void PrimeLuaEffectiveVisibilityOverrides(LuaVisibilitySnapshot*);
  friend void ClearLuaEffectiveVisibilityOverrides(LuaVisibilitySnapshot*);
};

inline LuaVisibilitySnapshot CaptureLuaVisibilitySubtree(lua_State* state,
                                                         const int root_index) {
  LuaVisibilitySnapshot snapshot;
  if (state == nullptr || lua_istable(state, root_index) == 0) {
    return snapshot;
  }

  snapshot.state_ = state;
  const int original_top = lua_gettop(state);
  const int absolute_root = lua_absindex(state, root_index);

  lua_pushvalue(state, absolute_root);
  const int root_ref = luaL_ref(state, LUA_REGISTRYINDEX);
  snapshot.entries_.push_back(
      {root_ref, IsLuaWidgetEffectivelyVisibleIterative(state, absolute_root),
       ReadLuaVisibilityGeneration(state, absolute_root),
       std::numeric_limits<std::size_t>::max()});

  struct WorkItem {
    std::size_t entry_index{0};
    bool expanded{false};
  };
  std::vector<WorkItem> work;
  snapshot.entries_.reserve(32);
  snapshot.steps_.reserve(64);
  work.reserve(64);
  work.push_back({0, false});
  std::unordered_set<const void*> visited;
  visited.reserve(32);
  visited.insert(lua_topointer(state, absolute_root));

  while (!work.empty()) {
    const WorkItem item = work.back();
    work.pop_back();
    if (item.expanded) {
      snapshot.steps_.push_back({item.entry_index, false});
      continue;
    }

    snapshot.steps_.push_back({item.entry_index, true});
    work.push_back({item.entry_index, true});
    lua_rawgeti(state, LUA_REGISTRYINDEX,
                snapshot.entries_[item.entry_index].registry_ref);
    openwow::ui::game::runtime::GetInternedLuaField(state, -1, "__ow_children");
    if (lua_istable(state, -1) != 0) {
      const lua_Integer child_count = luaL_len(state, -1);
      for (lua_Integer child = child_count; child >= 1; --child) {
        lua_geti(state, -1, child);
        const void* identity =
            lua_istable(state, -1) != 0 ? lua_topointer(state, -1) : nullptr;
        if (identity != nullptr && visited.insert(identity).second) {
          const bool child_was_visible =
              IsLuaWidgetEffectivelyVisibleIterative(state, -1);
          lua_pushvalue(state, -1);
          const int child_ref = luaL_ref(state, LUA_REGISTRYINDEX);
          const std::size_t child_entry = snapshot.entries_.size();
          snapshot.entries_.push_back(
              {child_ref, child_was_visible,
               ReadLuaVisibilityGeneration(state, -1), item.entry_index});
          work.push_back({child_entry, false});
        }
        lua_pop(state, 1);
      }
    }
    lua_settop(state, original_top);
  }

  lua_settop(state, original_top);
  return snapshot;
}

inline void PrimeLuaEffectiveVisibilityOverrides(
    LuaVisibilitySnapshot* snapshot) {
  if (snapshot == nullptr || snapshot->state_ == nullptr) {
    return;
  }

  lua_State* state = snapshot->state_;
  for (const auto& entry : snapshot->entries_) {
    const int original_top = lua_gettop(state);
    lua_rawgeti(state, LUA_REGISTRYINDEX, entry.registry_ref);
    if (lua_istable(state, -1) != 0) {
      StoreLuaEffectiveVisibility(state, -1, entry.was_visible);
    }
    lua_settop(state, original_top);
  }
}

inline bool LuaVisibilitySnapshotParentMatches(
    lua_State* state, const LuaVisibilitySnapshot& snapshot,
    const std::size_t entry_index, const int frame_index) {
  const auto parent_entry = snapshot.entries_[entry_index].parent_entry;
  if (parent_entry == std::numeric_limits<std::size_t>::max()) {
    return true;
  }

  openwow::ui::game::runtime::GetInternedLuaField(state, frame_index, "__ow_parent");
  lua_rawgeti(state, LUA_REGISTRYINDEX,
              snapshot.entries_[parent_entry].registry_ref);
  const bool matches = lua_istable(state, -2) != 0 &&
                       lua_istable(state, -1) != 0 &&
                       lua_rawequal(state, -2, -1) != 0;
  lua_pop(state, 2);
  return matches;
}

inline void DispatchLuaVisibilityTransitionSteps(
    LuaVisibilitySnapshot* snapshot, const bool force_hidden) {
  if (snapshot == nullptr || snapshot->state_ == nullptr) {
    return;
  }

  PrimeLuaEffectiveVisibilityOverrides(snapshot);
  lua_State* state = snapshot->state_;
  struct RuntimeEntry {
    lua_Integer generation{0};
    bool traversed{false};
    bool target_visible{false};
  };
  std::vector<RuntimeEntry> runtime(snapshot->entries_.size());

  for (const auto& step : snapshot->steps_) {
    const std::size_t entry_index = step.entry_index;
    const auto& entry = snapshot->entries_[entry_index];
    const int original_top = lua_gettop(state);
    lua_rawgeti(state, LUA_REGISTRYINDEX, entry.registry_ref);
    if (lua_istable(state, -1) == 0) {
      lua_settop(state, original_top);
      continue;
    }

    const int frame_index = lua_absindex(state, -1);
    if (step.entering) {
      const bool parent_was_traversed =
          entry.parent_entry == std::numeric_limits<std::size_t>::max() ||
          runtime[entry.parent_entry].traversed;
      if (parent_was_traversed &&
          ReadLuaVisibilityGeneration(state, frame_index) == entry.generation &&
          LuaVisibilitySnapshotParentMatches(state, *snapshot, entry_index,
                                             frame_index)) {
        const bool current_visibility =
            IsLuaWidgetEffectivelyVisibleIterative(state, frame_index);
        const bool target =
            force_hidden
                ? false
                : ComputeLuaWidgetEffectiveVisibility(state, frame_index);
        if (current_visibility != target) {
          StoreLuaEffectiveVisibility(state, frame_index, target);
          const lua_Integer generation = entry.generation + 1;
          lua_pushinteger(state, generation);
          lua_setfield(state, frame_index, "__ow_visibility_generation");
          runtime[entry_index] = {generation, true, target};
        }
      }
    } else if (runtime[entry_index].traversed &&
               ReadLuaVisibilityGeneration(state, frame_index) ==
                   runtime[entry_index].generation &&
               IsLuaWidgetEffectivelyVisibleIterative(state, frame_index) ==
                   runtime[entry_index].target_visible) {
      const auto invocation = InvokeFrameScriptHandler(
          state, frame_index,
          runtime[entry_index].target_visible ? "OnShow" : "OnHide", 0);
      if (invocation.status != LUA_OK) {
        lua_pop(state, 1);
      }
    }
    lua_settop(state, original_top);
  }
}

inline void DispatchLuaVisibilityTransitions(LuaVisibilitySnapshot* snapshot) {
  DispatchLuaVisibilityTransitionSteps(snapshot, false);
}

inline void DispatchForcedLuaVisibilityTransition(
    LuaVisibilitySnapshot* snapshot, const bool visible) {
  DispatchLuaVisibilityTransitionSteps(snapshot, !visible);
}

inline void ClearLuaEffectiveVisibilityOverrides(
    LuaVisibilitySnapshot* snapshot) {
  if (snapshot == nullptr || snapshot->state_ == nullptr) {
    return;
  }

  lua_State* state = snapshot->state_;
  for (const auto& entry : snapshot->entries_) {
    const int original_top = lua_gettop(state);
    lua_rawgeti(state, LUA_REGISTRYINDEX, entry.registry_ref);
    if (lua_istable(state, -1) != 0) {
      lua_pushnil(state);
      lua_setfield(state, -2, "__ow_effective_visible");
    }
    lua_settop(state, original_top);
  }
}

}

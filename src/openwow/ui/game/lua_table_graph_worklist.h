#pragma once

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <array>
#include <cstddef>
#include <unordered_set>
#include <vector>

namespace openwow::ui::game::detail {

class LuaTableGraphWorklist final {
 public:
  explicit LuaTableGraphWorklist(lua_State* state) : state_(state) {}

  ~LuaTableGraphWorklist() {
    for (std::size_t index = 0; index < inline_pending_count_; ++index) {
      UnrefPending(inline_pending_[index]);
    }
    for (const int ref : spilled_pending_) {
      UnrefPending(ref);
    }
  }

  LuaTableGraphWorklist(const LuaTableGraphWorklist&) = delete;
  LuaTableGraphWorklist& operator=(const LuaTableGraphWorklist&) = delete;

  bool Enqueue(const int index) {
    if (state_ == nullptr || lua_istable(state_, index) == 0) {
      return false;
    }

    const int absolute_index = lua_absindex(state_, index);
    const void* identity = lua_topointer(state_, absolute_index);
    if (identity == nullptr || !Discover(identity)) {
      return false;
    }

    lua_pushvalue(state_, absolute_index);
    const int ref = luaL_ref(state_, LUA_REGISTRYINDEX);
    if (inline_pending_count_ < kInlineCapacity) {
      inline_pending_[inline_pending_count_++] = ref;
    } else {
      spilled_pending_.push_back(ref);
    }
    return true;
  }

  bool PushNext() {
    if (state_ == nullptr) {
      return false;
    }

    int ref = LUA_NOREF;
    if (!spilled_pending_.empty()) {
      ref = spilled_pending_.back();
      spilled_pending_.pop_back();
    } else if (inline_pending_count_ > 0) {
      ref = inline_pending_[--inline_pending_count_];
    } else {
      return false;
    }
    lua_rawgeti(state_, LUA_REGISTRYINDEX, ref);
    luaL_unref(state_, LUA_REGISTRYINDEX, ref);
    return true;
  }

  [[nodiscard]] std::size_t visited_count() const noexcept {
    return inline_visited_count_ + spilled_visited_.size();
  }

 private:
  static constexpr std::size_t kInlineCapacity = 32;

  bool Discover(const void* const identity) {
    const auto inline_end = inline_visited_.begin() +
                            static_cast<std::ptrdiff_t>(inline_visited_count_);
    if (std::find(inline_visited_.begin(), inline_end, identity) != inline_end) {
      return false;
    }
    if (inline_visited_count_ < kInlineCapacity) {
      inline_visited_[inline_visited_count_++] = identity;
      return true;
    }
    return spilled_visited_.insert(identity).second;
  }

  void UnrefPending(const int ref) const {
    if (ref != LUA_NOREF && ref != LUA_REFNIL) {
      luaL_unref(state_, LUA_REGISTRYINDEX, ref);
    }
  }

  lua_State* state_{nullptr};
  std::array<const void*, kInlineCapacity> inline_visited_{};
  std::size_t inline_visited_count_{0};
  std::array<int, kInlineCapacity> inline_pending_{};
  std::size_t inline_pending_count_{0};
  std::unordered_set<const void*> spilled_visited_;
  std::vector<int> spilled_pending_;
};

}

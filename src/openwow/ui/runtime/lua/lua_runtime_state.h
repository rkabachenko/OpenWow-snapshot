#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct lua_State;

namespace openwow::ui::lua::detail {

struct LuaOwnerToken final {
  lua_State* state{nullptr};
  std::uint64_t generation{0};
  int reference_capture_helper{-2};
  bool alive{false};
};

struct LuaAllocatorState final {
  std::size_t allocations{0};
  std::size_t reallocations{0};
  std::size_t deallocations{0};
};

struct LuaBindingLease final {
  void* adapter{nullptr};
  int (*raw_handler)(lua_State*){nullptr};
};

struct LuaBindingClaim final {
  std::string module;
  std::uint8_t kind{0};
  std::uint64_t owner_id{0};
  std::shared_ptr<LuaBindingLease> lease;
};

struct LuaBindingRegistry final {
  std::unordered_map<std::string, LuaBindingClaim> claims;
  std::unordered_set<std::string> consumed_widget_types;
  std::vector<std::shared_ptr<LuaBindingLease>> leases;
};

}

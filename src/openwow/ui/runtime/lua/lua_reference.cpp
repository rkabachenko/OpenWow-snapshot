#include "openwow/ui/runtime/lua/lua_reference.h"

#include "openwow/ui/runtime/lua/lua_runtime_state.h"
#include "openwow/ui/runtime/lua/lua_vm.h"

#include <utility>

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::lua {

LuaReference::LuaReference(std::weak_ptr<detail::LuaOwnerToken> owner,
                           const std::uint64_t generation,
                           const int reference) noexcept
    : owner_(std::move(owner)),
      generation_(generation),
      reference_(reference) {}

LuaReference::~LuaReference() {
  Reset();
}

LuaReference::LuaReference(LuaReference&& other) noexcept
    : owner_(std::move(other.owner_)),
      generation_(std::exchange(other.generation_, 0)),
      reference_(std::exchange(other.reference_, LUA_NOREF)) {}

LuaReference& LuaReference::operator=(LuaReference&& other) noexcept {
  if (this != &other) {
    Reset();
    owner_ = std::move(other.owner_);
    generation_ = std::exchange(other.generation_, 0);
    reference_ = std::exchange(other.reference_, LUA_NOREF);
  }
  return *this;
}

LuaReference LuaReference::Capture(LuaVm& vm, const int stack_index) {
  const auto owner = vm.owner_token();
  if (!vm.alive() || !owner || owner->reference_capture_helper < 0) {
    return {};
  }
  const int top = lua_gettop(vm.state());
  const int absolute_index =
      stack_index > 0 ? stack_index : top + stack_index + 1;
  if (absolute_index <= 0 || absolute_index > top ||
      lua_checkstack(vm.state(), 2) == 0) {
    return {};
  }

  lua_rawgeti(vm.state(), LUA_REGISTRYINDEX,
              owner->reference_capture_helper);
  lua_pushvalue(vm.state(), absolute_index);
  if (lua_pcall(vm.state(), 1, 1, 0) != 0) {
    lua_pop(vm.state(), 1);
    return {};
  }
  const int reference = static_cast<int>(lua_tonumber(vm.state(), -1));
  lua_pop(vm.state(), 1);
  return LuaReference(owner, vm.generation(), reference);
}

bool LuaReference::valid() const noexcept {
  const auto owner = owner_.lock();
  return owner && owner->alive && owner->state != nullptr &&
         owner->generation == generation_ && reference_ >= 0;
}

bool LuaReference::Push() const noexcept {
  const auto owner = owner_.lock();
  if (!owner || !owner->alive || owner->state == nullptr ||
      owner->generation != generation_ || reference_ < 0) {
    return false;
  }
  if (lua_checkstack(owner->state, 1) == 0) {
    return false;
  }
  lua_rawgeti(owner->state, LUA_REGISTRYINDEX, reference_);
  return true;
}

void LuaReference::Reset() noexcept {
  const auto owner = owner_.lock();
  if (owner && owner->alive && owner->state != nullptr &&
      owner->generation == generation_ && reference_ >= 0) {
    luaL_unref(owner->state, LUA_REGISTRYINDEX, reference_);
  }
  owner_.reset();
  generation_ = 0;
  reference_ = LUA_NOREF;
}

}

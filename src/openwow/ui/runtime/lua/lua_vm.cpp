#include "openwow/ui/runtime/lua/lua_vm.h"

#include "openwow/ui/runtime/lua/lua_runtime_state.h"

#include <atomic>
#include <cstdlib>
#include <string>
#include <utility>

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::lua {
namespace {

std::atomic<std::uint64_t> next_generation{1};

void* ObserveLuaAllocation(void* user_data, void* memory,
                           std::size_t, const std::size_t new_size) {
  auto& state = *static_cast<detail::LuaAllocatorState*>(user_data);
  if (new_size == 0) {
    if (memory != nullptr) {
      ++state.deallocations;
      std::free(memory);
    }
    return nullptr;
  }
  if (memory == nullptr) {
    ++state.allocations;
    return std::malloc(new_size);
  }
  ++state.reallocations;
  return std::realloc(memory, new_size);
}

LuaError ErrorForStatus(const int status) noexcept {
  switch (status) {
    case LUA_ERRSYNTAX:
      return {LuaErrorCode::kSyntax, "Lua chunk is invalid"};
    case LUA_ERRRUN:
      return {LuaErrorCode::kRuntime, "Lua execution failed"};
    case LUA_ERRMEM:
      return {LuaErrorCode::kMemory, "Lua memory allocation failed"};
    case LUA_ERRERR:
      return {LuaErrorCode::kErrorHandler, "Lua error handler failed"};
    default:
      return {LuaErrorCode::kUnknown, "Lua execution failed"};
  }
}

int CaptureReferenceProtected(lua_State* state) {
  lua_pushvalue(state, 1);
  const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
  lua_pushnumber(state, static_cast<lua_Number>(reference));
  return 1;
}

struct InitializeContext final {
  bool open_standard_libraries;
  int capture_helper_reference;
};

int InitializeState(lua_State* state) {
  auto* context = static_cast<InitializeContext*>(lua_touserdata(state, 1));
  if (context->open_standard_libraries) {
    luaL_openlibs(state);
  }
  lua_pushcfunction(state, CaptureReferenceProtected);
  context->capture_helper_reference = luaL_ref(state, LUA_REGISTRYINDEX);
  return 0;
}

}

LuaVm::LuaVm() {
  Create(true);
}

LuaVm::LuaVm(const bool open_standard_libraries) {
  Create(open_standard_libraries);
}

LuaVm::~LuaVm() {
  Destroy();
}

LuaVm::LuaVm(LuaVm&& other) noexcept
    : state_(std::exchange(other.state_, nullptr)),
      owner_(std::move(other.owner_)),
      binding_registry_(std::move(other.binding_registry_)),
      allocator_state_(std::move(other.allocator_state_)) {}

LuaVm& LuaVm::operator=(LuaVm&& other) noexcept {
  if (this != &other) {
    Destroy();
    state_ = std::exchange(other.state_, nullptr);
    owner_ = std::move(other.owner_);
    binding_registry_ = std::move(other.binding_registry_);
    allocator_state_ = std::move(other.allocator_state_);
  }
  return *this;
}

std::uint64_t LuaVm::generation() const noexcept {
  return owner_ ? owner_->generation : 0;
}

LuaAllocatorStats LuaVm::allocator_stats() const noexcept {
  if (!allocator_state_) {
    return {};
  }
  return {allocator_state_->allocations, allocator_state_->reallocations,
          allocator_state_->deallocations};
}

void LuaVm::Create(const bool open_standard_libraries) {
  owner_ = std::make_shared<detail::LuaOwnerToken>();
  binding_registry_ = std::make_shared<detail::LuaBindingRegistry>();
  allocator_state_ = std::make_shared<detail::LuaAllocatorState>();
  owner_->generation = next_generation.fetch_add(1, std::memory_order_relaxed);
  lua_State* new_state =
      lua_newstate(ObserveLuaAllocation, allocator_state_.get());
  if (new_state == nullptr) {
    return;
  }

  InitializeContext context{open_standard_libraries, LUA_NOREF};
  if (lua_cpcall(new_state, InitializeState, &context) != 0) {
    lua_close(new_state);
    return;
  }

  state_ = new_state;
  owner_->state = state_;
  owner_->reference_capture_helper = context.capture_helper_reference;
  owner_->alive = true;
}

void LuaVm::Destroy() noexcept {
  if (owner_) {
    owner_->alive = false;
    owner_->state = nullptr;
  }
  if (state_ != nullptr) {
    lua_close(state_);
    state_ = nullptr;
  }
  owner_.reset();
  binding_registry_.reset();
  allocator_state_.reset();
}

void LuaVm::Recreate(const bool open_standard_libraries) {
  Destroy();
  Create(open_standard_libraries);
}

LuaExecutionResult LuaVm::LoadAndRun(const std::string_view source,
                                     const std::string_view chunk_name,
                                     const int result_count) {
  if (state_ == nullptr) {
    return {false, 0, {LuaErrorCode::kRuntime, "Lua VM is not available"}};
  }
  if (result_count < 0) {
    return {false, 0,
            {LuaErrorCode::kRuntime, "Lua result count is invalid"}};
  }

  const int initial_top = lua_gettop(state_);
  const std::string owned_name(chunk_name);
  const int load_status =
      luaL_loadbuffer(state_, source.empty() ? "" : source.data(), source.size(),
                      owned_name.c_str());
  if (load_status != 0) {
    lua_settop(state_, initial_top);
    return {false, 0, ErrorForStatus(load_status)};
  }
  return ProtectedCall(0, result_count);
}

LuaExecutionResult LuaVm::ProtectedCall(const int argument_count,
                                        const int result_count) {
  if (state_ == nullptr) {
    return {false, 0, {LuaErrorCode::kRuntime, "Lua VM is not available"}};
  }
  if (argument_count < 0 || result_count < 0) {
    return {false, 0,
            {LuaErrorCode::kRuntime, "Lua call frame is invalid"}};
  }

  const int base = lua_gettop(state_) - argument_count - 1;
  if (base < 0) {
    return {false, 0, {LuaErrorCode::kRuntime, "Lua call frame is invalid"}};
  }

  const int status = lua_pcall(state_, argument_count, result_count, 0);
  if (status != 0) {
    lua_settop(state_, base);
    return {false, 0, ErrorForStatus(status)};
  }

  return {true, lua_gettop(state_) - base, {}};
}

LuaStackFrame::LuaStackFrame(lua_State* state,
                             const int expected_result_count) noexcept
    : state_(state),
      initial_top_(state == nullptr ? 0 : lua_gettop(state)),
      expected_result_count_(expected_result_count) {}

bool LuaStackFrame::ValidateNormalCompletion() const noexcept {
  return state_ != nullptr && expected_result_count_ >= 0 &&
         lua_gettop(state_) == initial_top_ + expected_result_count_;
}

}

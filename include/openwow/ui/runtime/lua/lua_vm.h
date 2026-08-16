#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

struct lua_State;

namespace openwow::ui::lua {

namespace detail {
struct LuaOwnerToken;
struct LuaBindingRegistry;
struct LuaBindingLease;
struct LuaAllocatorState;
}

enum class LuaErrorCode : std::uint8_t {
  kSyntax,
  kRuntime,
  kMemory,
  kErrorHandler,
  kUnknown,
};

struct LuaError final {
  LuaErrorCode code{LuaErrorCode::kUnknown};
  std::string_view message{"Lua execution failed"};
};

struct LuaExecutionResult final {
  bool succeeded{false};
  int result_count{0};
  LuaError error{};

  [[nodiscard]] explicit operator bool() const noexcept { return succeeded; }
};

struct LuaAllocatorStats final {
  std::size_t allocations{0};
  std::size_t reallocations{0};
  std::size_t deallocations{0};

  constexpr bool operator==(const LuaAllocatorStats&) const = default;
};

class LuaVm final {
 public:
  LuaVm();
  explicit LuaVm(bool open_standard_libraries);
  ~LuaVm();

  LuaVm(const LuaVm&) = delete;
  LuaVm& operator=(const LuaVm&) = delete;
  LuaVm(LuaVm&& other) noexcept;
  LuaVm& operator=(LuaVm&& other) noexcept;

  [[nodiscard]] lua_State* state() const noexcept { return state_; }
  [[nodiscard]] std::uint64_t generation() const noexcept;
  [[nodiscard]] bool alive() const noexcept { return state_ != nullptr; }
  [[nodiscard]] LuaAllocatorStats allocator_stats() const noexcept;

  void Destroy() noexcept;
  void Recreate(bool open_standard_libraries = true);

  [[nodiscard]] LuaExecutionResult LoadAndRun(
      std::string_view source, std::string_view chunk_name = "openwow",
      int result_count = 0);
  [[nodiscard]] LuaExecutionResult ProtectedCall(int argument_count,
                                                 int result_count);

 private:
  friend class LuaReference;
  friend class BindingSet;
  friend class NativeLuaBindings;

  [[nodiscard]] std::shared_ptr<detail::LuaOwnerToken> owner_token() const {
    return owner_;
  }

  void Create(bool open_standard_libraries);

  lua_State* state_{nullptr};
  std::shared_ptr<detail::LuaOwnerToken> owner_;
  std::shared_ptr<detail::LuaBindingRegistry> binding_registry_;
  std::shared_ptr<detail::LuaAllocatorState> allocator_state_;
};

class LuaStackFrame final {
 public:
  LuaStackFrame(lua_State* state, int expected_result_count) noexcept;

  LuaStackFrame(const LuaStackFrame&) = delete;
  LuaStackFrame& operator=(const LuaStackFrame&) = delete;

  [[nodiscard]] bool ValidateNormalCompletion() const noexcept;
  [[nodiscard]] int initial_top() const noexcept { return initial_top_; }

 private:
  lua_State* state_;
  int initial_top_;
  int expected_result_count_;
};

}

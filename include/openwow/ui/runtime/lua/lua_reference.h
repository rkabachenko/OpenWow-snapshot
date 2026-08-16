#pragma once

#include <cstdint>
#include <memory>

struct lua_State;

namespace openwow::ui::lua {

class LuaVm;

namespace detail {
struct LuaOwnerToken;
}

class LuaReference final {
 public:
  LuaReference() = default;
  ~LuaReference();

  LuaReference(const LuaReference&) = delete;
  LuaReference& operator=(const LuaReference&) = delete;
  LuaReference(LuaReference&& other) noexcept;
  LuaReference& operator=(LuaReference&& other) noexcept;

  [[nodiscard]] static LuaReference Capture(LuaVm& vm, int stack_index);

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool Push() const noexcept;
  void Reset() noexcept;

 private:
  LuaReference(std::weak_ptr<detail::LuaOwnerToken> owner,
               std::uint64_t generation, int reference) noexcept;

  std::weak_ptr<detail::LuaOwnerToken> owner_;
  std::uint64_t generation_{0};
  int reference_{-2};
};

}

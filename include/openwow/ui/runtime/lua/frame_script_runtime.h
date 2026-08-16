#pragma once

#include "openwow/ui/runtime/lua/lua_binding.h"
#include "openwow/ui/runtime/lua/lua_reference.h"
#include "openwow/ui/runtime/lua/lua_vm.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::ui::lua {

enum class FrameScriptScope : std::uint8_t { kGlue, kWorld };
enum class ScriptSecurityOrigin : std::uint8_t { kSecure, kInsecure };

class LuaBindingComposition;

using LuaVmInitializer = bool (*)(LuaVm& vm);

class FrameScriptRuntime final {
 public:
  FrameScriptRuntime() = default;
  ~FrameScriptRuntime();

  FrameScriptRuntime(const FrameScriptRuntime&) = delete;
  FrameScriptRuntime& operator=(const FrameScriptRuntime&) = delete;

  bool BootGlue(std::unique_ptr<LuaBindingComposition> bindings,
                LuaVmInitializer initialize = nullptr);
  bool BootWorld(std::unique_ptr<LuaBindingComposition> bindings,
                 LuaVmInitializer initialize = nullptr);
  void Destroy() noexcept;

  [[nodiscard]] LuaVm* vm() noexcept;
  [[nodiscard]] const LuaVm* vm() const noexcept;
  [[nodiscard]] lua_State* state() noexcept;
  [[nodiscard]] std::uint64_t generation() const noexcept;
  [[nodiscard]] std::vector<BindingDescriptor> binding_descriptors() const;
  [[nodiscard]] std::optional<FrameScriptScope> scope() const noexcept {
    return scope_;
  }

  bool RecordScriptSecurity(std::uint64_t script_id,
                            ScriptSecurityOrigin origin);
  [[nodiscard]] std::optional<ScriptSecurityOrigin> ScriptSecurity(
      std::uint64_t script_id) const;
  bool RetainReference(int stack_index);
  bool RetainInvocationReference(int stack_index);
  void ClearInvocationState() noexcept;

 private:
  bool Boot(FrameScriptScope scope,
            std::unique_ptr<LuaBindingComposition> bindings,
            LuaVmInitializer initialize);

  std::optional<LuaVm> vm_;
  std::unique_ptr<LuaBindingComposition> bindings_;
  std::optional<FrameScriptScope> scope_;
  std::uint64_t state_generation_{0};
  std::unordered_map<std::uint64_t, ScriptSecurityOrigin> script_security_;
  std::vector<LuaReference> retained_references_;
  std::vector<LuaReference> invocation_references_;
};

}

#include "openwow/ui/runtime/lua/frame_script_runtime.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

#include <utility>

namespace openwow::ui::lua {

FrameScriptRuntime::~FrameScriptRuntime() {
  Destroy();
}

bool FrameScriptRuntime::BootGlue(
    std::unique_ptr<LuaBindingComposition> bindings,
    const LuaVmInitializer initialize) {
  return Boot(FrameScriptScope::kGlue, std::move(bindings), initialize);
}

bool FrameScriptRuntime::BootWorld(
    std::unique_ptr<LuaBindingComposition> bindings,
    const LuaVmInitializer initialize) {
  return Boot(FrameScriptScope::kWorld, std::move(bindings), initialize);
}

bool FrameScriptRuntime::Boot(
    const FrameScriptScope scope,
    std::unique_ptr<LuaBindingComposition> bindings,
    const LuaVmInitializer initialize) {
  Destroy();
  if (!bindings || bindings->scope() != scope) {
    return false;
  }

  vm_.emplace(false);
  if (!vm_->alive()) {
    Destroy();
    return false;
  }
  if (initialize != nullptr && !initialize(*vm_)) {
    Destroy();
    return false;
  }
  scope_ = scope;
  state_generation_ = vm_->generation();
  bindings_ = std::move(bindings);
  if (!bindings_->Install(*vm_)) {
    Destroy();
    return false;
  }
  return true;
}

void FrameScriptRuntime::Destroy() noexcept {
  bindings_.reset();
  ClearInvocationState();
  retained_references_.clear();
  script_security_.clear();
  state_generation_ = 0;
  scope_.reset();
  vm_.reset();
}

LuaVm* FrameScriptRuntime::vm() noexcept {
  return vm_ ? &*vm_ : nullptr;
}

const LuaVm* FrameScriptRuntime::vm() const noexcept {
  return vm_ ? &*vm_ : nullptr;
}

lua_State* FrameScriptRuntime::state() noexcept {
  return vm_ ? vm_->state() : nullptr;
}

std::uint64_t FrameScriptRuntime::generation() const noexcept {
  return vm_ ? vm_->generation() : 0;
}

std::vector<BindingDescriptor> FrameScriptRuntime::binding_descriptors() const {
  return bindings_ ? bindings_->descriptors()
                   : std::vector<BindingDescriptor>{};
}

bool FrameScriptRuntime::RecordScriptSecurity(
    const std::uint64_t script_id, const ScriptSecurityOrigin origin) {
  if (!vm_ || !vm_->alive() || vm_->generation() != state_generation_) {
    return false;
  }
  script_security_[script_id] = origin;
  return true;
}

std::optional<ScriptSecurityOrigin> FrameScriptRuntime::ScriptSecurity(
    const std::uint64_t script_id) const {
  if (!vm_ || !vm_->alive() || vm_->generation() != state_generation_) {
    return std::nullopt;
  }
  const auto found = script_security_.find(script_id);
  return found == script_security_.end()
             ? std::nullopt
             : std::optional<ScriptSecurityOrigin>(found->second);
}

bool FrameScriptRuntime::RetainReference(const int stack_index) {
  if (!vm_) {
    return false;
  }
  auto reference = LuaReference::Capture(*vm_, stack_index);
  if (!reference.valid()) {
    return false;
  }
  retained_references_.push_back(std::move(reference));
  return true;
}

bool FrameScriptRuntime::RetainInvocationReference(const int stack_index) {
  if (!vm_) {
    return false;
  }
  auto reference = LuaReference::Capture(*vm_, stack_index);
  if (!reference.valid()) {
    return false;
  }
  invocation_references_.push_back(std::move(reference));
  return true;
}

void FrameScriptRuntime::ClearInvocationState() noexcept {
  invocation_references_.clear();
}

}

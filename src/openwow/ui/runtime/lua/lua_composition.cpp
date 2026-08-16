#include "openwow/ui/runtime/lua/lua_composition.h"

#include "openwow/ui/runtime/lua/lua_runtime_state.h"

#include <algorithm>
#include <numeric>
#include <utility>

namespace openwow::ui::lua {
namespace {

bool ScopeAllowed(const FrameScriptScope runtime_scope,
                  const BindingScope binding_scope) {
  return binding_scope == BindingScope::kShared ||
         (runtime_scope == FrameScriptScope::kGlue &&
          binding_scope == BindingScope::kGlue) ||
         (runtime_scope == FrameScriptScope::kWorld &&
          binding_scope == BindingScope::kWorld);
}

template <typename Catalogs>
void UninstallCatalogs(Catalogs& catalogs,
                       const std::size_t count) noexcept {
  for (std::size_t index = 0; index < count; ++index) {
    catalogs[index].RetireBindings();
  }

  std::vector<std::size_t> lifecycle_order(count);
  std::iota(lifecycle_order.begin(), lifecycle_order.end(), 0);

  std::reverse(lifecycle_order.begin(), lifecycle_order.end());
  std::stable_sort(
      lifecycle_order.begin(), lifecycle_order.end(),
      [&catalogs](const std::size_t left, const std::size_t right) {
        return catalogs[left].teardown_phase() <
               catalogs[right].teardown_phase();
      });
  for (const auto index : lifecycle_order) {
    if (catalogs[index].has_lifecycle()) {
      catalogs[index].UninstallModule();
    }
  }
}

template <typename Catalogs>
bool InstallCatalogs(const FrameScriptScope scope, LuaVm& vm,
                     Catalogs& catalogs) {
  for (const auto& catalog : catalogs) {
    if (!ScopeAllowed(scope, catalog.scope())) {
      return false;
    }
  }
  std::size_t installed = 0;
  for (auto& catalog : catalogs) {
    if (!catalog.Install(vm)) {
      UninstallCatalogs(catalogs, installed);
      return false;
    }
    ++installed;
  }
  return true;
}

std::vector<NativeLuaBindings> MakeCatalogs(
    std::vector<NativeBindingCatalog> catalogs) {
  std::vector<NativeLuaBindings> result;
  result.reserve(catalogs.size());
  for (auto& catalog : catalogs) {
    result.emplace_back(std::move(catalog));
  }
  return result;
}

void AppendDescriptors(const std::vector<NativeLuaBindings>& catalogs,
                       std::vector<BindingDescriptor>& result) {
  for (const auto& catalog : catalogs) {
    auto descriptors = catalog.descriptors();
    result.insert(result.end(), descriptors.begin(), descriptors.end());
  }
}

}

NativeLuaBindings::NativeLuaBindings(NativeBindingCatalog catalog)
    : scope_(catalog.scope),
      functions_(std::move(catalog.functions)),
      constants_(std::move(catalog.constants)),
      widget_methods_(std::move(catalog.widget_methods)),
      install_module_(catalog.install),
      uninstall_module_(catalog.uninstall),
      lifecycle_context_(std::move(catalog.lifecycle_context)),
      teardown_phase_(catalog.teardown_phase),
      global_bindings_(catalog.owner, scope_) {
  if (!widget_methods_.empty()) {
    widget_bindings_.emplace(
        catalog.owner, scope_, BindingDestination::WidgetMethod(catalog.owner));
  }
}

NativeLuaBindings::~NativeLuaBindings() {
  Uninstall();
}

NativeLuaBindings::NativeLuaBindings(NativeLuaBindings&& other) noexcept
    : scope_(other.scope_),
      functions_(std::move(other.functions_)),
      constants_(std::move(other.constants_)),
      widget_methods_(std::move(other.widget_methods_)),
      install_module_(std::exchange(other.install_module_, nullptr)),
      uninstall_module_(std::exchange(other.uninstall_module_, nullptr)),
      lifecycle_context_(std::move(other.lifecycle_context_)),
      teardown_phase_(other.teardown_phase_),
      owner_(std::move(other.owner_)),
      generation_(std::exchange(other.generation_, 0)),
      global_bindings_(std::move(other.global_bindings_)),
      widget_bindings_(std::move(other.widget_bindings_)) {}

NativeLuaBindings& NativeLuaBindings::operator=(
    NativeLuaBindings&& other) noexcept {
  if (this != &other) {
    Uninstall();
    scope_ = other.scope_;
    functions_ = std::move(other.functions_);
    constants_ = std::move(other.constants_);
    widget_methods_ = std::move(other.widget_methods_);
    install_module_ = std::exchange(other.install_module_, nullptr);
    uninstall_module_ = std::exchange(other.uninstall_module_, nullptr);
    lifecycle_context_ = std::move(other.lifecycle_context_);
    teardown_phase_ = other.teardown_phase_;
    owner_ = std::move(other.owner_);
    generation_ = std::exchange(other.generation_, 0);
    global_bindings_ = std::move(other.global_bindings_);
    widget_bindings_ = std::move(other.widget_bindings_);
  }
  return *this;
}

bool NativeLuaBindings::Install(LuaVm& vm) {
  if (generation_ != 0 || !vm.alive()) {
    return false;
  }
  owner_ = vm.owner_token();
  generation_ = vm.generation();
  if (install_module_ != nullptr) {
    install_module_(vm.state(), lifecycle_context_.get());
  }

  std::vector<RawFunctionBinding> functions;
  functions.reserve(functions_.size());
  for (const auto& function : functions_) {
    functions.push_back(
        raw_function(function.name, function.handler, function.collision,
                     function.adapter_context
                         ? function.adapter_context.get()
                         : lifecycle_context_.get()));
  }
  if (!global_bindings_.InstallRawFunctions(vm, functions)) {
    Uninstall();
    return false;
  }

  std::vector<ConstantBinding> constants;
  constants.reserve(constants_.size());
  for (const auto& value : constants_) {
    constants.push_back(constant(value.name, value.value, value.collision));
  }
  if (!global_bindings_.InstallConstants(vm, constants)) {
    Uninstall();
    return false;
  }

  if (widget_bindings_) {
    std::vector<std::string_view> methods;
    methods.reserve(widget_methods_.size());
    for (const auto& method : widget_methods_) {
      methods.push_back(method);
    }
    if (!widget_bindings_->ClaimWidgetMethods(vm, methods)) {
      Uninstall();
      return false;
    }
  }
  return true;
}

void NativeLuaBindings::Uninstall() noexcept {
  RetireBindings();
  UninstallModule();
}

void NativeLuaBindings::RetireBindings() noexcept {
  if (widget_bindings_) {
    widget_bindings_->Uninstall();
  }
  global_bindings_.Uninstall();
}

void NativeLuaBindings::UninstallModule() noexcept {
  const auto owner = owner_.lock();
  if (owner && owner->alive && owner->generation == generation_ &&
      uninstall_module_ != nullptr) {
    uninstall_module_(owner->state, lifecycle_context_.get());
  }
  owner_.reset();
  generation_ = 0;
}

std::vector<BindingDescriptor> NativeLuaBindings::descriptors() const {
  std::vector<BindingDescriptor> result = global_bindings_.descriptors();
  if (widget_bindings_) {
    const auto& methods = widget_bindings_->descriptors();
    result.insert(result.end(), methods.begin(), methods.end());
  }
  return result;
}

LuaBindingComposition::LuaBindingComposition(
    const FrameScriptScope scope,
    std::vector<NativeBindingCatalog> catalogs)
    : scope_(scope), catalogs_(MakeCatalogs(std::move(catalogs))) {}

LuaBindingComposition::~LuaBindingComposition() {
  UninstallCatalogs(catalogs_, catalogs_.size());
}

bool LuaBindingComposition::Install(LuaVm& vm) {
  return InstallCatalogs(scope_, vm, catalogs_);
}

std::vector<BindingDescriptor> LuaBindingComposition::descriptors() const {
  std::vector<BindingDescriptor> result;
  AppendDescriptors(catalogs_, result);
  return result;
}

}

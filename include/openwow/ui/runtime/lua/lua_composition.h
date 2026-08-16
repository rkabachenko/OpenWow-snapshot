#pragma once

#include "openwow/ui/runtime/lua/frame_script_runtime.h"
#include "openwow/ui/runtime/lua/lua_binding.h"

#include <array>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace openwow::ui::lua {

struct NativeLuaFunction final {
  NativeLuaFunction() = default;
  NativeLuaFunction(
      std::string public_name,
      lua_CFunction public_handler,
      CollisionPolicy public_collision = CollisionPolicy::kReplaceExisting,
      std::shared_ptr<void> public_adapter_context = {})
      : name(std::move(public_name)),
        handler(public_handler),
        collision(public_collision),
        adapter_context(std::move(public_adapter_context)) {}

  std::string name;
  lua_CFunction handler{nullptr};
  CollisionPolicy collision{CollisionPolicy::kReplaceExisting};
  std::shared_ptr<void> adapter_context;
};

struct NativeLuaConstant final {
  std::string name;
  BindingConstantValue value;
  CollisionPolicy collision{CollisionPolicy::kReject};
};

struct WidgetMethodOwnership final {
  std::string_view owner;
  std::string_view name;
};

using NativeModuleInstall = void (*)(lua_State* state, void* context);
using NativeModuleUninstall = void (*)(lua_State* state, void* context);

enum class NativeModuleTeardownPhase : std::uint8_t {
  kDependent,
  kIndependent,
  kSharedFoundation,
};

struct NativeBindingCatalog final {
  std::string owner;
  BindingScope scope{BindingScope::kShared};
  std::vector<NativeLuaFunction> functions;
  std::vector<NativeLuaConstant> constants;
  std::vector<std::string> widget_methods;
  NativeModuleInstall install{nullptr};
  NativeModuleUninstall uninstall{nullptr};
  std::shared_ptr<void> lifecycle_context;
  NativeModuleTeardownPhase teardown_phase{
      NativeModuleTeardownPhase::kIndependent};
};

template <typename Binding>
NativeBindingCatalog NativeFunctionCatalog(std::string owner,
                                           BindingScope scope,
                                           std::span<const Binding> bindings) {
  NativeBindingCatalog catalog{.owner = std::move(owner), .scope = scope};
  catalog.functions.reserve(bindings.size());
  for (const auto& binding : bindings) {
    catalog.functions.emplace_back(
        std::string(binding.name), binding.handler);
  }
  return catalog;
}

template <typename Binding, std::size_t Size>
NativeBindingCatalog NativeFunctionCatalog(
    std::string owner, BindingScope scope, const Binding (&bindings)[Size]) {
  return NativeFunctionCatalog(std::move(owner), scope,
                               std::span<const Binding>{bindings});
}

template <typename Binding, std::size_t Size>
NativeBindingCatalog NativeFunctionCatalog(
    std::string owner, BindingScope scope,
    const std::array<Binding, Size>& bindings) {
  return NativeFunctionCatalog(std::move(owner), scope,
                               std::span<const Binding>{bindings});
}

template <typename Range>
NativeBindingCatalog NativeConstantCatalog(std::string owner,
                                           BindingScope scope,
                                           const Range& constants) {
  NativeBindingCatalog catalog{.owner = std::move(owner), .scope = scope};
  for (const auto& constant : constants) {
    using Value = std::remove_cvref_t<decltype(constant.value)>;
    BindingConstantValue value;
    if constexpr (std::same_as<Value, bool>) {
      value = constant.value;
    } else if constexpr (std::is_convertible_v<Value, const char*>) {
      value = std::string(constant.value != nullptr ? constant.value : "");
    } else {
      value = static_cast<double>(constant.value);
    }
    catalog.constants.push_back(
        {std::string(constant.name), std::move(value),
         CollisionPolicy::kReplaceExisting});
  }
  return catalog;
}

class NativeLuaBindings final {
 public:
  explicit NativeLuaBindings(NativeBindingCatalog catalog);
  ~NativeLuaBindings();

  NativeLuaBindings(const NativeLuaBindings&) = delete;
  NativeLuaBindings& operator=(const NativeLuaBindings&) = delete;
  NativeLuaBindings(NativeLuaBindings&& other) noexcept;
  NativeLuaBindings& operator=(NativeLuaBindings&& other) noexcept;

  bool Install(LuaVm& vm);
  void Uninstall() noexcept;
  void RetireBindings() noexcept;
  void UninstallModule() noexcept;
  [[nodiscard]] BindingScope scope() const noexcept { return scope_; }
  [[nodiscard]] bool has_lifecycle() const noexcept {
    return uninstall_module_ != nullptr;
  }
  [[nodiscard]] NativeModuleTeardownPhase teardown_phase() const noexcept {
    return teardown_phase_;
  }
  [[nodiscard]] std::vector<BindingDescriptor> descriptors() const;

 private:
  BindingScope scope_;
  std::vector<NativeLuaFunction> functions_;
  std::vector<NativeLuaConstant> constants_;
  std::vector<std::string> widget_methods_;
  NativeModuleInstall install_module_{nullptr};
  NativeModuleUninstall uninstall_module_{nullptr};
  std::shared_ptr<void> lifecycle_context_;
  NativeModuleTeardownPhase teardown_phase_{
      NativeModuleTeardownPhase::kIndependent};
  std::weak_ptr<detail::LuaOwnerToken> owner_;
  std::uint64_t generation_{0};
  BindingSet global_bindings_;
  std::optional<BindingSet> widget_bindings_;
};

class LuaBindingComposition final {
 public:
  LuaBindingComposition(
      FrameScriptScope scope,
      std::vector<NativeBindingCatalog> catalogs);
  ~LuaBindingComposition();

  LuaBindingComposition(const LuaBindingComposition&) = delete;
  LuaBindingComposition& operator=(const LuaBindingComposition&) = delete;

  [[nodiscard]] FrameScriptScope scope() const noexcept { return scope_; }
  bool Install(LuaVm& vm);
  [[nodiscard]] std::vector<BindingDescriptor> descriptors() const;

 private:
  FrameScriptScope scope_;
  std::vector<NativeLuaBindings> catalogs_;
};

}

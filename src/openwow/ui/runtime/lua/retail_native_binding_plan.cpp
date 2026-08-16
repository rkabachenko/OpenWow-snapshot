#include "openwow/ui/runtime/lua/retail_native_binding_plan.h"

#include <algorithm>
#include <array>
#include <deque>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace openwow::ui::lua {
namespace {

struct RetailRegistration final {
  std::string_view group;
  std::string_view name;
};

constexpr auto kRetailRegistrationOrder =
    std::to_array<RetailRegistration>({
#include "openwow/ui/runtime/lua/retail_native_registration_order.inc"
    });

constexpr auto kRetailGlueRegistrationOrder =
    std::to_array<RetailRegistration>({
#include "openwow/ui/runtime/lua/retail_glue_native_registration_order.inc"
    });

constexpr std::string_view kFrameScriptTypeRegistrationBoundary = "common";

struct Lifecycle final {
  NativeModuleInstall install{nullptr};
  NativeModuleUninstall uninstall{nullptr};
  std::shared_ptr<void> context;
  NativeModuleTeardownPhase teardown_phase{
      NativeModuleTeardownPhase::kIndependent};
};

std::unordered_map<std::string_view, std::string_view> RegistrationGroups(
    const std::span<const RetailRegistration> order) {
  std::unordered_map<std::string_view, std::string_view> groups;
  groups.reserve(order.size());
  for (const auto& registration : order) {
    groups.emplace(registration.name, registration.group);
  }
  return groups;
}

RetailNativeBindingPlan ComposeRetailNativeBindings(
    std::vector<NativeBindingCatalog> sources,
    const std::span<const RetailRegistration> order,
    const BindingScope feature_scope) {
  const auto groups = RegistrationGroups(order);
  std::unordered_map<std::string, std::deque<NativeLuaFunction>> functions;
  std::unordered_map<std::string, Lifecycle> lifecycles;
  std::vector<NativeBindingCatalog> constants;
  functions.reserve(order.size());

  for (auto& source : sources) {
    std::unordered_set<std::string_view> source_groups;
    for (auto& function : source.functions) {
      const auto group = groups.find(function.name);
      if (group == groups.end()) {
        throw std::logic_error("non-retail native Lua binding: " +
                               function.name);
      }
      source_groups.insert(group->second);
      if (!function.adapter_context) {
        function.adapter_context = source.lifecycle_context;
      }
      functions[function.name].push_back(std::move(function));
    }

    if (source.install != nullptr || source.uninstall != nullptr) {
      if (source_groups.size() != 1) {
        throw std::logic_error(
            "native Lua lifecycle must belong to one retail group");
      }
      const auto lifecycle_group = std::string(*source_groups.begin());
      const bool inserted =
          lifecycles.emplace(
              lifecycle_group,
              Lifecycle{source.install, source.uninstall,
                        std::move(source.lifecycle_context),
                        source.teardown_phase})
              .second;
      if (!inserted) {
        throw std::logic_error(
            "multiple native Lua lifecycles own retail group: " +
            lifecycle_group);
      }
    }

    if (!source.constants.empty()) {
      source.functions.clear();
      source.install = nullptr;
      source.uninstall = nullptr;
      constants.push_back(std::move(source));
    }
  }

  std::vector<NativeBindingCatalog> result;
  result.reserve(order.size() + constants.size());
  std::string_view active_group;
  NativeBindingCatalog* active_catalog = nullptr;
  for (const auto& registration : order) {
    if (registration.group != active_group) {
      active_group = registration.group;
      result.push_back({
          .owner = "ui.framescript." + std::string(active_group),
          .scope = active_group == "common" ? BindingScope::kShared
                                             : feature_scope,
      });
      active_catalog = &result.back();
      if (const auto lifecycle =
              lifecycles.find(std::string(active_group));
          lifecycle != lifecycles.end()) {
        active_catalog->install = lifecycle->second.install;
        active_catalog->uninstall = lifecycle->second.uninstall;
        active_catalog->lifecycle_context =
            std::move(lifecycle->second.context);
        active_catalog->teardown_phase =
            lifecycle->second.teardown_phase;
        lifecycles.erase(lifecycle);
      }
    }

    const auto function = functions.find(std::string(registration.name));
    if (function == functions.end()) {
      throw std::logic_error("missing retail native Lua binding: " +
                             std::string(registration.name));
    }
    active_catalog->functions.push_back(
        std::move(function->second.front()));
    function->second.pop_front();
    if (function->second.empty()) {
      functions.erase(function);
    }
  }

  if (!functions.empty() || !lifecycles.empty()) {
    throw std::logic_error("unconsumed native Lua binding source");
  }
  result.insert(result.end(),
                std::make_move_iterator(constants.begin()),
                std::make_move_iterator(constants.end()));

  const auto boundary_owner =
      "ui.framescript." + std::string(kFrameScriptTypeRegistrationBoundary);
  const auto boundary_catalog = std::find_if(
      result.begin(), result.end(),
      [&boundary_owner](const NativeBindingCatalog& catalog) {
        return catalog.owner == boundary_owner;
      });
  if (boundary_catalog == result.end()) {
    throw std::logic_error(
        "retail native Lua plan has no FrameScript type boundary");
  }

  RetailNativeBindingPlan plan;
  const auto after_boundary = std::next(boundary_catalog);
  plan.before_frame_script_types.insert(
      plan.before_frame_script_types.end(),
      std::make_move_iterator(result.begin()),
      std::make_move_iterator(after_boundary));
  plan.after_frame_script_types.insert(
      plan.after_frame_script_types.end(),
      std::make_move_iterator(after_boundary),
      std::make_move_iterator(result.end()));
  return plan;
}

}

RetailNativeBindingPlan ComposeRetailWorldNativeBindings(
    std::vector<NativeBindingCatalog> sources) {
  return ComposeRetailNativeBindings(
      std::move(sources), kRetailRegistrationOrder,
      BindingScope::kWorld);
}

RetailNativeBindingPlan ComposeRetailGlueNativeBindings(
    std::vector<NativeBindingCatalog> sources) {
  return ComposeRetailNativeBindings(
      std::move(sources), kRetailGlueRegistrationOrder,
      BindingScope::kGlue);
}

}

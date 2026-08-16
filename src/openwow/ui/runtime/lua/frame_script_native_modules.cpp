#include "openwow/ui/runtime/lua/frame_script_native_modules.h"

#include "openwow/ui/frame_script_shared_bindings.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/runtime/world_lua_runtime.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace openwow::ui {

#include "openwow/ui/retail_widget_method_surface.inc"

namespace lua::modules {
namespace {

void InstallSharedFrameScriptTypes(lua_State* state, void*) {
  FrameScript_RegisterSharedUiLuaBindings(state);
}

void UninstallSharedFrameScriptTypes(lua_State* state, void*) {
  FrameScript_UnregisterSharedUiLuaBindings(state);
}

void InstallWorldWidgetSubtypes(lua_State* state, void*) {
  game::frame_api::RegisterGameTooltipScriptMethods(state);
  game::frame_api::RegisterCooldownScriptMethods(state);
  game::frame_api::RegisterMinimapScriptMethods(state);
  game::frame_api::RegisterPlayerModelScriptMethods(state);
  game::frame_api::RegisterDressUpModelScriptMethods(state);
  game::frame_api::RegisterTabardModelScriptMethods(state);
  game::frame_api::RegisterQuestPOIFrameScriptMethods(state);
  game::runtime::WorldLuaRuntime::InstallFrameRuntimeCallbacks(state);
}

void UninstallWorldWidgetSubtypes(lua_State* state, void*) {
  game::runtime::WorldLuaRuntime::UninstallFrameRuntimeCallbacks(state);
  game::frame_api::UnregisterQuestPOIFrameScriptMethods(state);
  game::frame_api::UnregisterTabardModelScriptMethods(state);
  game::frame_api::UnregisterDressUpModelScriptMethods(state);
  game::frame_api::UnregisterPlayerModelScriptMethods(state);
  game::frame_api::UnregisterMinimapScriptMethods(state);
  game::frame_api::UnregisterCooldownScriptMethods(state);
  game::frame_api::UnregisterGameTooltipScriptMethods(state);
}

NativeBindingCatalog SharedFrameScriptTypeLifecycle() {
  return {
      .owner = "ui.framescript.shared_types",
      .scope = BindingScope::kShared,
      .install = InstallSharedFrameScriptTypes,
      .uninstall = UninstallSharedFrameScriptTypes,

      .teardown_phase = NativeModuleTeardownPhase::kSharedFoundation,
  };
}

NativeBindingCatalog WorldWidgetSubtypeLifecycle() {
  return {
      .owner = "ui.framescript.world_widget_subtypes",
      .scope = BindingScope::kWorld,
      .install = InstallWorldWidgetSubtypes,
      .uninstall = UninstallWorldWidgetSubtypes,
      .teardown_phase = NativeModuleTeardownPhase::kDependent,
  };
}

NativeBindingCatalog WidgetTypeCatalog(const std::string_view owner,
                                       const BindingScope scope) {
  NativeBindingCatalog catalog{.owner = std::string(owner), .scope = scope};
  for (const auto& method : kRetailWidgetMethodSurface) {
    if (method.owner == owner) {
      catalog.widget_methods.emplace_back(method.name);
    }
  }
  return catalog;
}

template <std::size_t Size>
std::vector<NativeBindingCatalog> WidgetTypeCatalogs(
    const std::array<std::string_view, Size>& registration_order,
    const BindingScope scope) {
  std::vector<NativeBindingCatalog> catalogs;
  catalogs.reserve(registration_order.size() + 1);
  for (const auto owner : registration_order) {
    catalogs.push_back(WidgetTypeCatalog(owner, scope));
  }
  return catalogs;
}

}

std::vector<NativeBindingCatalog> SharedFrameScriptTypeModules() {
  static constexpr std::array<std::string_view, 26> kRegistrationOrder{
      "Font",         "Texture",       "FontString", "Object",
      "Region",       "Frame",         "Button",     "CheckButton",
      "EditBox",      "SimpleHTML",    "MessageFrame",
      "ScrollingMessageFrame",          "Model",      "ScrollFrame",
      "Slider",       "StatusBar",     "ColorSelect", "MovieFrame",
      "AnimationGroup",                 "Animation",  "Alpha",
      "ControlPoint", "Path",          "Rotation",    "Scale",
      "Translation",
  };
  auto modules = WidgetTypeCatalogs(kRegistrationOrder, BindingScope::kShared);
  modules.insert(modules.begin(), SharedFrameScriptTypeLifecycle());
  return modules;
}

std::vector<NativeBindingCatalog> WorldWidgetSubtypeModules() {
  static constexpr std::array<std::string_view, 7> kRegistrationOrder{
      "GameTooltip", "Cooldown", "Minimap", "PlayerModel",
      "DressUpModel", "TabardModel", "QuestPOIFrame",
  };
  auto modules = WidgetTypeCatalogs(kRegistrationOrder, BindingScope::kWorld);
  modules.insert(modules.begin(), WorldWidgetSubtypeLifecycle());
  return modules;
}

}
}

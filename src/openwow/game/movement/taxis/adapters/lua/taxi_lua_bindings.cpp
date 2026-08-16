#include "openwow/game/movement/taxis/adapters/lua/taxi_lua_bindings.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/ui/game/api/game_lua_api_taxi.h"

#include "openwow/ui/lua_binding_registry.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game {

openwow::ui::lua::NativeBindingCatalog TaxiNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.movement.taxis", openwow::ui::lua::BindingScope::kWorld, kTaxiMapLuaFunctions);
}

}

#include "openwow/ui/game/framescript/core/lua_script_object_access.h"

#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/lua_table_field.h"

namespace openwow::ui::game::lua_adapter {

void AttachScriptObjectIdentity(lua_State* lua, const int index,
                                void* native_object) {
  detail::AttachLuaScriptObjectThis(lua, index, native_object);
}

void InvalidateNativeScriptObjectIdentity(lua_State* lua, const int index) {
  if (lua == nullptr || lua_istable(lua, index) == 0) {
    return;
  }
  const int table = lua_absindex(lua, index);
  void* const native_object = detail::GetLuaNativeScriptObjectThisPointer(
      lua, table);
  if (native_object == nullptr) {
    return;
  }

  detail::PushScriptObjectThisLookupRegistry(lua);
  const int lookup = lua_absindex(lua, -1);
  lua_pushlightuserdata(lua, native_object);
  lua_pushnil(lua);
  lua_rawset(lua, lookup);
  lua_pop(lua, 1);

  void* const token = detail::EnsureLuaScriptObjectThisToken(lua, table);
  lua_pushlightuserdata(lua, token);
  lua_rawseti(lua, table, 0);
  detail::CacheLuaScriptObjectThisLookup(lua, table, token);
}

bool HasScriptObjectIdentity(lua_State* lua, const int index) {
  return detail::HasLuaScriptObjectThis(lua, index);
}

openwow::ui::widgets::ScriptObjectType CanonicalScriptObjectType(
    lua_State* lua, const int index) {
  (void)detail::CanonicalizeLuaScriptObjectTable(lua, index);
  return detail::GetAttachedLuaCanonicalScriptObjectType(lua, index);
}

bool IsScriptObjectKindOf(
    lua_State* lua, const int index,
    const openwow::ui::widgets::ScriptObjectType expected_type) {
  return detail::LuaScriptObjectIsKindOfCanonicalType(
      lua, index, expected_type);
}

bool HasCanonicalScriptObjectType(
    lua_State* lua, const int index,
    const openwow::ui::widgets::ScriptObjectType expected_type) {
  return detail::LuaScriptObjectHasCanonicalType(lua, index, expected_type);
}

openwow::ui::widgets::CScriptObject* BorrowNativeScriptObject(
    lua_State* lua, const int index) {
  return static_cast<openwow::ui::widgets::CScriptObject*>(
      detail::GetLuaNativeScriptObjectThisPointer(lua, index));
}

const char* ScriptObjectDisplayName(lua_State* lua, int index) {
  const char* name =
      openwow::ui::BorrowRawLuaStringField(lua, index, "__ow_name");
  return name != nullptr && *name != '\0' ? name : "<unnamed>";
}

}

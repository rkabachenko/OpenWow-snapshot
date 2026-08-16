#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/lua_table_field.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/foundation/text/ascii.h"
#include <lua.hpp>
#include <cstring>
namespace openwow::ui::game::frame_api {
bool IsLuaFrameObjectType(const char *type_name) {
  if (type_name == nullptr || *type_name == '\0') {
    return false;
  }

  return !openwow::text::EqualsIgnoreCaseAscii(type_name, "Texture") &&
         !openwow::text::EqualsIgnoreCaseAscii(type_name, "FontString") &&
         !openwow::text::EqualsIgnoreCaseAscii(type_name, "Line") &&
         !openwow::text::EqualsIgnoreCaseAscii(type_name, "Animation") &&
         !openwow::text::EqualsIgnoreCaseAscii(type_name, "AnimationGroup") &&
         !openwow::text::EqualsIgnoreCaseAscii(type_name, "Alpha") &&
         !openwow::text::EqualsIgnoreCaseAscii(type_name, "Path") &&
         !openwow::text::EqualsIgnoreCaseAscii(type_name, "Rotation") &&
         !openwow::text::EqualsIgnoreCaseAscii(type_name, "Scale") &&
         !openwow::text::EqualsIgnoreCaseAscii(type_name, "Translation") &&
         !openwow::text::EqualsIgnoreCaseAscii(type_name, "Font");
}

int ValidateFrameResizeSelf(lua_State *L) {
  openwow::ui::game::detail::ScopedNeutralLuaTaint neutral_taint(L);
  if (lua_istable(L, 1) == 0) {
    neutral_taint.Restore();
    return luaL_error(L, "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
  }

  (void)openwow::ui::game::detail::CanonicalizeLuaScriptObjectTable(L, 1);

  const char *type_name = openwow::ui::BorrowRawLuaStringField(L, 1, "__ow_type");
  if (type_name == nullptr || *type_name == '\0') {
    neutral_taint.Restore();
    return luaL_error(L, "Attempt to find 'this' in non-framescript object");
  }
  if (!IsLuaFrameObjectType(type_name)) {
    neutral_taint.Restore();
    return luaL_error(L, "Wrong object type for member function");
  }
  return lua_absindex(L, 1);
}

std::string GetFrameUsageObjectName(lua_State *L, int self_idx) {
  const char *name = openwow::ui::BorrowRawLuaStringField(L, self_idx, "__ow_name");
  if (name == nullptr || *name == '\0') {
    return "<unnamed>";
  }
  return std::string(name);
}

std::string GetFrameManagerKey(lua_State *L, int self_idx) {
  const char *key = GetFrameRuntimeKeyOrName(L, self_idx);
  return key != nullptr && *key != '\0' ? std::string(key) : std::string();
}

int ValidateTypedFramescriptSelf(lua_State *L, const char *expected_type) {
  using openwow::ui::widgets::ScriptObjectType;

  openwow::ui::game::detail::ScopedNeutralLuaTaint neutral_taint(L);
  if (lua_istable(L, 1) == 0) {
    neutral_taint.Restore();
    return luaL_error(L, "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
  }
  (void)openwow::ui::game::detail::CanonicalizeLuaScriptObjectTable(L, 1);

  const auto expected = openwow::ui::widgets::ScriptObjectTypeFromName(expected_type);
  const auto actual = openwow::ui::game::detail::GetLuaCanonicalScriptObjectType(L, 1);
  if (actual == ScriptObjectType::COUNT_) {
    neutral_taint.Restore();
    return luaL_error(L, "Attempt to find 'this' in non-framescript object");
  }
  if (expected == ScriptObjectType::COUNT_ || actual != expected) {
    neutral_taint.Restore();
    return luaL_error(L, "Wrong object type for member function");
  }
  return lua_absindex(L, 1);
}

std::string GetObjectNameOrUnnamed(lua_State *L, int self_idx) {
  const char *name = openwow::ui::BorrowRawLuaStringField(L, self_idx, "__ow_name");
  if (name == nullptr || *name == '\0') {
    return "<unnamed>";
  }
  return std::string(name);
}

openwow::ui::widgets::CSimpleFrame *GetLuaSimpleFrame(lua_State *L, int frame_index) {
  auto *script_object = static_cast<openwow::ui::widgets::CScriptObject *>(
      openwow::ui::game::detail::GetLuaNativeScriptObjectThisPointer(L, frame_index));
  return dynamic_cast<openwow::ui::widgets::CSimpleFrame *>(script_object);
}

}


#include "openwow/ui/widgets/script_object.h"

#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/foundation/text/ascii.h"

extern "C" {
#include <lua.hpp>
#include <lua.hpp>
}

#include <algorithm>
#include <array>
#include <cstring>

namespace openwow::ui::widgets {

namespace {

struct TypeNameEntry {
  ScriptObjectType type;
  const char* name;
};

constexpr std::array kTypeNames = {
    TypeNameEntry{ScriptObjectType::Object, "Object"},
    TypeNameEntry{ScriptObjectType::Region, "Region"},
    TypeNameEntry{ScriptObjectType::FontString, "FontString"},
    TypeNameEntry{ScriptObjectType::Texture, "Texture"},
    TypeNameEntry{ScriptObjectType::Line, "Line"},
    TypeNameEntry{ScriptObjectType::Frame, "Frame"},
    TypeNameEntry{ScriptObjectType::Button, "Button"},
    TypeNameEntry{ScriptObjectType::CheckButton, "CheckButton"},
    TypeNameEntry{ScriptObjectType::EditBox, "EditBox"},
    TypeNameEntry{ScriptObjectType::Slider, "Slider"},
    TypeNameEntry{ScriptObjectType::StatusBar, "StatusBar"},
    TypeNameEntry{ScriptObjectType::ScrollFrame, "ScrollFrame"},
    TypeNameEntry{ScriptObjectType::ScrollingMessageFrame,
                  "ScrollingMessageFrame"},
    TypeNameEntry{ScriptObjectType::MessageFrame, "MessageFrame"},
    TypeNameEntry{ScriptObjectType::SimpleHTML, "SimpleHTML"},
    TypeNameEntry{ScriptObjectType::ColorSelect, "ColorSelect"},
    TypeNameEntry{ScriptObjectType::Model, "Model"},
    TypeNameEntry{ScriptObjectType::PlayerModel, "PlayerModel"},
    TypeNameEntry{ScriptObjectType::DressUpModel, "DressUpModel"},
    TypeNameEntry{ScriptObjectType::TabardModel, "TabardModel"},
    TypeNameEntry{ScriptObjectType::Minimap, "Minimap"},
    TypeNameEntry{ScriptObjectType::GameTooltip, "GameTooltip"},
    TypeNameEntry{ScriptObjectType::Cooldown, "Cooldown"},
    TypeNameEntry{ScriptObjectType::MovieFrame, "MovieFrame"},
    TypeNameEntry{ScriptObjectType::WorldFrame, "WorldFrame"},
    TypeNameEntry{ScriptObjectType::QuestPOIFrame, "QuestPOIFrame"},
    TypeNameEntry{ScriptObjectType::AnimationGroup, "AnimationGroup"},
    TypeNameEntry{ScriptObjectType::Animation, "Animation"},
    TypeNameEntry{ScriptObjectType::Alpha, "Alpha"},
    TypeNameEntry{ScriptObjectType::Scale, "Scale"},
    TypeNameEntry{ScriptObjectType::Translation, "Translation"},
    TypeNameEntry{ScriptObjectType::Rotation, "Rotation"},
    TypeNameEntry{ScriptObjectType::Font, "Font"},
};

}

const char* ScriptObjectTypeName(ScriptObjectType type) noexcept {
  for (const auto& e : kTypeNames) {
    if (e.type == type) return e.name;
  }
  return "Unknown";
}

ScriptObjectType ScriptObjectTypeFromName(const std::string_view name) noexcept {
  for (const auto& e : kTypeNames) {
    if (openwow::text::EqualsIgnoreCaseAscii(name, e.name)) return e.type;
  }
  return ScriptObjectType::COUNT_;
}

const char* ResolveRegisteredCreateFrameTypeName(std::string_view name) noexcept {

  if (openwow::text::EqualsIgnoreCaseAscii(name, "ModelFFX")) {
    return "ModelFFX";
  }

  for (const auto& entry : kTypeNames) {
    if (IsScriptTypeKindOf(entry.type, ScriptObjectType::Frame) &&
        openwow::text::EqualsIgnoreCaseAscii(name, entry.name)) {
      return entry.name;
    }
  }
  return nullptr;
}

void CScriptObject::SetName(const std::string& name) {
  lua_State* L = game::ScriptEventDispatch::Get().GetLuaState();

  if (!name_.empty() && L != nullptr) {
    openwow::ui::UnregisterLuaGlobal(L, name_.c_str());
  }
  name_.clear();

  if (!name.empty()) {
    name_ = name;

    if (L != nullptr && luaRef_ != LUA_NOREF) {
      lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef_);
      (void)openwow::ui::PublishLuaGlobalValueIfNil(L, name_.c_str(), -1);
      lua_pop(L, 1);
    }
  }
}

CScriptObject::~CScriptObject() {
  lua_State* L = game::ScriptEventDispatch::Get().GetLuaState();

  if (L != nullptr) {

    if (!name_.empty()) {
      openwow::ui::UnregisterLuaGlobal(L, name_.c_str());
    }

    if (luaRef_ != LUA_NOREF) {
      luaL_unref(L, LUA_REGISTRYINDEX, luaRef_);
      luaRef_ = LUA_NOREF;
    }
  }
}

bool CScriptObject::IsTypeOf(const char* typeName) const noexcept {

  return StrCaseEq(typeName, "Object");
}

}

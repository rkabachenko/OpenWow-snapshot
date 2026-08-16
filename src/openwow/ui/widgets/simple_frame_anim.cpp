
#include "openwow/ui/widgets/simple_frame_anim.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/widgets/simple_frame.h"

extern "C" {
#include <lua.hpp>
}

#include "openwow/foundation/text/ascii.h"
#include "openwow/ui/xml/frame_xml_parser.h"
#include "openwow/ui/xml/xml_value_helpers.h"

#include <cstring>

namespace openwow::ui::widgets {

namespace {

void* FindNamedAnimationGroup(lua_State* state, const char* global_name) {
  if (state == nullptr || global_name == nullptr || global_name[0] == '\0') {
    return nullptr;
  }

  lua_getglobal(state, global_name);
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    return nullptr;
  }

  lua_getfield(state, -1, "__ow_type");
  const char* type_name = lua_tostring(state, -1);
  const bool is_animation_group =
      type_name != nullptr && std::strcmp(type_name, "AnimationGroup") == 0;
  lua_pop(state, 1);
  if (!is_animation_group) {
    lua_pop(state, 1);
    return nullptr;
  }

  lua_rawgeti(state, -1, 0);
  void* object = lua_touserdata(state, -1);
  lua_pop(state, 2);
  return object;
}

}

uint32_t CFrameAnimController::GetStrata() const noexcept {
  if (!owner_) return 0;
  return static_cast<uint32_t>(owner_->GetFrameStrata());
}

void CFrameAnimController::LoadAnimationsXML(const void* xmlNode,
                                             void* errorHandler) {
  const auto* frame_def = static_cast<const openwow::ui::xml::XMLFrameDef*>(xmlNode);
  if (!frame_def || !owner_) {
    return;
  }

  const std::string parent_key = frame_def->raw_node.GetAttr("parentKey");
  if (!parent_key.empty()) {
    lua_State* state = openwow::ui::game::ScriptEventDispatch::Get().GetLuaState();
    if (state != nullptr) {
      const std::string& frame_name = owner_->GetName();
      if (!frame_name.empty()) {
        lua_getglobal(state, frame_name.c_str());
        if (lua_istable(state, -1) != 0) {
          lua_getfield(state, -1, "__ow_parent");
          if (lua_istable(state, -1) != 0) {
            lua_pushvalue(state, -2);
            lua_setfield(state, -2, parent_key.c_str());
          }
          lua_pop(state, 1);
        }
        lua_pop(state, 1);
      }
    }
  }

  ProcessAnimationChildren(xmlNode, errorHandler);
}

void CFrameAnimController::ProcessAnimationChildren(const void* xmlNode,
                                                     void* errorHandler) {
  const auto* frame_def = static_cast<const openwow::ui::xml::XMLFrameDef*>(xmlNode);
  auto* error_handler = static_cast<openwow::ui::xml::ErrorContext*>(errorHandler);
  if (!frame_def) {
    return;
  }

  const auto* animations_node = frame_def->raw_node.FindChild("Animations");
  if (!animations_node) {
    return;
  }

  const char* frame_name = owner_ ? owner_->GetDisplayName() : "<unnamed>";

  for (const auto& child : animations_node->children) {
    if (!openwow::text::EqualsIgnoreCaseAscii(child.tag, "AnimationGroup")) {
      if (error_handler != nullptr) {
        error_handler->ReportError(
            "Frame %s: Unknown child node in %s element: %s",
            frame_name, "Animations", child.tag.c_str());
      }
    }
  }
}

void* CFrameAnimController::FindChildByName(const char* name) const {
  if (!name || !*name || !owner_) {
    return nullptr;
  }

  lua_State* state = openwow::ui::game::ScriptEventDispatch::Get().GetLuaState();
  if (state == nullptr) {
    return nullptr;
  }

  const std::string qualified_name = ExpandParentNameToken(*owner_, name);
  return FindNamedAnimationGroup(state, qualified_name.c_str());
}

}

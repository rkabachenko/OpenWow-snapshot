#include "openwow/ui/game/framescript/core/script_region_ownership.h"

#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/widgets/status_bar.h"

#include <lua.hpp>

#include <array>
#include <cmath>
#include <string_view>

namespace openwow::ui::game::frame_api {

void SynchronizeTextureRole(lua_State* lua, const int texture_index,
                            const openwow::ui::framexml::TextureRole role) {
  auto* manager = runtime::WorldUiRuntimeContext::FromLua(lua);
  if (manager == nullptr || lua_istable(lua, texture_index) == 0) {
    return;
  }
  const int texture = lua_absindex(lua, texture_index);
  const char* const runtime_key = GetFrameRuntimeKeyOrName(lua, texture);
  if (runtime_key == nullptr || runtime_key[0] == '\0') {
    return;
  }

  const char* owner_key = nullptr;
  lua_getfield(lua, texture, "__ow_parent");
  if (lua_istable(lua, -1) != 0) {
    owner_key = GetFrameRuntimeKeyOrName(lua, -1);
  }
  manager->frame_store().SetTextureRole(
      runtime_key, owner_key != nullptr ? owner_key : "", role,
      lua_topointer(lua, texture));
  lua_pop(lua, 1);
}

void DetachTrackedTextureRole(lua_State* lua, const int texture_index) {
  auto* manager = runtime::WorldUiRuntimeContext::FromLua(lua);
  if (manager == nullptr || lua_istable(lua, texture_index) == 0) {
    return;
  }
  const int texture = lua_absindex(lua, texture_index);
  const char* const runtime_key = GetFrameRuntimeKeyOrName(lua, texture);
  if (runtime_key != nullptr && runtime_key[0] != '\0') {
    manager->frame_store().SetTextureRole(
        runtime_key, {}, openwow::ui::framexml::TextureRole::Normal,
        lua_topointer(lua, texture));
  }
}

void ReleaseTextureOwnership(lua_State* lua, const int texture_index) {
  if (lua == nullptr || lua_istable(lua, texture_index) == 0) {
    return;
  }
  const int texture = lua_absindex(lua, texture_index);
  lua_getfield(lua, texture, "__ow_parent");
  if (lua_istable(lua, -1) != 0) {
    const int owner = lua_absindex(lua, -1);
    static constexpr std::array<const char*, 8> kNativeTextureSlots{
        "__ow_btn_normal_tex", "__ow_btn_pushed_tex",
        "__ow_btn_disabled_tex", "__ow_btn_highlight_tex",
        "__ow_checked_tex", "__ow_disabled_checked_tex", "__ow_sl_thumb",
        "__ow_sb_texture"};
    for (const char* slot : kNativeTextureSlots) {
      lua_getfield(lua, owner, slot);
      const bool occupies_slot = lua_rawequal(lua, -1, texture) != 0;
      lua_pop(lua, 1);
      if (!occupies_slot) {
        continue;
      }
      if (std::string_view(slot) == "__ow_sb_texture") {
        if (auto* status_bar = dynamic_cast<openwow::ui::widgets::StatusBar*>(
                lua_adapter::BorrowNativeScriptObject(lua, owner));
            status_bar != nullptr) {
          (void)status_bar->DetachTexture(lua_topointer(lua, texture));
        }
      }
      lua_pushnil(lua);
      lua_setfield(lua, owner, slot);
    }
  }
  lua_pop(lua, 1);
  DetachTrackedTextureRole(lua, texture);
  ReparentScriptObjectTable(lua, texture, 0);
}

void BindTextureOwnership(lua_State* lua, const int texture_index,
                          const int owner_index,
                          const openwow::ui::framexml::TextureRole role) {
  if (lua == nullptr || lua_istable(lua, texture_index) == 0 ||
      lua_istable(lua, owner_index) == 0) {
    return;
  }
  const int texture = lua_absindex(lua, texture_index);
  const int owner = lua_absindex(lua, owner_index);
  ReleaseTextureOwnership(lua, texture);
  ReparentScriptObjectTable(lua, texture, owner);
  SynchronizeTextureRole(lua, texture, role);
}

void SyncTrackedTextureColor(lua_State* lua, const int texture_index,
                             const float red, const float green,
                             const float blue, const float alpha) {
  auto* manager = runtime::WorldUiRuntimeContext::FromLua(lua);
  if (manager == nullptr || lua_istable(lua, texture_index) == 0) {
    return;
  }
  const char* const runtime_key = GetFrameRuntimeKeyOrName(lua, texture_index);
  if (runtime_key != nullptr && runtime_key[0] != '\0') {
    manager->frame_store().SetTextureColor(runtime_key, red, green, blue,
                                            alpha);
  }
}

bool ApplyTextureVertexColor(
    lua_State* lua, const int texture_index,
    const openwow::ui::widgets::StatusBarColor& color) {
  if (lua == nullptr || lua_istable(lua, texture_index) == 0) {
    return false;
  }
  const int texture = lua_absindex(lua, texture_index);
  lua_getfield(lua, texture, "SetVertexColor");
  if (lua_isfunction(lua, -1) == 0) {
    lua_pop(lua, 1);
    return false;
  }
  lua_pushvalue(lua, texture);
  lua_pushnumber(lua, color.red);
  lua_pushnumber(lua, color.green);
  lua_pushnumber(lua, color.blue);
  lua_pushnumber(lua, std::isfinite(color.alpha) ? color.alpha : 1.0F);
  if (lua_pcall(lua, 5, 0, 0) == 0) {
    return true;
  }
  lua_pop(lua, 1);
  return false;
}

bool ApplyStatusBarTextureRotation(lua_State* lua, const int texture_index,
                                   const bool rotates) {
  if (lua == nullptr || lua_istable(lua, texture_index) == 0) {
    return false;
  }
  static constexpr std::array<double, 8> kDefaultCoordinates{
      0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 1.0, 1.0};
  static constexpr std::array<double, 8> kRotatedCoordinates{
      0.0, 1.0, 1.0, 1.0, 0.0, 0.0, 1.0, 0.0};
  const auto& coordinates = rotates ? kRotatedCoordinates : kDefaultCoordinates;
  const int texture = lua_absindex(lua, texture_index);
  lua_getfield(lua, texture, "SetTexCoord");
  if (lua_isfunction(lua, -1) == 0) {
    lua_pop(lua, 1);
    return false;
  }
  lua_pushvalue(lua, texture);
  for (const double coordinate : coordinates) {
    lua_pushnumber(lua, coordinate);
  }
  if (lua_pcall(lua, 9, 0, 0) == 0) {
    return true;
  }
  lua_pop(lua, 1);
  return false;
}

void TrackRuntimeRegion(lua_State* lua, int owner_index, int region_index,
                        const char* kind, const char* lua_name,
                        const char* draw_layer,
                        const openwow::ui::framexml::TextureRole texture_role,
                        const bool set_all_points) {
  auto* manager = runtime::WorldUiRuntimeContext::FromLua(lua);
  if (manager == nullptr || lua_istable(lua, owner_index) == 0 ||
      lua_istable(lua, region_index) == 0) {
    return;
  }

  owner_index = lua_absindex(lua, owner_index);
  region_index = lua_absindex(lua, region_index);
  const char* parent_key = GetFrameRuntimeKeyOrName(lua, owner_index);

  openwow::ui::framexml::UiFrame frame;
  frame.kind = kind != nullptr ? kind : "Region";
  frame.name = lua_name != nullptr ? lua_name : "";
  frame.publish_to_lua = !frame.name.empty();
  frame.parent = parent_key != nullptr ? parent_key : "";
  frame.draw_layer = draw_layer != nullptr ? draw_layer : "";
  frame.texture_role = texture_role;
  frame.set_all_points = set_all_points;
  frame.visible = true;

  lua_pushvalue(lua, region_index);
  (void)manager->frame_materializer().AdoptRuntimeFrame(frame);
  lua_pop(lua, 1);
}

}

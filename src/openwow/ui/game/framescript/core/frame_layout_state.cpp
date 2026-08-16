#include "openwow/ui/game/framescript/core/frame_layout_state.h"
#include "openwow/ui/animation/animation_coordinate_space.h"
#include "openwow/ui/game/framescript/core/frame_alpha.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_base_methods.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/widgets/simple_frame.h"
#include <lua.hpp>
namespace openwow::ui::game::frame_api {
namespace {
constexpr const char* kGameTitleRegionField = "__ow_title_region";
constexpr const char* kFrameMaxResizeWidthField = "__ow_max_w";
}
std::uint8_t GetFrameAlphaByteOrDefault(lua_State *L, int index, std::uint8_t fallback) {
  index = lua_absindex(L, index);
  lua_getfield(L, index, "__ow_alpha");
  double alpha = openwow::ui::game::NormalizeFrameAlphaByte(fallback);
  if (lua_isnumber(L, -1) != 0) {
    alpha = lua_tonumber(L, -1);
  }
  lua_pop(L, 1);
  return openwow::ui::game::QuantizeFrameAlphaByteTruncated(alpha);
}

int PushStoredTitleRegion(lua_State *L, int frame_idx) {
  frame_idx = lua_absindex(L, frame_idx);
  lua_getfield(L, frame_idx, kGameTitleRegionField);
  if (lua_istable(L, -1) != 0) {
    return 1;
  }
  lua_pop(L, 1);
  lua_pushnil(L);
  return 1;
}

int PushOrCreateTitleRegion(lua_State *L, int frame_idx) {
  frame_idx = lua_absindex(L, frame_idx);
  lua_getfield(L, frame_idx, kGameTitleRegionField);
  if (lua_istable(L, -1) != 0) {
    return 1;
  }
  lua_pop(L, 1);

  lua_newtable(L);
  const int region = lua_absindex(L, -1);
  lua_pushstring(L, "Region");
  lua_setfield(L, region, "__ow_type");
  openwow::ui::game::lua_adapter::AttachScriptObjectIdentity(L, region);
  lua_pushboolean(L, 1);
  lua_setfield(L, region, "__ow_visible");
  lua_pushvalue(L, frame_idx);
  lua_setfield(L, region, "__ow_parent");
  ApplyBaseFrameMethods(L);
  lua_pushvalue(L, region);
  lua_setfield(L, frame_idx, kGameTitleRegionField);
  return 1;
}

void SyncTrackedFrameUserPlaced(lua_State *L, int self_idx, bool user_placed) {
  auto *manager = runtime::WorldUiRuntimeContext::FromLua(L);
  if (manager == nullptr) {
    return;
  }
  manager->frame_store().SetFrameUserPlaced(GetFrameManagerKey(L, self_idx),
                                             user_placed);
}

void SyncTrackedFrameDontSavePosition(lua_State *L, int self_idx, bool dont_save_position) {
  auto *manager = runtime::WorldUiRuntimeContext::FromLua(L);
  if (manager == nullptr) {
    return;
  }
  manager->frame_store().SetFrameDontSavePosition(
      GetFrameManagerKey(L, self_idx), dont_save_position);
}

bool BeginTrackedFrameMoveSizing(lua_State *L, int self_idx, const std::string &frame_name,
                                 const int mode) {
  auto *manager = runtime::WorldUiRuntimeContext::FromLua(L);
  if (manager == nullptr ||
      !manager->input_router().BeginFrameMoveSizing(frame_name, mode)) {
    return false;
  }

  lua_pushboolean(L, 1);
  lua_setfield(L, self_idx, "__ow_user_placed");
  SyncTrackedFrameUserPlaced(L, self_idx, true);
  return true;
}

void SyncTrackedRegionDrawLayer(lua_State *L, int self_idx,
                                const char *canonical_layer) {
  auto *manager = runtime::WorldUiRuntimeContext::FromLua(L);
  if (manager == nullptr || canonical_layer == nullptr) {
    return;
  }
  manager->frame_store().SetRegionDrawLayer(GetFrameManagerKey(L, self_idx),
                                             canonical_layer);
}

double GetStoredResizeBound(lua_State *L, int self_idx, const char *field_name) {
  self_idx = lua_absindex(L, self_idx);
  lua_getfield(L, self_idx, field_name);
  const double value = lua_isnumber(L, -1) != 0 ? lua_tonumber(L, -1) : 0.0;
  lua_pop(L, 1);
  return value;
}

void SetStoredResizeBound(lua_State *L, int self_idx, const char *field_name, const double value) {
  self_idx = lua_absindex(L, self_idx);
  lua_pushnumber(L, value);
  lua_setfield(L, self_idx, field_name);
}

int SetFrameResizeBounds(lua_State *L, const char *width_field, const char *height_field,
                         const char *usage_format) {
  const int self_idx = ValidateFrameResizeSelf(L);
  if (lua_isnumber(L, 2) == 0 || lua_isnumber(L, 3) == 0) {
    return luaL_error(L, usage_format, GetFrameUsageObjectName(L, self_idx).c_str());
  }

  const float width_pixels = static_cast<float>(lua_tonumber(L, 2));
  const float height_pixels = static_cast<float>(lua_tonumber(L, 3));
  const float stored_width = openwow::ui::anim::PixelAnimationOffsetToStored(width_pixels);
  const float stored_height = openwow::ui::anim::PixelAnimationOffsetToStored(height_pixels);
  SetStoredResizeBound(L, self_idx, width_field, static_cast<double>(stored_width));
  SetStoredResizeBound(L, self_idx, height_field, static_cast<double>(stored_height));

  auto *script_object = static_cast<openwow::ui::widgets::CScriptObject *>(
      openwow::ui::game::detail::GetLuaNativeScriptObjectThisPointer(L, self_idx));
  if (auto *frame = dynamic_cast<openwow::ui::widgets::CSimpleFrame *>(script_object);
      frame != nullptr) {
    if (width_field == kFrameMaxResizeWidthField) {
      frame->SetMaxResize(stored_width, stored_height);
    } else {
      frame->SetMinResize(stored_width, stored_height);
    }
  }
  return 0;
}

int GetFrameResizeBounds(lua_State *L, const char *width_field, const char *height_field) {
  const int self_idx = ValidateFrameResizeSelf(L);
  const float stored_width = static_cast<float>(GetStoredResizeBound(L, self_idx, width_field));
  const float stored_height = static_cast<float>(GetStoredResizeBound(L, self_idx, height_field));
  lua_pushnumber(
      L, static_cast<lua_Number>(openwow::ui::anim::StoredAnimationOffsetToPixels(stored_width)));
  lua_pushnumber(
      L, static_cast<lua_Number>(openwow::ui::anim::StoredAnimationOffsetToPixels(stored_height)));
  return 2;
}

void SetLuaHitRectInsetField(lua_State *L, int frame_index, const char *field_name,
                             const lua_Number value) {
  frame_index = lua_absindex(L, frame_index);
  lua_pushnumber(L, value);
  lua_setfield(L, frame_index, field_name);
}

lua_Number ReadLuaHitRectInsetField(lua_State *L, int frame_index, const char *field_name) {
  frame_index = lua_absindex(L, frame_index);
  lua_getfield(L, frame_index, field_name);
  const lua_Number value = lua_isnumber(L, -1) != 0 ? lua_tonumber(L, -1) : 0.0;
  lua_pop(L, 1);
  return value;
}

}

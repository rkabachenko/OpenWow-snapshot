#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/core/frame_anchor_runtime.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/framexml/layout_anchor_resolution.h"
#include "openwow/ui/game/runtime/lua_interned_field_key.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/foundation/text/ascii.h"

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::ui::game::frame_api {

openwow::ui::widgets::ScriptObjectType GetLuaScriptObjectType(
    lua_State* L, int index) {
  const char* type_name =
      runtime::BorrowInternedLuaStringField(L, index, "__ow_type");
  if (type_name == nullptr || *type_name == '\0') {
    return openwow::ui::widgets::ScriptObjectType::COUNT_;
  }
  if (std::strcmp(type_name, "ModelFFX") == 0) {
    return openwow::ui::widgets::ScriptObjectType::Model;
  }
  if (std::strcmp(type_name, "QuestPOIFrame") == 0) {
    return openwow::ui::widgets::ScriptObjectType::Frame;
  }
  return openwow::ui::widgets::ScriptObjectTypeFromName(type_name);
}

bool IsFrameLikeScriptObjectType(
    const openwow::ui::widgets::ScriptObjectType type) {
  using openwow::ui::widgets::ScriptObjectType;

  switch (type) {
    case ScriptObjectType::Frame:
    case ScriptObjectType::Button:
    case ScriptObjectType::CheckButton:
    case ScriptObjectType::EditBox:
    case ScriptObjectType::Slider:
    case ScriptObjectType::StatusBar:
    case ScriptObjectType::ScrollFrame:
    case ScriptObjectType::ScrollingMessageFrame:
    case ScriptObjectType::MessageFrame:
    case ScriptObjectType::SimpleHTML:
    case ScriptObjectType::ColorSelect:
    case ScriptObjectType::Model:
    case ScriptObjectType::PlayerModel:
    case ScriptObjectType::DressUpModel:
    case ScriptObjectType::TabardModel:
    case ScriptObjectType::Minimap:
    case ScriptObjectType::GameTooltip:
    case ScriptObjectType::Cooldown:
    case ScriptObjectType::MovieFrame:
    case ScriptObjectType::WorldFrame:
      return true;
    default:
      return false;
  }
}

ParentLinkArrayKind GetParentLinkArrayKind(
    const openwow::ui::widgets::ScriptObjectType type) {
  using openwow::ui::widgets::ScriptObjectType;

  switch (type) {
    case ScriptObjectType::FontString:
    case ScriptObjectType::Texture:
    case ScriptObjectType::Line:
      return ParentLinkArrayKind::Regions;
    default:
      return IsFrameLikeScriptObjectType(type) ? ParentLinkArrayKind::Children
                                               : ParentLinkArrayKind::None;
  }
}

static bool IsAnchorableScriptObjectType(
    const openwow::ui::widgets::ScriptObjectType type) {
  using openwow::ui::widgets::ScriptObjectType;

  switch (type) {
    case ScriptObjectType::Region:
    case ScriptObjectType::Texture:
    case ScriptObjectType::FontString:
    case ScriptObjectType::Line:
      return true;
    default:
      return IsFrameLikeScriptObjectType(type);
  }
}

static bool IsValidLuaAnchorTargetTable(lua_State* L, const int index,
                                        const LuaAnchorTargetValidation validation) {
  if (lua_istable(L, index) == 0) {
    return false;
  }

  if (validation == LuaAnchorTargetValidation::kRequireScriptObjectThis &&
      !openwow::ui::game::detail::HasLuaScriptObjectThis(L, index)) {
    return false;
  }

  return IsAnchorableScriptObjectType(GetLuaScriptObjectType(L, index));
}

static void PushLuaGlobalByNameValue(lua_State* L) {
  lua_gettable(L, LUA_GLOBALSINDEX);
}

static int AcceptPushedLuaAnchorTarget(
    lua_State* L, const LuaAnchorTargetValidation validation) {
  if (!IsValidLuaAnchorTargetTable(L, -1, validation)) {
    lua_pop(L, 1);
    return 0;
  }

  return lua_absindex(L, -1);
}

static int PushNamedLuaAnchorTarget(lua_State* L, const char* object_name,
                                    const LuaAnchorTargetValidation validation) {
  lua_pushstring(L, object_name != nullptr ? object_name : "");
  PushLuaGlobalByNameValue(L);
  return AcceptPushedLuaAnchorTarget(L, validation);
}

static int PushInternedNamedLuaAnchorTarget(
    lua_State* L, const char* literal_name,
    const LuaAnchorTargetValidation validation) {
  runtime::PushInternedLuaString(L, literal_name);
  PushLuaGlobalByNameValue(L);
  return AcceptPushedLuaAnchorTarget(L, validation);
}

static bool PushResolvedLuaAnchorTarget(lua_State* L, const int value_index,
                                        const LuaAnchorTargetValidation validation) {
  const int absolute_index = lua_absindex(L, value_index);
  if (lua_isstring(L, absolute_index) != 0) {

    (void)lua_tostring(L, absolute_index);
    lua_pushvalue(L, absolute_index);
    PushLuaGlobalByNameValue(L);
    return AcceptPushedLuaAnchorTarget(L, validation) != 0;
  }

  lua_pushvalue(L, absolute_index);
  (void)openwow::ui::game::detail::CanonicalizeLuaScriptObjectTable(L, -1);
  if (!IsValidLuaAnchorTargetTable(L, -1, validation)) {
    lua_pop(L, 1);
    return false;
  }
  return true;
}

static int PushLuaExplicitParentAnchorTarget(
    lua_State* L, const int self_index,
    const LuaAnchorTargetValidation validation) {
  runtime::GetInternedLuaField(L, self_index, "__ow_parent");
  if (PushResolvedLuaAnchorTarget(L, -1, validation)) {
    lua_remove(L, -2);
    return lua_absindex(L, -1);
  }

  lua_pop(L, 1);
  return 0;
}

static int PushNamedLuaAnchorTargetForOwner(
    lua_State* L, const int self_index, const char* object_name,
    const LuaAnchorTargetValidation validation,
    bool* resolved_parent_token = nullptr) {
  if (resolved_parent_token != nullptr) {
    *resolved_parent_token = false;
  }

  constexpr std::string_view kParentToken = "$parent";
  const std::string_view requested_name =
      object_name != nullptr ? std::string_view(object_name) : std::string_view{};
  if (requested_name.size() < kParentToken.size() ||
      !openwow::text::EqualsIgnoreCaseAscii(
          requested_name.substr(0, kParentToken.size()), kParentToken)) {
    return PushNamedLuaAnchorTarget(L, object_name, validation);
  }

  runtime::GetInternedLuaField(L, self_index, "__ow_parent");
  for (int depth = 0; depth < 256; ++depth) {
    if (!IsValidLuaAnchorTargetTable(L, -1, validation)) {
      lua_pop(L, 1);
      return 0;
    }

    const int parent_index = lua_absindex(L, -1);
    const char* parent_name =
        runtime::BorrowInternedLuaStringField(L, parent_index, "__ow_name");
    if (parent_name != nullptr && *parent_name != '\0') {
      std::string expanded_name(parent_name);
      expanded_name.append(requested_name.substr(kParentToken.size()));
      lua_pop(L, 1);
      const int resolved_index =
          PushNamedLuaAnchorTarget(L, expanded_name.c_str(), validation);
      if (resolved_index != 0 && resolved_parent_token != nullptr) {
        *resolved_parent_token = true;
      }
      return resolved_index;
    }

    runtime::GetInternedLuaField(L, parent_index, "__ow_parent");
    lua_remove(L, parent_index);
  }

  lua_pop(L, 1);
  return 0;
}

static bool LuaAnchorTargetDependsOn(lua_State* L, const int target_index, const int query_index,
                                     const LuaAnchorTargetValidation validation) {
  const int original_top = lua_gettop(L);
  const int absolute_query_index = lua_absindex(L, query_index);
  const openwow::ui::game::detail::ScopedNeutralLuaTaint neutral_taint(L);
  openwow::ui::game::detail::LuaTableGraphWorklist worklist(L);
  (void)worklist.Enqueue(target_index);

  while (worklist.PushNext()) {
    const int current_index = lua_absindex(L, -1);
    runtime::GetInternedLuaField(L, current_index, "__ow_anchors");
    if (lua_istable(L, -1) != 0) {
      const int anchors_index = lua_absindex(L, -1);
      const lua_Integer anchor_count = luaL_len(L, anchors_index);
      for (lua_Integer i = 1; i <= anchor_count; ++i) {
        lua_rawgeti(L, anchors_index, i);
        if (lua_istable(L, -1) != 0 && !LuaAnchorIsHidden(L, -1)) {
          runtime::GetInternedLuaField(L, -1, "relativeTo");
          const int stored_relative_index = lua_absindex(L, -1);
          const bool resolved = PushResolvedLuaAnchorTarget(L, -1, validation);
          lua_remove(L, stored_relative_index);
          if (resolved) {
            if (lua_rawequal(L, -1, absolute_query_index) != 0) {
              lua_settop(L, original_top);
              return true;
            }
            (void)worklist.Enqueue(-1);
            lua_pop(L, 1);
          }
        }
        lua_pop(L, 1);
      }
    }
    lua_settop(L, current_index - 1);
  }

  lua_settop(L, original_top);
  return false;
}

constexpr float kNativeAnchorOffsetChangeEpsilon = 2.3841858e-07F;

static bool LuaAnchorSlotAlreadyHolds(lua_State* L, const int self_index,
                                      const int point_slot,
                                      const int relative_index,
                                      const int relative_point_slot,
                                      const float offset_x,
                                      const float offset_y,
                                      const LuaAnchorTargetValidation validation) {
  const int original_top = lua_gettop(L);
  const int absolute_relative_index = lua_absindex(L, relative_index);
  runtime::GetInternedLuaField(L, self_index, "__ow_anchors");
  if (lua_istable(L, -1) == 0) {
    lua_settop(L, original_top);
    return false;
  }
  const int anchors_index = lua_absindex(L, -1);
  const lua_Integer anchor_count = luaL_len(L, anchors_index);
  int anchor_index = 0;
  for (lua_Integer i = 1; i <= anchor_count; ++i) {
    lua_rawgeti(L, anchors_index, i);
    if (lua_istable(L, -1) != 0 && GetLuaAnchorPointSlot(L, -1) == point_slot) {
      anchor_index = lua_absindex(L, -1);
      break;
    }
    lua_pop(L, 1);
  }
  if (anchor_index == 0) {
    lua_settop(L, original_top);
    return false;
  }

  bool same = false;
  runtime::GetInternedLuaField(L, anchor_index, "relativePoint");
  const int stored_relative_point_slot =
      lua_isstring(L, -1) != 0
          ? openwow::ui::framexml::detail::FramePointSlotOrInvalidExact(
                lua_tostring(L, -1))
          : -1;
  lua_pop(L, 1);
  if (stored_relative_point_slot >= 0 &&
      stored_relative_point_slot == relative_point_slot) {
    runtime::GetInternedLuaField(L, anchor_index, "x");
    runtime::GetInternedLuaField(L, anchor_index, "y");
    if (lua_isnumber(L, -2) != 0 && lua_isnumber(L, -1) != 0) {
      const float stored_x = static_cast<float>(lua_tonumber(L, -2));
      const float stored_y = static_cast<float>(lua_tonumber(L, -1));
      const float dx = std::fabs(
          openwow::ui::PixelUiHorizontalCoordinateToStored(stored_x) -
          openwow::ui::PixelUiHorizontalCoordinateToStored(offset_x));
      const float dy = std::fabs(
          openwow::ui::PixelUiHorizontalCoordinateToStored(stored_y) -
          openwow::ui::PixelUiHorizontalCoordinateToStored(offset_y));
      if (dx < kNativeAnchorOffsetChangeEpsilon &&
          dy < kNativeAnchorOffsetChangeEpsilon) {
        lua_pop(L, 2);

        runtime::GetInternedLuaField(L, anchor_index, "relativeTo");
        if (PushResolvedLuaAnchorTarget(L, -1, validation)) {
          same = lua_rawequal(L, -1, absolute_relative_index) != 0;
        }
      }
    }
  }
  lua_settop(L, original_top);
  return same;
}

static void ReplaceLuaAnchorForPointSlot(lua_State* L, const int anchors_index,
                                         const int anchor_index, const int point_slot) {
  const int absolute_anchors_index = lua_absindex(L, anchors_index);
  const int absolute_anchor_index = lua_absindex(L, anchor_index);

  NormalizeLuaAnchorArray(L, absolute_anchors_index);
  const lua_Integer anchor_count = luaL_len(L, absolute_anchors_index);
  lua_Integer replace_index = 0;
  for (lua_Integer i = 1; i <= anchor_count; ++i) {
    lua_rawgeti(L, absolute_anchors_index, i);
    if (lua_istable(L, -1) != 0 && GetLuaAnchorPointSlot(L, -1) == point_slot) {
      replace_index = i;
      lua_pop(L, 1);
      break;
    }
    lua_pop(L, 1);
  }

  lua_pushvalue(L, absolute_anchor_index);
  lua_rawseti(L, absolute_anchors_index, replace_index != 0 ? replace_index : anchor_count + 1);
  NormalizeLuaAnchorArray(L, absolute_anchors_index);
}

enum class LuaAllPointsRelativeStorage : std::uint8_t {
  kOriginalArgument,
  kResolvedTarget,
  kUIParent,
};

static int PushLuaFallbackRelativeAnchorTarget(
    lua_State* L, const int self_index, const LuaAnchorTargetValidation validation) {
  if (const int parent_index =
          PushLuaExplicitParentAnchorTarget(L, self_index, validation);
      parent_index != 0) {
    return parent_index;
  }

  return PushInternedNamedLuaAnchorTarget(L, "UIParent", validation);
}

static void PushLuaStoredAllPointsRelativeValue(
    lua_State* L, const int relative_argument_index, const int resolved_relative_index,
    const LuaAllPointsRelativeStorage storage) {
  switch (storage) {
    case LuaAllPointsRelativeStorage::kOriginalArgument:
      lua_pushvalue(L, lua_absindex(L, relative_argument_index));
      return;
    case LuaAllPointsRelativeStorage::kResolvedTarget:
      if (resolved_relative_index != 0) {
        lua_pushvalue(L, lua_absindex(L, resolved_relative_index));
        return;
      }
      [[fallthrough]];
    case LuaAllPointsRelativeStorage::kUIParent:
      runtime::PushInternedLuaString(L, "UIParent");
      return;
  }
}

static void StoreLuaAllPointsAnchors(lua_State* L, const int self_index,
                                     const int relative_argument_index,
                                     const int resolved_relative_index,
                                     const LuaAllPointsRelativeStorage storage) {
  const int absolute_self_index = lua_absindex(L, self_index);

  lua_newtable(L);
  const int anchors_index = lua_absindex(L, -1);

  const auto push_anchor = [&](const char* point_name, const lua_Integer array_index) {
    lua_newtable(L);
    const int anchor_index = lua_absindex(L, -1);
    runtime::PushInternedLuaString(L, point_name);
    runtime::SetInternedLuaField(L, anchor_index, "point");
    PushLuaStoredAllPointsRelativeValue(
        L, relative_argument_index, resolved_relative_index, storage);
    runtime::SetInternedLuaField(L, anchor_index, "relativeTo");
    runtime::PushInternedLuaString(L, point_name);
    runtime::SetInternedLuaField(L, anchor_index, "relativePoint");
    lua_pushnumber(L, 0);
    runtime::SetInternedLuaField(L, anchor_index, "x");
    lua_pushnumber(L, 0);
    runtime::SetInternedLuaField(L, anchor_index, "y");
    lua_rawseti(L, anchors_index, array_index);
  };

  push_anchor("TOPLEFT", 1);
  push_anchor("BOTTOMRIGHT", 2);
  runtime::SetInternedLuaField(L, absolute_self_index, "__ow_anchors");
  lua_pushboolean(L, 1);
  runtime::SetInternedLuaField(L, absolute_self_index, "__ow_setAllPoints");
}

int LuaClearAllPointsInternal(lua_State* L,
                                     const LuaAnchorTargetValidation validation) {
  const int self_index =
      validation == LuaAnchorTargetValidation::kRequireScriptObjectThis
          ? ValidateFrameScriptSelf(L)
          : (lua_istable(L, 1) != 0 ? lua_absindex(L, 1) : 0);
  if (self_index == 0) {
    return 0;
  }

  if (!detail::AllowLuaFrameProtectedMutation(L, self_index)) {
    return 0;
  }

  lua_newtable(L);
  runtime::SetInternedLuaField(L, self_index, "__ow_anchors");
  lua_pushnil(L);
  runtime::SetInternedLuaField(L, self_index, "__ow_setAllPoints");

  openwow::ui::game::detail::ReindexLuaAnchorDependents(L, self_index);
  NotifyFrameInputMutation(L, self_index, false);
  return 0;
}

int LuaSetPointInternal(lua_State* L, const LuaAnchorTargetValidation validation) {
  const int self_index =
      validation == LuaAnchorTargetValidation::kRequireScriptObjectThis
          ? ValidateFrameScriptSelf(L)
          : (lua_istable(L, 1) != 0 ? lua_absindex(L, 1) : 0);
  if (self_index == 0) {
    return 0;
  }

  if (!detail::AllowLuaFrameProtectedMutation(L, self_index)) {
    return 0;
  }

  if (lua_isstring(L, 2) == 0) {
    return luaL_error(
        L, "Usage: %s:SetPoint(\"point\" [, region or nil] [, \"relativePoint\"] [, offsetX, offsetY])",
        lua_adapter::ScriptObjectDisplayName(L, self_index));
  }

  const char* point_name = lua_tostring(L, 2);
  const int point_slot =
      openwow::ui::framexml::detail::FramePointSlotOrInvalidExact(point_name);
  if (point_slot < 0) {
    return luaL_error(L, "%s:SetPoint(): Unknown region point",
                      lua_adapter::ScriptObjectDisplayName(L, self_index));
  }

  const int arg3_type = lua_type(L, 3);
  const bool explicit_relative_table = arg3_type == LUA_TTABLE;
  const bool explicit_relative_name = arg3_type == LUA_TSTRING;

  const bool default_to_ui_parent = arg3_type == LUA_TNIL;
  const bool default_to_frame_parent =
      !explicit_relative_table && !explicit_relative_name && !default_to_ui_parent;
  int relative_index = 0;
  bool store_resolved_relative = false;

  if (explicit_relative_name) {
    bool resolved_parent_token = false;
    if (PushNamedLuaAnchorTargetForOwner(
            L, self_index, lua_tostring(L, 3), validation,
            &resolved_parent_token) == 0) {
      const char* target_name = lua_tostring(L, 3);
      return luaL_error(L, "%s:SetPoint(): Couldn't find region named '%s'",
                        lua_adapter::ScriptObjectDisplayName(L, self_index),
                        target_name != nullptr ? target_name : "");
    }
    relative_index = lua_absindex(L, -1);
    store_resolved_relative = resolved_parent_token;
  } else if (explicit_relative_table) {
    if (!PushResolvedLuaAnchorTarget(L, 3, validation)) {
      const char* target_name = lua_tostring(L, 3);
      return luaL_error(L, "%s:SetPoint(): Couldn't find region named '%s'",
                        lua_adapter::ScriptObjectDisplayName(L, self_index),
                        target_name != nullptr ? target_name : "");
    }
    relative_index = lua_absindex(L, -1);
  } else if (default_to_ui_parent) {
    relative_index = PushInternedNamedLuaAnchorTarget(L, "UIParent", validation);
  } else {
    relative_index =
        PushLuaFallbackRelativeAnchorTarget(L, self_index, validation);
    store_resolved_relative = true;
  }

  const char* self_name =
      runtime::BorrowInternedLuaStringField(L, self_index, "__ow_name");
  if ((relative_index != 0 && lua_rawequal(L, relative_index, self_index) != 0) ||
      (default_to_ui_parent && self_name != nullptr && std::strcmp(self_name, "UIParent") == 0)) {
    if (relative_index != 0) {
      lua_pop(L, 1);
    }
    return luaL_error(L, "%s:SetPoint(): trying to anchor to itself",
                      lua_adapter::ScriptObjectDisplayName(L, self_index));
  }

  if (relative_index != 0 &&
      LuaAnchorTargetDependsOn(L, relative_index, self_index, validation)) {
    const char* relative_name =
        lua_adapter::ScriptObjectDisplayName(L, relative_index);
    return luaL_error(L, "%s:SetPoint(): %s is dependent on this",
                      lua_adapter::ScriptObjectDisplayName(L, self_index),
                      relative_name);
  }

  int relative_point_slot = point_slot;
  int coordinate_arg_index = default_to_frame_parent ? 3 : 4;
  if (lua_type(L, coordinate_arg_index) == LUA_TSTRING) {
    const int parsed = openwow::ui::framexml::detail::FramePointSlotOrInvalidExact(
        lua_tostring(L, coordinate_arg_index));
    if (parsed < 0) {
      if (relative_index != 0) {
        lua_pop(L, 1);
      }
      return luaL_error(L, "%s:SetPoint(): Unknown region point",
                        lua_adapter::ScriptObjectDisplayName(L, self_index));
    }
    relative_point_slot = parsed;
    ++coordinate_arg_index;
  }

  float offset_x = 0.0f;
  float offset_y = 0.0f;
  if (lua_isnumber(L, coordinate_arg_index) != 0 &&
      lua_isnumber(L, coordinate_arg_index + 1) != 0) {
    offset_x = static_cast<float>(lua_tonumber(L, coordinate_arg_index));
    offset_y = static_cast<float>(lua_tonumber(L, coordinate_arg_index + 1));
  }

  if (relative_index != 0 &&
      LuaAnchorSlotAlreadyHolds(L, self_index, point_slot, relative_index,
                                relative_point_slot, offset_x, offset_y,
                                validation)) {
    lua_pop(L, 1);
    return 0;
  }

  const int anchors_index = EnsureLuaAnchorArray(L, self_index);
  lua_newtable(L);
  const int anchor_index = lua_absindex(L, -1);

  runtime::PushInternedLuaString(L,
                                 openwow::ui::FramePointToString(point_slot));
  runtime::SetInternedLuaField(L, anchor_index, "point");
  if (store_resolved_relative && relative_index != 0) {
    lua_pushvalue(L, relative_index);
    runtime::SetInternedLuaField(L, anchor_index, "relativeTo");
  } else if (explicit_relative_table && relative_index != 0) {
    lua_pushvalue(L, relative_index);
    runtime::SetInternedLuaField(L, anchor_index, "relativeTo");
  } else if (explicit_relative_name) {
    lua_pushvalue(L, 3);
    runtime::SetInternedLuaField(L, anchor_index, "relativeTo");
  } else {
    runtime::PushInternedLuaString(L, "UIParent");
    runtime::SetInternedLuaField(L, anchor_index, "relativeTo");
  }
  runtime::PushInternedLuaString(
      L, openwow::ui::FramePointToString(relative_point_slot));
  runtime::SetInternedLuaField(L, anchor_index, "relativePoint");
  lua_pushnumber(L, offset_x);
  runtime::SetInternedLuaField(L, anchor_index, "x");
  lua_pushnumber(L, offset_y);
  runtime::SetInternedLuaField(L, anchor_index, "y");

  lua_pushnil(L);
  runtime::SetInternedLuaField(L, self_index, "__ow_setAllPoints");
  ReplaceLuaAnchorForPointSlot(L, anchors_index, anchor_index, point_slot);
  lua_pop(L, 2);
  if (relative_index != 0) {
    lua_pop(L, 1);
  }

  openwow::ui::game::detail::ReindexLuaAnchorDependents(L, self_index);
  NotifyFrameInputMutation(L, self_index, false);
  return 0;
}

int LuaSetAllPointsInternal(lua_State* L, const LuaAnchorTargetValidation validation) {
  const int self_index =
      validation == LuaAnchorTargetValidation::kRequireScriptObjectThis
          ? ValidateFrameScriptSelf(L)
          : (lua_istable(L, 1) != 0 ? lua_absindex(L, 1) : 0);
  if (self_index == 0) {
    return 0;
  }

  if (!detail::AllowLuaFrameProtectedMutation(L, self_index)) {
    return 0;
  }

  const int arg2_type = lua_type(L, 2);
  const bool explicit_relative_table = arg2_type == LUA_TTABLE;
  const bool explicit_relative_name = arg2_type == LUA_TSTRING;

  const bool default_to_screen_root = arg2_type == LUA_TNIL;
  const bool default_to_parent = arg2_type == LUA_TNONE;

  int relative_index = 0;
  LuaAllPointsRelativeStorage storage = LuaAllPointsRelativeStorage::kResolvedTarget;

  if (explicit_relative_name) {
    bool resolved_parent_token = false;
    if (PushNamedLuaAnchorTargetForOwner(
            L, self_index, lua_tostring(L, 2), validation,
            &resolved_parent_token) == 0) {
      const char* target_name = lua_tostring(L, 2);
      return luaL_error(L, "%s:SetAllPoints(): Couldn't find region named '%s'",
                        lua_adapter::ScriptObjectDisplayName(L, self_index),
                        target_name != nullptr ? target_name : "");
    }
    relative_index = lua_absindex(L, -1);
    storage = resolved_parent_token ? LuaAllPointsRelativeStorage::kResolvedTarget
                                    : LuaAllPointsRelativeStorage::kOriginalArgument;
  } else if (explicit_relative_table) {
    if (!PushResolvedLuaAnchorTarget(L, 2, validation)) {
      const char* target_name = lua_tostring(L, 2);
      return luaL_error(L, "%s:SetAllPoints(): Couldn't find region named '%s'",
                        lua_adapter::ScriptObjectDisplayName(L, self_index),
                        target_name != nullptr ? target_name : "");
    }
    relative_index = lua_absindex(L, -1);
    storage = LuaAllPointsRelativeStorage::kResolvedTarget;
  } else if (default_to_screen_root) {

    relative_index = PushInternedNamedLuaAnchorTarget(L, "UIParent", validation);
    storage = LuaAllPointsRelativeStorage::kUIParent;
  } else if (default_to_parent) {

    relative_index = PushLuaFallbackRelativeAnchorTarget(L, self_index, validation);
    storage = relative_index != 0 ? LuaAllPointsRelativeStorage::kResolvedTarget
                                  : LuaAllPointsRelativeStorage::kUIParent;
  } else {
    relative_index = PushLuaFallbackRelativeAnchorTarget(L, self_index, validation);
    storage = relative_index != 0 ? LuaAllPointsRelativeStorage::kResolvedTarget
                                  : LuaAllPointsRelativeStorage::kUIParent;
  }

  const char* self_name =
      runtime::BorrowInternedLuaStringField(L, self_index, "__ow_name");
  const bool stored_relative_is_ui_parent =
      storage == LuaAllPointsRelativeStorage::kUIParent && self_name != nullptr &&
      std::strcmp(self_name, "UIParent") == 0;
  if ((relative_index != 0 && lua_rawequal(L, relative_index, self_index) != 0) ||
      stored_relative_is_ui_parent) {
    if (relative_index != 0) {
      lua_pop(L, 1);
    }
    return luaL_error(L, "%s:SetAllPoints(): trying to anchor to itself",
                      lua_adapter::ScriptObjectDisplayName(L, self_index));
  }

  if (relative_index != 0 &&
      LuaAnchorTargetDependsOn(L, relative_index, self_index, validation)) {
    const char* relative_name =
        lua_adapter::ScriptObjectDisplayName(L, relative_index);
    return luaL_error(L, "%s:SetAllPoints(): %s is dependent on this",
                      lua_adapter::ScriptObjectDisplayName(L, self_index),
                      relative_name);
  }

  StoreLuaAllPointsAnchors(L, self_index, 2, relative_index, storage);
  if (relative_index != 0) {
    lua_pop(L, 1);
  }

  openwow::ui::game::detail::ReindexLuaAnchorDependents(L, self_index);
  NotifyFrameInputMutation(L, self_index, false);
  return 0;
}

int LuaGetPointInternal(lua_State* L) {
  const int self_index = ValidateFrameScriptSelf(L);
  const int requested_index =
      lua_isnumber(L, 2) != 0 ? static_cast<int>(lua_tonumber(L, 2)) : 1;
  const int visible_index = std::max(requested_index, 1);

  const int anchors_index = EnsureLuaAnchorArray(L, self_index);
  NormalizeLuaAnchorArray(L, anchors_index);
  if (!PushVisibleLuaAnchorByIndex(L, anchors_index, visible_index)) {
    lua_pop(L, 1);
    return 0;
  }

  const int anchor_index = lua_absindex(L, -1);
  runtime::GetInternedLuaField(L, anchor_index, "point");
  if ((GetLuaAnchorFlags(L, anchor_index) & 0x100u) != 0u) {
    runtime::GetInternedLuaField(L, anchor_index, "x");
    runtime::GetInternedLuaField(L, anchor_index, "y");
    lua_remove(L, anchor_index);
    lua_remove(L, anchors_index);
    return 3;
  }

  PushAnchorRelativeToValue(L, anchor_index);
  runtime::GetInternedLuaField(L, anchor_index, "relativePoint");
  if (lua_isnil(L, -1) != 0) {
    lua_pop(L, 1);
    runtime::GetInternedLuaField(L, anchor_index, "point");
  }
  runtime::GetInternedLuaField(L, anchor_index, "x");
  runtime::GetInternedLuaField(L, anchor_index, "y");
  lua_remove(L, anchor_index);
  lua_remove(L, anchors_index);
  return 5;
}

}

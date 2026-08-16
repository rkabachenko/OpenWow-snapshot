#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/widgets/model_widget_methods.h"
#include "openwow/ui/game/framescript/core/frame_region_state.h"
#include "openwow/ui/game/framescript/core/frame_color_runtime.h"
#include "openwow/ui/game/framescript/core/frame_model_runtime.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"
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

void ApplyModelMethods(lua_State *L) {
  int f = lua_absindex(L, -1);
  static constexpr PackedColorFieldNames kFogColorFields{{
      "__ow_model_fog_r", "__ow_model_fog_g", "__ow_model_fog_b",
      "__ow_model_fog_a"}};
  static constexpr PackedColorDefaultValues kFogColorDefaults{{0.0, 0.0, 0.0,
                                                                1.0}};

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushvalue(Ls, 2);
      lua_setfield(Ls, 1, "__ow_model_path");

      NotifyFrameInputMutation(Ls, 1, false);
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetModel");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_model_path");
    return 1;
  }, 0);
  lua_setfield(L, f, "GetModel");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, luaL_optnumber(Ls, 2, 1));
      lua_setfield(Ls, 1, "__ow_model_scale");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetModelScale");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 1);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_model_scale");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 1);
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "GetModelScale");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, luaL_optnumber(Ls, 2, 0));
      lua_setfield(Ls, 1, "__ow_model_x");
      lua_pushnumber(Ls, luaL_optnumber(Ls, 3, 0));
      lua_setfield(Ls, 1, "__ow_model_y");
      lua_pushnumber(Ls, luaL_optnumber(Ls, 4, 0));
      lua_setfield(Ls, 1, "__ow_model_z");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetPosition");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 0);
      return 3;
    }
    lua_getfield(Ls, 1, "__ow_model_x");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
    }
    lua_getfield(Ls, 1, "__ow_model_y");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
    }
    lua_getfield(Ls, 1, "__ow_model_z");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
    }
    return 3;
  }, 0);
  lua_setfield(L, f, "GetPosition");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, luaL_optnumber(Ls, 2, 0));
      lua_setfield(Ls, 1, "__ow_model_facing");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetFacing");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_model_facing");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "GetFacing");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    ValidateFrameObjectSelf(Ls, "Model");
    const int sequence = ValidateAndReadModelSequence(Ls, "SetSequence");
    StoreBoundUnitSequence(Ls, 1, static_cast<std::uint32_t>(sequence));
    return 0;
  }, 0);
  lua_setfield(L, f, "SetSequence");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    ValidateFrameObjectSelf(Ls, "Model");

    if (lua_isnumber(Ls, 2) == 0 || lua_isnumber(Ls, 3) == 0)
      return luaL_error(Ls, "Usage: %s:SetSequenceTime(sequence, time)",
                        lua_adapter::ScriptObjectDisplayName(Ls, 1));
    StoreModelSequenceTime(Ls, 1, ClampLuaNumberToClientU32(Ls, 2),
                           ClampLuaNumberToClientU32(Ls, 3));
    return 0;
  }, 0);
  lua_setfield(L, f, "SetSequenceTime");
  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    ValidateFrameObjectSelf(Ls, "Model");

    if (lua_isnumber(Ls, 2) == 0)
      return luaL_error(Ls, "Usage: %s:SetCamera(index)",
                        lua_adapter::ScriptObjectDisplayName(Ls, 1));
    StoreModelCamera(Ls, 1, ClampLuaNumberToClientU32(Ls, 2));
    return 0;
  }, 0);
  lua_setfield(L, f, "SetCamera");
  lua_pushcclosure(L, [](lua_State * ) -> int { return 0; }, 0);
  lua_setfield(L, f, "SetLight");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      lua_setfield(Ls, 1, "__ow_model_path");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "ClearModel");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    ValidateFrameObjectSelf(Ls, "Model");
    return 0;
  }, 0);
  lua_setfield(L, f, "AdvanceTime");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Model");
    if (lua_isstring(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:ReplaceIconTexture(\"texture\")",
                        lua_adapter::ScriptObjectDisplayName(Ls, self));
    }
    lua_pushvalue(Ls, 2);
    lua_setfield(Ls, self, "__ow_replace_icon_tex");
    return 0;
  }, 0);
  lua_setfield(L, f, "ReplaceIconTexture");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Model");
    StorePackedColor(Ls, self, kFogColorFields, kFogColorDefaults);
    lua_pushboolean(Ls, 1);
    lua_setfield(Ls, self, "__ow_model_fog_enabled");
    return 0;
  }, 0);
  lua_setfield(L, f, "SetFogColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Model");
    PushPackedColor(Ls, self, kFogColorFields, kFogColorDefaults);
    return 4;
  }, 0);
  lua_setfield(L, f, "GetFogColor");

  const auto install_fog_distance = [L, f](const char *setter,
                                            const char *getter,
                                            const char *field) {
    lua_pushstring(L, setter);
    lua_pushstring(L, field);
    lua_pushcclosure(L, [](lua_State *Ls) -> int {
      const int self = ValidateFrameObjectSelf(Ls, "Model");
      const char *method = lua_tostring(Ls, lua_upvalueindex(1));
      const char *storage = lua_tostring(Ls, lua_upvalueindex(2));
      if (lua_isnumber(Ls, 2) == 0) {
        return luaL_error(Ls, "Usage: %s:%s(value)",
                          lua_adapter::ScriptObjectDisplayName(Ls, self), method);
      }
      lua_pushnumber(Ls, lua_tonumber(Ls, 2));
      lua_setfield(Ls, self, storage);
      return 0;
    }, 2);
    lua_setfield(L, f, setter);

    lua_pushstring(L, field);
    lua_pushcclosure(L, [](lua_State *Ls) -> int {
      const int self = ValidateFrameObjectSelf(Ls, "Model");
      const char *storage = lua_tostring(Ls, lua_upvalueindex(1));
      lua_getfield(Ls, self, storage);
      if (lua_isnumber(Ls, -1) == 0) {
        lua_pop(Ls, 1);
        lua_pushnumber(Ls, 0.0);
      }
      return 1;
    }, 1);
    lua_setfield(L, f, getter);
  };
  install_fog_distance("SetFogNear", "GetFogNear", "__ow_model_fog_near");
  install_fog_distance("SetFogFar", "GetFogFar", "__ow_model_fog_far");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Model");
    lua_pushboolean(Ls, 0);
    lua_setfield(Ls, self, "__ow_model_fog_enabled");
    return 0;
  }, 0);
  lua_setfield(L, f, "ClearFog");
}

void ApplyPlayerModelSpecificMethods(lua_State *L) {
  const int f = lua_absindex(L, -1);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!detail::AllowLuaFrameProtectedMutation(Ls, 1)) {
      return 0;
    }
    const int self = ValidateFrameObjectSelf(Ls, "PlayerModel");
    if (!openwow::ui::ReadLuaBooleanFieldOrDefault(Ls, self, "__ow_visible", true)) {
      RestoreCharacterModelAfterShow(Ls, self);
    }
    if (openwow::ui::game::detail::SetLuaScriptRegionShown(Ls, self, true)) {
      NotifyFrameInputMutation(Ls, self, true);
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "Show");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!detail::AllowLuaFrameProtectedMutation(Ls, 1)) {
      return 0;
    }
    const int self = ValidateFrameObjectSelf(Ls, "PlayerModel");
    if (openwow::ui::ReadLuaBooleanFieldOrDefault(Ls, self, "__ow_visible", true)) {
      SuspendCharacterModelForHide(Ls, self);
    }
    if (openwow::ui::game::detail::SetLuaScriptRegionShown(Ls, self, false)) {
      NotifyFrameInputMutation(Ls, self, true);
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "Hide");

  lua_pushcfunction(L, LuaPlayerModelSetCreature);
  lua_setfield(L, f, "SetCreature");

  lua_pushcfunction(L, LuaPlayerModelSetUnit);
  lua_setfield(L, f, "SetUnit");

  lua_pushcfunction(L, LuaPlayerModelRefreshUnit);
  lua_setfield(L, f, "RefreshUnit");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1))
      return 0;
    if (lua_isnumber(Ls, 2) == 0)
      return luaL_error(Ls, "Usage: SetRotation(rotation (in radians))");
    lua_pushnumber(Ls, lua_tonumber(Ls, 2));
    lua_setfield(Ls, 1, "__ow_model_facing");
    return 0;
  }, 0);
  lua_setfield(L, f, "SetRotation");
}

void ApplyDressUpModelSpecificMethods(lua_State *L) {
  const int f = lua_absindex(L, -1);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "DressUpModel");
    ClearDressUpPreviewState(Ls, self);
    return 0;
  }, 0);
  lua_setfield(L, f, "Undress");
  lua_pushcfunction(L, LuaScriptTryOn);
  lua_setfield(L, f, "TryOn");
  lua_pushcfunction(L, LuaPlayerModelRefreshUnit);
  lua_setfield(L, f, "Dress");
}

void ApplyTabardModelSpecificMethods(lua_State *L) {
  const int f = lua_absindex(L, -1);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameObjectSelf(Ls, "TabardModel");
    InitializeTabardDesignValues(Ls, self_idx);
    return 0;
  }, 0);
  lua_setfield(L, f, "InitializeTabardColors");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameObjectSelf(Ls, "TabardModel");
    auto *session = openwow::ui::game::detail::GetWorldSession(Ls);
    if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
      return 0;
    }

    const auto values = ReadTabardDesignValues(Ls, self_idx);
    (void)openwow::game::TabardFrame_Save(
        *session, values, session->petition().tabard_vendor_guid());
    return 0;
  }, 0);
  lua_setfield(L, f, "Save");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameObjectSelf(Ls, "TabardModel");
    if (lua_isnumber(Ls, 2) == 0 || lua_isnumber(Ls, 3) == 0) {
      return luaL_error(Ls, "Usage: CycleVariation(variationIndex, delta)");
    }

    const auto variation_index =
        openwow::ui::SaturateLuaNumberToU32(lua_tonumber(Ls, 2)) - 1u;
    if (variation_index >= openwow::game::kTabardNumAxes) {
      return luaL_error(Ls, "Invalid variationIndex in CycleVariation");
    }

    auto values = ReadTabardDesignValues(Ls, self_idx);
    if (openwow::game::TabardFrame_CycleVariation(
            values.data(), variation_index, TruncateLuaIntegerArgument(Ls, 3))) {
      WriteTabardDesignValuesAndRefreshPreview(Ls, self_idx, values);
    } else {
      WriteTabardDesignValues(Ls, self_idx, values);
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "CycleVariation");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const auto values =
        ReadTabardDesignValues(Ls, ValidateFrameObjectSelf(Ls, "TabardModel"));
    const auto texture_path =
        openwow::game::BuildGuildTabardBackgroundTexturePath(
            openwow::game::TabardTextureHalf::Upper, values[4]);
    lua_pushstring(Ls, texture_path.c_str());
    return 1;
  }, 0);
  lua_setfield(L, f, "GetUpperBackgroundFileName");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const auto values =
        ReadTabardDesignValues(Ls, ValidateFrameObjectSelf(Ls, "TabardModel"));
    const auto texture_path =
        openwow::game::BuildGuildTabardBackgroundTexturePath(
            openwow::game::TabardTextureHalf::Lower, values[4]);
    lua_pushstring(Ls, texture_path.c_str());
    return 1;
  }, 0);
  lua_setfield(L, f, "GetLowerBackgroundFileName");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const auto values =
        ReadTabardDesignValues(Ls, ValidateFrameObjectSelf(Ls, "TabardModel"));
    const auto texture_path = openwow::game::BuildGuildTabardEmblemTexturePath(
        openwow::game::TabardTextureHalf::Upper, values[0], values[1]);
    lua_pushstring(Ls, texture_path.c_str());
    return 1;
  }, 0);
  lua_setfield(L, f, "GetUpperEmblemFileName");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const auto values =
        ReadTabardDesignValues(Ls, ValidateFrameObjectSelf(Ls, "TabardModel"));
    const auto texture_path = openwow::game::BuildGuildTabardEmblemTexturePath(
        openwow::game::TabardTextureHalf::Lower, values[0], values[1]);
    lua_pushstring(Ls, texture_path.c_str());
    return 1;
  }, 0);
  lua_setfield(L, f, "GetLowerEmblemFileName");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameObjectSelf(Ls, "TabardModel");
    const int texture_idx =
        GetValidatedTabardTextureArg(Ls, "GetUpperEmblemTexture");
    const auto values = ReadTabardDesignValues(Ls, self_idx);
    const auto descriptor =
        openwow::game::BuildGuildTabardEmblemRenderTargetDescriptor(
            openwow::game::TabardTextureHalf::Upper, values[0], values[1]);
    SetTabardEmblemRenderTarget(Ls, texture_idx, descriptor);
    return 0;
  }, 0);
  lua_setfield(L, f, "GetUpperEmblemTexture");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameObjectSelf(Ls, "TabardModel");
    const int texture_idx =
        GetValidatedTabardTextureArg(Ls, "GetLowerEmblemTexture");
    const auto values = ReadTabardDesignValues(Ls, self_idx);
    const auto descriptor =
        openwow::game::BuildGuildTabardEmblemRenderTargetDescriptor(
            openwow::game::TabardTextureHalf::Lower, values[0], values[1]);
    SetTabardEmblemRenderTarget(Ls, texture_idx, descriptor);
    return 0;
  }, 0);
  lua_setfield(L, f, "GetLowerEmblemTexture");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    auto *session = openwow::ui::game::detail::GetWorldSession(Ls);
    const auto *player =
        session != nullptr ? session->objects().GetActivePlayer() : nullptr;
    if (player == nullptr) {
      lua_pushnil(Ls);
      return 1;
    }

    if (session->guild().FindCachedGuildInfo(player->GetGuildID()) != nullptr &&
        !session->petition().tabard_save_pending()) {
      lua_pushnumber(Ls, 1.0);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "CanSaveTabardNow");
}

}

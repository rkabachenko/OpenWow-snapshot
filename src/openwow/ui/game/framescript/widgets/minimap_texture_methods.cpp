#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/framescript/widgets/texture_asset_probe.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"

extern "C" {
#include <lua.hpp>
}

#include <cstdint>
#include <string>

#undef lua_pushcfunction
#define lua_pushcfunction(L, ...) lua_pushcclosure(L, (__VA_ARGS__), 0)

namespace openwow::ui::game::frame_api {

namespace {

enum class MinimapTextureEmptyPolicy : std::uint8_t {
  kAllowLoadAttempt,
  kRejectAsUsage,
};

openwow::ui::MinimapSystem *MinimapStateOrNull(lua_State *L) {
  auto *const context =
      openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(L);
  return context != nullptr ? &context->minimap_state() : nullptr;
}

template <typename ApplyPathFn, typename ClearPathFn>
int SetLoadedMinimapTexturePath(lua_State *L, const char *method_name, const char *storage_field,
                                MinimapTextureEmptyPolicy empty_policy, ApplyPathFn &&apply_path,
                                ClearPathFn &&clear_path,
                                const char *load_failure_method_name = nullptr) {
  const int self_idx = ValidateTypedFramescriptSelf(L, "Minimap");
  const std::string object_name = GetObjectNameOrUnnamed(L, self_idx);
  if (lua_isstring(L, 2) == 0) {
    return luaL_error(L, "Usage: %s:%s(\"file\")", object_name.c_str(), method_name);
  }

  const char *requested_path = lua_tostring(L, 2);
  const bool reject_empty = empty_policy == MinimapTextureEmptyPolicy::kRejectAsUsage &&
                            (requested_path == nullptr || requested_path[0] == '\0');
  if (reject_empty) {
    return luaL_error(L, "Usage: %s:%s(\"file\")", object_name.c_str(), method_name);
  }

  if (!CanLoadTextureRequest(L, requested_path != nullptr ? requested_path : "")) {
    ClearLuaStringField(L, self_idx, storage_field);
    clear_path(L);
    return luaL_error(L, "%s:%s(): Couldn't load the file %s", object_name.c_str(),
                      load_failure_method_name != nullptr ? load_failure_method_name : method_name,
                      requested_path != nullptr ? requested_path : "");
  }

  lua_pushvalue(L, 2);
  lua_setfield(L, self_idx, storage_field);
  apply_path(L, requested_path != nullptr ? requested_path : "");
  return 0;
}

}

void ApplyMinimapTextureMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    return SetLoadedMinimapTexturePath(
        Ls, "SetMaskTexture", "__ow_mm_mask_tex",
        MinimapTextureEmptyPolicy::kAllowLoadAttempt,
        [](lua_State *state, const char *path) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->SetMaskTexturePath(path);
          }
        },
        [](lua_State *state) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->ClearMaskTexturePath();
          }
        });
  });
  lua_setfield(L, f, "SetMaskTexture");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    return SetLoadedMinimapTexturePath(
        Ls, "SetBlipTexture", "__ow_mm_blip_tex", MinimapTextureEmptyPolicy::kRejectAsUsage,
        [](lua_State *state, const char *path) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->SetObjectIconTexturePath(path);
          }
        },
        [](lua_State *state) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->ClearObjectIconTexturePath();
          }
        });
  });
  lua_setfield(L, f, "SetBlipTexture");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    return SetLoadedMinimapTexturePath(
        Ls, "SetClassBlipTexture", "__ow_mm_class_blip_tex",
        MinimapTextureEmptyPolicy::kAllowLoadAttempt,
        [](lua_State *state, const char *path) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->SetPartyRaidBlipsTexturePath(path);
          }
        },
        [](lua_State *state) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->ClearPartyRaidBlipsTexturePath();
          }
        });
  });
  lua_setfield(L, f, "SetClassBlipTexture");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    return SetLoadedMinimapTexturePath(
        Ls, "SetIconTexture", "__ow_mm_icon_tex",
        MinimapTextureEmptyPolicy::kAllowLoadAttempt,
        [](lua_State *state, const char *path) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->SetPoiIconTexturePath(path);
          }
        },
        [](lua_State *state) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->ClearPoiIconTexturePath();
          }
        });
  });
  lua_setfield(L, f, "SetIconTexture");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    return SetLoadedMinimapTexturePath(
        Ls, "SetPOITexture", "__ow_mm_poi_arrow_tex",
        MinimapTextureEmptyPolicy::kAllowLoadAttempt,
        [](lua_State *state, const char *path) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->SetRotatingArrowTexturePath(
                openwow::ui::MinimapRotatingArrowKind::kGuidePoi, path);
          }
        },
        [](lua_State *state) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->SetRotatingArrowTexturePath(
                openwow::ui::MinimapRotatingArrowKind::kGuidePoi, {});
          }
        });
  });
  lua_setfield(L, f, "SetPOIArrowTexture");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    return SetLoadedMinimapTexturePath(
        Ls, "SetStaticPOITexture", "__ow_mm_static_poi_arrow_tex",
        MinimapTextureEmptyPolicy::kAllowLoadAttempt,
        [](lua_State *state, const char *path) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->SetRotatingArrowTexturePath(
                openwow::ui::MinimapRotatingArrowKind::kStaticPoi, path);
          }
        },
        [](lua_State *state) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->SetRotatingArrowTexturePath(
                openwow::ui::MinimapRotatingArrowKind::kStaticPoi, {});
          }
        });
  });
  lua_setfield(L, f, "SetStaticPOIArrowTexture");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    return SetLoadedMinimapTexturePath(
        Ls, "SetCorpsePOITexture", "__ow_mm_corpse_poi_arrow_tex",
        MinimapTextureEmptyPolicy::kAllowLoadAttempt,
        [](lua_State *state, const char *path) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->SetRotatingArrowTexturePath(
                openwow::ui::MinimapRotatingArrowKind::kCorpsePoi, path);
          }
        },
        [](lua_State *state) {
          if (auto *const minimap = MinimapStateOrNull(state)) {
            minimap->SetRotatingArrowTexturePath(
                openwow::ui::MinimapRotatingArrowKind::kCorpsePoi, {});
          }
        },
        "SetCorspePOITexture");
  });
  lua_setfield(L, f, "SetCorpsePOIArrowTexture");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "Minimap");
    const std::string object_name = GetObjectNameOrUnnamed(Ls, self_idx);
    if (lua_isstring(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:SetPlayerTexture(\"file\")",
                        object_name.c_str());
    }
    const char *requested = lua_tostring(Ls, 2);
    auto *const minimap_state = MinimapStateOrNull(Ls);
    if (requested == nullptr || requested[0] == '\0') {
      if (minimap_state != nullptr) {
        minimap_state->ClearPlayerArrowTexturePath();
      }
      ClearLuaStringField(Ls, self_idx, "__ow_mm_player_tex");
      return 0;
    }
    if (!CanLoadTextureRequest(Ls, requested)) {
      return luaL_error(Ls, "%s:SetPlayerTexture(): Couldn't load the file %s",
                        object_name.c_str(), requested);
    }
    if (minimap_state != nullptr) {
      minimap_state->SetPlayerArrowTexturePath(requested);
    }
    lua_pushvalue(Ls, 2);
    lua_setfield(Ls, self_idx, "__ow_mm_player_tex");
    return 0;
  });
  lua_setfield(L, f, "SetPlayerTexture");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "Minimap");
    if (lua_isnumber(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:SetPlayerTextureHeight(height)",
                        GetObjectNameOrUnnamed(Ls, self_idx).c_str());
    }
    const float pixels = static_cast<float>(lua_tonumber(Ls, 2));
    if (auto *const minimap_state = MinimapStateOrNull(Ls)) {
      minimap_state->SetPlayerArrowSize(minimap_state->GetPlayerArrowWidth(),
                                        pixels);
    }
    lua_pushnumber(Ls, pixels);
    lua_setfield(Ls, self_idx, "__ow_mm_player_tex_h");
    return 0;
  });
  lua_setfield(L, f, "SetPlayerTextureHeight");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateTypedFramescriptSelf(Ls, "Minimap");
    if (lua_isnumber(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:SetPlayerTextureWidth(Width)",
                        GetObjectNameOrUnnamed(Ls, self_idx).c_str());
    }
    const float pixels = static_cast<float>(lua_tonumber(Ls, 2));
    if (auto *const minimap_state = MinimapStateOrNull(Ls)) {
      minimap_state->SetPlayerArrowSize(pixels,
                                        minimap_state->GetPlayerArrowHeight());
    }
    lua_pushnumber(Ls, pixels);
    lua_setfield(Ls, self_idx, "__ow_mm_player_tex_w");
    return 0;
  });
  lua_setfield(L, f, "SetPlayerTextureWidth");
}

}

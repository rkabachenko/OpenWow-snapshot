#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/widgets/media_widget_methods.h"
#include "openwow/ui/game/framescript/core/frame_region_geometry.h"
#include "openwow/ui/game/framescript/core/frame_script_dispatch.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/game/minimap_ping.h"
#include "openwow/game/minimap_terrain.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/ui/game/minimap_system.h"
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

void ApplyMovieFrameMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  ApplyFrameScriptHandlerMethods(L, f, true);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "MovieFrame");
    if (lua_isstring(Ls, 2) == 0 || lua_isnumber(Ls, 3) == 0) {
      return luaL_error(
          Ls, "Usage: %s:StartMovie(\"filename\", volume_0_to_255)",
          lua_adapter::ScriptObjectDisplayName(Ls, self));
    }

    const char *filename = lua_tostring(Ls, 2);
    const std::uint32_t raw_volume = static_cast<std::uint32_t>(
        openwow::ui::TruncateLuaNumberToI32(lua_tonumber(Ls, 3)));
    const int volume =
        raw_volume <= 255u
            ? static_cast<int>(raw_volume)
            : ((raw_volume & 0x80000000u) != 0u ? 0 : 255);
    const char *frame_key = GetFrameRuntimeKeyOrName(Ls, self);
    bool ok = false;
    if (auto *manager = runtime::WorldUiRuntimeContext::FromLua(Ls);
        manager != nullptr && frame_key != nullptr) {
      ok = manager->movie_runtime().Start(frame_key, filename, volume);
    }

    if (ok) {
      lua_pushstring(Ls, filename);
      lua_setfield(Ls, self, "__ow_movie_path");
      lua_pushinteger(Ls, volume);
      lua_setfield(Ls, self, "__ow_movie_volume");
    }
    lua_pushboolean(Ls, ok ? 1 : 0);
    lua_setfield(Ls, self, "__ow_movie_playing");
    if (ok) {
      lua_pushnumber(Ls, 1.0);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "StartMovie");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "MovieFrame");
    lua_pushboolean(Ls, 0);
    lua_setfield(Ls, self, "__ow_movie_playing");

    const char *const frame_key = GetFrameRuntimeKeyOrName(Ls, self);
    if (auto *manager = runtime::WorldUiRuntimeContext::FromLua(Ls);
        manager != nullptr && frame_key != nullptr) {
      manager->movie_runtime().Stop(frame_key);
    }

    return 0;
  }, 0);
  lua_setfield(L, f, "StopMovie");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "MovieFrame");
    lua_pushboolean(Ls, detail::ScriptReadBoolArgOrDefault(Ls, 2, true));
    lua_setfield(Ls, self, "__ow_movie_subtitles");
    return 0;
  }, 0);
  lua_setfield(L, f, "EnableSubtitles");
}

void ApplyMinimapMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    ValidateFrameObjectSelf(Ls, "Minimap");

    if (lua_isnumber(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: SetZoom(level)");
    }
    if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(Ls); manager != nullptr) {
      openwow::game::Minimap_SetZoomLevel(
          manager->minimap_state(),
          ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(Ls, 2)));
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetZoom");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    ValidateFrameObjectSelf(Ls, "Minimap");
    const auto* manager = runtime::WorldUiRuntimeContext::FromLua(Ls);
    lua_pushnumber(
        Ls, static_cast<lua_Number>(
                manager != nullptr
                    ? openwow::game::Minimap_GetZoomLevel(
                          manager->minimap_state())
                    : 0));
    return 1;
  }, 0);
  lua_setfield(L, f, "GetZoom");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    lua_pushnumber(
        Ls, static_cast<lua_Number>(openwow::ui::MinimapSystem::kMaxZoomLevel + 1u));
    return 1;
  }, 0);
  lua_setfield(L, f, "GetZoomLevels");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameObjectSelf(Ls, "Minimap");
    if (lua_isnumber(Ls, 2) == 0 || lua_isnumber(Ls, 3) == 0) {
      luaL_error(Ls, "Usage: %s:PingLocation(x, y)",
                 lua_adapter::ScriptObjectDisplayName(Ls, self_idx));
    }

    auto *session = detail::GetWorldSession(Ls);
    if (session == nullptr) {
      return 0;
    }

    const auto *player = session->objects().GetActivePlayer();
    if (player == nullptr) {
      return 0;
    }

    auto* manager = runtime::WorldUiRuntimeContext::FromLua(Ls);
    if (manager == nullptr) {
      return 0;
    }
    auto &ping_system = manager->minimap_ping();
    const LuaRegionSizeValues minimap_size =
        ResolveLuaRegionSizeValues(Ls, self_idx, false);
    const auto [world_x, world_y] =
        ping_system.ResolveWorldPositionFromFrameOffset(
            lua_tonumber(Ls, 2), lua_tonumber(Ls, 3), minimap_size.width,
            minimap_size.height, player->GetX(), player->GetY(),
            player->GetOrientation());
    ping_system.SetLastPingWorldPosition(world_x, world_y);

    const auto &group_system = openwow::game::GroupSystem::Get();
    const bool should_send_packet =
        group_system.GetTrackedPartyMemberCount() != 0 ||
        group_system.GetRealRaidMemberCount() != 0;
    if (should_send_packet) {
      openwow::net::wotlk::WorldPacket pkt(
          openwow::net::wotlk::Opcode::MSG_MINIMAP_PING);
      pkt.AppendFloat(world_x);
      pkt.AppendFloat(world_y);
      (void)openwow::net::ClientServices__SendPacket(pkt);
    }

    ping_system.DispatchPingEvent(player->GetGuid().GetRawValue(),
                                  player->GetGuid().GetRawValue(),
                                  player->GetX(), player->GetY(),
                                  player->GetOrientation());
    return 0;
  }, 0);
  lua_setfield(L, f, "PingLocation");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    ValidateFrameObjectSelf(Ls, "Minimap");

    auto *session = detail::GetWorldSession(Ls);
    const auto *player =
        session != nullptr ? session->objects().GetActivePlayer() : nullptr;
    if (player == nullptr) {
      lua_pushnumber(Ls, 0.0);
      lua_pushnumber(Ls, 0.0);
      return 2;
    }

    auto* manager = runtime::WorldUiRuntimeContext::FromLua(Ls);
    const auto position =
        manager != nullptr
            ? manager->minimap_ping().GetNormalizedPingPosition(
                  player->GetX(), player->GetY(), player->GetOrientation())
            : openwow::game::MinimapPingPosition{};
    lua_pushnumber(Ls, position.x);
    lua_pushnumber(Ls, position.y);
    return 2;
  }, 0);
  lua_setfield(L, f, "GetPingPosition");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameObjectSelf(Ls, "Minimap");
    if (lua_isstring(Ls, 2) == 0) {
      luaL_error(Ls, "Usage: %s:SetMaskTexture(\"file\")",
                 lua_adapter::ScriptObjectDisplayName(Ls, self_idx));
    }

    lua_pushvalue(Ls, 2);
    lua_setfield(Ls, self_idx, "__ow_mm_mask_tex");

    const char *path = lua_tostring(Ls, 2);
    if (path != nullptr) {
      if (auto* const manager =
              openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(Ls);
          manager != nullptr) {
        manager->minimap_state().SetMaskTexturePath(path);
      }
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetMaskTexture");

}

}

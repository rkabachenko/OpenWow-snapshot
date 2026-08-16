#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/game/framescript/core/frame_types_widgets.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/framescript/widgets/scrolling_widget_methods.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/retained_layout.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/foundation/text/ascii.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui::game::frame_api {

using namespace openwow::ui::game::detail;

static double GetNumField(lua_State* L, int idx, const char* field, double def) {
  lua_getfield(L, idx, field);
  double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
  lua_pop(L, 1);
  return v;
}

static lua_Integer GetIntField(lua_State* L, int idx, const char* field, lua_Integer def) {
  lua_getfield(L, idx, field);
  lua_Integer v = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : def;
  lua_pop(L, 1);
  return v;
}

static void StoreScrollFrameRanges(
    lua_State* L, int scroll_frame_index,
    const runtime::RetainedLayout::ScrollFrameRanges ranges) {
  scroll_frame_index = lua_absindex(L, scroll_frame_index);
  lua_pushnumber(L, ranges.horizontal);
  lua_setfield(L, scroll_frame_index, "__ow_sf_hrange");
  lua_pushnumber(L, ranges.vertical);
  lua_setfield(L, scroll_frame_index, "__ow_sf_vrange");
}

void ApplyScrollFrameWidgetExtras(lua_State* L) {
  int f = lua_absindex(L, -1);

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) { lua_pushnumber(Ls, 0); return 1; }
    lua_pushnumber(Ls, GetNumField(Ls, 1, "__ow_sf_vrange", 0.0));
    return 1;
  });
  lua_setfield(L, f, "GetVerticalScrollRange");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) { lua_pushnumber(Ls, 0); return 1; }
    lua_pushnumber(Ls, GetNumField(Ls, 1, "__ow_sf_hrange", 0.0));
    return 1;
  });
  lua_setfield(L, f, "GetHorizontalScrollRange");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) return 0;
    if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(Ls);
        manager != nullptr) {
      manager->retained_layout().Invalidate();
    }
    return 0;
  });
  lua_setfield(L, f, "UpdateScrollChildRect");
}

void RefreshScrollFrameWidgetState(
    lua_State* L, std::uint64_t* const published_ranges_generation) {
  auto* manager = runtime::WorldUiRuntimeContext::FromLua(L);
  if (manager == nullptr) return;

  auto& frames = manager->frame_store();
  auto& layout = manager->retained_layout();
  layout.SolveIfDirty();

  const std::uint64_t ranges_generation = layout.ScrollFrameRangesGeneration();
  if (ranges_generation == *published_ranges_generation) return;
  *published_ranges_generation = ranges_generation;
  constexpr double kRangeEpsilon = 9.5367431640625e-7;

  const std::vector<std::string> scroll_frames = [&] {
    const auto keys = frames.FramesOfKind("ScrollFrame");
    return std::vector<std::string>(keys.begin(), keys.end());
  }();
  for (const auto& key : scroll_frames) {
    const auto* frame = frames.FindFrame(key);
    const auto ref = frames.FindLuaRef(key);
    if (frame == nullptr || !ref.has_value() ||
        !openwow::text::EqualsIgnoreCaseAscii(frame->kind, "ScrollFrame")) {
      continue;
    }

    const int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, *ref);
    if (lua_istable(L, -1) == 0) {
      lua_settop(L, top);
      continue;
    }
    const int frame_index = lua_absindex(L, -1);
    const auto ranges = layout.ResolveScrollFrameRanges(key);
    const double previous_h =
        GetNumField(L, frame_index, "__ow_sf_hrange", 0.0);
    const double previous_v =
        GetNumField(L, frame_index, "__ow_sf_vrange", 0.0);
    if (std::fabs(previous_h - ranges.horizontal) < kRangeEpsilon &&
        std::fabs(previous_v - ranges.vertical) < kRangeEpsilon) {
      lua_settop(L, top);
      continue;
    }

    StoreScrollFrameRanges(L, frame_index, ranges);
    SetScrollFrameOffsetState(
        L, frame_index, false,
        std::clamp(GetNumField(L, frame_index, "__ow_sf_vscroll", 0.0),
                   0.0, ranges.vertical),
        true);
    SetScrollFrameOffsetState(
        L, frame_index, true,
        std::clamp(GetNumField(L, frame_index, "__ow_sf_hscroll", 0.0),
                   0.0, ranges.horizontal),
        true);
    lua_pushnumber(L, ranges.horizontal);
    lua_pushnumber(L, ranges.vertical);
    const auto invocation =
        InvokeFrameScriptHandler(L, frame_index, "OnScrollRangeChanged", 2);
    if (invocation.status != LUA_OK) lua_pop(L, 1);
    lua_settop(L, top);
  }
}

void ApplyScrollingMessageFrameWidgetExtras(lua_State* L) {
  int f = lua_absindex(L, -1);

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_Integer offset = static_cast<lua_Integer>(luaL_optinteger(Ls, 2, 0));
      if (offset < 0) offset = 0;
      lua_pushinteger(Ls, offset);
      lua_setfield(Ls, 1, "__ow_smf_scroll");
    }
    return 0;
  });
  lua_setfield(L, f, "SetScrollOffset");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) return 0;
    lua_Integer off = GetIntField(Ls, 1, "__ow_smf_scroll", 0);
    lua_Integer n = GetIntField(Ls, 1, "__ow_smf_num_msg", 0);
    if (off < n - 1) off++;
    lua_pushinteger(Ls, off);
    lua_setfield(Ls, 1, "__ow_smf_scroll");
    return 0;
  });
  lua_setfield(L, f, "ScrollUp");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) return 0;
    lua_Integer off = GetIntField(Ls, 1, "__ow_smf_scroll", 0);
    if (off > 0) off--;
    lua_pushinteger(Ls, off);
    lua_setfield(Ls, 1, "__ow_smf_scroll");
    return 0;
  });
  lua_setfield(L, f, "ScrollDown");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) return 0;
    lua_Integer n = GetIntField(Ls, 1, "__ow_smf_num_msg", 0);
    lua_pushinteger(Ls, n > 0 ? n - 1 : 0);
    lua_setfield(Ls, 1, "__ow_smf_scroll");
    return 0;
  });
  lua_setfield(L, f, "ScrollToTop");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) return 0;
    lua_pushinteger(Ls, 0);
    lua_setfield(Ls, 1, "__ow_smf_scroll");
    return 0;
  });
  lua_setfield(L, f, "ScrollToBottom");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) { lua_pushboolean(Ls, 1); return 1; }
    lua_Integer off = GetIntField(Ls, 1, "__ow_smf_scroll", 0);
    lua_Integer n = GetIntField(Ls, 1, "__ow_smf_num_msg", 0);
    lua_pushboolean(Ls, off >= (n > 0 ? n - 1 : 0));
    return 1;
  });
  lua_setfield(L, f, "AtTop");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) { lua_pushboolean(Ls, 1); return 1; }
    lua_Integer off = GetIntField(Ls, 1, "__ow_smf_scroll", 0);
    lua_pushboolean(Ls, off == 0);
    return 1;
  });
  lua_setfield(L, f, "AtBottom");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) return 0;
    lua_Integer off = GetIntField(Ls, 1, "__ow_smf_scroll", 0);
    lua_Integer n = GetIntField(Ls, 1, "__ow_smf_num_msg", 0);
    lua_Integer pageSize = GetIntField(Ls, 1, "__ow_smf_maxlines", 10);
    off += pageSize;
    if (n > 0 && off > n - 1) off = n - 1;
    lua_pushinteger(Ls, off);
    lua_setfield(Ls, 1, "__ow_smf_scroll");
    return 0;
  });
  lua_setfield(L, f, "PageUp");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) return 0;
    lua_Integer off = GetIntField(Ls, 1, "__ow_smf_scroll", 0);
    lua_Integer pageSize = GetIntField(Ls, 1, "__ow_smf_maxlines", 10);
    off -= pageSize;
    if (off < 0) off = 0;
    lua_pushinteger(Ls, off);
    lua_setfield(Ls, 1, "__ow_smf_scroll");
    return 0;
  });
  lua_setfield(L, f, "PageDown");
}

void ApplyMessageFrameWidgetExtras(lua_State* L) {
  int f = lua_absindex(L, -1);

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) { lua_pushstring(Ls, "TOP"); return 1; }
    lua_getfield(Ls, 1, "__ow_mf_insert");
    if (!lua_isstring(Ls, -1)) { lua_pop(Ls, 1); lua_pushstring(Ls, "TOP"); }
    return 1;
  });
  lua_setfield(L, f, "GetInsertMode");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) { lua_pushnumber(Ls, 3); return 1; }
    lua_getfield(Ls, 1, "__ow_mf_fadedur");
    if (!lua_isnumber(Ls, -1)) { lua_pop(Ls, 1); lua_pushnumber(Ls, 3); }
    return 1;
  });
  lua_setfield(L, f, "GetFadeDuration");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) { lua_pushnumber(Ls, 10); return 1; }
    lua_getfield(Ls, 1, "__ow_mf_timevis");
    if (!lua_isnumber(Ls, -1)) { lua_pop(Ls, 1); lua_pushnumber(Ls, 10); }
    return 1;
  });
  lua_setfield(L, f, "GetTimeVisible");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) { lua_pushnumber(Ls, 1); return 1; }
    lua_getfield(Ls, 1, "__ow_mf_fadepower");
    if (!lua_isnumber(Ls, -1)) { lua_pop(Ls, 1); lua_pushnumber(Ls, 1); }
    return 1;
  });
  lua_setfield(L, f, "GetFadePower");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) { lua_pushnumber(Ls, 1); return 1; }
    lua_getfield(Ls, 1, "__ow_mf_fading");
    if (lua_isnil(Ls, -1) || lua_toboolean(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 1);
    } else {
      lua_pop(Ls, 1);
      lua_pushnil(Ls);
    }
    return 1;
  });
  lua_setfield(L, f, "GetFading");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) return 0;
    lua_getfield(Ls, 1, "__ow_mf_num_msg");
    lua_Integer n = lua_isinteger(Ls, -1) ? lua_tointeger(Ls, -1) : 0;
    lua_pop(Ls, 1);
    lua_pushinteger(Ls, n + 1);
    lua_setfield(Ls, 1, "__ow_mf_num_msg");
    return 0;
  });
  lua_setfield(L, f, "AddMessage");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushinteger(Ls, 0);
      lua_setfield(Ls, 1, "__ow_mf_num_msg");
    }
    return 0;
  });
  lua_setfield(L, f, "Clear");
}

}

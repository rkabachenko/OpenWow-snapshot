#include "openwow/ui/game/framescript/core/frame_draw_layer_state.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/runtime/frame_input_router.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/ui/lua_c_api_convenience.h"

#include <lua.hpp>

#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>

namespace openwow::ui::game::frame_api {
namespace {

struct DrawLayerEntry {
  int id;
  const char* name;

  bool enabled_by_constructor;
};

constexpr std::array kDrawLayerEntries = {
    DrawLayerEntry{0, "BACKGROUND", true}, DrawLayerEntry{1, "BORDER", true},
    DrawLayerEntry{2, "ARTWORK", true},    DrawLayerEntry{3, "OVERLAY", true},
    DrawLayerEntry{4, kDrawLayerHighlightName, false},
};

static bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) return false;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
        std::tolower(static_cast<unsigned char>(rhs[i])))
      return false;
  }
  return true;
}

int FindDrawLayerEntryIndex(const char *requested) {
  if (requested == nullptr || *requested == '\0') {
    return -1;
  }

  for (std::size_t index = 0; index < kDrawLayerEntries.size(); ++index) {
    if (EqualsIgnoreCase(requested, kDrawLayerEntries[index].name)) {
      return static_cast<int>(index);
    }
  }

  return -1;
}

bool DrawLayerEnabledByConstructor(const char *canonical_name) {
  const int index = FindDrawLayerEntryIndex(canonical_name);
  return index < 0 ||
         kDrawLayerEntries[static_cast<std::size_t>(index)].enabled_by_constructor;
}

void WriteRegionDrawLayerEnabled(lua_State *L, int region_idx, bool enabled) {
  region_idx = lua_absindex(L, region_idx);
  lua_pushboolean(L, enabled ? 1 : 0);
  lua_setfield(L, region_idx, kLuaRegionDrawLayerEnabledField);
}

bool FrameIsCurrentMouseFocus(lua_State *L, int frame_idx) {
  const auto *context = runtime::WorldUiRuntimeContext::FromLua(L);
  if (context == nullptr) {
    return false;
  }

  const std::string key = GetFrameManagerKey(L, frame_idx);
  return !key.empty() && context->input_router().mouseover_frame_name() == key;
}

}

bool TryCanonicalizeDrawLayerName(const char *requested, const char **canonical_name) {
  if (canonical_name == nullptr) {
    return false;
  }

  *canonical_name = nullptr;
  const int index = FindDrawLayerEntryIndex(requested);
  if (index < 0) {
    return false;
  }

  *canonical_name = kDrawLayerEntries[static_cast<std::size_t>(index)].name;
  return true;
}

bool TryParseDrawLayerName(const char *requested, int *draw_layer_id) {
  if (draw_layer_id == nullptr) {
    return false;
  }

  const int index = FindDrawLayerEntryIndex(requested);
  if (index < 0) {
    return false;
  }

  *draw_layer_id = kDrawLayerEntries[static_cast<std::size_t>(index)].id;
  return true;
}

const char *GetDrawLayerNameById(const int draw_layer_id) noexcept {
  for (const DrawLayerEntry &entry : kDrawLayerEntries) {
    if (entry.id == draw_layer_id) {
      return entry.name;
    }
  }

  return "UNKNOWN";
}

const char *ReadRegionDrawLayerName(lua_State *L, int region_idx) {
  region_idx = lua_absindex(L, region_idx);
  lua_getfield(L, region_idx, "__ow_draw_layer");

  const char *resolved_name = "ARTWORK";
  if (lua_isnumber(L, -1) != 0) {
    resolved_name = GetDrawLayerNameById(static_cast<int>(lua_tointeger(L, -1)));
  } else if (lua_isstring(L, -1) != 0) {
    const char *canonical_name = nullptr;
    const char *stored_name = lua_tostring(L, -1);
    resolved_name =
        TryCanonicalizeDrawLayerName(stored_name, &canonical_name) ? canonical_name : "UNKNOWN";
  }

  lua_pop(L, 1);
  return resolved_name;
}

const char *ReadStoredTextureDrawLayerName(lua_State *L, int self_idx) {
  return ReadRegionDrawLayerName(L, self_idx);
}

void InitializeFrameDrawLayerState(lua_State *L, int frame_idx) {
  frame_idx = lua_absindex(L, frame_idx);
  lua_getfield(L, frame_idx, kLuaFrameDrawLayerStateField);
  if (lua_istable(L, -1) != 0) {
    lua_pop(L, 1);
    return;
  }

  lua_pop(L, 1);

  lua_newtable(L);
  const int state_idx = lua_absindex(L, -1);
  for (const DrawLayerEntry &entry : kDrawLayerEntries) {
    lua_pushboolean(L, entry.enabled_by_constructor ? 1 : 0);
    lua_setfield(L, state_idx, entry.name);
  }
  lua_setfield(L, frame_idx, kLuaFrameDrawLayerStateField);
}

bool IsFrameDrawLayerEnabled(lua_State *L, int frame_idx, const char *canonical_name) {
  if (canonical_name == nullptr || *canonical_name == '\0') {
    return true;
  }

  const bool constructed_enabled = DrawLayerEnabledByConstructor(canonical_name);
  frame_idx = lua_absindex(L, frame_idx);
  lua_getfield(L, frame_idx, kLuaFrameDrawLayerStateField);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return constructed_enabled;
  }

  lua_getfield(L, -1, canonical_name);
  const bool enabled =
      lua_isboolean(L, -1) == 0 ? constructed_enabled : lua_toboolean(L, -1) != 0;
  lua_pop(L, 2);
  return enabled;
}

void SetFrameDrawLayerEnabled(lua_State *L, int frame_idx, const char *canonical_name,
                              bool enabled) {
  if (canonical_name == nullptr || *canonical_name == '\0') {
    return;
  }

  frame_idx = lua_absindex(L, frame_idx);
  InitializeFrameDrawLayerState(L, frame_idx);
  lua_getfield(L, frame_idx, kLuaFrameDrawLayerStateField);
  lua_pushboolean(L, enabled ? 1 : 0);
  lua_setfield(L, -2, canonical_name);
  lua_pop(L, 1);
}

void SyncRegionDrawLayerEnabled(lua_State *L, int region_idx) {
  if (lua_istable(L, region_idx) == 0) {
    return;
  }

  region_idx = lua_absindex(L, region_idx);
  const char *draw_layer_name = ReadRegionDrawLayerName(L, region_idx);

  lua_getfield(L, region_idx, "__ow_parent");
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    WriteRegionDrawLayerEnabled(L, region_idx, true);
    return;
  }

  const bool enabled = IsFrameDrawLayerEnabled(L, lua_absindex(L, -1), draw_layer_name);
  lua_pop(L, 1);
  WriteRegionDrawLayerEnabled(L, region_idx, enabled);
}

void SyncFrameRegionsForDrawLayer(lua_State *L, int frame_idx, const char *canonical_name) {
  if (canonical_name == nullptr || *canonical_name == '\0') {
    return;
  }

  frame_idx = lua_absindex(L, frame_idx);
  lua_getfield(L, frame_idx, "__ow_regions");
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    return;
  }

  const int regions_idx = lua_absindex(L, -1);
  const lua_Integer region_count = luaL_len(L, regions_idx);
  for (lua_Integer region_index = 1; region_index <= region_count; ++region_index) {
    lua_geti(L, regions_idx, region_index);
    if (lua_istable(L, -1) != 0 &&
        std::strcmp(ReadRegionDrawLayerName(L, -1), canonical_name) == 0) {
      SyncRegionDrawLayerEnabled(L, -1);
      NotifyFrameInputMutation(L, lua_absindex(L, -1), true);
    }
    lua_pop(L, 1);
  }

  lua_pop(L, 1);
}

bool IsFrameHighlightLocked(lua_State *L, int frame_idx) {
  frame_idx = lua_absindex(L, frame_idx);
  lua_getfield(L, frame_idx, kLuaFrameHighlightLockField);
  const bool locked = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return locked;
}

void SetFrameHighlightLayerShown(lua_State *L, int frame_idx, const bool shown) {
  frame_idx = lua_absindex(L, frame_idx);

  if (IsFrameDrawLayerEnabled(L, frame_idx, kDrawLayerHighlightName) == shown) {
    return;
  }

  SetFrameDrawLayerEnabled(L, frame_idx, kDrawLayerHighlightName, shown);
  SyncFrameRegionsForDrawLayer(L, frame_idx, kDrawLayerHighlightName);
}

void SetFrameHighlightLock(lua_State *L, int frame_idx, const bool locked) {
  frame_idx = lua_absindex(L, frame_idx);

  if (IsFrameHighlightLocked(L, frame_idx) == locked) {
    return;
  }

  lua_pushboolean(L, locked ? 1 : 0);
  lua_setfield(L, frame_idx, kLuaFrameHighlightLockField);
  if (locked) {
    SetFrameHighlightLayerShown(L, frame_idx, true);
    return;
  }
  if (!FrameIsCurrentMouseFocus(L, frame_idx)) {
    SetFrameHighlightLayerShown(L, frame_idx, false);
  }
}

}

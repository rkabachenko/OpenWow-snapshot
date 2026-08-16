#pragma once

#include <cstddef>

struct lua_State;

namespace openwow::ui::game::frame_api {

inline constexpr const char* kLuaFrameDrawLayerStateField =
    "__ow_frame_draw_layers";
inline constexpr const char* kLuaRegionDrawLayerEnabledField =
    "__ow_draw_layer_enabled";

inline constexpr const char* kDrawLayerHighlightName = "HIGHLIGHT";

inline constexpr const char* kLuaFrameHighlightLockField = "__ow_btn_hl_locked";

bool TryCanonicalizeDrawLayerName(const char* requested,
                                  const char** canonical_name);
bool TryParseDrawLayerName(const char* requested, int* draw_layer_id);
const char* GetDrawLayerNameById(int draw_layer_id) noexcept;
const char* ReadRegionDrawLayerName(lua_State* lua, int region_index);
const char* ReadStoredTextureDrawLayerName(lua_State* lua, int texture_index);
void InitializeFrameDrawLayerState(lua_State* lua, int frame_index);
bool IsFrameDrawLayerEnabled(lua_State* lua, int frame_index,
                             const char* canonical_name);
void SetFrameDrawLayerEnabled(lua_State* lua, int frame_index,
                              const char* canonical_name, bool enabled);
void SyncRegionDrawLayerEnabled(lua_State* lua, int region_index);
void SyncFrameRegionsForDrawLayer(lua_State* lua, int frame_index,
                                  const char* canonical_name);

[[nodiscard]] bool IsFrameHighlightLocked(lua_State* lua, int frame_index);
void SetFrameHighlightLayerShown(lua_State* lua, int frame_index, bool shown);
void SetFrameHighlightLock(lua_State* lua, int frame_index, bool locked);

}

#pragma once

#include "openwow/game/tabard_frame.h"
#include "openwow/game/tabard_renderer.h"

#include <array>
#include <cstdint>

struct lua_State;

namespace openwow::ui::game::frame_api {

inline constexpr std::array<const char*, openwow::game::kTabardNumAxes>
    kTabardDesignFieldNames{"__ow_tabard_emblem_style",
                            "__ow_tabard_emblem_color",
                            "__ow_tabard_border_style",
                            "__ow_tabard_border_color",
                            "__ow_tabard_background_color"};

void WriteTabardDesignValues(
    lua_State* lua, int frame_index,
    const std::array<std::uint32_t, openwow::game::kTabardNumAxes>& values);
void WriteTabardDesignValuesAndRefreshPreview(
    lua_State* lua, int frame_index,
    const std::array<std::uint32_t, openwow::game::kTabardNumAxes>& values);
void InitializeTabardDesignValues(lua_State* lua, int frame_index);
int GetValidatedTabardTextureArg(lua_State* lua, const char* method_name);
void SetTabardEmblemRenderTarget(
    lua_State* lua, int texture_index,
    const openwow::game::TabardEmblemRenderTargetDescriptor& descriptor);
int TruncateLuaIntegerArgument(lua_State* lua, int index);
void StoreBoundUnitSequence(lua_State* lua, int frame_index,
                            std::uint32_t sequence_id);
[[nodiscard]] std::uint32_t ClampLuaNumberToClientU32(lua_State* lua,
                                                     int index);
void StoreModelCamera(lua_State* lua, int frame_index,
                      std::uint32_t camera_index);
void StoreModelSequenceTime(lua_State* lua, int frame_index,
                            std::uint32_t sequence_id,
                            std::uint32_t time_ms);
int ValidateAndReadModelSequence(lua_State* lua, const char* method_name);
void SuspendCharacterModelForHide(lua_State* lua, int frame_index);
void RestoreCharacterModelAfterShow(lua_State* lua, int frame_index);
void ClearDressUpPreviewState(lua_State* lua, int frame_index);
[[nodiscard]] std::array<std::uint32_t, openwow::game::kTabardNumAxes>
ReadTabardDesignValues(lua_State* lua, int frame_index);
int LuaPlayerModelSetUnit(lua_State* lua);
int LuaPlayerModelSetCreature(lua_State* lua);
int LuaPlayerModelRefreshUnit(lua_State* lua);
int LuaScriptTryOn(lua_State* lua);

}

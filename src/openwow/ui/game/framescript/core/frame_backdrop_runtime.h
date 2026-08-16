#pragma once

#include <array>
#include <optional>

struct lua_State;

namespace openwow::ui::widgets {
class CSimpleFrame;
struct BackdropInfo;
}

namespace openwow::ui::framexml {
struct UiFrame;
}

namespace openwow::ui::game::frame_api {

inline constexpr char kLuaBackdropField[] = "__ow_backdrop";
inline constexpr char kLuaBackdropColorRField[] = "__ow_bd_r";
inline constexpr char kLuaBackdropColorGField[] = "__ow_bd_g";
inline constexpr char kLuaBackdropColorBField[] = "__ow_bd_b";
inline constexpr char kLuaBackdropColorAField[] = "__ow_bd_a";
inline constexpr char kLuaBackdropBorderColorRField[] = "__ow_bdb_r";
inline constexpr char kLuaBackdropBorderColorGField[] = "__ow_bdb_g";
inline constexpr char kLuaBackdropBorderColorBField[] = "__ow_bdb_b";
inline constexpr char kLuaBackdropBorderColorAField[] = "__ow_bdb_a";

inline constexpr char kLuaBackdropPiecesField[] = "__ow_backdrop_pieces";

struct FrameBackdropColors {
  std::array<float, 4> background{1.0F, 1.0F, 1.0F, 1.0F};
  std::array<float, 4> border{1.0F, 1.0F, 1.0F, 1.0F};
};

void ApplyFrameXmlBackdropDefinition(
    lua_State* lua, int frame_index,
    const openwow::ui::framexml::UiFrame& frame);

void RebuildRuntimeBackdropPieces(lua_State* lua, int frame_index);
void ClearRuntimeBackdropPieces(lua_State* lua, int frame_index);
void ApplyRuntimeBackdropPieceColors(lua_State* lua, int frame_index,
                                     bool border, float red, float green,
                                     float blue, float alpha);
[[nodiscard]] std::optional<FrameBackdropColors> ReadFrameBackdropColors(
    lua_State* lua, int frame_index);

bool HasLuaBackdropShadow(lua_State* lua, int frame_index);
[[nodiscard]] float NormalizeBackdropLuaColorComponent(double value);
void StoreLuaBackdropColorShadow(lua_State* lua, int table_index,
                                 const char* red_field,
                                 const char* green_field,
                                 const char* blue_field,
                                 const char* alpha_field, float red,
                                 float green, float blue, float alpha);
void ClearLuaBackdropShadow(lua_State* lua, int table_index);
[[nodiscard]] openwow::ui::widgets::BackdropInfo ReadLuaBackdropInfo(
    lua_State* lua, int table_index);
void StoreLuaBackdropShadow(
    lua_State* lua, int table_index,
    const openwow::ui::widgets::BackdropInfo& backdrop);
[[nodiscard]] openwow::ui::widgets::CSimpleFrame* GetLuaBackdropFrame(
    lua_State* lua, int frame_index);
bool TryReadLuaBackdropShadow(lua_State* lua, int frame_index,
                              openwow::ui::widgets::BackdropInfo* backdrop);
int EnsureBackdropResultTable(lua_State* lua);
void FillBackdropResultTable(lua_State* lua, int backdrop_index,
                             const openwow::ui::widgets::BackdropInfo& backdrop);
void PushLuaBackdropColorFieldOrDefault(lua_State* lua, int table_index,
                                        const char* field_name,
                                        float default_value);

}

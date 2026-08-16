#pragma once

#include <array>
#include <cstdint>

struct lua_State;

namespace openwow::ui::game::frame_api {

using PackedColorFieldNames = std::array<const char*, 4>;
using PackedColorDefaultValues = std::array<double, 4>;

inline constexpr PackedColorFieldNames kShadowColorFieldNames{
    "__ow_shadow_r", "__ow_shadow_g", "__ow_shadow_b", "__ow_shadow_a"};
inline constexpr PackedColorDefaultValues kShadowColorDefaults{0.0, 0.0, 0.0,
                                                                1.0};
inline constexpr PackedColorFieldNames kTextureVertexColorFieldNames{
    "__ow_vc_r", "__ow_vc_g", "__ow_vc_b", "__ow_vc_a"};
inline constexpr PackedColorDefaultValues kTextureVertexColorDefaults{
    1.0, 1.0, 1.0, 1.0};

[[nodiscard]] double NormalizeScriptColorByte(std::uint8_t value);
[[nodiscard]] std::uint8_t QuantizeScriptColorByte(double value);
[[nodiscard]] double GetScriptColorArgumentOrDefault(lua_State* lua,
                                                     int argument_index,
                                                     double default_value);
void PushPackedColor(lua_State* lua, int table_index,
                     const PackedColorFieldNames& field_names,
                     const PackedColorDefaultValues& default_values);
void PushPackedTextColor(lua_State* lua, int table_index);
void StorePackedColor(lua_State* lua, int table_index,
                      const PackedColorFieldNames& field_names,
                      const PackedColorDefaultValues& default_values);
void StorePackedTextColor(lua_State* lua, int table_index);
[[nodiscard]] std::uint8_t ReadSharedFontAlphaByteOrDefault(
    lua_State* lua, int table_index, std::uint8_t fallback = 0xFF);
void StoreSharedFontAlphaByte(lua_State* lua, int table_index,
                              std::uint8_t alpha_byte);
void SyncSharedFontAlphaFromTextColor(lua_State* lua, int table_index);
[[nodiscard]] std::uint8_t ReadTextureAlphaByteOrDefault(
    lua_State* lua, int table_index, std::uint8_t fallback = 0xFF);
void StoreTextureAlphaByte(lua_State* lua, int table_index,
                           std::uint8_t alpha_byte);

}

#pragma once

#include "openwow/render/resources/fonts/text_layout.h"

#include <initializer_list>
#include <optional>
#include <string>

struct lua_State;

namespace openwow::ui::game::frame_api {

int ValidateSharedFontObjectSelf(lua_State* lua);
int SetPackedTextColorForTypedObject(lua_State* lua,
                                     const char* expected_type);
int GetPackedTextColorForTypedObject(lua_State* lua,
                                     const char* expected_type);
void SetIndentedWordWrapForTypedObject(lua_State* lua,
                                       const char* expected_type,
                                       const char* field_name);
int GetIndentedWordWrapForTypedObject(lua_State* lua,
                                      const char* expected_type,
                                      const char* field_name);
void StoreShadowOffsetForObject(lua_State* lua, int self_index, float x_pixels,
                                float y_pixels);
void PushShadowOffsetComponentForObject(lua_State* lua, int self_index,
                                        const char* field_name);
void ClearBoundFontObject(lua_State* lua, int target_index);
void SetBoundFontObject(lua_State* lua, int target_index, int font_index);
[[nodiscard]] bool WouldIntroduceFontObjectBindingCycle(lua_State* lua,
                                                        int target_index,
                                                        int candidate_index);

[[nodiscard]] float ResolveLuaFontStringRasterScale(lua_State* lua,
                                                    int font_string_index);

[[nodiscard]] std::optional<openwow::render::text::TextLayout>
MeasureLuaFontStringMetrics(lua_State* lua, int font_string_index);
int SetTableJustifyField(lua_State* lua, const char* expected_type,
                         const char* field_name, const char* method_name,
                         bool horizontal);
int PushLuaFontStringGetFontResults(lua_State* lua, int font_string_index);
int ValidateFontStringTextSelf(lua_State* lua, const char* method_name);
void PropagateSharedFontFaceStyle(lua_State* lua, int self_index);
void PropagateSharedFontShadowStyle(lua_State* lua, int self_index);
void PropagateSharedFontTextColorStyle(lua_State* lua, int self_index);
void PropagateSharedFontAlphaStyle(lua_State* lua, int self_index);
void PropagateSharedFontLayoutStyle(lua_State* lua, int self_index);
void PropagateSharedFontSpacingStyle(lua_State* lua, int self_index);
void CopyNamedFontObjectStyleGroup(
    lua_State* lua, int target_index, int font_index,
    std::initializer_list<const char*> field_names);
void CopyFontObjectStateFromSource(lua_State* lua, int target_index,
                                   int source_index);
void ApplyFontObjectMethods(lua_State* lua, int table_index);
int PushSharedFontObjectName(lua_State* lua);
int PushFontObjectJustify(lua_State* lua, const char* field_name,
                          const char* default_value, bool horizontal);
[[nodiscard]] std::string NormalizeNamedFontObjectKey(const char* name);
int EnsureNamedFontObjectRegistry(lua_State* lua);
void BindNamedFontObjectGlobalIfMissing(lua_State* lua, int font_index,
                                        const char* name);
void ApplyFontStringIdentityMethods(lua_State* lua, int table_index);
void InstallTypedFontJustifyPair(lua_State* lua, int table_index,
                                 const char* type_name, bool horizontal,
                                 const char* default_value);
void InstallTypedFontObjectPair(lua_State* lua, int table_index,
                                const char* type_name);
void InstallTypedFontShadowMethods(lua_State* lua, int table_index,
                                   const char* type_name);
void InstallTypedFontSpacingPair(lua_State* lua, int table_index,
                                 const char* type_name);
int SetPackedShadowColorForSharedFontObject(lua_State* lua);
int GetPackedShadowColorForSharedFontObject(lua_State* lua);
int SetShadowOffsetForSharedFontObject(lua_State* lua);
int GetShadowOffsetForSharedFontObject(lua_State* lua);
bool PushNamedFontObject(lua_State* lua, const char* name);
void RegisterNamedFontObject(lua_State* lua, int font_index, const char* name);
void ClearNamedFontObjectRegistry(lua_State* lua);
void CopyNamedFontObjectStyle(lua_State* lua, int target_index,
                              int font_index);
void ClearBoundFontObjectByRef(lua_State* lua, int script_object_ref);
int LuaCreateFont(lua_State* lua);

inline constexpr const char* kNamedFontObjectRegistryKey =
    "openwow.game.named_font_objects";
inline constexpr const char* kFontDependentsField = "__ow_font_dependents";

}

#pragma once

#include "openwow/ui/glue/glue_lua_api_internal.h"
#include "openwow/ui/frame_script_type_info.h"
#include "openwow/ui/framexml/framexml_parser_detail.h"
#include "openwow/render/resources/fonts/text_layout.h"

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::glue::detail {

int LuaShowUIPanel(lua_State* state);
int LuaHideUIPanel(lua_State* state);

extern const char* const kGlueMovableField;
extern const char* const kGlueResizableField;
extern const char* const kGlueToplevelField;
extern const char* const kGlueUserPlacedField;
extern const char* const kGlueMouseWheelEnabledField;
extern const char* const kGlueMouseEnabledField;
extern const char* const kGlueKeyboardEnabledField;
extern const char* const kGlueJoystickEnabledField;
extern const char* const kGlueRegisteredDragButtonMaskField;
extern const char* const kGlueTitleRegionField;
extern const char* const kGlueTitleRegionNameSuffix;
extern const char* const kWidgetMethodsRegistryKey;
extern const char* const kWidgetTablesRegistryKey;
extern const float kMinPositiveTextHeightPixels;
extern const double kSimpleWidgetWriteEpsilon;
extern const std::uint32_t kModelSequenceCount;

struct ScriptBackdropColorArgs {
  float red{0.0F};
  float green{0.0F};
  float blue{0.0F};
  float alpha{1.0F};
};

struct GlueWidgetBoundsPixels {
  float left{std::numeric_limits<float>::max()};
  float top{std::numeric_limits<float>::max()};
  float right{std::numeric_limits<float>::lowest()};
  float bottom{std::numeric_limits<float>::lowest()};

  [[nodiscard]] bool valid() const;
  void Include(float x, float y, float width, float height);
};

void EnsureWidgetMethodTable(lua_State* state);
void BindWidgetObjectTypeMethods(lua_State* state,
                                const std::string& widget_kind);
void StoreWidgetTableByRuntimeKey(lua_State* state,
                                  const std::string& runtime_key,
                                  int table_index);
bool PushStoredWidgetTableByRuntimeKey(lua_State* state,
                                       const std::string& runtime_key);
std::string ReadStoredWidgetRuntimeKey(lua_State* state, int table_index);
bool PushWidgetTableByRuntimeKey(lua_State* state,
                                 const std::string& runtime_key);
bool PushGlueWidgetGlobalTable(lua_State* state,
                               const std::string& widget_name);

std::string ExpandSimpleRenderScriptText(const char* text);
std::string ResolveScriptParentToken(GlueWidgetRuntime* runtime,
                                     const std::string& owner_name,
                                     std::string value);
bool IsFramePointToken(const char* value);
float NormalizePackedColorComponent(float value);
float GetScriptColorArgumentOrDefault(lua_State* state, int argument_index,
                                      float default_value);
ScriptBackdropColorArgs ParseScriptBackdropColorArgs(lua_State* state);
std::uint32_t ClampLuaNumberToClientU32(lua_State* state, int index);
int RuntimeModelIndexFromClientU32(std::uint32_t value);
int EnsureBackdropOutputTable(lua_State* state);
void FillBackdropOutputTable(
    lua_State* state, int backdrop_index,
    const openwow::ui::framexml::detail::BackdropSpec& backdrop);

std::optional<openwow::render::text::TextLayout> MeasureGlueFontString(
    GlueLuaRuntime* runtime, GlueWidgetRuntime* widget_runtime,
    const GlueWidgetState& widget);
float ConvertGlueWidgetPixelsToScriptDimension(
    const GlueWidgetRuntime& runtime, const std::string& widget_name,
    float pixels);
std::optional<GlueWidgetBoundsPixels> ResolveGlueWidgetBoundsPixels(
    lua_State* state, const std::string& root_name,
    const GlueWidgetState& root);
float ResolveGlueWidgetScriptDimension(lua_State* state,
                                       const std::string& widget_name,
                                       const GlueWidgetState& widget,
                                       bool horizontal, bool use_explicit);

void StoreGlueFrameId(lua_State* state, int index, int id);
int ReadGlueFrameId(lua_State* state, int index);
void SetGlueFrameBooleanField(lua_State* state, int index,
                              const char* field, bool value);
bool GetGlueFrameBooleanField(lua_State* state, int index,
                              const char* field);
std::string GetCheckedGlueWidgetName(lua_State* state);
std::string GetCheckedGlueFrameWidgetName(lua_State* state);
std::string GetCheckedGlueEditBoxWidgetName(lua_State* state);
std::string GetCheckedGlueRegionWidgetName(lua_State* state);
const openwow::ui::FrameScriptTypeInfo* ResolveGlueFrameScriptTypeInfo(
    lua_State* state, int self_index, int handler_index);
std::vector<std::string> CollectGlueFrameNamesInCreationOrder(
    GlueWidgetRuntime& runtime);
bool IsGlueFrameLikeForEnumeration(const GlueWidgetState& widget);
bool GlueWidgetMatchesFrameType(const GlueWidgetState& widget);

std::string ReadGlueTableStringField(lua_State* state, int index,
                                     const char* field_name);
std::string ReadGlueWidgetRuntimeKey(lua_State* state, int index);
bool IsGlueFrameLikeType(std::string_view frame_type);
int ReadCreateFrameNumericId(lua_State* state, int index);
void PushAnonymousGlueFrame(lua_State* state, std::string_view frame_type,
                            const std::string& runtime_key, int id);

bool EqualsIgnoreCaseAscii(const char* lhs, const char* rhs);
std::uint32_t ParseRegisteredMouseButtonMask(lua_State* state,
                                             int first_argument);
GlueWidgetState GetCheckedFontStringWidget(lua_State* state);
std::string GetUsageWidgetName(lua_State* state);

std::optional<GlueWidgetState> GetWidgetOrUiParent(
    lua_State* state, const std::string& name);
std::optional<GlueWidgetState> GetRectWidgetOrUiParent(
    lua_State* state, const std::string& name);
float GetOptionalMouseOverInset(lua_State* state, int argument_index);
std::optional<std::pair<float, float>> ResolveGlueCursorPositionPixels(
    lua_State* state);

}

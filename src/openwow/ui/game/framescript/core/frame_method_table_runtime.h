#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

inline constexpr const char* kGameTooltipMethodTableRegistryKey =
    "openwow.game.tooltip_method_table_ref";
inline constexpr const char* kCooldownMethodTableRegistryKey =
    "openwow.game.cooldown_method_table_ref";
inline constexpr const char* kFrameMethodTableRegistryKey =
    "openwow.game.frame_method_table_ref";
inline constexpr const char* kSimpleFrameLayoutMethodTableRegistryKey =
    "openwow.game.simple_frame_layout_method_table_ref";
inline constexpr const char* kTextureMethodTableRegistryKey =
    "openwow.game.texture_method_table_ref";
inline constexpr const char* kFontStringMethodTableRegistryKey =
    "openwow.game.fontstring_method_table_ref";
inline constexpr const char* kFontObjectMethodTableRegistryKey =
    "openwow.game.font_object_method_table_ref";
inline constexpr const char* kModelMethodTableRegistryKey =
    "openwow.game.model_method_table_ref";
inline constexpr const char* kPlayerModelMethodTableRegistryKey =
    "openwow.game.player_model_method_table_ref";
inline constexpr const char* kMovieFrameMethodTableRegistryKey =
    "openwow.game.movie_frame_method_table_ref";
inline constexpr const char* kDressUpModelMethodTableRegistryKey =
    "openwow.game.dressup_model_method_table_ref";
inline constexpr const char* kTabardModelMethodTableRegistryKey =
    "openwow.game.tabard_model_method_table_ref";
inline constexpr const char* kQuestPOIFrameMethodTableRegistryKey =
    "openwow.game.quest_poi_frame_method_table_ref";
inline constexpr const char* kMinimapMethodTableRegistryKey =
    "openwow.game.minimap_method_table_ref";
inline constexpr const char* kButtonMethodTableRegistryKey =
    "openwow.game.button_method_table_ref";
inline constexpr const char* kCheckButtonMethodTableRegistryKey =
    "openwow.game.check_button_method_table_ref";
inline constexpr const char* kEditBoxMethodTableRegistryKey =
    "openwow.game.edit_box_method_table_ref";
inline constexpr const char* kSimpleHTMLMethodTableRegistryKey =
    "openwow.game.simple_html_method_table_ref";
inline constexpr const char* kMessageFrameMethodTableRegistryKey =
    "openwow.game.message_frame_method_table_ref";
inline constexpr const char* kScrollingMessageFrameMethodTableRegistryKey =
    "openwow.game.scrolling_message_frame_method_table_ref";
inline constexpr const char* kScrollFrameMethodTableRegistryKey =
    "openwow.game.scroll_frame_method_table_ref";
inline constexpr const char* kSliderMethodTableRegistryKey =
    "openwow.game.slider_method_table_ref";
inline constexpr const char* kStatusBarMethodTableRegistryKey =
    "openwow.game.status_bar_method_table_ref";
inline constexpr const char* kColorSelectMethodTableRegistryKey =
    "openwow.game.color_select_method_table_ref";
inline constexpr const char* kSimpleModelBaseMethodTableRegistryKey =
    "openwow.game.simple_model_base_method_table_ref";

bool PushRegisteredMethodTable(lua_State* lua, const char* registry_key);
bool IsFrameTypeMethodAutoRegistrationEnabled(lua_State* lua);
void SelfIndexMethodTable(lua_State* lua, int table_index);
void RemoveFunctionFieldsFromTable(lua_State* lua, int table_index);
void CacheFunctionFieldsAsMethodTable(lua_State* lua, int object_index,
                                      const char* registry_key);
void ApplyCachedMethodTableAndStripFunctions(lua_State* lua, int object_index,
                                             const char* registry_key);
void CopyRegisteredMethodTableFields(lua_State* lua, const char* registry_key,
                                     int target_index);
void UnregisterCachedMethodTable(lua_State* lua, const char* registry_key);
void ApplyRegisteredMethodTableAsMetatable(lua_State* lua,
                                           const char* registry_key);
bool TryAttachCachedMethodTableToFreshInstance(lua_State* lua,
                                               int object_index,
                                               const char* registry_key);

void EnsureFrameTypeMethodTableRegistered(lua_State* lua,
                                          const char* frame_type);

}

#pragma once

struct lua_State;

namespace openwow::ui::game::frame_api {

void RegisterFrameScriptMethodsImpl(lua_State* lua);
void ApplyRegisteredFrameMethodsImpl(lua_State* lua);
void ApplyFrameTypeMethodsImpl(lua_State* lua, const char* frame_type);

void ApplyFrameTypeMethods(lua_State* lua, const char* frame_type);
void SetFrameTypeMethodAutoRegistration(lua_State* lua, bool enabled);

void RegisterFrameScriptMethods(lua_State* lua);
void UnregisterFrameScriptMethods(lua_State* lua);
void ApplyRegisteredFrameMethods(lua_State* lua);
void RegisterSimpleFrameLayoutMethods(lua_State* lua);
void UnregisterSimpleFrameLayoutMethods(lua_State* lua);
void ApplyRegisteredSimpleFrameLayoutMethods(lua_State* lua);

void RegisterButtonScriptMethods(lua_State* lua);
void UnregisterButtonScriptMethods(lua_State* lua);
void RegisterCheckButtonScriptMethods(lua_State* lua);
void UnregisterCheckButtonScriptMethods(lua_State* lua);
void RegisterEditBoxScriptMethods(lua_State* lua);
void UnregisterEditBoxScriptMethods(lua_State* lua);
void RegisterSimpleHTMLScriptMethods(lua_State* lua);
void UnregisterSimpleHTMLScriptMethods(lua_State* lua);
void RegisterMessageFrameScriptMethods(lua_State* lua);
void UnregisterMessageFrameScriptMethods(lua_State* lua);
void RegisterScrollingMessageFrameScriptMethods(lua_State* lua);
void UnregisterScrollingMessageFrameScriptMethods(lua_State* lua);
void RegisterScrollFrameScriptMethods(lua_State* lua);
void UnregisterScrollFrameScriptMethods(lua_State* lua);
void RegisterSliderScriptMethods(lua_State* lua);
void UnregisterSliderScriptMethods(lua_State* lua);
void RegisterStatusBarScriptMethods(lua_State* lua);
void UnregisterStatusBarScriptMethods(lua_State* lua);
void RegisterColorSelectScriptMethods(lua_State* lua);
void UnregisterColorSelectScriptMethods(lua_State* lua);
void RegisterCooldownScriptMethods(lua_State* lua);
void UnregisterCooldownScriptMethods(lua_State* lua);
void ApplyRegisteredCooldownMethods(lua_State* lua);
void RegisterMinimapScriptMethods(lua_State* lua);
void UnregisterMinimapScriptMethods(lua_State* lua);
void RegisterMovieFrameScriptMethods(lua_State* lua);
void UnregisterMovieFrameScriptMethods(lua_State* lua);
void RegisterQuestPOIFrameScriptMethods(lua_State* lua);
void UnregisterQuestPOIFrameScriptMethods(lua_State* lua);
void RegisterGameTooltipScriptMethods(lua_State* lua);
void UnregisterGameTooltipScriptMethods(lua_State* lua);
void ApplyRegisteredGameTooltipMethods(lua_State* lua);

void RegisterSimpleModelBaseScriptMethods(lua_State* lua);
void UnregisterSimpleModelBaseScriptMethods(lua_State* lua);
void RegisterModelScriptMethods(lua_State* lua);
void UnregisterModelScriptMethods(lua_State* lua);
void ApplyRegisteredModelMethods(lua_State* lua);
void RegisterPlayerModelScriptMethods(lua_State* lua);
void UnregisterPlayerModelScriptMethods(lua_State* lua);
void ApplyRegisteredPlayerModelMethods(lua_State* lua);
void RegisterDressUpModelScriptMethods(lua_State* lua);
void UnregisterDressUpModelScriptMethods(lua_State* lua);
void RegisterTabardModelScriptMethods(lua_State* lua);
void UnregisterTabardModelScriptMethods(lua_State* lua);

void RegisterTextureScriptMethods(lua_State* lua);
void UnregisterTextureScriptMethods(lua_State* lua);
void RegisterFontStringScriptMethods(lua_State* lua);
void UnregisterFontStringScriptMethods(lua_State* lua);
void RegisterFontObjectScriptMethods(lua_State* lua);
void UnregisterFontObjectScriptMethods(lua_State* lua);

}

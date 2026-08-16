#pragma once

#include "openwow/ui/runtime/lua/lua_composition.h"

struct lua_State;

namespace openwow::ui::game::detail {

int LuaKBArticle_BeginLoading(lua_State* state);
int LuaKBArticle_IsLoaded(lua_State* state);
int LuaKBArticle_GetData(lua_State* state);
int LuaKBQuery_BeginLoading(lua_State* state);
int LuaKBQuery_IsLoaded(lua_State* state);
int LuaKBQuery_GetArticleHeaderCount(lua_State* state);
int LuaKBQuery_GetArticleHeaderData(lua_State* state);
int LuaKBQuery_GetTotalArticleCount(lua_State* state);
int LuaKBSetup_BeginLoading(lua_State* state);
int LuaKBSetup_IsLoaded(lua_State* state);
int LuaKBSetup_GetArticleHeaderCount(lua_State* state);
int LuaKBSetup_GetArticleHeaderData(lua_State* state);
int LuaKBSetup_GetCategoryCount(lua_State* state);
int LuaKBSetup_GetCategoryData(lua_State* state);
int LuaKBSetup_GetLanguageCount(lua_State* state);
int LuaKBSetup_GetLanguageData(lua_State* state);
int LuaKBSetup_GetSubCategoryCount(lua_State* state);
int LuaKBSetup_GetSubCategoryData(lua_State* state);
int LuaKBSetup_GetTotalArticleCount(lua_State* state);
int LuaKBSystem_GetMOTD(lua_State* state);
int LuaKBSystem_GetServerNotice(lua_State* state);
int LuaKBSystem_GetServerStatus(lua_State* state);

[[nodiscard]] openwow::ui::lua::NativeBindingCatalog
KnowledgeBaseNativeBindingCatalog();

}

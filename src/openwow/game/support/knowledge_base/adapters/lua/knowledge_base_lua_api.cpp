#include "openwow/game/support/knowledge_base/adapters/lua/knowledge_base_lua_api.h"
#include "openwow/game/knowledge_base.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

#include <cstring>

namespace openwow::ui::game::detail {

namespace {

constexpr openwow::ui::LuaGlobalBinding kKnowledgeBaseBindings[] = {
    {"KBArticle_BeginLoading", LuaKBArticle_BeginLoading},
    {"KBArticle_IsLoaded", LuaKBArticle_IsLoaded},
    {"KBArticle_GetData", LuaKBArticle_GetData},
    {"KBQuery_BeginLoading", LuaKBQuery_BeginLoading},
    {"KBQuery_IsLoaded", LuaKBQuery_IsLoaded},
    {"KBQuery_GetArticleHeaderCount", LuaKBQuery_GetArticleHeaderCount},
    {"KBQuery_GetArticleHeaderData", LuaKBQuery_GetArticleHeaderData},
    {"KBSetup_BeginLoading", LuaKBSetup_BeginLoading},
    {"KBSetup_IsLoaded", LuaKBSetup_IsLoaded},
    {"KBSetup_GetCategoryCount", LuaKBSetup_GetCategoryCount},
    {"KBQuery_GetTotalArticleCount", LuaKBQuery_GetTotalArticleCount},
    {"KBSetup_GetArticleHeaderCount", LuaKBSetup_GetArticleHeaderCount},
    {"KBSetup_GetArticleHeaderData", LuaKBSetup_GetArticleHeaderData},
    {"KBSetup_GetCategoryData", LuaKBSetup_GetCategoryData},
    {"KBSetup_GetLanguageCount", LuaKBSetup_GetLanguageCount},
    {"KBSetup_GetLanguageData", LuaKBSetup_GetLanguageData},
    {"KBSetup_GetSubCategoryCount", LuaKBSetup_GetSubCategoryCount},
    {"KBSetup_GetSubCategoryData", LuaKBSetup_GetSubCategoryData},
    {"KBSetup_GetTotalArticleCount", LuaKBSetup_GetTotalArticleCount},
    {"KBSystem_GetMOTD", LuaKBSystem_GetMOTD},
    {"KBSystem_GetServerNotice", LuaKBSystem_GetServerNotice},
    {"KBSystem_GetServerStatus", LuaKBSystem_GetServerStatus},
};

void PushOptionalString(lua_State* state,
                        const std::optional<std::string>& value) {
  if (!value.has_value()) {
    lua_pushnil(state);
    return;
  }

  lua_pushlstring(state, value->data(), value->size());
}

lua_Number GetFrameScriptNumberArgumentOrZero(lua_State* state,
                                              const int index) {
  return lua_isnumber(state, index) != 0 ? lua_tonumber(state, index) : 0.0;
}

}

int LuaKBArticle_BeginLoading(lua_State* L) {
  auto& knowledge_base = ::openwow::game::KnowledgeBase::Get();
  const auto article_id =
      static_cast<std::int32_t>(luaL_optnumber(L, 1, 0));
  const auto search_type =
      static_cast<std::int32_t>(luaL_optnumber(L, 2, 0));
  (void)knowledge_base.BeginArticleLoading(article_id, search_type);
  return 0;
}

int LuaKBArticle_IsLoaded(lua_State* L) {
  const auto loaded =
      ::openwow::game::KnowledgeBase::Get().GetArticleState()
      == ::openwow::game::KBState::kLoaded;
  lua_pushboolean(L, loaded ? 1 : 0);
  return 1;
}

int LuaKBArticle_GetData(lua_State* L) {
  if (::openwow::game::KnowledgeBase::Get().GetArticleState()
      != ::openwow::game::KBState::kLoaded) {
    return luaL_error(L,
                      "KBArticle_GetData() failed because article is not loaded");
  }

  const auto& article = ::openwow::game::KnowledgeBase::Get().GetArticle();
  lua_pushnumber(L, article.id);
  PushOptionalString(L, article.subject);
  PushOptionalString(L, article.subject_alt);
  PushOptionalString(L, article.body);
  PushOptionalString(L, article.keyword);
  lua_pushnumber(L, article.language_id);
  lua_pushboolean(L, article.hot_issue ? 1 : 0);
  return 7;
}

int LuaKBQuery_BeginLoading(lua_State* L) {
  auto& knowledge_base = ::openwow::game::KnowledgeBase::Get();
  if (knowledge_base.GetSetupState() != ::openwow::game::KBState::kLoaded) {
    return luaL_error(L,
                      "KBQuery_BeginLoading() failed because setup is not loaded");
  }

  const char* const search = lua_tostring(L, 1);
  if (search == nullptr) {
    return luaL_error(
        L,
        "KBQuery_BeginLoading() called with a null string for search query");
  }

  if (std::strlen(search) > 128u) {
    return luaL_error(
        L,
        "KBQuery_BeginLoading() called with a string > 128 bytes for search query");
  }

  const auto category_selection =
      static_cast<int>(GetFrameScriptNumberArgumentOrZero(L, 2));
  const auto subcategory_selection =
      static_cast<int>(GetFrameScriptNumberArgumentOrZero(L, 3));
  const auto num_articles =
      static_cast<std::int32_t>(GetFrameScriptNumberArgumentOrZero(L, 4));
  const auto page_number =
      static_cast<std::int32_t>(GetFrameScriptNumberArgumentOrZero(L, 5));

  std::int32_t category_id = -1;
  if (subcategory_selection != 0) {
    if (category_selection == 0) {
      return luaL_error(
          L,
          "KBQuery_BeginLoading() called with subcategory without category");
    }

    const auto* const subcategory =
        category_selection > 0 && subcategory_selection > 0
            ? knowledge_base.GetSubCategory(
                  static_cast<std::uint32_t>(category_selection - 1),
                  static_cast<std::uint32_t>(subcategory_selection - 1))
            : nullptr;
    if (subcategory == nullptr) {
      return luaL_error(
          L,
          "KBSetup_GetCategoryData() called with invalid category index");
    }

    category_id = static_cast<std::int32_t>(subcategory->id);
  } else if (category_selection != 0) {
    const auto* const category =
        category_selection > 0
            ? knowledge_base.GetCategory(
                  static_cast<std::uint32_t>(category_selection - 1))
            : nullptr;
    if (category == nullptr) {
      return luaL_error(
          L,
          "KBSetup_GetCategoryData() called with invalid category index");
    }

    category_id = static_cast<std::int32_t>(category->id);
  }

  (void)knowledge_base.BeginQueryLoading(search, category_id,
                                         num_articles, page_number);
  return 0;
}

int LuaKBQuery_IsLoaded(lua_State* L) {
  const auto loaded =
      ::openwow::game::KnowledgeBase::Get().GetQueryState()
      == ::openwow::game::KBState::kLoaded;
  lua_pushboolean(L, loaded ? 1 : 0);
  return 1;
}

int LuaKBQuery_GetArticleHeaderCount(lua_State* L) {
  if (::openwow::game::KnowledgeBase::Get().GetQueryState()
      != ::openwow::game::KBState::kLoaded) {
    return luaL_error(L,
                      "KBQuery_GetArticleHeaderCount() failed because query is not loaded");
  }

  lua_pushnumber(
      L, ::openwow::game::KnowledgeBase::Get().GetQueryArticleHeaderCount());
  return 1;
}

int LuaKBQuery_GetArticleHeaderData(lua_State* L) {
  if (::openwow::game::KnowledgeBase::Get().GetQueryState()
      != ::openwow::game::KBState::kLoaded) {
    return luaL_error(
        L, "KBQuery_GetArticleHeaderData() failed because query is not loaded");
  }

  const auto index =
      static_cast<int>(GetFrameScriptNumberArgumentOrZero(L, 1)) - 1;
  const auto* const header =
      index >= 0
          ? ::openwow::game::KnowledgeBase::Get().GetQueryArticleHeader(
                static_cast<std::uint32_t>(index))
          : nullptr;
  if (!header) {
    return luaL_error(
        L,
        "KBQuery_GetArticleHeaderData() called with invalid article header index");
  }

  lua_pushnumber(L, header->id);
  PushOptionalString(L, header->subject);
  lua_pushboolean(L, header->hot_issue ? 1 : 0);
  lua_pushboolean(L, header->updated ? 1 : 0);
  return 4;
}

int LuaKBSetup_BeginLoading(lua_State* L) {
  const auto num_articles =
      static_cast<std::uint32_t>(luaL_optnumber(L, 1, 0));
  const auto page_number =
      static_cast<std::uint32_t>(luaL_optnumber(L, 2, 0));
  (void)::openwow::game::KnowledgeBase::Get().BeginSetupLoading(
      num_articles, page_number);
  return 0;
}

int LuaKBSetup_IsLoaded(lua_State* L) {
  const auto loaded =
      ::openwow::game::KnowledgeBase::Get().GetSetupState()
      == ::openwow::game::KBState::kLoaded;
  lua_pushboolean(L, loaded ? 1 : 0);
  return 1;
}

int LuaKBSetup_GetCategoryCount(lua_State* L) {
  if (::openwow::game::KnowledgeBase::Get().GetSetupState()
      != ::openwow::game::KBState::kLoaded) {
    return luaL_error(L,
                      "KBSetup_GetCategoryCount() failed because setup is not loaded");
  }

  lua_pushnumber(L, ::openwow::game::KnowledgeBase::Get().GetCategoryCount());
  return 1;
}

int LuaKBQuery_GetTotalArticleCount(lua_State* L) {
  if (::openwow::game::KnowledgeBase::Get().GetQueryState()
      != ::openwow::game::KBState::kLoaded) {
    return luaL_error(L,
                      "KBQuery_GetTotalArticleCount() failed because query is not loaded");
  }

  lua_pushnumber(
      L, ::openwow::game::KnowledgeBase::Get().GetQueryTotalArticleCount());
  return 1;
}

int LuaKBSetup_GetArticleHeaderCount(lua_State* L) {
  if (::openwow::game::KnowledgeBase::Get().GetSetupState()
      != ::openwow::game::KBState::kLoaded) {
    return luaL_error(L,
                      "KBSetup_GetArticleHeaderCount() failed because setup is not loaded");
  }

  lua_pushnumber(
      L, ::openwow::game::KnowledgeBase::Get().GetSetupArticleHeaderCount());
  return 1;
}

int LuaKBSetup_GetArticleHeaderData(lua_State* L) {
  if (::openwow::game::KnowledgeBase::Get().GetSetupState()
      != ::openwow::game::KBState::kLoaded) {
    return luaL_error(
        L, "KBSetup_GetArticleHeaderData() failed because setup is not loaded");
  }

  const auto index =
      static_cast<int>(GetFrameScriptNumberArgumentOrZero(L, 1)) - 1;
  const auto* const header =
      index >= 0
          ? ::openwow::game::KnowledgeBase::Get().GetSetupArticleHeader(
                static_cast<std::uint32_t>(index))
          : nullptr;
  if (!header) {
    return luaL_error(
        L,
        "KBSetup_GetArticleHeaderData() called with invalid article header index");
  }

  lua_pushnumber(L, header->id);
  PushOptionalString(L, header->subject);
  lua_pushboolean(L, header->hot_issue ? 1 : 0);
  lua_pushboolean(L, header->updated ? 1 : 0);
  return 4;
}

int LuaKBSetup_GetCategoryData(lua_State* L) {
  if (::openwow::game::KnowledgeBase::Get().GetSetupState()
      != ::openwow::game::KBState::kLoaded) {
    return luaL_error(L,
                      "KBSetup_GetCategoryData() failed because setup is not loaded");
  }

  const auto index =
      static_cast<int>(GetFrameScriptNumberArgumentOrZero(L, 1)) - 1;
  const auto* const category =
      index >= 0 ? ::openwow::game::KnowledgeBase::Get().GetCategory(
                       static_cast<std::uint32_t>(index))
                 : nullptr;
  if (!category) {
    return luaL_error(L,
                      "KBSetup_GetCategoryData() called with invalid category index");
  }

  lua_pushnumber(L, category->id);
  PushOptionalString(L, category->name);
  return 2;
}

int LuaKBSetup_GetLanguageCount(lua_State* L) {
  if (::openwow::game::KnowledgeBase::Get().GetSetupState()
      != ::openwow::game::KBState::kLoaded) {
    return luaL_error(L,
                      "KBSetup_GetLanguageCount() failed because setup is not loaded");
  }

  lua_pushnumber(L, ::openwow::game::KnowledgeBase::Get().GetLanguageCount());
  return 1;
}

int LuaKBSetup_GetLanguageData(lua_State* L) {
  if (::openwow::game::KnowledgeBase::Get().GetSetupState()
      != ::openwow::game::KBState::kLoaded) {
    return luaL_error(L,
                      "KBSetup_GetLanguageData() failed because setup is not loaded");
  }

  const auto index =
      static_cast<int>(GetFrameScriptNumberArgumentOrZero(L, 1)) - 1;
  const auto* const language =
      index >= 0 ? ::openwow::game::KnowledgeBase::Get().GetLanguage(
                       static_cast<std::uint32_t>(index))
                 : nullptr;
  if (!language) {
    return luaL_error(
        L, "KBSetup_GetLanguageData() called with invalid category index");
  }

  lua_pushnumber(L, language->id);
  PushOptionalString(L, language->name);
  return 2;
}

int LuaKBSetup_GetSubCategoryCount(lua_State* L) {
  if (::openwow::game::KnowledgeBase::Get().GetSetupState()
      != ::openwow::game::KBState::kLoaded) {
    return luaL_error(
        L, "KBSetup_GetSubCategoryCount() failed because setup is not loaded");
  }

  const auto category_index =
      static_cast<int>(GetFrameScriptNumberArgumentOrZero(L, 1)) - 1;
  const auto count =
      category_index >= 0
          ? ::openwow::game::KnowledgeBase::Get().GetSubCategoryCount(
                static_cast<std::uint32_t>(category_index))
          : 0;
  lua_pushnumber(L, count);
  return 1;
}

int LuaKBSetup_GetSubCategoryData(lua_State* L) {
  if (::openwow::game::KnowledgeBase::Get().GetSetupState()
      != ::openwow::game::KBState::kLoaded) {
    return luaL_error(
        L, "KBSetup_GetSubCategoryData() failed because setup is not loaded");
  }

  const auto cat =
      static_cast<int>(GetFrameScriptNumberArgumentOrZero(L, 1)) - 1;
  const auto sub =
      static_cast<int>(GetFrameScriptNumberArgumentOrZero(L, 2)) - 1;
  const auto* const category =
      cat >= 0 && sub >= 0
          ? ::openwow::game::KnowledgeBase::Get().GetSubCategory(
                static_cast<std::uint32_t>(cat),
                static_cast<std::uint32_t>(sub))
          : nullptr;
  if (!category) {
    return luaL_error(
        L,
        "KBSetup_GetSubCategoryData() called with invalid category or sub "
        "category index");
  }

  lua_pushnumber(L, category->id);
  PushOptionalString(L, category->name);
  return 2;
}

int LuaKBSetup_GetTotalArticleCount(lua_State* L) {
  if (::openwow::game::KnowledgeBase::Get().GetSetupState()
      != ::openwow::game::KBState::kLoaded) {
    return luaL_error(
        L, "KBSetup_GetTotalArticleCount() failed because setup is not loaded");
  }

  lua_pushnumber(
      L, ::openwow::game::KnowledgeBase::Get().GetSetupTotalArticleCount());
  return 1;
}

int LuaKBSystem_GetMOTD(lua_State* L) {
  const auto& system_motd = ::openwow::game::KnowledgeBase::Get().GetSystemMotd();
  lua_pushlstring(L, system_motd.c_str(), system_motd.size());
  return 1;
}

int LuaKBSystem_GetServerNotice(lua_State* L) {
  const auto& server_notice =
      ::openwow::game::KnowledgeBase::Get().GetServerNotice();
  lua_pushlstring(L, server_notice.c_str(), server_notice.size());
  return 1;
}

int LuaKBSystem_GetServerStatus(lua_State* L) {
  const auto& server_status =
      ::openwow::game::KnowledgeBase::Get().GetServerStatus();
  lua_pushlstring(L, server_status.c_str(), server_status.size());
  return 1;
}

openwow::ui::lua::NativeBindingCatalog KnowledgeBaseNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.support.knowledge_base", openwow::ui::lua::BindingScope::kWorld,
      kKnowledgeBaseBindings);
}

}

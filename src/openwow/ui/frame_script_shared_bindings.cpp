#include "openwow/ui/frame_script_shared_bindings.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui {

void FrameScript_RegisterSharedUiLuaBindings(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  game::frame_api::RegisterFontObjectScriptMethods(L);
  game::frame_api::RegisterTextureScriptMethods(L);
  game::frame_api::RegisterFontStringScriptMethods(L);

  game::frame_api::RegisterSimpleFrameLayoutMethods(L);
  game::frame_api::RegisterFrameScriptMethods(L);

  game::frame_api::RegisterButtonScriptMethods(L);
  game::frame_api::RegisterCheckButtonScriptMethods(L);
  game::frame_api::RegisterEditBoxScriptMethods(L);
  game::frame_api::RegisterSimpleHTMLScriptMethods(L);
  game::frame_api::RegisterMessageFrameScriptMethods(L);
  game::frame_api::RegisterScrollingMessageFrameScriptMethods(L);
  game::frame_api::RegisterSimpleModelBaseScriptMethods(L);
  game::frame_api::RegisterModelScriptMethods(L);
  game::frame_api::RegisterScrollFrameScriptMethods(L);
  game::frame_api::RegisterSliderScriptMethods(L);
  game::frame_api::RegisterStatusBarScriptMethods(L);
  game::frame_api::RegisterColorSelectScriptMethods(L);
  game::frame_api::RegisterMovieFrameScriptMethods(L);
}

void FrameScript_UnregisterSharedUiLuaBindings(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  game::frame_api::UnregisterMovieFrameScriptMethods(L);
  game::frame_api::UnregisterColorSelectScriptMethods(L);
  game::frame_api::UnregisterStatusBarScriptMethods(L);
  game::frame_api::UnregisterSliderScriptMethods(L);
  game::frame_api::UnregisterScrollFrameScriptMethods(L);
  game::frame_api::UnregisterModelScriptMethods(L);
  game::frame_api::UnregisterSimpleModelBaseScriptMethods(L);
  game::frame_api::UnregisterScrollingMessageFrameScriptMethods(L);
  game::frame_api::UnregisterMessageFrameScriptMethods(L);
  game::frame_api::UnregisterSimpleHTMLScriptMethods(L);
  game::frame_api::UnregisterEditBoxScriptMethods(L);
  game::frame_api::UnregisterCheckButtonScriptMethods(L);
  game::frame_api::UnregisterButtonScriptMethods(L);

  game::frame_api::UnregisterFrameScriptMethods(L);
  game::frame_api::UnregisterSimpleFrameLayoutMethods(L);

  game::frame_api::UnregisterFontStringScriptMethods(L);
  game::frame_api::UnregisterTextureScriptMethods(L);
  game::frame_api::UnregisterFontObjectScriptMethods(L);
}

}

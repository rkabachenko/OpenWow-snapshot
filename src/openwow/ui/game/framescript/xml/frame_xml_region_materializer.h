#pragma once

#include "openwow/ui/framexml/ui_frame.h"
#include "openwow/ui/game/framescript/xml/frame_template_resolver.h"

struct lua_State;

namespace openwow::ui::game::frame_api {

void ApplyResolvedTextureTemplates(lua_State* lua, int texture_index,
                                   const TemplateResolveResult& templates,
                                   bool explicit_draw_layer_argument);
void ApplyResolvedFontStringTemplates(lua_State* lua, int owner_index,
                                      int font_string_index,
                                      const TemplateResolveResult& templates,
                                      bool explicit_draw_layer_argument);

}

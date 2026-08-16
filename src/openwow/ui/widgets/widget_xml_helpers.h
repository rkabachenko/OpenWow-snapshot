
#pragma once

#include "openwow/ui/script_boolean.h"

namespace openwow::ui::xml {
struct ErrorContext;
struct XMLFrameDef;
}

namespace openwow::ui::widgets {

class CScriptRegion;

using openwow::ui::ScriptParseBoolStringOrDefault;

const char *FindAttributeValue(const openwow::ui::xml::XMLFrameDef &frame_def,
                               const char *name);
void LoadRegionLayoutFromXML(CScriptRegion &region,
                             const openwow::ui::xml::XMLFrameDef &frame_def,
                             openwow::ui::xml::ErrorContext *error_handler);

}

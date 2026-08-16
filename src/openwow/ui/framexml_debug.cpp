#include "openwow/ui/framexml_debug.h"

namespace openwow::ui {
namespace {

int g_frame_xml_debug_level = kDefaultFrameXMLDebugLevel;

}

void SetFrameXMLDebugLevel(int level) {
  g_frame_xml_debug_level = level;
}

int GetFrameXMLDebugLevel() {
  return g_frame_xml_debug_level;
}

}

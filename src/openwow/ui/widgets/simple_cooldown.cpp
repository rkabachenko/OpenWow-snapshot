
#include "openwow/ui/widgets/simple_cooldown.h"

#include "openwow/ui/widgets/widget_xml_helpers.h"
#include "openwow/ui/xml/frame_xml_parser.h"

namespace openwow::ui::widgets {

void CSimpleCooldown::LoadXML(const void *xmlNode, void *errorHandler) {
  const auto *frame_def =
      static_cast<const openwow::ui::xml::XMLFrameDef *>(xmlNode);
  if (frame_def == nullptr) {
    return;
  }

  CSimpleFrame::LoadXML(xmlNode, errorHandler);

  if (const char *reverse_attr = FindAttributeValue(*frame_def, "reverse");
      reverse_attr != nullptr && *reverse_attr != '\0') {
    SetReverse(ScriptParseBoolStringOrDefault(reverse_attr, false));
  }

  if (const char *draw_edge_attr = FindAttributeValue(*frame_def, "drawEdge");
      draw_edge_attr != nullptr && *draw_edge_attr != '\0') {
    SetDrawEdge(ScriptParseBoolStringOrDefault(draw_edge_attr, false));
  }
}

}

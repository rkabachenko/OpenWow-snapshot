#include "openwow/ui/xml/frame_xml_document_order.h"

#include "openwow/ui/xml/frame_xml_parser.h"
#include "openwow/foundation/text/ascii.h"

namespace openwow::ui::xml {

using openwow::text::ToLowerAscii;
using openwow::text::Trim;

namespace {

bool IsTextureRegionTag(const std::string& lower) {
  return lower.size() >= 7 && lower.rfind("texture") == (lower.size() - 7);
}

bool IsRuntimeWidgetTag(const std::string& tag) {
  const std::string lower = ToLowerAscii(tag);
  if (lower == "frame" || lower == "button" || lower == "checkbutton" ||
      lower == "editbox" || lower == "messageframe" || lower == "scrollframe" ||
      lower == "scrollingmessageframe" || lower == "slider" || lower == "simplehtml" ||
      lower == "statusbar" || lower == "colorselect" || lower == "model" ||
      lower == "modelffx" || lower == "playermodel" || lower == "dressupmodel" ||
      lower == "movieframe" || lower == "texture" || lower == "fontstring" ||
      lower == "buttontext" || lower == "questpoiframe" || lower == "cooldown" ||
      lower == "minimap" || lower == "gametooltip" || lower == "worldframe" ||
      lower == "tabardmodel") {
    return true;
  }
  return IsTextureRegionTag(lower);
}

}

FrameXmlDocumentOrderResult ExtractFrameXmlTopLevelElements(
    const std::string& xml_text) {
  XMLNode root;
  std::string error;
  if (!FrameXMLParser::ParseDocument(xml_text, &root, &error)) {
    FrameXmlDocumentOrderResult result;
    result.error = error.empty() ? "Failed to parse XML document" : error;
    return result;
  }

  return ExtractFrameXmlTopLevelElements(root);
}

FrameXmlDocumentOrderResult ExtractFrameXmlTopLevelElements(
    const XMLNode& root) {
  FrameXmlDocumentOrderResult result;

  for (const auto& child : root.children) {
    const std::string tag = ToLowerAscii(child.tag);
    if (tag == "include") {
      const std::string file = child.GetAttr("file");
      if (!file.empty()) {
        result.elements.push_back({
            .kind = FrameXmlTopLevelElementKind::kInclude,
            .value = file,
        });
      }
      continue;
    }

    if (tag == "script") {
      const std::string file = child.GetAttr("file");
      if (!file.empty()) {
        result.elements.push_back({
            .kind = FrameXmlTopLevelElementKind::kScriptFile,
            .value = file,
        });
      }

      std::string body = Trim(child.text);
      if (!body.empty()) {
        result.elements.push_back({
            .kind = FrameXmlTopLevelElementKind::kScriptInline,
            .value = std::move(body),
        });
      }
      continue;
    }

    if (tag == "font") {
      result.elements.push_back({
          .kind = FrameXmlTopLevelElementKind::kFont,
          .value = child.GetAttr("name"),
      });
      continue;
    }

    if (IsRuntimeWidgetTag(child.tag)) {
      result.elements.push_back({
          .kind = FrameXmlTopLevelElementKind::kFrame,
          .value = child.tag,
      });
    }
  }

  result.ok = true;
  return result;
}

}

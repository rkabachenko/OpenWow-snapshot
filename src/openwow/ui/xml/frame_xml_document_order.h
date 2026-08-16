#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui::xml {

struct XMLNode;

enum class FrameXmlTopLevelElementKind : std::uint8_t {
  kInclude,
  kScriptFile,
  kScriptInline,
  kFont,
  kFrame,
};

struct FrameXmlTopLevelElement {
  FrameXmlTopLevelElementKind kind{FrameXmlTopLevelElementKind::kFrame};
  std::string value;
};

struct FrameXmlDocumentOrderResult {
  bool ok{false};
  std::string error;
  std::vector<FrameXmlTopLevelElement> elements;
};

[[nodiscard]] FrameXmlDocumentOrderResult ExtractFrameXmlTopLevelElements(
    const std::string& xml_text);

[[nodiscard]] FrameXmlDocumentOrderResult ExtractFrameXmlTopLevelElements(
    const XMLNode& root);

}

#include "openwow/ui/xml/owned_xml_document_parser.h"

#include "openwow/ui/xml/frame_xml_parser.h"

#include <expat.h>

#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::ui::xml {
namespace {

struct PendingXmlNode {
  std::string tag;
  std::unordered_map<std::string, std::string> attributes;
  std::vector<std::unique_ptr<PendingXmlNode>> children;
  std::string text;
  bool text_started{false};
};

struct ParserDeleter {
  void operator()(XML_ParserStruct* parser) const {
    if (parser != nullptr) {
      XML_ParserFree(parser);
    }
  }
};

using ParserOwner =
    std::unique_ptr<XML_ParserStruct, ParserDeleter>;

struct ParseContext {
  static constexpr std::size_t kMaxDepth = 512;
  static constexpr std::size_t kMaxNodes = 1'000'000;

  XML_Parser parser{nullptr};
  std::unique_ptr<PendingXmlNode> root;
  std::vector<PendingXmlNode*> stack;
  std::size_t node_count{0};
  bool callback_failed{false};
};

bool IsXmlWhitespace(const char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

void StopAfterCallbackFailure(ParseContext* context) noexcept {
  context->callback_failed = true;
  if (context->parser != nullptr) {
    XML_StopParser(context->parser, XML_FALSE);
  }
}

void XMLCALL StartElement(void* user_data, const XML_Char* tag_name,
                          const XML_Char** attributes) noexcept {
  auto* const context = static_cast<ParseContext*>(user_data);
  try {
    if (context->stack.size() >= ParseContext::kMaxDepth ||
        context->node_count >= ParseContext::kMaxNodes) {
      StopAfterCallbackFailure(context);
      return;
    }
    auto node = std::make_unique<PendingXmlNode>();
    node->tag = tag_name != nullptr ? tag_name : "";
    if (attributes != nullptr) {
      for (const XML_Char** cursor = attributes; *cursor != nullptr;
           cursor += 2) {
        const char* const value = cursor[1] != nullptr ? cursor[1] : "";
        node->attributes.emplace(cursor[0], value);
      }
    }

    PendingXmlNode* const current = node.get();
    if (context->stack.empty()) {
      context->root = std::move(node);
    } else {
      context->stack.back()->children.push_back(std::move(node));
    }
    context->stack.push_back(current);
    ++context->node_count;
  } catch (...) {
    StopAfterCallbackFailure(context);
  }
}

void XMLCALL EndElement(void* user_data,
                        const XML_Char* ) noexcept {
  auto* const context = static_cast<ParseContext*>(user_data);
  if (!context->stack.empty()) {
    context->stack.pop_back();
  }
}

void XMLCALL CharacterData(void* user_data, const XML_Char* text,
                           const int size) noexcept {
  auto* const context = static_cast<ParseContext*>(user_data);
  if (size <= 0 || context->stack.empty()) {
    return;
  }

  try {
    PendingXmlNode* const node = context->stack.back();
    if (!node->text_started) {
      bool all_whitespace = true;
      for (int index = 0; index < size; ++index) {
        if (!IsXmlWhitespace(text[index])) {
          all_whitespace = false;
          break;
        }
      }
      if (all_whitespace) {
        return;
      }
      node->text_started = true;
    }
    node->text.append(text, static_cast<std::size_t>(size));
  } catch (...) {
    StopAfterCallbackFailure(context);
  }
}

XMLNode Materialize(PendingXmlNode&& source) {
  XMLNode result;
  result.tag = std::move(source.tag);
  result.attributes = std::move(source.attributes);
  result.text = std::move(source.text);
  result.children.reserve(source.children.size());
  for (auto& child : source.children) {
    result.children.push_back(Materialize(std::move(*child)));
  }
  return result;
}

void SetError(std::string* error, const char* message) {
  if (error != nullptr) {
    *error = message;
  }
}

}

bool ParseOwnedXmlDocument(const std::string_view xml_text,
                           XMLNode* const out_root,
                           std::string* const error) {
  if (out_root == nullptr) {
    SetError(error, "output root is null");
    return false;
  }
  if (xml_text.size() >
      static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    SetError(error, "Failed to parse XML document");
    return false;
  }

  ParserOwner parser(XML_ParserCreate(nullptr));
  if (parser == nullptr) {
    SetError(error, "Failed to parse XML document");
    return false;
  }

  ParseContext context;
  context.parser = parser.get();
  XML_SetUserData(parser.get(), &context);
  XML_SetElementHandler(parser.get(), StartElement, EndElement);
  XML_SetCharacterDataHandler(parser.get(), CharacterData);

  static constexpr char kEmptyBuffer[] = "";
  const char* const bytes =
      xml_text.empty() ? kEmptyBuffer : xml_text.data();
  if (XML_Parse(parser.get(), bytes, static_cast<int>(xml_text.size()),
                XML_TRUE) != XML_STATUS_OK ||
      context.callback_failed) {
    SetError(error, "Failed to parse XML document");
    return false;
  }
  if (context.root == nullptr) {
    SetError(error, "No XML content found");
    return false;
  }

  *out_root = Materialize(std::move(*context.root));
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

}

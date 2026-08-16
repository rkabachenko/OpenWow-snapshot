
#include "openwow/ui/xml/xml_tree.h"

#include "openwow/core/storm_string.h"

using openwow::core::SStrCmpNoCase;

#include <expat.h>

#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

namespace openwow::ui::xml {

static CXMLNode *g_free_list_head = nullptr;

namespace {

struct SharedXmlParser {
  ~SharedXmlParser() {
    if (parser != nullptr) {
      XML_ParserFree(parser);
    }
  }

  [[nodiscard]] XML_Parser Prepare() {
    if (parser != nullptr) {
      if (XML_ParserReset(parser, nullptr) == XML_FALSE) {
        return nullptr;
      }
    } else {
      parser = XML_ParserCreate(nullptr);
    }

    return parser;
  }

  std::mutex mutex;
  XML_Parser parser = nullptr;
};

bool IsXmlWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

SharedXmlParser &GetSharedXmlParser() {
  static SharedXmlParser shared_xml_parser;
  return shared_xml_parser;
}

CXMLNode *AllocNode() {
  if (g_free_list_head) {
    CXMLNode *node = g_free_list_head;
    g_free_list_head = node->free_next;
    node->free_next = nullptr;
    return node;
  }
  return new CXMLNode();
}

void XMLTree_EndElement(void *parse_ctx, const char * ) {
  auto *tree = static_cast<CXMLTree *>(parse_ctx);
  if (tree->current_node) {
    tree->current_node = tree->current_node->parent;
  }
}

void XMLCALL XMLTree_StartElementHandler(void *parse_ctx,
                                         const XML_Char *tag_name,
                                         const XML_Char **attributes) {
  std::vector<const char *> attribute_view;
  if (attributes != nullptr) {
    for (const XML_Char **cursor = attributes; *cursor != nullptr; ++cursor) {
      attribute_view.push_back(*cursor);
    }
    attribute_view.push_back(nullptr);
  }

  XMLNode_Create(parse_ctx,
                 tag_name,
                 attribute_view.empty() ? nullptr : attribute_view.data());
}

void XMLCALL XMLTree_EndElementHandler(void *parse_ctx, const XML_Char *tag_name) {
  XMLTree_EndElement(parse_ctx, tag_name);
}

void XMLCALL XMLTree_CharacterDataHandler(void *parse_ctx, const XML_Char *text, int size) {
  XMLTree_AppendText(parse_ctx, text, size);
}

const CXMLNode *FindChildByNameNoCaseImpl(const CXMLNode *node, const char *child_name) {
  if (node == nullptr) {
    return nullptr;
  }

  for (auto *child = node->first_child; child != nullptr; child = child->right_sibling) {
    if (openwow::core::SStrCmpNoCase(child->tag.c_str(), child_name, 0x7FFFFFFFu) == 0) {
      return child;
    }
  }

  return nullptr;
}

}

void CXMLAttributeArray::Resize(unsigned int new_count) {
  entries.resize(new_count);
}

void XMLTree_AppendText(void *parse_ctx, const void *text, int size) {
  if (size <= 0)
    return;

  auto *tree = static_cast<CXMLTree *>(parse_ctx);
  CXMLNode *node = tree->current_node;
  if (!node)
    return;

  const char *src = static_cast<const char *>(text);

  if (node->text) {
    const uint32_t old_len = node->text_size;
    const uint32_t new_len = old_len + static_cast<uint32_t>(size);
    char *buffer = static_cast<char *>(std::realloc(node->text, new_len + 1));
    std::memcpy(buffer + old_len, src, static_cast<size_t>(size));
    buffer[new_len] = '\0';
    node->text = buffer;
    node->text_size = new_len;
    return;
  }

  bool all_whitespace = true;
  for (int i = 0; i < size; ++i) {
    if (!IsXmlWhitespace(src[i])) {
      all_whitespace = false;
      break;
    }
  }
  if (all_whitespace)
    return;

  const uint32_t new_len = static_cast<uint32_t>(size);
  char *buffer = static_cast<char *>(std::malloc(new_len + 1));
  std::memcpy(buffer, src, new_len);
  buffer[new_len] = '\0';
  node->text = buffer;
  node->text_size = new_len;
}

const CXMLNode *XMLNode_FindChildByNameNoCase(const CXMLNode *node, const char *child_name) {
  return FindChildByNameNoCaseImpl(node, child_name);
}

CXMLNode *XMLNode_FindChildByNameNoCase(CXMLNode *node, const char *child_name) {
  return const_cast<CXMLNode *>(FindChildByNameNoCaseImpl(node, child_name));
}

void XMLNode_Unlink(CXMLNode *node) {
  if (!node || !node->parent)
    return;

  CXMLNode *parent = node->parent;

  if (parent->first_child == node) {
    parent->first_child = node->right_sibling;
  } else {
    CXMLNode *sibling = parent->first_child;
    while (sibling && sibling->right_sibling != node)
      sibling = sibling->right_sibling;
    if (sibling)
      sibling->right_sibling = node->right_sibling;
  }

  node->right_sibling = nullptr;
  node->parent = nullptr;
}

const char *XMLNode_GetAttributeValue(const CXMLNode *node, const char *attr_name) {
  if (node == nullptr || attr_name == nullptr) {
    return nullptr;
  }
  for (const auto &attr : node->attributes.entries) {
    if (SStrCmpNoCase(attr.name.c_str(), attr_name,
                      std::numeric_limits<size_t>::max()) == 0) {
      return attr.value.c_str();
    }
  }
  return nullptr;
}

void XMLNode_Destroy(CXMLNode *node) {
  if (!node)
    return;

  node->parent = nullptr;

  if (node->right_sibling) {
    XMLNode_Destroy(node->right_sibling);
    node->right_sibling = nullptr;
  }

  if (node->first_child) {
    XMLNode_Destroy(node->first_child);
    node->first_child = nullptr;
  }

  if (node->text) {
    std::free(node->text);
    node->text = nullptr;
    node->text_size = 0;
  }

  node->attributes.entries.clear();
  node->attributes.entries.shrink_to_fit();
  node->tag.clear();

  delete node;
}

void XMLNode_Create(void *parse_state, const char *tag_name, const char *const *attributes) {
  auto *tree = static_cast<CXMLTree *>(parse_state);
  CXMLNode *node = AllocNode();

  node->tag = tag_name ? tag_name : "";
  node->line_number = 0;

  if (tree->current_node) {
    node->parent = tree->current_node;

    if (!tree->current_node->first_child) {
      tree->current_node->first_child = node;
    } else {
      CXMLNode *sibling = tree->current_node->first_child;
      while (sibling->right_sibling)
        sibling = sibling->right_sibling;
      sibling->right_sibling = node;
    }
  } else {
    tree->root = node;
  }

  tree->current_node = node;

  unsigned int attribute_count = 0;
  if (attributes) {
    for (const char *const *cursor = attributes; *cursor != nullptr; cursor += 2)
      ++attribute_count;
  }

  if (attribute_count == 0)
    return;

  node->attributes.Resize(attribute_count);
  for (unsigned int index = 0; index < attribute_count; ++index) {
    node->attributes.entries[index].name = attributes[index * 2];
    node->attributes.entries[index].value =
        attributes[index * 2 + 1] ? attributes[index * 2 + 1] : "";
  }
}

void XMLNode_RecycleToFreeList(CXMLNode *node) {
  if (!node)
    return;

  node->parent = nullptr;

  if (node->right_sibling) {
    XMLNode_RecycleToFreeList(node->right_sibling);
    node->right_sibling = nullptr;
  }

  if (node->first_child) {
    XMLNode_RecycleToFreeList(node->first_child);
    node->first_child = nullptr;
  }

  if (node->text) {
    std::free(node->text);
    node->text = nullptr;
    node->text_size = 0;
  }

  node->attributes.entries.clear();
  node->tag.clear();

  node->free_next = g_free_list_head;
  g_free_list_head = node;
}

void XMLTree_Free(CXMLTree *tree) {
  if (!tree)
    return;

  if (tree->root) {
    XMLNode_RecycleToFreeList(tree->root);
    tree->root = nullptr;
  }

  delete tree;
}

CXMLTree *XMLTree_Parse(const void *xml_data, size_t xml_size) {
  if (xml_data == nullptr && xml_size != 0)
    return nullptr;

  if (xml_size > static_cast<size_t>(std::numeric_limits<int>::max()))
    return nullptr;

  auto *tree = new CXMLTree();

  if (xml_size == 0) {
    return tree;
  }

  static constexpr char kEmptyXmlBuffer[] = "";
  const char *xml_bytes =
      xml_data != nullptr ? static_cast<const char *>(xml_data) : kEmptyXmlBuffer;

  SharedXmlParser &shared_xml_parser = GetSharedXmlParser();
  std::lock_guard<std::mutex> lock(shared_xml_parser.mutex);

  XML_Parser parser = shared_xml_parser.Prepare();
  if (parser == nullptr) {
    delete tree;
    return nullptr;
  }

  XML_SetElementHandler(parser, XMLTree_StartElementHandler, XMLTree_EndElementHandler);
  XML_SetCharacterDataHandler(parser, XMLTree_CharacterDataHandler);
  XML_SetUserData(parser, tree);

  if (XML_Parse(parser, xml_bytes, static_cast<int>(xml_size), XML_TRUE) == XML_STATUS_OK)
    return tree;

  XMLTree_Free(tree);
  return nullptr;
}

CXMLNode *XMLTree_GetFreeListHead() {
  return g_free_list_head;
}

void XMLTree_PurgeFreeList() {
  while (g_free_list_head) {
    CXMLNode *next = g_free_list_head->free_next;
    delete g_free_list_head;
    g_free_list_head = next;
  }
}

}

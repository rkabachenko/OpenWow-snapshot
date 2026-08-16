#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui::xml {

struct CXMLAttribute {
  std::string name;
  std::string value;
};

struct CXMLAttributeArray {
  std::vector<CXMLAttribute> entries;

  void Resize(unsigned int new_count);
};

struct CXMLNode {
  CXMLNode *parent = nullptr;
  CXMLNode *first_child = nullptr;
  std::string tag;
  char *text = nullptr;
  uint32_t text_size = 0;
  CXMLAttributeArray attributes;
  int line_number = 0;
  CXMLNode *right_sibling = nullptr;

  CXMLNode *free_next = nullptr;
};

struct CXMLTree {
  CXMLNode *root = nullptr;
  CXMLNode *current_node = nullptr;
};

[[nodiscard]] const CXMLNode *XMLNode_FindChildByNameNoCase(const CXMLNode *node,
                                                            const char *child_name);
[[nodiscard]] CXMLNode *XMLNode_FindChildByNameNoCase(CXMLNode *node, const char *child_name);

[[nodiscard]] const char *XMLNode_GetAttributeValue(const CXMLNode *node,
                                                    const char *attr_name);

void XMLNode_Unlink(CXMLNode *node);

void XMLTree_AppendText(void *parse_ctx, const void *text, int size);

void XMLNode_Destroy(CXMLNode *node);

void XMLNode_Create(void *parse_state, const char *tag_name, const char *const *attributes);

void XMLNode_RecycleToFreeList(CXMLNode *node);

void XMLTree_Free(CXMLTree *tree);

CXMLTree *XMLTree_Parse(const void *xml_data, size_t xml_size);

CXMLNode *XMLTree_GetFreeListHead();

void XMLTree_PurgeFreeList();

}

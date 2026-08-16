#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui::xml {

struct XMLNode {
  std::string tag;
  std::unordered_map<std::string, std::string> attributes;
  std::vector<XMLNode> children;
  std::string text;

  [[nodiscard]] std::string GetAttr(const std::string& name,
                                    const std::string& def = "") const;
  [[nodiscard]] bool HasAttr(const std::string& name) const;
  [[nodiscard]] int GetAttrInt(const std::string& name, int def = 0) const;
  [[nodiscard]] float GetAttrFloat(const std::string& name,
                                   float def = 0.0f) const;
  [[nodiscard]] bool GetAttrBool(const std::string& name,
                                 bool def = false) const;

  [[nodiscard]] const XMLNode* FindChild(const std::string& tag) const;
  [[nodiscard]] std::vector<const XMLNode*> FindChildren(
      const std::string& tag) const;
};

struct XMLFrameDef {
  struct FontColorDef {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
  };

  struct FontShadowDef {
    float x = 0.001f;
    float y = -0.001f;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
  };

  std::string type;
  std::string name;
  std::string parent;
  std::string inherits;
  bool is_virtual = false;
  bool hidden = false;
  bool toplevel = false;

  struct {
    float x = 0;
    float y = 0;
    bool set = false;
  } size;

  struct Anchor {
    std::string point;
    std::string relative_to;
    std::string relative_point;
    float x_offset = 0;
    float y_offset = 0;
  };
  std::vector<Anchor> anchors;

  struct Layer {
    std::string level;
    std::vector<XMLFrameDef> items;
  };
  std::vector<Layer> layers;

  std::vector<XMLFrameDef> children;

  struct Script {
    std::string handler;
    std::string body;
    std::string function;
  };
  std::vector<Script> scripts;

  struct BackdropDef {
    std::string bg_file;
    std::string edge_file;
    bool tile = false;
    int tile_size = 0;
    int edge_size = 0;
    struct { float left = 0, right = 0, top = 0, bottom = 0; } insets;
    bool set = false;
  };
  BackdropDef backdrop;

  struct KeyValue {
    std::string key;
    std::string value;
    std::string type;
  };
  std::vector<KeyValue> kv_attributes;

  std::unordered_map<std::string, std::string> attributes;

  std::string font;
  std::string text;
  std::string file;
  float alpha = 1.0f;
  uint32_t id = 0;
  std::optional<float> font_height;
  bool has_text_color = false;
  FontColorDef text_color;
  bool has_shadow = false;
  FontShadowDef shadow;

  XMLNode raw_node;
};

class FrameXMLParser {
 public:
  struct ParseResult {
    std::vector<XMLFrameDef> frames;
    std::vector<XMLFrameDef> templates;
    std::vector<std::string> scripts;
    std::vector<std::string> includes;
    bool success = false;
    std::string error;
  };

  static ParseResult Parse(const std::string& xml_text,
                           const std::string& sourcePath = "");

  static bool ParseDocument(const std::string& xml_text, XMLNode* out_root,
                            std::string* error = nullptr);

  static const XMLFrameDef* GetTemplate(const std::string& name);
  static void ClearTemplates();

 private:

  static XMLFrameDef ParseFrameNode(const XMLNode& node);

  static void ParseAnchors(const XMLNode& node, XMLFrameDef& def);

  static void ParseLayers(const XMLNode& node, XMLFrameDef& def);

  static void ParseScripts(const XMLNode& node, XMLFrameDef& def);

  static void ParseSize(const XMLNode& node, XMLFrameDef& def);

  static void ParseBackdrop(const XMLNode& node, XMLFrameDef& def);

  static void ParseAttributes(const XMLNode& node, XMLFrameDef& def);

};

}

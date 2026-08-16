#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui {

namespace xml {
struct XMLNode;
}

struct FontColor {
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};

    bool operator==(const FontColor&) const = default;
};

struct FontShadowOffset {
    float x{0.0f};
    float y{0.0f};

    bool operator==(const FontShadowOffset&) const = default;
};

struct FontShadow {
    FontShadowOffset offset;
    FontColor color{0.0f, 0.0f, 0.0f, 1.0f};

    bool operator==(const FontShadow&) const = default;
};

struct FontDefinition {
    std::string name;
    std::string font_file;
    float height{12.0f};
    bool has_height{false};
    FontColor color;
    bool has_color{false};
    std::optional<FontShadow> shadow;
    std::string inherits;
    bool is_virtual{false};
    std::string outline;
    bool has_outline{false};
    bool monochrome{false};
    bool has_monochrome{false};
    float spacing{0.0f};
    bool has_spacing{false};
    std::string justify_h;
    bool has_justify_h{false};
    std::string justify_v;
    bool has_justify_v{false};
    bool non_space_wrap{false};
    bool has_non_space_wrap{false};
    bool indented_word_wrap{false};
    bool has_indented_word_wrap{false};

    bool operator==(const FontDefinition&) const = default;
};

struct IntrinsicTypeDef {
    std::string type_name;
    std::string inherits;
    bool is_virtual{false};
};

class FontDefinitionRegistry {
 public:
    FontDefinitionRegistry() = default;

    void Register(FontDefinition def);

    std::string RegisterFromXml(const std::string& xml_text);

    bool Unregister(const std::string& name);

    [[nodiscard]] std::optional<FontDefinition> Get(
        const std::string& name) const;

    [[nodiscard]] const FontDefinition* GetRaw(const std::string& name) const;

    [[nodiscard]] bool Has(const std::string& name) const;

    [[nodiscard]] size_t Count() const { return defs_.size(); }

    [[nodiscard]] std::vector<std::string> GetNames() const;

    [[nodiscard]] FontDefinition Resolve(const FontDefinition& def) const;

    void Clear();

 private:
    std::unordered_map<std::string, FontDefinition> defs_;
};

class IntrinsicTypeRegistry {
 public:
    IntrinsicTypeRegistry() = default;

    void Register(IntrinsicTypeDef def);

    [[nodiscard]] bool IsIntrinsic(const std::string& type_name) const;

    [[nodiscard]] const IntrinsicTypeDef* Get(
        const std::string& type_name) const;

    [[nodiscard]] size_t Count() const { return defs_.size(); }

    [[nodiscard]] std::vector<std::string> GetNames() const;

    void Clear();

 private:
    std::unordered_map<std::string, IntrinsicTypeDef> defs_;
};

[[nodiscard]] std::optional<FontDefinition> ParseFontXml(
    const std::string& xml_text);

struct FontXmlParseResult {
    std::vector<FontDefinition> fonts;
    std::vector<IntrinsicTypeDef> intrinsics;

    std::vector<std::string> skipped_intrinsic_frames;
    bool ok{false};
    std::string error;
};

[[nodiscard]] FontXmlParseResult ParseFontAndIntrinsicXml(
    const std::string& xml_text);

[[nodiscard]] FontXmlParseResult ParseFontAndIntrinsicXml(
    const xml::XMLNode& root);

}

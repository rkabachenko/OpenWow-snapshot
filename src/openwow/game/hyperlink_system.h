
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class HyperlinkType : uint8_t {
    Item,
    Spell,
    Quest,
    Achievement,
    Talent,
    Glyph,
    Enchant,
    Trade,
    Player,
    Channel,
};

struct HyperlinkEntry {
    HyperlinkType type = HyperlinkType::Item;
    uint32_t      id   = 0;
    std::string   displayText;
    std::string   rawLink;
    std::string   colorCode;
};

class HyperlinkSystem {
public:

    [[nodiscard]] static std::optional<HyperlinkEntry> ParseHyperlink(
        const std::string& link);

    [[nodiscard]] static std::optional<HyperlinkType> GetType(
        const std::string& link);

    [[nodiscard]] static std::string FormatHyperlink(
        HyperlinkType type, uint32_t id, const std::string& displayText);

    [[nodiscard]] static bool IsHyperlink(const std::string& text);

    [[nodiscard]] static std::vector<HyperlinkEntry> ExtractAll(
        const std::string& text);

    [[nodiscard]] static std::string GetTypeColorCode(HyperlinkType type);

    [[nodiscard]] static std::string GetTypeName(HyperlinkType type);

    [[nodiscard]] static std::string StripLinks(const std::string& text);

    [[nodiscard]] static size_t GetLinkCount(const std::string& text);
};

}

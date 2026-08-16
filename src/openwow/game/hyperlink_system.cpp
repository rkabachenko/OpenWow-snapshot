
#include "openwow/game/hyperlink_system.h"

#include <algorithm>
#include <charconv>
#include <sstream>

namespace openwow::game {

namespace {

struct TypeEntry {
    HyperlinkType type;
    const char*   tag;
    const char*   colorCode;
};

constexpr TypeEntry kTypeTable[] = {
    {HyperlinkType::Item,        "item",        "ff1eff00"},
    {HyperlinkType::Spell,       "spell",       "ff71d5ff"},
    {HyperlinkType::Quest,       "quest",       "ffffff00"},
    {HyperlinkType::Achievement, "achievement", "ffffff00"},
    {HyperlinkType::Talent,      "talent",      "ff4e96f7"},
    {HyperlinkType::Glyph,       "glyph",       "ff66bbff"},
    {HyperlinkType::Enchant,     "enchant",     "ffffd000"},
    {HyperlinkType::Trade,       "trade",       "ffffd000"},
    {HyperlinkType::Player,      "player",      "ffffffff"},
    {HyperlinkType::Channel,     "channel",     "ffffffff"},
};
constexpr size_t kTypeCount = sizeof(kTypeTable) / sizeof(kTypeTable[0]);

const TypeEntry* FindByTag(const std::string& tag) {
    for (size_t i = 0; i < kTypeCount; ++i) {
        if (tag == kTypeTable[i].tag) return &kTypeTable[i];
    }
    return nullptr;
}

const TypeEntry* FindByType(HyperlinkType type) {
    for (size_t i = 0; i < kTypeCount; ++i) {
        if (kTypeTable[i].type == type) return &kTypeTable[i];
    }
    return nullptr;
}

uint32_t ParseU32(const char* b, const char* e) {
    uint32_t v = 0;
    std::from_chars(b, e, v);
    return v;
}

}

std::optional<HyperlinkEntry> HyperlinkSystem::ParseHyperlink(
    const std::string& link) {

    auto cPos = link.find("|c");
    auto hPos = link.find("|H");
    if (cPos == std::string::npos || hPos == std::string::npos) return std::nullopt;

    if (cPos + 10 > link.size()) return std::nullopt;
    std::string colorCode = link.substr(cPos + 2, 8);

    size_t typeStart = hPos + 2;
    auto colonPos = link.find(':', typeStart);
    if (colonPos == std::string::npos) return std::nullopt;
    std::string tag = link.substr(typeStart, colonPos - typeStart);

    const TypeEntry* te = FindByTag(tag);
    if (!te) return std::nullopt;

    size_t idStart = colonPos + 1;
    size_t idEnd = link.find_first_of(":|", idStart);
    if (idEnd == std::string::npos) idEnd = link.size();
    uint32_t id = ParseU32(link.data() + idStart, link.data() + idEnd);

    std::string displayText;
    auto openBracket = link.find('[', hPos);
    auto closeBracket = link.find(']', openBracket != std::string::npos ? openBracket : 0);
    if (openBracket != std::string::npos && closeBracket != std::string::npos &&
        closeBracket > openBracket) {
        displayText = link.substr(openBracket + 1, closeBracket - openBracket - 1);
    }

    HyperlinkEntry entry;
    entry.type = te->type;
    entry.id = id;
    entry.displayText = std::move(displayText);
    entry.rawLink = link;
    entry.colorCode = std::move(colorCode);
    return entry;
}

std::optional<HyperlinkType> HyperlinkSystem::GetType(const std::string& link) {
    auto hPos = link.find("|H");
    if (hPos == std::string::npos) return std::nullopt;
    size_t start = hPos + 2;
    auto colonPos = link.find(':', start);
    if (colonPos == std::string::npos) return std::nullopt;
    std::string tag = link.substr(start, colonPos - start);
    const TypeEntry* te = FindByTag(tag);
    return te ? std::optional<HyperlinkType>{te->type} : std::nullopt;
}

std::string HyperlinkSystem::FormatHyperlink(HyperlinkType type, uint32_t id,
                                             const std::string& displayText) {
    const TypeEntry* te = FindByType(type);
    if (!te) return displayText;
    std::ostringstream os;
    os << "|c" << te->colorCode
       << "|H" << te->tag << ':' << id
       << "|h[" << displayText << "]|h|r";
    return os.str();
}

bool HyperlinkSystem::IsHyperlink(const std::string& text) {
    return text.find("|H") != std::string::npos;
}

std::vector<HyperlinkEntry> HyperlinkSystem::ExtractAll(const std::string& text) {
    std::vector<HyperlinkEntry> results;
    size_t offset = 0;
    while (true) {

        auto cPos = text.find("|c", offset);
        if (cPos == std::string::npos) break;

        auto rPos = text.find("|r", cPos);
        if (rPos == std::string::npos) break;
        rPos += 2;

        std::string segment = text.substr(cPos, rPos - cPos);
        auto entry = ParseHyperlink(segment);
        if (entry) results.push_back(std::move(*entry));

        offset = rPos;
    }
    return results;
}

std::string HyperlinkSystem::GetTypeColorCode(HyperlinkType type) {
    const TypeEntry* te = FindByType(type);
    return te ? std::string(te->colorCode) : "ffffffff";
}

std::string HyperlinkSystem::GetTypeName(HyperlinkType type) {
    const TypeEntry* te = FindByType(type);
    return te ? std::string(te->tag) : "unknown";
}

std::string HyperlinkSystem::StripLinks(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {

        if (i + 1 < text.size() && text[i] == '|' &&
            (text[i + 1] == 'c' || text[i + 1] == 'C')) {
            i += 10;
            continue;
        }

        if (i + 1 < text.size() && text[i] == '|' &&
            (text[i + 1] == 'H' || text[i + 1] == 'h')) {

            i += 2;
            if (text[i - 1] == 'H') {
                while (i < text.size() && text[i] != '|') ++i;
            }
            continue;
        }

        if (i + 1 < text.size() && text[i] == '|' &&
            (text[i + 1] == 'r' || text[i + 1] == 'R')) {
            i += 2;
            continue;
        }

        if (text[i] == '[' || text[i] == ']') {
            ++i;
            continue;
        }
        result += text[i];
        ++i;
    }
    return result;
}

size_t HyperlinkSystem::GetLinkCount(const std::string& text) {
    size_t count = 0;
    size_t offset = 0;
    while (true) {
        auto pos = text.find("|H", offset);
        if (pos == std::string::npos) break;
        ++count;
        offset = pos + 2;
    }
    return count;
}

}

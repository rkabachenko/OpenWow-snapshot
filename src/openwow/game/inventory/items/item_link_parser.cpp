
#include "openwow/game/inventory/items/item_link_parser.h"

#include <cctype>
#include <charconv>
#include <sstream>

namespace openwow::game {

namespace {

uint32_t ParseU32(const char* begin, const char* end) {
    uint32_t val = 0;
    auto [ptr, ec] = std::from_chars(begin, end, val);
    return (ec == std::errc{}) ? val : 0;
}

std::int32_t ParseI32(const char* begin, const char* end) {
    std::int32_t val = 0;
    auto [ptr, ec] = std::from_chars(begin, end, val);
    return (ec == std::errc{} && ptr != begin) ? val : 0;
}

struct ItemPayloadPosition {
    size_t start = std::string::npos;
    bool is_full_link = false;
};

ItemPayloadPosition FindItemPayload(const std::string& s, size_t offset = 0) {
    static constexpr const char kFullTag[] = "|Hitem:";
    static constexpr const char kRawTag[] = "item:";

    if (const auto full_pos = s.find(kFullTag, offset); full_pos != std::string::npos) {
        return {full_pos + sizeof(kFullTag) - 1, true};
    }

    if (const auto raw_pos = s.find(kRawTag, offset); raw_pos != std::string::npos) {
        return {raw_pos + sizeof(kRawTag) - 1, false};
    }

    return {};
}

std::string ExtractBracketText(const std::string& s, size_t searchFrom) {
    auto open = s.find('[', searchFrom);
    auto close = s.find(']', open != std::string::npos ? open : 0);
    if (open == std::string::npos || close == std::string::npos || close <= open)
        return {};
    return s.substr(open + 1, close - open - 1);
}

uint8_t QualityFromColorCode(const std::string& s, size_t linkStart) {

    if (linkStart < 10) return 1;
    auto cPos = s.rfind("|c", linkStart);
    if (cPos == std::string::npos || cPos + 10 > s.size()) return 1;
    std::string code = s.substr(cPos + 2, 8);

    for (auto& ch : code) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (const auto& [q, hex] : kQualityColors) {
        if (hex == code) return q;
    }
    return 1;
}

}

std::optional<ItemLinkData> ItemLinkParser::Parse(const std::string& link) {
    ItemLinkData data;
    data.Reset();

    const auto payload = FindItemPayload(link);
    if (payload.start == std::string::npos) return std::nullopt;

    auto payloadEnd = payload.is_full_link ? link.find("|h", payload.start)
                                           : std::string::npos;
    if (payloadEnd == std::string::npos) payloadEnd = link.size();

    std::string raw_payload = link.substr(payload.start, payloadEnd - payload.start);

    std::vector<std::string> fields;
    std::istringstream ss(raw_payload);
    std::string token;
    while (std::getline(ss, token, ':')) {
        fields.push_back(token);
    }

    if (!fields.empty()) {
        const char* begin = fields[0].data();
        data.itemId = ParseU32(begin, begin + fields[0].size());
    }
    if (fields.size() > 1) {
        const char* begin = fields[1].data();
        data.enchantId = ParseU32(begin, begin + fields[1].size());
    }
    if (fields.size() > 2) {
        const char* begin = fields[2].data();
        data.gemIds[0] = ParseU32(begin, begin + fields[2].size());
    }
    if (fields.size() > 3) {
        const char* begin = fields[3].data();
        data.gemIds[1] = ParseU32(begin, begin + fields[3].size());
    }
    if (fields.size() > 4) {
        const char* begin = fields[4].data();
        data.gemIds[2] = ParseU32(begin, begin + fields[4].size());
    }
    if (fields.size() > 5) {
        const char* begin = fields[5].data();
        data.extraEnchantId = ParseI32(begin, begin + fields[5].size());
    }
    if (fields.size() > 6) {
        const char* begin = fields[6].data();
        data.randomPropertyId = ParseI32(begin, begin + fields[6].size());
    }
    if (fields.size() > 7) {
        const char* begin = fields[7].data();
        data.suffixFactor = ParseI32(begin, begin + fields[7].size());
    }
    if (fields.size() > 8) {
        const char* begin = fields[8].data();
        data.linkLevel = ParseU32(begin, begin + fields[8].size());
    }

    if (payload.is_full_link) {
        data.name = ExtractBracketText(link, payloadEnd);
        data.quality = QualityFromColorCode(link, payload.start - 7);
    }

    if (data.itemId == 0) return std::nullopt;

    return data;
}

std::string ItemLinkParser::Generate(const ItemLinkData& data) {
    std::ostringstream os;
    os << "|c" << GetColorCode(data.quality)
       << "|Hitem:" << data.itemId
       << ':' << data.enchantId
       << ':' << data.gemIds[0]
       << ':' << data.gemIds[1]
       << ':' << data.gemIds[2]
       << ':' << data.extraEnchantId
       << ':' << data.randomPropertyId
       << ':' << data.suffixFactor
       << ':' << data.linkLevel
       << "|h[" << data.name << "]|h|r";
    return os.str();
}

std::optional<uint32_t> ItemLinkParser::GetItemId(const std::string& link) {
    auto parsed = Parse(link);
    if (!parsed) return std::nullopt;
    return parsed->itemId;
}

std::string ItemLinkParser::GetColorCode(uint8_t quality) {
    auto it = kQualityColors.find(quality);
    if (it != kQualityColors.end()) return it->second;
    return "ffffffff";
}

bool ItemLinkParser::IsItemLink(const std::string& text) {
    return text.find("item:") != std::string::npos;
}

std::vector<ItemLinkData> ItemLinkParser::ExtractLinks(const std::string& text) {
    std::vector<ItemLinkData> results;
    size_t offset = 0;
    while (true) {
        auto pos = text.find("|Hitem:", offset);
        if (pos == std::string::npos) break;

        auto endPos = text.find("|r", pos);
        if (endPos == std::string::npos) endPos = text.size();
        else endPos += 2;

        size_t startPos = pos;
        if (pos >= 10) {
            auto cPos = text.rfind("|c", pos);
            if (cPos != std::string::npos && pos - cPos <= 12)
                startPos = cPos;
        }

        std::string segment = text.substr(startPos, endPos - startPos);
        auto parsed = Parse(segment);
        if (parsed) results.push_back(std::move(*parsed));

        offset = endPos;
    }
    return results;
}

std::string ItemLinkParser::GetDisplayName(const ItemLinkData& data) {
    return "[" + data.name + "]";
}

bool ItemLinkParser::HasGems(const ItemLinkData& data) {
    return data.gemIds[0] != 0 || data.gemIds[1] != 0 || data.gemIds[2] != 0;
}

bool ItemLinkParser::HasEnchant(const ItemLinkData& data) {
    return data.enchantId != 0;
}

}

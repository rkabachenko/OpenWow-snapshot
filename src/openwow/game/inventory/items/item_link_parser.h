
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

struct ItemLinkData {
    std::uint32_t itemId = 0;
    std::uint32_t enchantId = 0;
    std::array<std::uint32_t, 3> gemIds = {0, 0, 0};

    std::int32_t extraEnchantId = 0;
    std::int32_t randomPropertyId = 0;
    std::int32_t suffixFactor = 0;
    std::uint32_t linkLevel = 0;

    std::string name;
    std::uint8_t quality = 1;

    void Reset() noexcept { *this = ItemLinkData{}; }
};

inline const std::unordered_map<uint8_t, std::string> kQualityColors = {
    {0, "ff9d9d9d"},
    {1, "ffffffff"},
    {2, "ff1eff00"},
    {3, "ff0070dd"},
    {4, "ffa335ee"},
    {5, "ffff8000"},
    {6, "ffe6cc80"},
    {7, "ff00ccff"},
};

class ItemLinkParser {
public:

    [[nodiscard]] static std::optional<ItemLinkData> Parse(const std::string& link);

    [[nodiscard]] static std::string Generate(const ItemLinkData& data);

    [[nodiscard]] static std::optional<uint32_t> GetItemId(const std::string& link);

    [[nodiscard]] static std::string GetColorCode(uint8_t quality);

    [[nodiscard]] static bool IsItemLink(const std::string& text);

    [[nodiscard]] static std::vector<ItemLinkData> ExtractLinks(const std::string& text);

    [[nodiscard]] static std::string GetDisplayName(const ItemLinkData& data);

    [[nodiscard]] static bool HasGems(const ItemLinkData& data);

    [[nodiscard]] static bool HasEnchant(const ItemLinkData& data);
};

}

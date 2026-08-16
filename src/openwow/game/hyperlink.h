
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

struct HyperlinkInfo {
  std::string type;
  std::uint32_t id = 0;
  std::vector<std::string> params;
  std::string display_text;
  std::uint32_t color = 0xFFFFFFFF;
};

class HyperlinkParser {
 public:

  static bool Parse(const std::string& link, HyperlinkInfo& out);

  static std::vector<HyperlinkInfo> ExtractAll(const std::string& text);

  static std::string Build(const std::string& type, std::uint32_t id,
                           const std::string& displayText,
                           std::uint32_t color = 0xFFFFFFFF,
                           const std::vector<std::string>& params = {});

  static std::string BuildItemLink(std::uint32_t itemId,
                                   const std::string& name,
                                   std::uint32_t quality,
                                   std::int32_t enchant = 0,
                                   std::int32_t gem1 = 0,
                                   std::int32_t gem2 = 0,
                                   std::int32_t gem3 = 0,
                                   std::int32_t suffixId = 0,
                                   std::int32_t uniqueId = 0,
                                   std::int32_t linkLevel = 0,
                                   std::int32_t extraId = 0);

  static std::string BuildSpellLink(std::uint32_t spellId,
                                    const std::string& name);

  static std::string BuildQuestLink(std::uint32_t questId,
                                    const std::string& name,
                                    std::int32_t level);

  static std::uint32_t GetQualityColor(std::uint32_t quality);

  static std::string StripLinks(const std::string& text);

  static std::string StripColors(const std::string& text);
};

}

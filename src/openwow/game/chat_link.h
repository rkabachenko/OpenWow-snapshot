#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

class ItemDefinitions;

enum class ChatLinkType : std::uint8_t {
  Item        = 0,
  Spell       = 1,
  Enchant     = 2,
  Achievement = 3,
  Quest       = 4,
  Talent      = 5,
  Glyph       = 6,
  Trade       = 7,
};

struct ChatLinkData {
  ChatLinkType               type{ChatLinkType::Item};
  std::uint32_t              link_id{0};
  std::string                display_text;
  std::string                raw_string;
  std::vector<std::uint32_t> extra_data;
};

class ChatLinkSystem {
 public:

  [[nodiscard]] static std::optional<ChatLinkData> ParseLink(
      const std::string& raw_link);

  [[nodiscard]] static std::string GenerateItemLink(
      const ItemDefinitions& item_definitions, std::uint32_t item_id,
      const std::string& name,
      std::uint32_t enchant_id = 0,
      const std::vector<std::uint32_t>& gems = {});

  [[nodiscard]] static std::string GenerateSpellLink(std::uint32_t spell_id,
                                                     const std::string& name);

  [[nodiscard]] static std::string GenerateAchievementLink(
      std::uint32_t ach_id, const std::string& name,
      bool completed = false);

  [[nodiscard]] static std::string GenerateQuestLink(
      std::uint32_t quest_id, const std::string& name,
      std::uint32_t level = 0);

  [[nodiscard]] static std::string GenerateTradeLink(
      std::uint32_t spell_id, const std::string& name);

  [[nodiscard]] static std::vector<ChatLinkData> ExtractLinks(
      const std::string& message);

  [[nodiscard]] static std::string StripLinks(const std::string& message);

  [[nodiscard]] static std::uint32_t GetLinkColor(ChatLinkType type);

  [[nodiscard]] static bool IsValidLink(const std::string& raw);

  [[nodiscard]] static ChatLinkType GetLinkType(
      const std::string& type_string);

  [[nodiscard]] static std::string GetTypeString(ChatLinkType type);

  static void Reset();
};

}

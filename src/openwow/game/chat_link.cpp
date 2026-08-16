#include "openwow/game/chat_link.h"

#include <charconv>
#include <cstdio>
#include <sstream>

#include "openwow/game/inventory/items/item_definitions.h"

namespace openwow::game {

namespace {

std::uint32_t ParseU32(std::string_view sv) {
  std::uint32_t val = 0;
  std::from_chars(sv.data(), sv.data() + sv.size(), val);
  return val;
}

std::vector<std::string_view> Split(std::string_view sv, char delim) {
  std::vector<std::string_view> parts;
  while (!sv.empty()) {
    auto pos = sv.find(delim);
    if (pos == std::string_view::npos) {
      parts.push_back(sv);
      break;
    }
    parts.push_back(sv.substr(0, pos));
    sv.remove_prefix(pos + 1);
  }
  return parts;
}

const char* LinkColorHex(ChatLinkType type) {
  switch (type) {
    case ChatLinkType::Item:        return "ff9d9d9d";
    case ChatLinkType::Spell:       return "ff71d5ff";
    case ChatLinkType::Enchant:     return "ffffd000";
    case ChatLinkType::Achievement: return "ffffff00";
    case ChatLinkType::Quest:       return "ffffff00";
    case ChatLinkType::Talent:      return "ff4e96f7";
    case ChatLinkType::Glyph:       return "ff66bbff";
    case ChatLinkType::Trade:       return "ffffd000";
  }
  return "ffffffff";
}

}

std::optional<ChatLinkData> ChatLinkSystem::ParseLink(
    const std::string& raw_link) {

  if (raw_link.size() < 12) return std::nullopt;

  std::string_view sv(raw_link);
  if (sv.starts_with("|c") || sv.starts_with("|C")) {

    if (sv.size() < 10) return std::nullopt;
    sv.remove_prefix(10);
  }

  if (!sv.starts_with("|H") && !sv.starts_with("|h")) return std::nullopt;

  if (sv[0] == '|' && (sv[1] == 'H' || sv[1] == 'h')) {
    sv.remove_prefix(2);
  } else {
    return std::nullopt;
  }

  auto h_pos = sv.find("|h");
  if (h_pos == std::string_view::npos) {
    h_pos = sv.find("|H");
    if (h_pos == std::string_view::npos) return std::nullopt;
  }

  std::string_view data_part = sv.substr(0, h_pos);
  sv.remove_prefix(h_pos + 2);

  auto fields = Split(data_part, ':');
  if (fields.size() < 2) return std::nullopt;

  ChatLinkData result;
  result.raw_string = raw_link;

  std::string type_str(fields[0]);
  result.type = GetLinkType(type_str);
  result.link_id = ParseU32(fields[1]);

  for (std::size_t i = 2; i < fields.size(); ++i) {
    result.extra_data.push_back(ParseU32(fields[i]));
  }

  if (sv.starts_with("[")) {
    auto close = sv.find(']');
    if (close != std::string_view::npos) {
      result.display_text = std::string(sv.substr(1, close - 1));
    }
  }

  return result;
}

std::string ChatLinkSystem::GenerateItemLink(
    const ItemDefinitions& item_definitions, std::uint32_t item_id,
    const std::string& name,
    std::uint32_t enchant_id,
    const std::vector<std::uint32_t>& gems) {

  const char* color_code = LinkColorHex(ChatLinkType::Item);
  const auto* item = item_definitions.GetItem(item_id);
  if (item) {
    color_code = ItemTemplate::GetQualityColorCode(item->quality);
  }
  std::ostringstream os;
  os << "|c" << color_code << "|Hitem:" << item_id << ":" << enchant_id;
  for (auto g : gems) os << ":" << g;
  os << "|h[" << name << "]|h|r";
  return os.str();
}

std::string ChatLinkSystem::GenerateSpellLink(std::uint32_t spell_id,
                                              const std::string& name) {
  std::ostringstream os;
  os << "|c" << LinkColorHex(ChatLinkType::Spell) << "|Hspell:" << spell_id << "|h[" << name << "]|h|r";
  return os.str();
}

std::string ChatLinkSystem::GenerateAchievementLink(std::uint32_t ach_id,
                                                    const std::string& name,
                                                    bool completed) {
  std::ostringstream os;
  os << "|c" << LinkColorHex(ChatLinkType::Achievement) << "|Hachievement:" << ach_id << ":" << (completed ? 1 : 0)
     << "|h[" << name << "]|h|r";
  return os.str();
}

std::string ChatLinkSystem::GenerateQuestLink(std::uint32_t quest_id,
                                              const std::string& name,
                                              std::uint32_t level) {
  std::ostringstream os;
  os << "|c" << LinkColorHex(ChatLinkType::Quest) << "|Hquest:" << quest_id << ":" << level << "|h[" << name
     << "]|h|r";
  return os.str();
}

std::string ChatLinkSystem::GenerateTradeLink(std::uint32_t spell_id,
                                              const std::string& name) {
  std::ostringstream os;
  os << "|c" << LinkColorHex(ChatLinkType::Trade) << "|Htrade:" << spell_id << "|h[" << name << "]|h|r";
  return os.str();
}

std::vector<ChatLinkData> ChatLinkSystem::ExtractLinks(
    const std::string& message) {
  std::vector<ChatLinkData> links;
  std::string_view sv(message);

  while (!sv.empty()) {

    auto start = sv.find("|H");
    auto cstart = sv.find("|c");
    if (start == std::string_view::npos && cstart == std::string_view::npos)
      break;

    std::size_t link_begin =
        (cstart != std::string_view::npos &&
         (start == std::string_view::npos || cstart < start))
            ? cstart
            : start;

    auto rest = sv.substr(link_begin);

    auto close_pos = rest.find("]|h|r");
    std::size_t link_len;
    if (close_pos != std::string_view::npos) {
      link_len = close_pos + 5;
    } else {
      close_pos = rest.find("]|h");
      if (close_pos == std::string_view::npos) break;
      link_len = close_pos + 3;
    }

    std::string raw(rest.substr(0, link_len));
    auto parsed = ParseLink(raw);
    if (parsed) links.push_back(std::move(*parsed));

    sv.remove_prefix(link_begin + link_len);
  }

  return links;
}

std::string ChatLinkSystem::StripLinks(const std::string& message) {
  std::string result;
  result.reserve(message.size());
  std::string_view sv(message);

  while (!sv.empty()) {
    auto start = sv.find("|c");
    auto hstart = sv.find("|H");
    std::size_t link_begin = std::string_view::npos;

    if (start != std::string_view::npos)
      link_begin = start;
    if (hstart != std::string_view::npos &&
        (link_begin == std::string_view::npos || hstart < link_begin))
      link_begin = hstart;

    if (link_begin == std::string_view::npos) {
      result.append(sv);
      break;
    }

    result.append(sv.substr(0, link_begin));

    auto rest = sv.substr(link_begin);

    auto b1 = rest.find('[');
    auto b2 = rest.find(']');
    if (b1 != std::string_view::npos && b2 != std::string_view::npos &&
        b2 > b1) {
      result.append(rest.substr(b1 + 1, b2 - b1 - 1));
    }

    auto end_pos = rest.find("]|h|r");
    if (end_pos != std::string_view::npos) {
      sv.remove_prefix(link_begin + end_pos + 5);
    } else {
      end_pos = rest.find("]|h");
      if (end_pos != std::string_view::npos) {
        sv.remove_prefix(link_begin + end_pos + 3);
      } else {

        result.append(rest);
        break;
      }
    }
  }

  return result;
}

std::uint32_t ChatLinkSystem::GetLinkColor(ChatLinkType type) {
  switch (type) {
    case ChatLinkType::Item:        return 0xFF9D9D9D;
    case ChatLinkType::Spell:       return 0xFF71D5FF;
    case ChatLinkType::Enchant:     return 0xFFFFD000;
    case ChatLinkType::Achievement: return 0xFFFFFF00;
    case ChatLinkType::Quest:       return 0xFFFFFF00;
    case ChatLinkType::Talent:      return 0xFF4E96F7;
    case ChatLinkType::Glyph:       return 0xFF66BBFF;
    case ChatLinkType::Trade:       return 0xFFFFD000;
  }
  return 0xFFFFFFFF;
}

bool ChatLinkSystem::IsValidLink(const std::string& raw) {
  return ParseLink(raw).has_value();
}

ChatLinkType ChatLinkSystem::GetLinkType(const std::string& type_string) {
  if (type_string == "item")        return ChatLinkType::Item;
  if (type_string == "spell")       return ChatLinkType::Spell;
  if (type_string == "enchant")     return ChatLinkType::Enchant;
  if (type_string == "achievement") return ChatLinkType::Achievement;
  if (type_string == "quest")       return ChatLinkType::Quest;
  if (type_string == "talent")      return ChatLinkType::Talent;
  if (type_string == "glyph")       return ChatLinkType::Glyph;
  if (type_string == "trade")       return ChatLinkType::Trade;
  return ChatLinkType::Item;
}

std::string ChatLinkSystem::GetTypeString(ChatLinkType type) {
  switch (type) {
    case ChatLinkType::Item:        return "item";
    case ChatLinkType::Spell:       return "spell";
    case ChatLinkType::Enchant:     return "enchant";
    case ChatLinkType::Achievement: return "achievement";
    case ChatLinkType::Quest:       return "quest";
    case ChatLinkType::Talent:      return "talent";
    case ChatLinkType::Glyph:       return "glyph";
    case ChatLinkType::Trade:       return "trade";
  }
  return "unknown";
}

void ChatLinkSystem::Reset() {

}

}

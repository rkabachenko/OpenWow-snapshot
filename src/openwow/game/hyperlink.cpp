
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/items/item_definitions.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace openwow::game {

static std::uint32_t ParseHexColor(const char* s, std::size_t len) {

  std::uint32_t v = 0;
  for (std::size_t i = 0; i < len && i < 8; ++i) {
    char c = s[i];
    std::uint32_t nibble = 0;
    if (c >= '0' && c <= '9')
      nibble = static_cast<std::uint32_t>(c - '0');
    else if (c >= 'a' && c <= 'f')
      nibble = static_cast<std::uint32_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
      nibble = static_cast<std::uint32_t>(c - 'A' + 10);
    else
      break;
    v = (v << 4) | nibble;
  }
  return v;
}

static std::uint32_t ParseUInt(const std::string& s) {
  std::uint32_t v = 0;
  std::from_chars(s.data(), s.data() + s.size(), v);
  return v;
}

static void SplitColon(const std::string& s, std::vector<std::string>& out) {
  out.clear();
  std::size_t start = 0;
  while (start <= s.size()) {
    auto pos = s.find(':', start);
    if (pos == std::string::npos) {
      out.push_back(s.substr(start));
      break;
    }
    out.push_back(s.substr(start, pos - start));
    start = pos + 1;
  }
}

bool HyperlinkParser::Parse(const std::string& link, HyperlinkInfo& out) {
  out = HyperlinkInfo{};

  auto cpos = link.find("|c");
  if (cpos == std::string::npos) cpos = link.find("|C");
  if (cpos == std::string::npos) return false;

  std::size_t color_start = cpos + 2;
  if (color_start + 8 > link.size()) return false;
  out.color = ParseHexColor(link.data() + color_start, 8);

  auto hstart = link.find("|H", color_start + 8);
  if (hstart == std::string::npos) hstart = link.find("|h", color_start + 8);

  hstart = link.find("|H", color_start + 8);
  if (hstart == std::string::npos) return false;

  auto hclose = link.find("|h", hstart + 2);
  if (hclose == std::string::npos) return false;

  std::string data = link.substr(hstart + 2, hclose - (hstart + 2));

  std::vector<std::string> parts;
  SplitColon(data, parts);
  if (parts.empty()) return false;

  out.type = parts[0];
  if (parts.size() > 1) out.id = ParseUInt(parts[1]);
  for (std::size_t i = 2; i < parts.size(); ++i) {
    out.params.push_back(parts[i]);
  }

  auto text_start = hclose + 2;
  auto text_end = link.find("|h", text_start);
  if (text_end == std::string::npos) text_end = link.size();
  std::string display = link.substr(text_start, text_end - text_start);

  if (display.size() >= 2 && display.front() == '[' && display.back() == ']') {
    display = display.substr(1, display.size() - 2);
  }
  out.display_text = display;

  return true;
}

std::vector<HyperlinkInfo> HyperlinkParser::ExtractAll(
    const std::string& text) {
  std::vector<HyperlinkInfo> results;
  std::size_t pos = 0;

  while (pos < text.size()) {

    auto cpos = text.find("|c", pos);
    if (cpos == std::string::npos) {
      cpos = text.find("|C", pos);
    }
    if (cpos == std::string::npos) break;

    auto rpos = text.find("|r", cpos);
    if (rpos == std::string::npos) {
      rpos = text.find("|R", cpos);
    }
    if (rpos == std::string::npos) break;

    std::string segment = text.substr(cpos, rpos + 2 - cpos);
    HyperlinkInfo info;
    if (Parse(segment, info)) {
      results.push_back(std::move(info));
    }
    pos = rpos + 2;
  }
  return results;
}

std::string HyperlinkParser::Build(const std::string& type, std::uint32_t id,
                                   const std::string& displayText,
                                   std::uint32_t color,
                                   const std::vector<std::string>& params) {

  char color_str[16];
  std::snprintf(color_str, sizeof(color_str), "%08x", color);

  std::ostringstream oss;
  oss << "|c" << color_str;
  oss << "|H" << type << ":" << id;
  for (const auto& p : params) {
    oss << ":" << p;
  }
  oss << "|h[" << displayText << "]|h|r";
  return oss.str();
}

std::string HyperlinkParser::BuildItemLink(std::uint32_t itemId,
                                           const std::string& name,
                                           std::uint32_t quality,
                                           std::int32_t enchant,
                                           std::int32_t gem1,
                                           std::int32_t gem2,
                                           std::int32_t gem3,
                                           std::int32_t suffixId,
                                           std::int32_t uniqueId,
                                           std::int32_t linkLevel,
                                           std::int32_t extraId) {
  std::uint32_t color = GetQualityColor(quality);

  std::vector<std::string> params;
  params.push_back(std::to_string(enchant));
  params.push_back(std::to_string(gem1));
  params.push_back(std::to_string(gem2));
  params.push_back(std::to_string(gem3));
  params.push_back(std::to_string(suffixId));
  params.push_back(std::to_string(uniqueId));
  params.push_back(std::to_string(linkLevel));
  params.push_back(std::to_string(extraId));
  return Build("item", itemId, name, color, params);
}

std::string HyperlinkParser::BuildSpellLink(std::uint32_t spellId,
                                            const std::string& name) {

  return Build("spell", spellId, name, 0xFF71d5ff);
}

std::string HyperlinkParser::BuildQuestLink(std::uint32_t questId,
                                            const std::string& name,
                                            std::int32_t level) {

  return Build("quest", questId, name, 0xFFFFFF00,
               {std::to_string(level)});
}

std::uint32_t HyperlinkParser::GetQualityColor(std::uint32_t quality) {
  return ItemTemplate::GetQualityColorInfo(quality).argb;
}

std::string HyperlinkParser::StripLinks(const std::string& text) {

  std::string result;
  result.reserve(text.size());

  std::size_t i = 0;
  while (i < text.size()) {

    if (i + 1 < text.size() && text[i] == '|' &&
        (text[i + 1] == 'c' || text[i + 1] == 'C')) {

      i += 2;

      std::size_t hex_count = 0;
      while (i < text.size() && hex_count < 8) {
        ++i;
        ++hex_count;
      }

      if (i + 1 < text.size() && text[i] == '|' && text[i + 1] == 'H') {
        i += 2;

        while (i + 1 < text.size()) {
          if (text[i] == '|' && (text[i + 1] == 'h' || text[i + 1] == 'H')) {
            i += 2;
            break;
          }
          ++i;
        }

        while (i < text.size()) {
          if (i + 1 < text.size() && text[i] == '|' &&
              (text[i + 1] == 'h' || text[i + 1] == 'H')) {
            i += 2;
            break;
          }

          if (text[i] != '[' && text[i] != ']') {
            result.push_back(text[i]);
          }
          ++i;
        }

        if (i + 1 < text.size() && text[i] == '|' &&
            (text[i + 1] == 'r' || text[i + 1] == 'R')) {
          i += 2;
        }
      } else {

        while (i < text.size()) {
          if (i + 1 < text.size() && text[i] == '|' &&
              (text[i + 1] == 'r' || text[i + 1] == 'R')) {
            i += 2;
            break;
          }
          result.push_back(text[i]);
          ++i;
        }
      }
    } else {
      result.push_back(text[i]);
      ++i;
    }
  }

  return result;
}

std::string HyperlinkParser::StripColors(const std::string& text) {

  std::string result;
  result.reserve(text.size());

  std::size_t i = 0;
  while (i < text.size()) {
    if (i + 1 < text.size() && text[i] == '|') {
      char next = text[i + 1];
      if (next == 'c' || next == 'C') {

        i += 2;
        std::size_t hex_count = 0;
        while (i < text.size() && hex_count < 8) {
          ++i;
          ++hex_count;
        }
        continue;
      }
      if (next == 'r' || next == 'R') {
        i += 2;
        continue;
      }
    }
    result.push_back(text[i]);
    ++i;
  }
  return result;
}

}

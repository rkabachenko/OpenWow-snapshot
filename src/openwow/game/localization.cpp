
#include "openwow/game/localization.h"

#include "openwow/core/storm_string.h"
#include "openwow/game/client_config.h"
#include "openwow/game/declined_words.h"
#include "openwow/game/name_declension.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

namespace openwow::game {

int FormatRuntimeStringTemplateInto(char* const buffer,
                                    const std::size_t buffer_bytes,
                                    const char* const format, ...) {
  if (buffer == nullptr || buffer_bytes == 0 || format == nullptr) {
    return -1;
  }

  std::va_list args;
  va_start(args, format);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
  const int written =
      static_cast<int>(openwow::core::SStrPrintfV(buffer, buffer_bytes, format, args));
#pragma GCC diagnostic pop
  va_end(args);
  return written;
}

namespace {

constexpr std::size_t kClientTextTagBufferBytes = 0x2000;
constexpr std::size_t kClientTextMaxVisibleBytes = kClientTextTagBufferBytes - 1;
constexpr std::size_t kDeclinedNameTagPayloadBytes = 1023;
constexpr char16_t kHangulFirstSyllable = 0xAC00;
constexpr char16_t kHangulLastSyllable = 0xD7A3;
constexpr char16_t kKoreanParticleEu = 0xC73C;
constexpr std::uint8_t kKoreanDigitSelectorTable[10] = {
    1, 1, 0, 1, 0, 0, 1, 1, 1, 0,
};
constexpr std::uint8_t kKoreanLetterSelectorTable[26] = {
    0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
    1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

std::string ClampClientTextBytes(std::string_view text) {
  const std::size_t capped_size = std::min(text.size(), kClientTextMaxVisibleBytes);
  return std::string(text.substr(0, capped_size));
}

std::string ClampVisibleBytes(std::string_view text, std::size_t max_visible_bytes) {
  return std::string(text.substr(0, std::min(text.size(), max_visible_bytes)));
}

void AppendCapped(std::string& output,
                  std::string_view text,
                  std::size_t max_visible_bytes) {
  if (output.size() >= max_visible_bytes) {
    return;
  }

  output.append(text.substr(0, max_visible_bytes - output.size()));
}

void AppendCapped(std::string& output, char ch, std::size_t max_visible_bytes) {
  if (output.size() < max_visible_bytes) {
    output.push_back(ch);
  }
}

std::u16string Utf8ToUtf16(std::string_view utf8) {
  std::u16string result;
  result.reserve(utf8.size());

  std::size_t i = 0;
  while (i < utf8.size()) {
    const auto first = static_cast<std::uint8_t>(utf8[i]);
    std::uint32_t code_point = 0;

    if (first < 0x80u) {
      code_point = first;
      ++i;
    } else if ((first & 0xE0u) == 0xC0u && i + 1 < utf8.size()) {
      code_point = ((first & 0x1Fu) << 6) |
                   (static_cast<std::uint8_t>(utf8[i + 1]) & 0x3Fu);
      i += 2;
    } else if ((first & 0xF0u) == 0xE0u && i + 2 < utf8.size()) {
      code_point = ((first & 0x0Fu) << 12) |
                   ((static_cast<std::uint8_t>(utf8[i + 1]) & 0x3Fu) << 6) |
                   (static_cast<std::uint8_t>(utf8[i + 2]) & 0x3Fu);
      i += 3;
    } else if ((first & 0xF8u) == 0xF0u && i + 3 < utf8.size()) {
      code_point = ((first & 0x07u) << 18) |
                   ((static_cast<std::uint8_t>(utf8[i + 1]) & 0x3Fu) << 12) |
                   ((static_cast<std::uint8_t>(utf8[i + 2]) & 0x3Fu) << 6) |
                   (static_cast<std::uint8_t>(utf8[i + 3]) & 0x3Fu);
      i += 4;
    } else {
      code_point = first;
      ++i;
    }

    if (code_point <= 0xFFFFu) {
      result.push_back(static_cast<char16_t>(code_point));
      continue;
    }

    code_point -= 0x10000u;
    result.push_back(static_cast<char16_t>(0xD800u + (code_point >> 10)));
    result.push_back(static_cast<char16_t>(0xDC00u + (code_point & 0x3FFu)));
  }

  return result;
}

std::string Utf16ToUtf8(std::u16string_view utf16) {
  std::string result;
  result.reserve(utf16.size() * 3);

  for (std::size_t i = 0; i < utf16.size(); ++i) {
    std::uint32_t code_point = utf16[i];
    if (code_point >= 0xD800u && code_point <= 0xDBFFu && i + 1 < utf16.size()) {
      const auto low = static_cast<std::uint32_t>(utf16[i + 1]);
      if (low >= 0xDC00u && low <= 0xDFFFu) {
        code_point =
            ((code_point - 0xD800u) << 10) + (low - 0xDC00u) + 0x10000u;
        ++i;
      }
    }

    if (code_point < 0x80u) {
      result.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800u) {
      result.push_back(static_cast<char>(0xC0u | (code_point >> 6)));
      result.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    } else if (code_point < 0x10000u) {
      result.push_back(static_cast<char>(0xE0u | (code_point >> 12)));
      result.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
      result.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    } else {
      result.push_back(static_cast<char>(0xF0u | (code_point >> 18)));
      result.push_back(static_cast<char>(0x80u | ((code_point >> 12) & 0x3Fu)));
      result.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
      result.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    }
  }

  return result;
}

bool IsTrackedKoreanSelectorChar(char16_t ch) {
  if ((ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z') ||
      (ch >= u'0' && ch <= u'9')) {
    return true;
  }
  return ch >= kHangulFirstSyllable && ch <= kHangulLastSyllable;
}

bool SelectSecondKoreanAlternative(char16_t previous_char,
                                   char16_t first_alternative_first_char) {
  if (previous_char >= kHangulFirstSyllable && previous_char <= kHangulLastSyllable) {
    if (((previous_char - 44040) % 28) == 0 &&
        first_alternative_first_char == kKoreanParticleEu) {
      return true;
    }
    return ((previous_char - 44032) % 28) == 0;
  }

  if (previous_char >= u'0' && previous_char <= u'9') {
    return kKoreanDigitSelectorTable[previous_char - u'0'] == 0;
  }

  if ((previous_char >= u'a' && previous_char <= u'z') ||
      (previous_char >= u'A' && previous_char <= u'Z')) {
    const char16_t lower =
        (previous_char >= u'A' && previous_char <= u'Z')
            ? static_cast<char16_t>(previous_char - u'A' + u'a')
            : previous_char;
    return kKoreanLetterSelectorTable[lower - u'a'] == 0;
  }

  return false;
}

std::string ApplyKoreanParticleTag(std::string_view input) {
  const std::u16string wide = Utf8ToUtf16(ClampClientTextBytes(input));
  std::u16string output;
  output.reserve(wide.size());

  char16_t previous_selector_char = 0;
  for (std::size_t i = 0; i < wide.size(); ++i) {
    const char16_t ch = wide[i];
    if (ch == u'|' && i + 1 < wide.size()) {
      const char16_t tag = wide[i + 1];
      if (tag == u'1' &&
          (i + 2 >= wide.size() || wide[i + 2] < u'0' || wide[i + 2] > u'9')) {
        const std::size_t first_start = i + 2;
        std::size_t second_start = first_start;
        while (second_start < wide.size() && wide[second_start] != u';') {
          ++second_start;
        }
        if (second_start < wide.size()) {
          ++second_start;
        }

        std::size_t after_second = second_start;
        while (after_second < wide.size() && wide[after_second] != u';') {
          ++after_second;
        }

        const std::size_t selected_start =
            SelectSecondKoreanAlternative(
                previous_selector_char,
                first_start < wide.size() ? wide[first_start] : 0)
                ? second_start
                : first_start;
        for (std::size_t cursor = selected_start;
             cursor < wide.size() && wide[cursor] != u';';
             ++cursor) {
          output.push_back(wide[cursor]);
        }

        i = after_second;
        continue;
      }

      output.push_back(u'|');
      output.push_back(tag);
      i += 1;

      if (tag == u'c' || tag == u'C') {
        for (std::size_t copied = 0; copied < 8 && i + 1 < wide.size(); ++copied) {
          output.push_back(wide[++i]);
        }
      }
      continue;
    }

    if (IsTrackedKoreanSelectorChar(ch)) {
      previous_selector_char = ch;
    }
    output.push_back(ch);
  }

  return Utf16ToUtf8(output);
}

bool IsFrenchElisionVowel(char16_t ch) {
  switch (ch) {
    case u'A':
    case u'E':
    case u'I':
    case u'O':
    case u'U':
    case u'a':
    case u'e':
    case u'i':
    case u'o':
    case u'u':
      return true;
    default:
      return (ch >= 0x00C0 && ch <= 0x00C6) ||
             (ch >= 0x00C8 && ch <= 0x00CF) ||
             (ch >= 0x00D2 && ch <= 0x00D6) ||
             (ch >= 0x00D8 && ch <= 0x00DC) ||
             (ch >= 0x00E0 && ch <= 0x00E6) ||
             (ch >= 0x00E8 && ch <= 0x00EF) ||
             (ch >= 0x00F2 && ch <= 0x00F6) ||
             (ch >= 0x00F8 && ch <= 0x00FC);
  }
}

std::string ApplyFrenchContractionTag(std::string_view input) {
  const std::u16string wide = Utf8ToUtf16(ClampClientTextBytes(input));
  std::u16string output;
  output.reserve(wide.size());

  for (std::size_t i = 0; i < wide.size(); ++i) {
    if (wide[i] == u'|' && i + 1 < wide.size()) {
      const char16_t tag = wide[i + 1];
      if (tag == u'2' &&
          (i + 2 >= wide.size() || wide[i + 2] < u'0' || wide[i + 2] > u'9')) {
        i += 2;
        while (i < wide.size() && wide[i] == u' ') {
          ++i;
        }

        if (i < wide.size() && wide[i] == u'L') {
          if (i + 1 < wide.size() && wide[i + 1] == u'\'') {
            output.append(u"de l'");
            i += 1;
            continue;
          }
          if (i + 2 < wide.size() && wide[i + 1] == u'e' && wide[i + 2] == u' ') {
            output.append(u"du ");
            i += 2;
            continue;
          }
          if (i + 2 < wide.size() && wide[i + 1] == u'a' && wide[i + 2] == u' ') {
            output.append(u"de la ");
            i += 2;
            continue;
          }
          if (i + 3 < wide.size() && wide[i + 1] == u'e' && wide[i + 2] == u's' &&
              wide[i + 3] == u' ') {
            output.append(u"des ");
            i += 3;
            continue;
          }
        }

        const char16_t first = i < wide.size() ? wide[i] : 0;
        const char16_t second = i + 1 < wide.size() ? wide[i + 1] : 0;
        if (IsFrenchElisionVowel(first) || first == u'h' || first == u'H' ||
            ((first == u'y' || first == u'Y') && IsFrenchElisionVowel(second))) {
          output.append(u"d'");
        } else {
          output.append(u"de ");
        }

        if (i < wide.size()) {
          --i;
        }
        continue;
      }
    }

    output.push_back(wide[i]);
  }

  return Utf16ToUtf8(output);
}

bool IsRussianCyrillicLeadingLetter(std::string_view text) {
  return declension::StartsWithCyrillicCodeUnit(text);
}

std::optional<std::string> LookupStoredDeclinedNameCase(std::string_view name,
                                                        int case_index,
                                                        Locale locale) {
  if (locale != Locale::ruRU || case_index < 1 || case_index > 5 ||
      !IsRussianCyrillicLeadingLetter(name)) {
    return std::nullopt;
  }

  const std::string name_copy(name);
  if (const DeclinedName* declined = DeclinedWords::Get().GetDeclinedName(name_copy);
      declined != nullptr) {
    return declined->forms[static_cast<std::size_t>(case_index - 1)];
  }

  return std::nullopt;
}

std::optional<std::string> ResolveDeclinedNameCase(std::string_view name,
                                                   int case_index,
                                                   Locale locale) {
  if (auto stored = LookupStoredDeclinedNameCase(name, case_index, locale)) {
    return stored;
  }

  const auto hyphen = name.find('-');
  if (hyphen != std::string_view::npos && hyphen != 0) {
    if (auto prefix = LookupStoredDeclinedNameCase(name.substr(0, hyphen), case_index, locale)) {
      return *prefix + std::string(name.substr(hyphen));
    }
  }

  if (locale != Locale::ruRU) {
    return std::nullopt;
  }

  std::array<std::string, 5> forms{};
  if (!declension::BuildForms(name, 2, 0, forms)) {
    return std::nullopt;
  }
  return forms[static_cast<std::size_t>(case_index - 1)];
}

int ParseDecimalPrefix(std::string_view text, std::size_t& cursor) {
  int value = 0;
  while (cursor < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[cursor]))) {
    value = value * 10 + (text[cursor] - '0');
    ++cursor;
  }
  return value;
}

void CopyDeclinedNameTagPrefix(std::string_view source,
                               std::size_t& cursor,
                               std::string& output,
                               std::size_t max_visible_bytes) {
  while (cursor < source.size() && source[cursor] == '|') {
    AppendCapped(output, '|', max_visible_bytes);
    ++cursor;
    if (cursor >= source.size()) {
      return;
    }

    const char tag = source[cursor];
    switch (tag) {
      case 'C':
      case 'c':
        AppendCapped(output, tag, max_visible_bytes);
        ++cursor;
        for (std::size_t copied = 0; copied < 8 && cursor < source.size(); ++copied, ++cursor) {
          AppendCapped(output, source[cursor], max_visible_bytes);
        }
        break;
      case 'H':
        while (cursor < source.size()) {
          const char current = source[cursor];
          AppendCapped(output, current, max_visible_bytes);
          ++cursor;
          if (current == '|' && cursor < source.size() && source[cursor] == 'h') {
            AppendCapped(output, source[cursor], max_visible_bytes);
            ++cursor;
            break;
          }
        }
        break;
      case 'T':
        while (cursor < source.size()) {
          const char current = source[cursor];
          AppendCapped(output, current, max_visible_bytes);
          ++cursor;
          if (current == '|' && cursor < source.size() && source[cursor] == 't') {
            AppendCapped(output, source[cursor], max_visible_bytes);
            ++cursor;
            break;
          }
        }
        break;
      default:
        AppendCapped(output, tag, max_visible_bytes);
        ++cursor;
        break;
    }
  }
}

std::string ApplyDeclinedNameTagBuffer(std::string_view input,
                                       int case_index,
                                       Locale locale,
                                       std::size_t max_visible_bytes) {
  const std::string source = ClampVisibleBytes(input, kDeclinedNameTagPayloadBytes);
  if (source.empty()) {
    return {};
  }

  if (source.front() != '|') {
    if (auto resolved = ResolveDeclinedNameCase(source, case_index, locale)) {
      return ClampVisibleBytes(*resolved, max_visible_bytes);
    }
    return ClampVisibleBytes(source, max_visible_bytes);
  }

  std::string output;
  output.reserve(std::min(source.size(), max_visible_bytes));

  std::size_t cursor = 0;
  CopyDeclinedNameTagPrefix(source, cursor, output, max_visible_bytes);

  if (cursor < source.size() && source[cursor] == '[') {
    AppendCapped(output, '[', max_visible_bytes);
    ++cursor;
  }

  const std::size_t token_start = cursor;
  while (cursor < source.size() && source[cursor] != ']' && source[cursor] != '|') {
    ++cursor;
  }

  const std::string token =
      ClampVisibleBytes(source.substr(token_start, cursor - token_start),
                        kDeclinedNameTagPayloadBytes);
  const std::size_t remaining_capacity =
      (output.size() < max_visible_bytes) ? (max_visible_bytes - output.size()) : 0;
  AppendCapped(output,
               ApplyDeclinedNameTagBuffer(token, case_index, locale, remaining_capacity),
               max_visible_bytes);
  AppendCapped(output, source.substr(cursor), max_visible_bytes);

  return output;
}

std::string ApplyDeclinedNameTag(std::string_view input, Locale locale) {
  const std::string source = ClampClientTextBytes(input);
  std::string output;
  output.reserve(source.size());

  std::size_t cursor = 0;
  while (cursor < source.size()) {
    if (source[cursor] != '|') {
      AppendCapped(output, source[cursor], kClientTextMaxVisibleBytes);
      ++cursor;
      continue;
    }

    if (cursor + 1 >= source.size()) {
      AppendCapped(output, '|', kClientTextMaxVisibleBytes);
      break;
    }

    if (source[cursor + 1] == '3' &&
        (cursor + 2 >= source.size() ||
         !std::isdigit(static_cast<unsigned char>(source[cursor + 2])))) {
      std::size_t case_cursor = cursor + 1;
      if (cursor + 3 < source.size() && source[cursor + 2] == '-' &&
          std::isdigit(static_cast<unsigned char>(source[cursor + 3]))) {
        case_cursor = cursor + 3;
      }

      std::size_t parse_cursor = case_cursor;
      const int case_index = ParseDecimalPrefix(source, parse_cursor);
      std::size_t token_cursor = parse_cursor;
      if (token_cursor < source.size() && source[token_cursor] == '(') {
        ++token_cursor;
      }

      const std::size_t payload_start = token_cursor;
      while (token_cursor < source.size() && source[token_cursor] != ')') {
        ++token_cursor;
      }

      const std::string payload =
          ClampVisibleBytes(source.substr(payload_start, token_cursor - payload_start),
                            kDeclinedNameTagPayloadBytes);
      AppendCapped(output,
                   ApplyDeclinedNameTagBuffer(payload,
                                             case_index,
                                             locale,
                                             kClientTextMaxVisibleBytes - output.size()),
                   kClientTextMaxVisibleBytes);

      cursor = (token_cursor < source.size()) ? (token_cursor + 1) : source.size();
      continue;
    }

    AppendCapped(output, '|', kClientTextMaxVisibleBytes);
    AppendCapped(output, source[cursor + 1], kClientTextMaxVisibleBytes);
    cursor += 2;
  }

  return output;
}

}

int GetPluralRuleMode(Locale locale) {
  switch (locale) {
    case Locale::koKR:
    case Locale::frFR:
    case Locale::zhCN:
    case Locale::zhTW:
      return 1;
    case Locale::ruRU:
      return 2;
    default:
      return 0;
  }
}

int SelectPluralFormIndex(int value, int plural_rule_mode) {
  if (plural_rule_mode == 0) {
    return value != 1;
  }
  if (plural_rule_mode == 1) {
    return value > 1;
  }
  if (plural_rule_mode != 2) {
    return 0;
  }
  if (static_cast<unsigned>(value % 100 - 11) <= 3u) {
    return 2;
  }
  if (value % 10 == 1) {
    return 0;
  }
  return (static_cast<unsigned>(value % 10 - 2) > 2u) + 1;
}

int SelectPluralFormIndex(int value) {
  return SelectPluralFormIndex(value, GetPluralRuleMode(Localization::Get().GetLocale()));
}

namespace {

std::optional<std::string> TryGetLuaGlobalString(lua_State* state,
                                                 std::string_view global_name) {
  if (state == nullptr) {
    return std::nullopt;
  }

  const std::string name(global_name);
  lua_getglobal(state, name.c_str());

  std::optional<std::string> value;
  if (lua_isstring(state, -1) != 0) {
    value = std::string(lua_tostring(state, -1));
  }

  lua_pop(state, 1);
  return value;
}

std::string BuildPluralVariantSuffix(Locale locale, int ordinal) {
  const int plural_form = SelectPluralFormIndex(ordinal, GetPluralRuleMode(locale));
  if (plural_form < 1 || plural_form > 9) {
    return {};
  }

  std::string suffix = "_P";
  suffix.push_back(static_cast<char>('0' + plural_form));
  return suffix;
}

std::string ApplyPluralTag(std::string_view input, Locale locale) {
  const std::string source = ClampClientTextBytes(input);
  std::string output;
  output.reserve(source.size());

  const int plural_rule_mode = GetPluralRuleMode(locale);
  int last_number = 0;
  bool previous_was_digit = false;

  for (std::size_t i = 0; i < source.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(source[i]);
    if (std::isdigit(ch)) {
      last_number = previous_was_digit ? (last_number * 10 + (ch - '0')) : (ch - '0');
      previous_was_digit = true;
    } else {
      previous_was_digit = false;
    }

    if (source[i] == '|' && i + 1 < source.size() && source[i + 1] == '4' &&
        (i + 2 >= source.size() || !std::isdigit(static_cast<unsigned char>(source[i + 2])))) {
      std::size_t cursor = i + 2;
      unsigned selected_index =
          static_cast<unsigned>(SelectPluralFormIndex(last_number, plural_rule_mode));

      for (unsigned skipped = 0; skipped < selected_index; ++cursor) {
        if (cursor >= source.size()) {
          break;
        }
        while (cursor < source.size() && source[cursor] != ':' && source[cursor] != ';') {
          ++cursor;
        }
        if (cursor >= source.size() || source[cursor] == ';') {
          break;
        }
        ++skipped;
      }

      while (cursor < source.size() && source[cursor] == ' ') {
        ++cursor;
      }
      while (cursor < source.size() && source[cursor] != ':' && source[cursor] != ';') {
        output.push_back(source[cursor++]);
      }
      while (cursor < source.size() && source[cursor] != ';') {
        ++cursor;
      }

      i = cursor;
      previous_was_digit = false;
      continue;
    }

    output.push_back(source[i]);
  }

  return output;
}

std::optional<int> FindNextLocalizedTag(std::string_view text) {
  for (std::size_t i = 0; i + 1 < text.size(); ++i) {
    if (text[i] != '|') {
      continue;
    }
    if (text[i + 1] == '|') {
      ++i;
      continue;
    }
    if (!std::isdigit(static_cast<unsigned char>(text[i + 1]))) {
      continue;
    }

    int value = 0;
    std::size_t cursor = i + 1;
    while (cursor < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[cursor]))) {
      value = value * 10 + (text[cursor] - '0');
      ++cursor;
    }
    if (value >= 1 && value <= 4) {
      return value;
    }
  }
  return std::nullopt;
}

}

Localization& Localization::Get() {
  static Localization instance;
  return instance;
}

static const char* kLocaleNames[] = {
    "enUS", "koKR", "frFR", "deDE", "zhCN", "zhTW",
    "esES", "esMX", "ruRU", "jaJP", "ptBR", "itIT",
};

void Localization::SetLocale(Locale locale) {
  std::lock_guard<std::mutex> lock(mutex_);
  locale_ = locale;
}

Locale Localization::GetLocale() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return locale_;
}

std::string Localization::GetLocaleName() const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto idx = static_cast<std::uint8_t>(locale_);
  if (idx < static_cast<std::uint8_t>(Locale::NumLocales)) {
    return kLocaleNames[idx];
  }
  return "Unknown";
}

std::uint8_t Localization::GetLocaleIndex() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<std::uint8_t>(locale_);
}

std::string ResolveLocalizedGlobalString(lua_State* state,
                                         std::string_view token,
                                         const int ordinal,
                                         const int gender) {
  const Locale locale = Localization::Get().GetLocale();
  const std::string token_string(token);
  const std::string plural_suffix = BuildPluralVariantSuffix(locale, ordinal);
  const std::string gender_suffix = (gender == 3) ? "_FEMALE" : "";

  const std::array<std::string, 4> lookup_order = {
      token_string + plural_suffix + gender_suffix,
      token_string + plural_suffix,
      token_string + gender_suffix,
      token_string,
  };

  for (const std::string& candidate : lookup_order) {
    if (const auto value = TryGetLuaGlobalString(state, candidate)) {
      return *value;
    }
  }

  return {};
}

void SyncLocalizedGlobalStrings(lua_State* state) {
  if (state == nullptr) {
    return;
  }

  const int stack_top = lua_gettop(state);
  std::unordered_map<std::string, std::string> strings;
  lua_pushvalue(state, LUA_GLOBALSINDEX);
  lua_pushnil(state);
  while (lua_next(state, -2) != 0) {
    if (lua_type(state, -2) == LUA_TSTRING &&
        lua_type(state, -1) == LUA_TSTRING) {
      std::size_t key_size = 0;
      std::size_t value_size = 0;
      const char* key = lua_tolstring(state, -2, &key_size);
      const char* value = lua_tolstring(state, -1, &value_size);
      if (key != nullptr && key_size != 0 && value != nullptr) {
        strings.insert_or_assign(std::string(key, key_size),
                                 std::string(value, value_size));
      }
    }
    lua_pop(state, 1);
  }
  lua_settop(state, stack_top);
  Localization::Get().MergeStrings(std::move(strings));
}

void Localization::SetString(const std::string& key,
                             const std::string& value) {
  std::lock_guard<std::mutex> lock(mutex_);
  strings_[key] = value;
}

void Localization::MergeStrings(
    std::unordered_map<std::string, std::string> strings) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [key, value] : strings) {
    strings_.insert_or_assign(std::move(key), std::move(value));
  }
}

std::string Localization::GetString(const std::string& key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = strings_.find(key);
  if (it != strings_.end()) return it->second;
  return key;
}

std::string Localization::GetString(const std::string& key,
                                    const std::string& defaultValue) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = strings_.find(key);
  if (it != strings_.end()) return it->second;
  return defaultValue;
}

bool Localization::HasString(const std::string& key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return strings_.find(key) != strings_.end();
}

std::string ExpandLocalizedTextTags(std::string_view text, Locale locale) {
  std::string current = ClampClientTextBytes(text);

  while (true) {
    const std::optional<int> tag = FindNextLocalizedTag(current);
    if (!tag.has_value()) {
      return current;
    }

    switch (*tag) {
      case 1:
        current = ApplyKoreanParticleTag(current);
        break;
      case 2:
        current = ApplyFrenchContractionTag(current);
        break;
      case 3:
        current = ApplyDeclinedNameTag(current, locale);
        break;
      case 4:
        current = ApplyPluralTag(current, locale);
        break;
      default:
        return current;
    }
  }
}

std::string Localization::FormatString(
    const std::string& format, const std::vector<std::string>& args) const {
  std::string result;
  result.reserve(format.size() * 2);

  std::size_t arg_idx = 0;
  std::size_t i = 0;

  while (i < format.size()) {

    if (format[i] == '%' && i + 1 < format.size()) {
      ++i;

      if (format[i] == '%') {
        result += '%';
        ++i;
        continue;
      }

      std::size_t pos_arg = arg_idx;
      bool positional = false;
      if (std::isdigit(static_cast<unsigned char>(format[i]))) {
        std::size_t num_start = i;
        while (i < format.size() &&
               std::isdigit(static_cast<unsigned char>(format[i]))) {
          ++i;
        }
        if (i < format.size() && format[i] == '$') {

          pos_arg = static_cast<std::size_t>(
                        std::stoul(format.substr(num_start, i - num_start))) -
                    1;
          positional = true;
          ++i;
        } else {

          i = num_start;
        }
      }

      bool zero_pad = false;
      int field_width = 0;
      bool parsing_width = true;
      while (i < format.size() &&
             (format[i] == '-' || format[i] == '+' || format[i] == ' ' ||
              format[i] == '0' ||
              std::isdigit(static_cast<unsigned char>(format[i])) ||
              format[i] == '.')) {
        if (parsing_width && format[i] == '0' && field_width == 0) {
          zero_pad = true;
        } else if (parsing_width &&
                   std::isdigit(static_cast<unsigned char>(format[i]))) {
          field_width = field_width * 10 + (format[i] - '0');
        } else if (format[i] == '.') {
          parsing_width = false;
        }
        ++i;
      }

      if (i >= format.size()) break;

      char spec = format[i++];
      std::size_t effective_arg = positional ? pos_arg : arg_idx;
      if (!positional) ++arg_idx;

      if (effective_arg < args.size()) {
        const auto& arg_val = args[effective_arg];
        switch (spec) {
          case 'd':
          case 'i': {
            int val = 0;
            auto [ptr, ec] =
                std::from_chars(arg_val.data(), arg_val.data() + arg_val.size(), val);
            if (ec == std::errc() && field_width > 0) {
              char buffer[64];
              std::snprintf(buffer, sizeof(buffer),
                            zero_pad ? "%0*d" : "%*d", field_width, val);
              result += buffer;
            } else {
              result += arg_val;
            }
            break;
          }
          case 'f': {
            result += arg_val;
            break;
          }
          case 's':
          default:
            result += arg_val;
            break;
        }
      }
      continue;
    }

    result += format[i++];
  }

  return ExpandLocalizedTextTags(result, GetLocale());
}

std::string Localization::WrapColor(const std::string& text,
                                    const std::string& colorHex) {

  return "|c" + colorHex + text + "|r";
}

std::string Localization::StripColors(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {
    if (text[i] == '|' && i + 1 < text.size()) {
      char next = text[i + 1];
      if (next == 'c' || next == 'C') {

        i += 10;
        continue;
      }
      if (next == 'r' || next == 'R') {

        i += 2;
        continue;
      }
    }
    result += text[i++];
  }
  return result;
}

std::string Localization::StripHyperlinks(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {

    if (text[i] == '|' && i + 1 < text.size() &&
        (text[i + 1] == 'H' || text[i + 1] == 'h')) {
      if (text[i + 1] == 'H') {

        i += 2;
        while (i < text.size()) {
          if (text[i] == '|' && i + 1 < text.size() && text[i + 1] == 'h') {
            i += 2;
            break;
          }
          ++i;
        }
        continue;
      }
      if (text[i + 1] == 'h') {

        i += 2;
        continue;
      }
    }
    result += text[i++];
  }
  return result;
}

void Localization::LoadGlobalStrings() {
  std::lock_guard<std::mutex> lock(mutex_);

  strings_["OKAY"] = "Okay";
  strings_["CANCEL"] = "Cancel";
  strings_["ACCEPT"] = "Accept";
  strings_["DECLINE"] = "Decline";
  strings_["YES"] = "Yes";
  strings_["NO"] = "No";
  strings_["UNKNOWN"] = "Unknown";
  strings_["NONE"] = "None";
  strings_["CLOSE"] = "Close";
  strings_["DELETE"] = "Delete";
  strings_["QUIT"] = "Quit";
  strings_["CONTINUE"] = "Continue";
  strings_["LEVEL"] = "Level";
  strings_["NAME"] = "Name";
  strings_["CLASS"] = "Class";
  strings_["RACE"] = "Race";
  strings_["HEALTH"] = "Health";
  strings_["MANA"] = "Mana";
  strings_["ENERGY"] = "Energy";
  strings_["RAGE"] = "Rage";
  strings_["STRENGTH"] = "Strength";
  strings_["AGILITY"] = "Agility";
  strings_["STAMINA"] = "Stamina";
  strings_["INTELLECT"] = "Intellect";
  strings_["SPIRIT"] = "Spirit";
  strings_["ARMOR"] = "Armor";
  strings_["ATTACK_POWER"] = "Attack Power";
  strings_["SPELL_POWER"] = "Spell Power";
  strings_["DEFENSE"] = "Defense";
  strings_["DODGE"] = "Dodge";
  strings_["PARRY"] = "Parry";
  strings_["BLOCK"] = "Block";
  strings_["HIT"] = "Hit";
  strings_["CRIT"] = "Crit";
  strings_["HASTE"] = "Haste";
  strings_["EXPERTISE"] = "Expertise";
  strings_["GOLD"] = "Gold";
  strings_["SILVER"] = "Silver";
  strings_["COPPER"] = "Copper";
  strings_["ERR_QUEST_ACCEPTED_S"] = "Quest accepted: %s";
  strings_["ERR_QUEST_COMPLETE_S"] = "Quest complete: %s";
  strings_["ERR_LEARN_SPELL_S"] = "You have learned a new spell: %s.";
  strings_["ERR_LEARN_ABILITY_S"] = "You have learned a new ability: %s.";
  strings_["ERR_MULTI_CAST_ACTION_TOTEM_S"] =
      "That spell cannot be placed in the %s slot.";
  strings_["LOOT_ITEM_SELF"] = "You receive loot: %s.";
  strings_["LOOT_ITEM"] = "%s receives loot: %s.";
  strings_["LOOT_MONEY"] = "You loot %s";
  strings_["YOU_DIED"] = "You have died.";
  strings_["CORPSE_RUNNING"] = "Release Spirit";
  strings_["AFK"] = "AFK";
  strings_["CLEARED_AFK"] = "AFK flag removed.";
  strings_["DND"] = "DND";
  strings_["CHAT_SAY_SEND"] = "%s says: %s";
  strings_["CHAT_YELL_SEND"] = "%s yells: %s";
  strings_["CHAT_WHISPER_SEND"] = "To %s: %s";
  strings_["CHAT_WHISPER_GET"] = "%s whispers: %s";
  strings_["CHAT_PARTY_SEND"] = "[Party] %s: %s";
  strings_["CHAT_RAID_SEND"] = "[Raid] %s: %s";
  strings_["CHAT_GUILD_SEND"] = "[Guild] %s: %s";
  strings_["CHAT_OFFICER_SEND"] = "[Officer] %s: %s";
  strings_["CHAT_EMOTE_SEND"] = "%s %s";
  strings_["CHAT_CHANNEL_SEND"] = "[%s] %s: %s";
  strings_["CHAT_AFK_SET"] = "You are now AFK.";
  strings_["CHAT_AFK_CLEAR"] = "You are no longer AFK.";
  strings_["CHAT_DND_SET"] = "You are now DND.";
  strings_["CHAT_DND_CLEAR"] = "You are no longer DND.";
  strings_["DAYS_ABBR"] = "%d |4Day:Days;";
  strings_["HOURS_ABBR"] = "%d |4Hour:Hours;";
  strings_["MINUTES_ABBR"] = "%d |4Minute:Minutes;";
  strings_["SECONDS_ABBR"] = "%d |4Second:Seconds;";
  strings_["TIME_UNIT_DELIMITER"] = " ";
  strings_["GUILD_MOTD_TEMPLATE"] = "Guild Message of the Day: %s";
  strings_["CHAT_YOU_CHANGED_NOTICE"] = "Changed Channel: [%s]";
  strings_["CHAT_YOU_JOINED_NOTICE"] = "Joined Channel: [%s]";
  strings_["CHAT_YOU_LEFT_NOTICE"] = "Left Channel: [%s]";
  strings_["CHAT_PLAYER_JOINED_NOTICE"] = "%s joined channel.";
  strings_["CHAT_PLAYER_LEFT_NOTICE"] = "%s left channel.";
  strings_["RANDOM_ROLL_RESULT"] = "%s rolls %d (%d-%d)";
  strings_["DAILY_QUESTS_REMAINING"] = "You have %d daily quests remaining.";
  strings_["NO_DAILY_QUESTS_REMAINING"] =
      "You have no daily quests remaining.";

  strings_["LOOT_ROLL_NEED_SELF"] = "You have selected Need for: %s";
  strings_["LOOT_ROLL_GREED_SELF"] = "You have selected Greed for: %s";
  strings_["LOOT_ROLL_DISENCHANT_SELF"] =
      "You have selected Disenchant for: %s";
  strings_["LOOT_ROLL_PASSED_SELF"] = "You passed on: %s";
  strings_["LOOT_ROLL_PASSED_SELF_AUTO"] =
      "You automatically passed on: %s because you cannot loot that item.";

  strings_["LOOT_ROLL_NEED"] = "%s has selected Need for: %s";
  strings_["LOOT_ROLL_GREED"] = "%s has selected Greed for: %s";
  strings_["LOOT_ROLL_DISENCHANT"] = "%s has selected Disenchant for: %s";
  strings_["LOOT_ROLL_PASSED"] = "%s passed on: %s";
  strings_["LOOT_ROLL_PASSED_AUTO"] = "%s automatically passed on: %s";

  strings_["LOOT_ROLL_ROLLED_NEED"] = "%s rolled Need - %d for: %s";
  strings_["LOOT_ROLL_ROLLED_GREED"] = "%s rolled Greed - %d for: %s";
  strings_["LOOT_ROLL_ROLLED_DE"] = "%s rolled Disenchant - %d for: %s";

  strings_["LOOT_ROLL_ALL_PASSED"] = "Everyone passed on: %s";
  strings_["LOOT_ROLL_YOU_WON"] = "You won: %s";
  strings_["LOOT_ROLL_WON"] = "%s won: %s";
  strings_["LOOT_ROLL_YOU_WON_NO_SPAM_NEED"] =
      "You won: %s (Need - %d)";
  strings_["LOOT_ROLL_YOU_WON_NO_SPAM_GREED"] =
      "You won: %s (Greed - %d)";
  strings_["LOOT_ROLL_YOU_WON_NO_SPAM_DE"] =
      "You won: %s (Disenchant - %d)";
  strings_["LOOT_ROLL_WON_NO_SPAM_NEED"] =
      "%s won: %s (Need - %d)";
  strings_["LOOT_ROLL_WON_NO_SPAM_GREED"] =
      "%s won: %s (Greed - %d)";
  strings_["LOOT_ROLL_WON_NO_SPAM_DE"] =
      "%s won: %s (Disenchant - %d)";
}

std::size_t Localization::GetStringCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return strings_.size();
}

void Localization::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  strings_.clear();
}

}

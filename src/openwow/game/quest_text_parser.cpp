
#include "openwow/game/quest_text_parser.h"
#include "openwow/game/localization.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace openwow::game {

namespace {

void AppendClamped(char* output, std::uint32_t size, const char* text) {
  if (!output || size == 0 || !text) {
    return;
  }

  const auto existing = static_cast<std::uint32_t>(std::strlen(output));
  if (existing >= size - 1) {
    return;
  }

  const auto remaining = size - existing - 1;
  const auto text_len = static_cast<std::uint32_t>(std::strlen(text));
  const auto copy_len = std::min(text_len, remaining);
  std::memcpy(output + existing, text, copy_len);
  output[existing + copy_len] = '\0';
}

void AppendWorldStateCountdownText(char* output, std::uint32_t size,
                                   std::int32_t end_time_seconds,
                                   std::int32_t current_time_seconds) {
  std::int32_t remaining = end_time_seconds - current_time_seconds;
  if (remaining < 0) {
    remaining = 0;
  }

  const auto hours = remaining / 3600;
  const auto minutes = (remaining % 3600) / 60;
  const auto seconds = remaining % 60;

  char buffer[16] = {};
  if (hours <= 0) {
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, seconds);
  }
  AppendClamped(output, size, buffer);
}

}

void QuestTextParser::AppendWorldStateCountdown(char* output,
                                                std::uint32_t size,
                                                std::int32_t end_time_seconds,
                                                std::int32_t current_time_seconds) {
  AppendWorldStateCountdownText(output, size, end_time_seconds,
                                current_time_seconds);
}

bool QuestTextParser::ParsePluralFormField(const char** cursor, char* output,
                                           std::uint32_t output_size,
                                           std::int32_t count) {
  if (!cursor || !*cursor || !output) return true;

  const char* p = *cursor;

  while (*p && *p == ' ') ++p;

  if (!*p) return true;

  const char* semi = std::strchr(p, ';');
  if (!semi) return true;

  *cursor = semi + 1;

  const auto target_index =
      static_cast<std::uint32_t>(SelectPluralFormIndex(count));

  const char* field_start = p;
  std::uint32_t skipped = 0;

  if (target_index > 0) {
    while (*field_start && field_start < semi) {
      if (*field_start == ':') {
        ++skipped;
        ++field_start;
        if (skipped >= target_index) break;
        continue;
      }

      while (*field_start && *field_start != ':' && *field_start != ';') {
        ++field_start;
      }
    }
  }

  while (*field_start == ' ') ++field_start;

  const char* field_end = field_start;
  while (*field_end && *field_end != ':' && *field_end != ';') {
    ++field_end;
  }

  auto len = static_cast<std::uint32_t>(field_end - field_start);
  while (len > 0 && field_start[len - 1] == ' ') --len;

  if (len == 0) return true;

  auto existing_len = static_cast<std::uint32_t>(std::strlen(output));
  std::uint32_t remaining = output_size - existing_len - 1;
  std::uint32_t copy_len = std::min(len, remaining);

  if (copy_len > 0) {
    std::memcpy(output + existing_len, field_start, copy_len);
    output[existing_len + copy_len] = '\0';
  }

  return true;
}

bool QuestTextParser::ExpandWorldStateFormat(const char* format, char* output,
                                            std::uint32_t size,
                                            const WorldStateValueResolver& resolve_value,
                                            std::int32_t current_time_seconds) {
  if (!format || !output || size == 0) return false;

  output[0] = '\0';
  bool all_resolved = true;
  const char* p = format;

  while (*p) {

    const char* pct = std::strchr(p, '%');
    if (!pct) {

      std::uint32_t existing = static_cast<std::uint32_t>(std::strlen(output));
      std::uint32_t remain_len = static_cast<std::uint32_t>(std::strlen(p));
      std::uint32_t copy = std::min(remain_len, size - existing - 1);
      std::memcpy(output + existing, p, copy);
      output[existing + copy] = '\0';
      break;
    }

    if (pct > p) {
      std::uint32_t existing = static_cast<std::uint32_t>(std::strlen(output));
      std::uint32_t lit_len = static_cast<std::uint32_t>(pct - p);
      std::uint32_t copy = std::min(lit_len, size - existing - 1);
      std::memcpy(output + existing, p, copy);
      output[existing + copy] = '\0';
    }

    const char* digit_start = pct + 1;
    const char* q = digit_start;
    while (*q >= '0' && *q <= '9') ++q;

    char fmt_char = *q;
    switch (fmt_char) {
      case 'W':
      case 'w': {
        std::uint32_t ws_id = static_cast<std::uint32_t>(std::atoi(digit_start));
        const auto value = resolve_value ? resolve_value(ws_id) : 0;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", value);
        AppendClamped(output, size, buf);
        p = q + 1;
        break;
      }
      case 'K':
      case 'k': {
        std::uint32_t ws_id = static_cast<std::uint32_t>(std::atoi(digit_start));
        const auto value = resolve_value ? resolve_value(ws_id) : 0;
        AppendWorldStateCountdown(output, size, value, current_time_seconds);
        p = q + 1;
        break;
      }
      default: {

        all_resolved = false;
        std::uint32_t existing = static_cast<std::uint32_t>(std::strlen(output));
        if (existing + 1 < size) {
          output[existing] = '%';
          output[existing + 1] = '\0';
        }
        p = q;
        break;
      }
    }
  }

  return all_resolved;
}

bool QuestTextParser::ExpandWorldStateFormat(const char* format, char* output,
                                            std::uint32_t size) {
  return ExpandWorldStateFormat(
      format,
      output,
      size,
      [](std::uint32_t ) { return 0; },
      0);
}

std::string QuestTextParser::ParsePluralFormField(const std::string& text,
                                                   std::int32_t count) {
  char buf[1024] = {};
  const char* cursor = text.c_str();
  ParsePluralFormField(&cursor, buf, sizeof(buf), count);
  return buf;
}

std::string QuestTextParser::ExpandWorldStateFormat(
    const std::string& format) {
  char buf[2048] = {};
  ExpandWorldStateFormat(format.c_str(), buf, sizeof(buf));
  return buf;
}

}

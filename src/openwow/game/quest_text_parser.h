#pragma once

#include <functional>
#include <cstdint>
#include <string>

namespace openwow::game {

class QuestTextParser {
 public:
  using WorldStateValueResolver = std::function<std::int32_t(std::uint32_t)>;

  static void AppendWorldStateCountdown(char* output,
                                        std::uint32_t size,
                                        std::int32_t end_time_seconds,
                                        std::int32_t current_time_seconds);

  static bool ParsePluralFormField(const char** cursor, char* output,
                                   std::uint32_t output_size,
                                   std::int32_t count);

  static bool ExpandWorldStateFormat(const char* format, char* output,
                                     std::uint32_t size,
                                     const WorldStateValueResolver& resolve_value,
                                     std::int32_t current_time_seconds);
  static bool ExpandWorldStateFormat(const char* format, char* output,
                                     std::uint32_t size);

  static std::string ParsePluralFormField(const std::string& text,
                                           std::int32_t count);
  static std::string ExpandWorldStateFormat(const std::string& format);
};

}

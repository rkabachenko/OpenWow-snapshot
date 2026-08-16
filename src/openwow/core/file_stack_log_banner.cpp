#include "openwow/core/file_stack_log_banner.h"
#include "openwow/runtime/time/game_time.h"
#include "openwow/core/storm_path.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <limits>
#include <string>

namespace openwow::core {
namespace {

constexpr auto kGameEpochUtc =
    std::chrono::sys_days{std::chrono::year{2000} / 1 / 1};

std::string NormalizeComputerName(std::string_view computer_name) {
  if (!computer_name.empty()) {
    return std::string(computer_name);
  }
  return "<unknown>";
}

std::int64_t TimePointToTimeNsSince2000(
    const std::chrono::system_clock::time_point tp) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             tp - kGameEpochUtc)
      .count();
}

ida::CalendarTimeBreakdown DecomposeUtc(
    const std::chrono::system_clock::time_point tp) {
  return ida::CalendarTimeBreakdownFromNsSince2000(
      TimePointToTimeNsSince2000(tp));
}

std::string NormalizeForwardSlashPath(std::string_view path,
                                      const std::size_t max_chars) {
  const std::size_t capped_length = std::min(path.size(), max_chars);
  std::string normalized;
  normalized.reserve(capped_length);
  for (std::size_t index = 0; index < capped_length; ++index) {
    normalized.push_back(path[index] == '\\' ? '/' : path[index]);
  }
  return normalized;
}

int ClampCapacityToInt(const std::size_t capacity) {
  return static_cast<int>(
      std::min(capacity,
               static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

bool CanResolvePath(const FileStackLogPathOptions& options, const char* path) {
  return options.can_resolve_path && path && *path != '\0'
      && options.can_resolve_path(path);
}

std::array<char, 512> BuildFileStackBaseRoot(
    const FileStackLogPathOptions& options, const char* normalized_directory) {
  const std::string default_root =
      NormalizeForwardSlashPath(options.default_root,
                                std::numeric_limits<std::size_t>::max());
  const std::string descriptor_root =
      NormalizeForwardSlashPath(options.descriptor_root,
                                std::numeric_limits<std::size_t>::max());

  std::array<char, 512> base_path{};
  if (normalized_directory
      && CanResolvePath(options, normalized_directory)) {
    return base_path;
  }

  if (default_root.empty()) {
    if (!descriptor_root.empty()) {
      CopyStormPath(base_path.data(), descriptor_root.c_str(),
                    ClampCapacityToInt(base_path.size()));
    }
    return base_path;
  }

  if (descriptor_root.empty()) {
    CopyStormPath(base_path.data(), default_root.c_str(),
                  ClampCapacityToInt(base_path.size()));
    return base_path;
  }

  if (CanResolvePath(options, default_root.c_str())
      && !CanResolvePath(options, descriptor_root.c_str())) {
    CopyStormPath(base_path.data(), default_root.c_str(),
                  ClampCapacityToInt(base_path.size()));
    AppendStormPath(base_path.data(), descriptor_root.c_str(),
                    ClampCapacityToInt(base_path.size()));
    return base_path;
  }

  CopyStormPath(base_path.data(), descriptor_root.c_str(),
                ClampCapacityToInt(base_path.size()));
  return base_path;
}

void AppendFormattedFileStackToken(char* cursor, const std::size_t capacity,
                                   const ida::CalendarTimeBreakdown& ts,
                                   const char token) {
  if (!cursor || capacity == 0) {
    return;
  }

  if (token == 'T') {
    std::snprintf(cursor, capacity, "%02d%02d%02d-%02d%02d%02d", ts.year % 100,
                  ts.month, ts.day, ts.hour, ts.minute, ts.second);
    return;
  }

  std::snprintf(cursor, capacity, "%02d%02d%02d", ts.year % 100, ts.month,
                ts.day);
}

}

bool BuildFileStackLogPath(std::string_view logical_path, char* output,
                           const std::size_t output_capacity,
                           const bool create_directory,
                           const FileStackLogPathOptions& options) {
  if (!output || output_capacity == 0) {
    return false;
  }

  output[0] = '\0';

  const std::string normalized_path =
      NormalizeForwardSlashPath(logical_path, 1023);
  const std::size_t separator = normalized_path.find_last_of('/');
  const bool has_directory = separator != std::string::npos;
  const std::string directory =
      has_directory ? normalized_path.substr(0, separator) : std::string{};
  const std::string leaf =
      has_directory ? normalized_path.substr(separator + 1) : normalized_path;
  const char* const normalized_directory =
      has_directory ? directory.c_str() : nullptr;
  const auto base_root =
      BuildFileStackBaseRoot(options, normalized_directory);

  const int output_capacity_int = ClampCapacityToInt(output_capacity);
  char* cursor = output + CopyStormPath(output, base_root.data(),
                                        output_capacity_int);

  if (has_directory) {
    const std::size_t used = static_cast<std::size_t>(cursor - output);
    const int remaining_capacity = ClampCapacityToInt(output_capacity - used);
    cursor += CopyStormPath(cursor, directory.c_str(), remaining_capacity);
    if (static_cast<std::size_t>(cursor - output) < output_capacity - 1) {
      *cursor++ = '/';
    }
  }
  *cursor = '\0';

  if (create_directory && options.create_directory) {
    (void)options.create_directory(output, true);
  }

  const auto ts = DecomposeUtc(options.when_utc);
  for (std::size_t index = 0; index < leaf.size();) {
    const std::ptrdiff_t used = cursor - output;
    if (used >= static_cast<std::ptrdiff_t>(output_capacity) - 20) {
      break;
    }

    const char value = leaf[index];
    if (value != '%') {
      *cursor++ = value;
      ++index;
      continue;
    }

    if (index + 1 >= leaf.size()) {
      break;
    }

    const char token = leaf[index + 1];
    index += 2;
    if (token == '%') {
      *cursor++ = '%';
      continue;
    }

    if (token == 'T' || token == 'Y') {
      const std::size_t remaining_capacity =
          output_capacity - static_cast<std::size_t>(cursor - output);
      AppendFormattedFileStackToken(cursor, remaining_capacity, ts, token);
      cursor += std::strlen(cursor);
    }
  }

  *cursor = '\0';
  NormalizePathToBackslashes(output, output, output_capacity_int);
  return true;
}

std::string BuildFileStackStartupBanner(std::chrono::system_clock::time_point start_time_utc,
                                        std::string_view computer_name) {
  const auto ts = DecomposeUtc(start_time_utc);
  const auto host = NormalizeComputerName(computer_name);
  const auto hundred_microseconds = ts.nanoseconds / 100000;

  char buffer[4096];
  std::snprintf(buffer, sizeof(buffer),
                "\n"
                "#-----------------------------------------------------------\n"
                "# System started at %04d-%02d-%02d %02d:%02d:%02d.%04d\n"
                "# system: %s\n"
                "#-----------------------------------------------------------\n",
                ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second, hundred_microseconds,
                host.c_str());
  return buffer;
}

std::string BuildFileStackTimestampPrefix(std::chrono::system_clock::time_point when_utc) {
  const auto ts = DecomposeUtc(when_utc);
  const auto hundred_microseconds = ts.nanoseconds / 100000;

  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%04d ", ts.hour, ts.minute, ts.second,
                hundred_microseconds);
  return buffer;
}

std::string BuildFileStackReopenBanner(std::chrono::system_clock::time_point when_utc,
                                       std::string_view computer_name) {
  const auto ts = DecomposeUtc(when_utc);
  const auto host = NormalizeComputerName(computer_name);

  char buffer[4096];
  std::snprintf(buffer, sizeof(buffer),
                "\n"
                "#-----------------------------------------------------------\n"
                "# %04d-%02d-%02d\n"
                "# system: %s\n"
                "#-----------------------------------------------------------\n",
                ts.year, ts.month, ts.day, host.c_str());
  return buffer;
}

}

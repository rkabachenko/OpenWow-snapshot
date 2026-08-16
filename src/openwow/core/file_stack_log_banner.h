#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace openwow::core {

struct FileStackLogPathOptions {
  std::chrono::system_clock::time_point when_utc{};
  std::string_view default_root;
  std::string_view descriptor_root;
  std::function<bool(const char*)> can_resolve_path;
  std::function<bool(const char*, bool)> create_directory;
};

[[nodiscard]] bool BuildFileStackLogPath(std::string_view logical_path,
                                         char* output,
                                         std::size_t output_capacity,
                                         bool create_directory,
                                         const FileStackLogPathOptions& options);

[[nodiscard]] std::string
BuildFileStackTimestampPrefix(std::chrono::system_clock::time_point when_utc);

[[nodiscard]] std::string
BuildFileStackStartupBanner(std::chrono::system_clock::time_point start_time_utc,
                            std::string_view computer_name);

[[nodiscard]] std::string BuildFileStackReopenBanner(std::chrono::system_clock::time_point when_utc,
                                                     std::string_view computer_name);

}

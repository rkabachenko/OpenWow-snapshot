#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace openwow::runtime::bootstrap {

struct StartupTraceEvent {
  std::string phase;
  std::string label;
};

class StartupTrace {
 public:
  void Add(std::string_view phase) { Add(phase, ""); }

  void Add(std::string_view phase, std::string_view label) {
    events_.push_back(StartupTraceEvent{
        .phase = std::string(phase),
        .label = std::string(label),
    });
  }

  [[nodiscard]] const std::vector<StartupTraceEvent>& events() const { return events_; }

  [[nodiscard]] std::string SerializeTsv() const;

  [[nodiscard]] std::error_code WriteTsvFile(
      const std::filesystem::path& path) const;

 private:
  static void AppendEscaped(std::string& out, std::string_view value);

  std::vector<StartupTraceEvent> events_;
};

}

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::media {

struct SubtitleEntry {
  std::uint32_t start_ms{0};
  std::uint32_t end_ms{0};
  std::string text;
};

class SubtitleTrack {
 public:

  static std::optional<SubtitleTrack> Parse(
      const std::vector<std::uint8_t>& data);

  static std::optional<SubtitleTrack> Parse(const std::string& text);

  [[nodiscard]] const std::vector<SubtitleEntry>& entries() const noexcept {
    return entries_;
  }

  [[nodiscard]] int FindActiveAt(std::uint32_t time_ms) const noexcept;

  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

 private:
  std::vector<SubtitleEntry> entries_;
};

}


#include "openwow/media/subtitles/subtitle_parser.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string_view>

namespace openwow::media {

namespace {

int ParseInt(std::string_view& sv) {

  while (!sv.empty() && !std::isdigit(static_cast<unsigned char>(sv.front())))
    sv.remove_prefix(1);
  if (sv.empty()) return 0;

  int val = 0;
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
  if (ec != std::errc{}) return 0;
  sv.remove_prefix(static_cast<std::size_t>(ptr - sv.data()));
  return val;
}

std::uint32_t ParseTimecodeMs(std::string_view tc) {
  int hours = ParseInt(tc);
  int minutes = ParseInt(tc);
  int seconds = ParseInt(tc);
  int centiseconds = ParseInt(tc);

  return static_cast<std::uint32_t>(
      ((60 * hours + minutes) * 60 + seconds) * 1000 + 41 * centiseconds);
}

}

std::optional<SubtitleTrack> SubtitleTrack::Parse(
    const std::vector<std::uint8_t>& data) {
  if (data.empty()) return std::nullopt;

  std::size_t offset = 0;
  if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB &&
      data[2] == 0xBF) {
    offset = 3;
  }

  std::string text(reinterpret_cast<const char*>(data.data() + offset),
                   data.size() - offset);
  return Parse(text);
}

std::optional<SubtitleTrack> SubtitleTrack::Parse(const std::string& text) {
  if (text.empty()) return std::nullopt;

  SubtitleTrack track;
  std::istringstream stream(text);
  std::string line;

  while (std::getline(stream, line)) {

    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;

    if (line[0] != '0') continue;

    std::string_view lv(line);

    auto start_ms = ParseTimecodeMs(lv);

    auto end_ms = ParseTimecodeMs(lv);

    std::string subtitle_text;
    if (!std::getline(stream, subtitle_text)) break;
    if (!subtitle_text.empty() && subtitle_text.back() == '\r')
      subtitle_text.pop_back();

    if (end_ms > start_ms) {
      track.entries_.push_back(
          {start_ms, end_ms, std::move(subtitle_text)});
    }
  }

  std::sort(track.entries_.begin(), track.entries_.end(),
            [](const SubtitleEntry& a, const SubtitleEntry& b) {
              return a.start_ms < b.start_ms;
            });

  if (track.entries_.empty()) return std::nullopt;
  return track;
}

int SubtitleTrack::FindActiveAt(std::uint32_t time_ms) const noexcept {

  for (std::size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].start_ms <= time_ms && entries_[i].end_ms > time_ms) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}

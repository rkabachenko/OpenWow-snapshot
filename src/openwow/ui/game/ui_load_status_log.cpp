#include "openwow/ui/game/ui_load_status_log.h"

#include <algorithm>
#include <utility>

namespace openwow::ui::game {

WoWClientLogFile::WoWClientLogFile(std::string display_path, WoWClientLogOpenMode mode)
    : log_(std::move(display_path),
           mode == WoWClientLogOpenMode::kAppend
               ? openwow::core::LegacyBufferedLogOpenMode::kAppend
               : openwow::core::LegacyBufferedLogOpenMode::kTruncate) {}

WoWClientLogFile::~WoWClientLogFile() {
  Flush();
}

void WoWClientLogFile::AppendStatus(std::intptr_t code, std::string_view message) {
  entries_.AppendStatus(code, message);
}

void WoWClientLogFile::AppendEntries(const std::vector<UiLoadStatusEntry> &entries) {
  for (const auto &entry : entries) {
    entries_.AppendStatus(entry.code, entry.message);
  }
}

void WoWClientLogFile::AppendBuffer(const UiLoadStatusBuffer &buffer) {
  entries_.AppendFrom(buffer);
}

void WoWClientLogFile::Flush() {
  if (entries_.empty()) {
    return;
  }

  for (const auto &entry : entries_.entries()) {
    log_.AppendLine(entry.message);
  }
  log_.FlushPending();
  entries_.Clear();
}

bool WoWClientLogFile::has_entries() const {
  return !entries_.empty();
}

bool WoWClientLogFile::is_open() const {
  return log_.IsOpen();
}

std::string ToWoWClientLogPath(std::string_view path) {
  std::string normalized(path);
  std::replace(normalized.begin(), normalized.end(), '/', '\\');
  while (!normalized.empty() && normalized.front() == '\\') {
    normalized.erase(normalized.begin());
  }
  return normalized;
}

void AppendUiLoadEntries(UiLoadStatusSink *sink, const std::vector<UiLoadStatusEntry> &entries) {
  if (sink == nullptr) {
    return;
  }
  for (const auto &entry : entries) {
    sink->AppendStatus(entry.code, entry.message);
  }
}

}

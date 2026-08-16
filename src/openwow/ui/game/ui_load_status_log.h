#pragma once

#include "openwow/core/legacy_buffered_log_file.h"
#include "openwow/ui/game/ui_load_status.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::game {

enum class WoWClientLogOpenMode {
  kTruncate,
  kAppend,
};

class WoWClientLogFile final : public UiLoadStatusSink {
public:
  WoWClientLogFile(std::string display_path, WoWClientLogOpenMode mode);
  ~WoWClientLogFile() override;

  WoWClientLogFile(const WoWClientLogFile &) = delete;
  WoWClientLogFile &operator=(const WoWClientLogFile &) = delete;

  void AppendStatus(std::intptr_t code, std::string_view message) override;
  void AppendEntries(const std::vector<UiLoadStatusEntry> &entries);
  void AppendBuffer(const UiLoadStatusBuffer &buffer);
  void Flush();

  [[nodiscard]] bool has_entries() const;
  [[nodiscard]] bool is_open() const;

private:
  UiLoadStatusBuffer entries_;
  openwow::core::LegacyBufferedLogFile log_;
};

std::string ToWoWClientLogPath(std::string_view path);
void AppendUiLoadEntries(UiLoadStatusSink *sink, const std::vector<UiLoadStatusEntry> &entries);

}

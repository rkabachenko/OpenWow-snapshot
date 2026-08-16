#include "openwow/game/client_text_log_files.h"

#include "openwow/core/legacy_buffered_log_file.h"

#include <array>
#include <mutex>
#include <string_view>

namespace openwow::game {
namespace {

struct ClientTextLogState {
  const char* relative_path = nullptr;
  bool enabled = false;
  openwow::core::LegacyBufferedLogFile file;
};

class ClientTextLogFiles {
public:
  static ClientTextLogFiles& Get() {
    static ClientTextLogFiles instance;
    return instance;
  }

  bool SetEnabled(const ClientTextLogKind kind, const bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    ClientTextLogState& state = StateFor(kind);
    state.enabled = enabled;
    if (!enabled) {
      return false;
    }
    if (!EnsureOpen(state)) {
      state.enabled = false;
    }
    return state.enabled;
  }

  bool IsEnabled(const ClientTextLogKind kind) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return StateFor(kind).enabled;
  }

  void AppendLine(const ClientTextLogKind kind, const std::string_view line) {
    std::lock_guard<std::mutex> lock(mutex_);
    ClientTextLogState& state = StateFor(kind);
    if (!state.enabled || !EnsureOpen(state)) {
      return;
    }
    state.file.AppendLine(line);
  }

  void Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (ClientTextLogState& state : states_) {
      state.enabled = false;
      state.file.Close();
    }
  }

private:
  ClientTextLogFiles() {
    states_[0].relative_path = "Logs\\WoWChatLog.txt";
    states_[1].relative_path = "Logs\\WoWCombatLog.txt";
  }

  ClientTextLogState& StateFor(const ClientTextLogKind kind) {
    return states_[static_cast<std::size_t>(kind)];
  }

  const ClientTextLogState& StateFor(const ClientTextLogKind kind) const {
    return states_[static_cast<std::size_t>(kind)];
  }

  static bool EnsureOpen(ClientTextLogState& state) {
    if (state.file.IsOpen()) {
      return true;
    }
    return state.file.Open(state.relative_path,
                           openwow::core::LegacyBufferedLogOpenMode::kAppend);
  }

  mutable std::mutex mutex_;
  std::array<ClientTextLogState, 2> states_{};
};

}

bool SetClientTextLogEnabled(const ClientTextLogKind kind, const bool enabled) {
  return ClientTextLogFiles::Get().SetEnabled(kind, enabled);
}

bool IsClientTextLogEnabled(const ClientTextLogKind kind) {
  return ClientTextLogFiles::Get().IsEnabled(kind);
}

void AppendClientTextLogLine(const ClientTextLogKind kind,
                             const std::string_view line) {
  ClientTextLogFiles::Get().AppendLine(kind, line);
}

void ShutdownClientTextLogs() {
  ClientTextLogFiles::Get().Shutdown();
}

void ResetClientTextLogFilesForTests() {
  ShutdownClientTextLogs();
}

}

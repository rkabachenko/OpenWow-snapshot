#pragma once

#include <string_view>

namespace openwow::game {

enum class ClientTextLogKind {
  Chat,
  Combat,
};

bool SetClientTextLogEnabled(ClientTextLogKind kind, bool enabled);
bool IsClientTextLogEnabled(ClientTextLogKind kind);
void AppendClientTextLogLine(ClientTextLogKind kind, std::string_view line);
void ShutdownClientTextLogs();

void ResetClientTextLogFilesForTests();

}

#pragma once

#include <filesystem>
#include <string>

namespace openwow::diagnostics {

enum class LogLevel {
  kTrace,
  kDebug,
  kInfo,
  kWarn,
  kError,
};

void InitLogging(const std::string& session_name, LogLevel level = LogLevel::kInfo);
void ShutdownLogging();
void Log(LogLevel level, const std::string& message);
[[nodiscard]] bool IsLogEnabled(LogLevel level);
std::filesystem::path CurrentLogFile();
LogLevel LogLevelFromEnvOr(LogLevel fallback);

}

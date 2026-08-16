#pragma once
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace openwow::debug {

enum class ErrorSeverity : std::uint8_t {
  Info    = 0,
  Warning = 1,
  Error   = 2,
  Fatal   = 3,
  Assert  = 4,
};

enum class ErrorCategory : std::uint8_t {
  General = 0,
  Runtime,
  Data,
  Network,
  Graphics,
  Audio,
  UserInterface,
};

[[nodiscard]] constexpr const char* ErrorSeverityToString(ErrorSeverity s) noexcept {
  switch (s) {
    case ErrorSeverity::Info:    return "INFO";
    case ErrorSeverity::Warning: return "WARNING";
    case ErrorSeverity::Error:   return "ERROR";
    case ErrorSeverity::Fatal:   return "FATAL";
    case ErrorSeverity::Assert:  return "ASSERT";
  }
  return "UNKNOWN";
}

struct ErrorRecord {
  ErrorSeverity severity = ErrorSeverity::Info;
  ErrorCategory category = ErrorCategory::General;
  std::string message;
  std::string filename;
  std::uint32_t line = 0;
  std::chrono::steady_clock::time_point timestamp;
  std::string stack_trace;
  std::uint32_t count = 1;
};

using CrashHandler = std::function<void(const ErrorRecord& error)>;
using AssertHandler = std::function<void(const ErrorRecord& error)>;
using ErrorForEachCallback = std::function<void(const ErrorRecord& error)>;

class ErrorHandler {
 public:
  static ErrorHandler& Get();

  void Report(ErrorSeverity severity, const std::string& message,
              const std::string& file = "", std::uint32_t line = 0,
              ErrorCategory category = ErrorCategory::General);

  template <typename... Args>
  void ReportF(ErrorSeverity severity, const char* fmt, Args&&... args) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf), fmt, std::forward<Args>(args)...);
    Report(severity, buf);
  }

  [[nodiscard]] std::vector<ErrorRecord> GetErrors() const;
  [[nodiscard]] std::uint32_t GetErrorCount(ErrorSeverity severity) const;
  [[nodiscard]] std::uint32_t GetTotalErrorCount() const;
  void ClearErrors();

  void SetLogToFile(bool enable);
  [[nodiscard]] bool IsLoggingToFile() const;
  void SetLogPath(const std::string& path);

  void SetAssertLogPath(const std::string& path);
  [[nodiscard]] std::string GetAssertLogPath() const;

  void SetMaxErrors(std::uint32_t max);

  [[nodiscard]] bool HasFatalError() const;
  [[nodiscard]] std::optional<ErrorRecord> GetLastFatalError() const;

  void SetCrashHandler(CrashHandler handler);
  void SetAssertHandler(AssertHandler handler);

  void ForEach(ErrorForEachCallback callback) const;

  void Reset();

 private:
  ErrorHandler() = default;

  mutable std::mutex mutex_;
  mutable std::mutex output_mutex_;
  std::deque<ErrorRecord> errors_;
  std::uint32_t max_errors_ = 1000;
  bool log_to_file_ = false;
  std::string log_path_;
  std::string assert_log_path_;
  CrashHandler crash_handler_;
  AssertHandler assert_handler_;
  std::optional<ErrorRecord> last_fatal_;
};

#define OPENWOW_ERROR(msg) \
  ::openwow::debug::ErrorHandler::Get().Report( \
      ::openwow::debug::ErrorSeverity::Error, (msg), __FILE__, __LINE__)

#define OPENWOW_WARNING(msg) \
  ::openwow::debug::ErrorHandler::Get().Report( \
      ::openwow::debug::ErrorSeverity::Warning, (msg), __FILE__, __LINE__)

#define OPENWOW_ASSERT(cond)                                  \
  do {                                                        \
    if (!(cond)) {                                            \
      ::openwow::debug::ErrorHandler::Get().Report(           \
          ::openwow::debug::ErrorSeverity::Assert,            \
          "Assertion failed: " #cond, __FILE__, __LINE__);    \
    }                                                         \
  } while (false)

}

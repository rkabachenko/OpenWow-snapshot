#include "openwow/debug/diagnostics/error_handler.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/runtime/time/game_time.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace openwow::debug {

namespace {

constexpr std::size_t kMaximumMessageLength = 16 * 1024;
constexpr std::size_t kMaximumFilenameLength = 4 * 1024;
constexpr std::uint32_t kMaximumErrorRecords = 5000;

std::string FormatAssertTimestampUtc() {
  const auto fields = openwow::core::ida::CalendarTimeBreakdownFromNsSince2000(
      openwow::core::GameClock::GetCurrentTimeNsSince2000());

  char ts[64];
  std::snprintf(ts, sizeof(ts), "%04d/%02d/%02d %02d:%02d:%02d",
                fields.year,
                fields.month,
                fields.day,
                fields.hour,
                fields.minute,
                fields.second);
  return ts;
}

}

ErrorHandler& ErrorHandler::Get() {
  static ErrorHandler instance;
  return instance;
}

void ErrorHandler::Report(ErrorSeverity severity, const std::string& message,
                          const std::string& file, std::uint32_t line,
                          ErrorCategory category) {
  const auto now = std::chrono::steady_clock::now();
  const std::string bounded_message = message.substr(0, kMaximumMessageLength);
  const std::string bounded_file = file.substr(0, kMaximumFilenameLength);
  ErrorRecord record;
  CrashHandler callback;
  bool log_to_file = false;
  std::string log_path;
  std::string assert_log_path;

  {
    std::lock_guard lock(mutex_);
    const auto duplicate = std::find_if(
        errors_.begin(), errors_.end(), [&](const ErrorRecord& existing) {
          return existing.severity == severity && existing.category == category &&
                 existing.message == bounded_message &&
                 existing.filename == bounded_file &&
                 existing.line == line;
        });
    if (duplicate != errors_.end()) {
      record = std::move(*duplicate);
      errors_.erase(duplicate);
      if (record.count != std::numeric_limits<std::uint32_t>::max()) {
        ++record.count;
      }
      record.timestamp = now;
      errors_.push_back(record);
      if (severity == ErrorSeverity::Fatal || severity == ErrorSeverity::Assert) {
        last_fatal_ = record;
      }
      return;
    }

    record.severity = severity;
    record.category = category;
    record.message = bounded_message;
    record.filename = bounded_file;
    record.line = line;
    record.timestamp = now;

    if (errors_.size() == max_errors_) {
      errors_.pop_front();
    }
    errors_.push_back(record);

    if (severity == ErrorSeverity::Fatal || severity == ErrorSeverity::Assert) {
      last_fatal_ = record;
    }
    if (severity == ErrorSeverity::Fatal) {
      callback = crash_handler_;
    } else if (severity == ErrorSeverity::Assert) {
      callback = assert_handler_;
    }
    log_to_file = log_to_file_;
    log_path = log_path_;
    assert_log_path = assert_log_path_;
  }

  {
    std::lock_guard output_lock(output_mutex_);
    if (log_to_file && !log_path.empty() && severity != ErrorSeverity::Assert) {
      std::ofstream output(log_path, std::ios::app);
      if (output) {
        output << "[" << ErrorSeverityToString(severity) << "] " << record.message;
        if (!record.filename.empty()) {
          output << " (" << record.filename << ":" << line << ")";
        }
        output << '\n';
      }
    }

    if (severity == ErrorSeverity::Assert) {
      const std::string timestamp = FormatAssertTimestampUtc();

      std::string assert_line;
      assert_line.reserve(256);
      assert_line += timestamp;
      assert_line += "  ";
      if (!record.filename.empty()) {
        assert_line += record.filename;
        assert_line += "(";
        assert_line += std::to_string(line);
        assert_line += ")";
      }
      assert_line += " : Assertion failed: ";
      assert_line += record.message;
      assert_line += "\n";

      std::cerr << assert_line;

      if (!assert_log_path.empty()) {
        const auto parent = std::filesystem::path(assert_log_path).parent_path();
        if (!parent.empty()) {
          std::filesystem::create_directories(parent);
        }
        std::ofstream output(assert_log_path, std::ios::app);
        if (output) {
          output << assert_line;
        }
      }
    }
  }

  if (callback) {
    callback(record);
  }
}

std::vector<ErrorRecord> ErrorHandler::GetErrors() const {
  std::lock_guard lock(mutex_);
  return {errors_.begin(), errors_.end()};
}

std::uint32_t ErrorHandler::GetErrorCount(ErrorSeverity severity) const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(
      std::count_if(errors_.begin(), errors_.end(),
                    [severity](const ErrorRecord& r) {
                      return r.severity == severity;
                    }));
}

std::uint32_t ErrorHandler::GetTotalErrorCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(errors_.size());
}

void ErrorHandler::ClearErrors() {
  std::lock_guard lock(mutex_);
  errors_.clear();
  last_fatal_.reset();
}

void ErrorHandler::SetLogToFile(bool enable) {
  std::lock_guard lock(mutex_);
  log_to_file_ = enable;
}

bool ErrorHandler::IsLoggingToFile() const {
  std::lock_guard lock(mutex_);
  return log_to_file_;
}

void ErrorHandler::SetLogPath(const std::string& path) {
  std::lock_guard lock(mutex_);
  log_path_ = path;
}

void ErrorHandler::SetAssertLogPath(const std::string& path) {
  std::lock_guard lock(mutex_);
  assert_log_path_ = path;
}

std::string ErrorHandler::GetAssertLogPath() const {
  std::lock_guard lock(mutex_);
  return assert_log_path_;
}

void ErrorHandler::SetMaxErrors(std::uint32_t max) {
  std::lock_guard lock(mutex_);
  max_errors_ = std::clamp<std::uint32_t>(max, 1, kMaximumErrorRecords);
  while (errors_.size() > max_errors_) {
    errors_.pop_front();
  }
}

bool ErrorHandler::HasFatalError() const {
  std::lock_guard lock(mutex_);
  return last_fatal_.has_value();
}

std::optional<ErrorRecord> ErrorHandler::GetLastFatalError() const {
  std::lock_guard lock(mutex_);
  return last_fatal_;
}

void ErrorHandler::SetCrashHandler(CrashHandler handler) {
  std::lock_guard lock(mutex_);
  crash_handler_ = std::move(handler);
}

void ErrorHandler::SetAssertHandler(AssertHandler handler) {
  std::lock_guard lock(mutex_);
  assert_handler_ = std::move(handler);
}

void ErrorHandler::ForEach(ErrorForEachCallback callback) const {
  const auto errors = GetErrors();
  for (const auto& rec : errors) {
    callback(rec);
  }
}

void ErrorHandler::Reset() {
  std::lock_guard lock(mutex_);
  errors_.clear();
  max_errors_ = 1000;
  log_to_file_ = false;
  log_path_.clear();
  assert_log_path_.clear();
  crash_handler_ = nullptr;
  assert_handler_ = nullptr;
  last_fatal_.reset();
}

}

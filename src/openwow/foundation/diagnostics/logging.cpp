#include "openwow/foundation/diagnostics/logging.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace openwow::diagnostics {
namespace {

struct LogEntry {
  LogLevel level{LogLevel::kInfo};
  std::string json_line;
};

struct LoggerState {
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<LogEntry> queue;
  std::thread worker;
  std::ofstream out;
  std::filesystem::path log_file;
  LogLevel min_level{LogLevel::kInfo};
  bool mirror_all{false};
  bool flush_always{false};
  bool initialized{false};
  bool stop_requested{false};
};

LoggerState& State() {
  static LoggerState state;
  struct LoggingFinalizer {
    ~LoggingFinalizer() {
      ShutdownLogging();
    }
  };
  static LoggingFinalizer finalizer;
  return state;
}

const char* LevelTag(LogLevel level) {
  switch (level) {
    case LogLevel::kTrace: return "TRACE";
    case LogLevel::kDebug: return "DEBUG";
    case LogLevel::kInfo: return "INFO";
    case LogLevel::kWarn: return "WARN";
    case LogLevel::kError: return "ERROR";
  }
  return "INFO";
}

bool ShouldMirrorToStderr(LogLevel level) {
  return level == LogLevel::kWarn || level == LogLevel::kError;
}

bool MirrorAllEnabled() {
  const char* value = std::getenv("OPENWOW_LOG_MIRROR");
  if (value == nullptr) return false;
  return std::string(value) == "1" || std::string(value) == "true";
}

bool FlushAlwaysEnabled() {
  const char* value = std::getenv("OPENWOW_LOG_FLUSH");

  if (value == nullptr) return true;
  return std::string(value) == "1" || std::string(value) == "true";
}

bool ShouldLog(LogLevel entry, LogLevel min_level) {
  return static_cast<int>(entry) >= static_cast<int>(min_level);
}

std::string EscapeJson(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 16);
  for (const char ch : input) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(ch); break;
    }
  }
  return out;
}

std::string TimestampNowUtc() {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t now_time = clock::to_time_t(now);

  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &now_time);
#else
  gmtime_r(&now_time, &tm);
#endif

  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S")
      << "." << std::setw(3) << std::setfill('0') << ms.count()
      << "Z";
  return out.str();
}

std::string BuildJsonLine(LogLevel level, const std::string& message) {
  std::ostringstream out;
  out << "{\"ts\":\"" << TimestampNowUtc() << "\""
      << ",\"level\":\"" << LevelTag(level) << "\""
      << ",\"thread\":\"" << std::hash<std::thread::id>{}(std::this_thread::get_id()) << "\""
      << ",\"msg\":\"" << EscapeJson(message) << "\"}";
  return out.str();
}

void WorkerLoop() {
  auto& state = State();
  std::unique_lock<std::mutex> lock(state.mutex);

  while (true) {
    state.cv.wait(lock, [&]() {
      return state.stop_requested || !state.queue.empty();
    });

    while (!state.queue.empty()) {
      LogEntry entry = std::move(state.queue.front());
      state.queue.pop_front();

      lock.unlock();
      if (state.out.is_open()) {
        state.out << entry.json_line << '\n';
      }
      if (state.mirror_all || ShouldMirrorToStderr(entry.level)) {
        std::cerr << entry.json_line << '\n';
      }
      if (state.out.is_open() && (state.flush_always || entry.level == LogLevel::kWarn
                                  || entry.level == LogLevel::kError)) {
        state.out.flush();
      }
      lock.lock();
    }

    if (state.stop_requested && state.queue.empty()) {
      break;
    }
  }

  if (state.out.is_open()) {
    state.out.flush();
  }
}

void EnsureInitializedLocked(const std::string& session_name, LogLevel level) {
  auto& state = State();
  if (state.initialized) {
    state.min_level = level;
    return;
  }

  std::filesystem::create_directories("logs");
  state.log_file = std::filesystem::absolute(std::filesystem::path("logs") / (session_name + ".log"));
  state.out.open(state.log_file, std::ios::out | std::ios::app);
  state.min_level = level;
  state.mirror_all = MirrorAllEnabled();
  state.flush_always = FlushAlwaysEnabled();
  state.stop_requested = false;
  state.initialized = true;
  state.worker = std::thread(WorkerLoop);

  state.queue.push_back({
      .level = LogLevel::kInfo,
      .json_line = BuildJsonLine(LogLevel::kInfo, "log-start"),
  });
  state.cv.notify_one();
}

}

LogLevel LogLevelFromEnvOr(LogLevel fallback) {
  const char* value = std::getenv("OPENWOW_LOG_LEVEL");
  if (value == nullptr) {
    return fallback;
  }
  std::string s(value);
  for (auto& ch : s) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (s == "trace") {
    return LogLevel::kTrace;
  }
  if (s == "debug") {
    return LogLevel::kDebug;
  }
  if (s == "info") {
    return LogLevel::kInfo;
  }
  if (s == "warn" || s == "warning") {
    return LogLevel::kWarn;
  }
  if (s == "error") {
    return LogLevel::kError;
  }
  return fallback;
}

void InitLogging(const std::string& session_name, LogLevel level) {
  auto& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  EnsureInitializedLocked(session_name, LogLevelFromEnvOr(level));
}

void ShutdownLogging() {
  auto& state = State();
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.initialized) {
      return;
    }
    state.stop_requested = true;
    state.cv.notify_one();
  }

  if (state.worker.joinable()) {
    state.worker.join();
  }

  std::lock_guard<std::mutex> lock(state.mutex);
  state.queue.clear();
  if (state.out.is_open()) {
    state.out.close();
  }
  state.initialized = false;
  state.stop_requested = false;
}

void Log(LogLevel level, const std::string& message) {
  auto& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (!state.initialized) {
    EnsureInitializedLocked("default", LogLevel::kInfo);
  }
  if (!ShouldLog(level, state.min_level)) {
    return;
  }
  state.queue.push_back({
      .level = level,
      .json_line = BuildJsonLine(level, message),
  });
  state.cv.notify_one();
}

bool IsLogEnabled(const LogLevel level) {
  auto& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (!state.initialized) {
    EnsureInitializedLocked("default", LogLevel::kInfo);
  }
  return ShouldLog(level, state.min_level);
}

std::filesystem::path CurrentLogFile() {
  auto& state = State();
  std::lock_guard<std::mutex> lock(state.mutex);
  return state.log_file;
}

}

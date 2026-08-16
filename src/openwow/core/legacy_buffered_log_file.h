#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>

namespace openwow::core {

enum class LegacyBufferedLogOpenMode {
  kTruncate,
  kAppend,
};

struct LegacyBufferedLogTimestampSample {
  std::uint64_t tick_milliseconds{0};
  std::chrono::system_clock::time_point wall_clock_time{};
};

class LegacyBufferedLogFile {
 public:
  using TimestampSampleProvider = std::function<LegacyBufferedLogTimestampSample()>;

  LegacyBufferedLogFile() = default;
  LegacyBufferedLogFile(std::string display_path, LegacyBufferedLogOpenMode open_mode);
  ~LegacyBufferedLogFile();

  LegacyBufferedLogFile(const LegacyBufferedLogFile&) = delete;
  LegacyBufferedLogFile& operator=(const LegacyBufferedLogFile&) = delete;

  [[nodiscard]] bool Open(std::string display_path, LegacyBufferedLogOpenMode open_mode);
  void Close();

  [[nodiscard]] bool IsOpen() const;
  [[nodiscard]] const std::string& display_path() const;

  void SetIndentSpaceCount(std::size_t indent_space_count);
  void AppendLine(std::string_view line);
  void FlushPending();

  static void FlushAll();

  static void ShutdownAll();

  static void ResetShutdownForTesting();

  static void SetSharedOpenPathPrefixForTesting(std::string path_prefix);
  static void ResetSharedOpenPathPrefixForTesting();
  static void SetTimestampSampleProviderForTesting(TimestampSampleProvider provider);
  static void ResetTimestampSampleProviderForTesting();

 private:
  static constexpr std::size_t kBufferCapacity = 0x10000;
  static constexpr std::size_t kFlushThreshold = 0xC000;
  static constexpr std::size_t kMaxIndentSpaceCount = 128;

  [[nodiscard]] bool EnsureOpen();
  [[nodiscard]] std::string TimestampPrefix();
  [[nodiscard]] static std::filesystem::path DisplayPathToFilesystemPath(
      std::string_view display_path);
  static void CreateDisplayPathDirectoriesBestEffort(std::string_view display_path);

  std::ofstream stream_;
  std::string display_path_;
  LegacyBufferedLogOpenMode open_mode_{LegacyBufferedLogOpenMode::kTruncate};
  std::string pending_bytes_;
  std::size_t indent_space_count_{0};
  bool open_enabled_{false};
};

}

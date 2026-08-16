#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::platform {

struct RingBufferEntry {
  std::chrono::steady_clock::time_point timestamp;
  std::string message;
  std::uint8_t severity{0};
};

class ErrorRingBuffer {
 public:
  static constexpr std::size_t kDefaultCapacity = 256;

  explicit ErrorRingBuffer(std::size_t capacity = kDefaultCapacity);

  void Push(std::string_view message, std::uint8_t severity);

  [[nodiscard]] std::vector<RingBufferEntry> Snapshot() const;

  [[nodiscard]] std::size_t Size() const;

  void Clear();

  [[nodiscard]] std::size_t Capacity() const { return capacity_; }

 private:
  mutable std::mutex mutex_;
  std::vector<RingBufferEntry> buffer_;
  std::size_t head_{0};
  std::size_t count_{0};
  std::size_t capacity_;
};

struct CrashContext {
  std::string build_version;
  std::string gpu_info;
  std::string active_state;
  std::string realm_name;
  std::string realm_type;
  std::string current_map;
  std::string logs_directory = "Logs";
};

class CrashHandler {
 public:
  static CrashHandler& Get();

  void Install(const CrashContext& context = {});

  void Uninstall();

  [[nodiscard]] bool IsInstalled() const { return installed_; }

  [[nodiscard]] ErrorRingBuffer& GetRingBuffer() { return ring_buffer_; }
  [[nodiscard]] const ErrorRingBuffer& GetRingBuffer() const { return ring_buffer_; }

  void SetBuildVersion(std::string_view version);
  void SetGpuInfo(std::string_view info);
  void SetActiveState(std::string_view state);
  void SetRealmInfo(std::string_view realm_name, std::string_view realm_type);
  void ClearRealmInfo();
  void SetCurrentMap(std::string_view map);

  [[nodiscard]] CrashContext GetContext() const;

  [[nodiscard]] std::string FormatCrashReport(
      std::string_view signal_info,
      const std::vector<std::string>& stack) const;

  [[nodiscard]] std::string WriteCrashReport(
      std::string_view signal_info,
      const std::vector<std::string>& stack) const;

  [[nodiscard]] static std::vector<std::string> CaptureStackTrace(
      int max_frames = 64);

  [[nodiscard]] static std::string SignalName(int signal_number);

  using PreCrashCallback = std::function<void()>;
  void SetPreCrashCallback(PreCrashCallback cb);

  void Reset();

 private:
  CrashHandler() = default;

  mutable std::mutex mutex_;
  CrashContext context_;
  ErrorRingBuffer ring_buffer_;
  bool installed_{false};
  PreCrashCallback pre_crash_callback_;
};

inline void CrashLog(std::string_view msg, std::uint8_t severity = 0) {
  CrashHandler::Get().GetRingBuffer().Push(msg, severity);
}

}

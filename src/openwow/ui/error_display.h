#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace openwow::ui {

enum class ErrorPriority : std::uint8_t {
  Low,
  Normal,
  High,
};

struct ErrorEntry {
  std::string message;
  std::uint32_t color = 0xFFFF0000;
  ErrorPriority priority = ErrorPriority::Normal;
  float startTime = 0.0f;
  float duration = 3.0f;
  float alpha = 1.0f;
};

class ErrorDisplay {
 public:
  ErrorDisplay() = default;

  void ShowError(const std::string& message,
                 ErrorPriority priority = ErrorPriority::Normal);
  void ShowCustomError(const std::string& message, std::uint32_t color,
                       float duration);

  [[nodiscard]] std::optional<ErrorEntry> GetCurrentError() const;
  [[nodiscard]] std::vector<std::string> GetErrorHistory() const;
  [[nodiscard]] std::uint32_t GetHistoryCount() const;
  [[nodiscard]] bool IsShowingError() const;
  [[nodiscard]] float GetCurrentAlpha() const;

  void SetEnabled(bool enabled) { enabled_ = enabled; }
  [[nodiscard]] bool IsEnabled() const { return enabled_; }
  void SetMaxHistory(std::uint32_t max) { max_history_ = max; }
  [[nodiscard]] std::uint32_t GetMaxHistory() const { return max_history_; }
  void SetDefaultDuration(float seconds) { default_duration_ = seconds; }
  [[nodiscard]] float GetDefaultDuration() const { return default_duration_; }

  void ClearCurrent();
  void ClearHistory();

  void Update(float dt);
  void Reset();

  static constexpr std::uint32_t GetDefaultColor() { return 0xFFFF0000; }

  void AddCommonErrors();

 private:
  void PushToHistory(const std::string& message);
  void EnqueueError(const ErrorEntry& entry);

  bool enabled_ = true;
  float default_duration_ = 3.0f;
  std::uint32_t max_history_ = 25;
  float elapsed_ = 0.0f;

  std::optional<ErrorEntry> current_;
  std::deque<ErrorEntry> pending_;
  std::vector<std::string> history_;
};

}

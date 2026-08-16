#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openwow::ui::game {

enum class ErrorType : std::uint8_t {
  kError = 0,
  kInfo = 1,
};

struct ErrorMessage {
  std::string text;
  ErrorType type = ErrorType::kError;
  double timestamp = 0;
  float duration = 3.0f;
};

class ErrorMessageSystem {
 public:
  static ErrorMessageSystem& Get();

  void ShowError(const std::string& text);

  void ShowInfo(const std::string& text);

  std::vector<ErrorMessage> GetPendingMessages();

  std::vector<ErrorMessage> GetActiveMessages() const;

  void Update(float delta_time);

  void Clear();

  void OnServerError(std::uint32_t error_id, const std::string& text);

  [[nodiscard]] std::size_t GetMessageCount() const;

 private:
  ErrorMessageSystem() = default;

  void AddMessage(const std::string& text, ErrorType type, float duration);

  std::vector<ErrorMessage> messages_;
  std::vector<ErrorMessage> pending_;
  double elapsed_time_ = 0;
  mutable std::mutex mutex_;
};

}

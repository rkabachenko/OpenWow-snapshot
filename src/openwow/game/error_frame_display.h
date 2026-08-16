
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class UIErrorType : std::uint8_t {
  Error  = 0,
  Info   = 1,
  Money  = 2,
  System = 3,
};

struct UIErrorColor {
  float r{1.0f};
  float g{1.0f};
  float b{1.0f};

  bool operator==(const UIErrorColor& o) const {
    return r == o.r && g == o.g && b == o.b;
  }
};

struct UIErrorEntry {
  std::string  message;
  UIErrorType  type{UIErrorType::Error};
  double       timestamp{0.0};
  float        displayDuration{3.0f};
  float        fadeProgress{0.0f};
  UIErrorColor color{1.0f, 0.1f, 0.1f};
};

class ErrorFrameDisplay {
 public:
  ErrorFrameDisplay() = default;

  void ShowError(const std::string& message);
  void ShowInfo(const std::string& message);
  void ShowMoney(const std::string& message);
  void ShowSystem(const std::string& message);
  void ShowCustom(const std::string& message, UIErrorType type);

  void Update(float deltaTime);

  [[nodiscard]] std::vector<UIErrorEntry> GetActiveMessages() const;
  [[nodiscard]] std::optional<UIErrorEntry> GetCurrentMessage() const;
  [[nodiscard]] std::uint32_t GetMessageCount() const;

  void ClearAll();
  void SetEnabled(bool enabled);

  void AddToHistory(const UIErrorEntry& entry);
  [[nodiscard]] std::vector<UIErrorEntry> GetHistory(std::uint32_t count) const;

  [[nodiscard]] static UIErrorColor GetDefaultColor(UIErrorType type);

  static constexpr std::uint32_t kMaxVisible = 3;
  static constexpr std::uint32_t kMaxHistory = 50;
  static constexpr double kDedupWindow = 1.0;
  static constexpr float kFadeTime = 1.0f;

 private:
  void PushMessage(const std::string& message, UIErrorType type);

  std::vector<UIErrorEntry> active_;
  std::deque<UIErrorEntry>  history_;
  bool                      enabled_{true};
  double                    clock_{0.0};
};

}

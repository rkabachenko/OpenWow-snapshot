#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace openwow::platform {

class SystemMouseSpeedBackend {
public:
  virtual ~SystemMouseSpeedBackend() = default;

  [[nodiscard]] virtual int GetSystemMouseSpeed() = 0;
  virtual bool SetSystemMouseSpeed(int raw_speed) = 0;
};

class DefaultSystemMouseSpeedBackend final : public SystemMouseSpeedBackend {
public:

  [[nodiscard]] int GetSystemMouseSpeed() override;
  bool SetSystemMouseSpeed(int raw_speed) override;

private:
  static int ClampRawSpeed(int raw_speed) {
    return std::clamp(raw_speed, 1, 20);
  }

  int cached_speed_ = 10;
};

class SystemMouseSpeedController {
public:
  static SystemMouseSpeedController &Instance() {
    static SystemMouseSpeedController instance;
    return instance;
  }

  [[nodiscard]] std::string CaptureSystemDefaultAsCVarValue() {
    std::lock_guard lock(mutex_);
    EnsureInitializedLocked();
    return FormatNormalizedValueLocked(original_speed_);
  }

  void BootstrapFromSystemDefault() {
    std::lock_guard lock(mutex_);
    EnsureInitializedLocked();
  }

  bool ApplyCVarValue(std::string_view text) {
    std::lock_guard lock(mutex_);
    EnsureInitializedLocked();
    desired_speed_ = NormalizedToRawSpeed(ParseNormalizedValue(text));
    if (window_active_) {
      backend_->SetSystemMouseSpeed(desired_speed_);
      applied_speed_ = desired_speed_;
    }
    return true;
  }

  void SetWindowActive(bool active) {
    std::lock_guard lock(mutex_);
    EnsureInitializedLocked();
    if (window_active_ == active) {
      return;
    }

    window_active_ = active;
    const int target_speed = window_active_ ? desired_speed_ : original_speed_;
    backend_->SetSystemMouseSpeed(target_speed);
    applied_speed_ = target_speed;
  }

  void RestoreSystemDefault() {
    std::lock_guard lock(mutex_);
    EnsureInitializedLocked();
    backend_->SetSystemMouseSpeed(original_speed_);
    applied_speed_ = original_speed_;
  }

  void RestoreOriginalSpeedIfWindowActive() {
    std::lock_guard lock(mutex_);
    EnsureInitializedLocked();
    if (!window_active_ || desired_speed_ == original_speed_) {
      return;
    }

    backend_->SetSystemMouseSpeed(original_speed_);
    applied_speed_ = original_speed_;
  }

  [[nodiscard]] int original_speed() const {
    std::lock_guard lock(mutex_);
    return original_speed_;
  }

  [[nodiscard]] int desired_speed() const {
    std::lock_guard lock(mutex_);
    return desired_speed_;
  }

  [[nodiscard]] int applied_speed() const {
    std::lock_guard lock(mutex_);
    return applied_speed_;
  }

  [[nodiscard]] bool window_active() const {
    std::lock_guard lock(mutex_);
    return window_active_;
  }

  void ResetForTests(std::unique_ptr<SystemMouseSpeedBackend> backend = {}) {
    std::lock_guard lock(mutex_);
    backend_ = std::move(backend);
    if (!backend_) {
      backend_ = std::make_unique<DefaultSystemMouseSpeedBackend>();
    }

    initialized_ = false;
    window_active_ = true;
    original_speed_ = 10;
    desired_speed_ = 10;
    applied_speed_ = 10;
  }

private:
  SystemMouseSpeedController() : backend_(std::make_unique<DefaultSystemMouseSpeedBackend>()) {}

  static bool IsAsciiDigit(char ch) {
    return ch >= '0' && ch <= '9';
  }

  static int ParseSignedExponent(std::string_view text) {
    bool negative = false;
    std::size_t position = 0;
    if (!text.empty() && text.front() == '-') {
      negative = true;
      position = 1;
    }

    int exponent = 0;
    while (position < text.size() && IsAsciiDigit(text[position])) {
      const int digit = text[position] - '0';
      if (exponent > (std::numeric_limits<int>::max() - digit) / 10) {
        exponent = std::numeric_limits<int>::max();
        break;
      }

      exponent = exponent * 10 + digit;
      ++position;
    }

    return negative ? -exponent : exponent;
  }

  static double ScaleByPowerOfTen(double value, int exponent) {
    if (value == 0.0 || exponent == 0) {
      return value;
    }

    double base = 10.0;
    double factor = 1.0;
    unsigned int remaining =
        exponent < 0 ? static_cast<unsigned int>(-static_cast<long long>(exponent))
                     : static_cast<unsigned int>(exponent);
    while (remaining != 0) {
      if ((remaining & 1u) != 0u) {
        factor *= base;
      }

      remaining >>= 1u;
      if (remaining != 0u) {
        base *= base;
      }
    }

    return exponent < 0 ? value / factor : value * factor;
  }

  static double ParseNormalizedValue(std::string_view text) {
    if (text.empty()) {
      return 0.0;
    }

    bool negative = false;
    std::size_t position = 0;
    if (text.front() == '-') {
      negative = true;
      position = 1;
    }

    double value = 0.0;
    while (position < text.size() && IsAsciiDigit(text[position])) {
      value = value * 10.0 + static_cast<double>(text[position] - '0');
      ++position;
    }

    if (position < text.size() && text[position] == '.') {
      ++position;
      double scale = 0.1;
      while (position < text.size() && IsAsciiDigit(text[position])) {
        value += static_cast<double>(text[position] - '0') * scale;
        scale *= 0.1;
        ++position;
      }
    }

    if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
      ++position;
      if (position < text.size() && text[position] == '+') {
        ++position;
      }
      value = ScaleByPowerOfTen(value, ParseSignedExponent(text.substr(position)));
    }

    return negative ? -value : value;
  }

  static int NormalizedToRawSpeed(double normalized_value) {
    normalized_value = std::clamp(normalized_value, 0.1, 2.0);
    return std::clamp(static_cast<int>(normalized_value * 10.0), 1, 20);
  }

  void EnsureInitializedLocked() {
    if (initialized_) {
      return;
    }

    original_speed_ = std::clamp(backend_->GetSystemMouseSpeed(), 1, 20);
    desired_speed_ = original_speed_;
    applied_speed_ = original_speed_;
    initialized_ = true;
  }

  static std::string FormatNormalizedValueLocked(int raw_speed) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.1f",
                  static_cast<double>(std::clamp(raw_speed, 1, 20)) * 0.1);
    return buffer;
  }

  std::unique_ptr<SystemMouseSpeedBackend> backend_;
  mutable std::mutex mutex_;
  bool initialized_ = false;
  bool window_active_ = true;
  int original_speed_ = 10;
  int desired_speed_ = 10;
  int applied_speed_ = 10;
};

}

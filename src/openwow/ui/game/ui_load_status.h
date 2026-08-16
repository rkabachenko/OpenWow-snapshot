#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::game {

inline constexpr std::size_t kLegacyUiLoadStatusMessageMaxLength = 0xFF;

struct UiLoadStatusEntry {
  std::intptr_t code{0};
  std::string message;
};

class UiLoadStatusSink {
 public:
  virtual ~UiLoadStatusSink() = default;

  virtual void AppendStatus(std::intptr_t code, std::string_view message) = 0;
};

class UiLoadStatusBuffer final : public UiLoadStatusSink {
 public:
  void AppendStatus(std::intptr_t code, std::string_view message) override {
    const std::size_t message_length =
        std::min(message.size(), kLegacyUiLoadStatusMessageMaxLength);
    if (message_length == 0) {
      return;
    }
    if (code > highest_code_) {
      highest_code_ = code;
    }
    entries_.push_back(
        {.code = code, .message = std::string(message.substr(0, message_length))});
  }

  void ReplayInto(UiLoadStatusSink& sink) const {
    for (const auto& entry : entries_) {
      sink.AppendStatus(entry.code, entry.message);
    }
  }

  void AppendFrom(const UiLoadStatusBuffer& child) {
    child.ReplayInto(*this);
  }

  void Clear() {
    entries_.clear();
    highest_code_ = 0;
  }

  [[nodiscard]] bool empty() const {
    return entries_.empty();
  }

  [[nodiscard]] std::size_t size() const {
    return entries_.size();
  }

  [[nodiscard]] std::intptr_t highest_code() const {
    return highest_code_;
  }

  [[nodiscard]] const std::vector<UiLoadStatusEntry>& entries() const {
    return entries_;
  }

 private:
  std::vector<UiLoadStatusEntry> entries_;
  std::intptr_t highest_code_{0};
};

}

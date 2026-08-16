
#include "openwow/ui/game/error_message.h"

#include <algorithm>

namespace openwow::ui::game {

ErrorMessageSystem& ErrorMessageSystem::Get() {
  static ErrorMessageSystem instance;
  return instance;
}

void ErrorMessageSystem::ShowError(const std::string& text) {
  AddMessage(text, ErrorType::kError, 3.0f);
}

void ErrorMessageSystem::ShowInfo(const std::string& text) {
  AddMessage(text, ErrorType::kInfo, 3.0f);
}

std::vector<ErrorMessage> ErrorMessageSystem::GetPendingMessages() {
  std::lock_guard<std::mutex> lock(mutex_);
  auto result = std::move(pending_);
  pending_.clear();
  return result;
}

std::vector<ErrorMessage> ErrorMessageSystem::GetActiveMessages() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return messages_;
}

void ErrorMessageSystem::Update(float delta_time) {
  std::lock_guard<std::mutex> lock(mutex_);
  elapsed_time_ += static_cast<double>(delta_time);

  messages_.erase(
      std::remove_if(messages_.begin(), messages_.end(),
                     [this](const ErrorMessage& msg) {
                       return (elapsed_time_ - msg.timestamp) >= msg.duration;
                     }),
      messages_.end());
}

void ErrorMessageSystem::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  messages_.clear();
  pending_.clear();
}

void ErrorMessageSystem::OnServerError(std::uint32_t error_id,
                                       const std::string& text) {

  (void)error_id;
  if (!text.empty()) {
    AddMessage(text, ErrorType::kError, 3.0f);
  }
}

std::size_t ErrorMessageSystem::GetMessageCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return messages_.size();
}

void ErrorMessageSystem::AddMessage(const std::string& text, ErrorType type,
                                    float duration) {
  std::lock_guard<std::mutex> lock(mutex_);
  ErrorMessage msg;
  msg.text = text;
  msg.type = type;
  msg.timestamp = elapsed_time_;
  msg.duration = duration;
  messages_.push_back(msg);
  pending_.push_back(msg);
}

}

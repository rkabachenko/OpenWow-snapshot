
#include "openwow/game/raid_warning_display.h"

#include <algorithm>

namespace openwow::game {

float RaidMsgDisplay::DurationForType(RaidMsgType type) const {
  switch (type) {
    case RaidMsgType::RaidBossEmote:
      return kBossEmoteDuration;
    case RaidMsgType::RaidBossWhisper:
      return kBossEmoteDuration;
    case RaidMsgType::RaidWarning:
      return kWarningDuration;
    case RaidMsgType::ZoneMessage:
      return kWarningDuration;
  }
  return kWarningDuration;
}

void RaidMsgDisplay::ShowMessage(const std::string& message,
                                 RaidMsgType type,
                                 const std::string& senderName) {
  if (!enabled_) return;

  RaidMsgEntry entry;
  entry.message      = message;
  entry.type         = type;
  entry.timestamp    = currentTime_;
  entry.duration     = DurationForType(type);
  entry.fadeProgress = 0.0f;
  entry.senderName   = senderName;

  queue_.insert(queue_.begin(), entry);

  while (queue_.size() > kMaxQueueSize) {
    queue_.pop_back();
  }
}

void RaidMsgDisplay::Update(float deltaTime) {
  if (!enabled_) return;

  currentTime_ += static_cast<double>(deltaTime);

  for (auto& msg : queue_) {
    msg.duration -= deltaTime;

    if (msg.duration <= 0.0f) {

      msg.fadeProgress = 1.0f;
    } else if (msg.duration <= kFadeDuration) {

      msg.fadeProgress = 1.0f - (msg.duration / kFadeDuration);
    } else {

      msg.fadeProgress = 0.0f;
    }
  }

  queue_.erase(
      std::remove_if(queue_.begin(), queue_.end(),
                     [](const RaidMsgEntry& e) { return e.duration <= 0.0f; }),
      queue_.end());
}

std::optional<RaidMsgEntry> RaidMsgDisplay::GetActiveMessage() const {
  if (queue_.empty()) return std::nullopt;
  return queue_.front();
}

std::vector<RaidMsgEntry> RaidMsgDisplay::GetMessageQueue() const {
  return queue_;
}

std::uint32_t RaidMsgDisplay::GetQueueSize() const {
  return static_cast<std::uint32_t>(queue_.size());
}

void RaidMsgDisplay::DismissCurrent() {
  if (!queue_.empty()) {
    queue_.erase(queue_.begin());
  }
}

void RaidMsgDisplay::SetEnabled(bool enabled) {
  enabled_ = enabled;

  if (!enabled_) {
    queue_.clear();
  }
}

bool RaidMsgDisplay::IsEnabled() const {
  return enabled_;
}

void RaidMsgDisplay::ClearAll() {
  queue_.clear();
}

}

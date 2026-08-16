#include "openwow/game/chat_bubble.h"

#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace openwow::game {
namespace {

bool ChatBubbleDisplayOrderLess(const ChatBubble& lhs, const ChatBubble& rhs) {
  if (lhs.insertionOrder != rhs.insertionOrder) {
    return lhs.insertionOrder < rhs.insertionOrder;
  }
  return lhs.id < rhs.id;
}

void SortChatBubblesByDisplayOrder(std::vector<ChatBubble>* bubbles) {
  if (bubbles != nullptr) {
    std::sort(bubbles->begin(), bubbles->end(), ChatBubbleDisplayOrderLess);
  }
}

std::optional<bool> IsOwnerWithinRetailRange(const ObjectManager* objects,
                                             const ChatBubble& bubble) {
  if (objects == nullptr) {
    return std::nullopt;
  }

  const auto* viewer = objects->GetActivePlayer();
  const auto* owner = objects->GetUnit(bubble.unitGuid);
  if (viewer == nullptr || owner == nullptr || !owner->Presentation().EnsureModelReady()) {
    return std::nullopt;
  }

  const auto viewer_position = viewer->GetPosition();
  const auto owner_position = owner->GetPosition();
  const float dx = viewer_position.x - owner_position.x;
  const float dy = viewer_position.y - owner_position.y;
  const float dz = viewer_position.z - owner_position.z;
  const float distance_squared = dx * dx + dy * dy + dz * dz;
  return std::isfinite(distance_squared) &&
         distance_squared < ChatBubbleSystem::kMaxDistanceSquared;
}

}

ChatBubbleSystem& ChatBubbleSystem::Get() {
  static ChatBubbleSystem instance;
  return instance;
}

std::uint32_t ChatBubbleSystem::AddSpeechBubble(
    const ObjectGuid unitGuid, const std::string& text,
    const bool isLocalPlayer, const std::uint32_t color) {
  std::lock_guard lock(mutex_);

  const auto timeout_ms = CalculateSpeechBubbleTimeoutMs(text, isLocalPlayer);
  if (timeout_ms == 0u) {
    return 0u;
  }

  ChatBubble bubble;
  bubble.unitGuid = unitGuid;
  bubble.text = text;
  bubble.color = color;
  bubble.startTime = elapsed_;
  ConfigureSpeechTiming(bubble, timeout_ms);
  return InsertBubbleLocked(std::move(bubble));
}

std::uint32_t ChatBubbleSystem::CalculateSpeechBubbleTimeoutMs(
    const std::string_view text, const bool isLocalPlayer) {
  if (text.empty()) {
    return 0u;
  }

  const std::uint32_t step_ms = isLocalPlayer ? 500u : 750u;
  std::uint32_t timeout_ms = step_ms + (isLocalPlayer ? 1000u : 2000u);
  bool previous_was_whitespace = true;
  for (const char ch : text) {
    const bool is_word_break = ch == ' ' || ch == '\t';
    if (is_word_break && !previous_was_whitespace) {
      timeout_ms += step_ms;
    }
    previous_was_whitespace = is_word_break;
  }
  return timeout_ms;
}

void ChatBubbleSystem::RemoveBubblesForUnit(const ObjectGuid guid) {
  std::lock_guard lock(mutex_);
  std::erase_if(bubbles_, [&](const auto& entry) {
    return entry.second.unitGuid == guid;
  });
}

std::optional<ChatBubble> ChatBubbleSystem::GetBubble(
    const std::uint32_t id) const {
  std::lock_guard lock(mutex_);
  const auto it = bubbles_.find(id);
  return it != bubbles_.end() ? std::optional<ChatBubble>(it->second)
                              : std::nullopt;
}

std::vector<ChatBubble> ChatBubbleSystem::GetBubblesForUnit(
    const ObjectGuid guid) const {
  std::lock_guard lock(mutex_);
  std::vector<ChatBubble> result;
  for (const auto& [id, bubble] : bubbles_) {
    (void)id;
    if (bubble.unitGuid == guid) {
      result.push_back(bubble);
    }
  }
  SortChatBubblesByDisplayOrder(&result);
  return result;
}

std::vector<ChatBubble> ChatBubbleSystem::GetAllBubbles() const {
  std::lock_guard lock(mutex_);
  std::vector<ChatBubble> result;
  result.reserve(bubbles_.size());
  for (const auto& [id, bubble] : bubbles_) {
    (void)id;
    result.push_back(bubble);
  }
  SortChatBubblesByDisplayOrder(&result);
  return result;
}

std::uint32_t ChatBubbleSystem::GetActiveBubbleCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(bubbles_.size());
}

void ChatBubbleSystem::Update(const float dt) {
  std::lock_guard lock(mutex_);
  UpdateLocked(dt, nullptr);
}

void ChatBubbleSystem::Update(const float dt, const ObjectManager& objects) {
  std::lock_guard lock(mutex_);
  UpdateLocked(dt, &objects);
}

void ChatBubbleSystem::UpdateLocked(float dt, const ObjectManager* objects) {
  if (!std::isfinite(dt) || dt < 0.0f) {
    dt = 0.0f;
  }
  elapsed_ += dt;

  for (auto it = bubbles_.begin(); it != bubbles_.end();) {
    auto& bubble = it->second;
    if (objects != nullptr && objects->GetUnit(bubble.unitGuid) == nullptr) {
      it = bubbles_.erase(it);
      continue;
    }

    if (const auto in_range = IsOwnerWithinRetailRange(objects, bubble);
        in_range.has_value()) {
      if (!*in_range) {
        if (AlphaByte(bubble) != 0u && !bubble.expireAfterFade &&
            bubble.fadeDirection != ChatBubbleFadeDirection::kOut) {
          BeginFadeOutLocked(bubble, false);
        }
      } else if (AlphaByte(bubble) != 0xFFu && !bubble.expireAfterFade &&
                 bubble.fadeDirection != ChatBubbleFadeDirection::kIn) {
        BeginFadeInLocked(bubble);
      }
    }

    const float age = elapsed_ - bubble.startTime;
    if (bubble.fadeDirection != ChatBubbleFadeDirection::kOut &&
        !bubble.expireAfterFade && age > bubble.fadeOutStartTime) {
      BeginFadeOutLocked(bubble, true);
    }

    if (AdvanceFadeLocked(bubble)) {
      it = bubbles_.erase(it);
      continue;
    }
    ++it;
  }
}

void ChatBubbleSystem::PruneMissingOwners(const ObjectManager& objects) {
  std::lock_guard lock(mutex_);
  std::erase_if(bubbles_, [&](const auto& entry) {
    return objects.GetUnit(entry.second.unitGuid) == nullptr;
  });
}

void ChatBubbleSystem::Clear() {
  std::lock_guard lock(mutex_);
  bubbles_.clear();
}

void ChatBubbleSystem::Reset() {
  std::lock_guard lock(mutex_);
  bubbles_.clear();
  nextId_ = 1u;
  elapsed_ = 0.0f;
  nextInsertionOrder_ = 1u;
}

std::uint32_t ChatBubbleSystem::InsertBubbleLocked(ChatBubble bubble) {
  const auto id = nextId_++;
  bubble.id = id;
  bubble.insertionOrder = nextInsertionOrder_++;
  bubbles_[id] = std::move(bubble);
  EnforceMaxBubbles();
  return id;
}

void ChatBubbleSystem::ConfigureSpeechTiming(
    ChatBubble& bubble, const std::uint32_t timeoutMs) {
  const float timeout_seconds = static_cast<float>(timeoutMs) / 1000.0f;
  bubble.duration = timeout_seconds + kGracePeriod + kFadeOutDuration;
  bubble.alpha = 0.0f;
  bubble.fadeInDuration = kFadeInDuration;
  bubble.fadeOutStartTime = timeout_seconds + kGracePeriod;
  bubble.fadeOutDuration = kFadeOutDuration;
  bubble.fadeDirection = ChatBubbleFadeDirection::kIn;
  bubble.fadeStartTime = bubble.startTime;
  bubble.expireAfterFade = false;
}

std::uint8_t ChatBubbleSystem::AlphaByte(const ChatBubble& bubble) {
  return static_cast<std::uint8_t>(
      std::clamp(bubble.alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
}

void ChatBubbleSystem::BeginFadeInLocked(ChatBubble& bubble) {
  if (bubble.expireAfterFade ||
      bubble.fadeDirection == ChatBubbleFadeDirection::kIn) {
    return;
  }
  const float completed = static_cast<float>(AlphaByte(bubble)) / 255.0f;
  bubble.fadeDirection = ChatBubbleFadeDirection::kIn;
  bubble.fadeStartTime = elapsed_ - completed * kFadeInDuration;
}

void ChatBubbleSystem::BeginFadeOutLocked(ChatBubble& bubble,
                                          const bool expireAfterFade) {
  bubble.expireAfterFade = bubble.expireAfterFade || expireAfterFade;
  if (bubble.fadeDirection == ChatBubbleFadeDirection::kOut) {
    return;
  }
  const float completed =
      (255.0f - static_cast<float>(AlphaByte(bubble))) / 255.0f;
  bubble.fadeDirection = ChatBubbleFadeDirection::kOut;
  bubble.fadeStartTime = elapsed_ - completed * kFadeOutDuration;
}

bool ChatBubbleSystem::AdvanceFadeLocked(ChatBubble& bubble) const {
  const float fade_age = std::max(0.0f, elapsed_ - bubble.fadeStartTime);
  if (bubble.fadeDirection == ChatBubbleFadeDirection::kIn) {
    if (fade_age >= kFadeInDuration) {
      bubble.alpha = 1.0f;
      bubble.fadeDirection = ChatBubbleFadeDirection::kNone;
    } else {
      bubble.alpha = fade_age / kFadeInDuration;
    }
    return false;
  }

  if (bubble.fadeDirection == ChatBubbleFadeDirection::kOut) {
    if (fade_age >= kFadeOutDuration) {
      bubble.alpha = 0.0f;
      bubble.fadeDirection = ChatBubbleFadeDirection::kNone;
      return bubble.expireAfterFade;
    }
    bubble.alpha = 1.0f - fade_age / kFadeOutDuration;
  }
  return false;
}

void ChatBubbleSystem::EnforceMaxBubbles() {
  while (bubbles_.size() > kMaxBubbles) {
    auto oldest = bubbles_.begin();
    for (auto it = bubbles_.begin(); it != bubbles_.end(); ++it) {
      if (ChatBubbleDisplayOrderLess(it->second, oldest->second)) {
        oldest = it;
      }
    }
    bubbles_.erase(oldest);
  }
}

}

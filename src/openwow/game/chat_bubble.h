#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::game {

class ObjectManager;

enum class ChatBubbleFadeDirection : std::uint8_t {
  kNone,
  kIn,
  kOut,
};

struct ChatBubble {
  std::uint32_t id{0};
  ObjectGuid unitGuid;
  std::string text;
  std::uint32_t color{0xFFFFFFFF};
  std::uint64_t insertionOrder{0};
  float startTime{0.0f};
  float duration{15.0f};
  float alpha{1.0f};
  float fadeInDuration{0.0f};
  float fadeOutStartTime{13.0f};
  float fadeOutDuration{2.0f};

  ChatBubbleFadeDirection fadeDirection{ChatBubbleFadeDirection::kNone};
  float fadeStartTime{0.0f};
  bool expireAfterFade{false};
};

class ChatBubbleSystem {
 public:
  static ChatBubbleSystem& Get();

  ChatBubbleSystem() = default;

  static constexpr std::uint32_t kMaxBubbles = 20;
  static constexpr float kMaxDistanceSquared = 625.0f;

  std::uint32_t AddSpeechBubble(ObjectGuid unitGuid, const std::string& text,
                                bool isLocalPlayer,
                                std::uint32_t color = 0xFFFFFFFF);

  [[nodiscard]] static std::uint32_t CalculateSpeechBubbleTimeoutMs(
      std::string_view text, bool isLocalPlayer);

  void RemoveBubblesForUnit(ObjectGuid guid);

  [[nodiscard]] std::optional<ChatBubble> GetBubble(std::uint32_t id) const;
  [[nodiscard]] std::vector<ChatBubble> GetBubblesForUnit(
      ObjectGuid guid) const;
  [[nodiscard]] std::vector<ChatBubble> GetAllBubbles() const;
  [[nodiscard]] std::uint32_t GetActiveBubbleCount() const;

  void Update(float dt);

  void Update(float dt, const ObjectManager& objects);

  void PruneMissingOwners(const ObjectManager& objects);

  void Clear();
  void Reset();

 private:
  [[nodiscard]] std::uint32_t InsertBubbleLocked(ChatBubble bubble);
  static void ConfigureSpeechTiming(ChatBubble& bubble,
                                    std::uint32_t timeoutMs);
  static std::uint8_t AlphaByte(const ChatBubble& bubble);
  void BeginFadeInLocked(ChatBubble& bubble);
  void BeginFadeOutLocked(ChatBubble& bubble, bool expireAfterFade);
  [[nodiscard]] bool AdvanceFadeLocked(ChatBubble& bubble) const;
  void UpdateLocked(float dt, const ObjectManager* objects);
  void EnforceMaxBubbles();

  mutable std::mutex mutex_;
  std::unordered_map<std::uint32_t, ChatBubble> bubbles_;
  std::uint32_t nextId_{1};
  float elapsed_{0.0f};
  std::uint64_t nextInsertionOrder_{1};

  static constexpr float kFadeInDuration = 0.250f;
  static constexpr float kFadeOutDuration = 0.250f;
  static constexpr float kGracePeriod = 0.250f;
};

}

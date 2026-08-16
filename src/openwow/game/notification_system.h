
#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class NotificationType {

  ChatMessage,
  WhisperReceived,

  FriendOnline,
  FriendOffline,
  GuildMemberOnline,
  GuildMemberOffline,

  LootReceived,
  MoneyReceived,
  CurrencyReceived,

  YouDied,
  ResSickness,
  DurabilityLow,

  GroupInvite,
  ReadyCheck,
  RolePoll,

  AchievementEarned,
  CriteriaComplete,

  BagFull,
  QuestComplete,
  QuestFailed,
  LevelUp,
  SkillUp,
  NewMail,
  CalendarInvite,
  ArenaSeasonEnd,
  MaintenanceWarning,

  ServerShutdown,
  Disconnected,
  AddonMessage,
};

struct Notification {
  NotificationType type{};
  std::string text;
  std::string sender;
  std::uint32_t data1 = 0;
  std::uint32_t data2 = 0;
  ObjectGuid sender_guid;
  std::uint32_t timestamp = 0;
};

class NotificationSystem {
 public:
  static NotificationSystem& Get();

  static constexpr std::size_t kMaxRecent = 50;

  void Notify(const Notification& notification);
  void Notify(NotificationType type, const std::string& text);

  using NotificationHandler = std::function<void(const Notification&)>;
  std::uint32_t RegisterHandler(NotificationType type,
                                NotificationHandler handler);
  void UnregisterHandler(std::uint32_t id);

  [[nodiscard]] const std::vector<Notification>& GetRecent() const;
  void ClearRecent();

  [[nodiscard]] std::uint32_t GetPendingCount(NotificationType type) const;
  void ClearPending(NotificationType type);

  void Reset();

 private:
  NotificationSystem() = default;

  struct HandlerEntry {
    std::uint32_t id;
    NotificationType type;
    NotificationHandler handler;
  };

  std::vector<HandlerEntry> handlers_;
  std::vector<Notification> recent_;
  std::unordered_map<int, std::uint32_t> pending_counts_;
  std::uint32_t next_handler_id_ = 1;
  mutable std::mutex mutex_;
};

}


#pragma once

#include "openwow/game/commerce/mail/mail_compose_state.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

class SocialManager;
class QueryCache;

inline constexpr int kMaxMailItems = 12;
inline constexpr int kMaxInboxAttachmentSlots = 16;
inline constexpr int kMaxInspectedEnchantmentSlot = 7;
inline constexpr int kMaxInboxClientCapacity = 50;
inline constexpr std::size_t kMaxNextMailSenders = 3;
inline constexpr float kPendingMailTimerEpsilon = 0.00000023841858f;

enum class MailType : std::uint8_t {
  kNormal = 0,
  kAuction = 2,
  kCreature = 3,
  kGameObject = 4,
  kCalendar = 5,
};

enum class MailResponseType : std::uint32_t {
  kSend = 0,
  kMoneyTaken = 1,
  kItemTaken = 2,
  kReturnedToSender = 3,
  kDeleted = 4,
  kMadePermanent = 5,
};

enum class MailDeleteReason : std::uint32_t {
  kManual = 0,
  kAutoIgnoredSenderCleanup = 1,
  kAutoAfterMoneyTaken = 2,
  kAutoAfterItemTaken = 3,
};

enum class MailResult : std::uint32_t {
  kOk = 0,
  kEquipError = 1,
  kCannotSendToSelf = 2,
  kNotEnoughMoney = 3,
  kRecipientNotFound = 4,
  kNotYourTeam = 5,
  kInternalError = 6,
  kDisabledForTrialAcc = 14,
  kRecipientCapReached = 15,
  kCantSendWrappedCod = 16,
  kMailAndChatSuspended = 17,
  kTooManyAttachments = 18,
  kMailAttachmentInvalid = 19,
  kItemHasExpired = 21,
};

enum class InboxRefreshRequestResult : std::uint8_t {
  kStarted = 0,
  kBlockedPending = 1,
  kBlockedClosed = 2,
  kThrottled = 3,
};

inline constexpr std::uint32_t kMailCheckedRead = 0x01;
inline constexpr std::uint32_t kMailCheckedReturned = 0x02;
inline constexpr std::uint32_t kMailCheckedCopied = 0x04;
inline constexpr std::uint32_t kMailCheckedCodPayment = 0x08;
inline constexpr std::uint32_t kMailCheckedCalendarInviteRemoved = 0x100;
inline constexpr std::uint32_t kMailCheckedReturnedAlt = 0x200;
inline constexpr std::uint32_t kMailReturnToSenderBlockedMask =
    kMailCheckedReturned | kMailCheckedCodPayment | kMailCheckedReturnedAlt;

struct MailEnchantData {
  std::uint32_t enchant_id = 0;
  std::uint32_t enchant_duration = 0;
  std::uint32_t enchant_charges = 0;
};

struct MailItemInfo {
  std::uint8_t index = 0;
  std::uint32_t item_guid_low = 0;
  std::uint32_t item_entry = 0;
  MailEnchantData enchants[kMaxInspectedEnchantmentSlot] = {};
  std::int32_t random_property_id = 0;
  std::uint32_t suffix_factor = 0;
  std::uint32_t stack_count = 0;
  std::uint32_t spell_charges = 0;
  std::uint32_t max_durability = 0;
  std::uint32_t durability = 0;
};

struct MailEntry {
  std::uint16_t message_size = 0;
  std::uint32_t message_id = 0;
  MailType message_type = MailType::kNormal;

  std::uint64_t sender_guid = 0;
  std::uint32_t sender_entry = 0;

  std::uint32_t cod = 0;

  std::uint32_t package_icon_id = 0;
  std::uint32_t stationery = 0;
  std::uint32_t money = 0;
  std::uint32_t checked = 0;
  float expiration_time = 0.0f;
  std::uint32_t mail_template_id = 0;
  std::string subject;
  std::string body;
  std::vector<MailItemInfo> items;
  bool removal_request_pending = false;
};

[[nodiscard]] inline bool CanDeleteInboxMail(const MailEntry &entry) {
  return (entry.checked & kMailReturnToSenderBlockedMask) != 0 || entry.money != 0 ||
         (entry.items.empty() && entry.cod == 0);
}

[[nodiscard]] inline bool CanReturnInboxMail(const MailEntry &entry) {
  if (entry.message_type != MailType::kNormal) {
    return false;
  }

  if (entry.money == 0 && entry.items.empty()) {
    return false;
  }

  return (entry.checked & kMailReturnToSenderBlockedMask) == 0;
}

struct MailSendResult {
  std::uint32_t mail_id = 0;
  MailResponseType action = MailResponseType::kSend;
  MailResult error = MailResult::kOk;

  std::uint32_t equip_error = 0;
  std::uint32_t item_guid_low = 0;
  std::uint32_t item_count = 0;
};

struct MailSendAttachment {
  std::uint8_t slot = 0;
  std::uint64_t item_guid = 0;
};

struct NextMailTimeSender {
  std::uint64_t sender_guid = 0;
  std::uint32_t sender_entry = 0;
  std::uint32_t message_type = 0;
  std::uint32_t stationery = 0;
  float time_left = 0.0f;
};

struct MailListSnapshot {
  std::uint32_t real_count = 0;
  std::vector<MailEntry> entries;
};

struct NextMailTimeSnapshot {
  float next_mail_time = -1.0f;
  std::vector<NextMailTimeSender> senders;
};

enum class MailFollowupKind : std::uint8_t {
  kDelete,
  kReturnToSender,
  kTakeItem,
  kTakeMoney,
};

struct MailFollowupCommand {
  MailFollowupKind kind = MailFollowupKind::kDelete;
  std::uint64_t mailbox_guid = 0;
  std::uint32_t mail_id = 0;
  std::uint64_t sender_guid = 0;
  std::uint32_t item_guid_low = 0;
  MailDeleteReason delete_reason = MailDeleteReason::kManual;
};

class MailInteraction {
 public:
  [[nodiscard]] MailComposeState& compose() noexcept { return compose_; }
  [[nodiscard]] const MailComposeState& compose() const noexcept { return compose_; }
  struct MailListResult {
    std::vector<MailFollowupCommand> followups;
  };

  struct MailSendChanges {
    bool inbox_updated = false;
    bool show_attachment_autoloot_error = false;
    std::vector<MailFollowupCommand> followups;

    std::vector<std::uint32_t> closed_inbox_indices;
  };

  struct InboxMailOpenedResult {
    bool marked_read = false;
    bool cleared_pending_mail = false;
  };

  [[nodiscard]] MailListResult HandleMailListResult(
      MailListSnapshot snapshot, const SocialManager* social = nullptr,
      QueryCache* query_cache = nullptr);
  [[nodiscard]] MailSendChanges HandleSendMailResult(
      MailSendResult result, bool can_continue_auto_loot = true);
  void HandleReceivedMail(float next_mail_delay);
  void HandleShowMailbox(std::uint64_t mailbox_guid);
  void HandleNextMailTime(NextMailTimeSnapshot snapshot);

  [[nodiscard]] const std::vector<MailEntry> &inbox() const {
    return inbox_;
  }
  [[nodiscard]] std::uint32_t real_count() const {
    return real_count_;
  }
  [[nodiscard]] const std::optional<MailSendResult> &last_result() const {
    return last_result_;
  }
  [[nodiscard]] std::uint64_t mailbox_guid() const {
    return mailbox_guid_;
  }
  void set_mailbox_guid(std::uint64_t guid) {
    mailbox_guid_ = guid;
  }
  [[nodiscard]] const MailEntry *GetInboxMail(std::size_t zero_based_index) const {
    if (zero_based_index >= inbox_.size()) {
      return nullptr;
    }
    return &inbox_[zero_based_index];
  }
  [[nodiscard]] MailEntry *GetMutableInboxMail(std::size_t zero_based_index) {
    if (zero_based_index >= inbox_.size()) {
      return nullptr;
    }
    return &inbox_[zero_based_index];
  }
  [[nodiscard]] bool has_new_mail() const {
    return HasPendingMail();
  }
  [[nodiscard]] bool HasPendingMail() const;
  [[nodiscard]] float next_mail_time() const {
    return next_mail_time_;
  }
  [[nodiscard]] const std::vector<NextMailTimeSender> &next_mail_senders() const {
    return next_mail_senders_;
  }
  [[nodiscard]] bool HasPendingMailboxOperation() const {
    return mail_request_pending_;
  }
  void ResetForPlayerEnterWorld();
  void ApplyPendingMailDelay(float delay_seconds);
  bool UpdatePendingMailTimers(float elapsed_seconds);
  bool ConsumePendingMailUpdateEvent();
  bool ConsumeMailInboxUpdateEvent();
  bool ConsumeSendInfoUpdateEvent();
  void QueueInboxUpdateEvent();
  void QueueSendInfoUpdateEvent();
  [[nodiscard]] InboxMailOpenedResult OpenInboxMail(std::size_t zero_based_index);
  bool ConsumeNextMailTimeQueryRequest();
  [[nodiscard]] bool TryStartMailboxAction();
  [[nodiscard]] bool TryStartInboxRemovalAction(MailEntry &entry);
  [[nodiscard]] InboxRefreshRequestResult TryStartInboxRefresh();
  struct AutoLootSequenceResult {
    std::vector<MailFollowupCommand> followups;
    bool show_attachment_autoloot_error = false;
  };

  [[nodiscard]] AutoLootSequenceResult StartAutoLootMailSequence(const MailEntry &entry,
                                                                 bool can_take_attachments);
  [[nodiscard]] AutoLootSequenceResult ContinueAutoLootMailSequence(
      bool can_take_attachments);
  void ClearPendingAutoLootMailSequence();
  void FinishPendingMailboxOperation();
  [[nodiscard]] bool CanComplainInboxItem(std::size_t zero_based_index,
                                          std::uint64_t active_player_guid,
                                          const SocialManager &social) const;
  [[nodiscard]] const MailItemInfo *GetMailItem(const MailEntry &entry, std::uint32_t slot) const;
  [[nodiscard]] bool MarkMailPermanent(std::uint32_t mail_id);
  [[nodiscard]] bool DeleteMailFromInbox(std::uint32_t mail_id);

  void Shutdown();

  void CloseMailbox(bool full_reset);
  void ResetCompose();

  void Initialize();

 private:
  MailComposeState compose_;
  void ResetRuntimeState();

  struct AutoLootAttachmentStep {
    MailFollowupCommand command;
    bool queue_followup = false;
  };

  using MailClock = std::chrono::steady_clock;

  static bool IsZeroTimer(float value);
  static bool IsActivePendingMailSender(const NextMailTimeSender &sender);
  [[nodiscard]] bool IsComplaintableInboxMail(const MailEntry &entry,
                                              std::uint64_t active_player_guid,
                                              const SocialManager &social) const;
  [[nodiscard]] std::optional<AutoLootAttachmentStep> BuildAutoLootAttachmentStep(
      const MailEntry &entry) const;
  void QueueMailInboxUpdate();
  void QueueNextMailTimeQuery();
  [[nodiscard]] MailEntry *FindInboxMail(std::uint32_t mail_id);
  [[nodiscard]] const MailEntry *FindInboxMail(std::uint32_t mail_id) const;
  void PrimeAuctionMailItemQuery(const MailEntry &entry, QueryCache &query_cache);
  static void RemoveInboxItemByGuid(MailEntry &entry, std::uint32_t item_guid_low);
  [[nodiscard]] bool ShouldAutoDeleteEmptiedMail(const MailEntry &entry) const;
  void QueueAutoDelete(
      MailEntry& entry, MailDeleteReason reason, MailSendChanges& changes);
  void QueuePendingMailUpdateEvent();

  std::vector<MailEntry> inbox_;
  std::uint32_t real_count_ = 0;
  std::optional<MailSendResult> last_result_;
  std::uint64_t mailbox_guid_ = 0;
  float next_mail_time_ = -1.0f;
  std::vector<NextMailTimeSender> next_mail_senders_;
  bool pending_mail_update_event_pending_ = false;
  bool mail_inbox_update_event_pending_ = false;
  bool send_info_update_event_pending_ = false;
  bool next_mail_time_query_requested_ = false;
  bool refresh_next_mail_time_on_close_ = false;
  bool mail_request_pending_ = false;
  std::optional<std::uint32_t> pending_auto_loot_mail_id_;
  MailClock::time_point next_inbox_refresh_allowed_at_{};

  bool initialized_ = false;
};

}

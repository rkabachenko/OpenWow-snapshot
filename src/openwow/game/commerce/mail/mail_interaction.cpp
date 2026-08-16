
#include "openwow/game/commerce/mail/mail_interaction.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>

#include "openwow/game/query_cache.h"
#include "openwow/game/social_manager.h"

namespace openwow::game {

namespace {

[[nodiscard]] bool ShouldAutoHandleIgnoredInboxMail(const MailEntry &entry,
                                                    const SocialManager *social) {
  return social != nullptr && entry.message_type == MailType::kNormal && entry.sender_guid != 0 &&
         social->IsIgnored(ObjectGuid(entry.sender_guid)) &&
         (entry.checked & kMailReturnToSenderBlockedMask) == 0;
}

[[nodiscard]] bool PendingSenderMatchesUnreadInboxMail(
    const NextMailTimeSender& sender, const MailEntry& entry) {
  if ((entry.checked & kMailCheckedRead) != 0) {
    return false;
  }
  if (sender.sender_guid != 0 && sender.sender_guid == entry.sender_guid) {
    return true;
  }
  return sender.sender_entry != 0 &&
         sender.sender_entry == entry.sender_entry;
}

[[nodiscard]] std::optional<std::uint32_t> ParseAuctionMailItemId(
    const std::string_view subject) {
  if (subject.empty()) {
    return std::nullopt;
  }

  const auto separator = subject.find(':');
  const auto token = separator == std::string_view::npos ? subject : subject.substr(0, separator);
  if (token.empty()) {
    return std::nullopt;
  }

  std::uint32_t item_id = 0;
  const auto result =
      std::from_chars(token.data(), token.data() + token.size(), item_id, 10);
  if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || item_id == 0) {
    return std::nullopt;
  }

  return item_id;
}

[[nodiscard]] AsyncQueryChannel::CallbackKey BuildAuctionMailItemQueryCallbackKey(
    const std::uint32_t message_id) {
  return AsyncQueryChannel::CallbackKey(
      reinterpret_cast<std::uintptr_t>(&BuildAuctionMailItemQueryCallbackKey), message_id);
}

}

MailInteraction::MailListResult MailInteraction::HandleMailListResult(
    MailListSnapshot snapshot, const SocialManager* social,
    QueryCache* query_cache) {
  MailListResult result;
  std::vector<MailEntry> parsed_inbox;
  parsed_inbox.reserve(snapshot.entries.size());
  for (MailEntry& e : snapshot.entries) {
    if (ShouldAutoHandleIgnoredInboxMail(e, social)) {
      if (CanReturnInboxMail(e)) {
        result.followups.push_back({
            .kind = MailFollowupKind::kReturnToSender,
            .mailbox_guid = mailbox_guid_,
            .mail_id = e.message_id,
            .sender_guid = e.sender_guid,
        });
      } else {
        result.followups.push_back({
            .kind = MailFollowupKind::kDelete,
            .mailbox_guid = mailbox_guid_,
            .mail_id = e.message_id,
            .delete_reason = MailDeleteReason::kAutoIgnoredSenderCleanup,
        });
      }
      --snapshot.real_count;
      continue;
    }

    parsed_inbox.push_back(std::move(e));
  }

  inbox_ = std::move(parsed_inbox);
  real_count_ = snapshot.real_count;

  if (query_cache != nullptr) {
    for (const auto &entry : inbox_) {
      PrimeAuctionMailItemQuery(entry, *query_cache);
    }
  }

  if (inbox_.empty() && HasPendingMail()) {
    QueueNextMailTimeQuery();
  }
  FinishPendingMailboxOperation();
  return result;
}

MailInteraction::MailSendChanges MailInteraction::HandleSendMailResult(
    MailSendResult res,
    const bool can_continue_auto_loot) {
  MailSendChanges changes;
  last_result_ = res;
  FinishPendingMailboxOperation();

  const auto apply_item_taken_update = [this, &res, &changes]() {
    auto *entry = FindInboxMail(res.mail_id);
    if (!entry) {
      return;
    }

    RemoveInboxItemByGuid(*entry, res.item_guid_low);
    entry->cod = 0;
    changes.inbox_updated = true;
    QueueAutoDelete(*entry, MailDeleteReason::kAutoAfterItemTaken, changes);
  };

  if (res.error == MailResult::kOk) {
    switch (res.action) {
    case MailResponseType::kMoneyTaken: {
      if (auto *entry = FindInboxMail(res.mail_id)) {
        entry->money = 0;
        changes.inbox_updated = true;
        QueueAutoDelete(*entry, MailDeleteReason::kAutoAfterMoneyTaken, changes);
      }
      break;
    }
    case MailResponseType::kItemTaken:
      apply_item_taken_update();
      {
        auto followup = ContinueAutoLootMailSequence(can_continue_auto_loot);
        changes.show_attachment_autoloot_error =
            followup.show_attachment_autoloot_error;
        for (auto &command : followup.followups) {
          changes.followups.push_back(std::move(command));
        }
      }
      break;
    case MailResponseType::kReturnedToSender:
    case MailResponseType::kDeleted:
      changes.inbox_updated = DeleteMailFromInbox(res.mail_id);
      break;
    case MailResponseType::kMadePermanent:
      changes.inbox_updated = MarkMailPermanent(res.mail_id);
      break;
    case MailResponseType::kSend:
      break;
    }
  } else if (res.action == MailResponseType::kItemTaken &&
             res.error == MailResult::kItemHasExpired) {
    apply_item_taken_update();
    ClearPendingAutoLootMailSequence();
  } else if (res.action == MailResponseType::kItemTaken) {
    ClearPendingAutoLootMailSequence();
  }

  return changes;
}

void MailInteraction::HandleReceivedMail(const float next_mail_delay) {
  ApplyPendingMailDelay(next_mail_delay);
}

void MailInteraction::HandleShowMailbox(const std::uint64_t mailbox_guid) {
  refresh_next_mail_time_on_close_ = false;
  mailbox_guid_ = mailbox_guid;
}

void MailInteraction::HandleNextMailTime(NextMailTimeSnapshot snapshot) {
  next_mail_time_ = snapshot.next_mail_time;
  next_mail_senders_ = std::move(snapshot.senders);
  refresh_next_mail_time_on_close_ = false;
  QueuePendingMailUpdateEvent();
}

void MailInteraction::ApplyPendingMailDelay(const float delay_seconds) {
  if (mailbox_guid_ != 0) {
    refresh_next_mail_time_on_close_ = true;
    return;
  }

  QueueNextMailTimeQuery();

  if (IsZeroTimer(delay_seconds)) {
    next_mail_time_ = 0.0f;
    QueuePendingMailUpdateEvent();
  } else {
    next_mail_time_ = delay_seconds;
  }
}

void MailInteraction::ResetRuntimeState() {
  compose_.Reset();
  inbox_.clear();
  real_count_ = 0;
  last_result_.reset();
  mailbox_guid_ = 0;
  next_mail_time_ = -1.0f;
  next_mail_senders_.clear();
  pending_mail_update_event_pending_ = false;
  mail_inbox_update_event_pending_ = false;
  send_info_update_event_pending_ = false;
  next_mail_time_query_requested_ = false;
  refresh_next_mail_time_on_close_ = false;
  mail_request_pending_ = false;
  pending_auto_loot_mail_id_.reset();
  next_inbox_refresh_allowed_at_ = {};
}

void MailInteraction::ResetForPlayerEnterWorld() {
  Initialize();
  ResetRuntimeState();
  QueueNextMailTimeQuery();
}

bool MailInteraction::HasPendingMail() const {
  return IsZeroTimer(next_mail_time_);
}

bool MailInteraction::UpdatePendingMailTimers(float elapsed_seconds) {
  if (!(elapsed_seconds > 0.0f))
    return false;

  bool fired_update = false;
  for (auto &sender : next_mail_senders_) {
    if (!IsActivePendingMailSender(sender) || !(sender.time_left > 0.0f)) {
      continue;
    }

    sender.time_left = std::max(sender.time_left - elapsed_seconds, 0.0f);
    if (IsZeroTimer(sender.time_left)) {
      sender.time_left = 0.0f;
      fired_update = true;
    }
  }

  if (next_mail_time_ > 0.0f) {
    next_mail_time_ = std::max(next_mail_time_ - elapsed_seconds, 0.0f);
    if (IsZeroTimer(next_mail_time_)) {
      next_mail_time_ = 0.0f;
      fired_update = true;
    }
  }

  if (fired_update) {
    QueuePendingMailUpdateEvent();
  }
  return fired_update;
}

bool MailInteraction::ConsumePendingMailUpdateEvent() {
  const bool pending = pending_mail_update_event_pending_;
  pending_mail_update_event_pending_ = false;
  return pending;
}

bool MailInteraction::ConsumeMailInboxUpdateEvent() {
  const bool pending = mail_inbox_update_event_pending_;
  mail_inbox_update_event_pending_ = false;
  return pending;
}

bool MailInteraction::ConsumeSendInfoUpdateEvent() {
  const bool pending = send_info_update_event_pending_;
  send_info_update_event_pending_ = false;
  return pending;
}

void MailInteraction::QueueInboxUpdateEvent() {
  QueueMailInboxUpdate();
}

void MailInteraction::QueueSendInfoUpdateEvent() {
  send_info_update_event_pending_ = true;
}

MailInteraction::InboxMailOpenedResult MailInteraction::OpenInboxMail(
    const std::size_t zero_based_index) {
  InboxMailOpenedResult result;

  auto *entry = GetMutableInboxMail(zero_based_index);
  if (entry == nullptr || (entry->checked & kMailCheckedRead) != 0) {
    return result;
  }

  entry->checked |= kMailCheckedRead;
  result.marked_read = true;

  bool cleared_pending_sender = false;
  for (auto& sender : next_mail_senders_) {
    if (!IsActivePendingMailSender(sender)) {
      continue;
    }
    const bool has_unread_match =
        std::any_of(inbox_.begin(), inbox_.end(),
                    [&sender](const MailEntry& candidate) {
                      return PendingSenderMatchesUnreadInboxMail(sender,
                                                                 candidate);
                    });
    if (!has_unread_match) {
      sender = {};
      cleared_pending_sender = true;
    }
  }
  result.cleared_pending_mail = cleared_pending_sender;

  if (HasPendingMail() &&
      std::none_of(inbox_.begin(), inbox_.end(), [](const MailEntry &candidate) {
        return (candidate.checked & kMailCheckedRead) == 0;
      })) {
    next_mail_time_ = -1.0f;
    result.cleared_pending_mail = true;
  }

  return result;
}

bool MailInteraction::ConsumeNextMailTimeQueryRequest() {
  const bool pending = next_mail_time_query_requested_;
  next_mail_time_query_requested_ = false;
  return pending;
}

bool MailInteraction::TryStartMailboxAction() {
  if (mail_request_pending_ || mailbox_guid_ == 0) {
    return false;
  }

  mail_request_pending_ = true;
  return true;
}

bool MailInteraction::TryStartInboxRemovalAction(MailEntry &entry) {
  if (entry.removal_request_pending) {
    return false;
  }

  if (!TryStartMailboxAction()) {
    return false;
  }

  entry.removal_request_pending = true;
  return true;
}

InboxRefreshRequestResult MailInteraction::TryStartInboxRefresh() {
  if (mail_request_pending_) {
    return InboxRefreshRequestResult::kBlockedPending;
  }
  if (mailbox_guid_ == 0) {
    return InboxRefreshRequestResult::kBlockedClosed;
  }

  const auto now = MailClock::now();
  if (now < next_inbox_refresh_allowed_at_) {
    return InboxRefreshRequestResult::kThrottled;
  }

  mail_request_pending_ = true;
  next_inbox_refresh_allowed_at_ = now + std::chrono::minutes(1);
  return InboxRefreshRequestResult::kStarted;
}

MailInteraction::AutoLootSequenceResult MailInteraction::StartAutoLootMailSequence(
    const MailEntry &entry, const bool can_take_attachments) {
  AutoLootSequenceResult result;
  if (mailbox_guid_ == 0) {
    return result;
  }

  if (entry.money != 0) {
    result.followups.push_back({
        .kind = MailFollowupKind::kTakeMoney,
        .mailbox_guid = mailbox_guid_,
        .mail_id = entry.message_id,
    });
  }

  if (!can_take_attachments) {
    result.show_attachment_autoloot_error = true;
    return result;
  }

  const auto step = BuildAutoLootAttachmentStep(entry);
  if (!step.has_value()) {
    return result;
  }

  mail_request_pending_ = true;
  if (step->queue_followup) {
    pending_auto_loot_mail_id_ = entry.message_id;
  }
  result.followups.push_back(std::move(step->command));
  return result;
}

MailInteraction::AutoLootSequenceResult MailInteraction::ContinueAutoLootMailSequence(
    const bool can_take_attachments) {
  AutoLootSequenceResult result;
  if (!pending_auto_loot_mail_id_.has_value()) {
    return result;
  }

  const std::uint32_t mail_id = *pending_auto_loot_mail_id_;
  pending_auto_loot_mail_id_.reset();

  const auto *entry = FindInboxMail(mail_id);
  if (entry == nullptr) {
    return result;
  }

  if (!can_take_attachments) {
    result.show_attachment_autoloot_error = true;
    return result;
  }

  const auto step = BuildAutoLootAttachmentStep(*entry);
  if (!step.has_value()) {
    return result;
  }

  mail_request_pending_ = true;
  if (step->queue_followup) {
    pending_auto_loot_mail_id_ = mail_id;
  }
  result.followups.push_back(std::move(step->command));
  return result;
}

void MailInteraction::ClearPendingAutoLootMailSequence() {
  pending_auto_loot_mail_id_.reset();
}

void MailInteraction::FinishPendingMailboxOperation() {
  mail_request_pending_ = false;
}

bool MailInteraction::CanComplainInboxItem(std::size_t zero_based_index,
                                       std::uint64_t active_player_guid,
                                       const SocialManager &social) const {
  if (active_player_guid == 0) {
    return false;
  }

  const auto *entry = GetInboxMail(zero_based_index);
  return entry != nullptr && IsComplaintableInboxMail(*entry, active_player_guid, social);
}

const MailItemInfo *MailInteraction::GetMailItem(const MailEntry &entry, std::uint32_t slot) const {
  if (slot == 0xFFFFFFFF) {
    const MailItemInfo *first_item = nullptr;
    for (const auto &item : entry.items) {
      if (item.item_entry == 0) {
        continue;
      }
      if (!first_item || item.index < first_item->index) {
        first_item = &item;
      }
    }
    return first_item;
  }

  if (slot >= kMaxInboxAttachmentSlots) {
    return nullptr;
  }

  for (const auto &item : entry.items) {
    if (item.index != slot) {
      continue;
    }
    return item.item_entry != 0 ? &item : nullptr;
  }

  return nullptr;
}

bool MailInteraction::MarkMailPermanent(std::uint32_t mail_id) {
  for (auto &entry : inbox_) {
    if (entry.message_id == mail_id) {
      entry.checked |= kMailCheckedCopied;
      return true;
    }
  }
  return false;
}

bool MailInteraction::DeleteMailFromInbox(std::uint32_t mail_id) {
  auto it = std::find_if(inbox_.begin(), inbox_.end(),
                         [mail_id](const MailEntry &e) { return e.message_id == mail_id; });
  if (it != inbox_.end()) {
    real_count_ = (real_count_ > 0) ? real_count_ - 1 : 0;
    inbox_.erase(it);
    return true;
  }
  return false;
}

void MailInteraction::PrimeAuctionMailItemQuery(const MailEntry &entry, QueryCache &query_cache) {
  if (entry.message_type != MailType::kAuction) {
    return;
  }

  const auto item_id = ParseAuctionMailItemId(entry.subject);
  if (!item_id.has_value()) {
    return;
  }

  (void)query_cache.GetOrRequestItemTemplate(
      *item_id,
      QueryCache::QueryRequestOptions{
          .callback_key = BuildAuctionMailItemQueryCallbackKey(entry.message_id),
          .callback =
              [this](const bool success) {
                if (success) {
                  QueueInboxUpdateEvent();
                }
              },
      });
}

bool MailInteraction::ShouldAutoDeleteEmptiedMail(const MailEntry &entry) const {
  if (entry.removal_request_pending || entry.money != 0 || !entry.items.empty()) {
    return false;
  }

  return entry.message_type == MailType::kAuction ||
         (entry.checked & kMailCheckedCodPayment) != 0 ||
         (entry.body.empty() && entry.mail_template_id == 0);
}

MailEntry *MailInteraction::FindInboxMail(std::uint32_t mail_id) {
  auto it = std::find_if(inbox_.begin(), inbox_.end(),
                         [mail_id](const MailEntry &entry) { return entry.message_id == mail_id; });
  return it == inbox_.end() ? nullptr : &*it;
}

const MailEntry *MailInteraction::FindInboxMail(std::uint32_t mail_id) const {
  auto it = std::find_if(inbox_.begin(), inbox_.end(),
                         [mail_id](const MailEntry &entry) { return entry.message_id == mail_id; });
  return it == inbox_.end() ? nullptr : &*it;
}

void MailInteraction::RemoveInboxItemByGuid(MailEntry &entry, std::uint32_t item_guid_low) {
  entry.items.erase(std::remove_if(entry.items.begin(), entry.items.end(),
                                   [item_guid_low](const MailItemInfo &item) {
                                     return item.item_entry != 0 &&
                                            item.item_guid_low == item_guid_low;
                                   }),
                    entry.items.end());
}

void MailInteraction::QueueAutoDelete(MailEntry &entry, MailDeleteReason reason,
                                      MailSendChanges& changes) {
  if (!ShouldAutoDeleteEmptiedMail(entry)) {
    return;
  }

  entry.removal_request_pending = true;
  changes.followups.push_back({
      .kind = MailFollowupKind::kDelete,
      .mailbox_guid = mailbox_guid_,
      .mail_id = entry.message_id,
      .delete_reason = reason,
  });

  const auto position = static_cast<std::size_t>(&entry - inbox_.data());
  if (position < inbox_.size()) {
    changes.closed_inbox_indices.push_back(
        static_cast<std::uint32_t>(position) + 1u);
  }
}

void MailInteraction::Shutdown() {
  ResetRuntimeState();
  initialized_ = false;

}

void MailInteraction::CloseMailbox(bool full_reset) {
  compose_.SetSendMailShowing(false);
  ResetCompose();

  if (full_reset) {
    mailbox_guid_ = 0;
    inbox_.clear();
    real_count_ = 0;
    next_mail_time_ = -1.0f;
    next_mail_senders_.clear();
    pending_mail_update_event_pending_ = false;
    mail_inbox_update_event_pending_ = false;
    send_info_update_event_pending_ = false;
    next_mail_time_query_requested_ = false;
    refresh_next_mail_time_on_close_ = false;
    mail_request_pending_ = false;
    pending_auto_loot_mail_id_.reset();
    next_inbox_refresh_allowed_at_ = {};
  } else if (mailbox_guid_ != 0) {
    mailbox_guid_ = 0;
    pending_auto_loot_mail_id_.reset();
    if (refresh_next_mail_time_on_close_) {
      QueueNextMailTimeQuery();
    }
  }
}

void MailInteraction::ResetCompose() {
  compose_.ClearDraft();
  compose_.SetComposeLocked(false);
}

void MailInteraction::Initialize() {
  if (initialized_)
    return;

  ResetRuntimeState();

  inbox_.reserve(8);

  initialized_ = true;
}

bool MailInteraction::IsZeroTimer(float value) {
  return std::fabs(value) < kPendingMailTimerEpsilon;
}

bool MailInteraction::IsActivePendingMailSender(const NextMailTimeSender &sender) {
  return sender.sender_guid != 0 || sender.sender_entry != 0;
}

bool MailInteraction::IsComplaintableInboxMail(const MailEntry &entry, std::uint64_t active_player_guid,
                                           const SocialManager &social) const {
  if (entry.message_type != MailType::kNormal) {
    return false;
  }

  if ((entry.checked & kMailCheckedReturned) != 0) {
    return false;
  }

  if (entry.stationery == 61) {
    return false;
  }

  if (entry.sender_guid == active_player_guid) {
    return false;
  }

  return !social.HasContact(ObjectGuid(entry.sender_guid));
}

std::optional<MailInteraction::AutoLootAttachmentStep> MailInteraction::BuildAutoLootAttachmentStep(
    const MailEntry &entry) const {
  if (mailbox_guid_ == 0) {
    return std::nullopt;
  }

  const auto *item = GetMailItem(entry, 0xFFFFFFFFu);
  if (item == nullptr || item->item_guid_low == 0) {
    return std::nullopt;
  }

  bool queue_followup = false;
  for (const auto &candidate : entry.items) {
    if (candidate.item_entry != 0 && candidate.index > item->index) {
      queue_followup = true;
      break;
    }
  }

  return AutoLootAttachmentStep{
      .command =
          {
              .kind = MailFollowupKind::kTakeItem,
              .mailbox_guid = mailbox_guid_,
              .mail_id = entry.message_id,
              .item_guid_low = item->item_guid_low,
          },
      .queue_followup = queue_followup,
  };
}

void MailInteraction::QueuePendingMailUpdateEvent() {
  pending_mail_update_event_pending_ = true;
}

void MailInteraction::QueueMailInboxUpdate() {
  mail_inbox_update_event_pending_ = true;
}

void MailInteraction::QueueNextMailTimeQuery() {
  next_mail_time_ = -1.0f;
  refresh_next_mail_time_on_close_ = false;
  next_mail_time_query_requested_ = true;
}

}

#include "openwow/game/commerce/mail/mail_compose_state.h"

#include <algorithm>
#include <utility>

namespace openwow::game {

void MailComposeState::SetSendMailShowing(const bool showing) {
  send_mail_showing_ = showing;
}

bool MailComposeState::IsSendMailShowing() const {
  return send_mail_showing_;
}

void MailComposeState::SetDraft(const MailDraft& draft) {
  draft_ = draft;
}

MailDraft MailComposeState::GetDraft() const {
  return draft_;
}

void MailComposeState::ClearDraft() {
  draft_ = {};
}

void MailComposeState::SetComposeLocked(const bool locked) {
  compose_locked_ = locked;
}

bool MailComposeState::IsComposeLocked() const {
  return compose_locked_;
}

bool MailComposeState::HasDraftAttachmentItemGuid(
    const std::uint64_t item_guid) const {
  if (item_guid == 0) {
    return false;
  }
  return std::any_of(
      draft_.attachments.begin(), draft_.attachments.end(),
      [item_guid](const MailAttachment& attachment) {
        return attachment.item_guid == item_guid;
      });
}

void MailComposeState::SetPackages(std::vector<MailPackageInfo> packages) {
  packages_ = std::move(packages);
  if (draft_.package_id == 0 && !packages_.empty()) {
    draft_.package_id = packages_.front().id;
  }
}

std::size_t MailComposeState::GetPackageCount() const {
  return packages_.size();
}

std::optional<MailPackageInfo> MailComposeState::GetPackageByIndex(
    const std::size_t one_based_index) const {
  if (one_based_index == 0 || one_based_index > packages_.size()) {
    return std::nullopt;
  }
  return packages_[one_based_index - 1];
}

std::optional<MailPackageInfo> MailComposeState::GetPackageById(
    const std::uint32_t package_id) const {
  const auto found = std::find_if(
      packages_.begin(), packages_.end(),
      [package_id](const MailPackageInfo& package) {
        return package.id == package_id;
      });
  return found == packages_.end() ? std::nullopt
                                  : std::optional<MailPackageInfo>(*found);
}

void MailComposeState::SelectPackageByIndex(const std::size_t one_based_index) {
  draft_.package_id =
      one_based_index > 0 && one_based_index <= packages_.size()
          ? packages_[one_based_index - 1].id
          : 0;
}

void MailComposeState::Reset() {
  send_mail_showing_ = false;
  draft_ = {};
  compose_locked_ = false;
  packages_.clear();
}

}

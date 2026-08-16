#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct MailAttachment {
  std::uint32_t slot = 0;
  std::uint32_t item_id = 0;
  std::uint32_t item_count = 0;
  std::uint32_t enchant_id = 0;
  std::array<std::uint32_t, 3> gem_ids{};
  std::int32_t random_property_id = 0;
  std::uint32_t suffix_factor = 0;
  std::uint32_t charges = 0;
  std::uint64_t item_guid = 0;
};

struct MailPackageInfo {
  std::uint32_t id = 0;
  std::string icon_path;
  std::uint32_t price = 0;
  std::string name;
};

struct MailDraft {
  std::string recipient;
  std::string subject;
  std::string body;
  std::uint32_t money = 0;
  std::uint32_t cod = 0;
  std::uint32_t stationery = 0;
  std::uint32_t package_id = 0;
  std::vector<MailAttachment> attachments;
};

class MailComposeState {
 public:
  void SetSendMailShowing(bool showing);
  [[nodiscard]] bool IsSendMailShowing() const;

  void SetDraft(const MailDraft& draft);
  [[nodiscard]] MailDraft GetDraft() const;
  void ClearDraft();
  void SetComposeLocked(bool locked);
  [[nodiscard]] bool IsComposeLocked() const;
  [[nodiscard]] bool HasDraftAttachmentItemGuid(std::uint64_t item_guid) const;

  void SetPackages(std::vector<MailPackageInfo> packages);
  [[nodiscard]] std::size_t GetPackageCount() const;
  [[nodiscard]] std::optional<MailPackageInfo> GetPackageByIndex(
      std::size_t one_based_index) const;
  [[nodiscard]] std::optional<MailPackageInfo> GetPackageById(
      std::uint32_t package_id) const;
  void SelectPackageByIndex(std::size_t one_based_index);

  void Reset();

 private:
  bool send_mail_showing_ = false;
  MailDraft draft_;
  bool compose_locked_ = false;
  std::vector<MailPackageInfo> packages_;
};

}

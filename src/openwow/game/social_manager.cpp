
#include "openwow/game/social_manager.h"

#include "openwow/core/storm_string.h"

#include <algorithm>
#include <utility>

namespace openwow::game {

bool SocialManager::HandleContactList(const std::uint8_t *data, std::size_t len) {
  PacketReader reader(data, len);

  std::uint32_t flags;
  if (!reader.ReadU32(flags))
    return false;

  std::uint32_t contact_count;
  if (!reader.ReadU32(contact_count))
    return false;
  constexpr std::size_t kMinimumContactWireSize = 13;
  if (contact_count > reader.Remaining() / kMinimumContactWireSize) {
    return false;
  }

  SocialManager parsed = *this;
  parsed.last_friend_status_update_.reset();
  parsed.ClearRequestedFlags(flags);
  constexpr std::size_t kMaximumDistinctContacts =
      kFriendLimit + kIgnoreLimit + kMuteLimit;
  parsed.contacts_.reserve(std::min(
      kMaximumDistinctContacts,
      std::max(parsed.contacts_.size(), static_cast<std::size_t>(contact_count))));
  std::size_t friend_count = parsed.GetFriends().size();
  std::size_t ignore_count = parsed.GetIgnored().size();
  std::size_t mute_count = parsed.GetMuted().size();

  for (std::uint32_t i = 0; i < contact_count; ++i) {
    std::uint64_t guid_raw;
    if (!reader.ReadU64(guid_raw))
      return false;
    const ObjectGuid guid(guid_raw);

    std::uint32_t contact_flags;
    if (!reader.ReadU32(contact_flags))
      return false;

    std::string note;
    if (!reader.ReadCString(note))
      return false;

    const bool has_capacity =
        ((contact_flags & static_cast<std::uint32_t>(SocialFlag::kFriend)) != 0 &&
         friend_count < kFriendLimit) ||
        ((contact_flags & static_cast<std::uint32_t>(SocialFlag::kIgnored)) != 0 &&
         ignore_count < kIgnoreLimit) ||
        ((contact_flags & static_cast<std::uint32_t>(SocialFlag::kMuted)) != 0 &&
         mute_count < kMuteLimit);
    ContactInfo overflow_contact{.guid = guid};
    ContactInfo *contact_ptr = parsed.FindContactMut(guid);
    if (contact_ptr == nullptr && has_capacity) {
      contact_ptr = &parsed.GetOrCreateContact(guid);
    }
    ContactInfo &contact = contact_ptr != nullptr ? *contact_ptr : overflow_contact;

    if ((contact_flags & static_cast<std::uint32_t>(SocialFlag::kFriend)) != 0) {
      std::uint8_t status_raw = 0;
      std::uint32_t area = 0;
      std::uint32_t level = 0;
      std::uint32_t player_class = 0;
      if (!reader.ReadU8(status_raw))
        return false;
      if (status_raw != 0) {
        if (!reader.ReadU32(area) || !reader.ReadU32(level) ||
            !reader.ReadU32(player_class))
          return false;
      }

      const bool already_friend = HasSocialFlag(contact.flags, SocialFlag::kFriend);
      if (already_friend || friend_count < kFriendLimit) {
        contact.flags = static_cast<SocialFlag>(static_cast<std::uint32_t>(contact.flags) |
                                                static_cast<std::uint32_t>(SocialFlag::kFriend));
        contact.note = std::move(note);
        contact.friend_delete_pending = false;
        contact.status = static_cast<FriendStatus>(status_raw);
        contact.area = area;
        contact.level = level;
        contact.player_class = player_class;
        friend_count += already_friend ? 0u : 1u;
      }
    }

    if ((contact_flags & static_cast<std::uint32_t>(SocialFlag::kIgnored)) != 0) {
      const bool already_ignored = HasSocialFlag(contact.flags, SocialFlag::kIgnored);
      if (already_ignored || ignore_count < kIgnoreLimit) {
        contact.flags = static_cast<SocialFlag>(static_cast<std::uint32_t>(contact.flags) |
                                                static_cast<std::uint32_t>(SocialFlag::kIgnored));
        contact.ignore_delete_pending = false;
        ignore_count += already_ignored ? 0u : 1u;
      }
    }

    if ((contact_flags & static_cast<std::uint32_t>(SocialFlag::kMuted)) != 0) {
      const bool already_muted = HasSocialFlag(contact.flags, SocialFlag::kMuted);
      if (already_muted || mute_count < kMuteLimit) {
        contact.flags = static_cast<SocialFlag>(static_cast<std::uint32_t>(contact.flags) |
                                                static_cast<std::uint32_t>(SocialFlag::kMuted));
        contact.mute_delete_pending = false;
        mute_count += already_muted ? 0u : 1u;
      }
    }
  }

  parsed.contacts_.erase(
      std::remove_if(parsed.contacts_.begin(), parsed.contacts_.end(),
                     [](const ContactInfo &contact) {
                       return static_cast<std::uint32_t>(contact.flags) == 0;
                     }),
      parsed.contacts_.end());
  contacts_ = std::move(parsed.contacts_);
  last_friend_status_update_.reset();
  return true;
}

bool SocialManager::HandleFriendStatus(const std::uint8_t *data, std::size_t len) {
  PacketReader reader(data, len);

  std::uint8_t result_raw;
  if (!reader.ReadU8(result_raw))
    return false;
  auto result = static_cast<FriendsResult>(result_raw);

  std::uint64_t guid_raw;
  if (!reader.ReadU64(guid_raw))
    return false;
  ObjectGuid guid(guid_raw);
  SocialManager parsed = *this;
  parsed.last_friend_status_update_ = FriendStatusUpdate{result, guid};

  switch (result_raw) {
  case 0x02: {
    std::uint8_t status_raw = 0;
    std::uint32_t area = 0;
    std::uint32_t level = 0;
    std::uint32_t player_class = 0;
    if (!reader.ReadU8(status_raw) || !reader.ReadU32(area) ||
        !reader.ReadU32(level) || !reader.ReadU32(player_class)) {
      return false;
    }
    auto *contact = parsed.FindContactMut(guid);
    if (contact != nullptr) {
      contact->status = static_cast<FriendStatus>(status_raw);
      contact->area = area;
      contact->level = level;
      contact->player_class = player_class;
      contact->friend_delete_pending = false;
    }
    break;
  }
  case 0x03: {
    auto *contact = parsed.FindContactMut(guid);
    if (contact != nullptr) {
      contact->status = FriendStatus::kOffline;
      contact->area = 0;
      contact->level = 0;
      contact->player_class = 0;
      contact->friend_delete_pending = false;
    }
    break;
  }
  case 0x05: {
    if (auto *contact = parsed.FindContactMut(guid); contact != nullptr) {
      contact->flags = static_cast<SocialFlag>(static_cast<std::uint32_t>(contact->flags) &
                                               ~static_cast<std::uint32_t>(SocialFlag::kFriend));
      contact->note.clear();
      contact->status = FriendStatus::kOffline;
      contact->area = 0;
      contact->level = 0;
      contact->player_class = 0;
      contact->friend_delete_pending = false;
    }
    parsed.contacts_.erase(
        std::remove_if(parsed.contacts_.begin(), parsed.contacts_.end(),
                       [](const ContactInfo &contact) {
                         return static_cast<std::uint32_t>(contact.flags) == 0;
                       }),
        parsed.contacts_.end());
    break;
  }
  case 0x06:
  case 0x07: {
    std::string note;
    if (!reader.ReadCString(note)) {
      return false;
    }
    std::uint8_t status_raw = 0;
    std::uint32_t area = 0;
    std::uint32_t level = 0;
    std::uint32_t player_class = 0;
    if (result_raw == 0x06) {
      if (!reader.ReadU8(status_raw)) {
        return false;
      }
      if (!reader.ReadU32(area) || !reader.ReadU32(level) ||
          !reader.ReadU32(player_class)) {
        return false;
      }
    }

    ContactInfo *contact = parsed.FindContactMut(guid);
    const bool already_friend =
        contact != nullptr && HasSocialFlag(contact->flags, SocialFlag::kFriend);
    if (already_friend || parsed.GetFriends().size() < kFriendLimit) {
      if (contact == nullptr) {
        contact = &parsed.GetOrCreateContact(guid);
      }
      contact->flags = static_cast<SocialFlag>(static_cast<std::uint32_t>(contact->flags) |
                                               static_cast<std::uint32_t>(SocialFlag::kFriend));
      contact->note = std::move(note);
      contact->friend_delete_pending = false;
      contact->status = static_cast<FriendStatus>(status_raw);
      contact->area = area;
      contact->level = level;
      contact->player_class = player_class;
    }
    break;
  }
  case 0x0F: {
    ContactInfo *contact = parsed.FindContactMut(guid);
    const bool already_ignored =
        contact != nullptr && HasSocialFlag(contact->flags, SocialFlag::kIgnored);
    if (already_ignored || parsed.GetIgnored().size() < kIgnoreLimit) {
      if (contact == nullptr) {
        contact = &parsed.GetOrCreateContact(guid);
      }
      contact->flags = static_cast<SocialFlag>(static_cast<std::uint32_t>(contact->flags) |
                                               static_cast<std::uint32_t>(SocialFlag::kIgnored));
      contact->ignore_delete_pending = false;
    }
    break;
  }
  case 0x10: {
    if (auto *contact = parsed.FindContactMut(guid); contact != nullptr) {
      contact->flags = static_cast<SocialFlag>(static_cast<std::uint32_t>(contact->flags) &
                                               ~static_cast<std::uint32_t>(SocialFlag::kIgnored));
      contact->ignore_delete_pending = false;
    }
    parsed.contacts_.erase(
        std::remove_if(parsed.contacts_.begin(), parsed.contacts_.end(),
                       [](const ContactInfo &contact) {
                         return static_cast<std::uint32_t>(contact.flags) == 0;
                       }),
        parsed.contacts_.end());
    break;
  }
  case 0x16: {
    ContactInfo *contact = parsed.FindContactMut(guid);
    const bool already_muted =
        contact != nullptr && HasSocialFlag(contact->flags, SocialFlag::kMuted);
    if (already_muted || parsed.GetMuted().size() < kMuteLimit) {
      if (contact == nullptr) {
        contact = &parsed.GetOrCreateContact(guid);
      }
      contact->flags = static_cast<SocialFlag>(static_cast<std::uint32_t>(contact->flags) |
                                               static_cast<std::uint32_t>(SocialFlag::kMuted));
      contact->mute_delete_pending = false;
    }
    break;
  }
  case 0x17: {
    if (auto *contact = parsed.FindContactMut(guid); contact != nullptr) {
      contact->flags = static_cast<SocialFlag>(static_cast<std::uint32_t>(contact->flags) &
                                               ~static_cast<std::uint32_t>(SocialFlag::kMuted));
      contact->mute_delete_pending = false;
    }
    parsed.contacts_.erase(
        std::remove_if(parsed.contacts_.begin(), parsed.contacts_.end(),
                       [](const ContactInfo &contact) {
                         return static_cast<std::uint32_t>(contact.flags) == 0;
                       }),
        parsed.contacts_.end());
    break;
  }
  case 0x1A: {
    std::uint8_t status_raw = 0;
    if (!reader.ReadU8(status_raw)) {
      return false;
    }
    auto *contact = parsed.FindContactMut(guid);
    if (contact != nullptr) {
      contact->status = static_cast<FriendStatus>(status_raw);
    }
    break;
  }
  case 0x1B: {
    std::uint32_t area = 0;
    if (!reader.ReadU32(area)) {
      return false;
    }
    auto *contact = parsed.FindContactMut(guid);
    if (contact != nullptr) {
      contact->area = area;
    }
    break;
  }
  default:
    break;
  }

  contacts_ = std::move(parsed.contacts_);
  last_friend_status_update_ = parsed.last_friend_status_update_;
  return true;
}

net::wotlk::WorldPacket SocialManager::BuildAddFriend(const std::string &name,
                                                      const std::string &note) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_ADD_FRIEND);
  pkt.AppendString(name.c_str());
  pkt.AppendString(note.c_str());
  return pkt;
}

net::wotlk::WorldPacket SocialManager::BuildDelFriend(const ObjectGuid &guid) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_DEL_FRIEND);
  pkt.AppendU64(guid.GetRawValue());
  return pkt;
}

net::wotlk::WorldPacket SocialManager::BuildAddIgnore(const std::string &name) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_ADD_IGNORE);
  pkt.AppendString(name.c_str());
  return pkt;
}

net::wotlk::WorldPacket SocialManager::BuildDelIgnore(const ObjectGuid &guid) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_DEL_IGNORE);
  pkt.AppendU64(guid.GetRawValue());
  return pkt;
}

net::wotlk::WorldPacket SocialManager::BuildAddMute(const std::string &name) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_ADD_VOICE_IGNORE);
  pkt.AppendString(name.c_str());
  return pkt;
}

net::wotlk::WorldPacket SocialManager::BuildDelMute(const ObjectGuid &guid) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_DEL_VOICE_IGNORE);
  pkt.AppendU64(guid.GetRawValue());
  return pkt;
}

net::wotlk::WorldPacket SocialManager::BuildSetContactNotes(const ObjectGuid &guid,
                                                            const std::string &note) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_SET_CONTACT_NOTES);
  pkt.AppendU64(guid.GetRawValue());
  pkt.AppendString(note.c_str());
  return pkt;
}

std::vector<const ContactInfo *> SocialManager::GetFriends() const {
  std::vector<const ContactInfo *> result;
  for (const auto &c : contacts_) {
    if (IsVisible(c, SocialFlag::kFriend)) {
      result.push_back(&c);
    }
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const ContactInfo *lhs, const ContactInfo *rhs) {
                     if (lhs == nullptr || rhs == nullptr) {
                       return lhs != nullptr;
                     }

                     const bool lhs_online = lhs->status != FriendStatus::kOffline;
                     const bool rhs_online = rhs->status != FriendStatus::kOffline;
                     if (lhs_online != rhs_online) {
                       return lhs_online;
                     }

                     return NameComesBefore(lhs->display_name, rhs->display_name);
                   });
  return result;
}

std::vector<const ContactInfo *> SocialManager::GetIgnored() const {
  std::vector<const ContactInfo *> result;
  for (const auto &c : contacts_) {
    if (IsVisible(c, SocialFlag::kIgnored)) {
      result.push_back(&c);
    }
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const ContactInfo *lhs, const ContactInfo *rhs) {
                     if (lhs == nullptr || rhs == nullptr) {
                       return lhs != nullptr;
                     }

                     return NameComesBefore(lhs->display_name, rhs->display_name);
                   });
  return result;
}

std::vector<const ContactInfo *> SocialManager::GetMuted() const {
  std::vector<const ContactInfo *> result;
  for (const auto &c : contacts_) {
    if (IsVisible(c, SocialFlag::kMuted)) {
      result.push_back(&c);
    }
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const ContactInfo *lhs, const ContactInfo *rhs) {
                     if (lhs == nullptr || rhs == nullptr) {
                       return lhs != nullptr;
                     }

                     return NameComesBefore(lhs->display_name, rhs->display_name);
                   });
  return result;
}

ObjectGuid SocialManager::GuidAtLuaIndex(
    const std::vector<const ContactInfo *> &contacts, const std::uint32_t index) {
  if (index < 1 || static_cast<std::size_t>(index) > contacts.size()) {
    return {};
  }
  const auto *contact = contacts[static_cast<std::size_t>(index - 1)];
  return contact != nullptr ? contact->guid : ObjectGuid{};
}

std::int32_t SocialManager::LuaIndexOfGuid(
    const std::vector<const ContactInfo *> &contacts, const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return 0;
  }
  const auto found = std::find_if(
      contacts.begin(), contacts.end(),
      [guid](const ContactInfo *contact) {
        return contact != nullptr && contact->guid == guid;
      });
  return found == contacts.end()
             ? 0
             : static_cast<std::int32_t>(
                   std::distance(contacts.begin(), found) + 1);
}

void SocialManager::SelectFriendByLuaIndex(const std::uint32_t index) {
  selected_friend_ = GuidAtLuaIndex(GetFriends(), index);
}

void SocialManager::SelectIgnoredByLuaIndex(const std::uint32_t index) {
  selected_ignored_ = GuidAtLuaIndex(GetIgnored(), index);
}

void SocialManager::SelectMutedByLuaIndex(const std::uint32_t index) {
  selected_muted_ = GuidAtLuaIndex(GetMuted(), index);
}

std::int32_t SocialManager::GetSelectedFriendLuaIndex() const {
  return LuaIndexOfGuid(GetFriends(), selected_friend_);
}

std::int32_t SocialManager::GetSelectedIgnoredLuaIndex() const {
  return LuaIndexOfGuid(GetIgnored(), selected_ignored_);
}

std::int32_t SocialManager::GetSelectedMutedLuaIndex() const {
  return LuaIndexOfGuid(GetMuted(), selected_muted_);
}

const ContactInfo *SocialManager::FindContact(const ObjectGuid &guid) const {
  for (const auto &c : contacts_) {
    if (c.guid == guid)
      return &c;
  }
  return nullptr;
}

bool SocialManager::HasContact(const ObjectGuid &guid) const {
  return FindContact(guid) != nullptr;
}

bool SocialManager::IsFriend(const ObjectGuid &guid) const {
  auto *contact = FindContact(guid);
  return contact != nullptr && IsVisible(*contact, SocialFlag::kFriend);
}

bool SocialManager::IsIgnored(const ObjectGuid &guid) const {
  auto *contact = FindContact(guid);
  return contact != nullptr && IsVisible(*contact, SocialFlag::kIgnored);
}

bool SocialManager::IsMuted(const ObjectGuid &guid) const {
  if (guid.IsEmpty()) {
    return false;
  }
  auto *contact = FindContact(guid);
  if (contact != nullptr && IsVisible(*contact, SocialFlag::kMuted)) {
    return true;
  }

  return complaint_status_ == kComplaintStatusEnabled &&
         HasRecentComplaintGuid(guid.GetRawValue());
}

bool SocialManager::IsIgnoredOrMuted(const ObjectGuid &guid) const {
  if (guid.IsEmpty()) {
    return false;
  }

  if (IsIgnored(guid)) {
    return true;
  }
  return IsMuted(guid);
}

bool SocialManager::IsDeletePending(const ObjectGuid &guid, const SocialFlag flag) const {
  const auto *contact = FindContact(guid);
  if (contact == nullptr) {
    return false;
  }

  switch (flag) {
  case SocialFlag::kNone:
    return false;
  case SocialFlag::kFriend:
    return contact->friend_delete_pending;
  case SocialFlag::kIgnored:
    return contact->ignore_delete_pending;
  case SocialFlag::kMuted:
    return contact->mute_delete_pending;
  }

  return false;
}

bool SocialManager::SetDisplayName(const ObjectGuid &guid, std::string display_name) {
  auto *contact = FindContactMut(guid);
  if (contact == nullptr) {
    return false;
  }

  contact->display_name = std::move(display_name);
  return true;
}

bool SocialManager::MarkDeletePending(const ObjectGuid &guid, const SocialFlag flag) {
  auto *contact = FindContactMut(guid);
  if (contact == nullptr) {
    return false;
  }

  switch (flag) {
  case SocialFlag::kNone:
    return false;
  case SocialFlag::kFriend:
    contact->friend_delete_pending = true;
    break;
  case SocialFlag::kIgnored:
    contact->ignore_delete_pending = true;
    break;
  case SocialFlag::kMuted:
    contact->mute_delete_pending = true;
    break;
  }

  return true;
}

bool SocialManager::HasRecentComplaintGuid(const std::uint64_t guid) const {
  if (guid == 0) {
    return false;
  }

  return std::find(recent_complaint_guids_.begin(), recent_complaint_guids_.end(), guid) !=
         recent_complaint_guids_.end();
}

bool SocialManager::ClearFriendReferAFriendFlag(const ObjectGuid &guid) {
  auto *contact = FindContactMut(guid);
  if (contact == nullptr || !HasSocialFlag(contact->flags, SocialFlag::kFriend) ||
      (static_cast<std::uint8_t>(contact->status) &
       static_cast<std::uint8_t>(FriendStatus::kRaf)) == 0) {
    return false;
  }

  contact->status = static_cast<FriendStatus>(static_cast<std::uint8_t>(contact->status) &
                                              ~static_cast<std::uint8_t>(FriendStatus::kRaf));
  return true;
}

void SocialManager::RememberRecentComplaintGuid(const std::uint64_t guid) {
  if (guid == 0) {
    return;
  }

  recent_complaint_guids_.erase(
      std::remove(recent_complaint_guids_.begin(), recent_complaint_guids_.end(), guid),
      recent_complaint_guids_.end());
  recent_complaint_guids_.insert(recent_complaint_guids_.begin(), guid);
  if (recent_complaint_guids_.size() > kRecentComplaintLimit) {
    recent_complaint_guids_.resize(kRecentComplaintLimit);
  }
}

void SocialManager::Clear() {
  contacts_.clear();
  recent_complaint_guids_.clear();
  last_friend_status_update_.reset();
  complaint_status_ = 0;
  selected_friend_ = {};
  selected_ignored_ = {};
  selected_muted_ = {};
  who_results_to_ui_ = false;
}

ContactInfo *SocialManager::FindContactMut(const ObjectGuid &guid) {
  for (auto &c : contacts_) {
    if (c.guid == guid)
      return &c;
  }
  return nullptr;
}

ContactInfo &SocialManager::GetOrCreateContact(const ObjectGuid &guid) {
  if (auto *contact = FindContactMut(guid); contact != nullptr) {
    return *contact;
  }

  contacts_.push_back(ContactInfo{.guid = guid});
  return contacts_.back();
}

void SocialManager::ClearRequestedFlags(const std::uint32_t requested_flags) {
  contacts_.erase(
      std::remove_if(
          contacts_.begin(), contacts_.end(),
          [requested_flags](ContactInfo &contact) {
            if ((requested_flags & static_cast<std::uint32_t>(SocialFlag::kFriend)) != 0 &&
                HasSocialFlag(contact.flags, SocialFlag::kFriend)) {
              contact.flags =
                  static_cast<SocialFlag>(static_cast<std::uint32_t>(contact.flags) &
                                          ~static_cast<std::uint32_t>(SocialFlag::kFriend));
              contact.note.clear();
              contact.status = FriendStatus::kOffline;
              contact.area = 0;
              contact.level = 0;
              contact.player_class = 0;
              contact.friend_delete_pending = false;
            }

            if ((requested_flags & static_cast<std::uint32_t>(SocialFlag::kIgnored)) != 0 &&
                HasSocialFlag(contact.flags, SocialFlag::kIgnored)) {
              contact.flags =
                  static_cast<SocialFlag>(static_cast<std::uint32_t>(contact.flags) &
                                          ~static_cast<std::uint32_t>(SocialFlag::kIgnored));
              contact.ignore_delete_pending = false;
            }

            if ((requested_flags & static_cast<std::uint32_t>(SocialFlag::kMuted)) != 0 &&
                HasSocialFlag(contact.flags, SocialFlag::kMuted)) {
              contact.flags =
                  static_cast<SocialFlag>(static_cast<std::uint32_t>(contact.flags) &
                                          ~static_cast<std::uint32_t>(SocialFlag::kMuted));
              contact.mute_delete_pending = false;
            }

            return static_cast<std::uint32_t>(contact.flags) == 0;
          }),
      contacts_.end());
}

bool SocialManager::IsVisible(const ContactInfo &contact, const SocialFlag flag) {
  if (!HasSocialFlag(contact.flags, flag)) {
    return false;
  }

  switch (flag) {
  case SocialFlag::kNone:
    return false;
  case SocialFlag::kFriend:
    return !contact.friend_delete_pending;
  case SocialFlag::kIgnored:
    return !contact.ignore_delete_pending;
  case SocialFlag::kMuted:
    return !contact.mute_delete_pending;
  }

  return false;
}

bool SocialManager::NameComesBefore(const std::string &lhs, const std::string &rhs) {
  if (lhs.empty() || rhs.empty()) {
    if (lhs.empty() == rhs.empty()) {
      return false;
    }
    return !lhs.empty();
  }

  return openwow::core::SStrCmpUTF8NoCase(lhs.c_str(), rhs.c_str(), 0x7FFFFFFF) < 0;
}

}

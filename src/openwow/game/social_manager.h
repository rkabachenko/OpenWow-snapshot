#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class FriendStatus : std::uint8_t {
  kOffline = 0x00,
  kOnline = 0x01,
  kAfk = 0x02,
  kDnd = 0x04,
  kRaf = 0x08,
};

enum class SocialFlag : std::uint32_t {
  kNone = 0x00,
  kFriend = 0x01,
  kIgnored = 0x02,
  kMuted = 0x04,
};

inline SocialFlag operator|(SocialFlag a, SocialFlag b) {
  return static_cast<SocialFlag>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline bool HasSocialFlag(SocialFlag field, SocialFlag flag) {
  return (static_cast<std::uint32_t>(field) & static_cast<std::uint32_t>(flag)) != 0;
}

enum class FriendsResult : std::uint8_t {
  kDbError = 0x00,
  kListFull = 0x01,
  kOnline = 0x02,
  kOffline = 0x03,
  kNotFound = 0x04,
  kRemoved = 0x05,
  kAddedOnline = 0x06,
  kAddedOffline = 0x07,
  kAlready = 0x08,
  kSelf = 0x09,
  kEnemy = 0x0A,
  kIgnoreListFull = 0x0B,
  kIgnoreSelf = 0x0C,
  kIgnoreNotFound = 0x0D,
  kIgnoreAlready = 0x0E,
  kIgnoreAdded = 0x0F,
  kIgnoreRemoved = 0x10,
  kMuteSelf = 0x11,
  kMuteNotFound = 0x12,
  kMuteAlready = 0x13,
  kMuteAdded = 0x15,
  kMuteAddedOnline = 0x16,
  kMuteRemoved = 0x17,
  kMuteListFull = 0x18,
  kMuteAmbiguous = 0x19,
  kFriendAlreadyOnline = 0x1A,
  kFriendAreaChanged = 0x1B,
};

struct ContactInfo {
  ObjectGuid guid;
  SocialFlag flags{SocialFlag::kNone};
  std::string note;
  std::string display_name;
  FriendStatus status{FriendStatus::kOffline};
  std::uint32_t area{0};
  std::uint32_t level{0};
  std::uint32_t player_class{0};
  bool friend_delete_pending{false};
  bool ignore_delete_pending{false};
  bool mute_delete_pending{false};
};

struct FriendStatusUpdate {
  FriendsResult result{FriendsResult::kDbError};
  ObjectGuid guid;
};

class SocialManager {
public:
  static constexpr std::size_t kFriendLimit = 100;
  static constexpr std::size_t kIgnoreLimit = 50;
  static constexpr std::size_t kMuteLimit = 50;
  static constexpr std::size_t kNoteMaxLen = 48;
  static constexpr std::size_t kRecentComplaintLimit = 32;
  static constexpr std::uint8_t kComplaintStatusEnabled = 2;

  SocialManager() = default;

  bool HandleContactList(const std::uint8_t *data, std::size_t len);

  bool HandleFriendStatus(const std::uint8_t *data, std::size_t len);

  static net::wotlk::WorldPacket BuildAddFriend(const std::string &name,
                                                const std::string &note = "");
  static net::wotlk::WorldPacket BuildDelFriend(const ObjectGuid &guid);
  static net::wotlk::WorldPacket BuildAddIgnore(const std::string &name);
  static net::wotlk::WorldPacket BuildDelIgnore(const ObjectGuid &guid);
  static net::wotlk::WorldPacket BuildAddMute(const std::string &name);
  static net::wotlk::WorldPacket BuildDelMute(const ObjectGuid &guid);
  static net::wotlk::WorldPacket BuildSetContactNotes(const ObjectGuid &guid,
                                                      const std::string &note);

  [[nodiscard]] const std::vector<ContactInfo> &contacts() const {
    return contacts_;
  }
  [[nodiscard]] std::size_t contact_count() const {
    return contacts_.size();
  }

  [[nodiscard]] std::vector<const ContactInfo *> GetFriends() const;
  [[nodiscard]] std::vector<const ContactInfo *> GetIgnored() const;
  [[nodiscard]] std::vector<const ContactInfo *> GetMuted() const;

  void SelectFriendByLuaIndex(std::uint32_t index);
  void SelectIgnoredByLuaIndex(std::uint32_t index);
  void SelectMutedByLuaIndex(std::uint32_t index);
  [[nodiscard]] std::int32_t GetSelectedFriendLuaIndex() const;
  [[nodiscard]] std::int32_t GetSelectedIgnoredLuaIndex() const;
  [[nodiscard]] std::int32_t GetSelectedMutedLuaIndex() const;
  void SetWhoResultsToUi(bool enabled) { who_results_to_ui_ = enabled; }
  [[nodiscard]] bool WhoResultsToUi() const { return who_results_to_ui_; }

  [[nodiscard]] const ContactInfo *FindContact(const ObjectGuid &guid) const;
  [[nodiscard]] bool HasContact(const ObjectGuid &guid) const;
  [[nodiscard]] bool IsFriend(const ObjectGuid &guid) const;
  [[nodiscard]] bool IsIgnored(const ObjectGuid &guid) const;
  [[nodiscard]] bool IsMuted(const ObjectGuid &guid) const;
  [[nodiscard]] bool IsIgnoredOrMuted(const ObjectGuid &guid) const;
  [[nodiscard]] bool IsDeletePending(const ObjectGuid &guid, SocialFlag flag) const;
  [[nodiscard]] bool HasRecentComplaintGuid(std::uint64_t guid) const;
  [[nodiscard]] const std::optional<FriendStatusUpdate> &last_friend_status_update() const {
    return last_friend_status_update_;
  }

  bool SetDisplayName(const ObjectGuid &guid, std::string display_name);
  bool MarkDeletePending(const ObjectGuid &guid, SocialFlag flag);
  bool ClearFriendReferAFriendFlag(const ObjectGuid &guid);
  void RememberRecentComplaintGuid(std::uint64_t guid);
  void SetComplaintStatus(std::uint8_t status) { complaint_status_ = status; }
  [[nodiscard]] std::uint8_t complaint_status() const { return complaint_status_; }
  void Clear();

private:
  std::vector<ContactInfo> contacts_;
  std::vector<std::uint64_t> recent_complaint_guids_;
  std::optional<FriendStatusUpdate> last_friend_status_update_;
  std::uint8_t complaint_status_{0};
  ObjectGuid selected_friend_;
  ObjectGuid selected_ignored_;
  ObjectGuid selected_muted_;
  bool who_results_to_ui_{false};

  ContactInfo *FindContactMut(const ObjectGuid &guid);
  ContactInfo &GetOrCreateContact(const ObjectGuid &guid);
  void ClearRequestedFlags(std::uint32_t requested_flags);
  static bool IsVisible(const ContactInfo &contact, SocialFlag flag);
  static bool NameComesBefore(const std::string &lhs, const std::string &rhs);
  static ObjectGuid GuidAtLuaIndex(
      const std::vector<const ContactInfo *> &contacts, std::uint32_t index);
  static std::int32_t LuaIndexOfGuid(
      const std::vector<const ContactInfo *> &contacts, ObjectGuid guid);
};

}

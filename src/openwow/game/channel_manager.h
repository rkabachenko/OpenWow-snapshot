#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::game {

enum class ChannelFlags : std::uint8_t {
  kNone              = 0x00,
  kAnnounce          = 0x01,
  kModerated         = 0x02,
  kPasswordProtected = 0x04,
  kTrade             = 0x08,
  kDefense           = 0x10,
  kLookingForGroup   = 0x20,
  kLinked            = 0x40,
};

[[nodiscard]] inline constexpr std::uint8_t operator|(ChannelFlags a,
                                                      ChannelFlags b) {
  return static_cast<std::uint8_t>(static_cast<std::uint8_t>(a) |
                                   static_cast<std::uint8_t>(b));
}

[[nodiscard]] inline constexpr bool HasFlag(std::uint8_t flags,
                                            ChannelFlags f) {
  return (flags & static_cast<std::uint8_t>(f)) != 0;
}

enum class ChannelMemberFlags : std::uint8_t {
  kNone      = 0x00,
  kOwner     = 0x01,
  kModerator = 0x02,
  kVoiced    = 0x04,
  kMuted     = 0x08,
};

[[nodiscard]] inline constexpr std::uint8_t operator|(ChannelMemberFlags a,
                                                      ChannelMemberFlags b) {
  return static_cast<std::uint8_t>(static_cast<std::uint8_t>(a) |
                                   static_cast<std::uint8_t>(b));
}

[[nodiscard]] inline constexpr bool HasMemberFlag(std::uint8_t flags,
                                                  ChannelMemberFlags f) {
  return (flags & static_cast<std::uint8_t>(f)) != 0;
}

struct ManagedChannelInfo {
  std::uint32_t channel_id{0};
  std::string name;
  std::string password;
  std::uint8_t flags{0};
  std::uint32_t zone_id{0};
  std::uint32_t member_count{0};
};

struct ManagedChannelMember {
  std::uint64_t guid{0};
  std::string name;
  std::uint8_t flags{0};
};

class ChannelManager {
 public:
  static ChannelManager& Get();

  static constexpr std::uint32_t kMaxJoinedChannels = 20;

  bool JoinChannel(const std::string& name,
                   const std::string& password = "");

  void LeaveChannel(const std::string& name);

  void LeaveAllChannels();

  [[nodiscard]] bool IsInChannel(const std::string& name) const;

  [[nodiscard]] std::optional<ManagedChannelInfo> GetChannel(
      const std::string& name) const;

  [[nodiscard]] std::optional<ManagedChannelInfo> GetChannelById(
      std::uint32_t id) const;

  [[nodiscard]] std::vector<ManagedChannelInfo> GetJoinedChannels() const;

  [[nodiscard]] std::vector<ManagedChannelMember> GetChannelMembers(
      const std::string& name) const;

  [[nodiscard]] std::uint32_t GetNumJoinedChannels() const;

  void SetOwner(const std::string& channel_name, std::uint64_t player_guid);
  void SetModerator(const std::string& channel_name,
                    std::uint64_t player_guid, bool enabled);
  void KickPlayer(const std::string& channel_name,
                  std::uint64_t player_guid);
  void BanPlayer(const std::string& channel_name,
                 std::uint64_t player_guid);
  void UnbanPlayer(const std::string& channel_name,
                   std::uint64_t player_guid);

  void SetPassword(const std::string& channel_name,
                   const std::string& password);
  void SetAnnouncements(const std::string& channel_name, bool enabled);
  void SetModerated(const std::string& channel_name, bool enabled);

  [[nodiscard]] std::vector<std::string> GetDefaultChannelNames() const;

  void AutoJoinDefaults(std::uint32_t zone_id);

  void AddMember(const std::string& channel_name,
                 const ManagedChannelMember& member);
  void RemoveMember(const std::string& channel_name, std::uint64_t guid);
  void UpdateMemberFlags(const std::string& channel_name,
                         std::uint64_t guid, std::uint8_t flags);

  void UpdateChannelInfo(const ManagedChannelInfo& info);

  void Reset();

 private:
  ChannelManager() = default;

  struct ChannelData {
    ManagedChannelInfo info;
    std::vector<ManagedChannelMember> members;
    std::unordered_set<std::uint64_t> banned;
  };

  ChannelData* FindChannel(const std::string& name);
  const ChannelData* FindChannel(const std::string& name) const;

  static std::string NormalizeName(const std::string& name);

  std::uint32_t next_channel_id_{1};

  std::unordered_map<std::string, ChannelData> channels_;

  mutable std::mutex mutex_;
};

}

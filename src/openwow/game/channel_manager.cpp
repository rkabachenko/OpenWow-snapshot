
#include "openwow/game/channel_manager.h"

#include <algorithm>
#include <cctype>

namespace openwow::game {

ChannelManager& ChannelManager::Get() {
  static ChannelManager instance;
  return instance;
}

std::string ChannelManager::NormalizeName(const std::string& name) {
  std::string result = name;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

ChannelManager::ChannelData* ChannelManager::FindChannel(
    const std::string& name) {
  auto key = NormalizeName(name);
  auto it = channels_.find(key);
  return it != channels_.end() ? &it->second : nullptr;
}

const ChannelManager::ChannelData* ChannelManager::FindChannel(
    const std::string& name) const {
  auto key = NormalizeName(name);
  auto it = channels_.find(key);
  return it != channels_.end() ? &it->second : nullptr;
}

bool ChannelManager::JoinChannel(const std::string& name,
                                 const std::string& password) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (channels_.size() >= kMaxJoinedChannels) return false;

  auto key = NormalizeName(name);
  if (channels_.count(key)) return false;

  ChannelData data;
  data.info.channel_id = next_channel_id_++;
  data.info.name = name;
  data.info.password = password;
  if (!password.empty()) {
    data.info.flags |= static_cast<std::uint8_t>(
        ChannelFlags::kPasswordProtected);
  }
  channels_[key] = std::move(data);
  return true;
}

void ChannelManager::LeaveChannel(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  channels_.erase(NormalizeName(name));
}

void ChannelManager::LeaveAllChannels() {
  std::lock_guard<std::mutex> lock(mutex_);
  channels_.clear();
}

bool ChannelManager::IsInChannel(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return channels_.count(NormalizeName(name)) > 0;
}

std::optional<ManagedChannelInfo> ChannelManager::GetChannel(
    const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* ch = FindChannel(name);
  if (!ch) return std::nullopt;
  return ch->info;
}

std::optional<ManagedChannelInfo> ChannelManager::GetChannelById(
    std::uint32_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& [key, data] : channels_) {
    if (data.info.channel_id == id) return data.info;
  }
  return std::nullopt;
}

std::vector<ManagedChannelInfo> ChannelManager::GetJoinedChannels() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ManagedChannelInfo> result;
  result.reserve(channels_.size());
  for (const auto& [key, data] : channels_) {
    result.push_back(data.info);
  }
  return result;
}

std::vector<ManagedChannelMember> ChannelManager::GetChannelMembers(
    const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* ch = FindChannel(name);
  if (!ch) return {};
  return ch->members;
}

std::uint32_t ChannelManager::GetNumJoinedChannels() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<std::uint32_t>(channels_.size());
}

void ChannelManager::SetOwner(const std::string& channel_name,
                              std::uint64_t player_guid) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* ch = FindChannel(channel_name);
  if (!ch) return;

  for (auto& m : ch->members) {
    m.flags &= ~static_cast<std::uint8_t>(ChannelMemberFlags::kOwner);
    if (m.guid == player_guid) {
      m.flags |= static_cast<std::uint8_t>(ChannelMemberFlags::kOwner);
    }
  }
}

void ChannelManager::SetModerator(const std::string& channel_name,
                                  std::uint64_t player_guid, bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* ch = FindChannel(channel_name);
  if (!ch) return;

  for (auto& m : ch->members) {
    if (m.guid == player_guid) {
      if (enabled) {
        m.flags |= static_cast<std::uint8_t>(ChannelMemberFlags::kModerator);
      } else {
        m.flags &= ~static_cast<std::uint8_t>(ChannelMemberFlags::kModerator);
      }
      break;
    }
  }
}

void ChannelManager::KickPlayer(const std::string& channel_name,
                                std::uint64_t player_guid) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* ch = FindChannel(channel_name);
  if (!ch) return;

  ch->members.erase(
      std::remove_if(ch->members.begin(), ch->members.end(),
                     [player_guid](const ManagedChannelMember& m) {
                       return m.guid == player_guid;
                     }),
      ch->members.end());
  if (ch->info.member_count > 0) --ch->info.member_count;
}

void ChannelManager::BanPlayer(const std::string& channel_name,
                               std::uint64_t player_guid) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* ch = FindChannel(channel_name);
  if (!ch) return;

  ch->banned.insert(player_guid);

  ch->members.erase(
      std::remove_if(ch->members.begin(), ch->members.end(),
                     [player_guid](const ManagedChannelMember& m) {
                       return m.guid == player_guid;
                     }),
      ch->members.end());
  if (ch->info.member_count > 0) --ch->info.member_count;
}

void ChannelManager::UnbanPlayer(const std::string& channel_name,
                                 std::uint64_t player_guid) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* ch = FindChannel(channel_name);
  if (!ch) return;
  ch->banned.erase(player_guid);
}

void ChannelManager::SetPassword(const std::string& channel_name,
                                 const std::string& password) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* ch = FindChannel(channel_name);
  if (!ch) return;
  ch->info.password = password;
  if (!password.empty()) {
    ch->info.flags |= static_cast<std::uint8_t>(
        ChannelFlags::kPasswordProtected);
  } else {
    ch->info.flags &= ~static_cast<std::uint8_t>(
        ChannelFlags::kPasswordProtected);
  }
}

void ChannelManager::SetAnnouncements(const std::string& channel_name,
                                      bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* ch = FindChannel(channel_name);
  if (!ch) return;
  if (enabled) {
    ch->info.flags |= static_cast<std::uint8_t>(ChannelFlags::kAnnounce);
  } else {
    ch->info.flags &= ~static_cast<std::uint8_t>(ChannelFlags::kAnnounce);
  }
}

void ChannelManager::SetModerated(const std::string& channel_name,
                                  bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* ch = FindChannel(channel_name);
  if (!ch) return;
  if (enabled) {
    ch->info.flags |= static_cast<std::uint8_t>(ChannelFlags::kModerated);
  } else {
    ch->info.flags &= ~static_cast<std::uint8_t>(ChannelFlags::kModerated);
  }
}

std::vector<std::string> ChannelManager::GetDefaultChannelNames() const {
  return {"General", "Trade", "LocalDefense", "WorldDefense",
          "LookingForGroup"};
}

void ChannelManager::AutoJoinDefaults(std::uint32_t zone_id) {

  JoinChannel("General");
  JoinChannel("LocalDefense");

  JoinChannel("Trade");
  JoinChannel("LookingForGroup");

  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [key, data] : channels_) {
      data.info.zone_id = zone_id;
    }
  }
}

void ChannelManager::AddMember(const std::string& channel_name,
                               const ManagedChannelMember& member) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* ch = FindChannel(channel_name);
  if (!ch) return;

  for (const auto& m : ch->members) {
    if (m.guid == member.guid) return;
  }
  ch->members.push_back(member);
  ++ch->info.member_count;
}

void ChannelManager::RemoveMember(const std::string& channel_name,
                                  std::uint64_t guid) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* ch = FindChannel(channel_name);
  if (!ch) return;

  auto before = ch->members.size();
  ch->members.erase(
      std::remove_if(ch->members.begin(), ch->members.end(),
                     [guid](const ManagedChannelMember& m) {
                       return m.guid == guid;
                     }),
      ch->members.end());
  if (ch->members.size() < before && ch->info.member_count > 0) {
    --ch->info.member_count;
  }
}

void ChannelManager::UpdateMemberFlags(const std::string& channel_name,
                                       std::uint64_t guid,
                                       std::uint8_t flags) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* ch = FindChannel(channel_name);
  if (!ch) return;

  for (auto& m : ch->members) {
    if (m.guid == guid) {
      m.flags = flags;
      break;
    }
  }
}

void ChannelManager::UpdateChannelInfo(const ManagedChannelInfo& info) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto key = NormalizeName(info.name);
  auto it = channels_.find(key);
  if (it != channels_.end()) {

    it->second.info = info;
  } else {
    ChannelData data;
    data.info = info;
    channels_[key] = std::move(data);
  }
}

void ChannelManager::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  channels_.clear();
  next_channel_id_ = 1;
}

}

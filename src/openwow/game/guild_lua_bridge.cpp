
#include "openwow/game/guild_lua_bridge.h"

#include "openwow/game/interaction_sender.h"
#include "openwow/game/world_session.h"

namespace openwow::game {

GuildLuaBridge& GuildLuaBridge::Get() {
  static GuildLuaBridge instance;
  return instance;
}

bool GuildLuaBridge::IsInGuild() const { return inGuild_; }

GuildLuaBridge::GuildInfoResult GuildLuaBridge::GetGuildInfo() const {
  if (!inGuild_) return {};
  return guildInfo_;
}

std::pair<std::uint32_t, std::uint32_t> GuildLuaBridge::GetNumGuildMembers(
    bool includeOffline) const {
  if (!inGuild_) return {0, 0};
  std::uint32_t online = 0;
  for (const auto& m : members_) {
    if (m.online) ++online;
  }
  if (includeOffline) {
    return {static_cast<std::uint32_t>(members_.size()), online};
  }
  return {online, online};
}

GuildLuaBridge::GuildMemberInfo GuildLuaBridge::GetGuildRosterInfo(
    std::uint32_t index) const {
  if (index >= members_.size()) return {};
  return members_[index];
}

void GuildLuaBridge::GuildRoster() {

  if (session_) {
    session_->interaction().SendGuildRoster();
  }
}

std::string GuildLuaBridge::GetGuildRosterMOTD() const { return motd_; }

bool GuildLuaBridge::GetGuildRosterShowOffline() const { return showOffline_; }
void GuildLuaBridge::SetGuildRosterShowOffline(bool show) {
  showOffline_ = show;
}

std::uint32_t GuildLuaBridge::GetNumGuildRanks() const {
  return static_cast<std::uint32_t>(ranks_.size());
}

std::string GuildLuaBridge::GetGuildRankName(std::uint32_t index) const {
  if (index >= ranks_.size()) return {};
  return ranks_[index];
}

bool GuildLuaBridge::CanEditGuildInfo() const { return canEditInfo_; }
bool GuildLuaBridge::CanEditGuildMOTD() const { return canEditMOTD_; }

void GuildLuaBridge::GuildPromote(const std::string& name) {
  if (session_) session_->interaction().SendGuildPromote(name);
}
void GuildLuaBridge::GuildDemote(const std::string& name) {
  if (session_) session_->interaction().SendGuildDemote(name);
}
void GuildLuaBridge::GuildUninvite(const std::string& name) {
  if (session_) session_->interaction().SendGuildRemove(name);
}
void GuildLuaBridge::GuildLeave() {
  if (session_) session_->interaction().SendGuildLeave();
}
void GuildLuaBridge::GuildDisband() {
  if (session_) session_->interaction().SendGuildDisband();
}
void GuildLuaBridge::GuildSetMOTD(const std::string& motd) {
  if (session_) session_->interaction().SendGuildSetMOTD(motd);

  motd_ = motd;
}

void GuildLuaBridge::SetGuildData(const std::string& name,
                                   const std::string& rankName,
                                   std::uint32_t rankIndex,
                                   const std::string& realm) {
  inGuild_ = true;
  guildInfo_ = {name, rankName, rankIndex, realm};
}

void GuildLuaBridge::AddMember(const GuildMemberInfo& member) {
  members_.push_back(member);
}

void GuildLuaBridge::SetMOTD(const std::string& motd) { motd_ = motd; }

void GuildLuaBridge::AddRank(const std::string& rankName) {
  ranks_.push_back(rankName);
}

void GuildLuaBridge::SetCanEditInfo(bool can) { canEditInfo_ = can; }
void GuildLuaBridge::SetCanEditMOTD(bool can) { canEditMOTD_ = can; }

void GuildLuaBridge::Clear() {
  inGuild_ = false;
  guildInfo_ = {};
  motd_.clear();
  showOffline_ = false;
  canEditInfo_ = false;
  canEditMOTD_ = false;
  members_.clear();
  ranks_.clear();
}

}

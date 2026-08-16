#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "openwow/game/world_session_fwd.h"

namespace openwow::game {

class GuildLuaBridge {
 public:
  static GuildLuaBridge& Get();

  void SetSession(WorldSession* session) { session_ = session; }

  [[nodiscard]] bool IsInGuild() const;

  struct GuildInfoResult {
    std::string name;
    std::string rankName;
    std::uint32_t rankIndex{0};
    std::string realm;
  };
  [[nodiscard]] GuildInfoResult GetGuildInfo() const;

  [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> GetNumGuildMembers(
      bool includeOffline = false) const;

  struct GuildMemberInfo {
    std::string name;
    std::string rank;
    std::uint32_t rankIndex{0};
    std::uint8_t level{0};
    std::uint8_t classId{0};
    std::uint32_t zone{0};
    std::string note;
    std::string officerNote;
    bool online{false};
    std::uint8_t status{0};
    std::string classFileName;
  };
  [[nodiscard]] GuildMemberInfo GetGuildRosterInfo(std::uint32_t index) const;

  void GuildRoster();

  [[nodiscard]] std::string GetGuildRosterMOTD() const;

  [[nodiscard]] bool GetGuildRosterShowOffline() const;
  void SetGuildRosterShowOffline(bool show);

  [[nodiscard]] std::uint32_t GetNumGuildRanks() const;

  [[nodiscard]] std::string GetGuildRankName(std::uint32_t index) const;

  [[nodiscard]] bool CanEditGuildInfo() const;
  [[nodiscard]] bool CanEditGuildMOTD() const;

  void GuildPromote(const std::string& name);
  void GuildDemote(const std::string& name);
  void GuildUninvite(const std::string& name);
  void GuildLeave();
  void GuildDisband();
  void GuildSetMOTD(const std::string& motd);

  void SetGuildData(const std::string& name, const std::string& rankName,
                    std::uint32_t rankIndex, const std::string& realm);

  void AddMember(const GuildMemberInfo& member);

  void SetMOTD(const std::string& motd);

  void AddRank(const std::string& rankName);

  void SetCanEditInfo(bool can);
  void SetCanEditMOTD(bool can);

  void Clear();

 private:
  GuildLuaBridge() = default;

  WorldSession* session_{nullptr};

  bool inGuild_{false};
  GuildInfoResult guildInfo_{};
  std::string motd_;
  bool showOffline_{false};
  bool canEditInfo_{false};
  bool canEditMOTD_{false};
  std::vector<GuildMemberInfo> members_;
  std::vector<std::string> ranks_;
};

}

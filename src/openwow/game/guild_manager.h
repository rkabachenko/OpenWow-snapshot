
#pragma once

#include <cstdint>
#include <ctime>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

namespace openwow::data {
class DBCacheRuntime;
}

namespace openwow::game {

class QueryCache;

inline constexpr int kGuildRanksMaxCount = 10;

enum class GuildEventType : std::uint8_t {
  kPromotion = 0,
  kDemotion = 1,
  kMotd = 2,
  kJoined = 3,
  kLeft = 4,
  kRemoved = 5,
  kLeaderIs = 6,
  kLeaderChanged = 7,
  kDisbanded = 8,
  kTabardChange = 9,
  kRankUpdated = 10,
  kRankDeleted = 11,
  kSignedOn = 12,
  kSignedOff = 13,
  kBankBagSlotsChanged = 14,
  kBankTabPurchased = 15,
  kBankTabUpdated = 16,
  kBankMoneySet = 17,
  kBankTabAndMoneyUpdated = 18,
  kBankTextChanged = 19,
};

struct GuildCommandResult {
  std::int32_t command = 0;
  std::string name;
  std::int32_t result = 0;
};

struct GuildEvent {
  GuildEventType type{};
  std::vector<std::string> params;
  ObjectGuid guid;
};

struct GuildEmblem {
  std::uint32_t style = 0;
  std::uint32_t color = 0;
  std::uint32_t border_style = 0;
  std::uint32_t border_color = 0;
  std::uint32_t background_color = 0;

  friend bool operator==(const GuildEmblem&, const GuildEmblem&) = default;
};

inline constexpr std::uint32_t kInvalidGuildEmblemIndex =
    std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] inline bool HasResolvedGuildEmblem(const GuildEmblem& emblem) {
  return emblem.style != kInvalidGuildEmblemIndex &&
         emblem.color != kInvalidGuildEmblemIndex &&
         emblem.border_style != kInvalidGuildEmblemIndex &&
         emblem.border_color != kInvalidGuildEmblemIndex &&
         emblem.background_color != kInvalidGuildEmblemIndex;
}

struct GuildInfo {
  std::uint32_t guild_id = 0;
  std::string name;
  std::string rank_names[kGuildRanksMaxCount];
  GuildEmblem emblem;
  std::uint32_t rank_count = 0;
};

struct GuildRankInfo {
  std::uint32_t flags = 0;
  std::uint32_t withdraw_gold_limit = 0;
  std::uint32_t tab_flags[6] = {};
  std::uint32_t tab_withdraw_item_limit[6] = {};
};

struct GuildMember {
  ObjectGuid guid;
  std::uint8_t status = 0;
  std::string name;
  std::int32_t rank_id = 0;
  std::uint8_t level = 0;
  std::uint8_t class_id = 0;
  std::uint8_t gender = 0;
  std::int32_t area_id = 0;
  float last_save = 0.0f;
  std::string note;
  std::string officer_note;
};

struct GuildRoster {
  std::string motd;
  std::string info_text;
  std::vector<GuildRankInfo> ranks;
  std::vector<GuildMember> members;
};

namespace detail {

bool ParseGuildRosterPacket(const std::uint8_t* data, std::size_t len,
                            GuildRoster* out_roster);
std::string SanitizeGuildEventParam(std::string_view raw_text);
std::string SanitizeGuildEventRosterMotd(std::string_view raw_text);

}

struct GuildEventLogEntry {
  std::uint8_t type = 0;
  ObjectGuid player;
  ObjectGuid other;
  std::uint8_t rank_id = 0;
  std::time_t event_time = 0;
};

struct GuildPermissions {
  std::uint32_t rank_id = 0;
  std::int32_t flags = 0;
  std::int32_t withdraw_gold_limit = 0;
  std::int8_t num_tabs = 0;
  struct TabPermission {
    std::int32_t flags = 0;
    std::int32_t withdraw_item_limit = 0;
  };
  TabPermission tabs[6] = {};
};

class GuildManager {
 public:
  explicit GuildManager(openwow::data::DBCacheRuntime& db_cache_runtime)
      : db_cache_runtime_(db_cache_runtime) {}

  bool HandleGuildQueryResponse(const std::uint8_t* data, std::size_t len);
  bool HandleGuildRoster(const std::uint8_t* data, std::size_t len);
  bool HandleGuildEvent(const std::uint8_t* data, std::size_t len);
  bool HandleGuildCommandResult(const std::uint8_t* data, std::size_t len);
  bool HandleGuildInvite(const std::uint8_t* data, std::size_t len);

  bool HandleGuildPermissions(const std::uint8_t* data, std::size_t len);
  bool HandleGuildEventLogQuery(const std::uint8_t* data, std::size_t len);
  [[nodiscard]] bool BeginGuildEventLogRefresh(
      QueryCache& query_cache,
      const std::function<void(std::uint64_t)>& send_name_query);
  [[nodiscard]] bool ResolveGuildEventLogNameQuery(std::uint64_t raw_guid);

  bool HandleGuildInfoPacket(const std::uint8_t* data, std::size_t len);

  bool HandleGuildDeclinePacket(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildQuery(
      std::uint32_t guild_id);
  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildRoster();
  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildInvite(
      const std::string& name);
  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildAccept();
  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildDecline();
  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildLeave();
  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildRemove(
      const std::string& name);
  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildMotd(
      const std::string& motd);
  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildSetPublicNote(
      const std::string& name, const std::string& note);
  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildSetOfficerNote(
      const std::string& name, const std::string& note);
  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildPromote(
      const std::string& name);
  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildDemote(
      const std::string& name);
  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildDisband();
  [[nodiscard]] static net::wotlk::WorldPacket BuildGuildEventLogQuery();

  [[nodiscard]] bool has_guild_info() const { return guild_info_.has_value(); }
  [[nodiscard]] const GuildInfo& guild_info() const {
    return guild_info_.value();
  }
  [[nodiscard]] const GuildInfo* FindCachedGuildInfo(
      std::uint32_t guild_id) const;

  [[nodiscard]] bool BeginGuildQuery(std::uint32_t guild_id);
  [[nodiscard]] bool has_roster() const { return roster_.has_value(); }
  [[nodiscard]] const GuildRoster& roster() const { return roster_.value(); }
  [[nodiscard]] bool has_permissions() const {
    return permissions_.has_value();
  }
  [[nodiscard]] const GuildPermissions& permissions() const {
    return permissions_.value();
  }
  [[nodiscard]] const std::vector<GuildEventLogEntry>& event_log() const {
    return event_log_;
  }
  [[nodiscard]] const std::optional<GuildEvent>& last_event() const {
    return last_event_;
  }
  [[nodiscard]] const std::optional<GuildCommandResult>& last_command_result()
      const {
    return last_command_result_;
  }
  void UpdateCachedRosterMotd(const std::string& motd);
  void ClearGuildQueryCacheForClientCacheVersion();

  [[nodiscard]] bool has_pending_invite() const {
    return !invite_from_.empty();
  }
  [[nodiscard]] const std::string& invite_from() const { return invite_from_; }
  [[nodiscard]] const std::string& invite_guild_name() const {
    return invite_guild_name_;
  }
  void ClearInvite() {
    invite_from_.clear();
    invite_guild_name_.clear();
  }

  struct GuildInfoData {
    std::string name;

    std::uint32_t created_packed_time = 0;
    std::uint32_t num_members = 0;
    std::uint32_t num_accounts = 0;

    [[nodiscard]] std::uint32_t created_day()   const { return (created_packed_time >> 14) & 0x3Fu; }
    [[nodiscard]] std::uint32_t created_month() const { return (created_packed_time >> 20) & 0xFu; }
    [[nodiscard]] std::uint32_t created_year()  const { return (created_packed_time >> 24) & 0x1Fu; }
  };
  [[nodiscard]] const std::optional<GuildInfoData>& guild_info_data() const {
    return guild_info_data_;
  }
  [[nodiscard]] const std::string& declined_name() const {
    return declined_name_;
  }

  void InvalidateCachedGuildInfo(std::uint32_t guild_id);
  void Clear();

 private:
  openwow::data::DBCacheRuntime& db_cache_runtime_;

  std::optional<GuildInfo> guild_info_;
  mutable std::unordered_map<std::uint32_t, GuildInfo> guild_query_cache_;
  std::unordered_set<std::uint32_t> pending_guild_queries_;
  std::optional<GuildRoster> roster_;
  std::optional<GuildPermissions> permissions_;
  std::vector<GuildEventLogEntry> event_log_;
  std::optional<GuildEvent> last_event_;
  std::optional<GuildCommandResult> last_command_result_;
  std::string invite_from_;
  std::string invite_guild_name_;
  std::optional<GuildInfoData> guild_info_data_;
  std::string declined_name_;
  struct PendingEventLogRefresh {
    bool active = false;
    std::unordered_set<std::uint64_t> pending_player_guids;
  } pending_event_log_refresh_;

  [[nodiscard]] bool TryCompleteGuildEventLogRefresh();
};

}

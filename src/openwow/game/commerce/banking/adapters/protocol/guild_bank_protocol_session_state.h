#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"

namespace openwow::game {

class QueryCache;

struct GuildBankSocketEnchant {
  std::uint8_t socket_index = 0;
  std::int32_t enchant_id = 0;
};

struct GuildBankItem {
  std::uint8_t slot = 0;
  std::uint32_t item_id = 0;
  std::int32_t flags = 0;
  std::int32_t random_property_id = 0;
  std::int32_t random_property_seed = 0;
  std::int32_t count = 0;
  std::int32_t enchant_id = 0;
  std::uint8_t charges = 0;
  std::vector<GuildBankSocketEnchant> socket_enchants;
};

struct GuildBankTabInfo {
  std::string name;
  std::string icon;
};

struct GuildBankList {
  std::uint64_t money = 0;
  std::uint8_t tab = 0;
  std::int32_t withdrawals_remaining = 0;
  bool full_update = false;
  std::vector<GuildBankTabInfo> tabs;
  std::vector<GuildBankItem> items;
};

inline constexpr std::uint8_t kGuildBankLogPages = 7;
inline constexpr std::uint8_t kGuildBankMoneyLogPage = 6;
inline constexpr std::uint8_t kGuildBankLogEntriesPerPage = 25;

struct GuildBankLogEntry {
  std::int8_t entry_type = 0;
  ObjectGuid player{ObjectGuid(0)};
  std::uint32_t item_or_money = 0;
  std::uint32_t count = 0;
  std::uint8_t other_tab = 0;
  std::uint32_t event_unix_time = 0;
};

struct GuildBankLog {
  std::uint8_t tab = 0;
  std::vector<GuildBankLogEntry> entries;
};

struct GuildBankMoneyWithdrawn {
  std::int32_t remaining = 0;
};

struct GuildBankText {
  std::uint8_t tab = 0;
  std::string text;
};

class GuildBankProtocolSessionState {
 public:
  bool HandleGuildBankList(const std::uint8_t* data, std::size_t len);
  bool HandleGuildBankLogQuery(const std::uint8_t* data, std::size_t len);
  bool HandleGuildBankMoneyWithdrawn(const std::uint8_t* data, std::size_t len);
  bool HandleGuildBankText(const std::uint8_t* data, std::size_t len);
  [[nodiscard]] bool BeginGuildBankListRefresh(QueryCache& query_cache);
  [[nodiscard]] bool ResolveGuildBankListItemQuery(std::uint32_t item_entry);
  [[nodiscard]] bool BeginGuildBankLogRefresh(
      QueryCache& query_cache,
      const std::function<void(std::uint64_t)>& send_name_query);
  [[nodiscard]] bool ResolveGuildBankLogNameQuery(std::uint64_t raw_guid);
  [[nodiscard]] bool ResolveGuildBankLogItemQuery(std::uint32_t item_entry);
  void ResetStateOnPlayerEnterWorld();

  const GuildBankList& last_bank_list() const { return last_bank_list_; }
  const GuildBankLog& last_bank_log() const { return last_bank_log_; }
  const GuildBankMoneyWithdrawn& last_money_withdrawn() const {
    return last_money_withdrawn_;
  }
  const GuildBankText& last_bank_text() const { return last_bank_text_; }

  void Clear();

 private:
  struct PendingListRefresh {
    std::uint32_t generation = 0;
    bool active = false;
    std::unordered_set<std::uint32_t> pending_item_entries;
  };

  struct PendingLogRefresh {
    std::uint32_t generation = 0;
    bool active = false;
    std::unordered_set<std::uint64_t> pending_player_guids;
    std::unordered_set<std::uint32_t> pending_item_entries;
  };

  [[nodiscard]] bool TryCompleteGuildBankListRefresh();
  [[nodiscard]] bool TryCompleteGuildBankLogRefresh();

  GuildBankList last_bank_list_{};
  GuildBankLog last_bank_log_{};
  GuildBankMoneyWithdrawn last_money_withdrawn_{};
  GuildBankText last_bank_text_{};
  PendingListRefresh pending_list_refresh_{};
  PendingLogRefresh pending_log_refresh_{};
};

}

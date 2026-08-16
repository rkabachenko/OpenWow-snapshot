#include "openwow/game/commerce/banking/adapters/protocol/guild_bank_protocol_session_state.h"

#include <algorithm>
#include <ctime>
#include <string_view>

#include "openwow/game/async_query_channel.h"
#include "openwow/game/query_cache.h"

namespace openwow::game {

namespace {

constexpr std::int8_t kGuildBankEntryTypeDeposit = 1;
constexpr std::int8_t kGuildBankEntryTypeWithdraw = 2;
constexpr std::int8_t kGuildBankEntryTypeMoveToTab = 3;
constexpr std::int8_t kGuildBankEntryTypeMoneyDeposit = 4;
constexpr std::int8_t kGuildBankEntryTypeMoneyWithdraw = 5;
constexpr std::int8_t kGuildBankEntryTypeRepair = 6;
constexpr std::int8_t kGuildBankEntryTypeMoveFromTab = 7;
constexpr std::int8_t kGuildBankEntryTypeWithdrawForTab = 8;
constexpr std::int8_t kGuildBankEntryTypeBuyTab = 9;
constexpr std::size_t kGuildBankTabNameReadLimit = 64;
constexpr std::size_t kGuildBankTabIconReadLimit = 256;
constexpr int kGuildBankTabNameMaxVisibleChars = 16;
constexpr std::size_t kGuildBankTextReadLimit = 0x2000;
constexpr int kGuildBankTextMaxVisibleChars = 2048;
constexpr std::uint64_t kGuildBankMoneyMax = (UINT64_C(1) << 51) - 1;

bool ReadBoundedCString(PacketReader& reader, std::string& out, std::size_t max_bytes) {
  out.clear();
  if (max_bytes == 0) {
    return false;
  }

  while (reader.Remaining() > 0) {
    std::uint8_t byte = 0;
    if (!reader.ReadU8(byte)) {
      return false;
    }

    if (byte == 0) {
      return true;
    }

    if (out.size() + 1 < max_bytes) {
      out.push_back(static_cast<char>(byte));
    }
  }

  return false;
}

std::string SanitizeGuildBankScriptText(std::string_view raw_text,
                                        std::size_t max_bytes,
                                        int max_visible_chars,
                                        bool truncate_at_line_breaks) {
  std::string sanitized;
  if (max_bytes == 0) {
    return sanitized;
  }

  sanitized.reserve(std::min<std::size_t>(raw_text.size(), max_bytes - 1));
  for (const char ch : raw_text) {
    if (sanitized.size() + 1 >= max_bytes) {
      break;
    }

    if (ch != '|') {
      sanitized.push_back(ch);
    }
  }

  if (truncate_at_line_breaks) {
    for (std::size_t i = 0; i < sanitized.size(); ++i) {
      const char ch = sanitized[i];
      if (ch == '\r' || ch == '\n' ||
          (ch == '\\' && i + 1 < sanitized.size() && sanitized[i + 1] == 'n')) {
        sanitized.resize(i);
        break;
      }
    }
  }

  int visible_chars = 0;
  for (std::size_t i = 0; i < sanitized.size(); ++i) {
    const auto byte = static_cast<std::uint8_t>(sanitized[i]);
    if ((byte & 0xC0u) == 0x80u) {
      continue;
    }

    ++visible_chars;
    if (visible_chars == max_visible_chars) {
      sanitized.resize(i);
      break;
    }
  }

  return sanitized;
}

std::string SanitizeGuildBankTabName(std::string_view raw_text) {
  return SanitizeGuildBankScriptText(raw_text, kGuildBankTabNameReadLimit,
                                     kGuildBankTabNameMaxVisibleChars, true);
}

bool GuildBankLogEntryNeedsItemTemplate(std::int8_t entry_type) {
  switch (entry_type) {
    case kGuildBankEntryTypeMoneyDeposit:
    case kGuildBankEntryTypeMoneyWithdraw:
    case kGuildBankEntryTypeRepair:
    case kGuildBankEntryTypeWithdrawForTab:
    case kGuildBankEntryTypeBuyTab:
      return false;
    case kGuildBankEntryTypeDeposit:
    case kGuildBankEntryTypeWithdraw:
    case kGuildBankEntryTypeMoveToTab:
    case kGuildBankEntryTypeMoveFromTab:
      return true;
    default:

      return true;
  }
}

bool ReadGuildBankTextCString(PacketReader& reader, std::string& out) {
  out.clear();

  const auto readable_bytes =
      std::min(reader.Remaining(), kGuildBankTextReadLimit);
  if (readable_bytes == 0) {
    return false;
  }

  const auto* data = reader.PeekBytes(readable_bytes);
  if (data == nullptr) {
    return false;
  }

  for (std::size_t i = 0; i < readable_bytes; ++i) {
    if (data[i] == 0) {
      out.assign(reinterpret_cast<const char*>(data), i);
      reader.Skip(i + 1);
      return true;
    }
  }

  if (readable_bytes == kGuildBankTextReadLimit) {
    reader.Skip(readable_bytes);
    out.clear();
    return true;
  }

  return false;
}

std::string SanitizeGuildBankText(std::string_view raw_text) {
  return SanitizeGuildBankScriptText(raw_text, kGuildBankTextReadLimit,
                                     kGuildBankTextMaxVisibleChars, false);
}

}

bool GuildBankProtocolSessionState::HandleGuildBankList(const std::uint8_t* data,
                                           std::size_t len) {
  PacketReader r(data, len);
  last_bank_list_ = {};
  pending_list_refresh_.active = false;
  pending_list_refresh_.pending_item_entries.clear();

  if (!r.ReadU64(last_bank_list_.money)) return false;
  last_bank_list_.money = std::min(last_bank_list_.money, kGuildBankMoneyMax);
  if (!r.ReadU8(last_bank_list_.tab)) return false;
  if (!r.ReadI32(last_bank_list_.withdrawals_remaining)) return false;

  std::uint8_t full_update = 0;
  if (!r.ReadU8(full_update)) return false;
  last_bank_list_.full_update = (full_update != 0);

  if (last_bank_list_.tab == 0 && last_bank_list_.full_update) {
    std::uint8_t tab_count = 0;
    if (!r.ReadU8(tab_count)) return false;
    last_bank_list_.tabs.resize(tab_count);
    for (std::uint8_t i = 0; i < tab_count; ++i) {
      std::string raw_name;
      std::string raw_icon;
      if (!ReadBoundedCString(r, raw_name, kGuildBankTabNameReadLimit)) return false;
      if (!ReadBoundedCString(r, raw_icon, kGuildBankTabIconReadLimit)) return false;

      last_bank_list_.tabs[i].name = SanitizeGuildBankTabName(raw_name);
      last_bank_list_.tabs[i].icon = std::move(raw_icon);
    }
  }

  std::uint8_t item_count = 0;
  if (!r.ReadU8(item_count)) return false;
  last_bank_list_.items.reserve(item_count);

  for (std::uint8_t i = 0; i < item_count; ++i) {
    GuildBankItem item;
    if (!r.ReadU8(item.slot)) return false;
    if (!r.ReadU32(item.item_id)) return false;
    if (item.item_id != 0) {
      if (!r.ReadI32(item.flags)) return false;
      if (!r.ReadI32(item.random_property_id)) return false;
      if (item.random_property_id != 0) {
        if (!r.ReadI32(item.random_property_seed)) return false;
      }
      if (!r.ReadI32(item.count)) return false;
      if (!r.ReadI32(item.enchant_id)) return false;
      if (!r.ReadU8(item.charges)) return false;
      std::uint8_t socket_count = 0;
      if (!r.ReadU8(socket_count)) return false;
      item.socket_enchants.resize(socket_count);
      for (std::uint8_t s = 0; s < socket_count; ++s) {
        if (!r.ReadU8(item.socket_enchants[s].socket_index)) return false;
        if (!r.ReadI32(item.socket_enchants[s].enchant_id)) return false;
      }
    }
    last_bank_list_.items.push_back(std::move(item));
  }
  return true;
}

bool GuildBankProtocolSessionState::BeginGuildBankListRefresh(QueryCache& query_cache) {
  auto& refresh = pending_list_refresh_;
  ++refresh.generation;
  refresh.active = true;
  refresh.pending_item_entries.clear();

  const auto generation = refresh.generation;
  const auto callback_key = AsyncQueryChannel::CallbackKey(
      reinterpret_cast<std::uintptr_t>(this), generation);

  for (const auto& item : last_bank_list_.items) {
    if (item.item_id == 0 || query_cache.GetItemTemplate(item.item_id) != nullptr ||
        !refresh.pending_item_entries.insert(item.item_id).second) {
      continue;
    }

    static_cast<void>(query_cache.GetOrRequestItemTemplate(
        item.item_id,
        QueryCache::QueryRequestOptions{
            .callback_key = callback_key,
            .callback =
                [this, generation, item_entry = item.item_id](bool ) {
                  if (pending_list_refresh_.generation != generation) {
                    return;
                  }

                  static_cast<void>(ResolveGuildBankListItemQuery(item_entry));
                },
        }));
  }

  const bool has_pending = !refresh.pending_item_entries.empty();
  if (!has_pending) {
    refresh.active = false;
  }
  return has_pending;
}

bool GuildBankProtocolSessionState::ResolveGuildBankListItemQuery(std::uint32_t item_entry) {
  if (!pending_list_refresh_.active) {
    return false;
  }

  pending_list_refresh_.pending_item_entries.erase(item_entry);
  return TryCompleteGuildBankListRefresh();
}

bool GuildBankProtocolSessionState::HandleGuildBankLogQuery(const std::uint8_t* data,
                                               std::size_t len) {
  PacketReader r(data, len);
  last_bank_log_ = {};
  if (!r.ReadU8(last_bank_log_.tab)) return false;

  std::uint8_t count = 0;
  if (!r.ReadU8(count)) return false;
  last_bank_log_.entries.reserve(count);
  const auto now = static_cast<std::uint32_t>(std::time(nullptr));

  for (std::uint8_t i = 0; i < count; ++i) {
    GuildBankLogEntry e;
    std::uint8_t type_raw = 0;
    if (!r.ReadU8(type_raw)) return false;
    e.entry_type = static_cast<std::int8_t>(type_raw);
    if (!r.ReadGuid(e.player)) return false;

    if (type_raw == 4 || type_raw == 5 || type_raw == 6 ||
        type_raw == 8 || type_raw == 9) {
      if (!r.ReadU32(e.item_or_money)) return false;
    } else {
      if (!r.ReadU32(e.item_or_money)) return false;
      if (!r.ReadU32(e.count)) return false;
      if ((type_raw == 3 || type_raw == 7) && !r.ReadU8(e.other_tab)) {
        return false;
      }
    }

    std::uint32_t time_offset = 0;
    if (!r.ReadU32(time_offset)) return false;
    e.event_unix_time = now - time_offset;
    last_bank_log_.entries.push_back(std::move(e));
  }
  return true;
}

bool GuildBankProtocolSessionState::HandleGuildBankMoneyWithdrawn(const std::uint8_t* data,
                                                     std::size_t len) {
  PacketReader r(data, len);
  return r.ReadI32(last_money_withdrawn_.remaining);
}

bool GuildBankProtocolSessionState::HandleGuildBankText(const std::uint8_t* data,
                                           std::size_t len) {
  PacketReader r(data, len);
  std::string raw_text;

  if (!r.ReadU8(last_bank_text_.tab)) return false;
  if (!ReadGuildBankTextCString(r, raw_text)) return false;
  last_bank_text_.text = SanitizeGuildBankText(raw_text);
  return true;
}

bool GuildBankProtocolSessionState::BeginGuildBankLogRefresh(
    QueryCache& query_cache,
    const std::function<void(std::uint64_t)>& send_name_query) {
  auto& refresh = pending_log_refresh_;
  ++refresh.generation;
  refresh.active = true;
  refresh.pending_player_guids.clear();
  refresh.pending_item_entries.clear();

  const auto generation = refresh.generation;
  const auto callback_key = AsyncQueryChannel::CallbackKey(
      reinterpret_cast<std::uintptr_t>(this), generation);

  for (const auto& entry : last_bank_log_.entries) {
    const auto raw_guid = entry.player.GetRawValue();
    if (raw_guid != 0 && query_cache.GetPlayerName(raw_guid) == nullptr &&
        refresh.pending_player_guids.insert(raw_guid).second) {
      send_name_query(raw_guid);
    }

    if (!GuildBankLogEntryNeedsItemTemplate(entry.entry_type) ||
        query_cache.GetItemTemplate(entry.item_or_money) != nullptr ||
        !refresh.pending_item_entries.insert(entry.item_or_money).second) {
      continue;
    }

    static_cast<void>(query_cache.GetOrRequestItemTemplate(
        entry.item_or_money,
        QueryCache::QueryRequestOptions{
            .callback_key = callback_key,
            .callback =
                [this, generation, item_entry = entry.item_or_money](
                    bool ) {
                  if (pending_log_refresh_.generation != generation) {
                    return;
                  }

                  static_cast<void>(ResolveGuildBankLogItemQuery(item_entry));
                },
        }));
  }

  const bool has_pending = !refresh.pending_player_guids.empty() ||
                           !refresh.pending_item_entries.empty();
  if (!has_pending) {
    refresh.active = false;
  }
  return has_pending;
}

bool GuildBankProtocolSessionState::ResolveGuildBankLogNameQuery(std::uint64_t raw_guid) {
  if (!pending_log_refresh_.active) {
    return false;
  }

  pending_log_refresh_.pending_player_guids.erase(raw_guid);
  return TryCompleteGuildBankLogRefresh();
}

bool GuildBankProtocolSessionState::ResolveGuildBankLogItemQuery(std::uint32_t item_entry) {
  if (!pending_log_refresh_.active) {
    return false;
  }

  pending_log_refresh_.pending_item_entries.erase(item_entry);
  return TryCompleteGuildBankLogRefresh();
}

void GuildBankProtocolSessionState::ResetStateOnPlayerEnterWorld() {
  last_bank_list_ = {};
  last_bank_log_ = {};
  last_bank_text_ = {};
  pending_list_refresh_ = {};
  pending_log_refresh_ = {};
}

void GuildBankProtocolSessionState::Clear() {
  ResetStateOnPlayerEnterWorld();
  last_money_withdrawn_ = {};
}

bool GuildBankProtocolSessionState::TryCompleteGuildBankListRefresh() {
  if (!pending_list_refresh_.active ||
      !pending_list_refresh_.pending_item_entries.empty()) {
    return false;
  }

  pending_list_refresh_.active = false;
  pending_list_refresh_.pending_item_entries.clear();
  return true;
}

bool GuildBankProtocolSessionState::TryCompleteGuildBankLogRefresh() {
  if (!pending_log_refresh_.active ||
      !pending_log_refresh_.pending_player_guids.empty() ||
      !pending_log_refresh_.pending_item_entries.empty()) {
    return false;
  }

  pending_log_refresh_.active = false;
  pending_log_refresh_.pending_player_guids.clear();
  pending_log_refresh_.pending_item_entries.clear();
  return true;
}

}

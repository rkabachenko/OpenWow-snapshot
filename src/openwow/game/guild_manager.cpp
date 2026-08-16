
#include "openwow/game/guild_manager.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <optional>
#include <string_view>

#include "openwow/data/db_cache_instances.h"
#include "openwow/data/wdb_persistence.h"
#include "openwow/game/bnet_message_reflect.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/query_cache.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"

namespace openwow::game {

using net::wotlk::Opcode;
using net::wotlk::WorldPacket;

namespace detail {
namespace {

constexpr std::size_t kGuildEventParamMaxBytes = 0x400 - 1;
constexpr std::size_t kGuildRosterMotdMaxBytes = 0x204 - 1;
constexpr std::size_t kGuildRosterMotdMaxLogicalChars = 129;
constexpr std::size_t kGuildQueryNameBytes = 0x60;
constexpr std::size_t kGuildQueryRankNameBytes = 0x40;
constexpr std::size_t kGuildQueryRankCount = 10;
constexpr std::size_t kGuildQueryRecordSize = 764;
constexpr std::size_t kGuildQueryNameOffset = 4;
constexpr std::size_t kGuildQueryRankOffset = 100;
constexpr std::size_t kGuildQueryEmblemOffset = 740;
constexpr std::size_t kGuildQueryRankCountOffset = 760;

bool ReadBoundedCString(PacketReader& reader, std::string& out,
                        const std::size_t max_bytes_including_nul) {
  out.clear();
  if (max_bytes_including_nul == 0) {
    return false;
  }

  for (std::size_t consumed = 0; consumed < max_bytes_including_nul;
       ++consumed) {
    std::uint8_t byte = 0;
    if (!reader.ReadU8(byte)) {
      return false;
    }
    if (byte == 0) {
      return true;
    }
    out.push_back(static_cast<char>(byte));
  }

  out.clear();
  return false;
}

std::string SanitizeGuildRosterText(const std::string_view raw_text,
                                    const std::size_t max_copied_bytes,
                                    const std::size_t max_logical_chars) {
  if (raw_text.empty()) {
    return {};
  }

  std::uint32_t validated_chars = 0;
  if (!BNetReflect_ValidateUTF8(static_cast<std::uint32_t>(raw_text.size()),
                                raw_text.data(), &validated_chars)) {
    return {};
  }
  (void)validated_chars;

  std::string sanitized;
  sanitized.reserve(std::min(raw_text.size(), max_copied_bytes));

  for (const char ch : raw_text) {
    if (ch == '|') {
      continue;
    }
    if (sanitized.size() >= max_copied_bytes) {
      break;
    }
    sanitized.push_back(ch);
  }

  std::size_t logical_length = 0;
  for (std::size_t index = 0; index < sanitized.size(); ++index) {
    const auto byte = static_cast<std::uint8_t>(sanitized[index]);
    if ((byte & 0xC0u) == 0x80u) {
      continue;
    }

    ++logical_length;
    if (logical_length == max_logical_chars) {
      sanitized.resize(index);
      break;
    }
  }

  return sanitized;
}

std::string ClampGuildLogicalLength(const std::string_view raw_text,
                                    const std::size_t max_copied_bytes,
                                    const std::size_t max_logical_chars,
                                    const bool remove_pipes) {
  std::string sanitized;
  sanitized.reserve(std::min(raw_text.size(), max_copied_bytes));

  for (const char ch : raw_text) {
    if (remove_pipes && ch == '|') {
      continue;
    }
    if (sanitized.size() >= max_copied_bytes) {
      break;
    }
    sanitized.push_back(ch);
  }

  std::size_t logical_length = 0;
  for (std::size_t index = 0; index < sanitized.size(); ++index) {
    const auto byte = static_cast<std::uint8_t>(sanitized[index]);
    if ((byte & 0xC0u) == 0x80u) {
      continue;
    }

    ++logical_length;
    if (logical_length == max_logical_chars) {
      sanitized.resize(index);
      break;
    }
  }

  return sanitized;
}

void WriteU32LE(std::vector<std::uint8_t>& bytes, const std::size_t offset,
                const std::uint32_t value) {
  bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xFFu);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
  bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
  bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

[[nodiscard]] std::uint32_t ReadU32LE(const std::vector<std::uint8_t>& bytes,
                                      const std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset + 0]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void WriteFixedCString(std::vector<std::uint8_t>& bytes, const std::size_t offset,
                       const std::size_t field_size, const std::string_view value) {
  std::memset(bytes.data() + offset, 0, field_size);
  const auto copied = std::min(field_size - 1, value.size());
  std::memcpy(bytes.data() + offset, value.data(), copied);
}

[[nodiscard]] std::string ReadFixedCString(const std::vector<std::uint8_t>& bytes,
                                           const std::size_t offset,
                                           const std::size_t field_size) {
  const auto* begin =
      reinterpret_cast<const char*>(bytes.data() + offset);
  const auto* end = begin;
  const auto* const limit = begin + field_size;
  while (end != limit && *end != '\0') {
    ++end;
  }
  return {begin, end};
}

[[nodiscard]] std::vector<std::uint8_t>
SerializeGuildQueryCacheRecord(const GuildInfo& info) {
  std::vector<std::uint8_t> bytes(kGuildQueryRecordSize, 0);
  WriteU32LE(bytes, 0, info.guild_id);
  WriteFixedCString(bytes, kGuildQueryNameOffset, kGuildQueryNameBytes, info.name);
  for (std::size_t index = 0; index < kGuildQueryRankCount; ++index) {
    WriteFixedCString(bytes, kGuildQueryRankOffset + index * kGuildQueryRankNameBytes,
                      kGuildQueryRankNameBytes, info.rank_names[index]);
  }
  WriteU32LE(bytes, kGuildQueryEmblemOffset + 0, info.emblem.style);
  WriteU32LE(bytes, kGuildQueryEmblemOffset + 4, info.emblem.color);
  WriteU32LE(bytes, kGuildQueryEmblemOffset + 8, info.emblem.border_style);
  WriteU32LE(bytes, kGuildQueryEmblemOffset + 12, info.emblem.border_color);
  WriteU32LE(bytes, kGuildQueryEmblemOffset + 16, info.emblem.background_color);
  WriteU32LE(bytes, kGuildQueryRankCountOffset, info.rank_count);
  return bytes;
}

[[nodiscard]] std::optional<GuildInfo>
ParseGuildQueryCacheRecord(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() != kGuildQueryRecordSize) {
    return std::nullopt;
  }

  GuildInfo info;
  info.guild_id = ReadU32LE(bytes, 0);
  info.name = ReadFixedCString(bytes, kGuildQueryNameOffset, kGuildQueryNameBytes);
  for (std::size_t index = 0; index < kGuildQueryRankCount; ++index) {
    info.rank_names[index] = ReadFixedCString(
        bytes, kGuildQueryRankOffset + index * kGuildQueryRankNameBytes,
        kGuildQueryRankNameBytes);
  }
  info.emblem.style = ReadU32LE(bytes, kGuildQueryEmblemOffset + 0);
  info.emblem.color = ReadU32LE(bytes, kGuildQueryEmblemOffset + 4);
  info.emblem.border_style = ReadU32LE(bytes, kGuildQueryEmblemOffset + 8);
  info.emblem.border_color = ReadU32LE(bytes, kGuildQueryEmblemOffset + 12);
  info.emblem.background_color = ReadU32LE(bytes, kGuildQueryEmblemOffset + 16);
  info.rank_count = ReadU32LE(bytes, kGuildQueryRankCountOffset);

  if (info.guild_id == 0 || info.name.empty()) {
    return std::nullopt;
  }

  return info;
}

}

std::string SanitizeGuildEventParam(const std::string_view raw_text) {
  std::string sanitized;
  sanitized.reserve(std::min(raw_text.size(), kGuildEventParamMaxBytes));

  for (std::size_t index = 0; index < raw_text.size(); ++index) {
    if (sanitized.size() >= kGuildEventParamMaxBytes) {
      break;
    }

    const char ch = raw_text[index];
    if (ch == '\r' || ch == '\n') {
      break;
    }
    if ((ch == '\\' || ch == '|') && index + 1 < raw_text.size() &&
        raw_text[index + 1] == 'n') {
      break;
    }

    sanitized.push_back(ch);
  }

  return sanitized;
}

std::string SanitizeGuildEventRosterMotd(const std::string_view raw_text) {
  return ClampGuildLogicalLength(SanitizeGuildEventParam(raw_text),
                                 kGuildRosterMotdMaxBytes,
                                 kGuildRosterMotdMaxLogicalChars, true);
}

bool ParseGuildRosterPacket(const std::uint8_t* data, const std::size_t len,
                            GuildRoster* out_roster) {
  if (out_roster == nullptr) {
    return false;
  }

  PacketReader reader(data, len);
  GuildRoster parsed;

  std::uint32_t member_count = 0;
  if (!reader.ReadU32(member_count)) {
    return false;
  }

  std::string raw_motd;
  if (!ReadBoundedCString(reader, raw_motd, 0x204)) {
    return false;
  }
  parsed.motd = SanitizeGuildRosterText(raw_motd, 0x204 - 1, 129);

  std::string raw_info_text;
  if (!ReadBoundedCString(reader, raw_info_text, 0x7D4)) {
    return false;
  }
  parsed.info_text = SanitizeGuildRosterText(raw_info_text, 0x7D4 - 1, 501);

  std::uint32_t rank_count = 0;
  if (!reader.ReadU32(rank_count)) {
    return false;
  }

  const auto stored_rank_count =
      std::min<std::uint32_t>(rank_count, kGuildRanksMaxCount);
  parsed.ranks.resize(stored_rank_count);
  for (std::uint32_t index = 0; index < rank_count; ++index) {
    GuildRankInfo rank{};
    if (!reader.ReadU32(rank.flags) ||
        !reader.ReadU32(rank.withdraw_gold_limit)) {
      return false;
    }
    for (int tab = 0; tab < static_cast<int>(GuildSystem::kGuildBankMaxTabs); ++tab) {
      if (!reader.ReadU32(rank.tab_flags[tab]) ||
          !reader.ReadU32(rank.tab_withdraw_item_limit[tab])) {
        return false;
      }
    }

    if (index < stored_rank_count) {
      parsed.ranks[index] = rank;
    }
  }

  parsed.members.resize(member_count);
  for (auto& member : parsed.members) {
    if (!reader.ReadGuid(member.guid) || !reader.ReadU8(member.status)) {
      return false;
    }
    if (!ReadBoundedCString(reader, member.name, 0x30)) {
      return false;
    }
    if (!reader.ReadI32(member.rank_id) || !reader.ReadU8(member.level) ||
        !reader.ReadU8(member.class_id) || !reader.ReadU8(member.gender) ||
        !reader.ReadI32(member.area_id)) {
      return false;
    }

    member.last_save = 0.0f;
    if (member.status == 0 && !reader.ReadFloat(member.last_save)) {
      return false;
    }

    std::string raw_note;
    if (!ReadBoundedCString(reader, raw_note, 0x80)) {
      return false;
    }
    member.note = SanitizeGuildRosterText(raw_note, 0x80 - 1, 32);

    std::string raw_officer_note;
    if (!ReadBoundedCString(reader, raw_officer_note, 0x80)) {
      return false;
    }
    member.officer_note =
        SanitizeGuildRosterText(raw_officer_note, 0x80 - 1, 32);
  }

  *out_roster = std::move(parsed);
  return true;
}

}

bool GuildManager::HandleGuildQueryResponse(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  GuildInfo info;
  if (!r.ReadU32(info.guild_id)) {
    return false;
  }
  pending_guild_queries_.erase(info.guild_id);
  if (!detail::ReadBoundedCString(r, info.name, 0x60)) {
    return false;
  }

  for (int i = 0; i < kGuildRanksMaxCount; ++i) {
    if (!detail::ReadBoundedCString(r, info.rank_names[i], 0x40)) {
      return false;
    }
  }

  if (!r.ReadU32(info.emblem.style) || !r.ReadU32(info.emblem.color) ||
      !r.ReadU32(info.emblem.border_style) ||
      !r.ReadU32(info.emblem.border_color) ||
      !r.ReadU32(info.emblem.background_color)) {
    return false;
  }

  if (!r.ReadU32(info.rank_count)) {
    return false;
  }

  auto &wdb_cache = db_cache_runtime_.cache();
  auto &wdb_persistence = db_cache_runtime_.persistence();
  if (info.name.empty()) {
    InvalidateCachedGuildInfo(info.guild_id);
    return true;
  }

  guild_query_cache_[info.guild_id] = info;
  wdb_cache.UpdateEntry(openwow::data::WDBCacheType::Guild, info.guild_id,
                        detail::SerializeGuildQueryCacheRecord(info),
                        openwow::data::wdb_format::kVersion_GuildStats);
  wdb_persistence.SetDirty(openwow::data::WDBCacheType::Guild);
  guild_info_ = std::move(info);
  return true;
}

const GuildInfo* GuildManager::FindCachedGuildInfo(
    const std::uint32_t guild_id) const {
  const auto it = guild_query_cache_.find(guild_id);
  if (it != guild_query_cache_.end()) {
    return &it->second;
  }

  const auto persisted = db_cache_runtime_.cache().Get(
      openwow::data::WDBCacheType::Guild, guild_id);
  if (!persisted.has_value()) {
    return nullptr;
  }

  const auto decoded = detail::ParseGuildQueryCacheRecord(persisted->data);
  if (!decoded.has_value()) {
    return nullptr;
  }

  const auto [inserted_it, _] = guild_query_cache_.emplace(guild_id, *decoded);
  return &inserted_it->second;
}

bool GuildManager::BeginGuildQuery(const std::uint32_t guild_id) {
  if (guild_id == 0u || FindCachedGuildInfo(guild_id) != nullptr) {
    return false;
  }
  return pending_guild_queries_.insert(guild_id).second;
}

void GuildManager::InvalidateCachedGuildInfo(const std::uint32_t guild_id) {
  guild_query_cache_.erase(guild_id);
  pending_guild_queries_.erase(guild_id);
  if (guild_info_.has_value() && guild_info_->guild_id == guild_id) {
    guild_info_.reset();
  }
  if (db_cache_runtime_.cache().InvalidateEntry(
          openwow::data::WDBCacheType::Guild, guild_id)) {
    db_cache_runtime_.persistence().SetDirty(
        openwow::data::WDBCacheType::Guild);
  }
}

void GuildManager::ClearGuildQueryCacheForClientCacheVersion() {
  guild_query_cache_.clear();
  pending_guild_queries_.clear();
  guild_info_.reset();
}

bool GuildManager::HandleGuildRoster(const std::uint8_t* data,
                                     std::size_t len) {
  GuildRoster roster;
  if (!detail::ParseGuildRosterPacket(data, len, &roster)) {
    return false;
  }

  roster_ = std::move(roster);
  return true;
}

bool GuildManager::HandleGuildEvent(const std::uint8_t* data,
                                    std::size_t len) {
  PacketReader r(data, len);
  GuildEvent evt;

  std::uint8_t type, param_count;
  if (!r.ReadU8(type) || !r.ReadU8(param_count)) return false;
  evt.type = static_cast<GuildEventType>(type);

  evt.params.resize(param_count);
  for (std::uint8_t i = 0; i < param_count; ++i) {
    if (!r.ReadCString(evt.params[i])) {
      return false;
    }
    evt.params[i] = detail::SanitizeGuildEventParam(evt.params[i]);
  }

  if (type == 3 || type == 4 || type == 12 || type == 13) {
    if (r.HasBytes(8)) (void)r.ReadGuid(evt.guid);
  }

  last_event_ = std::move(evt);
  return true;
}

void GuildManager::UpdateCachedRosterMotd(const std::string& motd) {
  if (!roster_.has_value()) {
    return;
  }
  roster_->motd = motd;
}

bool GuildManager::HandleGuildCommandResult(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  GuildCommandResult res;
  if (!r.ReadI32(res.command)) return false;
  if (!r.ReadCString(res.name, 0x60u)) return false;
  if (!r.ReadI32(res.result)) return false;
  last_command_result_ = std::move(res);
  return true;
}

bool GuildManager::HandleGuildInvite(const std::uint8_t* data,
                                     std::size_t len) {
  PacketReader r(data, len);
  std::string invite_from;
  std::string invite_guild_name;
  if (!r.ReadCString(invite_from, 0x30u)) return false;
  if (!r.ReadCString(invite_guild_name, 0x60u)) return false;
  invite_from_ = std::move(invite_from);
  invite_guild_name_ = std::move(invite_guild_name);
  return true;
}

bool GuildManager::HandleGuildPermissions(const std::uint8_t* data,
                                          std::size_t len) {
  PacketReader r(data, len);
  GuildPermissions p;
  if (!r.ReadU32(p.rank_id) || !r.ReadI32(p.flags) ||
      !r.ReadI32(p.withdraw_gold_limit))
    return false;
  std::int8_t num_tabs;
  if (!r.ReadU8(reinterpret_cast<uint8_t&>(num_tabs))) return false;
  p.num_tabs = num_tabs;

  for (int t = 0; t < static_cast<int>(GuildSystem::kGuildBankMaxTabs); ++t)
    if (!r.ReadI32(p.tabs[t].flags) ||
        !r.ReadI32(p.tabs[t].withdraw_item_limit))
      return false;

  permissions_ = std::move(p);
  return true;
}

bool GuildManager::HandleGuildEventLogQuery(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  std::uint8_t entry_count = 0;
  if (!r.ReadU8(entry_count)) return false;

  constexpr std::size_t kMaxGuildEventLogEntries = 100;
  const auto stored_entry_count =
      std::min<std::size_t>(entry_count, kMaxGuildEventLogEntries);
  const auto now = std::time(nullptr);

  event_log_.clear();
  event_log_.reserve(stored_entry_count);
  pending_event_log_refresh_ = {};

  for (std::size_t i = 0; i < stored_entry_count; ++i) {
    GuildEventLogEntry e;
    if (!r.ReadU8(e.type)) return false;
    if (!r.ReadGuid(e.player)) return false;

    if (e.type != 2 && e.type != 6) {
      if (!r.ReadGuid(e.other)) return false;
    }

    if (e.type == 3 || e.type == 4) {
      if (!r.ReadU8(e.rank_id)) return false;
    }

    std::uint32_t seconds_since_event = 0;
    if (!r.ReadU32(seconds_since_event)) return false;
    e.event_time = now - static_cast<std::time_t>(seconds_since_event);
    event_log_.push_back(e);
  }
  return true;
}

bool GuildManager::BeginGuildEventLogRefresh(
    QueryCache& query_cache,
    const std::function<void(std::uint64_t)>& send_name_query) {
  pending_event_log_refresh_ = {};

  auto queue_guid = [&](const std::uint64_t raw_guid) {
    if (raw_guid == 0 || query_cache.GetPlayerName(raw_guid) != nullptr) {
      return;
    }

    pending_event_log_refresh_.pending_player_guids.insert(raw_guid);
    if (query_cache.RequestNameQuery(raw_guid) &&
        !query_cache.HasNameQueryDispatcher()) {
      send_name_query(raw_guid);
    }
  };

  for (const auto& entry : event_log_) {
    queue_guid(entry.player.GetRawValue());
    queue_guid(entry.other.GetRawValue());
  }

  pending_event_log_refresh_.active =
      !pending_event_log_refresh_.pending_player_guids.empty();
  return pending_event_log_refresh_.active;
}

bool GuildManager::ResolveGuildEventLogNameQuery(std::uint64_t raw_guid) {
  if (!pending_event_log_refresh_.active) {
    return false;
  }

  pending_event_log_refresh_.pending_player_guids.erase(raw_guid);
  return TryCompleteGuildEventLogRefresh();
}

WorldPacket GuildManager::BuildGuildQuery(std::uint32_t guild_id) {
  WorldPacket pkt(Opcode::CMSG_GUILD_QUERY);
  pkt.AppendU32(guild_id);
  return pkt;
}

WorldPacket GuildManager::BuildGuildRoster() {
  return WorldPacket(Opcode::CMSG_GUILD_ROSTER);
}

WorldPacket GuildManager::BuildGuildInvite(const std::string& name) {
  WorldPacket pkt(Opcode::CMSG_GUILD_INVITE);
  pkt.AppendString(name.c_str());
  return pkt;
}

WorldPacket GuildManager::BuildGuildAccept() {
  return WorldPacket(Opcode::CMSG_GUILD_ACCEPT);
}

WorldPacket GuildManager::BuildGuildDecline() {
  return WorldPacket(Opcode::CMSG_GUILD_DECLINE);
}

WorldPacket GuildManager::BuildGuildLeave() {
  return WorldPacket(Opcode::CMSG_GUILD_LEAVE);
}

WorldPacket GuildManager::BuildGuildRemove(const std::string& name) {
  WorldPacket pkt(Opcode::CMSG_GUILD_REMOVE);
  pkt.AppendString(name.c_str());
  return pkt;
}

WorldPacket GuildManager::BuildGuildMotd(const std::string& motd) {
  return net::wotlk::PacketSender::BuildGuildMotd(motd);
}

WorldPacket GuildManager::BuildGuildSetPublicNote(const std::string& name,
                                                  const std::string& note) {
  WorldPacket pkt(Opcode::CMSG_GUILD_SET_PUBLIC_NOTE);
  pkt.AppendString(name.c_str());
  pkt.AppendString(note.c_str());
  return pkt;
}

WorldPacket GuildManager::BuildGuildSetOfficerNote(const std::string& name,
                                                   const std::string& note) {
  WorldPacket pkt(Opcode::CMSG_GUILD_SET_OFFICER_NOTE);
  pkt.AppendString(name.c_str());
  pkt.AppendString(note.c_str());
  return pkt;
}

WorldPacket GuildManager::BuildGuildPromote(const std::string& name) {
  WorldPacket pkt(Opcode::CMSG_GUILD_PROMOTE);
  pkt.AppendString(name.c_str());
  return pkt;
}

WorldPacket GuildManager::BuildGuildDemote(const std::string& name) {
  WorldPacket pkt(Opcode::CMSG_GUILD_DEMOTE);
  pkt.AppendString(name.c_str());
  return pkt;
}

WorldPacket GuildManager::BuildGuildDisband() {
  return WorldPacket(Opcode::CMSG_GUILD_DISBAND);
}

WorldPacket GuildManager::BuildGuildEventLogQuery() {
  return WorldPacket(Opcode::MSG_GUILD_EVENT_LOG_QUERY);
}

void GuildManager::Clear() {
  ClearGuildQueryCacheForClientCacheVersion();
  roster_.reset();
  permissions_.reset();
  event_log_.clear();
  last_event_.reset();
  last_command_result_.reset();
  invite_from_.clear();
  invite_guild_name_.clear();
  guild_info_data_.reset();
  declined_name_.clear();
  pending_event_log_refresh_ = {};
}

bool GuildManager::TryCompleteGuildEventLogRefresh() {
  if (!pending_event_log_refresh_.active ||
      !pending_event_log_refresh_.pending_player_guids.empty()) {
    return false;
  }

  pending_event_log_refresh_.active = false;
  return true;
}

bool GuildManager::HandleGuildInfoPacket(const std::uint8_t* data,
                                         std::size_t len) {
  PacketReader r(data, len);
  GuildInfoData info;
  if (!r.ReadCString(info.name, 0x60u)) return false;
  if (!r.ReadU32(info.created_packed_time)) return false;
  if (!r.ReadU32(info.num_members)) return false;
  if (!r.ReadU32(info.num_accounts)) return false;
  guild_info_data_ = std::move(info);
  return true;
}

bool GuildManager::HandleGuildDeclinePacket(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  std::string declined_name;
  if (!r.ReadCString(declined_name, 0x30u)) {
    return false;
  }
  declined_name_ = std::move(declined_name);
  return true;
}

}

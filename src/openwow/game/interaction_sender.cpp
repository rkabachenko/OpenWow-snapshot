
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/equipment/adapters/protocol/equipment_set_packet_codec.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/platform/system/os_system_info.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/attack_action_shapeshift.h"
#include "openwow/game/actions/adapters/protocol/wotlk_action_packets.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/commerce/auctions/adapters/protocol/auction_packet_codec.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/battlefield_mgr.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/chat_manager.h"
#include "openwow/game/emote_validation.h"
#include "openwow/game/inventory/loot/adapters/protocol/loot_packet_codec.h"
#include "openwow/game/gm_ticket_chat_log.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_on_use_spell.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/lfg_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/pet_manager.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/commerce/mail/adapters/protocol/mail_packet_codec.h"
#include "openwow/game/inventory/operations/player_item_packet_location.h"
#include "openwow/game/social_manager.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/game/commerce/trade/adapters/protocol/trade_packet_codec.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/update_object_parser.h"
#include "openwow/game/readable_text.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/world_scene_state.h"
#include "openwow/game/world_session.h"
#include "openwow/net/client_services.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/platform/process/os_platform.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <limits>
#include <string_view>
#include <utility>
#include "openwow/foundation/compiler/printf_format.h"
#include "openwow/foundation/compiler/wide_ctype.h"

namespace openwow::game {

using net::wotlk::Opcode;
using net::wotlk::PacketSender;
using net::wotlk::WorldPacket;

void InteractionSender::SendSaveEquipmentSet(
    const EquipmentSetSave& request) {
  Send(equipment_protocol::encode_save(request));
}

void InteractionSender::SendDeleteEquipmentSet(const ObjectGuid set) {
  Send(equipment_protocol::encode_delete(set));
}

void InteractionSender::SendAuctionSellItems(
    const std::uint64_t auctioneer,
    const std::vector<std::pair<std::uint64_t, std::uint32_t>>& items,
    const std::uint32_t start_bid, const std::uint32_t buyout,
    const std::uint32_t duration_minutes) {
  Send(auction_protocol::EncodeSellItem(
      auctioneer, items, start_bid, buyout, duration_minutes));
}

void InteractionSender::SendUseEquipmentSet(
    const EquipmentSetUse& request) {
  Send(equipment_protocol::encode_use(request));
}

namespace {

struct GMTicketSubmitContext {
  std::uint32_t map_id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  GMTicketChatLogPayload chat_log;
};

struct ActivePlayerWorldLocation {
  std::uint32_t map_id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

constexpr std::size_t kWhoFilterCopyLimit = 128;
constexpr std::size_t kWhoNamedFieldBufferSize = 48;
constexpr std::uint32_t kWhoMaxZoneCount = 10;
constexpr std::uint32_t kWhoMaxSearchStringCount = 4;
constexpr std::uint32_t kWhoDefaultMinLevel = 0;
constexpr std::uint32_t kWhoDefaultMaxLevel = 100;
constexpr std::uint32_t kAuraFlagCancelable = 0x10u;
constexpr std::uint32_t kActivatePrimaryTalentSpecSpellId = 63645;
constexpr std::uint32_t kActivateSecondaryTalentSpecSpellId = 63644;
constexpr std::size_t kBugReportSummaryStorageLimit = 4095;
constexpr std::size_t kBugReportLineBufferSize = 256;
constexpr std::uint64_t kBugReportGigahertzThresholdHz = 990000000ull;
constexpr std::uint32_t kTradeInitiatingSystemMessageId = 200;
constexpr std::uint32_t kTradeAlreadyPendingSystemMessageId = 211;
constexpr std::string_view kBugReportVersionString =
    "WoW [Release] Build 12340 (Jun 24 2010 23:54:57)";

[[nodiscard]] bool AuraCanBeCanceled(const AuraInfo &aura) {
  return aura.spell_id != 0u && (aura.flags & kAuraFlagCancelable) != 0u;
}

template <typename EmitFn>
void ForEachAttackActionAutoCancelAuraSpellId(const CGUnit_C &unit, const data::dbc::DbcLoader &dbc,
                                              EmitFn &&emit) {
  if (unit.Interaction().CurrentShapeshiftFormRequiresTurnSensitiveUse()) {
    const auto current_form = static_cast<std::uint32_t>(unit.Animation().GetShapeshiftForm());
    for (const auto &aura : unit.Auras().All()) {
      if (!AuraCanBeCanceled(aura)) {
        continue;
      }

      const auto *spell = dbc.spell().LookupEntry(aura.spell_id);
      if (spell != nullptr && SpellAppliesShapeshiftForm(*spell, current_form)) {
        emit(aura.spell_id);
      }
    }
  }

  if (unit.Presentation().DisplayId() ==
      unit.Presentation().NativeDisplayId()) {
    return;
  }

  for (const auto &aura : unit.Auras().All()) {
    if (!AuraCanBeCanceled(aura)) {
      continue;
    }

    const auto *spell = dbc.spell().LookupEntry(aura.spell_id);
    if (spell == nullptr) {
      continue;
    }

    for (std::size_t effect_index = 0; effect_index < spell->effect.size(); ++effect_index) {
      if (spell->effect_apply_aura[effect_index] != kShapeshiftAuraType) {
        continue;
      }

      const auto form_id = static_cast<std::uint32_t>(spell->effect_misc_value[effect_index]);
      if (ShapeshiftFormHasAttackActionCancelableDisplay(dbc, form_id)) {
        emit(aura.spell_id);
        break;
      }
    }
  }
}

template <typename SendFn>
void SendAttackActionAutoCancelPackets(const WorldSession &session, const CGUnit_C &active_player,
                                       SendFn &&send_cancel_aura) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr || !active_player.Movement().CanTurn() ||
      active_player.Interaction()
          .SuppressesAttackActionShapeshiftAutoCancel()) {
    return;
  }

  std::vector<std::uint32_t> spell_ids;
  ForEachAttackActionAutoCancelAuraSpellId(active_player, *dbc, [&](const std::uint32_t spell_id) {
    if (std::find(spell_ids.begin(), spell_ids.end(), spell_id) == spell_ids.end()) {
      spell_ids.push_back(spell_id);
    }
  });

  for (const auto spell_id : spell_ids) {
    send_cancel_aura(spell_id);
  }
}

template <typename SendFn>
void ApplyAttackActionAutoCancelForSpell(WorldSession *session, const std::uint32_t spell_id,
                                         SendFn &&send_cancel_aura) {
  if (session == nullptr) {
    return;
  }

  const auto *dbc = session->GetDbcLoader();
  const auto *active_player = session->objects().GetActivePlayer();
  if (dbc == nullptr || active_player == nullptr) {
    return;
  }

  const auto *spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr || !SpellHasAttackActionEffect(*spell)) {
    return;
  }

  SendAttackActionAutoCancelPackets(*session, *active_player,
                                    std::forward<SendFn>(send_cancel_aura));
}

template <typename SendFn>
void ApplyAttackActionAutoCancelForItemUse(WorldSession *session, const ItemInstance &item,
                                           SendFn &&send_cancel_aura) {
  if (session == nullptr) {
    return;
  }

  const auto *dbc = session->GetDbcLoader();
  const auto *active_player = session->objects().GetActivePlayer();
  if (dbc == nullptr || active_player == nullptr) {
    return;
  }

  const auto *item_template = session->item_definitions().GetItem(item.entry);
  const auto *on_use_spell =
      item_template != nullptr ? FindFirstOnUseSpell(*item_template) : nullptr;
  if (on_use_spell == nullptr) {
    return;
  }

  const auto *spell = dbc->spell().LookupEntry(on_use_spell->spell_id);
  if (spell == nullptr || !SpellHasAttackActionEffect(*spell)) {
    return;
  }

  SendAttackActionAutoCancelPackets(*session, *active_player,
                                    std::forward<SendFn>(send_cancel_aura));
}

void ApplyLocalStandStateChange(WorldSession &session, const std::uint8_t stand_state) {
  auto *player = session.objects().GetMutablePlayer(session.objects().GetLocalPlayerGuid());
  if (player == nullptr) {
    return;
  }
  player->Animation().ApplyRequestedStandState(session, stand_state);
}

struct DecodedWhoCodepoint {

  std::uint32_t value = 0;
  int bytes = 0;
};

DecodedWhoCodepoint DecodeWhoFilterCodepoint(const char *cursor) {
  if (cursor == nullptr || *cursor == '\0') {
    return {};
  }

  const char *next = cursor;
  std::uint32_t raw_codepoint = 0;
  std::uint32_t upper_codepoint = 0;
  int consumed = core::SStrGetNextUTF8Char_ToUpper(&raw_codepoint, &next, &upper_codepoint);
  if (consumed <= 0) {
    return {};
  }

  return {
      .value = raw_codepoint,
      .bytes = consumed,
  };
}

bool IsWhoWhitespace(const DecodedWhoCodepoint &decoded) {
  if (decoded.bytes <= 0) {
    return false;
  }
  return openwow::compiler::IsUnicodeWhitespace(decoded.value);
}

void SkipWhoFilterWhitespace(const char *&cursor) {
  while (true) {
    const auto decoded = DecodeWhoFilterCodepoint(cursor);
    if (!IsWhoWhitespace(decoded)) {
      return;
    }
    cursor += decoded.bytes;
  }
}

bool ReadNextWhoFilterToken(const char *&cursor, std::string &token) {
  SkipWhoFilterWhitespace(cursor);
  if (cursor == nullptr || *cursor == '\0') {
    return false;
  }

  token.clear();
  bool in_quotes = false;
  while (true) {
    const auto decoded = DecodeWhoFilterCodepoint(cursor);
    if (decoded.bytes <= 0) {
      break;
    }

    if (decoded.value == L'"') {
      in_quotes = !in_quotes;
      cursor += decoded.bytes;
      continue;
    }

    if (!in_quotes && IsWhoWhitespace(decoded)) {
      break;
    }

    token.append(cursor, static_cast<std::size_t>(decoded.bytes));
    cursor += decoded.bytes;
  }

  return true;
}

template <std::size_t N> std::string CopyWhoCString(std::string_view text) {
  std::array<char, N> buffer{};
  const std::string owned_text(text);
  core::SStrCopy(buffer.data(), owned_text.c_str(), buffer.size());
  return buffer.data();
}

bool WhoUtf8ContainsNoCase(std::string_view haystack, std::string_view needle) {
  if (haystack.empty()) {
    return false;
  }
  if (needle.empty()) {
    return true;
  }

  const std::string haystack_text(haystack);
  const std::string needle_text(needle);
  const auto needle_codepoints = core::CountLegacyUtf8Codepoints(needle_text);
  std::size_t offset = 0;

  while (offset < haystack_text.size()) {
    const char *candidate = haystack_text.c_str() + offset;
    if (core::SStrCmpUTF8NoCase(candidate, needle_text.c_str(), needle_codepoints) == 0) {
      return true;
    }

    const char *next = candidate;
    std::uint32_t raw_codepoint = 0;
    std::uint32_t upper_codepoint = 0;
    int consumed = core::SStrGetNextUTF8Char_ToUpper(&raw_codepoint, &next, &upper_codepoint);
    if (consumed <= 0) {
      break;
    }
    offset += static_cast<std::size_t>(consumed);
  }

  return false;
}

bool WhoTokenMatchesLocalizedTag(const std::string &token, const std::string &tag) {
  if (tag.empty()) {
    return false;
  }

  return core::SStrCmpUTF8NoCase(token.c_str(), tag.c_str(),
                                 core::CountLegacyUtf8Codepoints(tag)) == 0;
}

std::uint32_t ParseWhoAsciiUnsigned(const char *text) {
  std::uint64_t value = 0;
  while (*text != '\0' && std::isdigit(static_cast<unsigned char>(*text)) != 0) {
    value = value * 10u + static_cast<std::uint64_t>(*text - '0');
    if (value > std::numeric_limits<std::uint32_t>::max()) {
      return std::numeric_limits<std::uint32_t>::max();
    }
    ++text;
  }
  return static_cast<std::uint32_t>(value);
}

bool TryParseWhoLevelRange(const std::string &token, net::wotlk::WhoQuery &query) {
  if (token.empty()) {
    return false;
  }

  const unsigned char first = static_cast<unsigned char>(token.front());
  if (std::isdigit(first) != 0) {
    query.min_level = ParseWhoAsciiUnsigned(token.c_str());
    query.max_level = query.min_level;

    const char *cursor = token.c_str() + 1;
    while (*cursor != '\0' && std::isdigit(static_cast<unsigned char>(*cursor)) != 0) {
      ++cursor;
    }

    if (*cursor == '-') {
      ++cursor;
      query.max_level = kWhoDefaultMaxLevel;
    }

    if (*cursor != '\0' && std::isdigit(static_cast<unsigned char>(*cursor)) != 0) {
      query.max_level = ParseWhoAsciiUnsigned(cursor);
    }
    return true;
  }

  if (first == '-') {
    const char *cursor = token.c_str() + 1;
    if (*cursor != '\0' && std::isdigit(static_cast<unsigned char>(*cursor)) != 0) {
      query.min_level = kWhoDefaultMinLevel;
      query.max_level = ParseWhoAsciiUnsigned(cursor);
      return true;
    }
  }

  return false;
}

void AppendWhoZoneMatches(const data::dbc::DbcLoader *dbc, std::string_view zone_text,
                          net::wotlk::WhoQuery &query) {
  const auto previous_count = query.zones.size();
  if (previous_count < kWhoMaxZoneCount && dbc != nullptr) {
    for (const auto &entry : dbc->area_table()) {
      if (entry.parent_area != 0 || entry.name.empty()) {
        continue;
      }
      if (query.zones.size() >= kWhoMaxZoneCount) {
        break;
      }
      if (WhoUtf8ContainsNoCase(entry.name, zone_text)) {
        query.zones.push_back(entry.id);
      }
    }
  }

  if (previous_count == 0 && query.zones.empty()) {
    query.zones.push_back(0);
  }
}

void AppendWhoRaceMask(const data::dbc::DbcLoader *dbc, std::string_view race_text,
                       net::wotlk::WhoQuery &query) {
  if (query.race_mask == std::numeric_limits<std::uint32_t>::max()) {
    query.race_mask = 0;
  }
  if (dbc == nullptr) {
    return;
  }

  for (const auto &entry : dbc->chr_races()) {
    if (entry.name.empty()) {
      continue;
    }
    if (WhoUtf8ContainsNoCase(entry.name, race_text) && entry.id < 32) {
      query.race_mask |= (1u << entry.id);
    }
  }
}

void AppendWhoClassMask(const data::dbc::DbcLoader *dbc, std::string_view class_text,
                        net::wotlk::WhoQuery &query) {
  if (query.class_mask == std::numeric_limits<std::uint32_t>::max()) {
    query.class_mask = 0;
  }
  if (dbc == nullptr) {
    return;
  }

  for (const auto &entry : dbc->chr_classes()) {
    if (entry.name.empty()) {
      continue;
    }
    if (WhoUtf8ContainsNoCase(entry.name, class_text) && entry.id < 32) {
      query.class_mask |= (1u << entry.id);
    }
  }
}

struct ParsedWhoFilter {
  net::wotlk::WhoQuery query;
  WhoClientFilterInfo client_filter;
};

ParsedWhoFilter BuildWhoQueryFromFilter(const WorldSession *session, std::string_view filter,
                                        const std::uint32_t level_min,
                                        const std::uint32_t level_max,
                                        const std::uint32_t race_mask,
                                        const std::uint32_t class_mask) {
  ParsedWhoFilter parsed{};
  auto &query = parsed.query;
  auto &client_filter = parsed.client_filter;
  query.min_level = level_min;
  query.max_level = level_max;
  query.race_mask = race_mask;
  query.class_mask = class_mask;
  client_filter.min_level = level_min;
  client_filter.max_level = level_max;
  client_filter.race_mask = race_mask;
  client_filter.class_mask = class_mask;

  std::array<char, kWhoFilterCopyLimit + 1> filter_buffer{};
  const std::string owned_filter(filter);
  core::SStrCopy(filter_buffer.data(), owned_filter.c_str(), kWhoFilterCopyLimit);
  const auto filter_length = core::SStrLen(filter_buffer.data());
  filter_buffer[filter_length] = ' ';
  filter_buffer[filter_length + 1] = '\0';

  const auto *dbc = session != nullptr ? session->GetDbcLoader() : nullptr;
  auto &localization = Localization::Get();
  const std::string name_tag = localization.GetString("WHO_TAG_NAME");
  const std::string guild_tag = localization.GetString("WHO_TAG_GUILD");
  const std::string zone_tag = localization.GetString("WHO_TAG_ZONE");
  const std::string race_tag = localization.GetString("WHO_TAG_RACE");
  const std::string class_tag = localization.GetString("WHO_TAG_CLASS");

  const char *cursor = filter_buffer.data();
  std::string token;
  while (query.strings.size() < kWhoMaxSearchStringCount && ReadNextWhoFilterToken(cursor, token)) {
    if (WhoTokenMatchesLocalizedTag(token, name_tag)) {
      query.player_name =
          CopyWhoCString<kWhoNamedFieldBufferSize>(std::string_view(token).substr(name_tag.size()));
      client_filter.player_name = query.player_name;
      continue;
    }

    if (WhoTokenMatchesLocalizedTag(token, guild_tag)) {
      query.guild_name = CopyWhoCString<kWhoNamedFieldBufferSize>(
          std::string_view(token).substr(guild_tag.size()));
      client_filter.guild_name = query.guild_name;
      continue;
    }

    if (WhoTokenMatchesLocalizedTag(token, zone_tag)) {
      const std::string_view zone_term = std::string_view(token).substr(zone_tag.size());
      AppendWhoZoneMatches(dbc, zone_term, query);
      client_filter.zone_terms.emplace_back(zone_term);
      continue;
    }

    if (WhoTokenMatchesLocalizedTag(token, race_tag)) {
      AppendWhoRaceMask(dbc, std::string_view(token).substr(race_tag.size()), query);
      client_filter.race_mask = query.race_mask;
      continue;
    }

    if (WhoTokenMatchesLocalizedTag(token, class_tag)) {
      AppendWhoClassMask(dbc, std::string_view(token).substr(class_tag.size()), query);
      client_filter.class_mask = query.class_mask;
      continue;
    }

    if (TryParseWhoLevelRange(token, query)) {
      client_filter.min_level = query.min_level;
      client_filter.max_level = query.max_level;
      continue;
    }

    query.strings.push_back(token);
    client_filter.search_terms.push_back(token);
  }

  return parsed;
}

std::optional<ActivePlayerWorldLocation>
ResolveActivePlayerWorldLocation(const WorldSession &session) {
  const auto *active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr) {
    return std::nullopt;
  }

  ActivePlayerWorldLocation location;
  location.map_id = session.current_map_id();
  const auto position = active_player->GetPosition();
  location.x = position.x;
  location.y = position.y;
  location.z = position.z;
  return location;
}

std::optional<GMTicketSubmitContext> BuildGMTicketSubmitContext(const WorldSession &session) {
  const auto location = ResolveActivePlayerWorldLocation(session);
  if (!location.has_value()) {
    return std::nullopt;
  }

  GMTicketSubmitContext context;
  context.map_id = location->map_id;
  context.x = location->x;
  context.y = location->y;
  context.z = location->z;
  context.chat_log = GMTicketChatLog::Get().BuildPayload();
  return context;
}

void AppendBugReportChunk(std::string &summary, std::string_view chunk) {
  if (summary.size() >= kBugReportSummaryStorageLimit || chunk.empty()) {
    return;
  }

  const auto remaining = kBugReportSummaryStorageLimit - summary.size();
  summary.append(chunk.data(), std::min(chunk.size(), remaining));
}

OPENWOW_PRINTF_FORMAT(2, 3) void AppendBugReportFormat(
    std::string &summary, const char *format, ...) {
  if (summary.size() >= kBugReportSummaryStorageLimit) {
    return;
  }

  std::array<char, kBugReportLineBufferSize> line{};
  va_list args;
  va_start(args, format);
  std::vsnprintf(line.data(), line.size(), format, args);
  va_end(args);
  AppendBugReportChunk(summary, line.data());
}

[[nodiscard]] const char *BugReportProcessorVendorLabel(const core::CpuVendor vendor) {
  switch (vendor) {
  case core::CpuVendor::Intel:
    return "Intel";
  case core::CpuVendor::AMD:
    return "AMD";
  case core::CpuVendor::Unknown:
  default:
    return "Unknown";
  }
}

[[nodiscard]] const char *BugReportGenderLabel(const std::uint8_t gender) {
  switch (gender) {
  case 0:
    return "Male";
  case 1:
    return "Female";
  default:
    return "Unknown";
  }
}

[[nodiscard]] std::string LookupBugReportRaceName(const data::dbc::DbcLoader *dbc,
                                                  const std::uint8_t race_id,
                                                  const std::uint8_t gender) {
  if (dbc == nullptr) {
    return "Unknown";
  }

  const auto *entry = dbc->chr_races().LookupEntry(race_id);
  if (entry == nullptr) {
    return "Unknown";
  }

  const auto display_name = entry->DisplayNameForSex(gender);
  if (!display_name.empty()) {
    return std::string(display_name);
  }

  return entry->name.empty() ? "Unknown" : std::string(entry->name);
}

[[nodiscard]] std::string LookupBugReportClassName(const data::dbc::DbcLoader *dbc,
                                                   const std::uint8_t class_id,
                                                   const std::uint8_t gender) {
  if (dbc == nullptr) {
    return "Unknown";
  }

  const auto *entry = dbc->chr_classes().LookupEntry(class_id);
  if (entry == nullptr) {
    return "Unknown";
  }

  const auto display_name = entry->DisplayNameForSex(gender);
  if (!display_name.empty()) {
    return std::string(display_name);
  }

  return entry->name.empty() ? "Unknown" : std::string(entry->name);
}

[[nodiscard]] std::string LookupBugReportMapName(const data::dbc::DbcLoader *dbc,
                                                 const std::uint32_t map_id) {
  if (dbc == nullptr) {
    return {};
  }

  const auto *entry = dbc->map().LookupEntry(map_id);
  if (entry == nullptr || entry->name.empty()) {
    return {};
  }

  return std::string(entry->name);
}

[[nodiscard]] std::string LookupBugReportSpellName(const data::dbc::DbcLoader *dbc,
                                                   const std::uint32_t spell_id) {
  if (dbc == nullptr) {
    return "UNKNOWN";
  }

  const auto *entry = dbc->spell().LookupEntry(spell_id);
  if (entry == nullptr || entry->spell_name.empty()) {
    return "UNKNOWN";
  }

  return std::string(entry->spell_name);
}

void AppendBugReportWorldContext(std::string &summary, const WorldSession &session) {
  const auto *active_player = session.objects().GetActivePlayer();
  if (active_player == nullptr) {
    return;
  }

  const auto *dbc = session.GetDbcLoader();

  const auto player_name = active_player->ResolveRetailName(session);
  const auto race_name =
      LookupBugReportRaceName(dbc, active_player->State().GetRace(), active_player->State().GetGender());
  const auto class_name =
      LookupBugReportClassName(dbc, active_player->State().GetClass(), active_player->State().GetGender());

  AppendBugReportFormat(summary, "Character:\t%s (level %u %s %s %s)\n",
                        player_name.empty() ? "Unknown" : player_name.c_str(),
                        active_player->State().GetLevel(), race_name.c_str(),
                        BugReportGenderLabel(active_player->State().GetGender()), class_name.c_str());

  const std::uint32_t map_id = session.current_map_id();
  const std::string map_name = LookupBugReportMapName(dbc, map_id);
  if (!map_name.empty()) {
    AppendBugReportFormat(summary, "Map:\t\t%u (%s)\n", map_id, map_name.c_str());
  } else {
    AppendBugReportFormat(summary, "Map:\t\t%u\n", map_id);
  }

  const auto zone_text = session.scene_state().GetZoneText();
  if (!zone_text.empty()) {
    AppendBugReportFormat(summary, "Zone:\t\t%s", zone_text.c_str());
    const auto subzone_text = session.scene_state().GetSubZoneText();
    if (!subzone_text.empty()) {
      AppendBugReportFormat(summary, " - %s", subzone_text.c_str());
    }
    AppendBugReportChunk(summary, "\n");
  }

  const auto position = active_player->GetPosition();
  AppendBugReportFormat(summary, "Position:\t%f %f %f, %f\n", position.x, position.y, position.z,
                        position.facing);

  if (const auto *target = session.objects().GetTarget(); target != nullptr) {
    const auto target_name = target->GetName();
    if (!target_name.empty()) {
      AppendBugReportFormat(summary, "Target:\t%s\n", target_name.c_str());
    }
  }

  AppendBugReportChunk(summary, "Auras:\n");
  for (const auto &aura : active_player->Auras().All()) {
    if (aura.spell_id == 0u) {
      continue;
    }

    const auto spell_name = LookupBugReportSpellName(dbc, aura.spell_id);
    AppendBugReportFormat(summary, "\t%s(%u)\n", spell_name.c_str(), aura.spell_id);
  }
}

[[nodiscard]] std::string BuildClientBugReportSummary(const WorldSession &session) {
  std::string summary;
  summary.reserve(kBugReportSummaryStorageLimit);

  const auto username = platform::OS_GetUserName();
  if (!username.empty() && username != "UNKNOWN") {
    AppendBugReportFormat(summary, "Username:\t%s\n", username.c_str());
  }

  const auto computer_name = platform::OS_GetComputerName();
  if (!computer_name.empty() && computer_name != "UNKNOWN") {
    AppendBugReportFormat(summary, "Computer:\t%s\n", computer_name.c_str());
  }

  auto &system_info = core::OsSystemInfoDetector::Instance();
  system_info.Init();
  const auto &info = system_info.GetInfo();
  const auto processor_count = info.processorCount != 0u ? info.processorCount : 1u;
  AppendBugReportFormat(summary, "Processors:\t%u\n", processor_count);
  AppendBugReportFormat(summary, "Processor vendor:\t%s\n",
                        BugReportProcessorVendorLabel(info.cpuVendor));

  const auto processor_frequency_hz = platform::OS_GetProcessorFrequency();
  const bool use_ghz = processor_frequency_hz >= kBugReportGigahertzThresholdHz;
  const double speed_value = use_ghz ? static_cast<double>(processor_frequency_hz) / 1000000000.0
                                     : static_cast<double>(processor_frequency_hz) / 1000000.0;
  AppendBugReportFormat(summary, "Processor speed:\t%6.2f%cHz\n", speed_value, use_ghz ? 'G' : 'M');

  AppendBugReportFormat(summary, "Memory:\t%uMB\n",
                        static_cast<std::uint32_t>(platform::OS_GetPhysicalMemory() >> 20));
  AppendBugReportChunk(summary, "OS:\t\t");
  AppendBugReportChunk(summary, platform::OS_GetOSVersionString());
  AppendBugReportChunk(summary, "\n");
  AppendBugReportFormat(summary, "Version:\t%s\n", kBugReportVersionString.data());

  const auto &account_name = net::ClientServices::Instance().GetAccountName();
  if (account_name.empty()) {
    return summary;
  }

  AppendBugReportFormat(summary, "Realm:\t%s\n", net::GetRealmName());
  AppendBugReportFormat(summary, "Account:\t%s\n", account_name.c_str());
  AppendBugReportWorldContext(summary, session);
  return summary;
}

void AppendClientBugReportPayload(WorldPacket &pkt, const std::uint32_t report_type,
                                  const std::string &summary, const std::string &description) {
  const auto summary_length = static_cast<std::uint32_t>(summary.size() + 1u);
  const auto description_length = static_cast<std::uint32_t>(description.size() + 1u);

  pkt.AppendU32(report_type);
  pkt.AppendU32(summary_length);
  pkt.AppendBytes(reinterpret_cast<const std::uint8_t *>(summary.c_str()), summary_length);
  pkt.AppendU32(description_length);
  pkt.AppendBytes(reinterpret_cast<const std::uint8_t *>(description.c_str()), description_length);
}

void AppendGMTicketCreatePayload(WorldPacket &pkt, const GMTicketSubmitContext &context,
                                 const std::string &description, const bool need_response,
                                 const bool is_follow_up) {
  pkt.AppendU32(context.map_id);
  pkt.AppendFloat(context.x);
  pkt.AppendFloat(context.y);
  pkt.AppendFloat(context.z);
  pkt.AppendString(description.c_str());
  pkt.AppendU32(need_response ? 17u : 1u);
  pkt.AppendU8(is_follow_up ? 1u : 0u);
  pkt.AppendU32(context.chat_log.line_count);
  for (const std::uint32_t line_age : context.chat_log.line_ages) {
    pkt.AppendU32(line_age);
  }
  pkt.AppendU32(context.chat_log.decompressed_size);
  if (!context.chat_log.compressed_lines.empty()) {
    pkt.AppendBytes(context.chat_log.compressed_lines.data(),
                    context.chat_log.compressed_lines.size());
  }
}

std::optional<ItemInstance> ResolveUseItemPacketItem(const PlayerInventoryReplica &inventory,
                                                     const std::uint8_t bag,
                                                     const std::uint8_t slot) {
  if (bag == InventorySlots::kMainBag) {
    if (slot >= InventorySlots::kBagSlotsStart && slot < InventorySlots::kBagSlotsEnd) {
      const auto bag_index = static_cast<std::uint8_t>(slot - InventorySlots::kBagSlotsStart + 1);
      const auto *bag_info = inventory.GetBag(bag_index);
      if (bag_info == nullptr || bag_info->guid == 0) {
        return std::nullopt;
      }

      ItemInstance bag_item;
      bag_item.guid = bag_info->guid;
      bag_item.entry = bag_info->entry;
      bag_item.count = 1;
      return bag_item;
    }

    const auto *item = inventory.GetItemInSlot(slot);
    return item == nullptr ? std::nullopt : std::optional<ItemInstance>(*item);
  }

  if (bag < InventorySlots::kBagSlotsStart ||
      bag >= InventorySlots::kBagSlotsStart + PlayerInventoryReplica::kMaxBags) {
    return std::nullopt;
  }

  const auto *item = inventory.GetBagSlot(
      static_cast<std::uint8_t>(bag - InventorySlots::kBagSlotsStart + 1), slot);
  return item == nullptr ? std::nullopt : std::optional<ItemInstance>(*item);
}

[[nodiscard]] EmoteValidationState
BuildOutgoingTextEmoteValidationState(const WorldSession &session, const CGUnit_C &unit) {
  EmoteValidationState state;
  state.stand_state = unit.Animation().GetStandState();
  state.movement_flags = unit.GetMovementInfo().flags;
  state.caller_flag = true;

  if (const auto *spline = session.movement_spline_mgr().GetSpline(unit.GetGuid().GetRawValue());
      spline != nullptr) {
    state.has_active_spline = true;
    state.spline_flags = spline->GetSplineFlags();
  }

  return state;
}

[[nodiscard]] bool ShouldDisplayTextEmoteMovementWarning(const CGUnit_C &unit,
                                                         const ObjectManager &objects) {
  const std::uint32_t unit_flags = unit.GetUInt32(UNIT_FIELD_FLAGS);
  if ((unit_flags & 0x4u) == 0 && (unit_flags & 0x00C00004u) != 0) {
    return false;
  }

  if ((unit_flags & 0x01000000u) != 0) {
    auto controlling_guid = unit.GetGuidField(UNIT_FIELD_CHARM);
    if (controlling_guid.IsEmpty()) {
      controlling_guid = unit.GetGuidField(UNIT_FIELD_SUMMONEDBY);
    }

    const auto *controlling_unit = objects.GetUnit(controlling_guid);
    if (controlling_unit == nullptr || !controlling_unit->IsPlayer()) {
      return false;
    }

    return (controlling_unit->GetUInt32(UNIT_FIELD_FLAGS) & 0x1u) == 0;
  }

  if (!unit.IsPlayer() || !unit.GetGuidField(UNIT_FIELD_CHARM).IsEmpty()) {
    return false;
  }

  return (unit_flags & 0x1u) == 0;
}

[[nodiscard]] std::uint32_t LookupOutgoingTextEmoteVariant(const std::uint32_t text_emote_id,
                                                           const CGUnit_C &unit) {
  const auto selected_variant =
      unit.sound_runtime().GetEmotesTextSoundTable().LookupAndSelectVariant(
          unit.sound_runtime(),
          text_emote_id, unit.State().GetRace(), unit.State().GetGender());
  return static_cast<std::uint32_t>(selected_variant);
}

double DefaultProposalResponseTimeSeconds() {
  return openwow::core::GameClock::GetTickCountSeconds();
}

void PlayNamedSoundKit(openwow::audio::SoundRuntime& sound, std::string_view sound_kit_name) {
  const std::uint32_t sound_kit_id = sound.LookupSoundKitIdByName(sound_kit_name);
  if (sound_kit_id == 0) {
    return;
  }

  sound.ResetSoundKitVariationSelectionState(sound_kit_id);
  (void)sound.PlaySoundKit(sound_kit_id, nullptr, nullptr);
}

}

InteractionSender::InteractionSender(WorldSession& session) : session_(&session) {
  pending_bind_on_use_ = {};
  proposal_response_attempt_count_ = 0;
  proposal_response_window_anchor_seconds_ = 0.0;
}

bool InteractionSender::Send(WorldPacket &pkt) {
  if (!session_)
    return false;
  return session_->Send(pkt);
}

void InteractionSender::SendGossipHello(std::uint64_t guid) {
  auto pkt = PacketSender::BuildGossipHello(guid);
  Send(pkt);
}

void InteractionSender::SendGossipSelectOption(std::uint64_t guid, std::uint32_t menu_id,
                                               std::uint32_t option_id, const std::string &code) {

  WorldPacket pkt(Opcode::CMSG_GOSSIP_SELECT_OPTION);
  pkt.AppendU64(guid);
  pkt.AppendU32(menu_id);
  pkt.AppendU32(option_id);
  if (!code.empty()) {
    pkt.AppendString(code.c_str());
  }
  Send(pkt);
}

void InteractionSender::SendNpcTextQuery(std::uint64_t guid, std::uint32_t text_id) {
  WorldPacket pkt(Opcode::CMSG_NPC_TEXT_QUERY);
  pkt.AppendU32(text_id);
  pkt.AppendU64(guid);
  Send(pkt);
}

void InteractionSender::SendQuestGiverHello(std::uint64_t guid) {
  auto pkt = PacketSender::BuildQuestgiverHello(guid);
  Send(pkt);
}

void InteractionSender::SendQuestGiverQueryQuest(std::uint64_t guid, std::uint32_t quest_id) {
  WorldPacket pkt(Opcode::CMSG_QUESTGIVER_QUERY_QUEST);
  pkt.AppendU64(guid);
  pkt.AppendU32(quest_id);
  pkt.AppendU8(1);
  Send(pkt);
}

void InteractionSender::SendQuestGiverAcceptQuest(std::uint64_t guid, std::uint32_t quest_id,
                                                  std::uint32_t accept_packet_value) {
  auto pkt = PacketSender::BuildQuestgiverAcceptQuest(guid, quest_id, accept_packet_value);
  Send(pkt);
}

void InteractionSender::SendQuestGiverCompleteQuest(std::uint64_t guid, std::uint32_t quest_id) {
  auto pkt = PacketSender::BuildQuestgiverCompleteQuest(guid, quest_id);
  Send(pkt);
}

void InteractionSender::SendQuestGiverRequestReward(std::uint64_t guid, std::uint32_t quest_id) {
  auto pkt = PacketSender::BuildQuestgiverRequestReward(guid, quest_id);
  Send(pkt);
}

void InteractionSender::SendQuestGiverChooseReward(std::uint64_t guid, std::uint32_t quest_id,
                                                   std::uint32_t reward_index) {
  auto pkt = PacketSender::BuildQuestgiverChooseReward(guid, quest_id, reward_index);
  Send(pkt);
}

void InteractionSender::SendQuestPushResult(std::uint64_t receiver_guid, std::uint32_t quest_id,
                                            std::uint8_t result) {
  Send(PacketSender::BuildQuestPushResult(receiver_guid, quest_id, result));
}

void InteractionSender::SendQuestQuery(std::uint32_t quest_id) {
  WorldPacket pkt(Opcode::CMSG_QUEST_QUERY);
  pkt.AppendU32(quest_id);
  Send(pkt);
}

void InteractionSender::SendQuestPoiQuery(const std::vector<std::uint32_t> &quest_ids) {
  if (quest_ids.empty()) {
    return;
  }
  Send(PacketSender::BuildQuestPoiQuery(quest_ids));
}

void InteractionSender::SendQuestConfirmAccept(std::uint32_t quest_id) {
  WorldPacket pkt(Opcode::CMSG_QUEST_CONFIRM_ACCEPT);
  pkt.AppendU32(quest_id);
  Send(pkt);
}

void InteractionSender::SendQuestLogRemoveQuest(std::uint8_t slot) {
  WorldPacket pkt(Opcode::CMSG_QUESTLOG_REMOVE_QUEST);
  pkt.AppendU8(slot);
  Send(pkt);
}

void InteractionSender::SendQueryTime() {
  Send(PacketSender::BuildQueryTime());
}

void InteractionSender::SendListInventory(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_LIST_INVENTORY);
  pkt.AppendU64(guid);
  Send(pkt);
}

void InteractionSender::SendBuyItem(std::uint64_t vendor_guid, std::uint32_t item_id,
                                    std::uint32_t slot, std::uint32_t count,
                                    std::uint8_t bag) {
  auto pkt = PacketSender::BuildBuyItem(vendor_guid, item_id, slot, count, bag);
  Send(pkt);
}

void InteractionSender::SendBuyItemInSlot(std::uint64_t vendor_guid, std::uint32_t item_id,
                                          std::uint32_t vendor_slot, std::uint64_t target_guid,
                                          std::uint8_t target_slot, std::uint32_t count) {
  auto pkt = PacketSender::BuildBuyItemInSlot(vendor_guid, item_id, vendor_slot, target_guid,
                                              target_slot, count);
  Send(pkt);
}

void InteractionSender::SendSellItem(std::uint64_t vendor_guid, std::uint64_t item_guid,
                                     std::uint32_t count) {
  auto pkt = PacketSender::BuildSellItem(vendor_guid, item_guid, count);
  Send(pkt);
}

bool InteractionSender::SendItemRefundInfo(std::uint64_t item_guid) {
  return Send(PacketSender::BuildItemRefundInfo(item_guid));
}

bool InteractionSender::SendItemRefund(std::uint64_t item_guid) {
  return Send(PacketSender::BuildItemRefund(item_guid));
}

bool InteractionSender::SendSelfResurrect() {
  return Send(PacketSender::BuildSelfResurrect());
}

void InteractionSender::SendBuybackItem(std::uint64_t vendor_guid, std::uint32_t slot) {
  WorldPacket pkt(Opcode::CMSG_BUYBACK_ITEM);
  pkt.AppendU64(vendor_guid);
  pkt.AppendU32(slot);
  Send(pkt);
}

void InteractionSender::SendRepairItem(std::uint64_t vendor_guid, std::uint64_t item_guid,
                                       bool guild_bank) {
  WorldPacket pkt(Opcode::CMSG_REPAIR_ITEM);
  pkt.AppendU64(vendor_guid);
  pkt.AppendU64(item_guid);
  pkt.AppendU8(guild_bank ? 1 : 0);
  Send(pkt);
}

void InteractionSender::SendTrainerList(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_TRAINER_LIST);
  pkt.AppendU64(guid);
  Send(pkt);
}

void InteractionSender::SendTrainerBuySpell(std::uint64_t guid, std::uint32_t spell_id) {
  WorldPacket pkt(Opcode::CMSG_TRAINER_BUY_SPELL);
  pkt.AppendU64(guid);
  pkt.AppendU32(spell_id);
  Send(pkt);
}

void InteractionSender::SendLoot(std::uint64_t guid) {
  if (session_ != nullptr) {
    session_->loot().BeginLootRequest(ObjectGuid(guid));
  }
  auto pkt = PacketSender::BuildLoot(guid);
  Send(pkt);
}

void InteractionSender::SendAutoStoreLootItem(std::uint8_t slot) {
  auto pkt = PacketSender::BuildAutoStoreLootItem(slot);
  Send(pkt);
}

void InteractionSender::SendLootRelease(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_LOOT_RELEASE);
  pkt.AppendU64(guid);
  Send(pkt);
}

void InteractionSender::SendLootMoney() {
  WorldPacket pkt(Opcode::CMSG_LOOT_MONEY);
  Send(pkt);
}

void InteractionSender::SendAutoEquipItem(std::uint8_t bag, std::uint8_t slot) {
  auto pkt = PacketSender::BuildAutoEquipItem(bag, slot);
  Send(pkt);
}

void InteractionSender::SendGetMailList(std::uint64_t mailbox_guid) {
  auto pkt = mail_protocol::EncodeGetMailList(mailbox_guid);
  Send(pkt);
}

void InteractionSender::SendSendMail(std::uint64_t mailbox_guid, const std::string &recipient,
                                     const std::string &subject, const std::string &body,
                                     std::uint32_t money, std::uint32_t cod,
                                     std::uint32_t stationery,
                                     const std::vector<MailSendAttachment> &attachments,
                                     std::uint32_t package_id) {
  std::vector<mail_protocol::MailAttachment> protocol_attachments;
  protocol_attachments.reserve(attachments.size());
  for (const auto& attachment : attachments) {
    protocol_attachments.push_back({
        .slot = attachment.slot,
        .item_guid = attachment.item_guid,
    });
  }
  auto pkt = mail_protocol::EncodeSendMail(
      mailbox_guid, recipient, subject, body, stationery, money, cod,
      protocol_attachments, package_id);
  Send(pkt);
}

void InteractionSender::SendMailTakeMoney(std::uint64_t mailbox_guid, std::uint32_t mail_id) {
  auto pkt = mail_protocol::EncodeTakeMoney(mailbox_guid, mail_id);
  Send(pkt);
}

void InteractionSender::SendMailTakeItem(std::uint64_t mailbox_guid, std::uint32_t mail_id,
                                         std::uint32_t item_low_guid) {
  auto pkt =
      mail_protocol::EncodeTakeItem(mailbox_guid, mail_id, item_low_guid);
  Send(pkt);
}

void InteractionSender::SendMailDelete(std::uint64_t mailbox_guid, std::uint32_t mail_id,
                                       MailDeleteReason reason) {
  Send(mail_protocol::EncodeDelete(mailbox_guid, mail_id, reason));
}

void InteractionSender::SendMailReturnToSender(std::uint64_t mailbox_guid, std::uint32_t mail_id,
                                               std::uint64_t original_sender_guid) {
  Send(mail_protocol::EncodeReturnToSender(
      mailbox_guid, mail_id, original_sender_guid));
}

void InteractionSender::SendMailMarkAsRead(std::uint64_t mailbox_guid, std::uint32_t mail_id) {
  auto pkt = mail_protocol::EncodeMarkRead(mailbox_guid, mail_id);
  Send(pkt);
}

void InteractionSender::SendMailCreateTextItem(std::uint64_t mailbox_guid, std::uint32_t mail_id) {
  auto pkt = mail_protocol::EncodeCreateTextItem(mailbox_guid, mail_id);
  Send(pkt);
}

void InteractionSender::SendMailFollowup(const MailFollowupCommand& command) {
  Send(mail_protocol::EncodeFollowup(command));
}

void InteractionSender::SendQueryNextMailTime() {
  Send(mail_protocol::EncodeQueryNextMailTime());
}

void InteractionSender::SendBattlemasterHello(std::uint64_t guid) {
  auto pkt = PacketSender::BuildBattlemasterHello(guid);
  Send(pkt);
}

void InteractionSender::SendBankerActivate(std::uint64_t guid) {
  auto pkt = PacketSender::BuildBankerActivate(guid);
  Send(pkt);
}

void InteractionSender::SendAutoBankItem(std::uint8_t bag, std::uint8_t slot) {
  auto pkt = PacketSender::BuildAutoBankItem(bag, slot);
  Send(pkt);
}

void InteractionSender::SendAutoStoreBankItem(std::uint8_t bag, std::uint8_t slot) {
  auto pkt = PacketSender::BuildAutoStoreBankItem(bag, slot);
  Send(pkt);
}

void InteractionSender::SendBinderActivate(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_BINDER_ACTIVATE);
  pkt.AppendU64(guid);
  Send(pkt);
}

void InteractionSender::SendAreaSpiritHealerQuery(std::uint64_t guid) {
  auto pkt = PacketSender::BuildAreaSpiritHealerQuery(guid);
  Send(pkt);
}

void InteractionSender::SendAreaSpiritHealerQueue(std::uint64_t guid) {
  auto pkt = PacketSender::BuildAreaSpiritHealerQueue(guid);
  Send(pkt);
}

void InteractionSender::SendSpiritHealerActivate(std::uint64_t guid) {
  auto pkt = PacketSender::BuildSpiritHealerActivate(guid);
  Send(pkt);
}

void InteractionSender::SendAuctionHello(std::uint64_t guid) {
  auto pkt = auction_protocol::EncodeHello(guid);
  Send(pkt);
}

void InteractionSender::SendAuctionListItems(const net::wotlk::AuctionSearchParams &params) {
  std::vector<AuctionSortCriteria> sort;
  sort.reserve(params.sort_columns.size());
  for (const auto& entry : params.sort_columns) {
    sort.push_back({
        .sort_mode = entry.column,
        .is_desc = entry.reversed,
    });
  }
  auto packet = auction_protocol::EncodeListItems(
      params.auctioneer_guid.GetRawValue(), params.list_from,
      params.search_string, params.level_min, params.level_max,
      params.inventory_type, params.item_class, params.item_sub_class,
      params.quality, params.usable, params.get_all, sort);
  Send(packet);
}

void InteractionSender::SendAuctionPlaceBid(std::uint64_t auctioneer, std::uint32_t auction_id,
                                            std::uint32_t bid) {
  if (session_ != nullptr) {
    session_->auction().RecordPendingBid(auction_id, bid);
  }
  auto pkt = auction_protocol::EncodePlaceBid(auctioneer, auction_id, bid);
  Send(pkt);
}

void InteractionSender::SendAuctionListOwnerItems(std::uint64_t auctioneer) {
  auto owner_items = auction_protocol::EncodeListOwnerItems(auctioneer, 0);
  Send(owner_items);

  auto pending_sales = auction_protocol::EncodeListPendingSales(auctioneer);
  Send(pending_sales);
}

void InteractionSender::SendAuctionListPendingSales(std::uint64_t auctioneer) {
  auto pkt = auction_protocol::EncodeListPendingSales(auctioneer);
  Send(pkt);
}

void InteractionSender::SendAuctionListBidderItems(
    std::uint64_t auctioneer, std::uint32_t list_from,
    const std::vector<std::uint32_t> &outbidded_ids) {
  auto pkt = auction_protocol::EncodeListBidderItems(
      auctioneer, list_from, outbidded_ids);
  Send(pkt);
}

void InteractionSender::SendAuctionSellItem(std::uint64_t auctioneer, std::uint64_t item_guid,
                                            std::uint32_t count, std::uint32_t start_bid,
                                            std::uint32_t buyout, std::uint32_t runtime) {
  auto pkt = auction_protocol::EncodeSellItem(
      auctioneer, {{item_guid, count}}, start_bid, buyout, runtime);
  Send(pkt);
}

void InteractionSender::SendAuctionRemoveItem(std::uint64_t auctioneer, std::uint32_t auction_id) {
  auto pkt = auction_protocol::EncodeRemoveItem(auctioneer, auction_id);
  Send(pkt);
}

void InteractionSender::SendCastSpell(std::uint32_t spell_id, std::uint8_t cast_flags,
                                       std::uint64_t target_guid) {
  (void)TrySendCastSpell(spell_id, cast_flags, target_guid);
}

bool InteractionSender::TrySendCastSpell(std::uint32_t spell_id,
                                         std::uint8_t cast_flags,
                                         std::uint64_t target_guid) {
  ApplyAttackActionAutoCancelForSpell(
      session_, spell_id,
      [this](const std::uint32_t aura_spell_id) { SendCancelAura(aura_spell_id); });

  if (session_ == nullptr) {
    return false;
  }

  auto& spells = session_->spells();
  if (!spells.PrepareTargetedCast(spell_id)) {
    return false;
  }
  if (target_guid != 0) {
    spells.SetUnitTarget(SpellSlotType::kCurrent, ObjectGuid(target_guid));
  }
  const bool sent = spells.CastSpell(*session_, spell_id, target_guid,
                                     cast_flags) == SpellCastResult::kSuccess;
  spells.DiscardPreparedTargetedCast(spell_id);
  return sent;
}

void InteractionSender::SendCastSpellOnItem(std::uint32_t spell_id, std::uint8_t cast_flags,
                                            std::uint64_t item_guid) {
  if (item_guid == 0) {
    return;
  }

  if (session_ == nullptr) {
    return;
  }

  auto& spells = session_->spells();
  if (!spells.PrepareTargetedCast(spell_id)) {
    return;
  }
  spells.SetItemTarget(SpellSlotType::kCurrent, ObjectGuid(item_guid));
  (void)spells.CastSpell(*session_, spell_id, 0, cast_flags);
  spells.DiscardPreparedTargetedCast(spell_id);
}

bool InteractionSender::SendCastSpellOnUnitAndItem(std::uint32_t spell_id,
                                                   std::uint8_t cast_flags,
                                                   std::uint64_t target_guid,
                                                   std::uint64_t item_guid) {
  if (session_ == nullptr || target_guid == 0 || item_guid == 0) {
    return false;
  }

  ApplyAttackActionAutoCancelForSpell(
      session_, spell_id,
      [this](const std::uint32_t aura_spell_id) { SendCancelAura(aura_spell_id); });

  auto& spells = session_->spells();
  if (!spells.PrepareTargetedCast(spell_id)) {
    return false;
  }
  spells.SetUnitTarget(SpellSlotType::kCurrent, ObjectGuid(target_guid));
  spells.SetItemTarget(SpellSlotType::kCurrent, ObjectGuid(item_guid));
  const bool sent = spells.CastSpell(*session_, spell_id, target_guid, cast_flags) ==
                    SpellCastResult::kSuccess;
  spells.DiscardPreparedTargetedCast(spell_id);
  return sent;
}

void InteractionSender::SendCastSpellOnTradeEnchantSlot(std::uint32_t spell_id,
                                                         std::uint8_t cast_flags) {
  if (session_ == nullptr) {
    return;
  }

  auto& spells = session_->spells();
  if (!spells.PrepareTargetedCast(spell_id)) {
    return;
  }
  spells.SetTradeItemTarget(
      SpellSlotType::kCurrent,
      ObjectGuid(static_cast<std::uint64_t>(kTradeWillNotBeTradedSlot)));
  (void)spells.CastSpell(*session_, spell_id, 0, cast_flags);
  spells.DiscardPreparedTargetedCast(spell_id);
}

void InteractionSender::SendCastSpellOnTradeItem(std::uint32_t spell_id, std::uint8_t cast_flags,
                                                 std::uint64_t item_guid) {
  if (item_guid == 0) {
    return;
  }

  if (session_ == nullptr) {
    return;
  }

  auto& spells = session_->spells();
  if (!spells.PrepareTargetedCast(spell_id)) {
    return;
  }
  spells.SetTradeItemTarget(SpellSlotType::kCurrent, ObjectGuid(item_guid));
  (void)spells.CastSpell(*session_, spell_id, 0, cast_flags);
  spells.DiscardPreparedTargetedCast(spell_id);
}

void InteractionSender::SendCancelCast(std::uint32_t spell_id) {
  if (session_ == nullptr) {
    return;
  }

  auto& spells = session_->spells();
  const auto active_spell = spells.GetSlot(SpellSlotType::kCurrent);
  if (active_spell.spell_id != spell_id) {
    return;
  }
  spells.CancelSpell(*session_, SpellSlotType::kCurrent);
}

void InteractionSender::SendCancelAura(std::uint32_t spell_id) {
  auto pkt = PacketSender::BuildCancelAura(spell_id);
  Send(pkt);
}

void InteractionSender::SendTotemDestroyed(std::uint8_t slot) {
  WorldPacket pkt(Opcode::CMSG_TOTEM_DESTROYED);
  pkt.AppendU8(slot);
  Send(pkt);
}

bool InteractionSender::SendAttackSwing(std::uint64_t guid) {
  auto pkt = PacketSender::BuildAttackSwing(guid);
  return Send(pkt);
}

void InteractionSender::SendAttackStop() {
  auto pkt = PacketSender::BuildAttackStop();
  Send(pkt);
}

bool InteractionSender::TryQueueBindOnUseConfirmation(std::uint64_t item_guid,
                                                      std::uint32_t item_entry,
                                                      std::uint32_t item_flags,
                                                      std::uint64_t target_guid) {
  if (!session_ || item_guid == 0 || item_entry == 0) {
    return false;
  }

  const auto *item_template = session_->query_cache().GetItemTemplate(item_entry);
  if (!item_template || item_template->bonding != 3 || (item_flags & ItemFlags::kSoulbound) != 0) {
    return false;
  }

  pending_bind_on_use_ = PendingBindOnUseState{item_guid, target_guid};
  ::openwow::ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ::openwow::ui::game::events::USE_BIND_CONFIRM,
      {item_template->name, static_cast<int>(item_template->quality)});

  return true;
}

bool InteractionSender::ConfirmPendingBindOnUse() {
  const PendingBindOnUseState pending = pending_bind_on_use_;
  pending_bind_on_use_ = {};

  if (pending.item_guid == 0) {
    return false;
  }

  const auto location =
      ResolvePlayerItemPacketLocationByGuid(session_->inventory_replica(), pending.item_guid);
  if (!location.has_value()) {
    return false;
  }

  return SendUseItemPacket(location->packet_bag, location->packet_slot, 0, pending.target_guid);
}

bool InteractionSender::SendUseItemByGuid(std::uint64_t item_guid, std::uint8_t cast_flags,
                                          std::uint64_t target_guid) {
  const auto location = ResolvePlayerItemPacketLocationByGuid(session_->inventory_replica(), item_guid);
  if (!location.has_value()) {
    return false;
  }

  return SendUseItemPacket(location->packet_bag, location->packet_slot, cast_flags, target_guid);
}

bool InteractionSender::SendUseItemByGuidToGlyphSlot(std::uint64_t item_guid,
                                                     std::uint32_t glyph_index) {
  const auto location = ResolvePlayerItemPacketLocationByGuid(session_->inventory_replica(), item_guid);
  if (!location.has_value()) {
    return false;
  }

  return SendUseItemPacket(location->packet_bag, location->packet_slot, 0, 0, glyph_index);
}

bool InteractionSender::SendOpenItem(const std::uint8_t bag, const std::uint8_t slot,
                                     const bool play_wrapped_gift_sound) {
  if (session_ == nullptr) {
    return false;
  }

  if (play_wrapped_gift_sound) {
    PlayNamedSoundKit(session_->sound_runtime(), "UnwrapGift");
  }

  return Send(PacketSender::BuildOpenItem(bag, slot));
}

bool InteractionSender::SendWrapItem(const std::uint8_t source_bag, const std::uint8_t source_slot,
                                     const std::uint8_t target_bag,
                                     const std::uint8_t target_slot) {
  if (session_ == nullptr) {
    return false;
  }

  return Send(PacketSender::BuildWrapItem(source_bag, source_slot, target_bag, target_slot));
}

bool InteractionSender::SendReadItem(const std::uint8_t bag, const std::uint8_t slot) {
  if (session_ == nullptr) {
    return false;
  }

  const auto item = ResolveUseItemPacketItem(session_->inventory_replica(), bag, slot);
  if (!item.has_value() || item->guid == 0) {
    return false;
  }

  if (!ToggleOrBeginReadableObjectInteraction(*session_, item->guid)) {
    return true;
  }

  return Send(PacketSender::BuildReadItem(bag, slot));
}

bool InteractionSender::SendUseItem(std::uint8_t bag, std::uint8_t slot,
                                    std::uint8_t cast_flags) {
  return SendUseItem(bag, slot, cast_flags, 0);
}

bool InteractionSender::SendUseItem(std::uint8_t bag, std::uint8_t slot,
                                    std::uint8_t cast_flags,
                                    std::uint64_t target_guid) {
  return SendUseItemPacket(bag, slot, cast_flags, target_guid);
}

bool InteractionSender::SendUseItemPacket(std::uint8_t bag, std::uint8_t slot,
                                          std::uint8_t cast_flags, std::uint64_t target_guid,
                                          std::uint32_t glyph_index) {
  if (session_ == nullptr) {
    return false;
  }

  const auto item = ResolveUseItemPacketItem(session_->inventory_replica(), bag, slot);

  if (!item.has_value() || item->guid == 0 || item->entry == 0) {
    return false;
  }

  const auto *item_template =
      session_->query_cache().GetOrRequestItemTemplate(item->entry);
  if (cast_flags == 0 && target_guid == 0 && glyph_index == 0) {
    if (item_template != nullptr && (item_template->flags & ItemFlags::kWrapped) != 0 &&
        (item->flags & ItemFlags::kGiftWrapped) != 0) {
      return SendOpenItem(bag, slot, true);
    }

    if (item->IsReadable(item_template != nullptr ? item_template->page_text : 0)) {
      return SendReadItem(bag, slot);
    }
  }

  ApplyAttackActionAutoCancelForItemUse(
      session_, *item,
      [this](const std::uint32_t aura_spell_id) { SendCancelAura(aura_spell_id); });

  net::wotlk::SpellTargets targets{};
  if (target_guid != 0) {
    targets.target_mask = net::wotlk::kTargetFlagUnit;
    targets.unit_target = ObjectGuid(target_guid);
  } else {
    targets.target_mask = net::wotlk::kTargetFlagSelf;
  }

  const std::uint32_t spell_id =
      item_template != nullptr
          ? ResolveItemInstanceUseSpellId(*item, *item_template,
                                          session_->GetDbcLoader())
          : 0u;
  const std::uint64_t item_guid = item->guid;
  auto pkt = PacketSender::BuildUseItem(bag, slot, 0, spell_id,
                                        item_guid, glyph_index, cast_flags, targets);
  return Send(pkt);
}

void InteractionSender::SendDestroyItem(std::uint8_t bag, std::uint8_t slot, std::uint32_t count) {
  auto pkt = PacketSender::BuildDestroyItem(bag, slot, count);
  Send(pkt);
}

void InteractionSender::SendSwapInvItem(std::uint8_t dst_slot, std::uint8_t src_slot) {
  auto pkt = PacketSender::BuildSwapInvItem(dst_slot, src_slot);
  Send(pkt);
}

void InteractionSender::SendSwapItem(std::uint8_t dst_bag, std::uint8_t dst_slot,
                                     std::uint8_t src_bag, std::uint8_t src_slot) {
  auto pkt = PacketSender::BuildSwapItem(dst_bag, dst_slot, src_bag, src_slot);
  Send(pkt);
}

void InteractionSender::SendSplitItem(std::uint8_t src_bag, std::uint8_t src_slot,
                                      std::uint8_t dst_bag, std::uint8_t dst_slot,
                                      std::uint32_t count) {
  auto pkt = PacketSender::BuildSplitItem(src_bag, src_slot, dst_bag, dst_slot, count);
  Send(pkt);
}

void InteractionSender::SendAutoStoreBagItem(std::uint8_t src_bag, std::uint8_t src_slot,
                                             std::uint8_t dst_bag) {
  WorldPacket pkt(Opcode::CMSG_AUTOSTORE_BAG_ITEM);
  pkt.AppendU8(src_bag);
  pkt.AppendU8(src_slot);
  pkt.AppendU8(dst_bag);
  Send(pkt);
}

void InteractionSender::SendSetAmmo(const std::uint32_t item_entry) {
  WorldPacket packet(Opcode::CMSG_SET_AMMO);
  packet.AppendU32(item_entry);
  Send(packet);
}

void InteractionSender::SendResurrectResponse(std::uint64_t guid, std::uint8_t status) {
  auto pkt = PacketSender::BuildResurrectResponse(guid, status != 0);
  Send(pkt);
}

void InteractionSender::SendRepopRequest(bool auto_release) {
  auto pkt = PacketSender::BuildRepopRequest(auto_release);
  Send(pkt);
}

void InteractionSender::SendReclaimCorpse() {
  const auto corpse_guid =
      session_ != nullptr
          ? session_->objects().GetActivePlayerCorpseGuid().GetRawValue()
          : std::uint64_t{0};
  auto pkt = PacketSender::BuildReclaimCorpse(corpse_guid);
  Send(pkt);
}

void InteractionSender::SendHearthAndResurrect() {
  auto pkt = PacketSender::BuildHearthAndResurrect();
  Send(pkt);
}

void InteractionSender::SendAreaTrigger(std::uint32_t trigger_id) {
  WorldPacket pkt(Opcode::CMSG_AREATRIGGER);
  pkt.AppendU32(trigger_id);
  Send(pkt);
}

void InteractionSender::SendZoneUpdate(std::uint32_t zone_id) {
  auto pkt = PacketSender::BuildZoneUpdate(zone_id);
  Send(pkt);
}

void InteractionSender::SendCreatureQuery(std::uint32_t entry, std::uint64_t guid) {
  auto pkt = PacketSender::BuildCreatureQuery(entry, guid);
  Send(pkt);
}

void InteractionSender::SendGameObjectQuery(std::uint32_t entry, std::uint64_t guid) {
  auto pkt = PacketSender::BuildGameObjectQuery(entry, guid);
  Send(pkt);
}

void InteractionSender::SendGameObjectUse(std::uint64_t guid) {
  if (guid == 0) {
    return;
  }
  if (openwow::diagnostics::IsLogEnabled(
          openwow::diagnostics::LogLevel::kDebug)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kDebug,
        "interaction send CMSG_GAMEOBJ_USE guid=" + std::to_string(guid));
  }
  WorldPacket pkt(Opcode::CMSG_GAMEOBJ_USE);
  pkt.AppendU64(guid);
  Send(pkt);
}

void InteractionSender::SendGameObjectReportUse(std::uint64_t guid) {
  if (guid == 0) {
    return;
  }
  if (openwow::diagnostics::IsLogEnabled(
          openwow::diagnostics::LogLevel::kDebug)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kDebug,
        "interaction send CMSG_GAMEOBJ_REPORT_USE guid=" +
            std::to_string(guid));
  }
  WorldPacket pkt(Opcode::CMSG_GAMEOBJ_REPORT_USE);
  pkt.AppendU64(guid);
  Send(pkt);
}

void InteractionSender::SendItemQuery(std::uint32_t entry) {
  auto pkt = PacketSender::BuildItemQuerySingle(entry);
  Send(pkt);
}

void InteractionSender::SendNameQuery(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_NAME_QUERY);
  pkt.AppendU64(guid);
  Send(pkt);
}

void InteractionSender::SendItemTextQuery(const std::uint64_t item_guid) {
  if (item_guid == 0) {
    return;
  }

  Send(PacketSender::BuildItemTextQuery(item_guid));
}

void InteractionSender::SendPageTextQuery(std::uint32_t page_text_id) {
  WorldPacket pkt(Opcode::CMSG_PAGE_TEXT_QUERY);
  pkt.AppendU32(page_text_id);
  Send(pkt);
}

void InteractionSender::SendAcceptLevelGrant(std::uint64_t guid) {
  if (guid == 0) {
    return;
  }

  WorldPacket pkt(Opcode::CMSG_ACCEPT_LEVEL_GRANT);
  AppendPackedGuid(pkt, ObjectGuid(guid));
  Send(pkt);
}

void InteractionSender::SendGrantLevel(std::uint64_t guid) {
  if (guid == 0) {
    return;
  }

  WorldPacket pkt(Opcode::CMSG_GRANT_LEVEL);
  AppendPackedGuid(pkt, ObjectGuid(guid));
  Send(pkt);
}

void InteractionSender::SendSetSelection(std::uint64_t guid) {
  auto pkt = PacketSender::BuildSetSelection(guid);
  Send(pkt);
}

void InteractionSender::SendSetActionButton(std::uint8_t slot, const ActionPresentationEntry &button) {
  const auto action_slot = actions::ActionSlot::FromZeroBased(slot);
  if (action_slot) {
    Send(actions::adapters::protocol::EncodeActionAssignment(
        *action_slot, button.ToAssignedAction()));
  }
}

void InteractionSender::SendClearActionButton(std::uint8_t slot) {
  const auto action_slot = actions::ActionSlot::FromZeroBased(slot);
  if (action_slot) {
    Send(actions::adapters::protocol::EncodeActionAssignment(
        *action_slot, actions::Action::Empty()));
  }
}

void InteractionSender::SendTaxiNodeStatusQuery(std::uint64_t guid) {

  WorldPacket pkt(Opcode::CMSG_TAXINODE_STATUS_QUERY);
  pkt.AppendU64(guid);
  Send(pkt);
}

void InteractionSender::SendTaxiQueryAvailableNodes(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_TAXIQUERYAVAILABLENODES);
  pkt.AppendU64(guid);
  Send(pkt);
}

void InteractionSender::SendActivateTaxi(std::uint64_t npc_guid, std::uint32_t source_node,
                                         std::uint32_t dest_node) {
  WorldPacket pkt(Opcode::CMSG_ACTIVATETAXI);
  pkt.AppendU64(npc_guid);
  pkt.AppendU32(source_node);
  pkt.AppendU32(dest_node);
  Send(pkt);
}

void InteractionSender::SendActivateTaxiExpress(std::uint64_t npc_guid,
                                                const std::vector<std::uint32_t> &nodes) {

  WorldPacket pkt(Opcode::CMSG_ACTIVATETAXIEXPRESS);
  pkt.AppendU64(npc_guid);
  pkt.AppendU32(static_cast<std::uint32_t>(nodes.size()));
  for (const auto n : nodes) {
    pkt.AppendU32(n);
  }
  Send(pkt);
}

void InteractionSender::SendEnableTaxi(std::uint64_t npc_guid) {
  WorldPacket pkt(Opcode::CMSG_ENABLETAXI);
  pkt.AppendU64(npc_guid);
  Send(pkt);
}

void InteractionSender::SendSetTaxiBenchmarkMode(bool enabled) {
  WorldPacket pkt(Opcode::CMSG_SET_TAXI_BENCHMARK_MODE);
  pkt.AppendU8(enabled ? 1 : 0);
  Send(pkt);
}

void InteractionSender::SendLearnTalent(std::uint32_t talent_id, std::uint32_t rank) {
  WorldPacket pkt(Opcode::CMSG_LEARN_TALENT);
  pkt.AppendU32(talent_id);
  pkt.AppendU32(rank);
  Send(pkt);
}

void InteractionSender::SendLearnPetTalent(std::uint64_t pet_guid,
                                            std::uint32_t talent_id,
                                            std::uint32_t rank) {
  auto pkt = PacketSender::BuildLearnPetTalent(pet_guid, talent_id, rank);
  Send(pkt);
}

void InteractionSender::SendTalentWipeConfirm(const ObjectGuid &trainer_guid) {
  WorldPacket pkt(Opcode::MSG_TALENT_WIPE_CONFIRM);
  pkt.AppendU64(trainer_guid.GetRawValue());
  Send(pkt);
}

void InteractionSender::SendSetActiveTalentGroup(std::uint8_t group_index) {
  if (session_ == nullptr || group_index >= 2) {
    return;
  }

  const std::uint32_t spell_id = group_index == 0
                                     ? kActivatePrimaryTalentSpecSpellId
                                     : kActivateSecondaryTalentSpecSpellId;
  (void)session_->spells().CastSpell(*session_, spell_id);
}

void InteractionSender::SendSocketGems(std::uint64_t item_guid, std::uint64_t gem_guid_0,
                                       std::uint64_t gem_guid_1, std::uint64_t gem_guid_2) {

  if (item_guid == 0) {
    return;
  }

  int gem_count = 0;
  if (gem_guid_0 != 0)
    ++gem_count;
  if (gem_guid_1 != 0)
    ++gem_count;
  if (gem_guid_2 != 0)
    ++gem_count;
  if (gem_count == 0)
    return;

  WorldPacket pkt(Opcode::CMSG_SOCKET_GEMS);
  pkt.AppendU64(item_guid);
  pkt.AppendU64(gem_guid_0);
  pkt.AppendU64(gem_guid_1);
  pkt.AppendU64(gem_guid_2);
  Send(pkt);
}

void InteractionSender::SendSetGlyphSlot(std::uint32_t slot) {
  WorldPacket pkt(Opcode::CMSG_SET_GLYPH_SLOT);
  pkt.AppendU32(slot);
  Send(pkt);
}

void InteractionSender::SendRemoveGlyph(std::uint32_t slot) {
  WorldPacket pkt(Opcode::CMSG_REMOVE_GLYPH);
  pkt.AppendU32(slot);
  Send(pkt);
}

void InteractionSender::SendStandStateChange(const std::uint8_t stand_state) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_STANDSTATECHANGE);

  pkt.AppendU32(stand_state);
  if (!Send(pkt) || session_ == nullptr) {
    return;
  }

  ApplyLocalStandStateChange(*session_, stand_state);
}

void InteractionSender::SendAddFriend(const std::string &name, const std::string &note) {
  TutorialSystem::Instance().FlagTutorial(0x15u);

  auto pkt = PacketSender::BuildAddFriend(name, note);
  Send(pkt);
}

void InteractionSender::SendDelFriend(std::uint64_t guid) {
  auto pkt = PacketSender::BuildDelFriend(guid);
  Send(pkt);
}

void InteractionSender::SendAddIgnore(const std::string &name) {

  if (session_) {
    const auto ignored = session_->social().GetIgnored();
    for (const auto *ci : ignored) {
      if (!ci)
        continue;

      std::string resolved;
      if (!ci->display_name.empty()) {
        resolved = ci->display_name;
      } else if (const auto *pn = session_->query_cache().GetPlayerName(
                     ci->guid.GetRawValue());
                 pn && !pn->name.empty()) {
        resolved = pn->realm_name.empty()
                       ? pn->name
                       : pn->name + "-" + pn->realm_name;
      }
      if (!resolved.empty() &&
          core::SStrCmpUTF8NoCase(resolved.c_str(), name.c_str(),
                                  0x7FFFFFFF) == 0) {
        ui::game::DisplaySystemMessage(288, resolved.c_str());
        return;
      }
    }
  }

  auto pkt = PacketSender::BuildAddIgnore(name);
  Send(pkt);
}

void InteractionSender::SendDelIgnore(std::uint64_t guid) {
  auto pkt = PacketSender::BuildDelIgnore(guid);
  Send(pkt);
}

void InteractionSender::SendAddMute(const std::string &name) {
  auto pkt = SocialManager::BuildAddMute(name);
  Send(pkt);
}

void InteractionSender::SendDelMute(std::uint64_t guid) {
  auto pkt = SocialManager::BuildDelMute(ObjectGuid(guid));
  Send(pkt);
}

void InteractionSender::SendGuildRoster() {
  if (!GuildSystem::Get().TryBeginGuildRosterRequest(openwow::core::GameClock::GetTickCount32())) {
    return;
  }
  auto pkt = PacketSender::BuildGuildRoster();
  Send(pkt);
}

void InteractionSender::SendGuildRosterRefresh() {
  const auto current_time_ms = openwow::core::GameClock::GetTickCount32();
  auto pkt = PacketSender::BuildGuildRoster();
  Send(pkt);
  GuildSystem::Get().MarkGuildRosterRequestSent(current_time_ms);
}

void InteractionSender::SendGuildInvite(const std::string &name) {
  auto pkt = PacketSender::BuildGuildInvite(name);
  Send(pkt);
}

void InteractionSender::SendGuildLeave() {
  auto pkt = PacketSender::BuildGuildLeave();
  Send(pkt);
}

void InteractionSender::SendGuildDisband() {
  auto pkt = PacketSender::BuildGuildDisband();
  Send(pkt);
}

void InteractionSender::SendGroupInvite(const std::string &name, std::uint32_t role_flags) {
  TutorialSystem::Instance().FlagTutorial(0x11u);

  if (session_ != nullptr) {
    const auto *active_player = session_->objects().GetActivePlayer();
    if (active_player != nullptr &&
        openwow::core::SStrCmpUTF8NoCase(name.c_str(), active_player->GetPlayerName().c_str(),
                                         0x7FFFFFFFu) == 0) {
      ui::game::DisplaySystemMessage(64);
      return;
    }
  }

  auto pkt = PacketSender::BuildGroupInvite(name, role_flags);
  Send(pkt);
}

void InteractionSender::SendGroupAccept(const std::uint32_t role_flags) {
  if (session_ == nullptr || session_->objects().GetActivePlayer() == nullptr) {
    return;
  }

  auto pkt = PacketSender::BuildGroupAccept(role_flags);
  Send(pkt);
}

void InteractionSender::SendGroupDecline() {
  if (session_ == nullptr || session_->objects().GetActivePlayer() == nullptr) {
    return;
  }

  auto pkt = PacketSender::BuildGroupDecline();
  Send(pkt);
}

void InteractionSender::SendGroupUninvite(const std::string &name) {
  WorldPacket pkt(Opcode::CMSG_GROUP_UNINVITE);
  pkt.AppendString(name.c_str());
  Send(pkt);
}

void InteractionSender::SendGroupUninviteByGuid(const std::uint64_t target_guid,
                                                const std::string_view reason) {
  if (session_ == nullptr || session_->objects().GetActivePlayer() == nullptr) {
    return;
  }

  Send(PacketSender::BuildGroupUninviteByGuid(target_guid, reason));
}

void InteractionSender::SendGroupDisband() {

  if (session_ == nullptr || session_->objects().GetActivePlayer() == nullptr) {
    return;
  }

  auto pkt = PacketSender::BuildGroupDisband();
  Send(pkt);
}

void InteractionSender::SendWho(const std::string &filter, std::uint32_t level_min,
                                std::uint32_t level_max, std::uint32_t race_mask,
                                std::uint32_t class_mask) {
  if (session_ == nullptr) {
    return;
  }

  const auto parsed =
      BuildWhoQueryFromFilter(session_, filter, level_min, level_max, race_mask, class_mask);
  session_->misc().SetWhoClientFilter(parsed.client_filter);
  auto pkt = PacketSender::BuildWho(parsed.query);
  Send(pkt);
}

void InteractionSender::SendCancelMountAura() {
  WorldPacket pkt(Opcode::CMSG_CANCEL_MOUNT_AURA);
  Send(pkt);
}

void InteractionSender::SendSetSheathed(std::uint32_t sheath_state) {
  auto pkt = PacketSender::BuildSetSheathed(sheath_state);
  Send(pkt);
}

void InteractionSender::SendTextEmote(std::uint32_t emote_id, std::uint64_t target_guid) {
  if (session_ == nullptr) {
    return;
  }

  const auto *dbc = session_->GetDbcLoader();
  if (dbc == nullptr) {
    return;
  }

  auto *player = session_->objects().GetActivePlayer();
  if (player == nullptr) {
    return;
  }

  const auto *text_emote = dbc->emotes_text().LookupEntry(emote_id);
  if (text_emote == nullptr) {
    return;
  }

  const auto *emote = dbc->emotes().LookupEntry(text_emote->emote_id);
  if (emote == nullptr) {
    return;
  }

  const auto validation =
      ValidateEmoteConditions(static_cast<std::uint16_t>(emote->flags),
                              BuildOutgoingTextEmoteValidationState(*session_, *player));
  if (!validation.allowed) {
    return;
  }

  if (validation.needs_movement_warning &&
      ShouldDisplayTextEmoteMovementWarning(*player, session_->objects())) {
    ui::game::DisplaySystemMessage(332);
    return;
  }

  constexpr std::uint32_t kEmoteSpecProcState = 1u;
  if (player->Animation().GetStandState() == kStandStateSleep &&
      emote->spec != kEmoteSpecProcState) {
    return;
  }

  if (target_guid == player->GetGuid().GetRawValue()) {
    target_guid = 0;
  }

  constexpr std::uint32_t kClientDrivenStandStateMask = 0x10Bu;
  if (emote->spec == kEmoteSpecProcState &&
      ((kClientDrivenStandStateMask >> (emote->spec_param & 0x1Fu)) & 1u) !=
          0u) {
    SendStandStateChange(static_cast<std::uint8_t>(emote->spec_param));
  } else if (emote->anim_id != 0u &&
             player->Animation().CanPlayEmoteAnimationNow(*session_)) {

    player->Animation().PlayEmoteAnimation(
        static_cast<std::int32_t>(emote->anim_id), 0u);
  }

  auto pkt = PacketSender::BuildTextEmote(
      emote_id, LookupOutgoingTextEmoteVariant(emote_id, *player), target_guid);
  Send(pkt);
}

void InteractionSender::SendRandomRoll(std::uint32_t min_val, std::uint32_t max_val) {
  WorldPacket pkt(Opcode::MSG_RANDOM_ROLL);
  pkt.AppendU32(min_val);
  pkt.AppendU32(max_val);
  Send(pkt);
}

void InteractionSender::SendGroupSetLeader(const std::uint64_t target_guid) {
  Send(PacketSender::BuildGroupSetLeader(target_guid));
}

void InteractionSender::SendGroupAssistantLeader(const std::uint64_t target_guid, const bool set) {
  Send(PacketSender::BuildGroupAssistantLeader(target_guid, set));
}

void InteractionSender::SendGroupRaidConvert(bool to_raid) {
  (void)to_raid;
  WorldPacket pkt(Opcode::CMSG_GROUP_RAID_CONVERT);
  Send(pkt);
}

void InteractionSender::SendGroupChangeSubGroup(const std::string &name, std::uint8_t sub_group) {
  WorldPacket pkt(Opcode::CMSG_GROUP_CHANGE_SUB_GROUP);
  pkt.AppendString(name.c_str());
  pkt.AppendU8(sub_group);
  Send(pkt);
}

void InteractionSender::SendGroupSwapSubGroup(const std::string &name1, const std::string &name2) {
  WorldPacket pkt(Opcode::CMSG_GROUP_SWAP_SUB_GROUP);
  pkt.AppendString(name1.c_str());
  pkt.AppendString(name2.c_str());
  Send(pkt);
}

void InteractionSender::SendReadyCheck() {
  auto pkt = PacketSender::BuildReadyCheck();
  Send(pkt);
}

void InteractionSender::SendReadyCheckConfirm(bool is_ready) {

  WorldPacket pkt(Opcode::MSG_RAID_READY_CHECK);
  pkt.AppendU8(is_ready ? 1 : 0);
  Send(pkt);
}

void InteractionSender::SendReadyCheckFinished() {
  Send(PacketSender::BuildReadyCheckFinished());
}

void InteractionSender::SendContactList(std::uint32_t flags) {
  WorldPacket pkt(Opcode::CMSG_CONTACT_LIST);
  pkt.AppendU32(flags);
  Send(pkt);
}

void InteractionSender::SendSetContactNotes(std::uint64_t guid, const std::string &note) {
  WorldPacket pkt(Opcode::CMSG_SET_CONTACT_NOTES);
  pkt.AppendU64(guid);
  pkt.AppendString(note.c_str());
  Send(pkt);
}

void InteractionSender::SendSummonResponse(std::uint64_t summoner_guid, bool accept) {
  WorldPacket pkt(Opcode::CMSG_SUMMON_RESPONSE);
  pkt.AppendU64(summoner_guid);
  pkt.AppendU8(accept ? 1 : 0);
  Send(pkt);
}

void InteractionSender::SendComplain(std::uint8_t spam_type, std::uint64_t spammer_guid,
                                     std::uint32_t auxiliary_word,
                                     std::uint32_t mail_message_id,
                                     std::uint32_t trailing_word) {
  WorldPacket pkt(Opcode::CMSG_COMPLAIN);
  pkt.AppendU8(spam_type);
  pkt.AppendU64(spammer_guid);
  pkt.AppendU32(auxiliary_word);
  pkt.AppendU32(mail_message_id);
  pkt.AppendU32(trailing_word);
  Send(pkt);
}

void InteractionSender::SendChatComplain(std::uint64_t spammer_guid, std::uint32_t aux_value,
                                         std::uint32_t chat_type, std::uint32_t channel_lookup_id,
                                         std::uint32_t recorded_at,
                                         const std::string &formatted_line) {
  WorldPacket pkt(Opcode::CMSG_COMPLAIN);
  pkt.AppendU8(1);
  pkt.AppendU64(spammer_guid);
  pkt.AppendU32(aux_value);
  pkt.AppendU32(chat_type);
  pkt.AppendU32(channel_lookup_id);
  pkt.AppendU32(static_cast<std::uint32_t>(std::time(nullptr)) - recorded_at);
  pkt.AppendString(formatted_line.c_str());
  Send(pkt);
}

void InteractionSender::SendChatFiltered(std::uint64_t spammer_guid) {
  WorldPacket pkt(Opcode::CMSG_CHAT_FILTERED);
  pkt.AppendU64(spammer_guid);
  Send(pkt);
}

void InteractionSender::SendChatIgnored(std::uint64_t sender_guid, const bool commentator_squelch) {
  WorldPacket pkt(Opcode::CMSG_CHAT_IGNORED);
  pkt.AppendU64(sender_guid);
  pkt.AppendU8(commentator_squelch ? 1 : 0);
  Send(pkt);
}

void InteractionSender::SendChannelCommand(std::uint16_t opcode_raw, const std::string &channel,
                                           const std::string &target) {
  WorldPacket pkt(static_cast<Opcode>(opcode_raw));
  pkt.AppendString(channel.c_str());
  if (!target.empty()) {
    pkt.AppendString(target.c_str());
  }
  Send(pkt);
}

void InteractionSender::SendClearChannelWatch() {
  WorldPacket pkt(Opcode::CMSG_CLEAR_CHANNEL_WATCH);
  Send(pkt);
}

void InteractionSender::SendChannelStringPairCommand(std::uint16_t opcode_raw,
                                                     const std::string &channel,
                                                     const std::string &argument) {
  WorldPacket pkt(static_cast<Opcode>(opcode_raw));
  pkt.AppendString(channel.c_str());
  pkt.AppendString(argument.c_str());
  Send(pkt);
}

void InteractionSender::SendChannelTargetCommand(std::uint16_t opcode_raw,
                                                 const std::string &channel,
                                                 const std::string &target) {
  SendChannelStringPairCommand(opcode_raw, channel, target);
}

void InteractionSender::SendGroupVoiceSilence(const std::uint64_t target_guid, const bool silence,
                                              const bool battleground_group) {
  WorldPacket pkt(silence ? Opcode::CMSG_PARTY_SILENCE : Opcode::CMSG_PARTY_UNSILENCE);
  pkt.AppendU64(target_guid);
  pkt.AppendU8(battleground_group ? 1 : 0);
  Send(pkt);
}

void InteractionSender::SendSetActiveVoiceChannel(const std::uint32_t channel_type,
                                                  const std::string_view channel_name) {
  Send(PacketSender::BuildSetActiveVoiceChannel(channel_type, channel_name));
}

void InteractionSender::SendJoinChannel(std::uint32_t channel_id, const std::string &name,
                                        const std::string &password, const bool has_voice,
                                        const std::uint8_t join_flag) {
  auto pkt = ChatManager::BuildJoinChannel(channel_id, name, password, has_voice, join_flag);
  Send(pkt);
}

void InteractionSender::SendLeaveChannel(const std::uint32_t channel_id, const std::string &name) {
  auto pkt = ChatManager::BuildLeaveChannel(channel_id, name);
  Send(pkt);
}

void InteractionSender::SendAddonMessage(std::uint32_t chat_type, const std::string &message,
                                         const std::string &target) {
  auto pkt = ChatManager::BuildChatMessage(static_cast<ChatMsg>(chat_type), Language::kAddon,
                                           message, target);
  Send(pkt);
}

void InteractionSender::SendGuildAccept() {
  auto pkt = PacketSender::BuildGuildAccept();
  Send(pkt);
}

void InteractionSender::SendGuildDecline() {
  auto pkt = PacketSender::BuildGuildDecline();
  Send(pkt);
}

void InteractionSender::SendGuildInfo() {
  WorldPacket pkt(Opcode::CMSG_GUILD_INFO);
  Send(pkt);
}

void InteractionSender::SendGuildPromote(const std::string &name) {
  auto pkt = PacketSender::BuildGuildPromote(name);
  Send(pkt);
}

void InteractionSender::SendGuildDemote(const std::string &name) {
  auto pkt = PacketSender::BuildGuildDemote(name);
  Send(pkt);
}

void InteractionSender::SendGuildSetLeader(const std::string &name) {
  WorldPacket pkt(Opcode::CMSG_GUILD_LEADER);
  pkt.AppendString(name.c_str());
  Send(pkt);
}

void InteractionSender::SendGuildSetMOTD(const std::string &motd) {
  auto pkt = PacketSender::BuildGuildMotd(motd);
  Send(pkt);
}

void InteractionSender::SendGuildRemove(const std::string &name) {
  auto pkt = PacketSender::BuildGuildRemove(name);
  Send(pkt);
}

void InteractionSender::SendGuildSetPublicNote(const std::string &name, const std::string &note) {
  WorldPacket pkt(Opcode::CMSG_GUILD_SET_PUBLIC_NOTE);
  pkt.AppendString(name.c_str());
  pkt.AppendString(note.c_str());
  Send(pkt);
}

void InteractionSender::SendGuildSetOfficerNote(const std::string &name, const std::string &note) {
  WorldPacket pkt(Opcode::CMSG_GUILD_SET_OFFICER_NOTE);
  pkt.AppendString(name.c_str());
  pkt.AppendString(note.c_str());
  Send(pkt);
}

void InteractionSender::SendGuildPermissionsQuery() {
  WorldPacket pkt(Opcode::MSG_GUILD_PERMISSIONS);
  Send(pkt);
}

void InteractionSender::SendGuildBankMoneyWithdrawnQuery() {
  WorldPacket pkt(Opcode::MSG_GUILD_BANK_MONEY_WITHDRAWN);
  Send(pkt);
}

void InteractionSender::SendGuildBankDepositMoney(std::uint64_t banker_guid, std::uint32_t amount) {
  auto pkt = PacketSender::BuildGuildBankDepositMoney(banker_guid, amount);
  Send(pkt);
}

void InteractionSender::SendGuildBankWithdrawMoney(std::uint64_t banker_guid,
                                                   std::uint32_t amount) {
  auto pkt = PacketSender::BuildGuildBankWithdrawMoney(banker_guid, amount);
  Send(pkt);
}

void InteractionSender::SendGuildBankerActivate(std::uint64_t guid) {
  const auto active = GuildSystem::Get().GetBankerGuid();
  if (active != 0 && active == guid) {
    return;
  }

  WorldPacket pkt(Opcode::CMSG_GUILD_BANKER_ACTIVATE);
  pkt.AppendU64(guid);
  pkt.AppendU8(GuildSystem::Get().IsGuildBankTabContentsRefreshPending(0) ? 1 : 0);
  Send(pkt);
  GuildSystem::Get().SetPendingBankerGuid(guid);
}

void InteractionSender::SendGuildBankQueryTab(std::uint64_t guid, std::uint8_t tab) {
  const bool full_update = GuildSystem::Get().IsGuildBankTabContentsRefreshPending(tab);
  auto pkt = PacketSender::BuildGuildBankQueryTab(guid, tab, full_update);
  Send(pkt);
}

void InteractionSender::SendGuildBankBuyTab(std::uint64_t guid, std::uint8_t tab) {
  WorldPacket pkt(Opcode::CMSG_GUILD_BANK_BUY_TAB);
  pkt.AppendU64(guid);
  pkt.AppendU8(tab);
  Send(pkt);
}

void InteractionSender::SendGuildBankUpdateTab(std::uint64_t guid, std::uint8_t tab,
                                               const std::string &name, const std::string &icon) {
  WorldPacket pkt(Opcode::CMSG_GUILD_BANK_UPDATE_TAB);
  pkt.AppendU64(guid);
  pkt.AppendU8(tab);
  pkt.AppendString(name.c_str());
  pkt.AppendString(icon.c_str());
  Send(pkt);
}

void InteractionSender::SendGuildBankSetTabText(std::uint8_t tab, const std::string &text) {
  WorldPacket pkt(Opcode::CMSG_SET_GUILD_BANK_TEXT);
  pkt.AppendU8(tab);
  pkt.AppendString(text.c_str());
  Send(pkt);
}

void InteractionSender::SendGuildBankLogQuery(std::uint8_t tab) {
  WorldPacket pkt(Opcode::MSG_GUILD_BANK_LOG_QUERY);
  pkt.AppendU8(tab);
  Send(pkt);
}

void InteractionSender::SendGuildEventLogQuery() {
  WorldPacket pkt(Opcode::MSG_GUILD_EVENT_LOG_QUERY);
  Send(pkt);
}

void InteractionSender::SendGuildQueryBankText(std::uint8_t tab) {
  WorldPacket pkt(Opcode::MSG_QUERY_GUILD_BANK_TEXT);
  pkt.AppendU8(tab);
  Send(pkt);
}

void InteractionSender::SendGuildInfoText(const std::string &text) {
  auto pkt = PacketSender::BuildGuildInfoText(text);
  Send(pkt);
}

void InteractionSender::SendGuildSetRank(const std::string &name, std::uint32_t rank_id,
                                         std::uint32_t flags, std::uint32_t withdraw_limit,
                                         std::uint32_t tab_flags_count,
                                         const std::uint32_t *tab_flags,
                                         const std::uint32_t *tab_slots) {

  WorldPacket pkt(Opcode::CMSG_GUILD_RANK);
  pkt.AppendU32(rank_id);
  pkt.AppendU32(flags);
  pkt.AppendString(name.c_str());
  pkt.AppendU32(withdraw_limit);
  for (std::uint32_t i = 0; i < tab_flags_count && i < 6; ++i) {
    pkt.AppendU32(tab_flags ? tab_flags[i] : 0);
    pkt.AppendU32(tab_slots ? tab_slots[i] : 0);
  }
  Send(pkt);
}

void InteractionSender::SendInitiateTrade(std::uint64_t target_guid,
                                          bool defer_cursor_item_placement) {
  if (session_ == nullptr) {
    return;
  }

  if (session_->trade().begin_trade_guid() != 0) {
    ui::game::DisplaySystemMessage(kTradeAlreadyPendingSystemMessageId);
    return;
  }

  const auto now_ms = core::GameClock::GetTickCount32();
  if (session_->trade().IsInitiateThrottled(now_ms)) {
    return;
  }
  session_->trade().ResetInitiateThrottle(now_ms);

  auto pkt = trade_protocol::EncodeInitiate(target_guid);
  Send(pkt);

  session_->trade().MarkLocalInitiateRequest(target_guid, defer_cursor_item_placement);

  const auto *name_info =
      session_->query_cache().GetOrRequestPlayerName(target_guid);
  if (name_info) {
    ui::game::DisplaySystemMessage(kTradeInitiatingSystemMessageId,
                                   name_info->name.c_str());
  }
}

void InteractionSender::SendBeginTrade() {
  auto pkt = trade_protocol::EncodeBegin();
  Send(pkt);
}

void InteractionSender::SendBusyTrade() {
  auto pkt = trade_protocol::EncodeBusy();
  Send(pkt);
}

void InteractionSender::SendIgnoreTrade() {
  auto pkt = trade_protocol::EncodeIgnore();
  Send(pkt);
}

void InteractionSender::SendAcceptTrade(std::uint32_t accept_cookie) {
  auto pkt = trade_protocol::EncodeAccept(accept_cookie);
  Send(pkt);
}

void InteractionSender::SendUnacceptTrade() {
  auto pkt = trade_protocol::EncodeUnaccept();
  Send(pkt);
}

void InteractionSender::SendCancelTrade() {
  auto pkt = trade_protocol::EncodeCancel();
  Send(pkt);
}

void InteractionSender::SendSetTradeGold(std::uint32_t copper) {
  auto pkt = trade_protocol::EncodeSetGold(copper);
  Send(pkt);
}

bool InteractionSender::SendSetTradeItem(std::uint8_t trade_slot, std::uint8_t bag,
                                         std::uint8_t bag_slot) {
  auto pkt = trade_protocol::EncodeSetItem(trade_slot, bag, bag_slot);
  return Send(pkt);
}

bool InteractionSender::SendClearTradeItem(std::uint8_t trade_slot) {
  auto pkt = trade_protocol::EncodeClearItem(trade_slot);
  return Send(pkt);
}

void InteractionSender::SendLootRoll(std::uint64_t loot_guid, std::uint32_t slot,
                                     std::uint8_t roll_type) {
  auto pkt = loot_protocol::EncodeRollRequest(
      ObjectGuid(loot_guid), slot, static_cast<LootRollType>(roll_type));
  Send(pkt);
}

void InteractionSender::SendLootMethod(std::uint32_t method, std::uint64_t master_guid,
                                       std::uint32_t threshold) {
  auto pkt = PacketSender::BuildLootMethod(method, master_guid, threshold);
  Send(pkt);
}

void InteractionSender::SendLootMasterGive(std::uint64_t loot_guid, std::uint8_t slot,
                                           std::uint64_t target_guid) {
  auto pkt = loot_protocol::EncodeMasterGiveRequest(
      ObjectGuid(loot_guid), slot, ObjectGuid(target_guid));
  Send(pkt);
}

void InteractionSender::SendOptOutOfLoot(bool opt_out) {
  WorldPacket pkt(Opcode::CMSG_OPT_OUT_OF_LOOT);
  pkt.AppendU32(opt_out ? 1 : 0);
  Send(pkt);
}

void InteractionSender::SendDuelAccepted(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_DUEL_ACCEPTED);
  pkt.AppendU64(guid);
  Send(pkt);
}

void InteractionSender::SendDuelCancelled(std::uint64_t guid) {
  WorldPacket pkt(Opcode::CMSG_DUEL_CANCELLED);
  pkt.AppendU64(guid);
  Send(pkt);
}

void InteractionSender::SendAlterAppearance(std::uint32_t hair_style_id, std::uint32_t hair_color,
                                            std::uint32_t facial_hair_style_id,
                                            std::uint32_t skin_color_style_id) {
  WorldPacket pkt(Opcode::CMSG_ALTER_APPEARANCE);
  pkt.AppendU32(hair_style_id);
  pkt.AppendU32(hair_color);
  pkt.AppendU32(facial_hair_style_id);
  pkt.AppendU32(skin_color_style_id);
  Send(pkt);
}

void InteractionSender::SendSetTitle(std::uint32_t title_id) {
  WorldPacket pkt(Opcode::CMSG_SET_TITLE);
  pkt.AppendU32(title_id);
  Send(pkt);
}

void InteractionSender::SendPetitionRename(std::uint64_t petition_guid,
                                           const std::string &new_name) {
  Send(PacketSender::BuildPetitionRename(petition_guid, new_name));
}

void InteractionSender::SendOfferPetition(std::uint32_t petition_type_field,
                                          std::uint64_t petition_guid,
                                          std::uint64_t target_guid) {
  WorldPacket pkt(Opcode::CMSG_OFFER_PETITION);

  pkt.AppendU32(petition_type_field);
  pkt.AppendU64(petition_guid);
  pkt.AppendU64(target_guid);
  Send(pkt);
}

void InteractionSender::SendPetitionBuy(std::uint64_t npc_guid, const std::string &petition_name) {
  SendPetitionBuy(npc_guid, 0, petition_name);
}

void InteractionSender::SendPetitionBuy(std::uint64_t npc_guid, const std::uint32_t petition_type,
                                        const std::string &petition_name) {
  Send(PacketSender::BuildPetitionBuy(npc_guid, petition_type, petition_name));
}

void InteractionSender::SendTabardVendorActivate(std::uint64_t vendor_guid) {
  Send(PacketSender::BuildTabardVendorActivate(vendor_guid));
}

void InteractionSender::SendPetitionShowList(std::uint64_t npc_guid) {
  Send(PacketSender::BuildPetitionShowList(npc_guid));
}

void InteractionSender::SendShowingHelm(bool show) {
  WorldPacket pkt(Opcode::CMSG_SHOWING_HELM);
  pkt.AppendU8(show ? 1 : 0);
  Send(pkt);
}

void InteractionSender::SendShowingCloak(bool show) {
  WorldPacket pkt(Opcode::CMSG_SHOWING_CLOAK);
  pkt.AppendU8(show ? 1 : 0);
  Send(pkt);
}

void InteractionSender::SendSetAllowLowLevelRaid(bool allow) {
  WorldPacket enable_low_level_raid_1(Opcode::CMSG_SET_ALLOW_LOW_LEVEL_RAID1);
  enable_low_level_raid_1.AppendU8(allow ? 1 : 0);
  Send(enable_low_level_raid_1);

  WorldPacket enable_low_level_raid_2(Opcode::CMSG_SET_ALLOW_LOW_LEVEL_RAID2);
  enable_low_level_raid_2.AppendU8(allow ? 1 : 0);
  Send(enable_low_level_raid_2);
}

void InteractionSender::SendPushQuestToParty(std::uint32_t quest_id) {
  WorldPacket pkt(Opcode::CMSG_PUSHQUESTTOPARTY);
  pkt.AppendU32(quest_id);
  Send(pkt);
}

void InteractionSender::SendEquipItem(std::uint64_t item_guid, std::uint8_t dst_slot) {
  WorldPacket pkt(Opcode::CMSG_AUTOEQUIP_ITEM_SLOT);
  pkt.AppendU64(item_guid);
  pkt.AppendU8(dst_slot);
  Send(pkt);
}

void InteractionSender::SendEquipItem(std::uint8_t src_bag, std::uint8_t src_slot,
                                      std::uint8_t dst_slot) {
  const auto &inventory = session_->inventory_replica();
  const ItemInstance *item = nullptr;
  if (src_bag == InventorySlots::kMainBag) {
    item = inventory.GetItemInSlot(src_slot);
  } else {
    item = inventory.GetBagSlot(src_bag, src_slot);
  }

  if (item == nullptr || item->guid == 0) {
    return;
  }

  SendEquipItem(item->guid, dst_slot);
}

void InteractionSender::SendSetActionBarToggles(std::uint8_t flags) {
  auto pkt = PacketSender::BuildSetActionBarToggles(flags);
  Send(pkt);
}

void InteractionSender::SendTogglePetAutocast(std::uint32_t spell_id, bool enabled) {
  const auto pet_guid = session_ ? session_->pet().pet_bar().guid.GetRawValue() : 0;
  auto pkt = PacketSender::BuildPetSpellAutocast(pet_guid, spell_id, enabled);
  Send(pkt);
}

void InteractionSender::SendBugReport(const std::string &description) {
  if (session_ == nullptr || description.empty()) {
    return;
  }

  WorldPacket pkt(Opcode::CMSG_BUG);
  AppendClientBugReportPayload(pkt, 0u, BuildClientBugReportSummary(*session_), description);
  Send(pkt);
}

void InteractionSender::SendSuggestionReport(const std::string &description) {
  if (session_ == nullptr || description.empty()) {
    return;
  }

  WorldPacket pkt(Opcode::CMSG_BUG);
  AppendClientBugReportPayload(pkt, 1u, BuildClientBugReportSummary(*session_), description);
  Send(pkt);
}

void InteractionSender::SendGMReportLag(const std::int32_t lag_type_argument) {
  if (session_ == nullptr) {
    return;
  }

  const auto location = ResolveActivePlayerWorldLocation(*session_);
  if (!location.has_value()) {
    return;
  }

  WorldPacket pkt(Opcode::CMSG_GM_REPORT_LAG);
  pkt.AppendU32(static_cast<std::uint32_t>(lag_type_argument) - 1u);
  pkt.AppendU32(location->map_id);
  pkt.AppendFloat(location->x);
  pkt.AppendFloat(location->y);
  pkt.AppendFloat(location->z);
  Send(pkt);
}

void InteractionSender::SendCalendarGetCalendar() {
  if (!session_) {
    return;
  }
  auto pkt = PacketSender::BuildCalendarGetCalendar();
  Send(pkt);
  CalendarSystem::Get().MarkInitialSnapshotRequested();
}

void InteractionSender::SendCalendarGetNumPending() {
  auto pkt = PacketSender::BuildCalendarGetNumPending();
  Send(pkt);
}

void InteractionSender::SendCalendarGetEvent(std::uint64_t event_id) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_GET_EVENT);
  pkt.AppendU64(event_id);
  Send(pkt);
}

void InteractionSender::SendCalendarAddEvent(
    const std::string &title, const std::string &description, std::uint8_t type,
    std::uint8_t repeat_option, std::uint32_t max_invites, std::int32_t dungeon_id,
    std::uint32_t event_time, std::uint32_t secondary_time, std::uint32_t flags,
    const std::vector<net::wotlk::CalendarAddEventInvite> &invites) {
  auto pkt = PacketSender::BuildCalendarAddEvent(title, description, type, repeat_option,
                                                 max_invites, dungeon_id, event_time,
                                                 secondary_time, flags, invites);
  Send(pkt);
}

void InteractionSender::SendCalendarUpdateEvent(std::uint64_t event_id, std::uint64_t invite_id,
                                                const std::string &title,
                                                const std::string &description, std::uint8_t type,
                                                std::uint8_t repeat_option,
                                                std::uint32_t max_invites, std::int32_t dungeon_id,
                                                std::uint32_t event_time,
                                                std::uint32_t secondary_time, std::uint32_t flags) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_UPDATE_EVENT);
  pkt.AppendU64(event_id);
  pkt.AppendU64(invite_id);
  pkt.AppendString(title.c_str());
  pkt.AppendString(description.c_str());
  pkt.AppendU8(type);
  pkt.AppendU8(repeat_option);
  pkt.AppendU32(max_invites);
  pkt.AppendU32(static_cast<std::uint32_t>(dungeon_id));
  pkt.AppendU32(event_time);
  pkt.AppendU32(secondary_time);
  pkt.AppendU32(flags);
  Send(pkt);
}

void InteractionSender::SendCalendarRemoveEvent(std::uint64_t event_id, std::uint64_t invite_id,
                                                std::uint32_t flags) {
  auto pkt = PacketSender::BuildCalendarRemoveEvent(event_id, invite_id, flags);
  Send(pkt);
}

void InteractionSender::SendCalendarRemoveEventBuffer(std::uint64_t event_id,
                                                      std::uint64_t invite_id,
                                                      const bool uses_guild_calendar) {
  auto pkt = PacketSender::BuildCalendarRemoveEventBuffer(event_id, invite_id, uses_guild_calendar);
  Send(pkt);
}

bool InteractionSender::SendCalendarEventInvite(std::uint64_t event_id, std::uint64_t invite_id,
                                                const std::string &name, bool is_pre_invite,
                                                bool is_guild) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_EVENT_INVITE);
  pkt.AppendU64(event_id);
  pkt.AppendU64(invite_id);
  pkt.AppendString(name.c_str());
  pkt.AppendU8(is_pre_invite ? 1 : 0);
  pkt.AppendU8(is_guild ? 1 : 0);
  return Send(pkt);
}

void InteractionSender::SendCalendarEventRsvp(std::uint64_t event_id, std::uint64_t invite_id,
                                              std::uint32_t status) {
  auto pkt = PacketSender::BuildCalendarEventRsvp(event_id, invite_id, status);
  Send(pkt);
}

void InteractionSender::SendCalendarEventRemoveInvite(std::uint64_t target_invitee_guid,
                                                      std::uint64_t event_id,
                                                      std::uint64_t target_invite_id,
                                                      std::uint64_t self_invite_id) {
  auto pkt = PacketSender::BuildCalendarEventRemoveInvite(target_invitee_guid, event_id,
                                                          target_invite_id, self_invite_id);
  Send(pkt);
}

void InteractionSender::SendCalendarEventSignUp(std::uint64_t event_id, std::uint8_t tentative) {
  auto pkt = PacketSender::BuildCalendarEventSignUp(event_id, tentative);
  Send(pkt);
}

void InteractionSender::SendCalendarEventModeratorStatus(std::uint64_t target_invitee_guid,
                                                         std::uint64_t event_id,
                                                         std::uint64_t target_invite_id,
                                                         std::uint64_t self_invite_id,
                                                         std::uint32_t status) {
  auto pkt = PacketSender::BuildCalendarEventModeratorStatus(
      target_invitee_guid, event_id, target_invite_id, self_invite_id, status);
  Send(pkt);
}

void InteractionSender::SendCalendarEventStatus(std::uint64_t target_invitee_guid,
                                                std::uint64_t event_id,
                                                std::uint64_t target_invite_id,
                                                std::uint64_t self_invite_id,
                                                std::uint32_t status) {
  auto pkt = PacketSender::BuildCalendarEventStatus(target_invitee_guid, event_id, target_invite_id,
                                                    self_invite_id, status);
  Send(pkt);
}

void InteractionSender::SendCalendarComplain(std::uint64_t creator_guid, std::uint64_t event_id,
                                             std::uint64_t self_invite_id) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_COMPLAIN);
  pkt.AppendU64(creator_guid);
  pkt.AppendU64(event_id);
  pkt.AppendU64(self_invite_id);
  Send(pkt);
}

void InteractionSender::SendCalendarCopyEvent(std::uint64_t event_id, std::uint64_t invite_id,
                                              std::uint32_t event_time) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_COPY_EVENT);
  pkt.AppendU64(event_id);
  pkt.AppendU64(invite_id);
  pkt.AppendU32(event_time);
  Send(pkt);
}

void InteractionSender::SendCalendarGuildFilter(std::uint32_t min_level, std::uint32_t max_level,
                                                std::uint32_t rank) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_GUILD_FILTER);
  pkt.AppendU32(min_level);
  pkt.AppendU32(max_level);
  pkt.AppendU32(rank);
  Send(pkt);
}

void InteractionSender::SendCalendarArenaTeam(std::uint32_t team_id) {
  WorldPacket pkt(Opcode::CMSG_CALENDAR_ARENA_TEAM);
  pkt.AppendU32(team_id);
  Send(pkt);
}

bool InteractionSender::SendPetAction(std::uint64_t pet_guid,
                                      std::uint32_t action_data,
                                      std::uint64_t target_guid) {
  const auto action_kind =
      static_cast<std::uint8_t>((action_data >> 24) & 0x3Fu);
  if (IsPetSpellActionKind(action_kind)) {
    return session_ != nullptr &&
           SpellAction_ValidateAndInitiatePetCast(
               *session_, ObjectGuid(pet_guid),
               action_data & PetActionButton::kActionIdMask,
               ObjectGuid(target_guid));
  }
  auto pkt = PacketSender::BuildPetAction(pet_guid, action_data, target_guid);
  return Send(pkt);
}

void InteractionSender::SendPetSetAction(
    std::uint64_t pet_guid, std::optional<net::wotlk::PetSetActionSlotState> secondary_slot,
    net::wotlk::PetSetActionSlotState target_slot) {
  auto pkt = PacketSender::BuildPetSetAction(pet_guid, secondary_slot, target_slot);
  Send(pkt);
}

void InteractionSender::SendPetAbandon(std::uint64_t pet_guid) {
  auto pkt = PacketSender::BuildPetAbandon(pet_guid);
  Send(pkt);
}

void InteractionSender::SendPetCancelAura(std::uint64_t pet_guid, std::uint32_t spell_id) {
  auto pkt = PetManager::BuildPetCancelAura(ObjectGuid(pet_guid), spell_id);
  Send(pkt);
}

void InteractionSender::SendDismissCritter(std::uint64_t critter_guid) {
  auto pkt = PacketSender::BuildDismissCritter(critter_guid);
  Send(pkt);
}

void InteractionSender::SendPetRename(std::uint64_t pet_guid, const std::string &name,
                                      const std::array<std::string, 5> *declined_names) {
  auto pkt = PacketSender::BuildPetRename(pet_guid, name, declined_names);
  Send(pkt);
}

void InteractionSender::SendPetStopAttack(std::uint64_t pet_guid) {
  WorldPacket pkt(Opcode::CMSG_PET_STOP_ATTACK);
  pkt.AppendU64(pet_guid);
  Send(pkt);
}

void InteractionSender::SendListStabledPets(std::uint64_t npc_guid) {
  WorldPacket pkt(Opcode::MSG_LIST_STABLED_PETS);
  pkt.AppendU64(npc_guid);
  Send(pkt);
}

void InteractionSender::SendStablePet(std::uint64_t npc_guid) {
  WorldPacket pkt(Opcode::CMSG_STABLE_PET);
  pkt.AppendU64(npc_guid);
  Send(pkt);
}

void InteractionSender::SendUnstablePet(std::uint64_t npc_guid, std::uint32_t pet_number) {
  WorldPacket pkt(Opcode::CMSG_UNSTABLE_PET);
  pkt.AppendU64(npc_guid);
  pkt.AppendU32(pet_number);
  Send(pkt);
}

void InteractionSender::SendBuyStableSlot(std::uint64_t npc_guid) {
  WorldPacket pkt(Opcode::CMSG_BUY_STABLE_SLOT);
  pkt.AppendU64(npc_guid);
  Send(pkt);
}

void InteractionSender::SendStableSwapPet(std::uint64_t npc_guid, std::uint32_t slot) {
  WorldPacket pkt(Opcode::CMSG_STABLE_SWAP_PET);
  pkt.AppendU64(npc_guid);
  pkt.AppendU32(slot);
  Send(pkt);
}

void InteractionSender::SendStableRevivePet(std::uint64_t npc_guid) {
  WorldPacket pkt(Opcode::CMSG_STABLE_REVIVE_PET);
  pkt.AppendU64(npc_guid);
  Send(pkt);
}

void InteractionSender::SendTogglePvP() {
  WorldPacket pkt(Opcode::CMSG_TOGGLE_PVP);
  Send(pkt);
}

void InteractionSender::SendSetPvP(const std::uint8_t flag) {
  auto pkt = PacketSender::BuildSetPvP(flag);
  Send(pkt);
}

void InteractionSender::SendBattlefieldStatus() {
  WorldPacket pkt(Opcode::CMSG_BATTLEFIELD_STATUS);
  Send(pkt);
}

void InteractionSender::SendBattlefieldList(std::uint32_t bg_type,
                                            std::uint8_t from_where,
                                            std::uint8_t xp_locked) {
  WorldPacket pkt(Opcode::CMSG_BATTLEFIELD_LIST);
  pkt.AppendU32(bg_type);
  pkt.AppendU8(from_where);
  pkt.AppendU8(xp_locked);
  Send(pkt);
}

void InteractionSender::SendBattlemasterJoin(std::uint32_t bg_type, std::uint32_t instance_id,
                                             bool as_group) {
  const auto battlemaster_guid =
      session_ ? session_->battleground().battlefield_list().battlemaster_guid : 0;
  auto pkt = PacketSender::BuildBattlemasterJoin(battlemaster_guid, bg_type, instance_id, as_group);
  Send(pkt);
}

void InteractionSender::SendBattlemasterJoinArena(const std::uint8_t slot, const bool as_group,
                                                  const bool is_rated) {
  const auto battlemaster_guid =
      session_ ? session_->battleground().battlefield_list().battlemaster_guid : 0;
  auto pkt = PacketSender::BuildBattlemasterJoinArena(battlemaster_guid, slot, as_group, is_rated);
  Send(pkt);
}

void InteractionSender::SendBattlefieldMgrEntryInviteResponse(std::uint32_t battlefield_id,
                                                              bool accepted) {
  Send(BattlefieldMgrHandler::BuildEntryInviteResponse(battlefield_id, accepted));
}

void InteractionSender::SendBattlefieldMgrQueueInviteResponse(std::uint32_t battlefield_id,
                                                              bool accepted) {
  Send(BattlefieldMgrHandler::BuildQueueInviteResponse(battlefield_id, accepted));
}

void InteractionSender::SendBattlefieldMgrQueueRequest(std::uint32_t battlefield_id) {
  Send(BattlefieldMgrHandler::BuildQueueRequest(battlefield_id));
}

void InteractionSender::SendBattlefieldMgrExitRequest(std::uint32_t battlefield_id) {
  Send(BattlefieldMgrHandler::BuildExitRequest(battlefield_id));
}

void InteractionSender::SendLeaveBattlefield() {
  auto pkt = PacketSender::BuildLeaveBattlefield(BattlefieldInfo::Get().GetActiveBattlefieldGuid());
  Send(pkt);
}

void InteractionSender::SendPvpLogData() {
  WorldPacket pkt(Opcode::MSG_PVP_LOG_DATA);
  Send(pkt);
}

void InteractionSender::SendReportPvpAfk(std::uint64_t target_guid) {
  WorldPacket pkt(Opcode::CMSG_REPORT_PVP_AFK);
  pkt.AppendU64(target_guid);
  Send(pkt);
}

void InteractionSender::SendLfgJoin(std::uint32_t roles, const std::vector<std::uint32_t> &dungeons,
                                    const std::string &comment) {
  Send(PacketSender::BuildLfgJoin(roles, dungeons, comment));
}

void InteractionSender::SendLfgLeave() {
  auto pkt = PacketSender::BuildLfgLeave();
  Send(pkt);
}

void InteractionSender::SendLfgSetRoles(std::uint8_t roles) {
  Send(PacketSender::BuildLfgSetRoles(roles));
}

void InteractionSender::SendLfgSetComment(const std::string &comment) {
  WorldPacket pkt(Opcode::CMSG_SET_LFG_COMMENT);
  pkt.AppendString(comment.c_str());
  Send(pkt);
}

void InteractionSender::SendLfgGetStatus() {
  WorldPacket pkt(Opcode::CMSG_LFG_GET_STATUS);
  Send(pkt);
}

void InteractionSender::SendLfdPlayerLockInfoRequest() {
  WorldPacket pkt(Opcode::CMSG_LFD_PLAYER_LOCK_INFO_REQUEST);
  Send(pkt);
}

void InteractionSender::SendLfgSearchJoin(std::uint32_t packed_search_id) {
  if (!session_ || session_->lfg().joined_search_id() == packed_search_id) {
    return;
  }

  session_->lfg().StartSearchBrowse(packed_search_id);

  WorldPacket pkt(Opcode::CMSG_SEARCH_LFG_JOIN);
  pkt.AppendU32(packed_search_id);
  Send(pkt);
}

void InteractionSender::SendLfgSearchLeave() {
  if (!session_) {
    return;
  }

  const auto joined_search_id = session_->lfg().joined_search_id();
  if (joined_search_id == 0) {
    return;
  }

  WorldPacket pkt(Opcode::CMSG_SEARCH_LFG_LEAVE);
  pkt.AppendU32(joined_search_id);
  Send(pkt);
  session_->lfg().StopSearchBrowse();
}

void InteractionSender::SendGuildAddRank(const std::string &name) {
  WorldPacket pkt(Opcode::CMSG_GUILD_ADD_RANK);
  pkt.AppendString(name.c_str());
  Send(pkt);
}

void InteractionSender::SendGuildDeleteRank() {
  WorldPacket pkt(Opcode::CMSG_GUILD_DEL_RANK);
  Send(pkt);
}

bool InteractionSender::SendRawPacket(net::wotlk::WorldPacket &pkt) {
  return Send(pkt);
}

void InteractionSender::SendRequestVehicleExit() {
  auto pkt = PacketSender::BuildRequestVehicleExit();
  Send(pkt);
}

void InteractionSender::SendRequestVehicleNextSeat() {
  auto pkt = PacketSender::BuildRequestVehicleNextSeat();
  Send(pkt);
}

void InteractionSender::SendRequestVehiclePrevSeat() {
  auto pkt = PacketSender::BuildRequestVehiclePrevSeat();
  Send(pkt);
}

void InteractionSender::SendRequestVehicleSwitchSeat(
    const std::uint64_t vehicle_guid, const std::uint8_t seat_index) {
  auto pkt = PacketSender::BuildRequestVehicleSwitchSeat(vehicle_guid, seat_index);
  Send(pkt);
}

void InteractionSender::SendChangeSeatOnVehicle(std::uint8_t seat_index) {
  WorldPacket pkt(Opcode::CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE);
  pkt.AppendU8(seat_index);
  Send(pkt);
}

void InteractionSender::SendSpellClick(std::uint64_t unit_guid) {
  Send(PacketSender::BuildSpellClick(unit_guid));
}

void InteractionSender::SendPlayerVehicleEnter(std::uint64_t unit_guid) {
  Send(PacketSender::BuildPlayerVehicleEnter(unit_guid));
}

void InteractionSender::SendChangePlayerDifficulty(std::uint32_t difficulty) {
  WorldPacket pkt(Opcode::CMSG_CHANGEPLAYER_DIFFICULTY);
  pkt.AppendU32(difficulty);
  Send(pkt);
}

void InteractionSender::SendSetDungeonDifficulty(std::uint32_t difficulty) {
  WorldPacket pkt(Opcode::MSG_SET_DUNGEON_DIFFICULTY);
  pkt.AppendU32(difficulty);
  Send(pkt);
}

void InteractionSender::SendSetRaidDifficulty(std::uint32_t difficulty) {
  WorldPacket pkt(Opcode::MSG_SET_RAID_DIFFICULTY);
  pkt.AppendU32(difficulty);
  Send(pkt);
}

void InteractionSender::SendResetInstances() {
  WorldPacket pkt(Opcode::CMSG_RESET_INSTANCES);
  Send(pkt);
}

void InteractionSender::SendSetSavedInstanceExtend(const std::uint32_t map_id,
                                                   const std::uint32_t difficulty,
                                                   const bool extended) {
  WorldPacket pkt(Opcode::CMSG_SET_SAVED_INSTANCE_EXTEND);
  pkt.AppendU32(map_id);
  pkt.AppendU32(difficulty);
  pkt.AppendU8(extended ? 1u : 0u);
  Send(pkt);
}

void InteractionSender::SendRaidTargetUpdate(std::uint64_t guid, std::uint8_t icon) {
  WorldPacket pkt(Opcode::MSG_RAID_TARGET_UPDATE);
  pkt.AppendU8(icon);
  pkt.AppendU64(guid);
  Send(pkt);
}

void InteractionSender::SendRequestAllRaidTargets() {
  WorldPacket pkt(Opcode::MSG_RAID_TARGET_UPDATE);
  pkt.AppendU8(0xFF);
  Send(pkt);
}

void InteractionSender::SendRequestRaidInfo() {
  WorldPacket pkt(Opcode::CMSG_REQUEST_RAID_INFO);
  Send(pkt);
}

void InteractionSender::SendPartyAssignment(std::uint8_t role, bool apply,
                                            std::uint64_t target_guid) {
  WorldPacket pkt(Opcode::MSG_PARTY_ASSIGNMENT);
  pkt.AppendU8(role);
  pkt.AppendU8(apply ? 1 : 0);
  pkt.AppendU64(target_guid);
  Send(pkt);
}

void InteractionSender::SendArenaTeamAccept() {
  auto pkt = PacketSender::BuildArenaTeamAccept();
  Send(pkt);
}

void InteractionSender::SendArenaTeamDecline() {
  auto pkt = PacketSender::BuildArenaTeamDecline();
  Send(pkt);
}

void InteractionSender::SendArenaTeamLeave(std::uint32_t team_id) {
  WorldPacket pkt(Opcode::CMSG_ARENA_TEAM_LEAVE);
  pkt.AppendU32(team_id);
  Send(pkt);
}

void InteractionSender::SendArenaTeamDisband(std::uint32_t team_id) {
  WorldPacket pkt(Opcode::CMSG_ARENA_TEAM_DISBAND);
  pkt.AppendU32(team_id);
  Send(pkt);
}

void InteractionSender::SendArenaTeamInvite(std::uint32_t team_id, const std::string &name) {
  WorldPacket pkt(Opcode::CMSG_ARENA_TEAM_INVITE);
  pkt.AppendU32(team_id);
  pkt.AppendString(name.c_str());
  Send(pkt);
}

void InteractionSender::SendArenaTeamRemove(std::uint32_t team_id, const std::string &name) {
  WorldPacket pkt(Opcode::CMSG_ARENA_TEAM_REMOVE);
  pkt.AppendU32(team_id);
  pkt.AppendString(name.c_str());
  Send(pkt);
}

void InteractionSender::SendArenaTeamLeader(std::uint32_t team_id, const std::string &name) {
  WorldPacket pkt(Opcode::CMSG_ARENA_TEAM_LEADER);
  pkt.AppendU32(team_id);
  pkt.AppendString(name.c_str());
  Send(pkt);
}

void InteractionSender::SendArenaTeamRoster(std::uint32_t team_id) {
  WorldPacket pkt(Opcode::CMSG_ARENA_TEAM_ROSTER);
  pkt.AppendU32(team_id);
  Send(pkt);
}

void InteractionSender::SendArenaTeamQuery(std::uint32_t team_id) {
  WorldPacket pkt(Opcode::CMSG_ARENA_TEAM_QUERY);
  pkt.AppendU32(team_id);
  Send(pkt);
}

void InteractionSender::SendLfgBootVote(bool accept) {
  if (!LFGSystem::Get().TryPrepareBootVoteSend(accept)) {
    return;
  }
  auto pkt = PacketSender::BuildLfgSetBootVote(accept);
  Send(pkt);
}

void InteractionSender::SendLfgProposalResult(bool accept) {
  if (!session_) {
    return;
  }

  const auto &proposal = session_->lfg().proposal();

  if (!proposal.has_value()) {
    return;
  }
  if (!CanSendProposalResponse()) {
    return;
  }

  auto pkt = PacketSender::BuildLfgProposalResult(proposal->proposal_id, accept);
  Send(pkt);

  session_->lfg().ApplyProposalResponse(accept);
  LFGSystem::Get().ApplyProposalResponse(accept);
  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::LFG_PROPOSAL_UPDATE);
}

void InteractionSender::SendLfgTeleport(bool teleport_argument) {
  WorldPacket pkt(Opcode::CMSG_LFG_TELEPORT);
  pkt.AppendU8(teleport_argument ? 1 : 0);
  Send(pkt);
}

void InteractionSender::SendBattlefieldPort(const std::uint64_t battlefield_instance_guid,
                                            const bool accepted) {
  auto pkt = PacketSender::BuildBattlefieldPort(battlefield_instance_guid, accepted);
  Send(pkt);
}

void InteractionSender::SendPetitionSign(const std::uint64_t petition_guid,
                                         const std::uint8_t petition_choice) {
  Send(PacketSender::BuildPetitionSign(petition_guid, petition_choice));
}

void InteractionSender::SendTurnInPetition(const std::uint64_t petition_guid,
                                           const std::array<std::uint32_t, 5> &extra_fields) {
  auto pkt = PacketSender::BuildTurnInPetition(petition_guid, extra_fields);
  Send(pkt);
}

void InteractionSender::SendPetitionShowSignatures(std::uint64_t petition_guid) {
  WorldPacket pkt(Opcode::CMSG_PETITION_SHOW_SIGNATURES);
  pkt.AppendU64(petition_guid);
  Send(pkt);
}

bool InteractionSender::CanSendProposalResponse() {
  ++proposal_response_attempt_count_;
  if (proposal_response_attempt_count_ <= 2) {
    return true;
  }

  const double now_seconds = GetProposalResponseTimeSeconds();
  if (now_seconds - proposal_response_window_anchor_seconds_ < 10.0) {
    return false;
  }

  proposal_response_window_anchor_seconds_ = now_seconds;
  proposal_response_attempt_count_ = 0;
  return true;
}

double InteractionSender::GetProposalResponseTimeSeconds() const {
  if (proposal_response_clock_fn_) {
    return proposal_response_clock_fn_();
  }

  return DefaultProposalResponseTimeSeconds();
}

void InteractionSender::SendInspect(std::uint64_t guid) {
  auto pkt = PacketSender::BuildInspect(guid);
  Send(pkt);
}

void InteractionSender::SendInspectHonorDataRequests(std::uint64_t guid) {
  WorldPacket honor_pkt(Opcode::MSG_INSPECT_HONOR_STATS);
  honor_pkt.AppendU64(guid);
  Send(honor_pkt);

  WorldPacket arena_pkt(Opcode::MSG_INSPECT_ARENA_TEAMS);
  arena_pkt.AppendU64(guid);
  Send(arena_pkt);
}

void InteractionSender::SendUnlearnSkill(std::uint32_t skill_id) {
  WorldPacket pkt(Opcode::CMSG_UNLEARN_SKILL);
  pkt.AppendU32(skill_id);
  Send(pkt);
}

void InteractionSender::SendBuySkillStep(std::uint32_t skill_id) {
  auto pkt = PacketSender::BuildBuySkillStep(skill_id);
  Send(pkt);
}

void InteractionSender::SendBuySkillRanks(
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> &queued_ranks) {
  auto pkt = PacketSender::BuildBuySkillRanks(queued_ranks);
  Send(pkt);
}

void InteractionSender::SendCancelTempEnchantment(std::uint32_t slot) {
  WorldPacket pkt(Opcode::CMSG_CANCEL_TEMP_ENCHANTMENT);
  pkt.AppendU32(slot);
  Send(pkt);
}

void InteractionSender::SendLearnPreviewTalents(
    const std::vector<TalentEntry>& talents,
    const std::optional<ObjectGuid> pet_guid) {
  if (!session_ || talents.empty()) {
    return;
  }
  WorldPacket pkt(pet_guid.has_value()
                      ? Opcode::CMSG_LEARN_PREVIEW_TALENTS_PET
                      : Opcode::CMSG_LEARN_PREVIEW_TALENTS);
  if (pet_guid.has_value()) {
    pkt.AppendU64(pet_guid->GetRawValue());
  }
  pkt.AppendU32(static_cast<std::uint32_t>(talents.size()));
  for (const auto &talent : talents) {
    pkt.AppendU32(talent.talent_id);
    pkt.AppendU32(talent.rank);
  }
  Send(pkt);
}

void InteractionSender::SendGMTicketSystemStatus() {
  WorldPacket pkt(Opcode::CMSG_GMTICKET_SYSTEMSTATUS);
  Send(pkt);
}

void InteractionSender::SendGMTicketGetTicket() {
  WorldPacket pkt(Opcode::CMSG_GMTICKET_GETTICKET);
  Send(pkt);
}

void InteractionSender::SendGMTicketCreate(const std::string &description,
                                           std::uint8_t need_response) {
  if (session_ == nullptr || session_->gm_ticket().active_ticket_id() != 0) {
    return;
  }

  const auto context = BuildGMTicketSubmitContext(*session_);
  if (!context.has_value()) {
    return;
  }

  WorldPacket pkt(Opcode::CMSG_GMTICKET_CREATE);
  AppendGMTicketCreatePayload(pkt, *context, description, need_response != 0, false);
  Send(pkt);
}

void InteractionSender::SendGMResponseNeedMoreHelp(const std::string &description) {
  if (session_ == nullptr || session_->gm_ticket().active_response_id() == 0) {
    return;
  }

  const auto context = BuildGMTicketSubmitContext(*session_);
  if (!context.has_value()) {
    return;
  }

  WorldPacket pkt(Opcode::CMSG_GMTICKET_CREATE);
  AppendGMTicketCreatePayload(pkt, *context, description, true, true);
  Send(pkt);
}

void InteractionSender::SendGMTicketUpdateText(const std::string &text) {
  WorldPacket pkt(Opcode::CMSG_GMTICKET_UPDATETEXT);
  pkt.AppendString(text.c_str());
  Send(pkt);
}

void InteractionSender::SendGMTicketDelete() {
  WorldPacket pkt(Opcode::CMSG_GMTICKET_DELETETICKET);
  Send(pkt);
}

void InteractionSender::SendGMResponseResolve() {
  WorldPacket pkt(Opcode::CMSG_GMRESPONSE_RESOLVE);
  Send(pkt);
}

void InteractionSender::SendGMSurveySubmit() {
  if (session_ == nullptr) {
    return;
  }

  WorldPacket pkt(Opcode::CMSG_GMSURVEY_SUBMIT);
  const auto *dbc = session_->GetDbcLoader();
  const auto locale_index = Localization::Get().GetLocaleIndex();
  const auto *survey = dbc != nullptr ? ResolveCurrentGMSurvey(*dbc, locale_index) : nullptr;

  pkt.AppendU32(survey != nullptr ? survey->id : 0);

  for (std::uint32_t question_index = 0; question_index < kGMSurveyMaxQuestions; ++question_index) {
    const std::uint32_t question_id = survey != nullptr ? survey->questions[question_index] : 0;
    pkt.AppendU32(question_id);
    if (question_id == 0) {
      break;
    }

    const auto &question = session_->gm_survey().GetQuestion(question_index);
    pkt.AppendU8(question.rating);
    pkt.AppendString(question.comment.c_str());
  }

  pkt.AppendString(session_->gm_survey().GetOverallComment().c_str());
  Send(pkt);
  session_->gm_survey().Reset();
}

void InteractionSender::SendToggleXPGain() {
  WorldPacket pkt(Opcode::CMSG_TOGGLE_XP_GAIN);
  Send(pkt);
}

void InteractionSender::SendBuyBankSlot(std::uint64_t banker_guid) {
  auto pkt = PacketSender::BuildBuyBankSlot(banker_guid);
  Send(pkt);
}

void InteractionSender::SendGuildBankSwapItemsAutoStore(
    const std::uint64_t banker_guid, const std::uint8_t tab,
    const std::uint8_t slot, const std::uint32_t item_entry,
    const std::uint32_t item_count) {
  WorldPacket pkt(Opcode::CMSG_GUILD_BANK_SWAP_ITEMS);
  pkt.AppendU64(banker_guid);

  pkt.AppendU8(0);
  pkt.AppendU8(tab);
  pkt.AppendU8(slot);
  pkt.AppendU32(item_entry);
  pkt.AppendU8(1);
  pkt.AppendU32(item_count);
  pkt.AppendU8(1);
  pkt.AppendU32(0);
  Send(pkt);
}

void InteractionSender::SendGuildBankSwapItemsPlayerToBank(std::uint64_t banker_guid,
                                                           std::uint8_t tab, std::uint8_t slot,
                                                           std::uint32_t destination_item_entry,
                                                           std::uint8_t player_bag,
                                                           std::uint8_t player_slot,
                                                           std::uint32_t count) {
  Send(PacketSender::BuildGuildBankSwapItemsPlayerToBank(
      banker_guid, tab, slot, destination_item_entry, player_bag, player_slot, count));
}

void InteractionSender::SendGuildBankSwapItemsBankToPlayer(
    std::uint64_t banker_guid, std::uint8_t source_tab, std::uint8_t source_slot,
    std::uint32_t source_item_entry, std::uint8_t player_bag, std::uint8_t player_slot,
    std::uint32_t count) {
  Send(PacketSender::BuildGuildBankSwapItemsBankToPlayer(
      banker_guid, source_tab, source_slot, source_item_entry, player_bag, player_slot, count));
}

void InteractionSender::SendGuildBankSwapItemsBankToBank(
    std::uint64_t banker_guid, std::uint8_t source_tab, std::uint8_t source_slot,
    std::uint32_t destination_item_entry, std::uint8_t dest_tab, std::uint8_t dest_slot,
    std::uint32_t held_item_entry, std::uint32_t split_count) {
  Send(PacketSender::BuildGuildBankSwapItemsBankToBank(banker_guid, source_tab, source_slot,
                                                       destination_item_entry, dest_tab, dest_slot,
                                                       held_item_entry, split_count));
}

void InteractionSender::SendGuildBankSwapItemsBankToCursor(std::uint64_t banker_guid,
                                                           std::uint8_t tab, std::uint8_t slot) {
  WorldPacket pkt(Opcode::CMSG_GUILD_BANK_SWAP_ITEMS);
  pkt.AppendU64(banker_guid);
  pkt.AppendU8(1);
  pkt.AppendU32(tab);
  pkt.AppendU32(slot);
  pkt.AppendU32(0);
  pkt.AppendU8(0);
  pkt.AppendU32(0);
  pkt.AppendU8(0);
  pkt.AppendU8(0);
  pkt.AppendU8(0);
  Send(pkt);
}

void InteractionSender::SendPlayedTime(bool show_in_chat) {
  WorldPacket pkt(Opcode::CMSG_PLAYED_TIME);
  pkt.AppendU8(show_in_chat ? 1 : 0);
  Send(pkt);
}

void InteractionSender::SendOpeningCinematic() {

  WorldPacket pkt(Opcode::CMSG_OPENING_CINEMATIC);
  Send(pkt);
}

}


#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/game/commerce/adapters/ui/money_cursor_controller.h"
#include "openwow/ui/game/guild_bank_cursor_utils.h"
#include "openwow/ui/game/api/game_lua_api_guild.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/ui/game/api/game_lua_api_guild_roster_view.h"
#include "openwow/core/storm_string.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/adapters/retail/item_display_name_formatter.h"
#include "openwow/game/localization.h"
#include "openwow/game/name_validation.h"
#include "openwow/game/petition_frame.h"
#include "openwow/game/tabard_frame.h"
#include "openwow/game/tabard_renderer.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/gossip_manager.h"
#include "openwow/ui/game/cursor_texture_resolver.h"
#include "openwow/ui/game/runtime/lua/held_cursor_lua_binding.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>

namespace openwow::ui::game::detail {

namespace {

constexpr std::size_t kGuildMemberNameCommandLimit = 48;
constexpr std::size_t kGuildRosterNoteMaxBytes = 128;
constexpr std::size_t kGuildBankTabInfoMaxBytes = 64;
constexpr std::size_t kGuildBankTextMaxBytes = 0x2000;
constexpr std::array<std::uint32_t, 6> kGuildBankTabPurchaseCosts{
    1000000u, 2500000u, 5000000u, 10000000u, 25000000u, 50000000u};
constexpr int kGuildRosterNoteLeadByteCutoff = 32;
constexpr int kGuildBankTabInfoLeadByteCutoff = 16;

constexpr int kGuildBankTextLeadByteCutoff = 2048;

constexpr std::size_t kGuildControlRankNameBufferBytes = 0x40;
constexpr std::size_t kGuildControlRankNameLeadByteCutoff = 0x10;

openwow::game::MacroCatalog* FindMacroCatalog(lua_State* state) {
  auto* session = GetWorldSession(state);
  return session != nullptr ? &session->macros() : nullptr;
}

std::string NormalizeGuildRosterLookupName(const std::string_view raw_name) {
  const auto realm_separator = raw_name.find('-');
  if (realm_separator != std::string_view::npos && realm_separator > 0) {
    return std::string(raw_name.substr(0, realm_separator));
  }

  return std::string(raw_name);
}

std::optional<std::string>
ResolveGuildRosterLookupName(openwow::game::WorldSession& session, const std::string_view unit_or_name) {
  const auto guid = ResolveUnitId(&session, std::string(unit_or_name));
  if (guid.IsEmpty()) {
    return NormalizeGuildRosterLookupName(unit_or_name);
  }

  if (const auto* object = session.objects().GetObjectByGUID(guid);
      object != nullptr && object->IsPlayer()) {
    if (const auto name = object->GetName(); !name.empty()) {
      return NormalizeGuildRosterLookupName(name);
    }
  }

  if (const auto* cached_name = session.query_cache().GetPlayerName(guid.GetRawValue());
      cached_name != nullptr && !cached_name->name.empty()) {
    return NormalizeGuildRosterLookupName(cached_name->name);
  }

  if (const auto* name_entry = session.objects().GetNameEntry(guid);
      name_entry != nullptr && !name_entry->name.empty()) {
    return NormalizeGuildRosterLookupName(name_entry->name);
  }

  return std::nullopt;
}

enum class GuildRosterNoteKind : std::uint8_t {
  kPublic,
  kOfficer,
};

std::optional<std::uint32_t> GetGuildBankTabPurchaseCost(const std::size_t tab_count) {
  if (tab_count >= kGuildBankTabPurchaseCosts.size()) {
    return std::nullopt;
  }
  return kGuildBankTabPurchaseCosts[tab_count];
}

void PushGuildTabardFileNameFallback(lua_State* L) {
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
}

std::optional<std::string> ReadGuildMemberNameCommandArgument(lua_State* L) {
  const char* raw_name = lua_tostring(L, 1);
  if (raw_name == nullptr) {
    DisplaySystemMessage(325);
    return std::nullopt;
  }

  if (std::char_traits<char>::length(raw_name) > kGuildMemberNameCommandLimit) {
    luaL_error(L, "Name too long");
  }

  return std::string(raw_name);
}

const ::openwow::game::ItemInstance* ResolveGuildBankItem(
    const int lua_tab, const int lua_slot) {
  if (lua_tab < 1 || lua_slot < 1) {
    return nullptr;
  }

  return ::openwow::game::GuildSystem::Get().GetGuildBankTabItem(
      static_cast<std::uint8_t>(lua_tab - 1),
      static_cast<std::uint8_t>(lua_slot - 1));
}

void PushMissingGuildBankItemInfo(lua_State* L) {
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushnil(L);
}

int ReturnMissingGuildBankItemInfo(lua_State* L) {
  PushMissingGuildBankItemInfo(L);
  return 3;
}

std::string ResolveDisplayNameWithRandomProperty(
    lua_State* L, std::string display_name, const std::int32_t random_property_id) {
  return ::openwow::game::FormatItemDisplayNameWithRandomProperty(
      ::openwow::game::Localization::Get(), GetDbcLoader(L), display_name,
      random_property_id);
}

std::string SanitizeGuildScriptText(const char* raw_text,
                                    std::size_t max_bytes,
                                    int lead_byte_cutoff,
                                    bool truncate_at_line_breaks) {
  if (raw_text == nullptr || max_bytes == 0) {
    return {};
  }

  std::string sanitized;
  sanitized.reserve(max_bytes - 1);
  for (const auto* cursor = reinterpret_cast<const unsigned char*>(raw_text);
       *cursor != 0 && sanitized.size() + 1 < max_bytes;
       ++cursor) {
    if (*cursor == '|') {
      continue;
    }
    sanitized.push_back(static_cast<char>(*cursor));
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

  int lead_bytes = 0;
  for (std::size_t i = 0; i < sanitized.size(); ++i) {
    const auto ch = static_cast<unsigned char>(sanitized[i]);
    if ((ch & 0xC0u) == 0x80u) {
      continue;
    }

    ++lead_bytes;
    if (lead_bytes == lead_byte_cutoff) {
      sanitized.resize(i);
      break;
    }
  }

  return sanitized;
}

std::string NormalizeGuildRosterNoteText(const char* raw_note) {
  return SanitizeGuildScriptText(raw_note, kGuildRosterNoteMaxBytes,
                                 kGuildRosterNoteLeadByteCutoff, true);
}

std::string NormalizeGuildBankTabInfoName(const char* raw_name) {
  return SanitizeGuildScriptText(raw_name, kGuildBankTabInfoMaxBytes,
                                 kGuildBankTabInfoLeadByteCutoff, true);
}

std::string NormalizeGuildBankText(const char* raw_text) {
  return SanitizeGuildScriptText(raw_text, kGuildBankTextMaxBytes,
                                 kGuildBankTextLeadByteCutoff, false);
}

std::string_view TruncateGuildQueryRankNameForLua(
    const std::string_view raw_text) {
  for (std::size_t index = 0; index < raw_text.size(); ++index) {
    const char ch = raw_text[index];
    const bool terminator =
        (ch == '\\' || ch == '|') && index + 1 < raw_text.size() &&
        raw_text[index + 1] == 'n';
    if (ch == '\r' || ch == '\n' || terminator) {
      return raw_text.substr(0, index);
    }
  }

  return raw_text;
}

const std::string& GetGuildRosterNoteField(
    const ::openwow::game::GuildMember& member, const GuildRosterNoteKind kind) {
  return kind == GuildRosterNoteKind::kPublic ? member.note : member.officer_note;
}

void SendGuildRosterNoteUpdate(::openwow::game::WorldSession& session,
                               const ::openwow::game::GuildMember& member,
                               const GuildRosterNoteKind kind,
                               const std::string& note) {
  if (kind == GuildRosterNoteKind::kPublic) {
    session.interaction().SendGuildSetPublicNote(member.name, note);
    return;
  }

  session.interaction().SendGuildSetOfficerNote(member.name, note);
}

int LuaGuildRosterSetNote(lua_State* L,
                          const char* usage,
                          const GuildRosterNoteKind kind) {
  if (!lua_isnumber(L, 1) || !lua_isstring(L, 2)) {
    luaL_error(L, usage);
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto zero_based_index =
      SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  if (zero_based_index >=
      static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    return 0;
  }

  const auto* member =
      GetGuildRosterMemberByDisplayIndex(
          L, static_cast<int>(zero_based_index) + 1);
  if (member == nullptr) {
    return 0;
  }

  const char* raw_note = lua_tostring(L, 2);
  if (raw_note == nullptr) {
    return 0;
  }

  if (GetGuildRosterNoteField(*member, kind) == raw_note) {
    return 0;
  }

  SendGuildRosterNoteUpdate(*session, *member, kind,
                            NormalizeGuildRosterNoteText(raw_note));
  return 0;
}

std::optional<std::string> ResolveGuildBankTabInfoIconPath(lua_State* L) {
  auto* macros = FindMacroCatalog(L);
  if (macros == nullptr) {
    return std::nullopt;
  }
  if (macros->GetNumMacroItemIcons() == 0) {
    macros->LoadIconList();
  }

  const auto zero_based_icon = SaturateLuaNumberToU32(lua_tonumber(L, 3)) - 1u;
  if (zero_based_icon >= macros->GetNumMacroItemIcons()) {
    return std::nullopt;
  }

  return macros->MacroItemIconName(zero_based_icon + 1u);
}

void PushGuildTabardFileNames(lua_State* L,
                              const openwow::game::GuildEmblem& emblem) {
  lua_pushstring(
      L, openwow::game::BuildGuildTabardBackgroundTexturePath(
             openwow::game::TabardTextureHalf::Upper, emblem.background_color)
             .c_str());
  lua_pushstring(
      L, openwow::game::BuildGuildTabardBackgroundTexturePath(
             openwow::game::TabardTextureHalf::Lower, emblem.background_color)
             .c_str());
  lua_pushstring(
      L, openwow::game::BuildGuildTabardEmblemTexturePath(
             openwow::game::TabardTextureHalf::Upper, emblem.style,
             emblem.color)
             .c_str());
  lua_pushstring(
      L, openwow::game::BuildGuildTabardEmblemTexturePath(
             openwow::game::TabardTextureHalf::Lower, emblem.style,
             emblem.color)
             .c_str());
  lua_pushstring(
      L, openwow::game::BuildGuildTabardBorderTexturePath(
             openwow::game::TabardTextureHalf::Upper, emblem.border_style,
             emblem.border_color)
             .c_str());
  lua_pushstring(
      L, openwow::game::BuildGuildTabardBorderTexturePath(
             openwow::game::TabardTextureHalf::Lower, emblem.border_style,
             emblem.border_color)
             .c_str());
}

}

namespace guild_rights {
  inline constexpr std::uint32_t kInvite             = 0x00000010;
  inline constexpr std::uint32_t kRemove             = 0x00000020;
  inline constexpr std::uint32_t kCanGuildPromote    = 0x00000080;
  inline constexpr std::uint32_t kCanGuildDemote     = 0x00000100;
  inline constexpr std::uint32_t kCanEditMOTD        = 0x00001000;
  inline constexpr std::uint32_t kCanEditPublicNote  = 0x00002000;
  inline constexpr std::uint32_t kCanViewOfficerNote = 0x00004000;
  inline constexpr std::uint32_t kCanEditOfficerNote = 0x00008000;
  inline constexpr std::uint32_t kCanEditGuildInfo   = 0x00010000;
  inline constexpr std::uint32_t kCanGuildBankRepair = 0x00040000;
  inline constexpr std::uint32_t kCanWithdrawGuildBankMoney =
      0x00080000;
  inline constexpr std::uint32_t kLeader             = 0x00100000;
}

static constexpr std::uint32_t kRankFlagMasks[17] = {
    0x00000001,
    0x00000002,
    0x00000004,
    0x00000008,
    0x00000080,
    0x00000100,
    0x00000010,
    0x00000020,
    0x00001000,
    0x00002000,
    0x00004000,
    0x00008000,
    0x00010000,
    0x00020000,
    0x00040000,
    0x00080000,
    0x00100000,
};

static const ::openwow::game::GuildRank* TryGetActivePlayerGuildRank(
    lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    return nullptr;
  }

  const auto* player = session->objects().GetActivePlayer();
  if (!player || player->GetGuildID() == 0) {
    return nullptr;
  }

  return ::openwow::game::GuildSystem::Get().GetRank(player->GetGuildRank());
}

static std::optional<std::uint32_t> TryGetActivePlayerGuildRankFlags(
    lua_State* L) {
  if (const auto* rank = TryGetActivePlayerGuildRank(L)) {
    return rank->rights;
  }
  return std::nullopt;
}

static bool CanActivePlayerWithdrawGuildBankMoney(lua_State* L) {
  const auto* rank = TryGetActivePlayerGuildRank(L);
  return rank != nullptr && rank->money_per_day != 0 &&
         (rank->rights & guild_rights::kCanWithdrawGuildBankMoney) != 0;
}

static std::uint32_t GetActivePlayerGuildBankTabFlags(lua_State* L,
                                                      int tab_index) {
  auto* session = GetWorldSession(L);
  if (!session || tab_index < 0 ||
      tab_index >= static_cast<int>(::openwow::game::GuildSystem::kGuildBankMaxTabs)) {
    return 0;
  }

  const auto* player = session->objects().GetActivePlayer();
  if (player == nullptr) {
    return 0;
  }

  return ::openwow::game::GuildSystem::Get().GetControlBankTabFlags(
      player->GetGuildRank(), static_cast<std::size_t>(tab_index));
}

static std::int32_t GetActivePlayerGuildBankTabWithdrawLimit(lua_State* L,
                                                             int tab_index) {
  auto* session = GetWorldSession(L);
  if (!session || tab_index < 0 ||
      tab_index >= static_cast<int>(::openwow::game::GuildSystem::kGuildBankMaxTabs)) {
    return 0;
  }

  const auto* player = session->objects().GetActivePlayer();
  if (player == nullptr) {
    return 0;
  }

  return static_cast<std::int32_t>(
      ::openwow::game::GuildSystem::Get().GetControlBankTabWithdrawLimit(
          player->GetGuildRank(), static_cast<std::size_t>(tab_index)));
}

std::string GetGuildBankTabDisplayName(const ::openwow::game::GuildBankTab& tab,
                                       const int one_based_tab_index) {
  if (!tab.name.empty()) {
    return tab.name;
  }

  const auto format =
      ::openwow::game::Localization::Get().GetString("GUILDBANK_TAB_NUMBER");
  return ::openwow::game::Localization::Get().FormatString(
      format, {std::to_string(one_based_tab_index)});
}

std::string BuildGuildBankTabIconTexturePath(std::string_view icon_name) {
  const auto resolved_icon = icon_name.empty()
                                 ? std::string(kFallbackItemIconName)
                                 : std::string(icon_name);
  return std::string(kItemIconTexturePathPrefix) + resolved_icon;
}

static bool HasGuildRight(lua_State* L, std::uint32_t mask) {
  const auto rank_flags = TryGetActivePlayerGuildRankFlags(L);
  return rank_flags.has_value() && ((*rank_flags & mask) != 0);
}

static std::string SanitizeGuildControlRankName(std::string_view raw_name) {
  std::string filtered;
  filtered.reserve(std::min<std::size_t>(
      raw_name.size(), kGuildControlRankNameBufferBytes - 1));

  for (char ch : raw_name) {
    if (filtered.size() >= kGuildControlRankNameBufferBytes - 1) {
      break;
    }
    if (ch != '|') {
      filtered.push_back(ch);
    }
  }

  for (std::size_t i = 0; i < filtered.size(); ++i) {
    const char ch = filtered[i];
    if (ch == '\r' || ch == '\n') {
      filtered.resize(i);
      break;
    }
    if ((ch == '\\' || ch == '|') && i + 1 < filtered.size() &&
        filtered[i + 1] == 'n') {
      filtered.resize(i);
      break;
    }
  }

  std::size_t lead_byte_count = 0;
  for (const unsigned char byte : filtered) {
    if ((byte & 0xC0u) == 0x80u) {
      continue;
    }
    ++lead_byte_count;
    if (lead_byte_count == kGuildControlRankNameLeadByteCutoff) {
      return {};
    }
  }

  return filtered;
}

int LuaGetGuildRosterMOTD(lua_State* L) {
  const auto motd = openwow::game::GuildSystem::Get().GetGuildMOTD();
  lua_pushstring(L, motd.c_str());
  return 1;
}

int LuaGetGuildRosterShowOffline(lua_State* L) {
  lua_pushwowbool(L, GetGuildRosterShowOfflineState());
  return 1;
}

int LuaSetGuildRosterShowOffline(lua_State* L) {
  SetGuildRosterShowOfflineState(L, lua_toboolean(L, 1) != 0);
  return 0;
}

int LuaSortGuildRoster(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    luaL_error(L, "Usage: SortGuildRoster(\"type\")");
  }
  ApplyGuildRosterSort(L, lua_tostring(L, 1));
  return 0;
}

int LuaCloseGuildRoster([[maybe_unused]] lua_State* L) {
  return 0;
}

int LuaGuildPromote(lua_State* L) {
  const auto name = ReadGuildMemberNameCommandArgument(L);
  if (!name) {
    return 0;
  }

  auto* session = GetWorldSession(L);
  if (session) {
    session->interaction().SendGuildPromote(*name);
  }
  return 0;
}

int LuaGuildDemote(lua_State* L) {
  const auto name = ReadGuildMemberNameCommandArgument(L);
  if (!name) {
    return 0;
  }

  auto* session = GetWorldSession(L);
  if (session) {
    session->interaction().SendGuildDemote(*name);
  }
  return 0;
}

int LuaGuildSetLeader(lua_State* L) {
  const auto name = ReadGuildMemberNameCommandArgument(L);
  if (!name) {
    return 0;
  }

  auto* session = GetWorldSession(L);
  if (session) {
    session->interaction().SendGuildSetLeader(*name);
  }
  return 0;
}

int LuaGuildSetMOTD(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GuildSetMOTD(\"message\")");
  }

  auto* session = GetWorldSession(L);
  if (session != nullptr) {
    session->interaction().SendGuildSetMOTD(lua_tostring(L, 1));
  }
  return 0;
}

int LuaGuildUninvite(lua_State* L) {
  const auto name = ReadGuildMemberNameCommandArgument(L);
  if (!name) {
    return 0;
  }

  auto* session = GetWorldSession(L);
  if (session) {
    session->interaction().SendGuildRemove(*name);
  }
  return 0;
}

int LuaGetGuildInfoText(lua_State* L) {
  const auto info = openwow::game::GuildSystem::Get().GetGuildInfo();
  lua_pushstring(L, info.c_str());
  return 1;
}

int LuaGetNumGuildBankTabs(lua_State* L) {
  auto& gs = ::openwow::game::GuildSystem::Get();
  lua_pushnumber(L, static_cast<lua_Integer>(gs.GetNumBankTabs()));
  return 1;
}

int LuaGetCurrentGuildBankTab(lua_State* L) {
  const auto& gs = ::openwow::game::GuildSystem::Get();
  lua_pushnumber(
      L, static_cast<lua_Integer>(gs.GetCurrentGuildBankTabIndex() + 1));
  return 1;
}

int LuaSetCurrentTab(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetCurrentTab(tab)");
  }

  const int tab_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1)) - 1;
  if (tab_index >= 0 &&
      tab_index < static_cast<int>(::openwow::game::GuildSystem::kGuildBankMaxTabs)) {
    ::openwow::game::GuildSystem::Get().SetCurrentGuildBankTabIndex(
        static_cast<std::uint8_t>(tab_index));
  }
  return 0;
}

int LuaGetGuildBankTabInfo(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetGuildBankTabInfo(tab)");
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const auto requested_tab = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto zero_based_tab = requested_tab - 1;
  auto& gs = ::openwow::game::GuildSystem::Get();
  const auto* bt = zero_based_tab >= 0 &&
                           zero_based_tab <
                               static_cast<int>(::openwow::game::GuildSystem::kGuildBankMaxTabs)
                       ? gs.GetBankTab(static_cast<std::size_t>(zero_based_tab))
                       : nullptr;
  if (bt) {
    const auto flags = GetActivePlayerGuildBankTabFlags(L, zero_based_tab);
    lua_pushstring(L,
                   GetGuildBankTabDisplayName(*bt, requested_tab).c_str());
    lua_pushstring(L, BuildGuildBankTabIconTexturePath(bt->icon).c_str());
    lua_pushwowbool(L, (flags & 0x1u) != 0);
    lua_pushwowbool(L, (flags & 0x2u) != 0);
    lua_pushnumber(
        L, static_cast<lua_Number>(
               GetActivePlayerGuildBankTabWithdrawLimit(L, zero_based_tab)));
    lua_pushnumber(
        L, static_cast<lua_Number>(
               gs.GetLastGuildBankTabWithdrawalsRemaining()));
  } else {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
  }
  return 6;
}

int LuaGetGuildBankItemInfo(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetGuildBankItemInfo(tab, slot)");
  }

  const auto tab = static_cast<std::uint8_t>(
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) - 1u);
  const auto slot = SaturateLuaNumberToU32(lua_tonumber(L, 2)) - 1u;
  if (tab >= ::openwow::game::GuildSystem::kGuildBankMaxTabs ||
      slot >= ::openwow::game::GuildSystem::kGuildBankSlotsPerTab) {
    return ReturnMissingGuildBankItemInfo(L);
  }

  const auto* item = ::openwow::game::GuildSystem::Get().GetGuildBankTabItem(
      tab, static_cast<std::uint8_t>(slot));
  if (item == nullptr || item->IsEmpty()) {
    return ReturnMissingGuildBankItemInfo(L);
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return ReturnMissingGuildBankItemInfo(L);
  }

  const auto texture = TryResolveItemEntryIconTexturePath(L, item->entry);
  if (!texture.has_value()) {
    return ReturnMissingGuildBankItemInfo(L);
  }

  lua_pushstring(L, texture->c_str());
  lua_pushnumber(L, static_cast<lua_Number>(item->count));
  lua_pushwowbool(
      L, ::openwow::game::GuildSystem::Get().IsGuildBankTabItemLocked(
             tab, static_cast<std::uint8_t>(slot)));
  return 3;
}

int LuaGetGuildBankItemLink(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetGuildBankItemLink(tab, slot)");
  }

  const auto tab = static_cast<int>(lua_tonumber(L, 1));
  const auto slot = static_cast<int>(lua_tonumber(L, 2));
  const auto* item = ResolveGuildBankItem(tab, slot);
  if (item == nullptr || item->IsEmpty()) {
    return 0;
  }

  const auto* item_template = RequireItemDefinitions(L).GetItem(item->entry);
  if (item_template == nullptr) {
    return 0;
  }

  const auto display_name = ResolveDisplayNameWithRandomProperty(
      L, item_template->name, item->random_property);
  const auto link = ::openwow::game::HyperlinkParser::BuildItemLink(
      item->entry,
      display_name,
      static_cast<std::uint32_t>(item_template->quality),
      static_cast<std::int32_t>(item->GetPermanentEnchant()),
      static_cast<std::int32_t>(item->GetSocketEnchant(0)),
      static_cast<std::int32_t>(item->GetSocketEnchant(1)),
      static_cast<std::int32_t>(item->GetSocketEnchant(2)),
      item->random_property,
      static_cast<std::int32_t>(item->random_suffix));
  lua_pushstring(L, link.c_str());
  return 1;
}

int LuaGetGuildBankTabPermissions(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: GetGuildBankTabPermissions(tab)");
  }

  const int tab = static_cast<int>(lua_tonumber(L, 1)) - 1;
  const auto& control = ::openwow::game::GuildSystem::Get().GetControlState();

  std::uint32_t flags = 0;
  std::int32_t withdraw_item_limit = 0;
  if (tab >= 0 &&
      tab < static_cast<int>(control.bank_tab_flags.size())) {
    flags = control.bank_tab_flags[static_cast<std::size_t>(tab)];
    withdraw_item_limit = static_cast<std::int32_t>(
        control.bank_tab_withdraw_item_limits[static_cast<std::size_t>(tab)]);
  }

  lua_pushwowbool(L, (flags & 1u) != 0);
  lua_pushwowbool(L, (flags & 2u) != 0);
  lua_pushwowbool(L, (flags & 4u) != 0);
  lua_pushnumber(L, static_cast<lua_Number>(withdraw_item_limit));
  return 4;
}

int LuaQueryGuildBankTab(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: QueryGuildBankTab(tab)");
  }

  constexpr std::uint32_t kRetailGuildBankTabOneBasedOffset = 1u;
  const auto tab_index = static_cast<std::uint32_t>(
      TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) -
      kRetailGuildBankTabOneBasedOffset;
  if (tab_index >= ::openwow::game::GuildSystem::kGuildBankMaxTabs) {
    return 0;
  }

  auto* session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  const auto banker_guid = ::openwow::game::GuildSystem::Get().GetBankerGuid();
  if (banker_guid != 0) {
    session->interaction().SendGuildBankQueryTab(
        banker_guid, static_cast<std::uint8_t>(tab_index));
  }
  return 0;
}

namespace {

int TruncateLuaInteger(lua_State* L, int arg) {
  return static_cast<int>(lua_tonumber(L, arg));
}

std::uint8_t TruncateGuildBankTabArgumentToZeroBasedWrappedU8(lua_State* L,
                                                              int arg) {
  return static_cast<std::uint8_t>(
      TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, arg)) - 1u);
}

void FireGuildBankUpdateTextEvent(std::uint8_t tab_index) {
  ::openwow::ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ::openwow::ui::game::events::GUILDBANK_UPDATE_TEXT,
      {static_cast<int>(tab_index) + 1});
}

void PushGuildBankLogAge(lua_State* L, std::uint32_t event_unix_time) {
  const auto age_seconds = static_cast<std::uint32_t>(std::time(nullptr)) -
                           event_unix_time;
  const auto age_hours = age_seconds / 3600u;
  const auto years = age_hours / 8760u;
  const auto year_remainder = age_hours % 8760u;
  const auto months = year_remainder / 720u;
  const auto month_remainder = year_remainder % 720u;
  const auto days = month_remainder / 24u;
  const auto hours = month_remainder % 24u;

  lua_pushnumber(L, static_cast<lua_Number>(years));
  lua_pushnumber(L, static_cast<lua_Number>(months));
  lua_pushnumber(L, static_cast<lua_Number>(days));
  lua_pushnumber(L, static_cast<lua_Number>(hours));
}

const char* ResolveGuildBankMoneyTransactionType(std::int8_t entry_type) {
  switch (entry_type) {
    case 4: return "deposit";
    case 5: return "withdraw";
    case 8: return "withdrawForTab";
    case 9: return "buyTab";
    default: return "repair";
  }
}

const char* ResolveGuildBankTransactionType(std::int8_t entry_type) {
  switch (entry_type) {
    case 1: return "deposit";
    case 2: return "withdraw";
    case 3:
    case 7:
      return "move";
    default:
      return "unknown";
  }
}

void PushGuildBankLogPlayerName(lua_State* L, openwow::game::WorldSession* session,
                                std::uint64_t raw_guid) {
  if (!session) {
    lua_pushnil(L);
    return;
  }

  if (const auto* player_name = session->query_cache().GetPlayerName(raw_guid)) {
    lua_pushstring(L, player_name->name.c_str());
    return;
  }

  lua_pushnil(L);
}

const char* ResolveGuildEventLogType(std::uint8_t entry_type) {
  switch (entry_type) {
    case 1: return "invite";
    case 2: return "join";
    case 3: return "promote";
    case 4: return "demote";
    case 5: return "remove";
    case 6: return "quit";
    default:
      return "none";
  }
}

void PushGuildEventLogPlayerName(lua_State* L, openwow::game::WorldSession* session,
                                 std::uint64_t raw_guid) {
  if (!session || raw_guid == 0) {
    lua_pushnil(L);
    return;
  }

  if (const auto* player_name = session->query_cache().GetPlayerName(raw_guid)) {
    lua_pushstring(L, player_name->name.c_str());
    return;
  }

  lua_pushnil(L);
}

void PushGuildEventLogFallback(lua_State* L) {
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
}

void PushGuildBankLogItemLink(lua_State* L, openwow::game::WorldSession* session,
                              std::uint32_t item_id) {
  if (!session) {
    lua_pushstring(L, "");
    return;
  }

  if (const auto* item = session->query_cache().GetItemTemplate(item_id)) {
    const auto link = ::openwow::game::HyperlinkParser::BuildItemLink(
        item_id, item->name, static_cast<std::uint32_t>(item->quality));
    lua_pushstring(L, link.c_str());
    return;
  }

  lua_pushstring(L, "");
}

}

int LuaQueryGuildBankLog(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: QueryGuildBankLog(tab)");
  }

  auto* session = GetWorldSession(L);
  if (!session) return 0;

  const auto banker_guid = ::openwow::game::GuildSystem::Get().GetBankerGuid();
  if (banker_guid == 0) {
    return 0;
  }

  const auto tab = static_cast<int>(lua_tonumber(L, 1));
  session->interaction().SendGuildBankLogQuery(
      static_cast<std::uint8_t>(tab - 1));
  return 0;
}

int LuaQueryGuildBankText(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: QueryGuildBankLog(tab)");
  }

  const auto tab_index = TruncateGuildBankTabArgumentToZeroBasedWrappedU8(L, 1);
  auto& gs = ::openwow::game::GuildSystem::Get();
  if (!gs.BeginGuildBankTextQuery(tab_index)) {
    FireGuildBankUpdateTextEvent(tab_index);
    return 0;
  }

  auto* session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  session->interaction().SendGuildQueryBankText(tab_index);
  return 0;
}

int LuaGetNumGuildBankMoneyTransactions(lua_State* L) {
  const auto count = ::openwow::game::GuildSystem::Get()
                         .GetGuildBankMoneyLogEntryCount();
  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaGetGuildBankMoneyTransaction(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetGuildBankMoneyTransaction(index)");
  }

  const auto index = TruncateLuaInteger(L, 1);
  const auto slot = index > 0
                        ? static_cast<std::uint8_t>(index - 1)
                        : static_cast<std::uint8_t>(0xFF);
  const auto entry = ::openwow::game::GuildSystem::Get()
                         .GetGuildBankMoneyLogEntry(slot);
  auto* session = GetWorldSession(L);

  lua_pushstring(L, ResolveGuildBankMoneyTransactionType(entry.entry_type));
  PushGuildBankLogPlayerName(L, session, entry.player.GetRawValue());
  lua_pushnumber(L, static_cast<lua_Number>(entry.item_or_money));
  PushGuildBankLogAge(L, entry.event_unix_time);
  return 7;
}

int LuaGetNumGuildBankTransactions(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetNumGuildBankTransactions(tab)");
  }

  const auto tab = TruncateLuaInteger(L, 1);
  const auto page = tab > 0
                        ? static_cast<std::uint8_t>(tab - 1)
                        : static_cast<std::uint8_t>(0xFF);
  const auto count = ::openwow::game::GuildSystem::Get()
                         .GetGuildBankLogEntryCount(page);
  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaGetGuildBankTransaction(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetGuildBankTransaction(tab, index)");
  }

  auto* session = GetWorldSession(L);
  if (!session || !session->objects().GetActivePlayer()) {
    return 0;
  }

  const auto tab = TruncateLuaInteger(L, 1);
  const auto index = TruncateLuaInteger(L, 2);
  const auto page = tab > 0
                        ? static_cast<std::uint8_t>(tab - 1)
                        : static_cast<std::uint8_t>(0xFF);
  const auto slot = index > 0
                        ? static_cast<std::uint8_t>(index - 1)
                        : static_cast<std::uint8_t>(0xFF);
  const auto entry = ::openwow::game::GuildSystem::Get()
                         .GetGuildBankLogEntry(page, slot);

  lua_pushstring(L, ResolveGuildBankTransactionType(entry.entry_type));
  PushGuildBankLogPlayerName(L, session, entry.player.GetRawValue());
  PushGuildBankLogItemLink(L, session, entry.item_or_money);
  lua_pushnumber(L, static_cast<lua_Number>(entry.count));

  if (entry.entry_type == 3) {
    lua_pushnumber(L, static_cast<lua_Number>(page + 1));
    lua_pushnumber(L, static_cast<lua_Number>(entry.other_tab + 1));
  } else if (entry.entry_type == 7) {
    lua_pushnumber(L, static_cast<lua_Number>(entry.other_tab + 1));
    lua_pushnumber(L, static_cast<lua_Number>(page + 1));
  } else {
    lua_pushnil(L);
    lua_pushnil(L);
  }

  PushGuildBankLogAge(L, entry.event_unix_time);
  return 10;
}

int LuaAcceptGuild(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session || !session->objects().GetActivePlayer()) return 0;
  session->interaction().SendGuildAccept();
  return 0;
}

int LuaBuyGuildBankTab(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return 0;
  auto* player = session->objects().GetActivePlayer();
  if (!player) return 0;

  auto& guild = ::openwow::game::GuildSystem::Get();
  const auto banker_guid = guild.GetBankerGuid();
  if (!guild.IsBankFrameOpen() || banker_guid == 0) return 0;

  const auto tab_count = guild.GetNumBankTabs();
  const auto next_tab_cost = GetGuildBankTabPurchaseCost(tab_count);
  if (!next_tab_cost.has_value()) return 0;

  const auto available_money =
      static_cast<std::uint64_t>(player->GetMoney()) + guild.GetGuildBankMoney();
  if (available_money < next_tab_cost.value()) {
    DisplaySystemMessage(271);
    return 0;
  }

  session->interaction().SendGuildBankBuyTab(
      banker_guid, static_cast<std::uint8_t>(tab_count));
  return 0;
}

int LuaBuyGuildCharter(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: BuyGuildCharter(guildName)");
  }

  const auto guild_name = SafeLuaString(L, 1);
  if (::openwow::game::PetitionFrame_ValidateRename(guild_name.c_str()) != 0) {
    lua_pushnil(L);
    return 1;
  }

  if (auto* session = GetWorldSession(L)) {
    ::openwow::game::PetitionFrame_BuyGuildCharter(*session, guild_name.c_str());
  }

  lua_pushnumber(L, 1.0);
  return 1;
}

int LuaCloseGuildBankFrame([[maybe_unused]] lua_State* L) {
  SetGuildBankInteractionTarget({});
  return 0;
}

int LuaCloseGuildRegistrar(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session) {
    session->CloseGuildRegistrarInteraction();
  }
  return 0;
}

int LuaDeclineGuild(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session || !session->objects().GetActivePlayer()) return 0;
  session->interaction().SendGuildDecline();
  return 0;
}

int LuaDepositGuildBankMoney(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: DepositGuildBankMoney(money)");
  }

  auto* session = GetWorldSession(L);
  if (!session) return 0;
  auto* player = session->objects().GetActivePlayer();
  if (!player) return 0;

  const auto amount = SaturateLuaNumberToU32(lua_tonumber(L, 1));
  if (player->GetMoney() < amount) {
    DisplaySystemMessage(40);
    return 0;
  }

  const auto banker_guid = ::openwow::game::GuildSystem::Get().GetBankerGuid();
  if (banker_guid != 0) {
    session->interaction().SendGuildBankDepositMoney(banker_guid, amount);
  }
  return 0;
}

int LuaGuildRosterSetOfficerNote(lua_State* L) {
  return LuaGuildRosterSetNote(L, "Usage: GuildRosterSetOfficerNote(index, note)",
                               GuildRosterNoteKind::kOfficer);
}

int LuaGuildRosterSetPublicNote(lua_State* L) {
  return LuaGuildRosterSetNote(L, "Usage: GuildRosterSetPublicNote(index, note)",
                               GuildRosterNoteKind::kPublic);
}

int LuaQueryGuildEventLog(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return 0;

  const auto* player = session->objects().GetActivePlayer();
  if (!player || player->GetGuildID() == 0) {
    return 0;
  }

  session->interaction().SendGuildEventLogQuery();
  return 0;
}

int LuaSetGuildBankTabInfo(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return 0;

  if (!session->objects().GetActivePlayer()) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || !lua_isstring(L, 2) || !lua_isnumber(L, 3)) {
    return luaL_error(L, "Usage: SetGuildBankTabInfo(tab, name, iconIndex)");
  }

  const auto icon = ResolveGuildBankTabInfoIconPath(L);
  if (!icon) {
    return 0;
  }

  const auto zero_based_tab = static_cast<std::uint8_t>(
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) - 1u);
  const auto name = NormalizeGuildBankTabInfoName(lua_tostring(L, 2));
  const auto validation = ::openwow::game::ValidateGuildBankTabName(name);
  if (validation != ::openwow::game::NameValidationResult::kOk) {
    PetNameCache_HandlePetRenameResult(static_cast<int>(validation));
    return 0;
  }

  auto& guild = ::openwow::game::GuildSystem::Get();
  if (const auto banker_guid = guild.GetBankerGuid();
      banker_guid != 0 && zero_based_tab < guild.GetNumBankTabs()) {
    session->interaction().SendGuildBankUpdateTab(
        banker_guid, zero_based_tab, name, *icon);
  }
  return 0;
}

int LuaSetGuildBankTabPermissions(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    luaL_error(L, "Usage: SetGuildTabPermissions(tab, index, enabled)");
  }

  static constexpr std::uint32_t kTabPermissionMasks[] = {1u, 2u, 4u};

  const int tab = static_cast<int>(lua_tonumber(L, 1)) - 1;
  const int flag_index = static_cast<int>(lua_tonumber(L, 2)) - 1;
  const bool enabled = lua_toboolean(L, 3) != 0;
  const std::uint32_t mask =
      flag_index >= 0 && flag_index < 3 ? kTabPermissionMasks[flag_index] : 0u;
  if (tab >= 0) {
    ::openwow::game::GuildSystem::Get().SetControlBankTabFlagMask(
        static_cast<std::size_t>(tab), mask, enabled);
  }
  return 0;
}

int LuaSetGuildBankText(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: SetGuildBankText(tab, text)");
  }

  const auto tab_value = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto zero_based_tab = static_cast<std::uint8_t>(
      static_cast<std::uint32_t>(tab_value) - 1u);
  const auto text = NormalizeGuildBankText(lua_tostring(L, 2));

  auto* session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  auto& guild = ::openwow::game::GuildSystem::Get();
  if (const auto banker_guid = guild.GetBankerGuid();
      banker_guid != 0 && zero_based_tab < guild.GetNumBankTabs()) {
    session->interaction().SendGuildBankSetTabText(zero_based_tab, text);
  }
  return 0;
}

int LuaSetGuildBankWithdrawLimit(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: Script_SetGuildBankWithdrawLimit(amount)");
  }

  const auto limit = static_cast<std::uint32_t>(lua_tonumber(L, 1));
  ::openwow::game::GuildSystem::Get().SetControlMoneyWithdrawLimit(limit);
  return 0;
}

int LuaSetGuildRosterSelection(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: SetGuildRosterSelection(index)");
  }
  SetGuildRosterSelectionByDisplayIndex(
      L, static_cast<int>(lua_tonumber(L, 1)));
  return 0;
}

int LuaWithdrawGuildBankMoney(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: WithdrawGuildBankMoney(money)");
  }

  const auto amount = SaturateLuaNumberToU32(lua_tonumber(L, 1));
  auto* session = GetWorldSession(L);
  if (!session) return 0;

  const auto banker_guid = ::openwow::game::GuildSystem::Get().GetBankerGuid();
  if (banker_guid != 0) {
    session->interaction().SendGuildBankWithdrawMoney(banker_guid, amount);
  }
  return 0;
}

int LuaGuildInfo(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return 0;
  session->interaction().SendGuildInfo();
  return 0;
}

int LuaOfferPetition([[maybe_unused]] lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return 0;

  const auto petition_guid = session->petition().active_petition_guid();
  if (petition_guid == 0) return 0;

  const auto target_guid = session->objects().GetTargetGuid();
  if (target_guid.IsEmpty()) return 0;

  const auto* active_player = session->objects().GetActivePlayer();
  if (!active_player) return 0;

  if (!session->petition().HasActivePetitionQuery()) return 0;
  const auto& query = session->petition().last_petition_query();

  const auto* target_unit = session->objects().GetUnit(target_guid);
  if (!target_unit) {
    DisplaySystemMessage(134);
    return 0;
  }

  if (!target_unit->IsPlayer()) return 0;

  if (target_guid == session->objects().GetActivePlayerGuid()) {
    DisplaySystemMessage(348);
    return 0;
  }

  if (!active_player->Interaction().CanInteractWithFriendlyPlayerTarget(
          *target_unit)) {
    DisplaySystemMessage(118);
    return 0;
  }

  if (query.petition_type == 0) {
    const auto* target_player = session->objects().GetPlayer(target_guid);
    if (target_player && target_player->GetUInt32(PLAYER_GUILDID) != 0) {
      const auto target_name = target_unit->GetName();
      DisplaySystemMessage(89, target_name.c_str());
      return 0;
    }
  }

  const auto target_level = target_unit->State().GetLevel();
  const auto target_name = target_unit->GetName();
  if (query.allowed_min_level > 0 && target_level < query.allowed_min_level) {
    DisplaySystemMessage(541, target_name.c_str());
    return 0;
  }
  if (query.allowed_max_level > 0 && target_level > query.allowed_max_level) {
    DisplaySystemMessage(542, target_name.c_str());
    return 0;
  }

  std::uint32_t type_field = 0;
  if (query.petition_type == 1) {
    type_field = query.min_signatures + 1;
  }
  session->interaction().SendOfferPetition(type_field, petition_guid,
                                           target_guid.GetRawValue());

  DisplaySystemMessage(340, target_name.c_str());

  return 0;
}

int LuaCanEditGuildEvent(lua_State* L) {
  lua_pushwowbool(L, HasGuildRight(L, guild_rights::kLeader));
  return 1;
}

int LuaCanEditGuildInfo(lua_State* L) {
  lua_pushwowbool(L, HasGuildRight(L, guild_rights::kCanEditGuildInfo));
  return 1;
}

int LuaCanEditGuildTabInfo(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CanEditGuildTabInfo(tab)");
  }

  const auto tab_index = static_cast<int>(static_cast<std::uint8_t>(
      static_cast<std::uint32_t>(
          openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1))) -
      1u));
  constexpr std::uint32_t kGuildBankTabUpdateTextRight = 0x4u;
  if ((GetActivePlayerGuildBankTabFlags(L, tab_index) &
       kGuildBankTabUpdateTextRight) != 0) {
    lua_pushwowbool(L, true);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaCanEditMOTD(lua_State* L) {
  lua_pushwowbool(L, HasGuildRight(L, guild_rights::kCanEditMOTD));
  return 1;
}

int LuaCanEditOfficerNote(lua_State* L) {
  lua_pushwowbool(L, HasGuildRight(L, guild_rights::kCanEditOfficerNote));
  return 1;
}

int LuaCanEditPublicNote(lua_State* L) {
  lua_pushwowbool(L, HasGuildRight(L, guild_rights::kCanEditPublicNote));
  return 1;
}

int LuaCanGuildDemote(lua_State* L) {
  lua_pushwowbool(L, HasGuildRight(L, guild_rights::kCanGuildDemote));
  return 1;
}

int LuaCanGuildInvite(lua_State* L) {
  lua_pushwowbool(L, HasGuildRight(L, guild_rights::kInvite));
  return 1;
}

int LuaCanGuildPromote(lua_State* L) {
  lua_pushwowbool(L, HasGuildRight(L, guild_rights::kCanGuildPromote));
  return 1;
}

int LuaCanGuildRemove(lua_State* L) {
  lua_pushwowbool(L, HasGuildRight(L, guild_rights::kRemove));
  return 1;
}

int LuaCanViewOfficerNote(lua_State* L) {
  lua_pushwowbool(L, HasGuildRight(L, guild_rights::kCanViewOfficerNote));
  return 1;
}

int LuaCanWithdrawGuildBankMoney(lua_State* L) {
  lua_pushwowbool(L, CanActivePlayerWithdrawGuildBankMoney(L));
  return 1;
}

int LuaGetGuildBankTabCost(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session || !session->objects().GetActivePlayer()) {
    return 0;
  }

  const auto next_tab_cost =
      GetGuildBankTabPurchaseCost(::openwow::game::GuildSystem::Get().GetNumBankTabs());
  if (!next_tab_cost.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(next_tab_cost.value()));
  return 1;
}

int LuaGetGuildBankText(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetGuildBankText(tab)");
  }

  int tab = static_cast<int>(lua_tonumber(L, 1));
  if (tab < 1) {
    lua_pushnil(L);
    return 1;
  }

  auto text = ::openwow::game::GuildSystem::Get().GetGuildBankTabText(
      static_cast<std::uint8_t>(tab - 1));
  if (!text.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushstring(L, text->c_str());
  return 1;
}

int LuaGetGuildBankWithdrawLimit(lua_State* L) {
  const auto& control = ::openwow::game::GuildSystem::Get().GetControlState();
  lua_pushnumber(
      L, static_cast<lua_Number>(control.money_withdraw_limit / 10000u));
  return 1;
}

int LuaGetGuildCharterCost(lua_State* L) {
  std::uint32_t charter_cost = 0;
  if (auto* session = GetWorldSession(L)) {
    charter_cost = session->petition().guild_registrar().charter_offer.cost;
  }
  lua_pushnumber(L, static_cast<lua_Number>(charter_cost));
  return 1;
}

int LuaGetGuildEventInfo(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetGuildEventInfo(index)");
  }

  auto* session = GetWorldSession(L);
  const int index = static_cast<int>(lua_tonumber(L, 1));
  if (!session) {
    PushGuildEventLogFallback(L);
    return 8;
  }

  const auto& log = session->guild().event_log();
  if (index < 1 || index > static_cast<int>(log.size())) {
    PushGuildEventLogFallback(L);
    return 8;
  }

  const auto& entry = log[static_cast<size_t>(index - 1)];

  lua_pushstring(L, ResolveGuildEventLogType(entry.type));
  PushGuildEventLogPlayerName(L, session, entry.player.GetRawValue());
  PushGuildEventLogPlayerName(L, session, entry.other.GetRawValue());

  const auto* active_player = session->objects().GetActivePlayer();
  if (active_player != nullptr && session->guild().has_guild_info() &&
      entry.rank_id < session->guild().guild_info().rank_count) {
    lua_pushstring(L, session->guild().guild_info().rank_names[entry.rank_id].c_str());
  } else {
    lua_pushnil(L);
  }

  const auto now = std::time(nullptr);
  auto elapsed_hours = static_cast<long long>(now - entry.event_time) / 3600;
  const auto years = static_cast<int>(elapsed_hours / 8760);
  elapsed_hours %= 8760;
  const auto months = static_cast<int>(elapsed_hours / 720);
  elapsed_hours %= 720;
  const auto days = static_cast<int>(elapsed_hours / 24);
  const auto hours = static_cast<int>(elapsed_hours % 24);
  lua_pushnumber(L, static_cast<lua_Number>(years));
  lua_pushnumber(L, static_cast<lua_Number>(months));
  lua_pushnumber(L, static_cast<lua_Number>(days));
  lua_pushnumber(L, static_cast<lua_Number>(hours));
  return 8;
}

int LuaGetGuildRosterLastOnline(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: GetGuildRosterLastOnline(index)");
  }
  const int index = static_cast<int>(lua_tonumber(L, 1));
  if (!session || !session->guild().has_roster()) {
    lua_pushnil(L); lua_pushnil(L); lua_pushnil(L); lua_pushnil(L);
    return 4;
  }
  const auto* member = GetGuildRosterMemberByDisplayIndex(L, index);
  if (member == nullptr || member->status != 0) {
    lua_pushnil(L); lua_pushnil(L); lua_pushnil(L); lua_pushnil(L);
    return 4;
  }

  float total_days = member->last_save;
  int years = static_cast<int>(total_days / 365.0f);
  total_days -= years * 365.0f;
  int months = static_cast<int>(total_days / 30.0f);
  total_days -= months * 30.0f;
  int days = static_cast<int>(total_days);
  float frac = total_days - days;
  int hours = static_cast<int>(frac * 24.0f);
  lua_pushnumber(L, years);
  lua_pushnumber(L, months);
  lua_pushnumber(L, days);
  lua_pushnumber(L, hours);
  return 4;
}

int LuaGetGuildRosterSelection(lua_State* L) {
  lua_pushnumber(
      L, static_cast<lua_Number>(GetGuildRosterSelectionDisplayIndex(L)));
  return 1;
}

int LuaGetGuildTabardFileNames(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  const auto* player = session->objects().GetActivePlayer();
  if (!player) {
    return 0;
  }

  const auto player_guild_id = player->GetGuildID();
  const auto* guild_info = player_guild_id == 0
                               ? nullptr
                               : session->guild().FindCachedGuildInfo(player_guild_id);
  if (guild_info == nullptr || !openwow::game::HasResolvedGuildEmblem(guild_info->emblem)) {
    PushGuildTabardFileNameFallback(L);
    return 6;
  }

  PushGuildTabardFileNames(L, guild_info->emblem);
  return 6;
}

int LuaGetNumGuildEvents(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) { lua_pushnumber(L, 0); return 1; }
  lua_pushnumber(L, static_cast<lua_Number>(session->guild().event_log().size()));
  return 1;
}

int LuaGetTabardCreationCost(lua_State* L) {
  lua_pushnumber(L, static_cast<lua_Number>(
      openwow::game::kTabardCreationCostCopper));
  return 1;
}

int LuaGetTabardInfo(lua_State* L) {
  if (auto* session = GetWorldSession(L)) {
    ::openwow::game::PetitionFrame_RequestTabardInfo(*session);
  }
  return 0;
}

int LuaGuildControlGetRankFlags(lua_State* L) {
  const auto& control = ::openwow::game::GuildSystem::Get().GetControlState();

  for (int i = 0; i < 17; ++i) {
    lua_pushwowbool(L, (control.rights & kRankFlagMasks[i]) != 0);
  }
  return 17;
}

int LuaGuildControlSetRankFlag(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: GuildControlSetRankFlag(index, enabled)");
  }

  const int flag_index = static_cast<int>(lua_tonumber(L, 1)) - 1;
  const bool enabled = lua_toboolean(L, 2) != 0;
  if (flag_index >= 0 && flag_index < 17) {
    ::openwow::game::GuildSystem::Get().SetControlRankFlagMask(
        kRankFlagMasks[flag_index], enabled);
  }
  return 0;
}

int LuaIsGuildLeader(lua_State* L) {
  const auto* const session = GetWorldSession(L);
  const auto* const player =
      session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  lua_pushwowbool(L, player != nullptr && player->GetGuildID() != 0u &&
                         player->GetGuildRank() == 0u);
  return 1;
}

int LuaGuildControlAddRank(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    luaL_error(L, "Usage: GuildControlAddRank(name)");
  }

  const char* name = lua_tostring(L, 1);
  auto* session = GetWorldSession(L);
  if (!session || !name || !name[0]) return 0;
  if (::openwow::game::GuildSystem::Get().GetNumRanks() >= 10) return 0;

  session->interaction().SendGuildAddRank(SanitizeGuildControlRankName(name));
  return 0;
}

int LuaGuildControlDelRank(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return 0;
  if (::openwow::game::GuildSystem::Get().GetNumRanks() <= 5) return 0;

  session->interaction().SendGuildDeleteRank();
  return 0;
}

int LuaGuildControlSaveRank(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    luaL_error(L, "Usage: GuildControlSaveRank(name)");
  }

  const char* name = lua_tostring(L, 1);
  auto* session = GetWorldSession(L);
  if (!session || !name || !name[0]) return 0;

  auto& guild_system = ::openwow::game::GuildSystem::Get();
  const auto& control = guild_system.GetControlState();
  if (control.selected_rank_index < 0 ||
      static_cast<std::size_t>(control.selected_rank_index) >=
          guild_system.GetNumRanks()) {
    return 0;
  }

  const auto packet_money_limit =
      static_cast<std::uint32_t>(control.money_withdraw_limit * 10000u);
  session->interaction().SendGuildSetRank(
      SanitizeGuildControlRankName(name),
      static_cast<std::uint32_t>(control.selected_rank_index), control.rights,
      packet_money_limit, static_cast<std::uint32_t>(control.bank_tab_flags.size()),
      control.bank_tab_flags.data(),
      control.bank_tab_withdraw_item_limits.data());
  return 0;
}

int LuaAutoStoreGuildBankItem(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: AutoStoreGuildBankItem(tab, slot)");
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const auto wire_tab = static_cast<std::uint8_t>(
      TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 1)) - 1u);
  const auto wire_slot = static_cast<std::uint8_t>(
      TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 2)) - 1u);

  if (wire_tab >= ::openwow::game::GuildSystem::kGuildBankMaxTabs ||
      wire_slot >= ::openwow::game::GuildSystem::kGuildBankSlotsPerTab) {
    return 0;
  }

  if (auto* cursor = session->held_cursor(); cursor != nullptr) {
    cursor->Clear();
  }

  auto& guild = ::openwow::game::GuildSystem::Get();
  const auto banker = guild.GetBankerGuid();
  if (banker == 0) {
    return 0;
  }
  const auto* item = guild.GetGuildBankTabItemSlotState(wire_tab, wire_slot);
  if (item == nullptr) {
    return 0;
  }
  if (!guild_bank_cursor::CanActivePlayerMoveGuildBankItemsFromTab(
          *session, wire_tab)) {
    DisplaySystemMessage(guild_bank_cursor::kGuildPermissionsMessage);
    return 0;
  }

  session->interaction().SendGuildBankSwapItemsAutoStore(
      banker, wire_tab, wire_slot, item->entry, item->count);
  return 0;
}

namespace {

std::uint8_t ResolvePickupGuildBankWireIndex(lua_State* L, const int arg_index) {
  return static_cast<std::uint8_t>(
      TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, arg_index)) - 1u);
}

}

int LuaPickupGuildBankItem(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: PickupGuildBankItem(tab, slot)");
  }

  auto* session = GetWorldSession(L);
  if (!session || !session->objects().GetActivePlayer()) return 0;

  const auto wire_tab = ResolvePickupGuildBankWireIndex(L, 1);
  const auto wire_slot = ResolvePickupGuildBankWireIndex(L, 2);

  if (wire_tab >= 6 || wire_slot >= 98) return 0;

  auto& guild = ::openwow::game::GuildSystem::Get();
  const auto banker = guild.GetBankerGuid();
  auto* cursor = session->held_cursor();
  const auto* held_item =
      cursor != nullptr
          ? cursor->get_if<::openwow::game::actions::held_cursor::GuildBankItem>()
          : nullptr;
  if (held_item != nullptr) {
    const guild_bank_cursor::GuildBankHeldItemView held_guild_bank_item{
        .item_entry = held_item->item_entry,
        .linear_slot = held_item->linear_slot,
        .split_count = held_item->split_count,
    };
    if (banker == 0) {
      return 0;
    }

    const auto source_tab = static_cast<std::uint8_t>(
        held_guild_bank_item.linear_slot /
        ::openwow::game::GuildSystem::kGuildBankSlotsPerTab);
    const auto source_slot = static_cast<std::uint8_t>(
        held_guild_bank_item.linear_slot %
        ::openwow::game::GuildSystem::kGuildBankSlotsPerTab);
    const auto* destination_item = guild.GetGuildBankTabItem(wire_tab, wire_slot);
    const auto destination_item_entry =
        destination_item != nullptr && !destination_item->IsEmpty()
            ? destination_item->entry
            : 0u;
    const auto* held_item_template =
        session->query_cache().GetItemTemplate(held_guild_bank_item.item_entry);
    if ((held_item_template == nullptr || held_item_template->stackable <= 0) &&
        destination_item != nullptr &&
        destination_item->entry == held_guild_bank_item.item_entry) {
      cursor->Clear();

      const auto dest_linear =
          static_cast<std::uint32_t>(wire_tab) *
              ::openwow::game::GuildSystem::kGuildBankSlotsPerTab +
          wire_slot;
      guild_bank_cursor::ClearGuildBankItemLockAtLinearSlotAndNotify(
          destination_item->entry, dest_linear);
      return 0;
    }

    if (source_tab != wire_tab &&
        (!guild_bank_cursor::CanActivePlayerMoveGuildBankItemsFromTab(
             *session, source_tab) ||
         (destination_item_entry != 0 &&
          !guild_bank_cursor::CanActivePlayerMoveGuildBankItemsFromTab(
              *session, wire_tab)))) {
      DisplaySystemMessage(guild_bank_cursor::kGuildPermissionsMessage);
      return 0;
    }

    session->interaction().SendGuildBankSwapItemsBankToBank(
        banker, source_tab, source_slot, destination_item_entry, wire_tab,
        wire_slot, held_guild_bank_item.item_entry,
        guild_bank_cursor::ComputeGuildBankHeldItemMoveCount(
            held_guild_bank_item, held_item_template,
            destination_item));
    cursor->Clear();
    ScriptEventDispatch::Get().FireEvent(events::GUILDBANKBAGSLOTS_CHANGED);
  } else if (cursor != nullptr && cursor->live_item() != nullptr) {
    guild_bank_cursor::TryMoveCursorItemToGuildBankTab(
        *session, banker, wire_tab, wire_slot);
  } else if (const auto* item = guild.GetGuildBankTabItem(wire_tab, wire_slot);
             item != nullptr && !item->IsEmpty() &&
             !guild.IsGuildBankTabItemLocked(wire_tab, wire_slot)) {
    if (guild_bank_cursor::ResolveGuildBankItemDisplayId(
            session->item_definitions(), item->entry) == 0) {
      return 0;
    }

    guild_bank_cursor::BeginHeldGuildBankCursor(
        L, wire_tab, wire_slot, *item);
  }
  return 0;
}

int LuaPickupGuildBankMoney(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: PickupGuildBankMoney(amount)");
  }

  const auto amount = SaturateLuaNumberToU32(lua_tonumber(L, 1));
  const auto available_money =
      ::openwow::game::GuildSystem::Get().GetGuildBankMoney();
  if (amount == 0 || amount > available_money) {
    return 0;
  }

  auto* cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
  if (cursor == nullptr) {
    return 0;
  }

  cursor->Clear();
  const auto effect = ::openwow::game::commerce::ui::PickupMoneyCursor(
      *cursor,
      ::openwow::game::commerce::CopperAmount(amount),
      ::openwow::game::commerce::ui::MoneyCursorKind::kGuildBankMoney);
  if (effect == ::openwow::game::commerce::ui::
                    MoneyCursorPickupEffect::kGuildBankMoneyUpdate) {
    ::openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
        ::openwow::ui::game::events::GUILDBANK_UPDATE_MONEY);
  }
  return 0;
}

int LuaCanGuildBankRepair(lua_State* L) {
  lua_pushwowbool(L, HasGuildRight(L, guild_rights::kCanGuildBankRepair));
  return 1;
}

int LuaGetGuildBankMoney(lua_State* L) {
  auto& gs = ::openwow::game::GuildSystem::Get();
  lua_pushnumber(L, static_cast<lua_Number>(gs.GetGuildBankMoney()));
  return 1;
}

int LuaGuildControlGetNumRanks(lua_State* L) {
  auto& gs = ::openwow::game::GuildSystem::Get();
  lua_pushnumber(L, static_cast<lua_Number>(gs.GetNumRanks()));
  return 1;
}

int LuaGuildControlGetRankName(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GuildControlGetRankName(index)");
  }

  const auto zero_based_index =
      TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 1)) - 1u;
  std::string_view rank_name;

  auto* session = GetWorldSession(L);
  if (session != nullptr) {
    const auto* player = session->objects().GetActivePlayer();
    if (player != nullptr) {
      const auto player_guild_id = player->GetGuildID();
      const auto* guild_info =
          player_guild_id == 0 ? nullptr
                               : session->guild().FindCachedGuildInfo(player_guild_id);
      if (guild_info != nullptr && zero_based_index < guild_info->rank_count &&
          zero_based_index < std::size(guild_info->rank_names)) {
        rank_name = TruncateGuildQueryRankNameForLua(
            guild_info->rank_names[zero_based_index]);
      }
    }
  }

  if (rank_name.empty()) {
    lua_pushliteral(L, "");
  } else {
    lua_pushlstring(L, rank_name.data(), rank_name.size());
  }
  return 1;
}

int LuaGuildControlSetRank(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: GuildControlSetRank(rank)");
  }

  const auto rank_index = static_cast<int>(lua_tonumber(L, 1)) - 1;
  ::openwow::game::GuildSystem::Get().SetControlRankFromIndex(
      static_cast<std::uint32_t>(rank_index));
  return 0;
}

int LuaSetGuildBankTabWithdraw(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    luaL_error(L, "Usage: SetGuildBankTabWithdraw(tab, amount)");
  }

  const auto tab_index =
      static_cast<std::uint8_t>(static_cast<int>(lua_tonumber(L, 1)) - 1);
  const auto amount = static_cast<std::uint32_t>(
      static_cast<int>(lua_tonumber(L, 2)));
  ::openwow::game::GuildSystem::Get().SetControlBankTabWithdrawLimit(
      tab_index, amount);
  return 0;
}

int LuaSetGuildInfoText(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: SetGuildInfoText(text)");
  }

  const char* text = lua_tostring(L, 1);
  auto* session = GetWorldSession(L);
  if (session && text) {
    session->interaction().SendGuildInfoText(text);
  }
  return 0;
}

int LuaSplitGuildBankItem(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session || !session->objects().GetActivePlayer()) {
    return 0;
  }

  const auto tab = static_cast<int>(luaL_checknumber(L, 1));
  const auto slot = static_cast<int>(luaL_checknumber(L, 2));
  const auto amount = static_cast<int>(luaL_checknumber(L, 3));
  if (tab <= 0 || slot <= 0 || amount <= 0) {
    return 0;
  }

  const auto wire_tab = static_cast<std::uint8_t>(tab - 1);
  const auto wire_slot = static_cast<std::uint8_t>(slot - 1);
  if (wire_tab >= ::openwow::game::GuildSystem::kGuildBankMaxTabs ||
      wire_slot >= ::openwow::game::GuildSystem::kGuildBankSlotsPerTab) {
    return 0;
  }

  const auto* item =
      ::openwow::game::GuildSystem::Get().GetGuildBankTabItem(wire_tab, wire_slot);
  if (item == nullptr || item->IsEmpty() ||
      ::openwow::game::GuildSystem::Get().IsGuildBankTabItemLocked(
          wire_tab, wire_slot) ||
      amount >= static_cast<int>(item->count)) {
    return 0;
  }

  guild_bank_cursor::BeginHeldGuildBankCursor(
      L, wire_tab, wire_slot, *item, static_cast<std::uint32_t>(amount));
  return 0;
}

int LuaUnitIsInMyGuild(lua_State* L) {
  if (!lua_isstring(L, 1))
    return luaL_error(L, "Usage: UnitIsInMyGuild(\"name\")");

  auto* session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }

  const auto uid = SafeLuaString(L, 1);
  const auto guid = ResolveUnitId(session, uid);
  const auto* player = session->objects().GetLocalPlayer();
  if (!player) {
    lua_pushnil(L);
    return 1;
  }

  if (!guid.IsEmpty()) {
    const auto* target = session->objects().GetObjectByGUID(guid);
    if (target && target->IsPlayer() && player) {
      uint32_t my_guild = player->GetUInt32(PLAYER_GUILDID);
      uint32_t their_guild = target->GetUInt32(PLAYER_GUILDID);
      if (my_guild != 0 && my_guild == their_guild) {
        lua_pushnumber(L, 1.0);
        return 1;
      }
    }
  }

  const auto name = ResolveGuildRosterLookupName(*session, uid);
  if (name.has_value() && !name->empty() &&
      ::openwow::game::GuildSystem::Get().IsMemberByName(*name)) {
    lua_pushnumber(L, 1.0);
    return 1;
  }

  lua_pushnil(L);
  return 1;
}

}


#include "openwow/ui/game/api/game_lua_api_social.h"

#include "openwow/runtime/scheduling/burst_throttle.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/battlenet_api.h"
#include "openwow/game/battlenet_utf8.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/chat_manager.h"
#include "openwow/game/chat_types.h"
#include "openwow/game/group_manager.h"
#include "openwow/game/group_system.h"
#include "openwow/game/guild_manager.h"
#include "openwow/game/localization.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/player_pvp_info.h"
#include "openwow/game/social_manager.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/unit_query_bridge.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/net/client_services.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/api/game_lua_api_guild_roster_view.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/script_text_sanitize.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

static const openwow::data::dbc::DbcLoader *GetDbcLoaderFromLuaRegistry(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  auto *dbc = static_cast<const openwow::data::dbc::DbcLoader *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return dbc;
}

static std::string LookupAreaName(lua_State *L, std::uint32_t area_id) {
  if (area_id == 0)
    return "UNKNOWN";
  const auto *dbc = GetDbcLoaderFromLuaRegistry(L);
  if (dbc) {
    const auto *entry = dbc->area_table().LookupEntry(area_id);
    if (entry && !entry->name.empty())
      return std::string(entry->name);
  }
  return "UNKNOWN";
}

static const openwow::data::dbc::ChrClassesEntry *LookupClassEntry(lua_State *L,
                                                                   const std::uint8_t class_id) {
  const auto *dbc = GetDbcLoaderFromLuaRegistry(L);
  if (dbc == nullptr) {
    return nullptr;
  }
  return dbc->chr_classes().LookupEntry(class_id);
}

namespace {

constexpr std::size_t kBnConversationListLineCapacity = 0x100;
constexpr std::size_t kBnCustomMessageMaxCodepoints = 127;
constexpr std::size_t kBnFriendNoteMaxCodepoints = 255;
constexpr std::size_t kBnReportPlayerNoteMaxBytes = 509;
constexpr std::size_t kBnReportPlayerNoteMaxCodepoints = 127;
constexpr std::uint32_t kBnCustomMessageThrottleWindowMs = 3000;
constexpr int kBnCustomMessageThrottleSystemMessageId = 729;
constexpr std::size_t kUninviteReasonMaxBytes = 0x100;
constexpr int kUninviteReasonLeadByteCutoff = 64;
constexpr std::uint32_t kSpectatorUnitFlags2 = 0x00080000u;
constexpr std::uint64_t kUnresolvedUnitGuidSentinel = 0xFFFFFFFFFFFFFFFEull;
constexpr std::int32_t kBNetPresenceKeyOnline =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kOnline);
constexpr std::int32_t kBNetPresenceKeyToonName =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kToonName);
constexpr std::int32_t kBNetPresenceKeyClient =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kClient);
constexpr std::int32_t kBNetPresenceKeyRealmName =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kRealmName);
constexpr std::int32_t kBNetPresenceKeyRace =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kRace);
constexpr std::int32_t kBNetPresenceKeyClass =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kClass);
constexpr std::int32_t kBNetPresenceKeyLevel =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kLevel);
constexpr std::int32_t kBNetPresenceKeyGuild =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kGuild);
constexpr std::int32_t kBNetPresenceKeyZone =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kZone);
constexpr std::int32_t kBNetPresenceKeyCustomMessage =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kCustomMessage);
constexpr std::int32_t kBNetPresenceKeyAfk =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kAFK);
constexpr std::int32_t kBNetPresenceKeyDnd =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kDND);
constexpr std::int32_t kBNetPresenceKeyLastOnline =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kLastOnline);
constexpr std::int32_t kBNetPresenceKeyFaction =
    static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kFaction);
constexpr std::uint32_t kReferAFriendLevelCap = 60;
constexpr std::size_t kSendChatMessageExpandedCapacity = 0x100;
constexpr std::size_t kGrantLevelAvailabilityByteOffset = 4197;

constexpr std::size_t kAddFriendNameBufferCapacity = 0x131;
constexpr std::size_t kAddFriendNoteBufferCapacity = 0x200;
constexpr std::size_t kAddIgnoreNameBufferCapacity = 0x100;
constexpr std::size_t kAddMuteNameBufferCapacity = 0x131;
constexpr int kIgnoreAlreadySystemMessageId = 0x120;
constexpr int kMuteAlreadySystemMessageId = 0x239;

enum class SummonFriendEligibilityResult : std::uint32_t {
  kSuccess = 0,
  kTargetNotReferAFriendLinked = 1,
  kInvalidTarget = 8,
  kTargetNotInPartyOrRaid = 9,
  kTargetTooHighLevel = 10,
  kSummonSpellUnavailable = 11,
  kCachedGroupTargetNotReferAFriendLinked = 13,
};

struct SummonFriendTargetState {
  const openwow::game::CGPlayer_C *live_target = nullptr;
  std::optional<openwow::game::CachedPartyMemberStats> cached_member;
  bool is_active_player_or_party_member = false;
  bool is_raid_member = false;
};

bool StartsWithIgnoreCase(std::string_view value, std::string_view prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }

  for (std::size_t index = 0; index < prefix.size(); ++index) {
    const auto lhs = static_cast<unsigned char>(value[index]);
    const auto rhs = static_cast<unsigned char>(prefix[index]);
    if (std::tolower(lhs) != std::tolower(rhs)) {
      return false;
    }
  }

  return true;
}

std::string CopyRetailCStringToBuffer(const char *value, const std::size_t buffer_capacity) {

  std::string copied(value);
  if (copied.size() >= buffer_capacity) {
    copied.resize(buffer_capacity - 1);
  }
  return copied;
}

openwow::net::wotlk::WorldPacket BuildRetailAddFriendPacket(const char *name, const char *note) {

  openwow::game::TutorialSystem::Instance().FlagTutorial(0x15u);
  return openwow::net::wotlk::PacketSender::BuildAddFriend(
      CopyRetailCStringToBuffer(name, kAddFriendNameBufferCapacity),
      CopyRetailCStringToBuffer(note, kAddFriendNoteBufferCapacity));
}

struct RetailCachedContactName {
  std::string full_name;
  std::string base_name;
};

std::optional<RetailCachedContactName> ResolveRetailCachedContactName(
    const openwow::game::WorldSession &session, const openwow::game::ContactInfo &contact) {

  const auto *name_info = session.query_cache().GetPlayerName(contact.guid.GetRawValue());
  if (name_info == nullptr || name_info->name.empty()) {
    return std::nullopt;
  }
  return RetailCachedContactName{
      .full_name = name_info->realm_name.empty() ? name_info->name
                                                  : name_info->name + "-" + name_info->realm_name,
      .base_name = name_info->name,
  };
}

const openwow::game::ContactInfo *FindRetailSocialContactByName(
    const openwow::game::WorldSession &session,
    const std::vector<const openwow::game::ContactInfo *> &contacts, const char *name) {

  for (const auto *contact : contacts) {
    if (contact == nullptr || contact->guid.IsEmpty()) {
      continue;
    }

    const auto cached_name = ResolveRetailCachedContactName(session, *contact);
    if (cached_name.has_value() &&
        openwow::text::EqualsIgnoreCaseAscii(cached_name->full_name, name)) {
      return contact;
    }
  }
  return nullptr;
}

bool IsRetailIgnoredName(openwow::game::WorldSession &session, const char *name) {
  const auto *contact = FindRetailSocialContactByName(session, session.social().GetIgnored(), name);
  if (contact == nullptr) {
    return false;
  }

  const auto cached_name = ResolveRetailCachedContactName(session, *contact);
  openwow::ui::game::DisplaySystemMessage(kIgnoreAlreadySystemMessageId,
                                          cached_name->base_name.c_str());
  return true;
}

bool IsRetailMutedName(openwow::game::WorldSession &session, const char *name) {

  const auto *contact = FindRetailSocialContactByName(session, session.social().GetMuted(), name);
  if (contact == nullptr) {
    return false;
  }

  const auto cached_name = ResolveRetailCachedContactName(session, *contact);
  openwow::ui::game::DisplaySystemMessage(kMuteAlreadySystemMessageId,
                                          cached_name->base_name.c_str());
  return true;
}

std::size_t FindLegacyNameSuffixOffset(std::string_view token) {
  std::string lower(token);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  const auto target_pos = lower.find("-target");
  const auto pet_pos = lower.find("-pet");
  if (target_pos == std::string::npos) {
    return pet_pos;
  }
  if (pet_pos == std::string::npos) {
    return target_pos;
  }
  return std::min(target_pos, pet_pos);
}

std::uint64_t ResolveLegacyNpcGuid(const openwow::game::WorldSession &session) {
  if (session.gossip().has_gossip()) {
    return session.gossip().gossip().npc_guid.GetRawValue();
  }
  if (session.gossip().merchant().active()) {
    return session.gossip().merchant().snapshot().vendor_guid.GetRawValue();
  }
  if (session.gossip().has_trainer()) {
    return session.gossip().trainer().trainer_guid.GetRawValue();
  }
  if (const auto battlemaster_guid = session.battleground().GetBattlefieldListBattlemasterGuid();
      battlemaster_guid != 0) {
    return battlemaster_guid;
  }
  if (const auto gossip_guid = openwow::game::CGPlayer_C::GetGossipNpcGuid(); gossip_guid != 0) {
    return gossip_guid;
  }
  if (const auto talent_master_guid = openwow::game::CGPlayer_C::GetTalentMasterNpcGuid();
      talent_master_guid != 0) {
    return talent_master_guid;
  }
  return openwow::game::CGPlayer_C::GetBinderNpcGuid();
}

std::uint64_t ResolveLegacyQuestNpcGuid(const openwow::game::WorldSession &session) {
  if (session.quests().has_active_details()) {
    return session.quests().active_details().npc_guid.GetRawValue();
  }
  if (session.quests().has_active_reward()) {
    return session.quests().active_reward().npc_guid.GetRawValue();
  }
  if (session.quests().has_active_request()) {
    return session.quests().active_request().npc_guid.GetRawValue();
  }
  return 0;
}

struct LegacyUnitBaseResolution {
  std::uint64_t guid = 0;
  bool recognized = false;
  bool is_none_token = false;
};

LegacyUnitBaseResolution ResolveLegacyUninviteBaseToken(openwow::game::WorldSession &session,
                                                        std::string_view base_token) {
  if (StartsWithIgnoreCase(base_token, "none") && base_token.size() == 4) {
    return {.guid = std::numeric_limits<std::uint64_t>::max(),
            .recognized = true,
            .is_none_token = true};
  }

  if (StartsWithIgnoreCase(base_token, "npc") && base_token.size() == 3) {
    return {.guid = ResolveLegacyNpcGuid(session), .recognized = true};
  }

  if (StartsWithIgnoreCase(base_token, "questnpc") && base_token.size() == 8) {
    return {.guid = ResolveLegacyQuestNpcGuid(session), .recognized = true};
  }

  const auto resolved_guid = openwow::ui::game::detail::ResolveUnitId(&session, std::string(base_token)).GetRawValue();
  if (resolved_guid != 0) {
    return {.guid = resolved_guid, .recognized = true};
  }

  if (openwow::game::ParseUnitId(base_token).kind != openwow::game::UnitIdKind::kUnknown) {
    return {.guid = 0, .recognized = true};
  }

  return {};
}

bool WalkLegacyUnitSuffixChain(openwow::game::WorldSession &session,
                               std::string_view suffixes,
                               std::uint64_t *guid) {
  if (guid == nullptr) {
    return false;
  }

  auto remaining = suffixes;
  while (!remaining.empty()) {
    if (remaining.front() == '-') {
      remaining.remove_prefix(1);
    }

    if (StartsWithIgnoreCase(remaining, "target")) {
      remaining.remove_prefix(6);
      if (*guid == session.objects().GetActivePlayerGuid().GetRawValue()) {
        *guid = session.objects().GetTargetGuid().GetRawValue();
        continue;
      }

      const auto *unit = session.objects().GetUnit(openwow::game::ObjectGuid(*guid));
      *guid = unit != nullptr ? unit->GetGuidField(openwow::game::UNIT_FIELD_TARGET).GetRawValue() : 0;
      continue;
    }

    const auto *active_player = session.objects().GetLocalPlayerTyped();
    const bool pet_allowed =
        StartsWithIgnoreCase(remaining, "pet") &&
        (*guid == session.objects().GetActivePlayerGuid().GetRawValue() ||
         openwow::game::GroupSystem::Get().IsActivePlayerPartyOrRaidUnitGuid(
             session.objects(), *guid) ||
         (active_player != nullptr &&
          (active_player->State().GetUnitFlags2() & kSpectatorUnitFlags2) != 0u));
    if (pet_allowed) {
      remaining.remove_prefix(3);
      const auto *unit = session.objects().GetUnit(openwow::game::ObjectGuid(*guid));
      *guid = unit != nullptr ? unit->State().GetPrimaryControlledUnitGUID().GetRawValue() : 0;
      continue;
    }

    *guid = 0;
    return false;
  }

  return true;
}

std::string NormalizeUninviteReason(const char *raw_reason) {
  if (raw_reason == nullptr) {
    return {};
  }

  std::string sanitized;
  sanitized.reserve(kUninviteReasonMaxBytes - 1);
  for (const auto *cursor = reinterpret_cast<const unsigned char *>(raw_reason);
       *cursor != 0 && sanitized.size() + 1 < kUninviteReasonMaxBytes;
       ++cursor) {
    if (*cursor == '|') {
      continue;
    }
    sanitized.push_back(static_cast<char>(*cursor));
  }

  int lead_bytes = 0;
  for (std::size_t index = 0; index < sanitized.size(); ++index) {
    const auto ch = static_cast<unsigned char>(sanitized[index]);
    if ((ch & 0xC0u) == 0x80u) {
      continue;
    }

    ++lead_bytes;
    if (lead_bytes == kUninviteReasonLeadByteCutoff) {
      sanitized.resize(index);
      break;
    }
  }

  return sanitized;
}

bool TryResolveUninviteTargetGuid(openwow::game::WorldSession &session,
                                  const char *raw_unit_token,
                                  std::uint64_t *out_guid) {
  if (out_guid == nullptr) {
    return false;
  }

  if (raw_unit_token == nullptr || *raw_unit_token == '\0') {
    *out_guid = session.objects().GetTargetGuid().GetRawValue();
    return true;
  }

  const std::string_view unit_token(raw_unit_token);
  const auto resolved_guid = openwow::ui::game::detail::ResolveUnitId(&session, std::string(unit_token)).GetRawValue();
  if (resolved_guid != 0) {
    *out_guid = resolved_guid;
    return true;
  }

  if (StartsWithIgnoreCase(unit_token, "questnpc")) {
    auto base_guid = ResolveLegacyQuestNpcGuid(session);
    if (unit_token.size() == 8) {
      *out_guid = base_guid != 0 ? base_guid : kUnresolvedUnitGuidSentinel;
      return true;
    }

    if (!WalkLegacyUnitSuffixChain(session, unit_token.substr(8), &base_guid)) {
      return false;
    }

    *out_guid = base_guid != 0 ? base_guid : kUnresolvedUnitGuidSentinel;
    return true;
  }

  if (StartsWithIgnoreCase(unit_token, "npc")) {
    auto base_guid = ResolveLegacyNpcGuid(session);
    if (unit_token.size() == 3) {
      *out_guid = base_guid != 0 ? base_guid : kUnresolvedUnitGuidSentinel;
      return true;
    }

    if (!WalkLegacyUnitSuffixChain(session, unit_token.substr(3), &base_guid)) {
      return false;
    }

    *out_guid = base_guid != 0 ? base_guid : kUnresolvedUnitGuidSentinel;
    return true;
  }

  if (StartsWithIgnoreCase(unit_token, "none")) {
    if (unit_token.size() == 4) {
      *out_guid = std::numeric_limits<std::uint64_t>::max();
      return true;
    }

    auto base_guid = std::numeric_limits<std::uint64_t>::max();
    if (!WalkLegacyUnitSuffixChain(session, unit_token.substr(4), &base_guid)) {
      return false;
    }

    *out_guid = base_guid != 0 ? base_guid : kUnresolvedUnitGuidSentinel;
    return true;
  }

  const auto suffix_offset = FindLegacyNameSuffixOffset(unit_token);
  if (suffix_offset != std::string::npos) {
    const auto base_resolution =
        ResolveLegacyUninviteBaseToken(session, unit_token.substr(0, suffix_offset));
    if (!base_resolution.recognized) {
      return false;
    }

    auto guid = base_resolution.guid;
    if (!WalkLegacyUnitSuffixChain(session, unit_token.substr(suffix_offset), &guid)) {
      return false;
    }

    *out_guid = guid != 0 ? guid : kUnresolvedUnitGuidSentinel;
    return true;
  }

  const auto base_resolution = ResolveLegacyUninviteBaseToken(session, unit_token);
  if (!base_resolution.recognized) {
    return false;
  }

  *out_guid = base_resolution.is_none_token || base_resolution.guid != 0
                  ? base_resolution.guid
                  : kUnresolvedUnitGuidSentinel;
  return true;
}

void AppendBounded(std::array<char, kSendChatMessageExpandedCapacity> &output,
                   std::string_view text) {
  const auto existing = std::char_traits<char>::length(output.data());
  if (existing >= output.size() - 1 || text.empty()) {
    return;
  }

  const auto remaining = output.size() - existing - 1;
  const auto copy_count = std::min(text.size(), remaining);
  std::copy_n(text.data(), copy_count, output.data() + existing);
  output[existing + copy_count] = '\0';
}

std::string ResolveTrackedChatUnitName(openwow::game::WorldSession &session,
                                       const openwow::game::ObjectGuid &guid) {
  if (guid.IsEmpty()) {
    return {};
  }

  if (const auto *unit = session.objects().GetUnit(guid)) {
    std::string name = unit->GetName();

    if (name.empty() && !unit->IsPlayer()) {
      const auto entry = unit->GetEntry();
      if (entry != 0) {
        if (const auto *creature = session.query_cache().GetCreatureTemplate(entry);
            creature != nullptr) {
          name = creature->name;
        }
      }
    }

    if (name.empty() && unit->IsPlayer()) {
      if (const auto *player_name = session.query_cache().GetPlayerName(guid.GetRawValue());
          player_name != nullptr && !player_name->name.empty()) {
        name = player_name->name;
      }
      if (name.empty()) {
        name = session.objects().GetPlayerName(guid);
      }
    }

    if (name.empty()) {
      name = openwow::game::Localization::Get().GetString("UNKNOWNOBJECT");
    }
    return name;
  }

  if (const auto *player_name = session.query_cache().GetPlayerName(guid.GetRawValue());
      player_name != nullptr && !player_name->name.empty()) {
    return player_name->name;
  }

  if (const auto *entry = session.objects().GetNameEntry(guid)) {
    return entry->name;
  }

  return {};
}

std::string LocalizedChatTokenFallback(const char *key) {
  return openwow::game::Localization::Get().GetString(key, key);
}

std::string ResolveSocialContactName(openwow::game::WorldSession &session,
                                     const openwow::game::ContactInfo &contact) {
  if (!contact.display_name.empty()) {
    return contact.display_name;
  }

  if (const auto *player_name = session.query_cache().GetPlayerName(contact.guid.GetRawValue());
      player_name != nullptr && !player_name->name.empty()) {
    if (player_name->realm_name.empty()) {
      return player_name->name;
    }
    return player_name->name + "-" + player_name->realm_name;
  }

  if (const auto *entry = session.objects().GetNameEntry(contact.guid)) {
    return entry->name;
  }

  return session.objects().GetPlayerName(contact.guid);
}

const openwow::game::ContactInfo *FindVisibleSocialContactByName(
    openwow::game::WorldSession &session, const std::string &name,
    openwow::game::SocialFlag flag) {
  if (name.empty()) {
    return nullptr;
  }

  for (const auto &contact : session.social().contacts()) {
    if (!openwow::game::HasSocialFlag(contact.flags, flag)) {
      continue;
    }

    const std::string resolved_name = ResolveSocialContactName(session, contact);
    if (!resolved_name.empty() &&
        openwow::core::SStrCmpUTF8NoCase(resolved_name.c_str(), name.data(), 0x7FFFFFFF) == 0) {
      return &contact;
    }
  }

  return nullptr;
}

std::string ExpandSendChatMessageTokens(openwow::game::WorldSession &session,
                                        std::string_view message) {
  std::array<char, kSendChatMessageExpandedCapacity> expanded{};
  const auto target_guid = session.objects().GetTargetGuid();
  const auto focus_guid = session.objects().GetFocusTargetGuid();

  std::size_t cursor = 0;
  while (cursor < message.size()) {
    const auto percent = message.find('%', cursor);
    if (percent == std::string_view::npos) {
      AppendBounded(expanded, message.substr(cursor));
      break;
    }

    AppendBounded(expanded, message.substr(cursor, percent - cursor));
    if (percent + 1 >= message.size()) {
      AppendBounded(expanded, "%");
      break;
    }

    const auto token = message[percent + 1];
    switch (token) {
    case 'F':
    case 'f': {
      auto replacement = ResolveTrackedChatUnitName(session, focus_guid);
      if (replacement.empty()) {
        replacement = LocalizedChatTokenFallback("FOCUS_TOKEN_NOT_FOUND");
      }
      AppendBounded(expanded, replacement);
      cursor = percent + 2;
      break;
    }
    case 'N':
    case 'T':
    case 'n':
    case 't': {
      auto replacement = ResolveTrackedChatUnitName(session, target_guid);
      if (replacement.empty()) {
        replacement = LocalizedChatTokenFallback("TARGET_TOKEN_NOT_FOUND");
      }
      AppendBounded(expanded, replacement);
      cursor = percent + 2;
      break;
    }
    default:
      AppendBounded(expanded, "%");
      cursor = percent + 1;
      break;
    }
  }

  return expanded.data();
}

bool ValidateAndNormalizeSendChatEscapes(std::string* message) {
  if (message == nullptr) {
    return false;
  }

  auto pipe = message->find('|');
  while (pipe != std::string::npos) {
    if (pipe + 1 >= message->size()) {
      return false;
    }

    const char escape = (*message)[pipe + 1];
    if (escape == '|') {
      pipe = message->find('|', pipe + 2);
      continue;
    }
    if (escape != 'c') {
      return false;
    }

    const auto color_end = message->find("|r", pipe + 2);
    if (color_end == std::string::npos) {
      message->resize(pipe);
      return true;
    }
    pipe = message->find('|', color_end + 2);
  }

  return true;
}

std::optional<std::string> ResolveSendChatChannelTarget(
    const std::string_view target) {
  const auto channel_number =
      openwow::ui::game::detail::ParseChannelCommandIndex(target);
  if (channel_number < 1) {
    return std::nullopt;
  }

  const auto slot = static_cast<std::size_t>(channel_number - 1);
  const auto& chat_system = openwow::game::ChatSystem::Get();
  if (slot >= chat_system.GetChannelSlotCount()) {
    return std::nullopt;
  }

  const auto* channel = chat_system.GetLuaChannelBySlot(slot);
  if (channel == nullptr) {
    return std::nullopt;
  }
  return channel->name;
}

std::uint32_t GetSummonFriendSpellId() {
  return openwow::game::SpellbookSystem::Get().GetSummonFriendSpellId();
}

SummonFriendTargetState InspectSummonFriendTarget(openwow::game::WorldSession &session,
                                                  const std::uint64_t target_guid) {
  auto &group_system = openwow::game::GroupSystem::Get();
  const auto active_player_guid = session.objects().GetActivePlayerGuid().GetRawValue();

  SummonFriendTargetState state;
  state.live_target = session.objects().GetPlayer(openwow::game::ObjectGuid(target_guid));
  state.cached_member = session.party_stats().GetCachedMember(target_guid);
  state.is_active_player_or_party_member =
      target_guid == active_player_guid || group_system.FindPartySlotByGuid(target_guid) >= 0;
  state.is_raid_member =
      group_system.IsInRaid() && group_system.GetMemberByGuid(target_guid) != nullptr;
  return state;
}

bool HasLiveReferAFriendFlag(const openwow::game::CGPlayer_C &target_player) {
  return (target_player.State().GetDynamicFlags() & openwow::game::UnitDynFlag::kUnitDynFlagReferAFriend) !=
         0;
}

bool HasCachedReferAFriendFlag(const openwow::game::CachedPartyMemberStats &cached_member) {
  return (cached_member.stats.status & openwow::game::GroupMemberStatus::kReferAFriendLinked) != 0;
}

bool HasFriendListReferAFriendFlag(const openwow::game::WorldSession &session,
                                   const std::uint64_t target_guid) {
  const auto *contact = session.social().FindContact(openwow::game::ObjectGuid(target_guid));
  if (contact == nullptr ||
      !openwow::game::HasSocialFlag(contact->flags, openwow::game::SocialFlag::kFriend)) {
    return false;
  }

  return (static_cast<std::uint8_t>(contact->status) &
          static_cast<std::uint8_t>(openwow::game::FriendStatus::kRaf)) != 0;
}

bool HasReferAFriendLink(openwow::game::WorldSession &session, const std::uint64_t target_guid) {
  if (target_guid == 0) {
    return false;
  }

  if (const auto *target_player =
          session.objects().GetPlayer(openwow::game::ObjectGuid(target_guid));
      target_player != nullptr) {
    return HasLiveReferAFriendFlag(*target_player);
  }

  if (const auto cached_member = session.party_stats().GetCachedMember(target_guid);
      cached_member.has_value() && HasCachedReferAFriendFlag(*cached_member)) {
    return true;
  }

  return HasFriendListReferAFriendFlag(session, target_guid);
}

std::uint32_t ResolveFactionGroupMask(const openwow::game::WorldSession &session,
                                      const std::uint32_t faction_template_id) {
  if (faction_template_id == 0) {
    return 0;
  }

  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return 0;
  }

  const auto *entry = dbc->faction_template().LookupEntry(faction_template_id);
  return entry != nullptr ? entry->faction_group : 0;
}

bool HasGrantLevelAvailability(const openwow::game::CGPlayer_C &player) {
  constexpr auto kFieldIndex =
      static_cast<std::uint16_t>(kGrantLevelAvailabilityByteOffset / sizeof(std::uint32_t));
  constexpr auto kShift =
      static_cast<std::uint32_t>((kGrantLevelAvailabilityByteOffset % sizeof(std::uint32_t)) * 8);
  return ((player.GetUInt32(kFieldIndex) >> kShift) & 0xFFu) != 0;
}

openwow::game::GrantLevelEligibilityResult
EvaluateGrantLevelEligibility(openwow::game::WorldSession &session,
                              const std::uint64_t target_guid) {
  openwow::game::GrantLevelEligibilityParams params{};
  params.target_guid = target_guid;

  const auto *source_player = session.objects().GetActivePlayer();
  if (source_player != nullptr) {
    params.source_level = source_player->State().GetLevel();
    params.source_faction_group_mask =
        ResolveFactionGroupMask(session, source_player->State().GetFactionTemplate());
    params.has_grant_level_availability = HasGrantLevelAvailability(*source_player);
  }

  const auto *target_player = session.objects().GetPlayer(openwow::game::ObjectGuid(target_guid));
  params.target_is_player = target_player != nullptr;
  params.has_refer_a_friend_link = HasReferAFriendLink(session, target_guid);

  if (target_player != nullptr && source_player != nullptr) {
    params.target_level = target_player->State().GetLevel();
    params.target_faction_group_mask =
        ResolveFactionGroupMask(session, target_player->State().GetFactionTemplate());
    params.can_assist =
        source_player->Interaction().CanAssistSpellTarget(*target_player, false);
  }

  return openwow::game::CheckGrantLevelEligibility(params);
}

SummonFriendEligibilityResult EvaluateSummonFriendEligibility(openwow::game::WorldSession &session,
                                                              const std::uint64_t target_guid,
                                                              const std::uint32_t summon_spell_id) {
  if (target_guid == 0) {
    return SummonFriendEligibilityResult::kInvalidTarget;
  }

  const auto target_state = InspectSummonFriendTarget(session, target_guid);

  if (target_state.live_target != nullptr && !HasLiveReferAFriendFlag(*target_state.live_target)) {
    return SummonFriendEligibilityResult::kTargetNotReferAFriendLinked;
  }

  if (!target_state.is_active_player_or_party_member && !target_state.is_raid_member) {
    return SummonFriendEligibilityResult::kTargetNotInPartyOrRaid;
  }

  if (target_state.live_target != nullptr) {
    if (target_state.live_target->State().GetLevel() > kReferAFriendLevelCap) {
      return SummonFriendEligibilityResult::kTargetTooHighLevel;
    }
  } else {
    if (!target_state.cached_member.has_value()) {
      return SummonFriendEligibilityResult::kTargetNotInPartyOrRaid;
    }

    if (!HasCachedReferAFriendFlag(*target_state.cached_member)) {
      return SummonFriendEligibilityResult::kCachedGroupTargetNotReferAFriendLinked;
    }

    if (target_state.cached_member->stats.level > kReferAFriendLevelCap) {
      return SummonFriendEligibilityResult::kTargetTooHighLevel;
    }
  }

  if (summon_spell_id == 0 || session.spell_book().IsOnCooldown(summon_spell_id)) {
    return SummonFriendEligibilityResult::kSummonSpellUnavailable;
  }

  return SummonFriendEligibilityResult::kSuccess;
}

struct BNetConversationListContext {
  std::uint8_t channel_index{0};
  std::uint8_t reserved0{0};
  std::uint8_t reserved1{0};
  std::uint8_t reserved2{0};
  std::int32_t current_account_presence_id{0};
  std::uint8_t terminator{0};
};

struct BNetCustomMessageThrottleState {
  openwow::core::IdaBurstThrottle throttle;

  bool Consume(const double now_seconds) {
    return !throttle.TryConsume(
        now_seconds,
        1,
        static_cast<double>(kBnCustomMessageThrottleWindowMs) * 0.001);
  }

  void Reset() { throttle.Reset(); }
};

BNetCustomMessageThrottleState &GetBNetCustomMessageThrottleState() {
  static BNetCustomMessageThrottleState state;
  return state;
}

double GetBNetCustomMessageTickCountSeconds() {
  return openwow::core::GameClock::GetTickCountSeconds();
}

bool IsBNetRidLuaEnabled() {
  return openwow::net::ClientServices::Instance().HasBattleNetRidTransport() &&
         openwow::game::BattleNetApi::Instance().IsRIDEnabled();
}

int SetBNetBooleanPresenceValue(lua_State *L, const char *usage_error, std::int32_t presence_key) {
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (lua_type(L, 1) != LUA_TBOOLEAN) {
    return luaL_error(L, usage_error);
  }

  openwow::game::BattleNetApi::Instance().SetPresenceValue(
      presence_key, openwow::game::BNetVariant::PresenceFlag(lua_toboolean(L, 1) != 0));
  return 0;
}

int PushBNetBlockListCount(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled() || !api.IsFullyConnected()) {
    lua_pushnumber(L, 0);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(api.GetBlockList().count));
  return 1;
}

bool IsBnReportNoteTooLong(const char *note) {
  if (!note) {
    return false;
  }

  return openwow::core::SStrCountUtf8CodepointsBounded(note, kBnReportPlayerNoteMaxBytes) >
         kBnReportPlayerNoteMaxCodepoints;
}

std::size_t CountLegacyUtf8Codepoints(const char *text) {
  if (!text) {
    return 0;
  }

  std::size_t count = 0;
  for (std::size_t index = 0; text[index] != '\0'; ++index) {
    if ((static_cast<unsigned char>(text[index]) & 0xC0u) != 0x80u) {
      ++count;
    }
  }

  return count;
}

const char *GetOptionalBNetInviteNote(lua_State *L, int index,
                                      std::array<char, openwow::ui::game::detail::kBNetSanitizedChatTextBufferSize> &buffer) {
  if (!lua_isstring(L, index)) {
    buffer[0] = '\0';
    return buffer.data();
  }

  const char *const note_text = lua_tostring(L, index);
  if (CountLegacyUtf8Codepoints(note_text) > kBnFriendNoteMaxCodepoints) {
    luaL_error(L, "Invite note text too long, maximum length %d chars", 255);
  }

  if (!note_text) {
    buffer[0] = '\0';
    return buffer.data();
  }

  openwow::ui::game::detail::CopySanitizedBNetChatText(note_text, buffer.data(), buffer.size());
  return buffer.data();
}

std::uint32_t TruncateLuaNumberToBNetU32(lua_Number value) {
  return openwow::ui::SaturateLuaNumberToU32(value);
}

std::int32_t TruncateLuaNumberToBNetPresenceId(lua_Number value) {
  return openwow::ui::SignedI32FromU32Bits(TruncateLuaNumberToBNetU32(value));
}

std::uint32_t TruncateLuaNumberToBNetZeroBasedIndex(lua_Number value) {
  return TruncateLuaNumberToBNetU32(value) - 1u;
}

std::uint32_t TruncateLuaNumberMinusOneToBNetIndex(lua_Number value) {
  return TruncateLuaNumberToBNetU32(value - 1.0);
}

std::uint8_t TruncateLuaNumberToBNetChannelIndex(lua_Number value) {
  const double zero_based = static_cast<double>(value) - 1.0;
  if (!std::isfinite(zero_based) ||
      zero_based < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
      zero_based > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {

    return 0;
  }
  return static_cast<std::uint8_t>(static_cast<std::int32_t>(zero_based));
}

std::size_t TruncateLuaNumberToZeroBasedRosterIndex(lua_Number value) {
  const auto truncated_value = static_cast<std::int64_t>(value);
  return static_cast<std::size_t>(truncated_value - 1);
}

std::optional<openwow::game::GroupSystemMember> ResolveTrackedRaidRosterMemberByLuaIndex(
    lua_State *L, const int argument_index) {
  auto &group_system = openwow::game::GroupSystem::Get();
  if (!group_system.IsInRaid()) {
    return std::nullopt;
  }

  return group_system.GetMemberSnapshot(
      TruncateLuaNumberToZeroBasedRosterIndex(lua_tonumber(L, argument_index)));
}

template <typename Setter>
int SetSelectedBNetBlockListEntry(lua_State *L, const char *usage_error,
                                  const char *bounds_error, Setter set_selected_presence_id) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, usage_error);
  }

  const auto zero_based_index = TruncateLuaNumberToBNetZeroBasedIndex(lua_tonumber(L, 1));
  const auto block_list = api.GetBlockList();
  if (zero_based_index >= static_cast<std::uint32_t>(block_list.count)) {
    return luaL_error(L, bounds_error,
                      static_cast<std::int32_t>(zero_based_index + 1u), block_list.count);
  }

  const auto presence_id = block_list.entries[static_cast<std::size_t>(zero_based_index)].presence_id;

  if (presence_id == 0) {
    openwow::core::SErrFatalCondition("%s", "");
  }

  set_selected_presence_id(presence_id);
  return 0;
}

std::optional<std::int32_t> ParseBnReportPlayerReason(const char *reason_text) {
  if (openwow::core::SStrCmpNoCase(reason_text, "SPAM", 0x7fffffffu) == 0) {
    return 0;
  }
  if (openwow::core::SStrCmpNoCase(reason_text, "ABUSE", 0x7fffffffu) == 0) {
    return 1;
  }
  if (openwow::core::SStrCmpNoCase(reason_text, "THREAT", 0x7fffffffu) == 0) {
    return 2;
  }
  if (openwow::core::SStrCmpNoCase(reason_text, "NAME", 0x7fffffffu) == 0) {
    return 3;
  }
  return std::nullopt;
}

std::string CopyBNetConversationName(const char *name) {
  const std::string_view text = name ? std::string_view(name) : std::string_view();
  if (text.size() < kBnConversationListLineCapacity) {
    return std::string(text);
  }
  return std::string(text.substr(0, kBnConversationListLineCapacity - 1));
}

void PushBNetFriendLuaInfo(lua_State *L, const openwow::game::BNetFriendLuaInfo &info) {
  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(info.presence_id)));
  lua_pushstring(L, info.first_name.c_str());
  lua_pushstring(L, info.last_name.c_str());

  if (info.has_toon) {
    lua_pushstring(L, info.toon_name.c_str());
    lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(info.toon_presence_id)));
    lua_pushstring(L, info.realm_id.c_str());
  } else {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
  }

  lua_pushboolean(L, info.is_online ? 1 : 0);
  lua_pushnumber(L,
                 static_cast<lua_Number>(static_cast<std::uint32_t>(info.num_game_accounts)));
  lua_pushboolean(L, info.is_afk ? 1 : 0);
  lua_pushboolean(L, info.is_dnd ? 1 : 0);

  if (info.custom_message.has_value()) {
    lua_pushstring(L, info.custom_message->c_str());
  } else {
    lua_pushnil(L);
  }

  if (info.note_text.has_value()) {
    lua_pushstring(L, info.note_text->c_str());
  } else {
    lua_pushnil(L);
  }

  lua_pushboolean(L, info.is_friend ? 1 : 0);
  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(info.last_online)));
}

std::string EncodeBNetRealmIdForLua(std::uint32_t realm_id) {
  std::string value;
  value.reserve(4);

  for (int shift = 24; shift >= 0; shift -= 8) {
    const auto byte = static_cast<char>((realm_id >> shift) & 0xFFu);
    if (byte != '\0') {
      value.push_back(byte);
    }
  }

  return value;
}

std::string GetBNetPresenceStringValue(openwow::game::BattleNetApi &api, std::int32_t presence_id,
                                       std::int32_t key) {
  const auto value = api.GetPresenceValue(presence_id, key);

  if (value.type == openwow::game::BNetPresenceValue::Type::kString) {
    return value.str_val;
  }

  return {};
}

std::optional<std::string> GetBNetPresenceOptionalTextValue(openwow::game::BattleNetApi &api,
                                                            std::int32_t presence_id,
                                                            std::int32_t key) {
  const auto value = api.GetPresenceValue(presence_id, key);
  if (value.type == openwow::game::BNetPresenceValue::Type::kString) {
    return value.str_val;
  }

  return std::nullopt;
}

std::optional<std::int32_t> GetBNetPresenceIntValue(openwow::game::BattleNetApi &api,
                                                    std::int32_t presence_id, std::int32_t key) {
  const auto value = api.GetPresenceValue(presence_id, key);
  if (value.type == openwow::game::BNetPresenceValue::Type::kInt32) {
    return value.int_val;
  }

  return std::nullopt;
}

std::optional<std::uint8_t> GetBNetPresenceByteValue(openwow::game::BattleNetApi &api,
                                                     std::int32_t presence_id, std::int32_t key) {
  const auto value = api.GetPresenceValue(presence_id, key);
  if (value.type == openwow::game::BNetPresenceValue::Type::kBool) {
    return value.byte_val;
  }

  return std::nullopt;
}

bool GetBNetPresenceBoolValue(openwow::game::BattleNetApi &api, std::int32_t presence_id,
                              std::int32_t key) {
  const auto value = api.GetPresenceValue(presence_id, key);
  return value.type == openwow::game::BNetPresenceValue::Type::kBool && value.byte_val != 0;
}

bool GetBNetPresenceFlagValue(openwow::game::BattleNetApi &api, std::int32_t presence_id,
                              std::int32_t key) {
  const auto value = api.GetPresenceValue(presence_id, key);
  return value.type == openwow::game::BNetPresenceValue::Type::kPresenceFlag &&
         value.byte_val != 0;
}

bool IsBNetMatureLanguageFilterEnabled(
    const openwow::game::BNetVariant &value) {

  std::uint8_t low_byte = 0;
  switch (value.type) {
  case openwow::game::BNetVariantType::kBool:
    low_byte = value.bool_val ? 1u : 0u;
    break;
  case openwow::game::BNetVariantType::kInt32:
  case openwow::game::BNetVariantType::kPresenceId:
    low_byte = static_cast<std::uint8_t>(value.int_val);
    break;
  case openwow::game::BNetVariantType::kFloat64: {
    constexpr double kSignedInt64Minimum = -9223372036854775808.0;
    constexpr double kSignedInt64ExclusiveMaximum = 9223372036854775808.0;
    double truncated_value = value.float_val;
    if (!std::isfinite(truncated_value)) {
      break;
    }
    if (truncated_value >= kSignedInt64ExclusiveMaximum) {
      truncated_value -= kSignedInt64ExclusiveMaximum;
    }
    if (truncated_value < kSignedInt64Minimum ||
        truncated_value >= kSignedInt64ExclusiveMaximum) {
      break;
    }
    low_byte = static_cast<std::uint8_t>(
        static_cast<std::int64_t>(std::trunc(truncated_value)));
    break;
  }
  case openwow::game::BNetVariantType::kInlineString:
    if (value.str_ptr != nullptr) {
      for (const char *character = value.str_ptr; *character != '\0'; ++character) {
        low_byte = static_cast<std::uint8_t>(*character);
      }
    }
    break;
  case openwow::game::BNetVariantType::kStringPtr:
    if (value.str_ptr != nullptr) {
      low_byte = static_cast<std::uint8_t>(std::strtoull(value.str_ptr, nullptr, 0));
    }
    break;
  default:
    break;
  }
  return low_byte != 0;
}

int PushBNetToonLuaInfo(lua_State *L, openwow::game::BattleNetApi &api,
                        std::int32_t toon_presence_id) {
  const auto account_presence_id = api.GetAccountPresenceId(toon_presence_id);
  lua_pushboolean(L, api.GetFocusedToon(account_presence_id) == toon_presence_id ? 1 : 0);

  const auto toon_name_value = api.GetPresenceValue(toon_presence_id, kBNetPresenceKeyToonName);
  if (toon_name_value.type == openwow::game::BNetPresenceValue::Type::kToonName) {
    const char *toon_name = api.GetToonNameForPresenceId(toon_presence_id, false);
    if ((!toon_name || *toon_name == '\0') && !toon_name_value.str_val.empty()) {
      toon_name = toon_name_value.str_val.c_str();
    }
    lua_pushstring(L, toon_name ? toon_name : "");

    const auto realm_id = EncodeBNetRealmIdForLua(
        static_cast<std::uint32_t>(toon_name_value.aux_int));
    lua_pushstring(L, realm_id.c_str());
  } else {
    lua_pushstring(L, "");
    lua_pushstring(L, "");
  }

  const std::string client =
      GetBNetPresenceStringValue(api, toon_presence_id, kBNetPresenceKeyClient);
  lua_pushstring(L, client.c_str());

  const auto faction = GetBNetPresenceByteValue(api, toon_presence_id, kBNetPresenceKeyFaction);
  lua_pushnumber(L, faction.has_value() ? static_cast<lua_Number>(*faction) : -1.0);

  const std::string realm_name =
      GetBNetPresenceStringValue(api, toon_presence_id, kBNetPresenceKeyRealmName);
  lua_pushstring(L, realm_name.c_str());

  const std::string race = GetBNetPresenceStringValue(api, toon_presence_id, kBNetPresenceKeyRace);
  lua_pushstring(L, race.c_str());

  const std::string class_name =
      GetBNetPresenceStringValue(api, toon_presence_id, kBNetPresenceKeyClass);
  lua_pushstring(L, class_name.c_str());

  const std::string guild =
      GetBNetPresenceStringValue(api, toon_presence_id, kBNetPresenceKeyGuild);
  lua_pushstring(L, guild.c_str());

  const auto level = GetBNetPresenceByteValue(api, toon_presence_id, kBNetPresenceKeyLevel);
  const std::string level_text = level.has_value() ? std::to_string(*level) : std::string();
  lua_pushstring(L, level_text.c_str());

  const std::string zone = GetBNetPresenceStringValue(api, toon_presence_id, kBNetPresenceKeyZone);
  lua_pushstring(L, zone.c_str());

  const std::string custom_message =
      GetBNetPresenceStringValue(api, toon_presence_id, kBNetPresenceKeyCustomMessage);
  lua_pushstring(L, custom_message.c_str());

  const auto last_online =
      GetBNetPresenceIntValue(api, toon_presence_id, kBNetPresenceKeyLastOnline);
  lua_pushnumber(L, last_online.has_value()
                        ? static_cast<lua_Number>(static_cast<std::uint32_t>(*last_online))
                        : 0.0);

  lua_pushboolean(L,
                  GetBNetPresenceBoolValue(api, toon_presence_id, kBNetPresenceKeyOnline) ? 1 : 0);
  return 14;
}

void FlushBNetConversationListLine(const openwow::game::ObjectManager& objects,
                                   const std::string &line, std::uint8_t channel_index,
                                   std::int32_t current_account_presence_id) {

  const BNetConversationListContext context{channel_index, 0, 0, 0, current_account_presence_id, 0};
  openwow::game::ChatFrame_DisplayMessage(
      objects, line.c_str(), openwow::game::ChatDisplayType::kBnConversationList, nullptr, 0, nullptr,
      nullptr, nullptr, 0, 0, 0, 0, 0, &context);
}

}

namespace openwow::ui::game::detail {

void ResetBNetCustomMessageThrottleForTesting() {
  GetBNetCustomMessageThrottleState().Reset();
}

int LuaSendChatMessage(lua_State *L) {
  if (!GameUI_CanPerformProtectedAction(protected_action_kind::kChatMessage)) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  const auto *player = session->objects().GetLocalPlayerTyped();
  if (player == nullptr)
    return 0;

  if (!lua_isstring(L, 1)) {
    luaL_error(L, "Usage: SendChatMessage(text [,type] [,language] [,targetPlayer])");
  }
  auto message = SafeLuaString(L, 1);

  std::string msg_type_str = "SAY";
  if (lua_isstring(L, 2)) {
    msg_type_str = SafeLuaString(L, 2);
  }

  std::string lang_str;
  if (lua_isstring(L, 3)) {
    lang_str = SafeLuaString(L, 3);
  }

  auto target = SafeLuaString(L, 4);

  using namespace openwow::game;

  ChatMsg type = ChatMsg::kSay;
  if (lua_isstring(L, 2) && !ChatTypeStringToID(msg_type_str.c_str(), &type)) {
    return luaL_error(L, "SendChatMessage(): Unknown chat type");
  }

  if (message.empty() && type != ChatMsg::kDnd && type != ChatMsg::kAfk) {
    return 0;
  }

  if (!openwow::ui::ValidateUtf8String(message.c_str())) {
    return luaL_error(L, "SendChatMessage(): Chat message must be UTF-8 text");
  }

  std::uint32_t default_language_id = 0;
  {
    const auto *default_language_dbc = session->GetDbcLoader();
    if (default_language_dbc == nullptr) {
      default_language_dbc = GetDbcLoader(L);
    }
    if (default_language_dbc != nullptr) {
      default_language_id =
          ::openwow::game::ResolveDefaultChatLanguageId(*player, *default_language_dbc);
    }
  }

  Language language = static_cast<Language>(default_language_id);
  if (!lang_str.empty()) {
    const auto *dbc = session->GetDbcLoader();
    if (dbc == nullptr) {
      dbc = GetDbcLoader(L);
    }
    if (dbc == nullptr) {
      return luaL_error(L, "SendChatMessage(): Unknown language");
    }

    const auto requested_language = ::openwow::game::FindChatLanguageByName(*dbc, lang_str);
    if (!requested_language.has_value()) {
      return luaL_error(L, "SendChatMessage(): Unknown language");
    }

    if (::openwow::game::GetChatLanguageComprehensionValue(*player, *dbc, requested_language->id) ==
        0) {
      ::openwow::ui::game::DisplaySystemMessage(548);
      return 0;
    }

    language = static_cast<Language>(requested_language->id);
  }

  if (type == ChatMsg::kWhisper && target.empty()) {
    return luaL_error(L, "SendChatMessage(): Whisper message missing target player!");
  }

  if (type == ChatMsg::kChannel && target.empty()) {
    return luaL_error(L, "SendChatMessage(): Channel send missing channel number");
  }

  const auto& group = session->group();
  const auto local_guid = session->objects().GetLocalPlayerGuid();

  if (type == ChatMsg::kParty && !group.IsInGroup() && !group.IsBattlegroundGroup()) {
    ::openwow::ui::game::DisplaySystemMessage(80);
    return 0;
  }

  if (type == ChatMsg::kRaidWarning) {
    if (!group.IsInGroup()) {
      ::openwow::ui::game::DisplaySystemMessage(80);
      return 0;
    }

    const bool is_leader = !local_guid.IsEmpty() && local_guid == group.leader_guid();
    const bool is_assistant =
        (group.my_flags() &
         static_cast<std::uint8_t>(::openwow::game::GroupMemberFlag::kAssistant)) != 0;
    if (!is_leader && !is_assistant) {
      ::openwow::ui::game::DisplaySystemMessage(84);
      return 0;
    }
  }

  if (type == ChatMsg::kRaid && !group.IsRaid()) {
    ::openwow::ui::game::DisplaySystemMessage(445);
    return 0;
  }

  if (type == ChatMsg::kBattleground && !group.IsBattlegroundGroup()) {
    ::openwow::ui::game::DisplaySystemMessage(551);
    return 0;
  }

  if (type == ChatMsg::kParty || type == ChatMsg::kRaid || type == ChatMsg::kBattleground) {
    const auto &leader = group.leader_guid();
    if (!local_guid.IsEmpty() && local_guid == leader) {
      if (type == ChatMsg::kParty)
        type = ChatMsg::kPartyLeader;
      else if (type == ChatMsg::kRaid)
        type = ChatMsg::kRaidLeader;
      else
        type = ChatMsg::kBattlegroundLeader;
    }
  }

  if (type == ChatMsg::kChannel) {
    const auto channel_name = ResolveSendChatChannelTarget(target);
    if (!channel_name.has_value()) {
      return 0;
    }
    target = *channel_name;
  }

  std::string outgoing_message = message;
  if (type != ChatMsg::kAfk) {
    outgoing_message = ExpandSendChatMessageTokens(*session, outgoing_message);
  }

  if (type != ChatMsg::kAfk && player->HasActiveInebriation()) {
    std::array<char, kSendChatMessageExpandedCapacity> filtered{};
    if (openwow::game::SpellTextFormatter::ApplyDrunkSpeechFilter(
            session->sound_runtime(),
            outgoing_message.c_str(), filtered.data(), static_cast<std::uint32_t>(filtered.size()),
            player->GetNormalizedInebriation()) != 0) {
      outgoing_message.assign(filtered.data());
    }
  }

  if (type != ChatMsg::kAfk) {
    openwow::ui::TruncateAtNewlineOrPipeN(outgoing_message.data());
    outgoing_message.resize(std::char_traits<char>::length(outgoing_message.c_str()));
    if (!ValidateAndNormalizeSendChatEscapes(&outgoing_message)) {
      return luaL_error(L, "SendChatMessage(): Invalid escape code in chat message");
    }
  }

  session->chat_sender().SendTyped(type, language, target, outgoing_message);
  return 0;
}

int LuaGetDefaultLanguage(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  const auto *player = session->objects().GetActivePlayer();
  if (!player)
    return 0;

  const auto *dbc = session->GetDbcLoader();
  if (dbc == nullptr) {
    dbc = GetDbcLoader(L);
  }
  if (dbc == nullptr) {
    return 0;
  }

  const auto default_language_id =
      ::openwow::game::ResolveDefaultChatLanguageId(*player, *dbc);
  const auto default_language =
      ::openwow::game::FindChatLanguageById(*dbc, default_language_id);
  if (!default_language.has_value()) {
    return 0;
  }

  lua_pushlstring(L, default_language->name.data(),
                  static_cast<size_t>(default_language->name.size()));
  return 1;
}

int LuaGetNumFriends(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
  }
  auto friends = session->social().GetFriends();
  int online_count = 0;
  for (const auto *ci : friends) {
    if (ci->status != ::openwow::game::FriendStatus::kOffline)
      ++online_count;
  }
  lua_pushnumber(L, static_cast<lua_Number>(friends.size()));
  lua_pushnumber(L, static_cast<lua_Number>(online_count));
  return 2;
}

int LuaGetFriendInfo(lua_State *L) {
  if (!lua_isnumber(L, 1) && !lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GetFriendInfo(index or name)");
  }
  auto *session = GetWorldSession(L);

  if (!session) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushstring(L, "UNKNOWN");
    lua_pushstring(L, "UNKNOWN");
    lua_pushnil(L);
    lua_pushstring(L, "");
    lua_pushnil(L);
    lua_pushnil(L);
    return 8;
  }

  auto friends = session->social().GetFriends();
  const auto *info = [&]() -> const ::openwow::game::ContactInfo * {
    if (lua_isnumber(L, 1)) {
      int index = static_cast<int>(lua_tonumber(L, 1));
      if (index < 1 || index > static_cast<int>(friends.size()))
        return nullptr;
      return friends[static_cast<std::size_t>(index - 1)];
    } else if (lua_isstring(L, 1)) {

      const char *name_arg = lua_tostring(L, 1);
      if (!name_arg)
        return nullptr;
      return FindVisibleSocialContactByName(
          *session, name_arg, ::openwow::game::SocialFlag::kFriend);
    }
    return nullptr;
  }();

  if (!info) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushstring(L, "UNKNOWN");
    lua_pushstring(L, "UNKNOWN");
    lua_pushnil(L);
    lua_pushstring(L, "");
    lua_pushnil(L);
    lua_pushnil(L);
    return 8;
  }

  auto st = static_cast<std::uint8_t>(info->status);
  bool online = st != 0;

  std::string friend_name = ResolveSocialContactName(*session, *info);
  lua_pushstring(L, friend_name.c_str());

  lua_pushnumber(L, static_cast<lua_Number>(info->level));

  const auto *name_info = session->query_cache().GetPlayerName(info->guid.GetRawValue());
  const std::string_view class_name =
      name_info != nullptr
          ? LookupClassDisplayName(L, static_cast<std::uint8_t>(info->player_class),
                                   name_info->sex)
          : LookupClassBaseName(L, static_cast<std::uint8_t>(info->player_class));
  if (!class_name.empty()) {
    lua_pushlstring(L, class_name.data(), class_name.size());
  } else {
    lua_pushstring(L, "UNKNOWN");
  }

  std::string area_name = LookupAreaName(L, info->area);
  lua_pushstring(L, area_name.c_str());

  if (online)
    lua_pushnumber(L, 1.0);
  else
    lua_pushnil(L);

  if (st & 0x04)
    lua_pushstring(L, "CHAT_FLAG_DND");
  else if (st & 0x02)
    lua_pushstring(L, "CHAT_FLAG_AFK");
  else
    lua_pushstring(L, "");

  if (!info->note.empty())
    lua_pushstring(L, info->note.c_str());
  else
    lua_pushnil(L);

  if (st & 0x08)
    lua_pushnumber(L, 1.0);
  else
    lua_pushnil(L);

  return 8;
}

int LuaAddFriend(lua_State *L) {

  const char *name = lua_tostring(L, 1);
  if (name == nullptr) {
    return 0;
  }

  const char *note = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";

  const auto packet = BuildRetailAddFriendPacket(name, note);

  (void)openwow::net::ClientServices__SendPacket(packet);
  return 0;
}

int LuaRemoveFriend(lua_State *L) {
  if (!lua_isnumber(L, 1) && !lua_isstring(L, 1)) {
    lua_pushliteral(L, "Usage: RemoveFriend([\"name\"] or [index])");
    return lua_error(L);
  }
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  auto friends = session->social().GetFriends();
  if (lua_isnumber(L, 1)) {
    int idx = static_cast<int>(lua_tonumber(L, 1));
    if (idx >= 1 && idx <= static_cast<int>(friends.size())) {
      session->interaction().SendDelFriend(
          friends[static_cast<std::size_t>(idx - 1)]->guid.GetRawValue());
    }
  } else if (lua_isstring(L, 1)) {
    session->DeleteFriendContactByName(SafeLuaString(L, 1));
  }
  return 0;
}

int LuaSetSelectedFriend(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetSelectedFriend(index)");
  }
  if (auto *session = GetWorldSession(L); session != nullptr) {
    session->social().SelectFriendByLuaIndex(
        openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)));
  }
  return 0;
}

int LuaGetSelectedFriend(lua_State *L) {
  const auto *session = GetWorldSession(L);
  lua_pushnumber(L, static_cast<lua_Number>(
                        session != nullptr
                            ? session->social().GetSelectedFriendLuaIndex()
                            : 0));
  return 1;
}

int LuaGetNumIgnores(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    return 1;
  }
  auto ignored = session->social().GetIgnored();
  lua_pushnumber(L, static_cast<lua_Number>(ignored.size()));
  return 1;
}

int LuaGetIgnoreName(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetIgnoreName(index)");
  }
  auto *session = GetWorldSession(L);
  int index = static_cast<int>(lua_tonumber(L, 1));

  if (!session) {
    lua_pushstring(L, "UNKNOWN");
    return 1;
  }

  auto ignored = session->social().GetIgnored();
  if (index < 1 || index > static_cast<int>(ignored.size())) {
    lua_pushstring(L, "UNKNOWN");
    return 1;
  }

  const auto *info = ignored[static_cast<std::size_t>(index - 1)];
  std::string name = ResolveSocialContactName(*session, *info);
  if (name.empty()) {
    lua_pushstring(L, "UNKNOWN");
  } else {
    lua_pushstring(L, name.c_str());
  }
  return 1;
}

int LuaAddIgnore(lua_State *L) {

  const char *name = lua_tostring(L, 1);
  if (name == nullptr) {
    return 0;
  }

  auto *session = GetWorldSession(L);

  if (session == nullptr || IsRetailIgnoredName(*session, name)) {
    return 0;
  }

  const auto packet = openwow::net::wotlk::PacketSender::BuildAddIgnore(
      CopyRetailCStringToBuffer(name, kAddIgnoreNameBufferCapacity));
  (void)openwow::net::ClientServices__SendPacket(packet);
  return 0;
}

int LuaDelIgnore(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  auto name = SafeLuaString(L, 1);
  if (name.empty())
    return 0;
  session->DeleteIgnoredContactByName(name);
  return 0;
}

int LuaGetGuildRosterInfo(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: GetGuildRosterInfo(index)");
  }
  const int index = static_cast<int>(lua_tonumber(L, 1));

  if (!session || !session->guild().has_roster()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushstring(L, "");
    lua_pushnil(L);
    return 11;
  }

  const auto *member = GetGuildRosterMemberByDisplayIndex(L, index);
  if (member == nullptr) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushstring(L, "");
    lua_pushnil(L);
    return 11;
  }

  lua_pushstring(L, member->name.c_str());

  std::string rank_name;
  if (session->guild().has_guild_info()) {
    const auto &gi = session->guild().guild_info();
    if (member->rank_id >= 0 && static_cast<std::uint32_t>(member->rank_id) < gi.rank_count) {
      rank_name = gi.rank_names[member->rank_id];
    }
  }
  if (rank_name.empty()) {
    rank_name = std::to_string(member->rank_id);
  }
  lua_pushstring(L, rank_name.c_str());

  lua_pushnumber(L, static_cast<lua_Number>(member->rank_id));

  lua_pushnumber(L, static_cast<lua_Number>(member->level));

  if (const auto *class_entry = LookupClassEntry(L, static_cast<std::uint8_t>(member->class_id));
      class_entry != nullptr) {
    const auto display_name = class_entry->DisplayNameForSex(member->gender);
    lua_pushstring(L, std::string(display_name).c_str());
  } else {
    lua_pushstring(L, ClassName(static_cast<std::uint8_t>(member->class_id)));
  }

  std::string zone_text = LookupAreaName(L, member->area_id);
  if (zone_text == "UNKNOWN" && member->area_id == 0)
    lua_pushnil(L);
  else
    lua_pushstring(L, zone_text.c_str());

  lua_pushstring(L, member->note.c_str());

  lua_pushstring(L, member->officer_note.c_str());

  if (member->status != 0)
    lua_pushnumber(L, 1.0);
  else
    lua_pushnil(L);

  if (member->status & 4)
    lua_pushstring(L, "DND");
  else if (member->status & 2)
    lua_pushstring(L, "AFK");
  else
    lua_pushstring(L, "");

  std::string class_file;
  if (const auto *class_entry = LookupClassEntry(L, static_cast<std::uint8_t>(member->class_id));
      class_entry != nullptr && !class_entry->client_file_string.empty()) {
    class_file.assign(class_entry->client_file_string);
  } else {
    class_file = ClassName(static_cast<std::uint8_t>(member->class_id));
    for (auto &c : class_file)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    class_file.erase(std::remove(class_file.begin(), class_file.end(), ' '), class_file.end());
  }
  lua_pushstring(L, class_file.c_str());
  return 11;
}

int LuaGetNumGuildMembers(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !session->guild().has_roster()) {
    lua_pushnumber(L, 0);
    return 1;
  }
  const bool force_total = luaL_optinteger(L, 1, 0) != 0;
  const int total = GetGuildRosterTotalMemberCount(L);
  const int visible = GetGuildRosterVisibleMemberCount(L);
  lua_pushnumber(L, static_cast<lua_Number>(
                        GetGuildRosterShowOfflineState() || force_total ? total : visible));
  return 1;
}

int LuaGuildRoster(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  session->interaction().SendGuildRoster();
  return 0;
}

int LuaGetGuildInfo(lua_State *L) {
  if (!lua_isstring(L, 1))
    return luaL_error(L, "Usage: GetGuildInfo(\"unit\")");

  auto *session = GetWorldSession(L);
  if (session != nullptr) {
    const auto unit_id = SafeLuaString(L, 1);
    const auto guid = ResolveUnitId(session, unit_id);
    if (!guid.IsEmpty()) {
      const auto *obj = session->objects().Get(guid);
      if (obj != nullptr && obj->IsPlayer()) {
        const auto *player =
            static_cast<const openwow::game::CGPlayer_C *>(obj);
        const auto guild_id = player->GetGuildID();
        const auto rank_idx = player->GetGuildRank();
        if (guild_id != 0) {
          const auto *guild_info =
              session->guild().FindCachedGuildInfo(guild_id);
          if (guild_info != nullptr) {
            lua_pushstring(L, guild_info->name.c_str());
            std::string rank_name;
            if (rank_idx < guild_info->rank_count &&
                rank_idx < std::size(guild_info->rank_names)) {
              rank_name = guild_info->rank_names[rank_idx];
            }
            lua_pushstring(L, rank_name.c_str());
            lua_pushnumber(L, static_cast<lua_Number>(rank_idx));
            return 3;
          }
        }
      }
    }
  }

  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  return 3;
}

int LuaIsInGuild(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const auto *player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  lua_pushwowbool(L, player != nullptr && player->GetGuildID() != 0);
  return 1;
}

int LuaGuildInvite(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  auto name = SafeLuaString(L, 1);
  if (!name.empty())
    session->interaction().SendGuildInvite(name);
  return 0;
}

int LuaGuildLeave(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  session->interaction().SendGuildLeave();
  return 0;
}

int LuaGuildDisband(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  session->interaction().SendGuildDisband();
  return 0;
}

int LuaInviteUnit(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  if (session->objects().GetActivePlayer() == nullptr)
    return 0;

  const char *name = lua_tostring(L, 1);
  if (!name)
    return 0;
  if (openwow::core::SStrLen(name) > 0x30u)
    return luaL_error(L, "Name too long");

  std::uint32_t role_flags = 0;
  if (ScriptReadBoolArgOrDefault(L, 2, false))
    role_flags |= 0x2u;
  if (ScriptReadBoolArgOrDefault(L, 3, false))
    role_flags |= 0x4u;
  if (ScriptReadBoolArgOrDefault(L, 4, false))
    role_flags |= 0x8u;

  session->interaction().SendGroupInvite(name, role_flags);
  return 0;
}

int LuaUninviteUnit(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  if (!GameUI_CanPerformHardwareEventAction()) {
    return 0;
  }

  std::uint64_t target_guid = 0;
  if (!TryResolveUninviteTargetGuid(*session, lua_tostring(L, 1), &target_guid)) {
    return 0;
  }

  session->interaction().SendGroupUninviteByGuid(
      target_guid, NormalizeUninviteReason(lua_tostring(L, 2)));
  return 0;
}

int LuaAcceptGroup(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  std::uint32_t role_flags = 0;
  if (ScriptReadBoolArgOrDefault(L, 1, false))
    role_flags |= 0x2u;
  if (ScriptReadBoolArgOrDefault(L, 2, false))
    role_flags |= 0x4u;
  if (ScriptReadBoolArgOrDefault(L, 3, false))
    role_flags |= 0x8u;

  session->interaction().SendGroupAccept(role_flags);
  return 0;
}

int LuaDeclineGroup(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  session->interaction().SendGroupDecline();
  return 0;
}

int LuaLeaveParty(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  session->interaction().SendGroupDisband();
  return 0;
}

int LuaGetNumPartyMembers(lua_State *L) {
  const auto &group_system = ::openwow::game::GroupSystem::Get();
  lua_pushnumber(L, static_cast<lua_Number>(group_system.GetTrackedPartyMemberCount()));
  return 1;
}

int LuaGetNumRaidMembers(lua_State *L) {
  auto &group_system = ::openwow::game::GroupSystem::Get();
  if (!group_system.IsInRaid()) {
    lua_pushnumber(L, 0);
    return 1;
  }

  std::size_t roster_count = group_system.GetNumGroupMembers();
  const auto local_player_guid = group_system.GetLocalPlayerGuid().GetRawValue();
  if (local_player_guid != 0 && group_system.GetMemberByGuid(local_player_guid) == nullptr) {
    ++roster_count;
  }
  lua_pushnumber(L, static_cast<lua_Number>(roster_count));
  return 1;
}

int LuaGetPartyLeaderIndex(lua_State *L) {
  const auto tracked_leader_index =
      ::openwow::game::GroupSystem::Get().GetTrackedPartyLeaderIndex();
  lua_pushnumber(L, static_cast<lua_Number>(tracked_leader_index));
  return 1;
}

static const char *ClassFileName(uint8_t class_id) {
  switch (class_id) {
  case 1:
    return "WARRIOR";
  case 2:
    return "PALADIN";
  case 3:
    return "HUNTER";
  case 4:
    return "ROGUE";
  case 5:
    return "PRIEST";
  case 6:
    return "DEATHKNIGHT";
  case 7:
    return "SHAMAN";
  case 8:
    return "MAGE";
  case 9:
    return "WARLOCK";
  case 11:
    return "DRUID";
  default:
    return "WARRIOR";
  }
}

int LuaGetRaidRosterInfo(lua_State *L) {
  auto *session = GetWorldSession(L);
  int index = static_cast<int>(lua_tonumber(L, 1));
  auto &gs = ::openwow::game::GroupSystem::Get();

  auto push_nil_path = [&]() -> int {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 1);
    lua_pushnumber(L, 1);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 14;
  };

  if (!gs.IsInRaid()) {
    return push_nil_path();
  }

  if (index < 1) {
    return push_nil_path();
  }

  const auto member = gs.GetMemberSnapshot(static_cast<std::size_t>(index - 1));
  if (!member.has_value()) {
    return push_nil_path();
  }

  lua_pushstring(L, member->name.c_str());

  int rank = 0;
  if (member->guid == gs.GetLeaderGuid()) {
    rank = 2;
  } else if (member->flags & static_cast<uint8_t>(::openwow::game::GroupMemberFlag::kAssistant)) {
    rank = 1;
  }
  lua_pushnumber(L, static_cast<lua_Number>(rank));

  lua_pushnumber(L, static_cast<lua_Number>(member->group_index + 1));

  uint32_t level = 0;
  uint8_t cls = member->class_id;
  const auto raw_guid = member->guid;
  const auto *obj = session != nullptr ? session->objects().Get(ObjectGuid(raw_guid)) : nullptr;
  if (obj) {
    level = obj->GetUInt32(UNIT_FIELD_LEVEL);
    if (const auto live_class = GetUnitClass(obj); live_class != 0) {
      cls = live_class;
    }
  }
  if (cls == 0 && session != nullptr) {
    if (const auto *name_info = session->query_cache().GetPlayerName(raw_guid);
        name_info != nullptr && name_info->class_id != 0) {
      cls = name_info->class_id;
    } else if (const auto *name_entry = session->objects().GetNameEntry(ObjectGuid(raw_guid));
               name_entry != nullptr && name_entry->class_id != 0) {
      cls = name_entry->class_id;
    }
  }
  lua_pushnumber(L, static_cast<lua_Number>(level));

  lua_pushstring(L, ClassName(cls));

  lua_pushstring(L, ClassFileName(cls));

  const auto online_status =
      member->online_status != 0 ? member->online_status : (member->is_online ? 0x01u : 0u);
  const bool is_online =
      (online_status & static_cast<uint8_t>(::openwow::game::GroupMemberOnline::kOnline)) != 0;
  if (!is_online) {
    lua_pushstring(L, "Offline");
  } else {
    lua_pushstring(L, "");
  }

  lua_pushwowbool(L, is_online);

  if (obj) {
    lua_pushwowbool(L, obj->GetHealth() <= 0);
  } else {
    const bool is_dead =
        is_online &&
        (online_status & static_cast<uint8_t>(::openwow::game::GroupMemberOnline::kDead)) != 0;
    lua_pushwowbool(L, is_dead);
  }

  if (member->flags & static_cast<uint8_t>(::openwow::game::GroupMemberFlag::kMainTank)) {
    lua_pushstring(L, "MAINTANK");
  } else if (member->flags & static_cast<uint8_t>(::openwow::game::GroupMemberFlag::kMainAssist)) {
    lua_pushstring(L, "MAINASSIST");
  } else {
    lua_pushnil(L);

  }

  constexpr uint32_t kMasterLoot = 2;
  lua_pushwowbool(L, (gs.GetLootMethod() == kMasterLoot && raw_guid == gs.GetMasterLooter()));

  lua_pushwowbool(L, (member->role & 0x02u) != 0);

  lua_pushwowbool(L, (member->role & 0x04u) != 0);

  lua_pushwowbool(L, (member->role & 0x08u) != 0);

  return 14;
}

int LuaSendWho(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  auto filter = SafeLuaString(L, 1);
  session->interaction().SendWho(filter);
  return 0;
}

int LuaSetWhoToUI(lua_State *L) {
  if (auto *session = GetWorldSession(L); session != nullptr) {
    session->social().SetWhoResultsToUi(lua_toboolean(L, 1) != 0);
  }
  return 0;
}

int LuaGetNumWhoResults(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
  }
  const auto &wl = session->misc().who_list();
  lua_pushnumber(L, static_cast<lua_Integer>(wl.display_count));
  lua_pushnumber(L, static_cast<lua_Integer>(wl.match_count));
  return 2;
}

int LuaGetWhoInfo(lua_State *L) {
  if (!lua_isnumber(L, 1))
    return luaL_error(L, "Usage: GetWhoInfo(index)");

  const auto index =
      static_cast<std::size_t>(static_cast<std::int64_t>(lua_tonumber(L, 1))) - 1;

  auto *session = GetWorldSession(L);
  const openwow::game::WhoListInfo *wl =
      session ? &session->misc().who_list() : nullptr;

  if (!wl || index >= wl->entries.size()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 7;
  }

  const auto &entry = wl->entries[index];
  const auto class_id = static_cast<std::uint8_t>(entry.class_id);
  const auto race_id = static_cast<std::uint8_t>(entry.race_id);
  const auto gender = entry.gender;

  lua_pushstring(L, entry.name.c_str());

  lua_pushstring(L, entry.guild_name.c_str());

  lua_pushnumber(L, static_cast<lua_Number>(entry.level));

  const std::string_view race_display = LookupRaceDisplayName(L, race_id, gender);
  if (!race_display.empty()) {
    lua_pushlstring(L, race_display.data(), race_display.size());
  } else {
    lua_pushstring(L, "UNKNOWN");
  }

  const std::string_view class_display = LookupClassDisplayName(L, class_id, gender);
  if (!class_display.empty()) {
    lua_pushlstring(L, class_display.data(), class_display.size());
  } else {
    lua_pushstring(L, "UNKNOWN");
  }

  const std::string zone_name = LookupAreaName(L, entry.zone_id);
  lua_pushstring(L, zone_name.c_str());

  const auto *class_entry = LookupChrClassEntry(L, class_id);
  if (class_entry && !class_entry->client_file_string.empty()) {
    lua_pushlstring(L, class_entry->client_file_string.data(),
                    class_entry->client_file_string.size());
  } else {
    lua_pushnil(L);
  }

  return 7;
}

int LuaSortWho(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: SortWho(\"type\")");
  }
  auto sort_type = SafeLuaString(L, 1);
  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  session->misc().UpdateWhoSortOrder(sort_type);
  session->misc().SortWhoResults(session->GetDbcLoader());
  openwow::ui::game::ScriptEventDispatch::Get().FireWhoListUpdate();

  return 0;
}

int LuaSetRaidSubgroup(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: SetRaidSubgroup(index, subgroup)");
  }

  if (!GameUI_CanPerformProtectedAction(protected_action_kind::kRaidSubgroup)) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  const auto sub_group = static_cast<std::int64_t>(lua_tonumber(L, 2)) - 1;
  if (sub_group < 0 || sub_group >= 8) {
    return luaL_error(L, "Usage: SetRaidSubgroup(index, subgroup)");
  }

  const auto member = ResolveTrackedRaidRosterMemberByLuaIndex(L, 1);
  if (!member.has_value() || member->group_index == sub_group || member->name.empty()) {
    return 0;
  }

  session->interaction().SendGroupChangeSubGroup(member->name,
                                                 static_cast<std::uint8_t>(sub_group));
  return 0;
}

int LuaSwapRaidSubgroup(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: SwapRaidSubgroup(index1, index2)");
  }

  if (!GameUI_CanPerformProtectedAction(protected_action_kind::kRaidSubgroup)) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  const auto first_member = ResolveTrackedRaidRosterMemberByLuaIndex(L, 1);
  if (!first_member.has_value()) {
    return 0;
  }

  const auto second_member = ResolveTrackedRaidRosterMemberByLuaIndex(L, 2);
  if (!second_member.has_value()) {
    return 0;
  }

  if (first_member->group_index == second_member->group_index ||
      first_member->name.empty() || second_member->name.empty()) {
    return 0;
  }

  session->interaction().SendGroupSwapSubGroup(first_member->name, second_member->name);
  return 0;
}

int LuaPromoteToLeader(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (lua_type(L, 1) != LUA_TSTRING) {
    return luaL_error(L, "Usage: PromoteToLeader(name)");
  }
  if (!session) {
    return 0;
  }

  const auto exact_match = ScriptReadBoolArgOrDefault(L, 2, false);
  const auto target_guid =
      ResolveGroupPlayerTargetGuid(session, lua_tostring(L, 1), exact_match);
  if (!target_guid.IsEmpty()) {
    session->interaction().SendGroupSetLeader(target_guid.GetRawValue());
  }
  return 0;
}

int LuaDemoteAssistant(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!lua_isstring(L, 1)) {
    luaL_error(L, "Usage: DemoteAssistant(name)");
  }
  if (!session) {
    return 0;
  }

  const auto exact_match = ScriptReadBoolArgOrDefault(L, 2, false);
  const auto target_guid =
      ResolveGroupPlayerTargetGuid(session, lua_tostring(L, 1), exact_match);
  if (!target_guid.IsEmpty()) {
    session->interaction().SendGroupAssistantLeader(target_guid.GetRawValue(), false);
  }
  return 0;
}

int LuaUnitIsRaidOfficer(lua_State *L) {
  const LuaCallFrame call{L};
  auto *session = call.world_session();
  if (session == nullptr) {
    return call.nil();
  }
  const auto uid = SafeLuaString(L, 1);
  if (uid.empty()) {
    return call.nil();
  }
  const auto guid = ResolveUnitId(session, uid);
  if (guid.IsEmpty()) {
    return call.nil();
  }
  return call.wow_bool(openwow::game::GroupSystem::Get().HasRaidOfficerRank(guid.GetRawValue()));
}

int LuaConvertToRaid(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  auto &gs = ::openwow::game::GroupSystem::Get();
  if (!gs.HasPartyMembers())
    return 0;

  const auto active_guid = session->objects().GetActivePlayerGuid();
  if (active_guid.IsEmpty() || gs.GetLeaderGuid() != active_guid.GetRawValue())
    return 0;

  const auto *player = session->objects().GetActivePlayer();
  if (!player || player->State().GetLevel() < 10)
    return 0;

  session->interaction().SendGroupRaidConvert(true);
  return 0;
}

int LuaGetReadyCheckStatus(lua_State *L) {
  auto uid = std::string(luaL_optstring(L, 1, "player"));
  auto &gs = ::openwow::game::GroupSystem::Get();
  if (!gs.IsReadyCheckInProgress()) {
    lua_pushnil(L);
    return 1;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }
  auto guid = ResolveUnitId(session, uid);
  if (guid.IsEmpty()) {
    lua_pushnil(L);
    return 1;
  }

  switch (gs.QueryReadyCheckStatus(guid.GetRawValue())) {
  case openwow::game::ReadyCheckQueryResult::Waiting:
    lua_pushstring(L, "waiting");
    break;
  case openwow::game::ReadyCheckQueryResult::Ready:
    lua_pushstring(L, "ready");
    break;
  case openwow::game::ReadyCheckQueryResult::NotReady:
    lua_pushstring(L, "notready");
    break;
  case openwow::game::ReadyCheckQueryResult::None:
  default:
    lua_pushnil(L);
    break;
  }
  return 1;
}

int LuaDoReadyCheck(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  openwow::game::GroupSystem::Get().DoReadyCheck(
      [session]() { session->interaction().SendReadyCheck(); },
      [](const int message_id) { ::openwow::ui::game::DisplaySystemMessage(message_id); });
  return 0;
}

int LuaConfirmReadyCheck(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  auto &gs = ::openwow::game::GroupSystem::Get();

  const bool has_group_context =
      gs.GetRealRaidMemberCount() != 0 || gs.HasPartyMembers();
  if (!has_group_context)
    return 0;

  const auto end_time = static_cast<std::uint32_t>(gs.GetReadyCheckEndTime());
  if (end_time == 0)
    return 0;

  const auto now_tick = openwow::core::GameClock::GetTickCount32();
  if (static_cast<std::int32_t>(now_tick - end_time) >= 0)
    return 0;

  bool is_ready = lua_toboolean(L, 1) != 0;
  session->interaction().SendReadyCheckConfirm(is_ready);
  return 0;
}

int LuaGetReadyCheckTimeLeft(lua_State *L) {
  auto &gs = ::openwow::game::GroupSystem::Get();
  const bool has_ready_check_group_context =
      gs.GetRealRaidMemberCount() != 0 || gs.HasPartyMembers();
  if (!has_ready_check_group_context) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto end_time = static_cast<std::uint32_t>(gs.GetReadyCheckEndTime());
  const auto now_tick = openwow::core::GameClock::GetTickCount32();
  if (end_time == 0 || static_cast<std::int32_t>(now_tick - end_time) >= 0) {
    lua_pushnumber(L, 0);
  } else {
    lua_pushnumber(L, static_cast<lua_Integer>((end_time - now_tick) / 1000));
  }
  return 1;
}

int LuaAddMute(lua_State *L) {

  const char *name = lua_tostring(L, 1);
  if (name == nullptr) {
    return 0;
  }

  auto *session = GetWorldSession(L);

  if (session == nullptr || IsRetailMutedName(*session, name)) {
    return 0;
  }

  const auto packet = openwow::game::SocialManager::BuildAddMute(
      CopyRetailCStringToBuffer(name, kAddMuteNameBufferCapacity));
  (void)openwow::net::ClientServices__SendPacket(packet);
  return 0;
}

int LuaAddOrDelIgnore(lua_State *L) {

  const char *name = lua_tostring(L, 1);
  if (name == nullptr) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto *contact =
      FindRetailSocialContactByName(*session, session->social().GetIgnored(), name);

  const auto packet = contact != nullptr
                          ? openwow::net::wotlk::PacketSender::BuildDelIgnore(
                                contact->guid.GetRawValue())
                          : openwow::net::wotlk::PacketSender::BuildAddIgnore(name);
  (void)openwow::net::ClientServices__SendPacket(packet);
  return 0;
}

int LuaApi_AddOrDelMute(lua_State *L) {

  const char *name = lua_tostring(L, 1);
  if (name == nullptr) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto *contact =
      FindRetailSocialContactByName(*session, session->social().GetMuted(), name);

  const auto packet = contact != nullptr
                          ? openwow::game::SocialManager::BuildDelMute(contact->guid)
                          : openwow::game::SocialManager::BuildAddMute(name);
  (void)openwow::net::ClientServices__SendPacket(packet);
  return 0;
}

int LuaApi_AddOrRemoveFriend(lua_State *L) {

  const char *name = lua_tostring(L, 1);
  if (name == nullptr) {
    return 0;
  }
  const char *note = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";

  auto *session = GetWorldSession(L);
  const auto *contact = session != nullptr
                            ? FindRetailSocialContactByName(*session,
                                                            session->social().GetFriends(), name)
                            : nullptr;

  const auto packet = contact != nullptr
                          ? openwow::game::SocialManager::BuildDelFriend(contact->guid)
                          : BuildRetailAddFriendPacket(name, note);
  (void)openwow::net::ClientServices__SendPacket(packet);
  return 0;
}

int LuaBNSetCustomMessage(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: BNSetCustomMessage(text)");
  }

  if (GetBNetCustomMessageThrottleState().Consume(GetBNetCustomMessageTickCountSeconds())) {
    ::openwow::ui::game::DisplaySystemMessage(kBnCustomMessageThrottleSystemMessageId);
    return 0;
  }

  const char *const text = lua_tostring(L, 1);
  if (CountLegacyUtf8Codepoints(text) > kBnCustomMessageMaxCodepoints) {
    return luaL_error(L, "Message text too long, maximum length %d chars",
                      static_cast<int>(kBnCustomMessageMaxCodepoints));
  }

  std::array<char, kBNetSanitizedChatTextBufferSize> sanitized_text{};
  std::array<char, kBNetSanitizedChatTextBufferSize> bounded_text{};
  CopySanitizedBNetChatText(text ? std::string_view(text) : std::string_view(),
                            sanitized_text.data(), sanitized_text.size());
  CopyBoundedLegacyUtf8Text(bounded_text.data(), bounded_text.size(), sanitized_text.data(),
                            kBnCustomMessageMaxCodepoints);
  api.SetCustomMessage(bounded_text.data());
  return 0;
}

int LuaBNSetSelectedBlock(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  return SetSelectedBNetBlockListEntry(
      L, "Usage: BNSetSelectedBlock(index)", "Block index %d too large, only %d blocked.",
      [&api](const std::int32_t presence_id) { api.SetSelectedBlockPresenceId(presence_id); });
}

int LuaBNSetSelectedFriend(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNSetSelectedFriend(index)");
  }

  if (!api.IsFriendListInitialized()) {
    return 0;
  }

  const auto zero_based_index = TruncateLuaNumberToBNetZeroBasedIndex(lua_tonumber(L, 1));
  if (zero_based_index >= static_cast<std::uint32_t>(api.GetFriendCount())) {
    return luaL_error(L, "Friend index %d too large, only %d friends.",
                      static_cast<std::int32_t>(zero_based_index + 1u),
                      api.GetFriendCount());
  }

  const auto *friend_info = api.GetFriend(static_cast<std::int32_t>(zero_based_index));

  if (friend_info == nullptr || friend_info->presence_id == 0) {
    openwow::core::SErrFatalCondition("%s", "");
  }

  api.SetSelectedFriendPresenceId(friend_info->presence_id);

  return 0;
}

int LuaCancelSummon(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  session->interaction().SendSummonResponse(
      session->summon().pending().summoner_guid, false);
  return 0;
}

int LuaDelMute(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  session->DeleteMutedContactByName(SafeLuaString(L, 1));
  return 0;
}

int LuaSetFriendNotes(lua_State *L) {
  if ((!lua_isnumber(L, 1) && !lua_isstring(L, 1)) ||
      !lua_isstring(L, 2)) {
    return luaL_error(
        L, "Usage: SetFriendNotes([\"name\"] or [index], [\"notes\"])");
  }
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  auto note = SafeLuaString(L, 2);
  auto friends = session->social().GetFriends();
  const ::openwow::game::ContactInfo *contact = nullptr;
  if (lua_isnumber(L, 1)) {
    const int index = static_cast<int>(lua_tonumber(L, 1));
    if (index >= 1 && index <= static_cast<int>(friends.size())) {
      contact = friends[static_cast<std::size_t>(index - 1)];
    }
  } else {
    const auto name = SafeLuaString(L, 1);
    contact = FindVisibleSocialContactByName(
        *session, name, ::openwow::game::SocialFlag::kFriend);
    if (contact == nullptr) {

      session->DisplaySocialApiError(278);
    }
  }
  if (contact != nullptr) {
    session->interaction().SendSetContactNotes(contact->guid.GetRawValue(), note);
  }
  return 0;
}

int LuaSetSelectedIgnore(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetSelectedIgnore(index)");
  }
  if (auto *session = GetWorldSession(L); session != nullptr) {
    session->social().SelectIgnoredByLuaIndex(
        openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)));
  }
  return 0;
}

int LuaShowFriends(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  session->interaction().SendContactList(0x07);
  return 0;
}

int LuaIsReferAFriendLinked(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto unit_token = SafeLuaString(L, 1);
  const auto target_guid = ResolveUnitId(session, unit_token).GetRawValue();
  if (!HasReferAFriendLink(*session, target_guid)) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushnumber(L, 1.0);
  return 1;
}

int LuaCanSummonFriend(lua_State *L) {
  const auto *session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushboolean(L, 0);
    return 1;
  }

  const auto &summon = session->summon().pending();
  if (summon.summoner_guid == 0) {
    lua_pushboolean(L, 0);
    return 1;
  }

  const auto *player = session->objects().GetLocalPlayerTyped();
  if (player == nullptr || player->State().IsDeadOrGhost()) {
    lua_pushboolean(L, 0);
    return 1;
  }

  lua_pushnumber(L, 1.0);
  return 1;
}

int LuaCanGrantLevel(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto unit_token = SafeLuaString(L, 1);
  const auto target_guid = ResolveUnitId(session, unit_token).GetRawValue();
  if (EvaluateGrantLevelEligibility(*session, target_guid) !=
      openwow::game::GrantLevelEligibilityResult::kSuccess) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushnumber(L, 1.0);
  return 1;
}

int LuaSummonFriend(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  const auto unit_token = SafeLuaString(L, 1);
  const auto target_guid = ResolveUnitId(session, unit_token).GetRawValue();
  const auto summon_spell_id = GetSummonFriendSpellId();
  const auto result = EvaluateSummonFriendEligibility(*session, target_guid, summon_spell_id);
  if (session->objects().GetActivePlayer() == nullptr &&
      result == SummonFriendEligibilityResult::kSuccess) {
    return 0;
  }

  if (result != SummonFriendEligibilityResult::kSuccess) {
    session->DisplayReferAFriendFailure(static_cast<std::uint32_t>(result), target_guid);
    return 0;
  }

  const auto previous_target_guid = session->objects().GetTargetGuid().GetRawValue();
  const bool changed_selection = previous_target_guid != target_guid;
  if (changed_selection) {
    session->interaction().SendSetSelection(target_guid);
  }

  session->interaction().SendCastSpell(summon_spell_id, 0, target_guid);

  if (changed_selection) {
    session->interaction().SendSetSelection(previous_target_guid);
  }
  return 0;
}

int LuaGrantLevel(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const auto unit_token = SafeLuaString(L, 1);
  const auto target_guid = ResolveUnitId(session, unit_token).GetRawValue();
  const auto result = EvaluateGrantLevelEligibility(*session, target_guid);
  if (result != openwow::game::GrantLevelEligibilityResult::kSuccess) {
    session->DisplayReferAFriendFailure(static_cast<std::uint32_t>(result), target_guid);
    return 0;
  }

  session->interaction().SendGrantLevel(target_guid);
  return 0;
}

int LuaBNSendFriendInvite(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: BNSendFriendInvite(text, noteText)");
  }

  const char *const email = lua_tostring(L, 1);
  std::array<char, kBNetSanitizedChatTextBufferSize> sanitized_note{};
  api.SendRIDFriendInviteByEmail(email ? email : "",
                                 GetOptionalBNetInviteNote(L, 2, sanitized_note));
  return 0;
}

int LuaBNGetFriendInfo(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNGetFriendInfo(index)");
  }

  if (!api.IsFriendListInitialized()) {
    return 0;
  }

  const auto requested_index = TruncateLuaNumberToBNetU32(lua_tonumber(L, 1));
  const auto zero_based_index = requested_index - 1u;
  if (zero_based_index >= static_cast<std::uint32_t>(api.GetFriendCount())) {
    return luaL_error(L, "Friend index %d too large, only %d friends.",
                      static_cast<std::int32_t>(requested_index),
                      api.GetFriendCount());
  }

  const auto friend_info = api.GetFriendLuaInfo(static_cast<std::int32_t>(zero_based_index));
  if (!friend_info.has_value()) {
    return 0;
  }

  PushBNetFriendLuaInfo(L, *friend_info);
  return 14;
}

int LuaBNGetFriendInfoByID(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNGetFriendInfoByID(ID)");
  }

  if (!api.IsFriendListInitialized()) {
    return 0;
  }

  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  const auto friend_index = api.FindFriendIndexByPresenceID(presence_id);
  if (friend_index < 0) {
    return 0;
  }

  const auto friend_info = api.GetFriendLuaInfo(friend_index);
  if (!friend_info.has_value()) {
    return 0;
  }

  PushBNetFriendLuaInfo(L, *friend_info);
  return 14;
}

int LuaBNGetInfo(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  const auto account_presence_id = api.GetPresenceIDForCurrentAccount();
  const auto toon_presence_id = api.GetPresenceIDForCurrentToon();

  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(account_presence_id)));
  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(toon_presence_id)));

  if (const auto custom_message =
          GetBNetPresenceOptionalTextValue(api, toon_presence_id, kBNetPresenceKeyCustomMessage);
      custom_message.has_value()) {
    lua_pushstring(L, custom_message->c_str());
  } else {
    lua_pushnil(L);
  }

  lua_pushboolean(L,
                  GetBNetPresenceFlagValue(api, toon_presence_id, kBNetPresenceKeyAfk) ? 1 : 0);
  lua_pushboolean(L,
                  GetBNetPresenceFlagValue(api, toon_presence_id, kBNetPresenceKeyDnd) ? 1 : 0);
  return 5;
}

int LuaBNGetNumFriends(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled() || !api.IsFriendListInitialized() || !api.IsFullyConnected()) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
  }

  lua_pushnumber(L, static_cast<lua_Number>(api.GetFriendCount()));
  lua_pushnumber(L, static_cast<lua_Number>(api.GetOnlineFriendCount()));
  return 2;
}

int LuaBNGetSelectedBlock(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(api.GetSelectedBlockLuaIndex()));
  return 1;
}

int LuaBNGetSelectedFriend(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled() || !api.IsFriendListInitialized()) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(api.GetSelectedFriendLuaIndex()));
  return 1;
}

int LuaBNIsFriend(lua_State *L) {
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNIsFriend(presenceID)");
  }

  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  lua_pushboolean(L, BattleNetApi::Instance().IsPresenceIDFriend(presence_id) ? 1 : 0);
  return 1;
}

int LuaBNIsSelf(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNIsSelf(presenceID)");
  }

  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  lua_pushboolean(L, api.IsPresenceIDSelf(presence_id) ? 1 : 0);
  return 1;
}

int LuaGetMuteName(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetMuteName(index)");
  }
  auto *session = GetWorldSession(L);
  int index = static_cast<int>(lua_tonumber(L, 1));
  if (!session) {
    lua_pushstring(L, "UNKNOWN");
    return 1;
  }
  const auto muted = session->social().GetMuted();
  if (index < 1 || index > static_cast<int>(muted.size())) {
    lua_pushstring(L, "UNKNOWN");
    return 1;
  }
  const std::string name = ResolveSocialContactName(
      *session, *muted[static_cast<std::size_t>(index - 1)]);
  if (name.empty())
    lua_pushstring(L, "UNKNOWN");
  else
    lua_pushstring(L, name.c_str());
  return 1;
}

int LuaGetNumMutes(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Number>(session->social().GetMuted().size()));
  return 1;
}

int LuaGetSelectedIgnore(lua_State *L) {
  const auto *session = GetWorldSession(L);
  lua_pushnumber(L, static_cast<lua_Number>(
                        session != nullptr
                            ? session->social().GetSelectedIgnoredLuaIndex()
                            : 0));
  return 1;
}

int LuaSetSelectedMute(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetSelectedMute(index)");
  }
  if (auto *session = GetWorldSession(L); session != nullptr) {
    session->social().SelectMutedByLuaIndex(
        openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)));
  }
  return 0;
}

int LuaGetSelectedMute(lua_State *L) {
  const auto *session = GetWorldSession(L);
  lua_pushnumber(L, static_cast<lua_Number>(
                        session != nullptr
                            ? session->social().GetSelectedMutedLuaIndex()
                            : 0));
  return 1;
}

int LuaGetSummonFriendCooldown(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto summon_spell_id = GetSummonFriendSpellId();
  if (session == nullptr || summon_spell_id == 0) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
  }

  const auto cooldown_it = session->spell_book().cooldowns().find(summon_spell_id);
  if (cooldown_it == session->spell_book().cooldowns().end()) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 1);
    return 3;
  }

  const auto enabled = (cooldown_it->second.category_cooldown_ms & 0x80000000u) == 0 ? 1 : 0;
  const auto category_cooldown_ms = cooldown_it->second.category_cooldown_ms & 0x7FFFFFFFu;
  const auto duration_ms = std::max(cooldown_it->second.cooldown_ms, category_cooldown_ms);
  if (duration_ms == 0) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, enabled);
    return 3;
  }

  lua_pushnumber(L, cooldown_it->second.start_time_s);
  lua_pushnumber(L, static_cast<lua_Number>(duration_ms) / 1000.0);
  lua_pushnumber(L, enabled);
  return 3;
}

int LuaIsIgnored(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }
  auto name = SafeLuaString(L, 1);
  if (name.empty()) {
    lua_pushnil(L);
    return 1;
  }
  const auto *contact = FindVisibleSocialContactByName(
      *session, name, ::openwow::game::SocialFlag::kIgnored);
  lua_pushwowbool(L, contact != nullptr);
  return 1;
}

int LuaIsMuted(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }
  auto name = SafeLuaString(L, 1);
  if (name.empty()) {
    lua_pushnil(L);
    return 1;
  }
  const auto *contact = FindVisibleSocialContactByName(
      *session, name, ::openwow::game::SocialFlag::kMuted);
  lua_pushwowbool(L, contact != nullptr);
  return 1;
}

int LuaIsIgnoredOrMuted(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }
  auto name = SafeLuaString(L, 1);
  if (name.empty()) {
    lua_pushnil(L);
    return 1;
  }
  const auto *ignored = FindVisibleSocialContactByName(
      *session, name, ::openwow::game::SocialFlag::kIgnored);
  const auto *muted = FindVisibleSocialContactByName(
      *session, name, ::openwow::game::SocialFlag::kMuted);
  lua_pushwowbool(L, ignored != nullptr || muted != nullptr);
  return 1;
}

int LuaBNConnected(lua_State *L) {
  const auto &client_services = openwow::net::ClientServices::Instance();
  const auto &api = ::openwow::game::BattleNetApi::Instance();
  lua_pushboolean(L, client_services.IsBNLogin() && api.IsConnectedState());
  return 1;
}

int LuaBNFeaturesEnabled(lua_State *L) {
  lua_pushboolean(L, IsBNetRidLuaEnabled());
  return 1;
}

int LuaBNFeaturesEnabledAndConnected(lua_State *L) {
  lua_pushboolean(L, openwow::game::BattleNetApi::Instance().IsFullyConnected());
  return 1;
}

int LuaIsBNLogin(lua_State *L) {
  lua_pushboolean(L, openwow::net::ClientServices::Instance().IsBNLogin() ? 1 : 0);
  return 1;
}

int LuaBNGetNumFriendToons(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    lua_pushnumber(L, 0);
    return 1;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNGetNumFriendToons(index)");
  }

  if (!api.IsFriendListInitialized()) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto requested_index = TruncateLuaNumberToBNetU32(lua_tonumber(L, 1));
  const auto zero_based_index = requested_index - 1u;
  if (zero_based_index >= static_cast<std::uint32_t>(api.GetFriendCount())) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto *friend_info = api.GetFriend(static_cast<std::int32_t>(zero_based_index));
  if (!friend_info) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto total_toons = api.GetNumToons(friend_info->presence_id);
  std::int32_t online_toon_count = 0;
  for (std::int32_t toon_index = 0; toon_index < total_toons; ++toon_index) {
    const auto toon_presence_id = api.GetToon(friend_info->presence_id, toon_index);
    if (GetBNetPresenceBoolValue(api, toon_presence_id, kBNetPresenceKeyOnline)) {
      ++online_toon_count;
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(online_toon_count));
  return 1;
}

int LuaBNRemoveFriend(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNRemoveFriend(ID)");
  }

  if (!api.IsFriendListInitialized()) {
    return 0;
  }

  api.RemoveFriend(TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1)));
  return 0;
}

int LuaBNSetFriendNote(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNSetFriendNote(ID, noteText)");
  }

  if (!api.IsFriendListInitialized()) {
    return 0;
  }

  if (!lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: BNSetFriendNote(ID, noteText)");
  }

  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  const char *const note_text = lua_tostring(L, 2);
  if (CountLegacyUtf8Codepoints(note_text) > kBnFriendNoteMaxCodepoints) {
    return luaL_error(L, "Friend note text too long, maximum length %d chars", 255);
  }

  api.SetFriendNote(presence_id, note_text ? note_text : "");
  return 0;
}

int LuaBNGetNumFriendInvites(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled() || !api.IsFullyConnected()) {
    lua_pushnumber(L, 0);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(api.GetFriendInviteCount()));
  return 1;
}

int LuaBNGetFriendInviteInfo(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNGetFriendInviteInfo(index)");
  }

  const auto invite_index = TruncateLuaNumberToBNetZeroBasedIndex(lua_tonumber(L, 1));
  if (invite_index >= static_cast<std::uint32_t>(api.GetFriendInviteCount())) {
    return 0;
  }

  const auto invite = api.GetFriendInviteInfo(static_cast<std::int32_t>(invite_index));
  if (!invite.has_value()) {
    return luaL_error(L, "No Friend at index");
  }

  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(invite->presence_id)));
  if (invite->has_name) {
    lua_pushstring(L, invite->formatted_name_left.c_str());
    lua_pushstring(L, invite->formatted_name_right.c_str());
  } else {
    lua_pushnil(L);
    lua_pushnil(L);
  }

  if (invite->has_message) {
    lua_pushstring(L, invite->message.c_str());
  } else {
    lua_pushnil(L);
  }

  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(invite->timestamp)));
  return 5;
}

int LuaBNSendFriendInviteByID(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: BNSendFriendInviteByID(ID, noteText)");
  }

  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  std::array<char, kBNetSanitizedChatTextBufferSize> sanitized_note{};
  api.SendRIDFriendInviteByPresenceId(presence_id, GetOptionalBNetInviteNote(L, 2, sanitized_note));
  return 0;
}

int LuaBNAcceptFriendInvite(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNAcceptFriendInvite(ID)");
  }

  api.AcceptFriendInvite(TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1)));
  return 0;
}

int LuaBNDeclineFriendInvite(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNDeclineFriendInvite(ID)");
  }

  api.DeclineFriendInvite(TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1)));
  return 0;
}

int LuaBNReportFriendInvite(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNReportFriendInvite(ID)");
  }

  api.ReportFriendInvite(TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1)));
  return 0;
}

int LuaBNSetAFK(lua_State *L) {
  return SetBNetBooleanPresenceValue(L, "Usage: BNSetAFK(bool)", kBNetPresenceKeyAfk);
}

int LuaBNSetDND(lua_State *L) {
  return SetBNetBooleanPresenceValue(L, "Usage: BNSetDND(bool)", kBNetPresenceKeyDnd);
}

int LuaBNGetCustomMessageTable(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_istable(L, 1)) {
    return luaL_error(L, "Usage: BNGetCustomMessageTable(table)");
  }

  if (!api.IsFriendListInitialized()) {
    lua_pushnil(L);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 3;
  }

  constexpr auto kCustomMessageKey =
      static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kCustomMessage);
  constexpr auto kOnlineKey =
      static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kOnline);

  std::int32_t online_count = 0;
  std::int32_t offline_count = 0;
  lua_pushvalue(L, 1);
  for (std::int32_t index = 0; index < api.GetFriendCount(); ++index) {
    const auto *friend_info = api.GetFriend(index);
    bool has_custom_message = false;
    if (friend_info != nullptr) {
      const auto custom_message = api.GetPresenceValue(friend_info->presence_id, kCustomMessageKey);

      has_custom_message =
          custom_message.type == openwow::game::BNetPresenceValue::Type::kString &&
          !custom_message.str_val.empty();
      if (has_custom_message) {
        const auto online = api.GetPresenceValue(friend_info->presence_id, kOnlineKey);
        if (online.type == openwow::game::BNetPresenceValue::Type::kBool &&
            online.byte_val != 0) {
          ++online_count;
        } else {
          ++offline_count;
        }
      }
    }

    lua_pushboolean(L, has_custom_message ? 1 : 0);
    lua_rawseti(L, 1, index + 1);
  }

  lua_pushnumber(L, static_cast<lua_Number>(online_count));
  lua_pushnumber(L, static_cast<lua_Number>(offline_count));
  return 3;
}

int LuaBNSetFocus(lua_State * ) {
  if (IsBNetRidLuaEnabled()) {
    openwow::game::BattleNetApi::Instance().SetPresenceValue(
        static_cast<std::int32_t>(openwow::game::BNetPresenceKey::kFocus),
        openwow::game::BNetVariant());
  }
  return 0;
}

int LuaBNCreateConversation(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!api.IsFullyConnected()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNCreateConversation(id,id)");
  }

  constexpr auto kToonNameKey = static_cast<std::int32_t>(BNetPresenceKey::kToonName);
  const auto first_presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  const auto first_toon_name = api.GetPresenceValue(first_presence_id, kToonNameKey);
  if (first_toon_name.type != BNetPresenceValue::Type::kToonName) {
    return 0;
  }

  if (!lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: BNCreateConversation(id,id)");
  }

  const auto second_presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 2));
  const auto second_toon_name = api.GetPresenceValue(second_presence_id, kToonNameKey);
  if (second_toon_name.type != BNetPresenceValue::Type::kToonName) {
    return 0;
  }

  api.CreateConversation(first_toon_name, second_toon_name);
  lua_pushboolean(L, 1);
  return 1;
}

int LuaBNInviteToConversation(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!api.IsFullyConnected()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNInviteToConversation(channel,id)");
  }

  const auto wrapped_channel = TruncateLuaNumberToBNetChannelIndex(lua_tonumber(L, 1));
  if (wrapped_channel >= openwow::game::kBNetConversationChannelCount) {
    return luaL_error(L, "Invalid Channel");
  }

  if (!lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: BNInviteToConversation(channel,id)");
  }

  constexpr auto kToonNameKey = static_cast<std::int32_t>(BNetPresenceKey::kToonName);
  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 2));
  const auto invitee_toon_name = api.GetPresenceValue(presence_id, kToonNameKey);
  if (invitee_toon_name.type != BNetPresenceValue::Type::kToonName) {
    return 0;
  }

  api.InviteToConversation(wrapped_channel, invitee_toon_name);
  return 0;
}

int LuaBNLeaveConversation(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNLeaveConversation(channel)");
  }

  if (!api.IsFullyConnected()) {
    return 0;
  }

  const auto wrapped_channel = TruncateLuaNumberToBNetChannelIndex(lua_tonumber(L, 1));
  if (wrapped_channel >= openwow::game::kBNetConversationChannelCount) {
    return luaL_error(L, "Invalid Channel");
  }

  api.LeaveConversation(wrapped_channel);
  return 0;
}

int LuaBNSendConversationMessage(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: BNSendConversationMessage(channel,text)");
  }

  if (!api.IsFullyConnected()) {
    return 0;
  }

  const auto wrapped_channel = TruncateLuaNumberToBNetChannelIndex(lua_tonumber(L, 1));
  const char *text = lua_tostring(L, 2);
  std::array<char, kBNetSanitizedChatTextBufferSize> sanitized_text{};
  CopySanitizedBNetChatText(text ? std::string_view(text) : std::string_view(),
                            sanitized_text.data(), sanitized_text.size());

  if (wrapped_channel >= openwow::game::kBNetConversationChannelCount) {
    return luaL_error(L, "Invalid Channel");
  }

  api.SendConversationMessage(wrapped_channel, sanitized_text.data());
  return 0;
}

int LuaBNGetNumConversationMembers(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNGetNumConversationMembers(channel)");
  }

  if (!api.IsFullyConnected()) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  const auto wrapped_channel = TruncateLuaNumberToBNetChannelIndex(lua_tonumber(L, 1));
  if (wrapped_channel >= openwow::game::kBNetConversationChannelCount) {
    return luaL_error(L, "Invalid Channel");
  }

  lua_pushnumber(
      L, static_cast<lua_Number>(api.GetConversationMemberList(wrapped_channel).size()));
  return 1;
}

int LuaBNGetConversationInfo(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNGetConversationInfo(channel)");
  }

  const auto channel_index = TruncateLuaNumberToBNetChannelIndex(lua_tonumber(L, 1));

  if (api.GetChatChannelType(channel_index) == kBNetConversationChannelTypeRegular) {
    lua_pushstring(L, "conversation");
  } else {
    lua_pushnil(L);
  }

  return 1;
}

int LuaBNGetNumBlocked(lua_State *L) {
  return PushBNetBlockListCount(L);
}

int LuaBNIsBlocked(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNIsBlocked(ID)");
  }

  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  lua_pushboolean(L, api.IsPresenceIDBlocked(presence_id) ? 1 : 0);
  return 1;
}

int LuaBNSetBlocked(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || lua_type(L, 2) != LUA_TBOOLEAN) {
    return luaL_error(L, "Usage: BNSetBlocked(ID, true/false)");
  }

  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  if (lua_toboolean(L, 2) != 0) {
    api.AddRIDBlock(presence_id);
    return 0;
  }

  api.RemoveRIDBlock(presence_id);
  return 0;
}

int LuaBNGetNumBlockedToons(lua_State *L) {
  return PushBNetBlockListCount(L);
}

int LuaBNGetBlockedToonInfo(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNGetBlockedToonInfo(index)");
  }

  const auto block_list = api.GetBlockList();
  const auto block_index = TruncateLuaNumberMinusOneToBNetIndex(lua_tonumber(L, 1));
  if (block_index >= static_cast<std::uint32_t>(block_list.count)) {
    return luaL_error(L, "Invalid Index");
  }

  const auto presence_id =
      block_list.entries[static_cast<std::size_t>(block_index)].presence_id;
  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(presence_id)));
  lua_pushstring(L, api.GetExactNameForPresenceId(presence_id, false));
  return 2;
}

int LuaBNIsToonBlocked(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNIsToonBlocked(ID)");
  }

  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  lua_pushboolean(L, api.IsCIDBlock(presence_id) ? 1 : 0);
  return 1;
}

int LuaBNSetToonBlocked(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || lua_type(L, 2) != LUA_TBOOLEAN) {
    return luaL_error(L, "Usage: BNSetToonBlocked(ID, true/false)");
  }

  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  if (lua_toboolean(L, 2) != 0) {
    (void)api.AddCIDBlock(presence_id);
  } else {
    (void)api.RemoveCIDBlock(presence_id);
  }
  return 0;
}

int LuaBNSetSelectedToonBlock(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  return SetSelectedBNetBlockListEntry(
      L, "Usage: BNSetSelectedToonBlock(index)",
      "Toon Block index %d too large, only %d blocked.",
      [&api](const std::int32_t presence_id) { api.SetSelectedToonBlockPresenceId(presence_id); });
}

int LuaBNGetSelectedToonBlock(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(api.GetSelectedToonBlockLuaIndex()));
  return 1;
}

int LuaBNReportPlayer(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: BNReportPlayer(ID,typeText,note)");
  }

  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  const char *const reason_text = lua_tostring(L, 2);

  const char *note = nullptr;
  if (lua_isstring(L, 3)) {
    note = lua_tostring(L, 3);
    if (IsBnReportNoteTooLong(note)) {
      return luaL_error(L, "Report note is too long.");
    }
  }

  const auto reason = ParseBnReportPlayerReason(reason_text);
  if (!reason.has_value()) {
    return luaL_error(L, "Unknown Reason");
  }

  if (api.ReportPlayer(presence_id, *reason, note) == 0) {

    if (auto *session = GetWorldSession(L); session != nullptr) {
      const auto message = Localization::Get().GetString("BNET_REPORT_SENT");
      ChatFrame_DisplayMessage(session->objects(), message.c_str(), ChatDisplayType::kSystem, nullptr, 0, nullptr,
                               nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
    }
  }
  return 0;
}

int LuaBNGetNumFOF(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNGetNumFOF(ID,mutual,non)");
  }

  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  if (presence_id != api.GetFriendsOfFriendSourcePresenceId()) {
    return luaL_error(L, "Incorrect ID");
  }

  const auto [mutual_count, non_mutual_count] = api.GetFriendsOfFriendCounts();
  lua_pushnumber(L, static_cast<lua_Number>(mutual_count));
  lua_pushnumber(L, static_cast<lua_Number>(non_mutual_count));
  return 2;
}

int LuaBNGetFOFInfo(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 4)) {
    return luaL_error(L, "Usage: BNGetFOFInfo(ID, mutual, non, index)");
  }

  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  if (presence_id != api.GetFriendsOfFriendSourcePresenceId()) {
    return luaL_error(L, "Incorrect ID");
  }

  const bool include_mutual = lua_toboolean(L, 2) != 0;
  const bool include_non_mutual = lua_toboolean(L, 3) != 0;
  if (!include_mutual && !include_non_mutual) {
    return luaL_error(L, "Must select mutual and/or non.");
  }

  const auto filtered_index = TruncateLuaNumberToBNetZeroBasedIndex(lua_tonumber(L, 4));
  const auto fof_info = api.GetFriendOfFriendInfo(static_cast<std::int32_t>(filtered_index),
                                                  include_mutual, include_non_mutual);
  if (!fof_info.has_value()) {
    return luaL_error(L, "No friend at that index.");
  }

  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(fof_info->presence_id)));
  if (fof_info->has_name) {
    lua_pushstring(L, fof_info->formatted_name_left.c_str());
    lua_pushstring(L, fof_info->formatted_name_right.c_str());
  } else {
    lua_pushnil(L);
    lua_pushnil(L);
  }
  lua_pushboolean(L, fof_info->is_mutual ? 1 : 0);
  return 4;
}

int LuaBNRequestFOF(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled())
    return 0;

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNRequestFOF(ID)");
  }

  const auto presence_id = TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1));
  const auto error_code = api.RequestFriendsOfFriendInfo(presence_id);
  lua_pushboolean(L, error_code == 0);
  api.HandleError(error_code, presence_id);
  return 1;
}

int LuaBNSetMatureLanguageFilter(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (lua_type(L, 1) != LUA_TBOOLEAN) {
    return luaL_error(L, "Usage: BNSetMatureLanguageFilter(true/false)");
  }

  if (!api.IsFullyConnected()) {
    return luaL_error(L, "not connected to Battle.net");
  }

  const bool enabled = lua_toboolean(L, 1) != 0;
  api.SetSetting("Chat.ProfanityFilterEnabled",
                 openwow::game::BNetVariant(static_cast<std::int32_t>(enabled)),
                 true);
  return 0;
}

int LuaBNGetMatureLanguageFilter(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!api.IsFullyConnected()) {
    return luaL_error(L, "not connected to Battle.net");
  }

  const openwow::game::BNetVariant value =
      api.GetSetting("Chat.ProfanityFilterEnabled");
  const bool enabled = IsBNetMatureLanguageFilterEnabled(value);
  lua_pushboolean(L, enabled ? 1 : 0);
  return 1;
}

int LuaBNGetMaxPlayersInConversation(lua_State *L) {
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(openwow::game::kBNetConversationMaxMembers));
  return 1;
}

int LuaBNGetFriendToonInfo(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: BNGetToonInfo(friendIndex,toonIndex)");
  }

  if (!api.IsFriendListInitialized()) {
    return 0;
  }

  const auto requested_friend_index = TruncateLuaNumberToBNetU32(lua_tonumber(L, 1));
  const auto requested_online_toon_index =
      TruncateLuaNumberToBNetZeroBasedIndex(lua_tonumber(L, 2));

  if (requested_friend_index == 0 ||
      requested_friend_index > static_cast<std::uint32_t>(api.GetFriendCount())) {
    return luaL_error(L, "Friend index %d too large, only %d friends.",
                      static_cast<std::int32_t>(requested_friend_index), api.GetFriendCount());
  }

  const auto *friend_info =
      api.GetFriend(static_cast<std::int32_t>(requested_friend_index - 1u));
  if (!friend_info) {
    return 0;
  }

  const auto total_toons = api.GetNumToons(friend_info->presence_id);
  if (requested_online_toon_index >= static_cast<std::uint32_t>(total_toons)) {
    return luaL_error(L, "Toon index %d too large, only %d toons.",
                      static_cast<std::int32_t>(requested_online_toon_index + 1u), total_toons);
  }

  std::optional<std::int32_t> matched_toon_list_index;
  std::int32_t matched_online_toon_count = 0;
  for (std::int32_t toon_index = 0; toon_index < total_toons; ++toon_index) {
    const auto toon_presence_id = api.GetToon(friend_info->presence_id, toon_index);
    if (!GetBNetPresenceBoolValue(api, toon_presence_id, kBNetPresenceKeyOnline)) {
      continue;
    }

    if (static_cast<std::uint32_t>(matched_online_toon_count) == requested_online_toon_index) {
      matched_toon_list_index = toon_index;
      break;
    }

    ++matched_online_toon_count;
  }

  if (!matched_toon_list_index.has_value()) {
    return luaL_error(L, "Couldn't find a toon at friend index %d, online toon index %d",
                      static_cast<std::int32_t>(requested_friend_index),
                      static_cast<std::int32_t>(requested_online_toon_index + 1u));
  }

  const auto toon_presence_id = api.GetToon(friend_info->presence_id, *matched_toon_list_index);
  if (toon_presence_id == 0) {
    return luaL_error(
        L, "Couldn't find a toon at friend index %d, toon index %d, online toon index %d",
        static_cast<std::int32_t>(requested_friend_index), *matched_toon_list_index + 1,
        static_cast<std::int32_t>(requested_online_toon_index + 1u));
  }

  return PushBNetToonLuaInfo(L, api, toon_presence_id);
}

int LuaBNGetToonInfo(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNGetToonInfo(ID)");
  }

  return PushBNetToonLuaInfo(L, api, TruncateLuaNumberToBNetPresenceId(lua_tonumber(L, 1)));
}

int LuaBNGetConversationMemberInfo(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled()) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: BNGetConversationMemberInfo(channel,index)");
  }

  if (!api.IsFullyConnected()) {
    return 0;
  }

  const auto wrapped_channel = TruncateLuaNumberToBNetChannelIndex(lua_tonumber(L, 1));
  if (wrapped_channel >= openwow::game::kBNetConversationChannelCount) {
    return luaL_error(L, "Invalid Channel");
  }

  const auto zero_based_member_index =
      TruncateLuaNumberToBNetZeroBasedIndex(lua_tonumber(L, 2));
  const auto members = api.GetConversationMemberList(wrapped_channel);
  if (zero_based_member_index >= members.size()) {
    return luaL_error(L, "Invalid Index");
  }

  const auto member_presence_id = members[static_cast<std::size_t>(zero_based_member_index)];
  const auto account_presence_id = api.GetAccountPresenceId(member_presence_id);
  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(account_presence_id)));
  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(member_presence_id)));
  lua_pushstring(L, api.GetNameForPresenceID(member_presence_id));
  return 3;
}

int LuaBNListConversation(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled())
    return 0;

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNListConversation(channel)");
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto channel_index = TruncateLuaNumberToBNetChannelIndex(lua_tonumber(L, 1));

  if (api.GetChatChannelType(channel_index) != kBNetConversationChannelTypeRegular) {
    return 0;
  }

  const auto members = api.GetConversationMemberList(channel_index);
  if (members.empty())
    return 0;

  const std::string delimiter = Localization::Get().GetString("PLAYER_LIST_DELIMITER", "");
  const auto current_account_presence_id = api.GetPresenceIDForCurrentAccount();

  bool has_line = false;
  std::string line;
  for (const auto presence_id : members) {
    const std::string name = CopyBNetConversationName(api.GetNameForPresenceID(presence_id));
    if (!has_line) {
      line = name;
      has_line = true;
      continue;
    }

    if (line.size() + delimiter.size() + name.size() < kBnConversationListLineCapacity) {
      line += delimiter;
      line += name;
      continue;
    }

    FlushBNetConversationListLine(session->objects(), line, channel_index,
                                  current_account_presence_id);
    line = name;
  }

  if (has_line) {
    FlushBNetConversationListLine(session->objects(), line, channel_index,
                                  current_account_presence_id);
  }
  return 0;
}

int LuaBNGetBlockedInfo(lua_State *L) {
  auto &api = ::openwow::game::BattleNetApi::Instance();
  if (!IsBNetRidLuaEnabled())
    return 0;

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BNGetBlockedInfo(index)");
  }

  const auto block_list = api.GetBlockList();
  const auto block_index = TruncateLuaNumberMinusOneToBNetIndex(lua_tonumber(L, 1));
  if (block_index >= static_cast<std::uint32_t>(block_list.count)) {
    return luaL_error(L, "Invalid Index");
  }

  const auto entry_index = static_cast<std::size_t>(block_index);
  const auto presence_id = static_cast<std::uint32_t>(block_list.entries[entry_index].presence_id);
  lua_pushnumber(L, static_cast<lua_Number>(presence_id));
  lua_pushstring(L, api.GetNameForPresenceID(block_list.entries[entry_index].presence_id));
  return 2;
}

}

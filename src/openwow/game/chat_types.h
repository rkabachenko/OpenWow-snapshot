#pragma once

#include "openwow/core/storm_string.h"

#include <cstdint>

namespace openwow::game {

enum class ChatMsg : std::uint8_t {
  kSystem       = 0x00,
  kSay          = 0x01,
  kParty        = 0x02,
  kRaid         = 0x03,
  kGuild        = 0x04,
  kOfficer      = 0x05,
  kYell         = 0x06,
  kWhisper      = 0x07,
  kWhisperForeign = 0x08,
  kWhisperInform = 0x09,
  kEmote        = 0x0A,
  kTextEmote    = 0x0B,
  kMonsterSay   = 0x0C,
  kMonsterParty = 0x0D,
  kMonsterYell  = 0x0E,
  kMonsterWhisper = 0x0F,
  kMonsterEmote = 0x10,
  kChannel      = 0x11,
  kChannelJoin  = 0x12,
  kChannelLeave = 0x13,
  kChannelList  = 0x14,
  kChannelNotice = 0x15,
  kChannelNoticeUser = 0x16,
  kAfk          = 0x17,
  kDnd          = 0x18,
  kIgnored      = 0x19,
  kSkill        = 0x1A,
  kLoot         = 0x1B,
  kMoney        = 0x1C,
  kOpening      = 0x1D,
  kTradeskills  = 0x1E,
  kPetInfo      = 0x1F,
  kCombatMiscInfo = 0x20,
  kCombatXpGain = 0x21,
  kCombatHonorGain = 0x22,
  kCombatFactionChange = 0x23,
  kBgSystemNeutral = 0x24,
  kBgSystemAlliance = 0x25,
  kBgSystemHorde = 0x26,
  kRaidLeader   = 0x27,
  kRaidWarning  = 0x28,
  kRaidBossEmote = 0x29,
  kRaidBossWhisper = 0x2A,
  kFiltered     = 0x2B,
  kBattleground = 0x2C,
  kBattlegroundLeader = 0x2D,
  kRestricted   = 0x2E,
  kBattlenet    = 0x2F,
  kAchievement  = 0x30,
  kGuildAchievement = 0x31,
  kArenaPoints  = 0x32,
  kPartyLeader  = 0x33,
  kTargetIcons  = 0x34,

  kBnWhisper        = 0x35,
  kBnWhisperInform  = 0x36,
  kBnConversation   = 0x37,
  kBnConversationNotice = 0x38,
  kBnConversationList = 0x39,
  kBnInlineToastAlert = 0x3A,
  kBnInlineToastBroadcast = 0x3B,
  kBnInlineToastBroadcastInform = 0x3C,
  kBnInlineToastConversation = 0x3D,
};

constexpr std::uint8_t kMaxChatMsgType = 0x3D;

enum class Language : std::uint32_t {
  kUniversal    = 0,
  kOrcish       = 1,
  kDarnassian   = 2,
  kTaurahe      = 3,
  kDwarvish     = 6,
  kCommon       = 7,
  kDemonic      = 8,
  kTitan        = 9,
  kThalassian   = 10,
  kDraconic     = 11,
  kKalimag      = 12,
  kGnomish      = 13,
  kTroll        = 14,
  kGutterspeak  = 33,
  kDraenei      = 35,
  kZombie       = 36,
  kGnomishBinary = 37,
  kGoblinBinary = 38,
  kAddon        = 0xFFFFFFFF,
};

enum class ChatTag : std::uint8_t {
  kNone = 0x00,
  kAfk  = 0x01,
  kDnd  = 0x02,
  kGm   = 0x04,
  kCom  = 0x08,
  kDev  = 0x10,
};

enum class ChannelNotify : std::uint8_t {
  kJoined       = 0x00,
  kLeft         = 0x01,
  kYouJoined    = 0x02,
  kYouLeft      = 0x03,
  kWrongPassword = 0x04,
  kNotMember    = 0x05,
  kNotModerator = 0x06,
  kPasswordChanged = 0x07,
  kOwnerChanged = 0x08,
  kPlayerNotFound = 0x09,
  kNotOwner     = 0x0A,
  kChannelOwner = 0x0B,
  kModeChange   = 0x0C,
  kAnnouncementsOn = 0x0D,
  kAnnouncementsOff = 0x0E,
  kModerationOn = 0x0F,
  kModerationOff = 0x10,
  kMuted        = 0x11,
  kPlayerKicked = 0x12,
  kBanned       = 0x13,
  kPlayerBanned = 0x14,
  kPlayerUnbanned = 0x15,
  kPlayerNotBanned = 0x16,
  kPlayerAlreadyMember = 0x17,
  kInvite       = 0x18,
  kInviteWrongFaction = 0x19,
  kWrongFaction = 0x1A,
  kInvalidName  = 0x1B,
  kNotModerated = 0x1C,
  kPlayerInvited = 0x1D,
  kPlayerInviteBanned = 0x1E,
  kThrottled    = 0x1F,
  kNotInArea    = 0x20,
  kNotInLfg     = 0x21,
  kVoiceOn      = 0x22,
  kVoiceOff     = 0x23,
  kVoiceOnSilent = 0x24,

};

constexpr bool IsMonsterChatType(ChatMsg type) {
  switch (type) {
    case ChatMsg::kMonsterSay:
    case ChatMsg::kMonsterParty:
    case ChatMsg::kMonsterYell:
    case ChatMsg::kMonsterWhisper:
    case ChatMsg::kMonsterEmote:
    case ChatMsg::kRaidBossEmote:
    case ChatMsg::kRaidBossWhisper:
    case ChatMsg::kBattlenet:
      return true;
    default:
      return false;
  }
}

constexpr bool IsBgSystemMessage(ChatMsg type) {
  return type == ChatMsg::kBgSystemNeutral ||
         type == ChatMsg::kBgSystemAlliance ||
         type == ChatMsg::kBgSystemHorde;
}

constexpr bool IsAchievementMessage(ChatMsg type) {
  return type == ChatMsg::kAchievement ||
         type == ChatMsg::kGuildAchievement;
}

constexpr bool ChatTypeNeedsTarget(ChatMsg type) {
  return type == ChatMsg::kWhisper;
}

constexpr bool ChatTypeNeedsChannel(ChatMsg type) {
  return type == ChatMsg::kChannel;
}

inline const char* GetLanguageName(Language lang) {
  switch (lang) {
    case Language::kUniversal:     return "";
    case Language::kOrcish:        return "Orcish";
    case Language::kDarnassian:    return "Darnassian";
    case Language::kTaurahe:       return "Taurahe";
    case Language::kDwarvish:      return "Dwarvish";
    case Language::kCommon:        return "Common";
    case Language::kDemonic:       return "Demonic";
    case Language::kTitan:         return "Titan";
    case Language::kThalassian:    return "Thalassian";
    case Language::kDraconic:      return "Draconic";
    case Language::kKalimag:       return "Kalimag";
    case Language::kGnomish:       return "Gnomish";
    case Language::kTroll:         return "Troll";
    case Language::kGutterspeak:   return "Gutterspeak";
    case Language::kDraenei:       return "Draenei";
    case Language::kZombie:        return "Zombie";
    case Language::kGnomishBinary: return "Gnomish Binary";
    case Language::kGoblinBinary:  return "Goblin Binary";
    case Language::kAddon:         return "";
    default:                       return "";
  }
}

inline bool ChatTypeStringToID(const char* str, ChatMsg* outType) {
  struct Entry { const char* name; ChatMsg id; };
  static constexpr Entry kTable[] = {
    {"SAY",              ChatMsg::kSay},
    {"PARTY",            ChatMsg::kParty},
    {"RAID",             ChatMsg::kRaid},
    {"GUILD",            ChatMsg::kGuild},
    {"OFFICER",          ChatMsg::kOfficer},
    {"YELL",             ChatMsg::kYell},
    {"WHISPER",          ChatMsg::kWhisper},
    {"EMOTE",            ChatMsg::kEmote},
    {"CHANNEL",          ChatMsg::kChannel},
    {"AFK",              ChatMsg::kAfk},
    {"DND",              ChatMsg::kDnd},
    {"RAID_WARNING",     ChatMsg::kRaidWarning},
    {"BATTLEGROUND",     ChatMsg::kBattleground},
    {"BN",               ChatMsg::kBattlenet},
    {"BN_WHISPER",       ChatMsg::kBnWhisper},
    {"BN_WHISPER_INFORM",ChatMsg::kBnWhisperInform},
    {"BN_CONVERSATION",  ChatMsg::kBnConversation},
  };
  for (const auto& e : kTable) {
    if (openwow::core::SStrCmpNoCase(e.name, str, 0x7FFFFFFFu) == 0) {
      *outType = e.id;
      return true;
    }
  }
  return false;
}

}

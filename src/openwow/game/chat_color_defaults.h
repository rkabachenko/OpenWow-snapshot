#pragma once

#include "openwow/game/chat_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace openwow::game {

struct BuiltinChatColorDefault {
  std::string_view token;
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  bool color_name_by_class;
};

inline constexpr std::array<BuiltinChatColorDefault, 62> kBuiltinChatColorDefaults{{
    {"SYSTEM", 255, 255, 0, false},
    {"SAY", 255, 255, 255, false},
    {"PARTY", 170, 170, 255, false},
    {"RAID", 255, 127, 0, false},
    {"GUILD", 64, 255, 64, false},
    {"OFFICER", 64, 192, 64, false},
    {"YELL", 255, 64, 64, false},
    {"WHISPER", 255, 128, 255, false},
    {"WHISPER_FOREIGN", 255, 128, 255, false},
    {"WHISPER_INFORM", 255, 128, 255, false},
    {"EMOTE", 255, 128, 64, false},
    {"TEXT_EMOTE", 255, 128, 64, false},
    {"MONSTER_SAY", 255, 255, 159, false},
    {"MONSTER_PARTY", 170, 170, 255, false},
    {"MONSTER_YELL", 255, 64, 64, false},
    {"MONSTER_WHISPER", 255, 181, 235, false},
    {"MONSTER_EMOTE", 255, 128, 64, false},
    {"CHANNEL", 255, 192, 192, false},
    {"CHANNEL_JOIN", 192, 128, 128, false},
    {"CHANNEL_LEAVE", 192, 128, 128, false},
    {"CHANNEL_LIST", 192, 128, 128, false},
    {"CHANNEL_NOTICE", 192, 192, 192, false},
    {"CHANNEL_NOTICE_USER", 192, 192, 192, false},
    {"AFK", 255, 128, 255, false},
    {"DND", 255, 128, 255, false},
    {"IGNORED", 255, 0, 0, false},
    {"SKILL", 85, 85, 255, false},
    {"LOOT", 0, 170, 0, false},
    {"MONEY", 255, 255, 0, false},
    {"OPENING", 128, 128, 255, false},
    {"TRADESKILLS", 255, 255, 255, false},
    {"PET_INFO", 128, 128, 255, false},
    {"COMBAT_MISC_INFO", 128, 128, 255, false},
    {"COMBAT_XP_GAIN", 111, 111, 255, false},
    {"COMBAT_HONOR_GAIN", 224, 202, 10, false},
    {"COMBAT_FACTION_CHANGE", 128, 128, 255, false},
    {"BG_SYSTEM_NEUTRAL", 255, 120, 10, false},
    {"BG_SYSTEM_ALLIANCE", 0, 174, 239, false},
    {"BG_SYSTEM_HORDE", 255, 0, 0, false},
    {"RAID_LEADER", 255, 72, 9, false},
    {"RAID_WARNING", 255, 72, 0, false},
    {"RAID_BOSS_EMOTE", 255, 221, 0, false},
    {"RAID_BOSS_WHISPER", 255, 221, 0, false},
    {"FILTERED", 255, 0, 0, false},
    {"BATTLEGROUND", 255, 127, 0, false},
    {"BATTLEGROUND_LEADER", 255, 219, 183, false},
    {"RESTRICTED", 255, 0, 0, false},
    {"BATTLENET", 255, 255, 255, false},
    {"ACHIEVEMENT", 255, 255, 0, false},
    {"GUILD_ACHIEVEMENT", 64, 255, 64, false},
    {"ARENA_POINTS", 255, 255, 255, false},
    {"PARTY_LEADER", 118, 200, 255, false},
    {"TARGETICONS", 255, 255, 0, false},
    {"BN_WHISPER", 0, 255, 246, false},
    {"BN_WHISPER_INFORM", 0, 255, 246, false},
    {"BN_CONVERSATION", 0, 177, 240, false},
    {"BN_CONVERSATION_NOTICE", 0, 177, 240, false},
    {"BN_CONVERSATION_LIST", 0, 177, 240, false},
    {"BN_INLINE_TOAST_ALERT", 130, 197, 255, false},
    {"BN_INLINE_TOAST_BROADCAST", 130, 197, 255, false},
    {"BN_INLINE_TOAST_BROADCAST_INFORM", 130, 197, 255, false},
    {"BN_INLINE_TOAST_CONVERSATION", 130, 197, 255, false},
}};

inline constexpr std::size_t kDynamicChatTypeCount = 10;
inline constexpr std::uint8_t kDynamicChatDefaultColor = 255;
inline constexpr bool kDynamicChatDefaultColorNameByClass = true;

constexpr const BuiltinChatColorDefault* GetBuiltinChatColorDefault(
    ChatMsg type) noexcept {
  const auto index = static_cast<std::size_t>(type);
  return index < kBuiltinChatColorDefaults.size()
             ? &kBuiltinChatColorDefaults[index]
             : nullptr;
}

constexpr float DecodeChatColorByte(std::uint8_t value) noexcept {
  return static_cast<float>(value) / 255.0f;
}

}


#pragma once

#include "openwow/game/chat_types.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::game {

namespace PlayerFlagBits {
inline constexpr std::uint32_t kAFK = 0x02;
inline constexpr std::uint32_t kDND = 0x04;
inline constexpr std::uint32_t kGM = 0x08;
inline constexpr std::uint32_t kPvPDesired = 0x200;
inline constexpr std::uint32_t kGhost = 0x400;
inline constexpr std::uint32_t kResting = 0x20;
inline constexpr std::uint32_t kHideHelm = 0x400;
inline constexpr std::uint32_t kHideCloak = 0x800;
inline constexpr std::uint32_t kPvPToggle = 0x40000;
inline constexpr std::uint32_t kNoXPGain = 0x20000;
inline constexpr std::uint32_t kHiddenGM = 0x8000;
}

inline bool HasPlayerChatTag(const ChatTag value, const ChatTag mask) {
  return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(mask)) != 0;
}

inline std::string ResolvePlayerChatTagToken(const ChatTag tag) {
  if (HasPlayerChatTag(tag, ChatTag::kDev)) {
    return "DEV";
  }
  if (HasPlayerChatTag(tag, ChatTag::kGm)) {
    return "GM";
  }
  if (HasPlayerChatTag(tag, ChatTag::kDnd)) {
    return "DND";
  }
  if (HasPlayerChatTag(tag, ChatTag::kAfk)) {
    return "AFK";
  }
  return {};
}

inline bool IsGameMasterChatTagToken(const std::string_view tag) {
  return tag == "GM" || tag == "DEV";
}

}

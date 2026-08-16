
#pragma once

#include <cstdint>

namespace openwow::game {

namespace MirrorSection {
inline constexpr std::uint8_t kUnit   = 3;
inline constexpr std::uint8_t kPlayer = 4;
}

inline constexpr std::uint32_t kUnitFieldEventSlotCount = 142;

inline constexpr std::uint32_t kEventInventoryChanged  = 147;
inline constexpr std::uint32_t kEventQuestLogChanged   = 460;

inline constexpr std::uint32_t kPlayerInventoryOffset = 0x21C;
inline constexpr std::uint32_t kPlayerInventorySize   = 152;

inline constexpr std::uint32_t kPlayerQuestLogOffset       = 0x28;
inline constexpr std::uint32_t kPlayerQuestLogSlotCount    = 25;
inline constexpr std::uint32_t kPlayerQuestLogSlotSize     = 5 * sizeof(std::uint32_t);

int PlayerUnitFieldChangedCallback(std::uint32_t guid_low,
                                   std::uint32_t guid_high,
                                   std::uint32_t byte_offset);

int PlayerInventoryChangedCallback(std::uint32_t guid_low,
                                   std::uint32_t guid_high);

int PlayerQuestLogChangedCallback(std::uint32_t guid_low,
                                  std::uint32_t guid_high);

const char* GetUnitFieldEventName(std::uint32_t field_index);

void Player_RegisterUnitFieldEventCallbacks(void* player_obj);

void Player_UnregisterUnitFieldEventCallbacks(void* player_obj);

}

#pragma once

#include <cstdint>
#include <string>

namespace openwow::net::wotlk {

namespace SpellTargetFlag {
  inline constexpr std::uint32_t None       = 0x00000000;
  inline constexpr std::uint32_t Self       = 0x00000000;
  inline constexpr std::uint32_t Unit       = 0x00000002;
  inline constexpr std::uint32_t Item       = 0x00000010;
  inline constexpr std::uint32_t SourceLoc  = 0x00000020;
  inline constexpr std::uint32_t DestLoc    = 0x00000040;
  inline constexpr std::uint32_t Corpse     = 0x00000200;
}

struct SpellCastRequest {
  std::uint32_t spell_id{0};
};

struct SpellCastTargetedRequest {
  std::uint32_t spell_id{0};
  std::uint64_t target_guid{0};
  std::uint32_t target_flags{SpellTargetFlag::Unit};
  float dest_x{0.0f};
  float dest_y{0.0f};
  float dest_z{0.0f};
};

struct SpellStopRequest {
  std::uint32_t spell_id{0};
  std::uint32_t cast_id{0};
};

struct CancelAuraRequest {
  std::uint32_t spell_id{0};
};

struct CancelChannellingRequest {
  std::uint32_t spell_id{0};
};

struct UseItemRequest {
  std::uint64_t item_guid{0};
  std::uint8_t  bag_index{0};
  std::uint8_t  slot_index{0};
  std::uint32_t spell_id{0};
  std::uint64_t target_guid{0};
};

struct CancelAutoRepeatRequest {};

SpellCastRequest           BuildSpellCastRequest(std::uint32_t spell_id);
SpellCastTargetedRequest   BuildSpellCastTargetedRequest(std::uint32_t spell_id,
                                                         std::uint64_t target_guid,
                                                         std::uint32_t flags = SpellTargetFlag::Unit);
SpellCastTargetedRequest   BuildSpellCastAtLocationRequest(std::uint32_t spell_id,
                                                           float x, float y, float z);
SpellStopRequest           BuildSpellStopRequest(std::uint32_t spell_id,
                                                  std::uint32_t cast_id = 0);
CancelAuraRequest          BuildCancelAuraRequest(std::uint32_t spell_id);
CancelChannellingRequest   BuildCancelChannellingRequest(std::uint32_t spell_id);
UseItemRequest             BuildUseItemRequest(std::uint64_t item_guid,
                                                std::uint8_t bag, std::uint8_t slot,
                                                std::uint32_t spell_id = 0,
                                                std::uint64_t target = 0);

}

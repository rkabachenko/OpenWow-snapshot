#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::net::wotlk {

struct RealmConnectionCharEquipSlot {
  std::uint32_t display_id = 0;
  std::uint8_t inventory_type = 0;
  std::uint32_t enchant_aura = 0;
};

struct RealmConnectionCharEnumEntry {
  std::uint64_t guid = 0;
  std::string name;
  std::uint8_t race = 0;
  std::uint8_t char_class = 0;
  std::uint8_t gender = 0;
  std::uint8_t skin = 0;
  std::uint8_t face = 0;
  std::uint8_t hair_style = 0;
  std::uint8_t hair_color = 0;
  std::uint8_t facial_hair = 0;
  std::uint8_t level = 0;
  std::uint32_t zone_id = 0;
  std::uint32_t map_id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  std::uint32_t guild_id = 0;
  std::uint32_t char_flags = 0;
  std::uint32_t customize_flags = 0;
  std::uint8_t first_login = 0;
  std::uint32_t pet_display_id = 0;
  std::uint32_t pet_level = 0;
  std::uint32_t pet_family = 0;
  std::array<RealmConnectionCharEquipSlot, 23> equipment{};
};

struct RealmConnectionCharEnumPayload {
  std::vector<RealmConnectionCharEnumEntry> characters;
  std::array<std::uint32_t, 10> trailing_u32s{};
  bool has_trailing_u32s = false;
  bool truncated_count = false;
};

struct RealmConnectionAuthResponsePayload {
  std::uint8_t result_code = 0;
  bool authenticated = false;
  bool has_account_info = false;
  std::uint32_t billing_time = 0;
  std::uint8_t billing_flags = 0;
  std::uint32_t billing_rested = 0;
  std::uint8_t expansion_level = 0;
  bool has_queue_position = false;
  std::uint32_t queue_position = 0;
  std::uint8_t free_character_migration = 0;
};

struct RealmConnectionLogoutResponsePayload {
  std::uint32_t result = 0;
  std::uint8_t instant_flag = 0;
};

bool ParseRealmConnectionCharEnum(const std::uint8_t* data,
                                  std::size_t size,
                                  RealmConnectionCharEnumPayload& out,
                                  std::size_t* consumed = nullptr);

bool ParseRealmConnectionAuthResponseWithFallback(
    const std::uint8_t* data,
    std::size_t size,
    std::uint8_t fallback_result_code,
    RealmConnectionAuthResponsePayload& out,
    std::size_t* consumed = nullptr);
bool ParseRealmConnectionAuthResponse(
    const std::uint8_t* data,
    std::size_t size,
    RealmConnectionAuthResponsePayload& out,
    std::size_t* consumed = nullptr);
void ParseRealmConnectionLogoutResponse(
    const std::uint8_t* data,
    std::size_t size,
    RealmConnectionLogoutResponsePayload& out,
    std::size_t* consumed = nullptr);

}

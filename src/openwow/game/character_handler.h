
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/packet_reader.h"
#include "openwow/net/wotlk/main_thread_packet_dispatcher.h"

namespace openwow::game {

enum class CharResult : std::uint8_t {
  kSuccess = 0x00,
  kError = 0x01,
  kFailed = 0x02,
  kInProgress = 0x03,
  kNameInUse = 0x32,

};

enum class LoginFailureReason : std::uint8_t {
  kFailed = 0x00,
  kNoWorld = 0x01,
  kDuplicateCharacter = 0x02,
  kNoInstances = 0x03,
  kDisabled = 0x04,
  kNoCharacter = 0x05,
  kLockedForTransfer = 0x06,
  kLockedByBilling = 0x07,
};

struct CharEquipSlot {
  std::uint32_t display_id = 0;
  std::uint8_t inv_type = 0;
  std::uint32_t enchant_aura = 0;
};

static constexpr int kCharEquipSlotCount = 23;

struct CharEnumEntry {
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
  std::array<CharEquipSlot, kCharEquipSlotCount> equipment{};
};

struct CharRenameResult {
  std::uint8_t result = 0;
  std::uint64_t guid = 0;
  std::string name;
};

struct CharCustomizeResult {
  std::uint8_t result = 0;
  std::uint64_t guid = 0;
  std::string name;
  std::uint8_t gender = 0;
  std::uint8_t skin = 0;
  std::uint8_t face = 0;
  std::uint8_t hair_style = 0;
  std::uint8_t hair_color = 0;
  std::uint8_t facial_hair = 0;
};

struct CharFactionChangeResult {
  std::uint8_t result = 0;
  std::uint64_t guid = 0;
  std::string name;
  std::uint8_t race = 0;
  std::uint8_t gender = 0;
  std::uint8_t skin = 0;
  std::uint8_t face = 0;
  std::uint8_t hair_style = 0;
  std::uint8_t hair_color = 0;
  std::uint8_t facial_hair = 0;
};

class CharacterHandler {
 public:
  void BindWorldPacketHandlers(
      net::wotlk::MainThreadPacketDispatcher& dispatcher);

  bool HandleCharCreate(const std::uint8_t* data, std::size_t len);

  bool HandleCharEnum(const std::uint8_t* data, std::size_t len);

  bool HandleCharDelete(const std::uint8_t* data, std::size_t len);

  bool HandleLoginFailed(const std::uint8_t* data, std::size_t len);

  bool HandleInvalidatePlayer(const std::uint8_t* data, std::size_t len);

  bool HandleCharRename(const std::uint8_t* data, std::size_t len);

  bool HandleCharCustomize(const std::uint8_t* data, std::size_t len);

  bool HandleCharFactionChange(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] std::uint8_t last_create_result() const { return last_create_result_; }
  [[nodiscard]] std::uint8_t last_delete_result() const { return last_delete_result_; }
  [[nodiscard]] std::uint8_t last_login_failure() const { return last_login_failure_; }
  [[nodiscard]] const std::vector<CharEnumEntry>& characters() const { return characters_; }
  [[nodiscard]] const std::array<std::uint32_t, 10>& char_enum_trailing_u32s() const {
    return char_enum_trailing_u32s_;
  }
  [[nodiscard]] bool last_char_enum_success() const { return last_char_enum_success_; }
  [[nodiscard]] std::uint64_t invalidated_player_guid() const { return invalidated_guid_; }
  [[nodiscard]] const std::optional<CharRenameResult>& last_char_rename() const {
    return last_char_rename_;
  }
  [[nodiscard]] const std::optional<CharCustomizeResult>& last_char_customize() const {
    return last_char_customize_;
  }
  [[nodiscard]] const std::optional<CharFactionChangeResult>& last_char_faction_change() const {
    return last_char_faction_change_;
  }

  void Clear();

 private:
  std::uint8_t last_create_result_ = 0;
  std::uint8_t last_delete_result_ = 0;
  std::uint8_t last_login_failure_ = 0;
  std::vector<CharEnumEntry> characters_;
  std::array<std::uint32_t, 10> char_enum_trailing_u32s_{};
  bool last_char_enum_success_ = false;
  std::uint64_t invalidated_guid_{0};
  std::optional<CharRenameResult> last_char_rename_;
  std::optional<CharCustomizeResult> last_char_customize_;
  std::optional<CharFactionChangeResult> last_char_faction_change_;
  std::vector<net::wotlk::MainThreadPacketDispatcher::Registration>
      packet_registrations_;
};

}

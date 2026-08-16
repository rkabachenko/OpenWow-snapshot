
#include "openwow/game/character_handler.h"

#include "openwow/network/protocol/wotlk/world_packet.h"

#include "openwow/net/wotlk/protocol/realm_connection_packets.h"
#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::game {

void CharacterHandler::BindWorldPacketHandlers(
    net::wotlk::MainThreadPacketDispatcher& dispatcher) {
  using Opcode = net::wotlk::Opcode;
  packet_registrations_.clear();
  const auto bind =
      [this, &dispatcher](
          const Opcode opcode,
          bool (CharacterHandler::*handler)(const std::uint8_t*, std::size_t)) {
        packet_registrations_.push_back(dispatcher.Register(
            opcode, "character",
            [this, handler](const net::wotlk::WorldPacket& packet) {
              return (this->*handler)(packet.payload.data(),
                                      packet.payload.size());
            }));
      };
  bind(Opcode::SMSG_CHAR_CREATE, &CharacterHandler::HandleCharCreate);
  bind(Opcode::SMSG_CHAR_ENUM, &CharacterHandler::HandleCharEnum);
  bind(Opcode::SMSG_CHAR_DELETE, &CharacterHandler::HandleCharDelete);
  bind(Opcode::SMSG_CHARACTER_LOGIN_FAILED,
       &CharacterHandler::HandleLoginFailed);
  bind(Opcode::SMSG_CHAR_RENAME, &CharacterHandler::HandleCharRename);
  bind(Opcode::SMSG_CHAR_CUSTOMIZE, &CharacterHandler::HandleCharCustomize);
  bind(Opcode::SMSG_CHAR_FACTION_CHANGE,
       &CharacterHandler::HandleCharFactionChange);
}

bool CharacterHandler::HandleCharCreate(const std::uint8_t* data,
                                        std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU8(last_create_result_)) return false;
  return true;
}

bool CharacterHandler::HandleCharEnum(const std::uint8_t* data,
                                      std::size_t len) {
  net::wotlk::RealmConnectionCharEnumPayload payload;
  const bool ok =
      net::wotlk::ParseRealmConnectionCharEnum(data, len, payload);
  last_char_enum_success_ = ok;
  characters_.clear();
  if (ok && payload.has_trailing_u32s) {
    char_enum_trailing_u32s_ = payload.trailing_u32s;
  }
  if (!ok) {
    if (payload.truncated_count) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "CharEnum: count exceeded hard cap of 10");
    }
    return false;
  }

  characters_.reserve(payload.characters.size());
  for (const auto& src : payload.characters) {
    CharEnumEntry dst{};
    dst.guid = src.guid;
    dst.name = src.name;
    dst.race = src.race;
    dst.char_class = src.char_class;
    dst.gender = src.gender;
    dst.skin = src.skin;
    dst.face = src.face;
    dst.hair_style = src.hair_style;
    dst.hair_color = src.hair_color;
    dst.facial_hair = src.facial_hair;
    dst.level = src.level;
    dst.zone_id = src.zone_id;
    dst.map_id = src.map_id;
    dst.x = src.x;
    dst.y = src.y;
    dst.z = src.z;
    dst.guild_id = src.guild_id;
    dst.char_flags = src.char_flags;
    dst.customize_flags = src.customize_flags;
    dst.first_login = src.first_login;
    dst.pet_display_id = src.pet_display_id;
    dst.pet_level = src.pet_level;
    dst.pet_family = src.pet_family;
    for (int slot = 0; slot < kCharEquipSlotCount; ++slot) {
      dst.equipment[slot].display_id = src.equipment[slot].display_id;
      dst.equipment[slot].inv_type = src.equipment[slot].inventory_type;
      dst.equipment[slot].enchant_aura = src.equipment[slot].enchant_aura;
    }
    characters_.push_back(std::move(dst));
  }

  return true;
}

bool CharacterHandler::HandleCharDelete(const std::uint8_t* data,
                                        std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU8(last_delete_result_)) return false;
  return true;
}

bool CharacterHandler::HandleLoginFailed(const std::uint8_t* data,
                                         std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU8(last_login_failure_)) return false;
  return true;
}

void CharacterHandler::Clear() {
  last_create_result_ = 0;
  last_delete_result_ = 0;
  last_login_failure_ = 0;
  characters_.clear();
  char_enum_trailing_u32s_.fill(0);
  last_char_enum_success_ = false;
  invalidated_guid_ = 0;
  last_char_rename_.reset();
  last_char_customize_.reset();
  last_char_faction_change_.reset();
}

bool CharacterHandler::HandleInvalidatePlayer(const std::uint8_t* data,
                                              std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU64(invalidated_guid_)) return false;
  return true;
}

bool CharacterHandler::HandleCharRename(const std::uint8_t* data,
                                        std::size_t len) {
  PacketReader r(data, len);
  CharRenameResult res{};
  if (!r.ReadU8(res.result)) return false;
  if (res.result == 0) {
    if (!r.ReadU64(res.guid)) return false;
    if (!r.ReadCString(res.name)) return false;
  }
  last_char_rename_ = std::move(res);
  return true;
}

bool CharacterHandler::HandleCharCustomize(const std::uint8_t* data,
                                           std::size_t len) {
  PacketReader r(data, len);
  CharCustomizeResult res{};
  if (!r.ReadU8(res.result)) return false;
  if (res.result == 0) {
    if (!r.ReadU64(res.guid)) return false;
    if (!r.ReadCString(res.name)) return false;
    if (!r.ReadU8(res.gender)) return false;
    if (!r.ReadU8(res.skin)) return false;
    if (!r.ReadU8(res.face)) return false;
    if (!r.ReadU8(res.hair_style)) return false;
    if (!r.ReadU8(res.hair_color)) return false;
    if (!r.ReadU8(res.facial_hair)) return false;
  }
  last_char_customize_ = std::move(res);
  return true;
}

bool CharacterHandler::HandleCharFactionChange(const std::uint8_t* data,
                                               std::size_t len) {
  PacketReader r(data, len);
  CharFactionChangeResult res{};
  if (!r.ReadU8(res.result)) return false;
  if (res.result == 0) {
    if (!r.ReadU64(res.guid)) return false;
    if (!r.ReadCString(res.name)) return false;
    if (!r.ReadU8(res.race)) return false;
    if (!r.ReadU8(res.gender)) return false;
    if (!r.ReadU8(res.skin)) return false;
    if (!r.ReadU8(res.face)) return false;
    if (!r.ReadU8(res.hair_style)) return false;
    if (!r.ReadU8(res.hair_color)) return false;
    if (!r.ReadU8(res.facial_hair)) return false;
  }
  last_char_faction_change_ = std::move(res);
  return true;
}

}

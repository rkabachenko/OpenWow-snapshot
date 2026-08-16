
#include "openwow/net/wotlk/glue_packet_handlers.h"

#include "openwow/net/client_services.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/ui/glue/glue_lua_value.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cstring>

namespace openwow::net::wotlk {

using openwow::ui::glue::MakeLuaString;
using openwow::ui::glue::MakeLuaNumber;
using openwow::ui::glue::MakeLuaBool;
using openwow::ui::glue::GlueLuaValue;

namespace {

constexpr std::size_t kRealmSplitDateBufferBytes = 64;
constexpr std::size_t kCharacterServiceNameBufferBytes = 0x30;

class PacketReader {
 public:
  explicit PacketReader(const std::vector<std::uint8_t>& data)
      : data_(data.data()), size_(data.size()) {}

  [[nodiscard]] bool CanRead(std::size_t bytes) const { return pos_ + bytes <= size_; }
  [[nodiscard]] std::size_t Remaining() const { return size_ - pos_; }

  bool ReadU8(std::uint8_t& out) {
    if (!CanRead(1)) return false;
    out = data_[pos_++];
    return true;
  }

  bool ReadU32(std::uint32_t& out) {
    if (!CanRead(4)) return false;
    out = static_cast<std::uint32_t>(data_[pos_]) |
          (static_cast<std::uint32_t>(data_[pos_ + 1]) << 8) |
          (static_cast<std::uint32_t>(data_[pos_ + 2]) << 16) |
          (static_cast<std::uint32_t>(data_[pos_ + 3]) << 24);
    pos_ += 4;
    return true;
  }

  bool ReadU64(std::uint64_t& out) {
    if (!CanRead(8)) return false;
    out = 0;
    for (int i = 0; i < 8; ++i) {
      out |= static_cast<std::uint64_t>(data_[pos_ + i]) << (8 * i);
    }
    pos_ += 8;
    return true;
  }

  bool ReadCString(std::string& out, std::size_t max_len = 256) {
    out.clear();
    std::size_t count = 0;
    while (pos_ < size_ && count < max_len) {
      const char c = static_cast<char>(data_[pos_++]);
      ++count;
      if (c == '\0') return true;
      out += c;
    }
    return false;
  }

  bool ReadFixedString(std::string& out, std::size_t len) {
    if (!CanRead(len)) return false;
    out.assign(reinterpret_cast<const char*>(data_ + pos_), len);

    const auto nul_pos = out.find('\0');
    if (nul_pos != std::string::npos) out.resize(nul_pos);
    pos_ += len;
    return true;
  }

 private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_{0};
};

[[nodiscard]] std::uint32_t ReadLeU32Unchecked(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0])
       | (static_cast<std::uint32_t>(data[1]) << 8)
       | (static_cast<std::uint32_t>(data[2]) << 16)
       | (static_cast<std::uint32_t>(data[3]) << 24);
}

[[nodiscard]] RealmSplitResult DecodeRealmSplitForGlue(const WorldPacket& pkt) {
  RealmSplitResult result;
  const auto& payload = pkt.payload;

  if (payload.size() >= sizeof(std::uint32_t)) {
    result.realm_id = ReadLeU32Unchecked(payload.data());
  }
  if (payload.size() >= sizeof(std::uint32_t) * 2) {
    result.split_state = ReadLeU32Unchecked(payload.data() + sizeof(std::uint32_t));
  }
  if (payload.size() <= sizeof(std::uint32_t) * 2) {
    return result;
  }

  const std::size_t date_offset = sizeof(std::uint32_t) * 2;
  const std::size_t available =
      std::min(kRealmSplitDateBufferBytes, payload.size() - date_offset);
  for (std::size_t i = 0; i < available; ++i) {
    if (payload[date_offset + i] == 0) {
      result.date.assign(reinterpret_cast<const char*>(payload.data() + date_offset), i);
      return result;
    }
  }

  result.date.clear();
  return result;
}

}

const char* CharResultGenericErrorKey(std::uint8_t code) {

  const char* const key = ClientServices::GetResultString(code);
  return key != nullptr && key[0] != '\0' ? key : "CHAR_CREATE_ERROR";
}

const char* CharFactionChangeErrorKey(std::uint8_t code) {

  switch (code) {
    case 0x00: return nullptr;
    case 0x32: return nullptr;
    case 0x3D: return "CHAR_FACTION_CHANGE_STILL_IN_GUILD";
    case 0x3E: return "CHAR_FACTION_CHANGE_RACECLASS_RESTRICTED";
    case 0x3F: return "CHAR_FACTION_CHANGE_CHOOSE_RACE";
    case 0x40: return "CHAR_FACTION_CHANGE_ARENA_LEADER";
    case 0x41: return "CHAR_FACTION_CHANGE_DELETE_MAIL";
    case 0x42: return "CHAR_FACTION_CHANGE_SWAP_FACTION";
    case 0x43: return "CHAR_FACTION_CHANGE_RACE_ONLY";
    case 0x44: return "CHAR_FACTION_CHANGE_GOLD_LIMIT";
    case 0x45: return "CHAR_FACTION_CHANGE_FORCE_LOGIN";
    default:   return "CHAR_FACTION_CHANGE_FAILED";
  }
}

bool ParseCharRename(const WorldPacket& pkt, CharRenameResult& out) {

  PacketReader r(pkt.payload);
  if (!r.ReadU8(out.result_code)) return false;

  if (out.result_code == 0) {
    if (!r.ReadU64(out.guid)) return false;
    if (!r.ReadCString(out.new_name, kCharacterServiceNameBufferBytes)) return false;
  }
  return true;
}

bool ParseDeclinedNamesResult(const WorldPacket& pkt, DeclinedNamesResult& out) {

  PacketReader r(pkt.payload);
  if (!r.ReadU32(out.result_code)) return false;
  out.guid = 0;
  if (out.result_code == 0) {
    if (!r.ReadU64(out.guid)) return false;
  }
  return true;
}

bool ParseCharCustomize(const WorldPacket& pkt, CharCustomizeResult& out) {

  PacketReader r(pkt.payload);
  if (!r.ReadU8(out.result_code)) return false;

  if (out.result_code == 0) {
    if (!r.ReadU64(out.guid)) return false;
    if (!r.ReadCString(out.name, kCharacterServiceNameBufferBytes)) return false;
    if (!r.ReadU8(out.gender)) return false;
    if (!r.ReadU8(out.skin)) return false;
    if (!r.ReadU8(out.face)) return false;
    if (!r.ReadU8(out.hair_style)) return false;
    if (!r.ReadU8(out.hair_color)) return false;
    if (!r.ReadU8(out.facial_hair)) return false;
  }
  return true;
}

bool ParseRealmSplit(const WorldPacket& pkt, RealmSplitResult& out) {

  PacketReader r(pkt.payload);
  if (!r.ReadU32(out.realm_id)) return false;
  if (!r.ReadU32(out.split_state)) return false;
  if (!r.ReadCString(out.date, kRealmSplitDateBufferBytes)) return false;
  return true;
}

bool ParseKickReason(const WorldPacket& pkt, KickReasonResult& out) {

  PacketReader r(pkt.payload);
  if (!r.ReadU8(out.reason)) return false;

  out.has_token_seed_key = false;
  if (r.Remaining() >= out.token_seed_key.size()) {
    std::memcpy(out.token_seed_key.data(),
                pkt.payload.data() + 1,
                out.token_seed_key.size());
    out.has_token_seed_key = true;
  }
  return true;
}

bool ParseCharFactionChange(const WorldPacket& pkt, CharFactionChangeResult& out) {

  PacketReader r(pkt.payload);
  if (!r.ReadU8(out.result_code)) return false;

  if (out.result_code == 0) {
    if (!r.ReadU64(out.guid)) return false;
    if (!r.ReadCString(out.name, kCharacterServiceNameBufferBytes)) return false;
    if (!r.ReadU8(out.race)) return false;
    if (!r.ReadU8(out.gender)) return false;
    if (!r.ReadU8(out.skin)) return false;
    if (!r.ReadU8(out.face)) return false;
    if (!r.ReadU8(out.hair_style)) return false;
    if (!r.ReadU8(out.hair_color)) return false;
    if (!r.ReadU8(out.facial_hair)) return false;
  }
  return true;
}

void HandleCharRename(const WorldPacket& pkt, const GlueHandlerCallbacks& cb) {

  CharRenameResult result;
  if (!ParseCharRename(pkt, result)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "HandleCharRename: malformed packet");
    return;
  }

  if (cb.fire_event) {
    cb.fire_event("CLOSE_STATUS_DIALOG", {});
  }

  if (result.result_code != 0) {
    std::string error_text;
    if (result.result_code == CHAR_CREATE_NAME_IN_USE) {

      error_text = CharResultGenericErrorKey(CHAR_CREATE_NAME_IN_USE);
    } else {

      error_text = cb.get_global_string
          ? cb.get_global_string("CHAR_RENAME_FAILED")
          : std::string();
      if (error_text.empty()) {
        error_text = "CHAR_RENAME_FAILED";
      }
    }
    if (cb.fire_event) {
      cb.fire_event("FORCE_RENAME_CHARACTER", {MakeLuaString(error_text)});
    }
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        std::string("HandleCharRename: failed (code=")
            + std::to_string(result.result_code) + ", text=" + error_text + ")");
    return;
  }

  if (cb.update_character) {
    cb.update_character(result.guid, result.new_name,
                        0, 0, 0, 0, 0, 0, 0);
  }

  if (cb.fire_event) {
    cb.fire_event("CHARACTER_LIST_UPDATE", {});
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
      "HandleCharRename: success, guid=" + std::to_string(result.guid)
          + " name=" + result.new_name);
}

void HandleDeclinedNamesResult(const WorldPacket& pkt, const GlueHandlerCallbacks& cb) {

  DeclinedNamesResult result;
  if (!ParseDeclinedNamesResult(pkt, result)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "HandleDeclinedNamesResult: malformed packet");
    return;
  }

  if (cb.fire_event) {
    cb.fire_event("CLOSE_STATUS_DIALOG", {});
  }

  if (result.result_code != 0) {
    if (cb.fire_event) {
      cb.fire_event("FORCE_DECLINE_CHARACTER", {MakeLuaString("CHAR_DECLINE_FAILED")});
    }
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        "HandleDeclinedNamesResult: failed (code="
            + std::to_string(result.result_code) + ")");
    return;
  }

  bool selected_character_declined = false;
  if (cb.mark_declined_character) {
    selected_character_declined = cb.mark_declined_character(result.guid);
  }
  if (selected_character_declined && cb.retry_enter_world) {
    cb.retry_enter_world();
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
      "HandleDeclinedNamesResult: success, guid=" + std::to_string(result.guid));
}

void HandleCharCustomize(const WorldPacket& pkt, const GlueHandlerCallbacks& cb) {

  CharCustomizeResult result;
  if (!ParseCharCustomize(pkt, result)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "HandleCharCustomize: malformed packet");
    return;
  }

  if (result.result_code != 0) {
    const char* error_key = (result.result_code == 0x32)
        ? CharResultGenericErrorKey(0x32)
        : "CHAR_CUSTOMIZE_FAILED";
    if (cb.fire_event) {
      cb.fire_event("OPEN_STATUS_DIALOG", {MakeLuaString("OKAY"), MakeLuaString(error_key)});
    }
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        std::string("HandleCharCustomize: failed (code=")
            + std::to_string(result.result_code) + ", key=" + error_key + ")");
    return;
  }

  if (cb.update_character) {
    cb.update_character(result.guid, result.name,
                        0, result.gender,
                        result.skin, result.face,
                        result.hair_style, result.hair_color,
                        result.facial_hair);
  }

  if (cb.fire_event) {
    cb.fire_event("CHARACTER_LIST_UPDATE", {});
    cb.fire_event("CLOSE_STATUS_DIALOG", {});
    cb.fire_event("SET_GLUE_SCREEN", {MakeLuaString("charselect")});
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
      "HandleCharCustomize: success, guid=" + std::to_string(result.guid)
          + " name=" + result.name);
}

void HandleRealmSplit(const WorldPacket& pkt, const GlueHandlerCallbacks& cb) {

  const RealmSplitResult result = DecodeRealmSplitForGlue(pkt);

  if (cb.fire_event) {
    cb.fire_event("SERVER_SPLIT_NOTICE", {
        MakeLuaNumber(static_cast<double>(result.realm_id)),
        MakeLuaNumber(static_cast<double>(result.split_state)),
        MakeLuaString(result.date)
    });
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
      "HandleRealmSplit: realm=" + std::to_string(result.realm_id)
          + " state=" + std::to_string(result.split_state)
          + " date=" + result.date);
}

void HandleKickReason(const WorldPacket& pkt, const GlueHandlerCallbacks& cb) {

  KickReasonResult result;
  if (!ParseKickReason(pkt, result)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "HandleKickReason: malformed packet");
    return;
  }

  const bool glue_active = static_cast<bool>(cb.fire_event)
                        || static_cast<bool>(cb.update_character)
                        || static_cast<bool>(cb.request_disconnect);
  const bool entering_world = cb.is_entering_world ? cb.is_entering_world() : false;
  if (glue_active && !entering_world && result.has_token_seed_key) {
    const std::string token_seed =
        cb.get_global_string ? cb.get_global_string("TOKEN_SEED") : std::string();
    if (cb.process_legacy_token_seed) {
      cb.process_legacy_token_seed(token_seed,
                                   result.token_seed_key.data());
    }
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
        "HandleKickReason: consumed TOKEN_SEED payload");
    return;
  }

  if (cb.request_disconnect) {
    cb.request_disconnect(result.reason);
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
      "HandleKickReason: deferred disconnect reason=" + std::to_string(result.reason));
}

void HandleCharFactionChange(const WorldPacket& pkt, const GlueHandlerCallbacks& cb) {

  CharFactionChangeResult result;
  if (!ParseCharFactionChange(pkt, result)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "HandleCharFactionChange: malformed packet");
    return;
  }

  if (result.result_code == 0) {

    if (cb.update_character) {
      cb.update_character(result.guid, result.name,
                          result.race, result.gender,
                          result.skin, result.face,
                          result.hair_style, result.hair_color,
                          result.facial_hair);
    }

    if (cb.fire_event) {
      cb.fire_event("CHARACTER_LIST_UPDATE", {});
      cb.fire_event("CLOSE_STATUS_DIALOG", {});
      cb.fire_event("SET_GLUE_SCREEN", {MakeLuaString("charselect")});
    }

    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
        "HandleCharFactionChange: success, guid=" + std::to_string(result.guid)
            + " name=" + result.name + " race=" + std::to_string(result.race));
    return;
  }

  const char* error_key = nullptr;
  if (result.result_code == 0x32) {
    error_key = CharResultGenericErrorKey(0x32);
  } else {
    error_key = CharFactionChangeErrorKey(result.result_code);
    if (error_key == nullptr) {
      error_key = "CHAR_FACTION_CHANGE_FAILED";
    }
  }

  if (cb.fire_event) {
    cb.fire_event("OPEN_STATUS_DIALOG", {MakeLuaString("OKAY"), MakeLuaString(error_key)});
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
      std::string("HandleCharFactionChange: failed (code=")
          + std::to_string(result.result_code) + ", key=" + error_key + ")");
}

}

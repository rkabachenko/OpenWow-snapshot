#include "openwow/net/wotlk/protocol/realm_connection_packets.h"

#include <cstring>
#include <utility>

namespace openwow::net::wotlk {
namespace {

class ByteCursor {
 public:
  ByteCursor(const std::uint8_t* data, std::size_t size)
      : data_(data), size_(size) {}

  [[nodiscard]] std::size_t Remaining() const { return size_ - pos_; }
  [[nodiscard]] bool AtEnd() const { return pos_ == size_; }
  [[nodiscard]] bool Has(std::size_t count) const { return pos_ + count <= size_; }
  [[nodiscard]] std::size_t Position() const { return pos_; }

  bool ReadU8(std::uint8_t& out) {
    if (!Has(1)) return false;
    out = data_[pos_++];
    return true;
  }

  bool ReadU32(std::uint32_t& out) {
    if (!Has(4)) return false;
    std::memcpy(&out, data_ + pos_, sizeof(out));
    pos_ += sizeof(out);
    return true;
  }

  bool ReadU64(std::uint64_t& out) {
    if (!Has(8)) return false;
    std::memcpy(&out, data_ + pos_, sizeof(out));
    pos_ += sizeof(out);
    return true;
  }

  bool ReadFloat(float& out) {
    if (!Has(4)) return false;
    std::memcpy(&out, data_ + pos_, sizeof(out));
    pos_ += sizeof(out);
    return true;
  }

  bool ReadCString(std::string& out, std::size_t max_bytes_including_nul) {
    out.clear();
    std::size_t consumed = 0;
    while (Has(1) && consumed < max_bytes_including_nul) {
      const char ch = static_cast<char>(data_[pos_++]);
      ++consumed;
      if (ch == '\0') return true;
      out.push_back(ch);
    }
    return false;
  }

 private:
  const std::uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t pos_ = 0;
};

void ClearFailedParse(RealmConnectionCharEnumPayload& out) {
  out.characters.clear();
  out.trailing_u32s.fill(0);
  out.has_trailing_u32s = false;
}

}

bool ParseRealmConnectionCharEnum(const std::uint8_t* data,
                                  std::size_t size,
                                  RealmConnectionCharEnumPayload& out,
                                  std::size_t* consumed) {
  out = {};

  if (data == nullptr) {
    size = 0;
  }

  ByteCursor cursor(data, size);
  const auto finish = [&](const bool ok) {
    if (consumed != nullptr) {
      *consumed = cursor.Position();
    }
    return ok;
  };

  std::uint8_t count = 0;
  if (!cursor.ReadU8(count)) return finish(false);

  if (count > 10) {
    count = 0;
    out.truncated_count = true;
  }

  out.characters.reserve(count);
  for (std::uint8_t i = 0; i < count; ++i) {
    RealmConnectionCharEnumEntry entry{};
    if (!cursor.ReadU64(entry.guid) ||
        !cursor.ReadCString(entry.name, 0x30) ||
        !cursor.ReadU8(entry.race) ||
        !cursor.ReadU8(entry.char_class) ||
        !cursor.ReadU8(entry.gender) ||
        !cursor.ReadU8(entry.skin) ||
        !cursor.ReadU8(entry.face) ||
        !cursor.ReadU8(entry.hair_style) ||
        !cursor.ReadU8(entry.hair_color) ||
        !cursor.ReadU8(entry.facial_hair) ||
        !cursor.ReadU8(entry.level) ||
        !cursor.ReadU32(entry.zone_id) ||
        !cursor.ReadU32(entry.map_id) ||
        !cursor.ReadFloat(entry.x) ||
        !cursor.ReadFloat(entry.y) ||
        !cursor.ReadFloat(entry.z) ||
        !cursor.ReadU32(entry.guild_id) ||
        !cursor.ReadU32(entry.char_flags) ||
        !cursor.ReadU32(entry.customize_flags) ||
        !cursor.ReadU8(entry.first_login) ||
        !cursor.ReadU32(entry.pet_display_id) ||
        !cursor.ReadU32(entry.pet_level) ||
        !cursor.ReadU32(entry.pet_family)) {
      ClearFailedParse(out);
      return finish(false);
    }

    for (auto& slot : entry.equipment) {
      if (!cursor.ReadU32(slot.display_id) ||
          !cursor.ReadU8(slot.inventory_type) ||
          !cursor.ReadU32(slot.enchant_aura)) {
        ClearFailedParse(out);
        return finish(false);
      }
    }

    out.characters.push_back(std::move(entry));
  }

  if (cursor.AtEnd()) {
    if (out.truncated_count) {
      ClearFailedParse(out);
      return finish(false);
    }
    return finish(true);
  }

  if (out.truncated_count) {
    ClearFailedParse(out);
    return finish(false);
  }

  for (auto& value : out.trailing_u32s) {
    if (!cursor.ReadU32(value)) {
      ClearFailedParse(out);
      return finish(false);
    }
  }

  if (!cursor.AtEnd()) {
    ClearFailedParse(out);
    return finish(false);
  }

  out.has_trailing_u32s = true;
  return finish(true);
}

bool ParseRealmConnectionAuthResponseWithFallback(
    const std::uint8_t* data,
    std::size_t size,
    const std::uint8_t fallback_result_code,
    RealmConnectionAuthResponsePayload& out,
    std::size_t* consumed) {
  out = {};

  if (data == nullptr) {
    size = 0;
  }

  ByteCursor cursor(data, size);
  const auto finish = [&](const bool ok) {
    if (consumed != nullptr) {
      *consumed = cursor.Position();
    }
    return ok;
  };

  out.result_code = fallback_result_code;
  const bool has_result_code = cursor.ReadU8(out.result_code);

  out.authenticated = (out.result_code == 12);

  if (out.result_code == 12 || out.result_code == 27) {
    const std::size_t queue_tail = (out.result_code == 27) ? 5 : 0;
    if (cursor.Remaining() >= queue_tail + 10) {
      if (!cursor.ReadU32(out.billing_time) ||
          !cursor.ReadU8(out.billing_flags) ||
          !cursor.ReadU32(out.billing_rested) ||
          !cursor.ReadU8(out.expansion_level)) {
        return finish(false);
      }
      out.has_account_info = true;
    }
  }

  if (out.result_code == 27 && cursor.Has(5)) {
    if (!cursor.ReadU32(out.queue_position) ||
        !cursor.ReadU8(out.free_character_migration)) {
      return finish(false);
    }
    out.has_queue_position = true;
  }

  return finish(has_result_code);
}

bool ParseRealmConnectionAuthResponse(
    const std::uint8_t* data,
    std::size_t size,
    RealmConnectionAuthResponsePayload& out,
    std::size_t* consumed) {
  return ParseRealmConnectionAuthResponseWithFallback(data, size, 0, out,
                                                      consumed);
}

void ParseRealmConnectionLogoutResponse(
    const std::uint8_t* data,
    std::size_t size,
    RealmConnectionLogoutResponsePayload& out,
    std::size_t* consumed) {
  out = {};

  if (data == nullptr) {
    size = 0;
  }

  ByteCursor cursor(data, size);
  cursor.ReadU32(out.result);
  cursor.ReadU8(out.instant_flag);

  if (consumed != nullptr) {
    *consumed = cursor.Position();
  }
}

}

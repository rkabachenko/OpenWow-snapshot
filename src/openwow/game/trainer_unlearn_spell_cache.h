#pragma once

#include "openwow/game/packet_reader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace openwow::game {

class TrainerUnlearnSpellCache {
 public:
  bool HandleSendUnlearnSpells(const std::uint8_t* data, std::size_t len) {
    PacketReader reader(data, len);

    std::uint32_t count = 0;
    if (!reader.ReadU32(count)) {
      return false;
    }

    spell_ids_.assign(count, 0);
    for (std::uint32_t index = 0; index < count; ++index) {
      if (!reader.ReadU32(spell_ids_[index])) {
        return false;
      }
    }

    return true;
  }

  [[nodiscard]] bool ContainsRawSpellId(const std::int32_t spell_id) const {
    return ContainsSpellId(static_cast<std::uint32_t>(spell_id));
  }

  [[nodiscard]] bool ContainsSpellId(const std::uint32_t spell_id) const {
    return std::find(spell_ids_.begin(), spell_ids_.end(), spell_id) != spell_ids_.end();
  }

  [[nodiscard]] std::uint32_t Signature() const {
    std::uint32_t signature = static_cast<std::uint32_t>(spell_ids_.size());
    for (const std::uint32_t spell_id : spell_ids_) {
      signature ^= spell_id + 0x9e3779b9u + (signature << 6) + (signature >> 2);
    }
    return signature;
  }

  [[nodiscard]] const std::vector<std::uint32_t>& spell_ids() const {
    return spell_ids_;
  }

  void Clear() {
    spell_ids_.clear();
  }

 private:
  std::vector<std::uint32_t> spell_ids_;
};

}

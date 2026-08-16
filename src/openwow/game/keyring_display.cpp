
#include "openwow/game/keyring_display.h"

#include <algorithm>
#include <iterator>

namespace openwow::game {

void KeyringDisplay::SetKeys(std::vector<KeyringEntry> keys) {

    if (keys.size() > kKeyringMaxSlots) {
        keys.resize(kKeyringMaxSlots);
    }

    std::sort(keys.begin(), keys.end(),
              [](const KeyringEntry& a, const KeyringEntry& b) {
                  return a.slot < b.slot;
              });

    keys_ = std::move(keys);
}

const std::vector<KeyringEntry>& KeyringDisplay::GetKeys() const {
    return keys_;
}

std::optional<KeyringEntry> KeyringDisplay::GetKey(std::uint8_t slot) const {

    for (const auto& k : keys_) {
        if (k.slot == slot) return k;
    }
    return std::nullopt;
}

std::size_t KeyringDisplay::GetKeyCount() const {
    return keys_.size();
}

std::size_t KeyringDisplay::GetMaxSlots() const {
    return kKeyringMaxSlots;
}

bool KeyringDisplay::HasKey(std::uint32_t itemId) const {
    return std::any_of(keys_.begin(), keys_.end(),
                       [itemId](const KeyringEntry& k) {
                           return k.itemId == itemId;
                       });
}

std::vector<KeyringEntry> KeyringDisplay::GetQuestKeys() const {
    std::vector<KeyringEntry> result;
    result.reserve(keys_.size());
    std::copy_if(keys_.begin(), keys_.end(), std::back_inserter(result),
                 [](const KeyringEntry& k) { return k.isQuestKey; });
    return result;
}

bool KeyringDisplay::IsKeyringOpen() const {
    return open_;
}

void KeyringDisplay::SetOpen(bool open) {
    open_ = open;
}

void KeyringDisplay::ToggleOpen() {
    open_ = !open_;
}

void KeyringDisplay::Reset() {
    keys_.clear();
    open_ = false;
}

}

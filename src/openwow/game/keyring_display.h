#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

inline constexpr std::size_t kKeyringMaxSlots = 32;

struct KeyringEntry {
    std::uint8_t  slot       = 0;
    std::uint32_t itemId     = 0;
    std::string   itemName;
    std::uint32_t iconId     = 0;
    bool          isQuestKey = false;
};

class KeyringDisplay {
public:
    void SetKeys(std::vector<KeyringEntry> keys);
    [[nodiscard]] const std::vector<KeyringEntry>& GetKeys() const;
    [[nodiscard]] std::optional<KeyringEntry> GetKey(std::uint8_t slot) const;
    [[nodiscard]] std::size_t GetKeyCount() const;
    [[nodiscard]] std::size_t GetMaxSlots() const;
    [[nodiscard]] bool HasKey(std::uint32_t itemId) const;
    [[nodiscard]] std::vector<KeyringEntry> GetQuestKeys() const;

    [[nodiscard]] bool IsKeyringOpen() const;
    void SetOpen(bool open);
    void ToggleOpen();

    void Reset();

private:
    std::vector<KeyringEntry> keys_;
    bool                      open_ = false;
};

}

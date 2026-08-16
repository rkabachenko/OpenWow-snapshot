#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class PushbackType : std::uint8_t {
    Damage,
    Interrupt,
};

struct SpellPushbackState {
    std::uint32_t spellId = 0;
    float originalCastTime = 0.0f;
    float currentCastTime = 0.0f;
    std::uint8_t pushbackCount = 0;
    static constexpr std::uint8_t kMaxPushbacks = 2;
    float pushbackAmount = 0.0f;
};

class SpellPushbackSystem {
 public:

    void BeginCast(std::uint32_t spellId, float castTime);

    float ApplyPushback(std::uint32_t spellId, PushbackType type);

    float ApplyChannelPushback(std::uint32_t spellId);

    [[nodiscard]] std::optional<SpellPushbackState> GetPushbackState(
        std::uint32_t spellId) const;
    [[nodiscard]] float GetCurrentCastTime(std::uint32_t spellId) const;
    [[nodiscard]] std::uint8_t GetPushbackCount(std::uint32_t spellId) const;
    [[nodiscard]] bool IsMaxPushback(std::uint32_t spellId) const;

    void CancelCast(std::uint32_t spellId);
    void CompleteCast(std::uint32_t spellId);

    [[nodiscard]] std::vector<SpellPushbackState> GetActiveCasts() const;

    void Clear();

 private:
    std::unordered_map<std::uint32_t, SpellPushbackState> casts_;
};

}

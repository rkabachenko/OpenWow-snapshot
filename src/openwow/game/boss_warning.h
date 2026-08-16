
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class BossWarningType : std::uint8_t {
    Emote = 0,
    Yell,
    Phase,
    Timer,
    Spell,
};

struct BossWarningEntry {
    BossWarningType warningType{BossWarningType::Emote};
    std::string bossName;
    std::string message;
    std::uint32_t spellId{0};
    float duration{5.0f};
    float remaining{5.0f};
    std::uint32_t color{0xFFFF0000};
    bool hasTimer{false};
    std::uint32_t soundId{0};
};

class BossWarningSystem {
 public:

    void AddWarning(BossWarningEntry entry);

    [[nodiscard]] std::vector<BossWarningEntry> GetActiveWarnings() const;

    [[nodiscard]] std::optional<BossWarningEntry> GetWarning(
        std::uint32_t index) const;

    [[nodiscard]] bool HasActiveWarning() const;

    [[nodiscard]] std::uint32_t GetActiveCount() const;

    void ClearWarning(std::uint32_t index);

    void ClearAll();

    [[nodiscard]] std::vector<BossWarningEntry> GetWarningsByType(
        BossWarningType type) const;

    [[nodiscard]] std::vector<BossWarningEntry> GetTimerWarnings() const;

    void Update(float dt);

    void SetEnabled(bool enabled);
    [[nodiscard]] bool IsEnabled() const;

    [[nodiscard]] static std::string GetTypeName(BossWarningType type);

    void Reset();

 private:
    std::vector<BossWarningEntry> warnings_;
    bool enabled_{true};
};

}

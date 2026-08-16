
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct RaidWarningEntry {
    std::string message;
    std::string senderName;
    float timestamp{0.0f};
    float duration{3.0f};
    float remaining{3.0f};
    std::uint32_t color{0xFFFF4800};
};

class RaidWarningDisplay {
 public:

    void ShowWarning(const std::string& message, const std::string& sender,
                     std::uint32_t color = 0xFFFF4800);

    [[nodiscard]] std::optional<RaidWarningEntry> GetCurrentWarning() const;

    [[nodiscard]] bool HasWarning() const;

    [[nodiscard]] std::vector<RaidWarningEntry> GetWarningHistory() const;

    [[nodiscard]] std::uint32_t GetHistoryCount() const;

    void SetMaxHistory(std::uint32_t max);

    void SetDefaultDuration(float seconds);
    [[nodiscard]] float GetDefaultDuration() const;

    [[nodiscard]] float GetFadeProgress() const;

    void Update(float dt);

    void Clear();

    void ClearHistory();

 private:
    std::optional<RaidWarningEntry> current_;
    std::vector<RaidWarningEntry> history_;
    std::uint32_t maxHistory_{20};
    float defaultDuration_{3.0f};
    float elapsedTime_{0.0f};
};

}

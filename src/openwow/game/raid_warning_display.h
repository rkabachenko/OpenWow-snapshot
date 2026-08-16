
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class RaidMsgType : std::uint8_t {
    RaidWarning = 0,
    RaidBossEmote = 1,
    RaidBossWhisper = 2,
    ZoneMessage = 3,
};

struct RaidMsgEntry {
    std::string message;
    RaidMsgType type{RaidMsgType::RaidWarning};
    double timestamp{0.0};
    float duration{5.0f};
    float fadeProgress{0.0f};
    std::string senderName;
};

class RaidMsgDisplay {
 public:
    static constexpr std::uint32_t kMaxQueueSize = 10;
    static constexpr float kFadeDuration = 1.5f;
    static constexpr float kWarningDuration = 5.0f;
    static constexpr float kBossEmoteDuration = 8.0f;

    void ShowMessage(const std::string& message, RaidMsgType type,
                     const std::string& senderName = {});

    void Update(float deltaTime);

    [[nodiscard]] std::optional<RaidMsgEntry> GetActiveMessage() const;

    [[nodiscard]] std::vector<RaidMsgEntry> GetMessageQueue() const;

    [[nodiscard]] std::uint32_t GetQueueSize() const;

    void DismissCurrent();

    void SetEnabled(bool enabled);

    [[nodiscard]] bool IsEnabled() const;

    void ClearAll();

 private:
    [[nodiscard]] float DurationForType(RaidMsgType type) const;

    std::vector<RaidMsgEntry> queue_;
    bool enabled_{true};
    double currentTime_{0.0};
};

}

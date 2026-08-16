
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class DeserterType : uint8_t {
    LFG          = 0,
    Battleground = 1,
};

struct DeserterEntry {
    DeserterType type       = DeserterType::LFG;
    float        expiryTime = 0.0f;
    std::string  reason;
};

class DeserterTracker {
 public:
    void AddDeserter(DeserterType type, float duration,
                     const std::string& reason);
    [[nodiscard]] bool HasDeserter(DeserterType type) const;
    [[nodiscard]] float GetRemainingTime(DeserterType type) const;
    [[nodiscard]] std::optional<DeserterEntry> GetDeserter(
        DeserterType type) const;
    void RemoveDeserter(DeserterType type);
    [[nodiscard]] std::vector<DeserterEntry> GetAllDeserters() const;

    void Update(float dt);

    [[nodiscard]] bool IsLFGLocked() const;
    [[nodiscard]] bool IsBGLocked() const;

    [[nodiscard]] static constexpr float GetDefaultLFGDuration() {
        return 1800.0f;
    }
    [[nodiscard]] static constexpr float GetDefaultBGDuration() {
        return 900.0f;
    }

    void Reset();

 private:
    std::unordered_map<uint8_t, DeserterEntry> deserters_;
    mutable std::mutex mutex_;
};

}

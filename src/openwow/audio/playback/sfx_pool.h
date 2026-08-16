
#pragma once

#include <cstdint>
#include <unordered_map>

namespace openwow::audio {

struct SFXPoolEntry {
    std::uint32_t soundId{0};
    float         lastPlayTime{0.0f};
    float         minInterval{0.1f};
    std::uint32_t maxConcurrent{4};
    std::uint32_t currentPlaying{0};
    std::uint32_t priority{0};
};

class SFXPool {
public:
    SFXPool() = default;
    ~SFXPool() = default;

    void RegisterSound(std::uint32_t soundId,
                       float minInterval,
                       std::uint32_t maxConcurrent,
                       std::uint32_t priority = 0);

    [[nodiscard]] bool CanPlay(std::uint32_t soundId) const;

    bool Play(std::uint32_t soundId);

    void StopOne(std::uint32_t soundId);

    void StopAll(std::uint32_t soundId);

    [[nodiscard]] std::uint32_t GetPlayingCount(std::uint32_t soundId) const;
    [[nodiscard]] std::uint32_t GetRegisteredCount() const;
    [[nodiscard]] bool IsRegistered(std::uint32_t soundId) const;
    [[nodiscard]] float GetTimeSinceLastPlay(std::uint32_t soundId) const;

    void SetGlobalConcurrentLimit(std::uint32_t limit);
    [[nodiscard]] std::uint32_t GetGlobalConcurrentLimit() const;
    [[nodiscard]] std::uint32_t GetTotalPlaying() const;

    void Update(float dt);

    void Reset();

private:
    std::unordered_map<std::uint32_t, SFXPoolEntry> entries_;
    float elapsedTime_{0.0f};
    std::uint32_t globalConcurrentLimit_{32};
};

}

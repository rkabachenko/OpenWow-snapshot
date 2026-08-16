#pragma once

#include <cstdint>

namespace openwow::core {

struct GameLoopConfig {
    float fixedTimestep{1.0f / 60.0f};
    float maxDeltaTime{0.25f};
    std::uint32_t maxSubSteps{8};
    bool enableFrameRateLimit{false};
    std::uint32_t targetFPS{60};
};

class CoreGameLoop {
public:
    CoreGameLoop();

    void SetConfig(const GameLoopConfig& config);
    [[nodiscard]] GameLoopConfig GetConfig() const;

    void Begin();

    void Update(float rawDeltaTime);

    [[nodiscard]] float GetDeltaTime() const;

    [[nodiscard]] float GetFixedDeltaTime() const;

    [[nodiscard]] float GetAccumulator() const;

    bool ShouldFixedUpdate();

    [[nodiscard]] float GetInterpolationAlpha() const;

    [[nodiscard]] float GetFPS() const;

    [[nodiscard]] std::uint64_t GetFrameCount() const;

    [[nodiscard]] double GetTotalTime() const;

    void SetPaused(bool paused);
    [[nodiscard]] bool IsPaused() const;

    void SetTimeScale(float scale);
    [[nodiscard]] float GetTimeScale() const;

    [[nodiscard]] float GetFrameTimeMs() const;

    [[nodiscard]] float GetMinDelta() const;
    [[nodiscard]] float GetMaxDelta() const;

    [[nodiscard]] std::uint32_t GetLastFixedUpdateCount() const;

    void Reset();

private:
    GameLoopConfig config_{};
    float deltaTime_{0.0f};
    float accumulator_{0.0f};
    float smoothedFPS_{0.0f};
    double totalTime_{0.0};
    std::uint64_t frameCount_{0};
    bool paused_{false};
    float timeScale_{1.0f};
    float minDelta_{1e30f};
    float maxDelta_{0.0f};
    std::uint32_t lastFixedUpdates_{0};
};

}

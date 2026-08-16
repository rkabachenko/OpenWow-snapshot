
#include "openwow/core/game_loop.h"

#include <algorithm>
#include <cmath>

namespace openwow::core {

CoreGameLoop::CoreGameLoop() { Reset(); }

void CoreGameLoop::SetConfig(const GameLoopConfig& config) {
    config_ = config;
    if (config_.fixedTimestep <= 0.0f) config_.fixedTimestep = 1.0f / 60.0f;
    if (config_.maxDeltaTime <= 0.0f) config_.maxDeltaTime = 0.25f;
    if (config_.maxSubSteps == 0) config_.maxSubSteps = 1;
}

GameLoopConfig CoreGameLoop::GetConfig() const { return config_; }

void CoreGameLoop::Begin() {

    lastFixedUpdates_ = 0;
}

void CoreGameLoop::Update(float rawDeltaTime) {
    if (paused_) {
        deltaTime_ = 0.0f;
        return;
    }

    float scaled = rawDeltaTime * timeScale_;
    deltaTime_ = std::min(scaled, config_.maxDeltaTime);
    accumulator_ += deltaTime_;
    totalTime_ += static_cast<double>(deltaTime_);
    ++frameCount_;

    if (rawDeltaTime > 0.0f) {
        minDelta_ = std::min(minDelta_, rawDeltaTime);
        maxDelta_ = std::max(maxDelta_, rawDeltaTime);
    }

    const float instantFPS = (deltaTime_ > 0.0f) ? (1.0f / deltaTime_) : 0.0f;
    constexpr float kSmoothFactor = 0.1f;
    smoothedFPS_ = smoothedFPS_ + kSmoothFactor * (instantFPS - smoothedFPS_);
}

float CoreGameLoop::GetDeltaTime() const { return deltaTime_; }

float CoreGameLoop::GetFixedDeltaTime() const { return config_.fixedTimestep; }

float CoreGameLoop::GetAccumulator() const { return accumulator_; }

float CoreGameLoop::GetFPS() const { return smoothedFPS_; }

std::uint64_t CoreGameLoop::GetFrameCount() const { return frameCount_; }

double CoreGameLoop::GetTotalTime() const { return totalTime_; }

bool CoreGameLoop::IsPaused() const { return paused_; }

bool CoreGameLoop::ShouldFixedUpdate() {
    if (accumulator_ >= config_.fixedTimestep) {
        accumulator_ -= config_.fixedTimestep;
        ++lastFixedUpdates_;
        return true;
    }
    return false;
}

float CoreGameLoop::GetInterpolationAlpha() const {
    if (config_.fixedTimestep <= 0.0f) return 0.0f;
    return accumulator_ / config_.fixedTimestep;
}

std::uint32_t CoreGameLoop::GetLastFixedUpdateCount() const {
    return lastFixedUpdates_;
}

void CoreGameLoop::SetTimeScale(float scale) {
    timeScale_ = std::max(0.0f, scale);
}

float CoreGameLoop::GetTimeScale() const {
    return timeScale_;
}

float CoreGameLoop::GetFrameTimeMs() const {
    return deltaTime_ * 1000.0f;
}

float CoreGameLoop::GetMinDelta() const {
    return minDelta_ < 1e29f ? minDelta_ : 0.0f;
}

float CoreGameLoop::GetMaxDelta() const {
    return maxDelta_;
}

void CoreGameLoop::SetPaused(bool paused) { paused_ = paused; }

void CoreGameLoop::Reset() {
    deltaTime_ = 0.0f;
    accumulator_ = 0.0f;
    smoothedFPS_ = 0.0f;
    totalTime_ = 0.0;
    frameCount_ = 0;
    paused_ = false;
    config_ = {};
    timeScale_ = 1.0f;
    minDelta_ = 1e30f;
    maxDelta_ = 0.0f;
    lastFixedUpdates_ = 0;
}

}

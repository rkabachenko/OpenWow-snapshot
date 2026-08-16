
#include "openwow/audio/spatial/audio_environment.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace openwow::audio {

ReverbParams ReverbParams::Lerp(const ReverbParams& a,
                                const ReverbParams& b,
                                float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    ReverbParams result;
    result.decayTime    = a.decayTime    + (b.decayTime    - a.decayTime)    * t;
    result.reflections  = a.reflections  + (b.reflections  - a.reflections)  * t;
    result.lateReverb   = a.lateReverb   + (b.lateReverb   - a.lateReverb)   * t;
    result.diffusion    = a.diffusion    + (b.diffusion    - a.diffusion)    * t;
    result.density      = a.density      + (b.density      - a.density)      * t;
    result.roomSize     = a.roomSize     + (b.roomSize     - a.roomSize)     * t;
    result.wetDry       = a.wetDry       + (b.wetDry       - a.wetDry)       * t;
    result.hfDamping    = a.hfDamping    + (b.hfDamping    - a.hfDamping)    * t;
    return result;
}

float LowPassFilter::ComputeAlpha(float cutoffHz, float sampleRate) {

    if (cutoffHz <= 0.0f || sampleRate <= 0.0f) return 1.0f;
    float rc = 1.0f / (2.0f * std::numbers::pi_v<float> * cutoffHz);
    float dt = 1.0f / sampleRate;
    return dt / (rc + dt);
}

float LowPassFilter::Process(float input, float alpha) {
    previous = previous + alpha * (input - previous);
    return previous;
}

void AudioEnvironment::SetZoneEnvironment(ZoneEnvironment env, float transitionTime) {
    if (state_.environment == env) return;
    state_.environment = env;

    ReverbPreset preset = ReverbPreset::None;
    switch (env) {
        case ZoneEnvironment::Outdoor:    preset = ReverbPreset::None; break;
        case ZoneEnvironment::Indoor:     preset = ReverbPreset::MediumRoom; break;
        case ZoneEnvironment::Cave:       preset = ReverbPreset::Cave; break;
        case ZoneEnvironment::Underwater: preset = ReverbPreset::Underwater; break;
    }
    SetReverbPreset(preset, transitionTime);
}

ZoneEnvironment AudioEnvironment::GetZoneEnvironment() const {
    return state_.environment;
}

void AudioEnvironment::SetReverbPreset(ReverbPreset preset, float transitionTime) {
    if (state_.activePreset == preset && state_.transitionProgress >= 1.0f) return;

    state_.activePreset = preset;
    state_.targetParams = GetPresetParams(preset);
    if (transitionTime <= 0.0f) {

        state_.transitionProgress = 1.0f;
        state_.transitionSpeed = 0.0f;
        state_.currentParams = state_.targetParams;
    } else {
        state_.transitionProgress = 0.0f;
        state_.transitionSpeed = 1.0f / transitionTime;
    }
}

ReverbPreset AudioEnvironment::GetActivePreset() const {
    return state_.activePreset;
}

void AudioEnvironment::SetReverbParams(const ReverbParams& params, float transitionTime) {
    state_.targetParams = params;
    if (transitionTime <= 0.0f) {
        state_.transitionProgress = 1.0f;
        state_.transitionSpeed = 0.0f;
        state_.currentParams = state_.targetParams;
    } else {
        state_.transitionProgress = 0.0f;
        state_.transitionSpeed = 1.0f / transitionTime;
    }
}

const ReverbParams& AudioEnvironment::GetCurrentReverbParams() const {
    return state_.currentParams;
}

void AudioEnvironment::SetUnderwater(bool underwater, float depth) {
    state_.isUnderwater = underwater;
    state_.underwaterDepth = std::max(0.0f, depth);
    if (underwater) {
        state_.underwaterLpfCutoff = ComputeUnderwaterCutoff(state_.underwaterDepth);
    } else {
        state_.underwaterLpfCutoff = 22050.0f;
        lpf_.Reset();
    }
}

bool AudioEnvironment::IsUnderwater() const {
    return state_.isUnderwater;
}

float AudioEnvironment::GetUnderwaterDepth() const {
    return state_.underwaterDepth;
}

float AudioEnvironment::GetUnderwaterCutoff() const {
    return state_.underwaterLpfCutoff;
}

bool AudioEnvironment::IsIndoor() const {
    return state_.environment == ZoneEnvironment::Indoor ||
           state_.environment == ZoneEnvironment::Cave;
}

bool AudioEnvironment::IsCave() const {
    return state_.environment == ZoneEnvironment::Cave;
}

void AudioEnvironment::Update(float dt) {

    if (state_.transitionProgress < 1.0f) {
        state_.transitionProgress += dt * state_.transitionSpeed;
        if (state_.transitionProgress >= 1.0f) {
            state_.transitionProgress = 1.0f;
            state_.currentParams = state_.targetParams;
        } else {

            state_.currentParams = ReverbParams::Lerp(
                state_.currentParams, state_.targetParams,
                dt * state_.transitionSpeed);
        }
    }

    if (state_.isUnderwater) {
        float target = ComputeUnderwaterCutoff(state_.underwaterDepth);

        float speed = 5.0f * dt;
        state_.underwaterLpfCutoff += (target - state_.underwaterLpfCutoff) * std::min(speed, 1.0f);
    }
}

ReverbParams AudioEnvironment::GetPresetParams(ReverbPreset preset) {
    ReverbParams p;
    switch (preset) {
        case ReverbPreset::None:
            p = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            break;
        case ReverbPreset::SmallRoom:
            p = {0.5f, 0.5f, 0.3f, 0.6f, 0.4f, 0.2f, 0.2f, 0.4f};
            break;
        case ReverbPreset::MediumRoom:
            p = {1.0f, 0.4f, 0.5f, 0.7f, 0.5f, 0.4f, 0.3f, 0.5f};
            break;
        case ReverbPreset::LargeRoom:
            p = {1.8f, 0.3f, 0.6f, 0.8f, 0.6f, 0.6f, 0.4f, 0.5f};
            break;
        case ReverbPreset::Cave:
            p = {2.5f, 0.5f, 0.7f, 0.5f, 0.4f, 0.5f, 0.5f, 0.3f};
            break;
        case ReverbPreset::DeepCave:
            p = {3.5f, 0.4f, 0.8f, 0.4f, 0.3f, 0.7f, 0.6f, 0.2f};
            break;
        case ReverbPreset::Tunnel:
            p = {2.0f, 0.6f, 0.5f, 0.3f, 0.2f, 0.3f, 0.4f, 0.4f};
            break;
        case ReverbPreset::Cathedral:
            p = {4.0f, 0.3f, 0.8f, 0.9f, 0.8f, 0.9f, 0.5f, 0.6f};
            break;
        case ReverbPreset::Underwater:
            p = {1.5f, 0.2f, 0.6f, 0.9f, 0.7f, 0.5f, 0.4f, 0.8f};
            break;
        case ReverbPreset::Arena:
            p = {1.2f, 0.5f, 0.4f, 0.7f, 0.5f, 0.5f, 0.35f, 0.4f};
            break;
        case ReverbPreset::Dungeon:
            p = {2.2f, 0.4f, 0.6f, 0.6f, 0.5f, 0.5f, 0.45f, 0.4f};
            break;
    }
    return p;
}

std::string AudioEnvironment::GetPresetName(ReverbPreset preset) {
    switch (preset) {
        case ReverbPreset::None:        return "None";
        case ReverbPreset::SmallRoom:   return "SmallRoom";
        case ReverbPreset::MediumRoom:  return "MediumRoom";
        case ReverbPreset::LargeRoom:   return "LargeRoom";
        case ReverbPreset::Cave:        return "Cave";
        case ReverbPreset::DeepCave:    return "DeepCave";
        case ReverbPreset::Tunnel:      return "Tunnel";
        case ReverbPreset::Cathedral:   return "Cathedral";
        case ReverbPreset::Underwater:  return "Underwater";
        case ReverbPreset::Arena:       return "Arena";
        case ReverbPreset::Dungeon:     return "Dungeon";
    }
    return "Unknown";
}

void AudioEnvironment::Reset() {
    state_ = EnvironmentState{};
    lpf_.Reset();
}

float AudioEnvironment::ComputeUnderwaterCutoff(float depth) {

    constexpr float kMaxCutoff = 22050.0f;
    constexpr float kMinCutoff = 400.0f;
    constexpr float kDecayRate = 0.25f;

    float cutoff = kMaxCutoff * std::exp(-depth * kDecayRate);
    return std::clamp(cutoff, kMinCutoff, kMaxCutoff);
}

}

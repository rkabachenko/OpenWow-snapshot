
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace openwow::audio {

enum class ReverbPreset : std::uint8_t {
    None        = 0,
    SmallRoom   = 1,
    MediumRoom  = 2,
    LargeRoom   = 3,
    Cave        = 4,
    DeepCave    = 5,
    Tunnel      = 6,
    Cathedral   = 7,
    Underwater  = 8,
    Arena       = 9,
    Dungeon     = 10,
};

constexpr int kReverbPresetCount = 11;

struct ReverbParams {
    float decayTime{1.0f};
    float reflections{0.3f};
    float lateReverb{0.5f};
    float diffusion{0.8f};
    float density{0.5f};
    float roomSize{0.5f};
    float wetDry{0.3f};
    float hfDamping{0.5f};

    [[nodiscard]] static ReverbParams Lerp(const ReverbParams& a,
                                           const ReverbParams& b,
                                           float t);
};

struct LowPassFilter {
    float cutoffHz{22050.0f};
    float resonance{0.0f};
    float previous{0.0f};

    [[nodiscard]] static float ComputeAlpha(float cutoffHz, float sampleRate);

    float Process(float input, float alpha);

    void Reset() { previous = 0.0f; }
};

enum class ZoneEnvironment : std::uint8_t {
    Outdoor     = 0,
    Indoor      = 1,
    Cave        = 2,
    Underwater  = 3,
};

struct EnvironmentState {
    ZoneEnvironment environment{ZoneEnvironment::Outdoor};
    ReverbPreset    activePreset{ReverbPreset::None};
    ReverbParams    currentParams;
    ReverbParams    targetParams;
    float           transitionProgress{1.0f};
    float           transitionSpeed{1.0f};
    bool            isUnderwater{false};
    float           underwaterDepth{0.0f};
    float           underwaterLpfCutoff{22050.0f};
};

class AudioEnvironment {
public:
    AudioEnvironment() = default;
    ~AudioEnvironment() = default;

    void SetZoneEnvironment(ZoneEnvironment env, float transitionTime = 1.0f);
    [[nodiscard]] ZoneEnvironment GetZoneEnvironment() const;

    void SetReverbPreset(ReverbPreset preset, float transitionTime = 1.0f);
    [[nodiscard]] ReverbPreset GetActivePreset() const;

    void SetReverbParams(const ReverbParams& params, float transitionTime = 1.0f);

    [[nodiscard]] const ReverbParams& GetCurrentReverbParams() const;

    void SetUnderwater(bool underwater, float depth = 0.0f);
    [[nodiscard]] bool IsUnderwater() const;
    [[nodiscard]] float GetUnderwaterDepth() const;

    [[nodiscard]] float GetUnderwaterCutoff() const;

    [[nodiscard]] bool IsIndoor() const;
    [[nodiscard]] bool IsCave() const;

    [[nodiscard]] const EnvironmentState& GetState() const { return state_; }

    void Update(float dt);

    void SetReverbEnabled(bool enabled) { reverbEnabled_ = enabled; }
    [[nodiscard]] bool IsReverbEnabled() const { return reverbEnabled_; }

    [[nodiscard]] static ReverbParams GetPresetParams(ReverbPreset preset);

    [[nodiscard]] static std::string GetPresetName(ReverbPreset preset);

    void Reset();

private:
    EnvironmentState state_;
    LowPassFilter    lpf_;
    bool             reverbEnabled_{true};

    [[nodiscard]] static float ComputeUnderwaterCutoff(float depth);
};

}

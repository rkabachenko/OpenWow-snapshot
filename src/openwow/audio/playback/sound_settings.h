
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace openwow::audio {

enum class SoundChannel : uint8_t {
    Master      = 0,
    Music       = 1,
    SFX         = 2,
    Ambience    = 3,
    Dialog      = 4,
    ErrorSpeech = 5,
};

constexpr int kSoundChannelCount = 6;

struct ListenerPosition {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

class SoundSettings {
public:
    static SoundSettings& Instance();

    void SetEnabled(bool enabled);
    [[nodiscard]] bool IsEnabled() const;

    void  SetVolume(SoundChannel ch, float volume);
    [[nodiscard]] float GetVolume(SoundChannel ch) const;

    void SetChannelEnabled(SoundChannel ch, bool enabled);
    [[nodiscard]] bool IsChannelEnabled(SoundChannel ch) const;

    void SetReverbEnabled(bool enabled);
    [[nodiscard]] bool IsReverbEnabled() const;

    void SetEAXEnabled(bool enabled);
    [[nodiscard]] bool IsEAXEnabled() const;

    void SetStereo(bool stereo);
    [[nodiscard]] bool IsStereo() const;

    void SetSampleRate(uint32_t rate);
    [[nodiscard]] uint32_t GetSampleRate() const;

    void SetBufferSize(uint32_t size);
    [[nodiscard]] uint32_t GetBufferSize() const;

    void SetSoftwareChannels(uint32_t count);
    [[nodiscard]] uint32_t GetSoftwareChannels() const;

    void MuteAll();
    void UnmuteAll();
    [[nodiscard]] bool IsMuted() const;

    void SetListenerPosition(float x, float y, float z);
    void SetListenerOrientation(float fx, float fy, float fz);
    [[nodiscard]] ListenerPosition GetListenerPosition() const;

    void ApplyDefaults();
    void Reset();

private:
    SoundSettings() { ApplyDefaults(); }

    mutable std::mutex mutex_;

    bool     enabled_    = true;
    bool     muted_      = false;
    bool     reverb_     = true;
    bool     eax_        = false;
    bool     stereo_     = true;
    uint32_t sampleRate_ = 44100;
    uint32_t bufferSize_ = 4096;
    uint32_t swChannels_ = 32;

    float volumes_[kSoundChannelCount]  = {1.0f, 0.4f, 1.0f, 0.6f, 1.0f, 1.0f};
    bool  enabled_ch_[kSoundChannelCount] = {true, true, true, true, true, true};

    ListenerPosition listenerPos_;
    float listenerFX_ = 0.0f;
    float listenerFY_ = 0.0f;
    float listenerFZ_ = -1.0f;
};

}


#include "openwow/audio/playback/sound_settings.h"

#include <algorithm>
#include <cstring>

namespace openwow::audio {

SoundSettings& SoundSettings::Instance() {
    static SoundSettings instance;
    return instance;
}

void SoundSettings::SetEnabled(bool enabled) {
    std::lock_guard lock(mutex_);
    enabled_ = enabled;
}

bool SoundSettings::IsEnabled() const {
    std::lock_guard lock(mutex_);
    return enabled_;
}

void SoundSettings::SetVolume(SoundChannel ch, float volume) {
    std::lock_guard lock(mutex_);
    auto idx = static_cast<int>(ch);
    if (idx >= 0 && idx < kSoundChannelCount) {
        volumes_[idx] = std::clamp(volume, 0.0f, 1.0f);
    }
}

float SoundSettings::GetVolume(SoundChannel ch) const {
    std::lock_guard lock(mutex_);
    auto idx = static_cast<int>(ch);
    if (idx >= 0 && idx < kSoundChannelCount) {
        return volumes_[idx];
    }
    return 0.0f;
}

void SoundSettings::SetChannelEnabled(SoundChannel ch, bool enabled) {
    std::lock_guard lock(mutex_);
    auto idx = static_cast<int>(ch);
    if (idx >= 0 && idx < kSoundChannelCount) {
        enabled_ch_[idx] = enabled;
    }
}

bool SoundSettings::IsChannelEnabled(SoundChannel ch) const {
    std::lock_guard lock(mutex_);
    auto idx = static_cast<int>(ch);
    if (idx >= 0 && idx < kSoundChannelCount) {
        return enabled_ch_[idx];
    }
    return false;
}

void SoundSettings::SetReverbEnabled(bool enabled) {
    std::lock_guard lock(mutex_);
    reverb_ = enabled;
}

bool SoundSettings::IsReverbEnabled() const {
    std::lock_guard lock(mutex_);
    return reverb_;
}

void SoundSettings::SetEAXEnabled(bool enabled) {
    std::lock_guard lock(mutex_);
    eax_ = enabled;
}

bool SoundSettings::IsEAXEnabled() const {
    std::lock_guard lock(mutex_);
    return eax_;
}

void SoundSettings::SetStereo(bool stereo) {
    std::lock_guard lock(mutex_);
    stereo_ = stereo;
}

bool SoundSettings::IsStereo() const {
    std::lock_guard lock(mutex_);
    return stereo_;
}

void SoundSettings::SetSampleRate(uint32_t rate) {
    std::lock_guard lock(mutex_);

    if (rate == 22050 || rate == 44100 || rate == 48000) {
        sampleRate_ = rate;
    }
}

uint32_t SoundSettings::GetSampleRate() const {
    std::lock_guard lock(mutex_);
    return sampleRate_;
}

void SoundSettings::SetBufferSize(uint32_t size) {
    std::lock_guard lock(mutex_);
    bufferSize_ = size;
}

uint32_t SoundSettings::GetBufferSize() const {
    std::lock_guard lock(mutex_);
    return bufferSize_;
}

void SoundSettings::SetSoftwareChannels(uint32_t count) {
    std::lock_guard lock(mutex_);
    swChannels_ = count;
}

uint32_t SoundSettings::GetSoftwareChannels() const {
    std::lock_guard lock(mutex_);
    return swChannels_;
}

void SoundSettings::MuteAll() {
    std::lock_guard lock(mutex_);
    muted_ = true;
}

void SoundSettings::UnmuteAll() {
    std::lock_guard lock(mutex_);
    muted_ = false;
}

bool SoundSettings::IsMuted() const {
    std::lock_guard lock(mutex_);
    return muted_;
}

void SoundSettings::SetListenerPosition(float x, float y, float z) {
    std::lock_guard lock(mutex_);
    listenerPos_ = {x, y, z};
}

void SoundSettings::SetListenerOrientation(float fx, float fy, float fz) {
    std::lock_guard lock(mutex_);
    listenerFX_ = fx;
    listenerFY_ = fy;
    listenerFZ_ = fz;
}

ListenerPosition SoundSettings::GetListenerPosition() const {
    std::lock_guard lock(mutex_);
    return listenerPos_;
}

void SoundSettings::ApplyDefaults() {
    std::lock_guard lock(mutex_);
    enabled_     = true;
    muted_       = false;
    reverb_      = false;

    eax_         = false;
    stereo_      = true;
    sampleRate_  = 44100;
    bufferSize_  = 4096;
    swChannels_  = 32;

    volumes_[0] = 1.0f;  volumes_[1] = 0.4f;  volumes_[2] = 1.0f;
    volumes_[3] = 0.6f;  volumes_[4] = 1.0f;  volumes_[5] = 1.0f;

    for (auto& e : enabled_ch_) e = true;

    listenerPos_ = {0.0f, 0.0f, 0.0f};
    listenerFX_  = 0.0f;
    listenerFY_  = 0.0f;
    listenerFZ_  = -1.0f;
}

void SoundSettings::Reset() {
    ApplyDefaults();
}

}

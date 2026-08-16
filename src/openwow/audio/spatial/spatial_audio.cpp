
#include "openwow/audio/spatial/spatial_audio.h"
#include "openwow/audio/playback/sound_engine.h"

#include <algorithm>
#include <cmath>

#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#endif
#include <SDL2/SDL.h>

namespace openwow::audio {

SpeakerMode DetectSpeakerMode() {

    if (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) {

        const char* driver = SDL_GetCurrentAudioDriver();
        if (driver) {

        }
    }

#ifdef __APPLE__

    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyStreamConfiguration,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    AudioDeviceID device_id = kAudioObjectUnknown;
    UInt32 size = sizeof(device_id);
    AudioObjectPropertyAddress default_addr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &default_addr, 0,
                                   nullptr, &size, &device_id) == noErr) {
        AudioBufferList* buffer_list = nullptr;
        size = 0;
        if (AudioObjectGetPropertyDataSize(device_id, &addr, 0, nullptr,
                                           &size) == noErr && size > 0) {
            buffer_list = static_cast<AudioBufferList*>(std::malloc(size));
            if (buffer_list &&
                AudioObjectGetPropertyData(device_id, &addr, 0, nullptr,
                                           &size, buffer_list) == noErr) {
                UInt32 ch_count = 0;
                for (UInt32 i = 0; i < buffer_list->mNumberBuffers; ++i) {
                    ch_count += buffer_list->mBuffers[i].mNumberChannels;
                }
                std::free(buffer_list);
                if (ch_count >= 8) return SpeakerMode::k7point1;
                if (ch_count >= 6) return SpeakerMode::kSurround;
                if (ch_count >= 4) return SpeakerMode::kQuad;
                if (ch_count >= 2) return SpeakerMode::kStereo;
                return SpeakerMode::kMono;
            }
            std::free(buffer_list);
        }
    }
#endif

    return SpeakerMode::kStereo;
}

const char* SpeakerModeToString(const SpeakerMode mode) {
    switch (mode) {
        case SpeakerMode::kMono:     return "Mono (1.0)";
        case SpeakerMode::kStereo:   return "Stereo (2.0)";
        case SpeakerMode::kQuad:     return "Quad (4.0)";
        case SpeakerMode::kSurround: return "Surround (5.1)";
        case SpeakerMode::k7point1:  return "Surround (7.1)";
        default:                     return "Unknown";
    }
}

void SpatialAudio::SetListener(const ListenerState& listener) {
    listener_ = listener;
}

void SpatialAudio::SetListenerPosition(float x, float y, float z, float facing) {
    listener_.position = {x, y, z};
    listener_.facing = facing;
    listener_.UpdateFromFacing();
}

std::uint32_t SpatialAudio::AddSource(const SpatialSource& source) {
    SpatialSource s = source;
    if (s.sourceId == 0) {
        s.sourceId = nextSourceId_++;
    } else if (s.sourceId >= nextSourceId_) {
        nextSourceId_ = s.sourceId + 1;
    }
    sources_[s.sourceId] = s;
    return s.sourceId;
}

void SpatialAudio::RemoveSource(std::uint32_t sourceId) {
    sources_.erase(sourceId);
}

void SpatialAudio::UpdateSourcePosition(std::uint32_t sourceId,
                                        float x, float y, float z) {
    auto it = sources_.find(sourceId);
    if (it != sources_.end()) {
        it->second.position = {x, y, z};
    }
}

std::optional<SpatialSource> SpatialAudio::GetSource(std::uint32_t sourceId) const {
    auto it = sources_.find(sourceId);
    if (it != sources_.end()) return it->second;
    return std::nullopt;
}

std::vector<SpatialSource> SpatialAudio::GetActiveSources() const {
    std::vector<SpatialSource> result;
    result.reserve(sources_.size());
    for (const auto& [id, src] : sources_) {
        if (src.active) result.push_back(src);
    }
    return result;
}

std::uint32_t SpatialAudio::GetActiveSourceCount() const {
    std::uint32_t count = 0;
    for (const auto& [id, src] : sources_) {
        if (src.active) ++count;
    }
    return count;
}

SpatialResult SpatialAudio::ComputeSpatial(
    const ListenerState& listener,
    const Vec3& sourcePos,
    float minDistance,
    float maxDistance) {

    SpatialResult result;
    result.distance = listener.position.DistanceTo(sourcePos);

    result.volume = ComputeDistanceAttenuation(result.distance, minDistance, maxDistance);
    result.culled = (result.volume <= 0.0f);

    if (!result.culled && result.distance > 0.1f) {
        result.pan = ComputePan(listener, sourcePos);
    }

    return result;
}

float SpatialAudio::ComputeDistanceAttenuation(
    float distance, float minDistance, float maxDistance) {
    return static_cast<float>(
        SoundEngine_Custom3DRolloff(minDistance, maxDistance, distance));
}

float SpatialAudio::ComputePan(
    const ListenerState& listener, const Vec3& sourcePos) {

    Vec3 toSource = sourcePos - listener.position;
    float lenXY = std::sqrt(toSource.x * toSource.x + toSource.y * toSource.y);
    if (lenXY < 1e-6f) return 0.0f;

    float dirX = toSource.x / lenXY;
    float dirY = toSource.y / lenXY;

    float pan = dirX * listener.right.x + dirY * listener.right.y;

    return std::clamp(pan, -1.0f, 1.0f);
}

void SpatialAudio::Update() {
    for (auto& [id, src] : sources_) {
        if (!src.active) continue;

        SpatialResult r = ComputeSpatial(
            listener_, src.position, src.minDistance, src.maxDistance);

        src.distanceToListener = r.distance;
        src.computedVolume = r.volume * src.baseVolume;
        src.computedPan = r.pan;

        if (r.distance > maxAudibleDistance_ * rolloffFactor_) {
            src.computedVolume = 0.0f;
        }
    }
}

void SpatialAudio::Reset() {
    sources_.clear();
    nextSourceId_ = 1;
    listener_ = ListenerState{};
    speaker_mode_ = SpeakerMode::kStereo;
}

SpeakerMode SpatialAudio::DetectAndCacheSpeakerMode() {
    speaker_mode_ = DetectSpeakerMode();
    return speaker_mode_;
}

int SpatialAudio::GetSpeakerChannelCount() const {
    switch (speaker_mode_) {
        case SpeakerMode::kMono:     return 1;
        case SpeakerMode::kStereo:   return 2;
        case SpeakerMode::kQuad:     return 4;
        case SpeakerMode::kSurround: return 6;
        case SpeakerMode::k7point1:  return 8;
        default:                     return 2;
    }
}

}

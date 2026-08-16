
#include "openwow/audio/playback/sfx_pool.h"

namespace openwow::audio {

void SFXPool::RegisterSound(std::uint32_t soundId,
                             float minInterval,
                             std::uint32_t maxConcurrent,
                             std::uint32_t priority) {
    SFXPoolEntry entry;
    entry.soundId       = soundId;
    entry.lastPlayTime  = -minInterval;
    entry.minInterval   = minInterval;
    entry.maxConcurrent = maxConcurrent;
    entry.currentPlaying = 0;
    entry.priority      = priority;
    entries_[soundId]   = entry;
}

bool SFXPool::CanPlay(std::uint32_t soundId) const {
    auto it = entries_.find(soundId);
    if (it == entries_.end()) return false;

    const auto& e = it->second;

    if (e.currentPlaying >= e.maxConcurrent) return false;

    if (GetTotalPlaying() >= globalConcurrentLimit_) return false;

    if ((elapsedTime_ - e.lastPlayTime) < e.minInterval) return false;

    return true;
}

bool SFXPool::Play(std::uint32_t soundId) {
    if (!CanPlay(soundId)) return false;

    auto& e = entries_[soundId];
    e.currentPlaying++;
    e.lastPlayTime = elapsedTime_;
    return true;
}

void SFXPool::StopOne(std::uint32_t soundId) {
    auto it = entries_.find(soundId);
    if (it != entries_.end() && it->second.currentPlaying > 0)
        it->second.currentPlaying--;
}

void SFXPool::StopAll(std::uint32_t soundId) {
    auto it = entries_.find(soundId);
    if (it != entries_.end())
        it->second.currentPlaying = 0;
}

std::uint32_t SFXPool::GetPlayingCount(std::uint32_t soundId) const {
    auto it = entries_.find(soundId);
    return (it != entries_.end()) ? it->second.currentPlaying : 0;
}

std::uint32_t SFXPool::GetRegisteredCount() const {
    return static_cast<std::uint32_t>(entries_.size());
}

bool SFXPool::IsRegistered(std::uint32_t soundId) const {
    return entries_.find(soundId) != entries_.end();
}

float SFXPool::GetTimeSinceLastPlay(std::uint32_t soundId) const {
    auto it = entries_.find(soundId);
    if (it == entries_.end()) return 0.0f;
    return elapsedTime_ - it->second.lastPlayTime;
}

void SFXPool::SetGlobalConcurrentLimit(std::uint32_t limit) {
    globalConcurrentLimit_ = limit;
}

std::uint32_t SFXPool::GetGlobalConcurrentLimit() const {
    return globalConcurrentLimit_;
}

std::uint32_t SFXPool::GetTotalPlaying() const {
    std::uint32_t total = 0;
    for (const auto& [id, e] : entries_)
        total += e.currentPlaying;
    return total;
}

void SFXPool::Update(float dt) {
    elapsedTime_ += dt;
}

void SFXPool::Reset() {
    entries_.clear();
    elapsedTime_ = 0.0f;
    globalConcurrentLimit_ = 32;
}

}


#pragma once

#include <cstdint>

namespace openwow::audio {

class SoundEngine;
class VoiceChatLoopback;

void VoiceChat_ApplyOutboundChatVolume(SoundEngine& engine,
                                        std::int16_t* samples,
                                        unsigned int sample_count);

void VoiceChat_SetOutboundChatVolume(float linear_gain) noexcept;

int VoiceChat_GetMicrophoneSignalLevel(SoundEngine& engine);

int VoiceChat_SetupSoundOutput(SoundEngine& engine,
                               void* sound_handle_ptr,
                               void* context_ptr,
                               int use_voice_system);

[[nodiscard]] bool VoiceChat_SetupAudioDrivers(SoundEngine& engine,
                                                float sample_rate_hz);
bool VoiceChat_SetCaptureEnabled(SoundEngine& engine, bool enabled);

bool VoiceChat_RecordLoopbackSound(VoiceChatLoopback& loopback,
                                   std::uint32_t max_record_seconds);

void VoiceChat_StopRecordingLoopbackSound(VoiceChatLoopback& loopback);
bool VoiceChat_SetLoopbackPlayback(VoiceChatLoopback& loopback, bool enabled);
[[nodiscard]] bool VoiceChat_IsRecordingLoopbackSound(VoiceChatLoopback& loopback);
[[nodiscard]] bool VoiceChat_IsPlayingLoopbackSound(VoiceChatLoopback& loopback);

}

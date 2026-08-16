
#pragma once

#include <cstdint>

namespace openwow::audio {

void AudioSystem_Shutdown();

void AudioSystem_ShutdownSoundProvider();

void AudioSystem_DestroyMixer();

void AudioSystem_ReleaseProviderRef();

namespace detail {

using AudioProviderReleaseCallback = void (*)(void* context);

void InstallAudioProviderShutdownRef(std::uint32_t ref_count,
                                     AudioProviderReleaseCallback release_callback,
                                     void* release_context);

[[nodiscard]] bool HasAudioProviderShutdownRef();
[[nodiscard]] std::uint32_t GetAudioProviderShutdownRefCount();
void ResetAudioProviderShutdownRef();

}

}

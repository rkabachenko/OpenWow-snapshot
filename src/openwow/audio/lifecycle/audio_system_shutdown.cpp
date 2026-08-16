
#include "openwow/audio/lifecycle/audio_system_shutdown.h"

#include "openwow/runtime/scheduling/evt_sched.h"

namespace openwow::audio {

namespace {

struct AudioProviderShutdownRefSlot {
  bool installed{false};
  std::uint32_t ref_count{0};
  detail::AudioProviderReleaseCallback release_callback{nullptr};
  void* release_context{nullptr};
};

AudioProviderShutdownRefSlot& MutableAudioProviderShutdownRefSlot() {
  static AudioProviderShutdownRefSlot slot;
  return slot;
}

void ClearAudioProviderShutdownRefSlot(AudioProviderShutdownRefSlot& slot) {
  slot = {};
}

}

namespace detail {

void InstallAudioProviderShutdownRef(
    const std::uint32_t ref_count,
    const AudioProviderReleaseCallback release_callback,
    void* const release_context) {
  auto& slot = MutableAudioProviderShutdownRefSlot();
  if (ref_count == 0) {
    ClearAudioProviderShutdownRefSlot(slot);
    return;
  }

  slot.installed = true;
  slot.ref_count = ref_count;
  slot.release_callback = release_callback;
  slot.release_context = release_context;
}

bool HasAudioProviderShutdownRef() {
  return MutableAudioProviderShutdownRefSlot().installed;
}

std::uint32_t GetAudioProviderShutdownRefCount() {
  const auto& slot = MutableAudioProviderShutdownRefSlot();
  return slot.installed ? slot.ref_count : 0;
}

void ResetAudioProviderShutdownRef() {
  ClearAudioProviderShutdownRefSlot(MutableAudioProviderShutdownRefSlot());
}

}

void AudioSystem_ShutdownSoundProvider() {
  openwow::core::EvtSched_RestoreStartupInputStateAfterAudioShutdown();
}

void AudioSystem_DestroyMixer() {}

void AudioSystem_ReleaseProviderRef() {
  auto& slot = MutableAudioProviderShutdownRefSlot();
  if (!slot.installed) {
    return;
  }

  --slot.ref_count;
  const bool should_release = slot.ref_count == 0;
  const auto release_callback = slot.release_callback;
  void* const release_context = slot.release_context;

  if (should_release && release_callback != nullptr) {
    release_callback(release_context);
  }

  ClearAudioProviderShutdownRefSlot(slot);
}

void AudioSystem_Shutdown() {
  openwow::core::EvtSched_ClearWindowTimerCallback();
  openwow::core::EvtSched_Shutdown();
  AudioSystem_ShutdownSoundProvider();
  AudioSystem_DestroyMixer();
  AudioSystem_ReleaseProviderRef();

}

}

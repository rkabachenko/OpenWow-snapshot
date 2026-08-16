#pragma once

#include "openwow/core/cmap_hashtable.h"
#include "openwow/foundation/hashing/retail_adler_seed.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "openwow/audio/playback/sound_asset_runtime_state.h"
#include "openwow/audio/playback/sound_playback_runtime_state.h"

namespace openwow::audio {
class AudioEngine;
class SoundEngine;
class VoiceChatLoopback;
struct WorldAudioCallbackRegistrationState;
struct SoundValidationCallbackRegistrationState;

class SoundRuntimeState : protected SoundAssetRuntimeState, protected SoundPlaybackRuntimeState {
protected:
  std::unique_ptr<AudioEngine> audio_engine_;
  std::unique_ptr<SoundEngine> sound_engine_;
  std::unique_ptr<VoiceChatLoopback> voice_loopback_;
  std::unique_ptr<WorldAudioCallbackRegistrationState> world_audio_callbacks_;
  std::unique_ptr<SoundValidationCallbackRegistrationState> sound_validation_callbacks_;
  std::shared_ptr<void> callback_lifetime_;
};
}

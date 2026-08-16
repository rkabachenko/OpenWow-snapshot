#include "openwow/audio/integration/game/game_audio_lua_bindings.h"

#include "openwow/audio/adapters/lua/sound_voicechat_lua_registration.h"
#include "openwow/ui/game/api/game_lua_api_sound.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

namespace openwow::audio::integration::lua {

namespace {

constexpr openwow::ui::LuaGlobalBinding kWorldMusicVoiceChatLuaBindings[] = {
    {"MusicPlayer_PlayPause",
     openwow::ui::game::detail::kMusicPlayer_PlayPause.handler},
    {"MusicPlayer_VolumeUp",
     openwow::ui::game::detail::kMusicPlayer_VolumeUp.handler},
    {"MusicPlayer_VolumeDown",
     openwow::ui::game::detail::kMusicPlayer_VolumeDown.handler},
    {"MusicPlayer_BackTrack",
     openwow::ui::game::detail::kMusicPlayer_BackTrack.handler},
    {"MusicPlayer_NextTrack",
     openwow::ui::game::detail::kMusicPlayer_NextTrack.handler},
    {"GetNumVoiceSessions",
     openwow::ui::game::detail::kGetNumVoiceSessions.handler},
    {"GetActiveVoiceChannel",
     openwow::ui::game::detail::kGetActiveVoiceChannel.handler},
    {"SetActiveVoiceChannel",
     openwow::ui::game::detail::kSetActiveVoiceChannel.handler},
    {"GetVoiceCurrentSessionID",
     openwow::ui::game::detail::kGetVoiceCurrentSessionID.handler},
    {"GetVoiceSessionInfo",
     openwow::ui::game::detail::kGetVoiceSessionInfo.handler},
    {"GetNumVoiceSessionMembersBySessionID",
     openwow::ui::game::detail::kGetNumVoiceSessionMembersBySessionID.handler},
    {"GetVoiceSessionMemberInfoBySessionID",
     openwow::ui::game::detail::kGetVoiceSessionMemberInfoBySessionID.handler},
    {"SetActiveVoiceChannelBySessionID",
     openwow::ui::game::detail::kSetActiveVoiceChannelBySessionID.handler},
    {"UnitIsSilenced", openwow::ui::game::detail::kUnitIsSilenced.handler},
    {"GetMuteStatus", openwow::ui::game::detail::kGetMuteStatus.handler},
    {"GetVoiceStatus", openwow::ui::game::detail::kGetVoiceStatus.handler},
    {"IsVoiceChatEnabled",
     openwow::ui::game::detail::kIsVoiceChatEnabled.handler},
    {"VoiceEnumerateOutputDevices",
     openwow::ui::game::detail::kVoiceEnumerateOutputDevices.handler},
    {"VoiceEnumerateCaptureDevices",
     openwow::ui::game::detail::kVoiceEnumerateCaptureDevices.handler},
    {"VoiceSelectOutputDevice",
     openwow::ui::game::detail::kVoiceSelectOutputDevice.handler},
    {"VoiceSelectCaptureDevice",
     openwow::ui::game::detail::kVoiceSelectCaptureDevice.handler},
    {"VoiceGetCurrentOutputDevice",
     openwow::ui::game::detail::kVoiceGetCurrentOutputDevice.handler},
    {"VoiceGetCurrentCaptureDevice",
     openwow::ui::game::detail::kVoiceGetCurrentCaptureDevice.handler},
    {"VoiceIsDisabledByClient",
     openwow::ui::game::detail::kVoiceIsDisabledByClient.handler},
    {"VoicePushToTalkStart",
     openwow::ui::game::detail::kVoicePushToTalkStart.handler},
    {"VoicePushToTalkStop",
     openwow::ui::game::detail::kVoicePushToTalkStop.handler},
    {"IsVoiceChatAllowed",
     openwow::ui::game::detail::kIsVoiceChatAllowed.handler},
    {"IsVoiceChatAllowedByServer",
     openwow::ui::game::detail::kIsVoiceChatAllowedByServer.handler},
    {"UnitIsTalking", openwow::ui::game::detail::kUnitIsTalking.handler},
};

}

openwow::ui::lua::NativeBindingCatalog
SharedSoundVoiceChatNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "audio.integration.sound_voice_chat",
      openwow::ui::lua::BindingScope::kShared,
      openwow::audio::GetSoundVoiceChatLuaBindings());
}

openwow::ui::lua::NativeBindingCatalog
WorldMusicVoiceChatNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "audio.integration.world_music_voice_chat",
      openwow::ui::lua::BindingScope::kWorld,
      kWorldMusicVoiceChatLuaBindings);
}

}

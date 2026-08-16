#pragma once

#include <cstdint>

namespace openwow::audio {
class SoundEngine;
class SoundRuntime;
class VoiceChatLoopback;
}

namespace openwow::game {

class WorldSession;

struct VolumeDuckingState {
  float time_elapsed = 0.0f;
  float inv_delay = 0.0f;
  float inv_transition = 0.0f;
  bool is_ducking = false;
  uint32_t active_remote_talker_count = 0;

  float sound_vol = 0.0f;
  float music_vol = 0.0f;
  float ambience_vol = 0.0f;

  float sound_delta = 0.0f;
  float music_delta = 0.0f;
  float ambience_delta = 0.0f;
};

enum class ComSatCommandType : uint32_t {
  kSessionVolume = 1,
  kMuteToggle = 2,
  kSensitivity = 3,
  kRemoteTalkerVol = 4,
  kSelectSession = 5,
  kDeselectSession = 6,
  kPriority = 7,
  kCodec = 8,
  kReportInterval = 9,
  kPushToTalkReassign = 10,
};

struct ComSatCommand {
  uint32_t type;
  uint32_t reserved;
  uint32_t param1;
  uint32_t param2;
  union {
    float float_val;
    int32_t int_val;
  };
  uint32_t param4;
};

static_assert(sizeof(ComSatCommand) == 24, "ComSatCommand must be 24 bytes");

struct ComSatSessionKey {
  std::uint32_t session_id_low{0};
  std::uint32_t session_id_high{0};
};

struct ComSatRuntimeStateSnapshot {
  bool active{false};
  std::uint32_t shutdown_deadline_tick_ms{0};
  bool driver_created{false};
  bool sound_io_initialized{false};
  std::uint32_t sound_io_slot_count{0};
  bool datagram_socket_open{false};
  bool datagram_socket_bound{false};
};

void VoiceChat_BindRegisteredCVars(openwow::audio::SoundRuntime& sound_runtime);

void VoiceChat_SelectOutputDriverIndex(openwow::audio::SoundEngine& engine,
                                       int driver_index);

void VoiceChat_SelectInputDriverIndex(openwow::audio::SoundEngine& engine,
                                      int driver_index);

void ComSat_UpdateDriverState();

int VoiceChat_IsDisabled();

int VoiceChat_CheckSingleInstance();

int VoiceChat_KeyNameToCode(const char *name);

uint32_t MiddleButton_NameToBitmask(const char *name);

void VoiceChat_StartVolumeDucking(openwow::audio::SoundEngine& engine,
                                  VolumeDuckingState &state);

void VoiceChat_StopVolumeDucking(openwow::audio::SoundEngine& engine,
                                 VolumeDuckingState &state);

void VoiceChat_InitVolumeDucking(openwow::audio::SoundEngine& engine,
                                 VolumeDuckingState &state);

void VoiceChat_UpdateVolumeDucking(openwow::audio::SoundEngine& engine,
                                   openwow::audio::VoiceChatLoopback& loopback,
                                   VolumeDuckingState &state,
                                   std::uint32_t tick_count_ms);

bool VoiceChat_IsVoiceActivityActive(openwow::audio::VoiceChatLoopback& loopback);

void VoiceChat_SetLocalPlayerTalking(bool talking);
void VoiceChat_SetLocalPlayerGuid(std::uint64_t player_guid);

[[nodiscard]] bool ReadVoiceChatCVarBool(const char *name, bool fallback = false);

void VoiceChat_RequestRecordingLoopback(openwow::audio::VoiceChatLoopback& loopback);

void VoiceChat_ResetActivityState(openwow::audio::VoiceChatLoopback& loopback);

void VoiceChat_ClearActiveComSatSessions();
void VoiceChat_AddActiveComSatSession(std::uint32_t session_id_low,
                                      std::uint32_t session_id_high);
void VoiceChat_RemoveActiveComSatSession(std::uint32_t session_id_low,
                                         std::uint32_t session_id_high);
[[nodiscard]] bool VoiceChat_StopTrackedLocalSpeaker(const WorldSession& session);
[[nodiscard]] bool VoiceChat_StopTrackedRemoteSpeaker(std::uint64_t guid);

void VoiceChat_ResetComSatRuntimeState(openwow::audio::VoiceChatLoopback& loopback);

void VoiceChat_LeaveAllSessions(const WorldSession& session);

void VoiceChat_UpdateDriverState_Full();

void *ComSatClient_Alloc(uint32_t size);

void ComSatClient_Free(void *ptr);

bool CVar_EnableMicrophone_OnChanged(openwow::audio::SoundEngine& engine,
                                     const char *new_value);

bool CVar_VoiceActivationSensitivity_OnChanged(const char *new_value);

void VoiceChat_InitComSatDriver(openwow::audio::SoundEngine& engine);

void VoiceChat_Initialize(openwow::audio::SoundRuntime& sound_runtime);

bool CVar_EnableVoiceChat_OnChanged(openwow::audio::SoundRuntime& sound_runtime,
                                    const char *old_value,
                                    const char *new_value);

void ComSat_ThreadProc();

void VoiceChat_HandlePushToTalkReassign();

void ComSat_Init();

void VoiceChat_Shutdown(openwow::audio::SoundRuntime& sound_runtime);

void VoiceChat_EnqueueFloatCommand(uint32_t type, float value, uint32_t param1, uint32_t param2);

void VoiceChat_EnqueueIntCommand(uint32_t type, int32_t value, uint32_t param1, uint32_t param2);

void VoiceChat_EnqueueComSatEvent(uint32_t type, uint32_t p1, uint32_t p2, uint32_t p3,
                                  uint32_t p4);

void VoiceChat_ProcessCommandQueue();

int VoiceChat_ScheduledUpdate(const WorldSession& session,
                              openwow::audio::SoundEngine& engine,
                              openwow::audio::VoiceChatLoopback& loopback,
                              std::uint32_t tick_count_ms);

[[nodiscard]] ComSatRuntimeStateSnapshot VoiceChat_GetComSatRuntimeStateSnapshot();

}

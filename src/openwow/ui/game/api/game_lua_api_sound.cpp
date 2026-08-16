#include "openwow/ui/game/api/game_lua_api_sound.h"
#include "openwow/audio/playback/sound_engine.h"
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/audio/voice/voice_chat_audio_setup.h"
#include "openwow/audio/voice/voice_chat_loopback.h"
#include "openwow/game/chat_system.h"
#include "openwow/game/comsat_client.h"
#include "openwow/game/group_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/voice_chat.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/runtime/movie_recording_runtime.h"
#include "openwow/ui/lua_numeric.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace openwow::ui::lua {

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::SoundLuaNumber, Policy>::Valid(
    lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::SoundLuaNumber, Policy>::Read(
    lua_State* state, const int index) noexcept -> Storage {
  if (index > lua_gettop(state) || lua_isnumber(state, index) == 0) {
    return {};
  }
  return {{static_cast<double>(lua_tonumber(state, index))}};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::SoundLuaNumber, Policy>::Argument(
    Storage value) noexcept -> Storage {
  return value;
}

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::SoundLuaString, Policy>::Valid(
    lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::SoundLuaString, Policy>::Read(
    lua_State* state, const int index) -> Storage {
  if (index > lua_gettop(state) || lua_isstring(state, index) == 0) {
    return {};
  }
  const char* value = lua_tostring(state, index);
  return {{value != nullptr ? value : ""}};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::SoundLuaString, Policy>::Argument(
    Storage value) noexcept -> Storage {
  return value;
}

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::SoundLuaValue, Policy>::Valid(
    lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::SoundLuaValue, Policy>::Read(
    lua_State* state, const int index) -> Storage {
  if (index > lua_gettop(state)) {
    return {};
  }
  if (lua_isnumber(state, index) != 0) {
    return {{static_cast<double>(lua_tonumber(state, index))}, std::nullopt};
  }
  if (lua_isstring(state, index) != 0) {
    const char* value = lua_tostring(state, index);
    return {std::nullopt, std::string(value != nullptr ? value : "")};
  }
  return {};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::SoundLuaValue, Policy>::Argument(
    Storage value) noexcept -> Storage {
  return value;
}

template <ConversionPolicy Policy>
bool LuaConverter<openwow::ui::game::detail::VoiceSelectorArgument,
                  Policy>::Valid(lua_State*, int) noexcept {
  return true;
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::VoiceSelectorArgument,
                  Policy>::Read(lua_State* state, const int index) -> Storage {
  if (index > lua_gettop(state) || lua_isnil(state, index) != 0) {
    return {};
  }
  if (lua_isstring(state, index) == 0) {
    return {openwow::ui::game::detail::VoiceSelectorArgumentKind::kInvalid,
            {}};
  }
  const char* value = lua_tostring(state, index);
  return {openwow::ui::game::detail::VoiceSelectorArgumentKind::kString,
          value != nullptr ? value : ""};
}

template <ConversionPolicy Policy>
auto LuaConverter<openwow::ui::game::detail::VoiceSelectorArgument,
                  Policy>::Argument(Storage value) noexcept -> Storage {
  return value;
}

template struct LuaConverter<openwow::ui::game::detail::SoundLuaNumber,
                             openwow::ui::game::detail::kSoundLuaConversion>;
template struct LuaConverter<openwow::ui::game::detail::SoundLuaString,
                             openwow::ui::game::detail::kSoundLuaConversion>;
template struct LuaConverter<openwow::ui::game::detail::SoundLuaValue,
                             openwow::ui::game::detail::kSoundLuaConversion>;
template struct LuaConverter<openwow::ui::game::detail::VoiceSelectorArgument,
                             openwow::ui::game::detail::kSoundLuaConversion>;

}

namespace openwow::ui::game::detail {

namespace {

constexpr std::uint32_t kScriptSoundTypeSound = 4;
constexpr std::uint32_t kScriptSoundTypeMusic = 5;

constexpr std::size_t kRetailSoundKitNameBufferSize = 0x100;

std::string CopyRetailSoundKitNameForLookup(const std::string_view value) {
  constexpr std::size_t kRetailSoundKitNameMaximumLength =
      kRetailSoundKitNameBufferSize - 1;
  return std::string(value.substr(0, kRetailSoundKitNameMaximumLength));
}
constexpr int kVoiceSessionCapacity = 32;
constexpr int kVoiceSessionMemberCapacity = 40;
constexpr std::uint8_t kVoiceStatusTalking = 0x04;
constexpr std::uint8_t kVoiceStatusMuted = 0x08;
constexpr std::uint8_t kVoiceStatusConnected = 0x40;
constexpr char kPartyVoiceSessionName[] = "Party";
constexpr char kRaidVoiceSessionName[] = "Raid";
constexpr char kBattlegroundVoiceSessionName[] = "Battleground";

openwow::audio::SoundRuntime& SoundRuntime(
    SoundLuaContext& context) {
  return context.sound_runtime;
}

openwow::audio::SoundEngine& SoundEngine(
    SoundLuaContext& context) {
  return SoundRuntime(context).sound_engine();
}

openwow::audio::VoiceChatLoopback& VoiceLoopback(
    SoundLuaContext& context) {
  return SoundRuntime(context).voice_loopback();
}

std::mutex s_external_music_player_callback_mutex;
ExternalMusicPlayerControlCallback s_external_music_player_callback;

void DispatchExternalMusicPlayerCommand(const ExternalMusicPlayerCommand command) {
  ExternalMusicPlayerControlCallback callback;
  {
    const std::scoped_lock lock(s_external_music_player_callback_mutex);
    callback = s_external_music_player_callback;
  }
  if (callback) {
    callback(command);
  }
}

}

void BindSoundLuaContext(lua_State& state, SoundLuaContext& context) {
  lua_pushlightuserdata(&state, &context);
  lua_setfield(
      &state, LUA_REGISTRYINDEX,
      openwow::ui::lua::LuaRegistryContext<SoundLuaContext>::key.data());
}

bool SoundLuaContext::CanDispatchScriptAudio() const noexcept {
  return movie_recording == nullptr || !movie_recording->IsRecording() ||
         movie_recording->IsCapturingSound();
}

void SetExternalMusicPlayerControlCallback(ExternalMusicPlayerControlCallback callback) {
  const std::scoped_lock lock(s_external_music_player_callback_mutex);
  s_external_music_player_callback = std::move(callback);
}

namespace {

using openwow::game::ObjectGuid;
using openwow::game::VoiceChat;
using openwow::game::VoiceChatChannelType;
using openwow::game::WorldSession;

enum class VoiceSessionSelectorMode : std::uint8_t {
  kCurrentSession = 0,
  kInvalidSelector,
  kChannelType,
  kNamedSession,
};

struct VoiceSessionSelector {
  VoiceSessionSelectorMode mode{VoiceSessionSelectorMode::kCurrentSession};
  VoiceChatChannelType channel_type{VoiceChatChannelType::kCustom};
  std::string session_name;
};

std::optional<ObjectGuid> ResolveVoiceTargetGuid(WorldSession *session,
                                                 const char *name_or_unit_id) {
  if (!session || !name_or_unit_id || name_or_unit_id[0] == '\0') {
    return std::nullopt;
  }

  if (const auto guid = session->objects().FindPlayerGuidByName(name_or_unit_id);
      guid.has_value() && !guid->IsEmpty()) {
    return guid;
  }

  const auto unit_guid = ResolveUnitId(session, name_or_unit_id);
  if (!unit_guid.IsEmpty()) {
    return unit_guid;
  }

  return std::nullopt;
}

VoiceSessionSelector ParseVoiceSessionSelector(const char *raw_selector) {
  if (!raw_selector || raw_selector[0] == '\0') {
    VoiceSessionSelector selector;
    selector.mode = VoiceSessionSelectorMode::kInvalidSelector;
    return selector;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(raw_selector, kPartyVoiceSessionName)) {
    VoiceSessionSelector selector;
    selector.mode = VoiceSessionSelectorMode::kChannelType;
    selector.channel_type = VoiceChatChannelType::kParty;
    return selector;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(raw_selector, kRaidVoiceSessionName)) {
    VoiceSessionSelector selector;
    selector.mode = VoiceSessionSelectorMode::kChannelType;
    selector.channel_type = VoiceChatChannelType::kRaid;
    return selector;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(raw_selector, kBattlegroundVoiceSessionName)) {
    VoiceSessionSelector selector;
    selector.mode = VoiceSessionSelectorMode::kChannelType;
    selector.channel_type = VoiceChatChannelType::kBattleground;
    return selector;
  }

  VoiceSessionSelector selector;
  selector.mode = VoiceSessionSelectorMode::kNamedSession;
  selector.session_name = raw_selector;
  return selector;
}

bool IsVoiceSessionPlayerMuted(const VoiceChat &voice_chat, const VoiceSessionSelector &selector,
                               const ObjectGuid guid) {
  switch (selector.mode) {
  case VoiceSessionSelectorMode::kCurrentSession:
    return voice_chat.IsPlayerMutedInCurrentSession(guid);
  case VoiceSessionSelectorMode::kInvalidSelector:
    return false;
  case VoiceSessionSelectorMode::kChannelType:
    return voice_chat.IsPlayerMutedInChannelType(selector.channel_type, guid);
  case VoiceSessionSelectorMode::kNamedSession:
    return voice_chat.IsSessionPlayerMuted(selector.session_name, guid);
  }

  return false;
}

std::optional<std::uint8_t> ResolveVoiceSessionMemberStatusFlags(
    const VoiceChat& voice_chat, const VoiceSessionSelector& selector, const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return std::nullopt;
  }

  std::optional<std::uint32_t> session_ordinal;
  switch (selector.mode) {
  case VoiceSessionSelectorMode::kCurrentSession:
    session_ordinal = voice_chat.GetCurrentSessionOrdinal();
    break;
  case VoiceSessionSelectorMode::kInvalidSelector:
    return std::nullopt;
  case VoiceSessionSelectorMode::kChannelType:
    session_ordinal = voice_chat.GetSessionOrdinalByChannel(selector.channel_type, {});
    break;
  case VoiceSessionSelectorMode::kNamedSession:
    session_ordinal =
        voice_chat.GetSessionOrdinalByChannel(VoiceChatChannelType::kCustom, selector.session_name);
    break;
  }

  if (!session_ordinal.has_value()) {
    return std::nullopt;
  }

  return voice_chat.GetSessionMemberStatusFlagsByOrdinal(*session_ordinal, guid);
}

std::string ResolveVoiceSessionMemberName(WorldSession* session, const ObjectGuid guid) {
  const std::string unknown_name =
      openwow::game::Localization::Get().GetString("UNKNOWN", "UNKNOWN");
  if (session == nullptr || guid.IsEmpty()) {
    return unknown_name;
  }

  const std::string cached_name = session->objects().GetPlayerName(guid);
  if (!cached_name.empty()) {
    return cached_name;
  }

  if (const auto* name_info = session->query_cache().GetOrRequestPlayerName(guid.GetRawValue());
      name_info != nullptr && !name_info->name.empty()) {
    return name_info->name;
  }

  return unknown_name;
}

int ReadVoiceSessionIndexArgument(const double value) {
  return TruncateLuaNumberToSseI32(value);
}

std::string ResolveVoiceSessionDisplayName(const openwow::game::VoiceSessionOrdinalInfo &session) {
  const char *key = nullptr;
  switch (session.channel_type) {
  case VoiceChatChannelType::kBattleground:
    key = "CHAT_MSG_BATTLEGROUND";
    break;
  case VoiceChatChannelType::kParty:
    key = "CHAT_MSG_PARTY";
    break;
  case VoiceChatChannelType::kRaid:
    key = "CHAT_MSG_RAID";
    break;
  case VoiceChatChannelType::kCustom:
    return session.session_name;
  }

  return openwow::game::Localization::Get().GetString(key, key);
}

std::optional<std::uint32_t> ResolveVoiceSessionOrdinalForDisplaySlot(
    const std::uint32_t display_slot) {
  auto& chat_system = openwow::game::ChatSystem::Get();
  const auto display_info = chat_system.GetDisplayChannelInfo(display_slot);
  if (!display_info.has_value() || display_info->is_header || !display_info->voice_enabled) {
    return std::nullopt;
  }

  VoiceChatChannelType session_type = VoiceChatChannelType::kCustom;
  std::string session_name;
  switch (display_info->kind) {
  case openwow::game::DisplayChannelKind::kJoinedChannel: {
    const auto resolved = chat_system.ResolveDisplayChannel(display_slot);
    if (!resolved.has_value() || !resolved->channel.has_value()) {
      return std::nullopt;
    }

    session_name = resolved->channel->name;
    break;
  }
  case openwow::game::DisplayChannelKind::kSpecialSlot1:
    session_type = VoiceChatChannelType::kBattleground;
    break;
  case openwow::game::DisplayChannelKind::kSpecialSlot2:
    session_type = VoiceChatChannelType::kParty;
    break;
  case openwow::game::DisplayChannelKind::kSpecialSlot3:
    session_type = VoiceChatChannelType::kRaid;
    break;
  case openwow::game::DisplayChannelKind::kInvalid:
    return std::nullopt;
  }

  return VoiceChat::Get().GetSessionOrdinalByChannel(session_type, session_name);
}

void ApplyActiveVoiceChannelSelection(WorldSession &session,
                                      const std::optional<std::uint32_t> ordinal) {
  (void)openwow::game::VoiceChat_ApplyActiveSessionSelection(session, ordinal);
}

int GetEnumeratedOutputDriverCount(
    SoundLuaContext& context,
    const bool use_voice_output_devices) {
  return SoundRuntime(context).GetEnumeratedOutputDriverCount(
      use_voice_output_devices);
}

std::variant<int, openwow::ui::lua::LuaUsageError> ReadDriverIndex(
    const SoundLuaNumber index, const char* usage) {
  if (!index.value) {
    return openwow::ui::lua::LuaUsageError{usage};
  }
  return openwow::ui::TruncateLuaNumberToI32(*index.value);
}

SoundStringResult GetEnumeratedOutputDriverName(
    SoundLuaContext& context,
    const SoundLuaNumber index, const bool use_voice_output_devices,
    const char* usage) {
  const auto driver_index = ReadDriverIndex(index, usage);
  if (const auto* error =
          std::get_if<openwow::ui::lua::LuaUsageError>(&driver_index)) {
    return *error;
  }
  return SoundRuntime(context).GetEnumeratedOutputDriverName(
      std::get<int>(driver_index), use_voice_output_devices);
}

int GetEnumeratedGameInputDriverCount(
    SoundLuaContext& context) {
  return SoundRuntime(context).GetEnumeratedGameInputDriverCount();
}

SoundStringResult GetEnumeratedGameInputDriverName(
    SoundLuaContext& context,
    const SoundLuaNumber index, const char* usage) {
  const auto driver_index = ReadDriverIndex(index, usage);
  if (const auto* error =
          std::get_if<openwow::ui::lua::LuaUsageError>(&driver_index)) {
    return *error;
  }
  return SoundRuntime(context).GetEnumeratedGameInputDriverName(
      std::get<int>(driver_index));
}

int GetEnumeratedRecordInputDriverCount(
    SoundLuaContext& context) {
  return SoundRuntime(context).GetEnumeratedRecordInputDriverCount();
}

SoundStringResult GetEnumeratedRecordInputDriverName(
    SoundLuaContext& context,
    const SoundLuaNumber index, const char* usage) {
  const auto driver_index = ReadDriverIndex(index, usage);
  if (const auto* error =
          std::get_if<openwow::ui::lua::LuaUsageError>(&driver_index)) {
    return *error;
  }
  return SoundRuntime(context).GetEnumeratedRecordInputDriverName(
      std::get<int>(driver_index));
}

}

SoundVoidResult PlaySound(
    SoundLuaContext& context,
    const SoundLuaValue sound_argument) {
  if (!context.CanDispatchScriptAudio()) {
    return openwow::ui::lua::NoLuaResults{};
  }

  auto& sound = SoundRuntime(context);
  if (sound_argument.number) {

    const auto sound_id = static_cast<std::uint32_t>(
        openwow::ui::TruncateLuaNumberToI32(*sound_argument.number));
    (void)sound.PlayVoiceChatToggle(sound_id);
    return openwow::ui::lua::NoLuaResults{};
  }

  if (sound_argument.string) {
    const auto sound_id = sound.LookupSoundKitIdByName(
        CopyRetailSoundKitNameForLookup(*sound_argument.string));
    (void)sound.PlayVoiceChatToggle(sound_id);
    return openwow::ui::lua::NoLuaResults{};
  }

  return openwow::ui::lua::LuaUsageError{"Usage: PlaySound(\"sound\")"};
}

SoundNumberResult PlaySoundFile(
    SoundLuaContext& context,
    const SoundLuaString path) {
  if (!context.CanDispatchScriptAudio()) {
    return openwow::ui::lua::NoLuaResults{};
  }
  if (!path.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: PlaySoundFile(\"soundfile\")"};
  }

  SoundRuntime(context).PlayScriptSound(*path.value, kScriptSoundTypeSound);
  return 1.0;
}

SoundNumberResult PlayMusic(
    SoundLuaContext& context,
    const SoundLuaString path) {
  if (!context.CanDispatchScriptAudio()) {
    return openwow::ui::lua::NoLuaResults{};
  }
  if (!path.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: PlayMusic(\"musicfile\")"};
  }
  SoundRuntime(context).PlayScriptSound(*path.value, kScriptSoundTypeMusic);
  return 1.0;
}

void StopMusic(SoundLuaContext& context) {
  SoundRuntime(context).StopScriptMusic();
}

void MusicPlayerPlayPause() {
  DispatchExternalMusicPlayerCommand(ExternalMusicPlayerCommand::kPlayPause);
}

void MusicPlayerVolumeUp() {
  DispatchExternalMusicPlayerCommand(ExternalMusicPlayerCommand::kVolumeUp);
}

void MusicPlayerVolumeDown() {
  DispatchExternalMusicPlayerCommand(ExternalMusicPlayerCommand::kVolumeDown);
}

void MusicPlayerBackTrack() {
  DispatchExternalMusicPlayerCommand(ExternalMusicPlayerCommand::kBackTrack);
}

void MusicPlayerNextTrack() {
  DispatchExternalMusicPlayerCommand(ExternalMusicPlayerCommand::kNextTrack);
}

int SoundGameSystemGetNumOutputDrivers(
    SoundLuaContext& context) {
  return GetEnumeratedOutputDriverCount(context, false);
}

SoundStringResult SoundGameSystemGetOutputDriverNameByIndex(
    SoundLuaContext& context,
    const SoundLuaNumber index) {
  return GetEnumeratedOutputDriverName(
      context, index, false,
      "Usage: Sound_GetOutputDriverNameByIndex(OutputDriverIndex)");
}

void SoundGameSystemRestartSoundSystem(
    SoundLuaContext& context) {
  SoundRuntime(context).RestartGameSoundSystemFromLua();
}

SoundStringResult SoundChatSystemGetInputDriverNameByIndex(
    SoundLuaContext& context,
    const SoundLuaNumber index) {
  return GetEnumeratedRecordInputDriverName(
      context, index,
      "Usage: Sound_ChatSystem_GetInputDriverNameByIndex(InputDriverIndex)");
}

int SoundChatSystemGetNumInputDrivers(
    SoundLuaContext& context) {
  return GetEnumeratedRecordInputDriverCount(context);
}

int SoundChatSystemGetNumOutputDrivers(
    SoundLuaContext& context) {
  return GetEnumeratedOutputDriverCount(context, true);
}

SoundStringResult SoundChatSystemGetOutputDriverNameByIndex(
    SoundLuaContext& context,
    const SoundLuaNumber index) {
  return GetEnumeratedOutputDriverName(
      context, index, true,
      "Usage: Sound_ChatSystem_GetOutputDriverNameByIndex(OutputDriverIndex)");
}

SoundStringResult SoundGameSystemGetInputDriverNameByIndex(
    SoundLuaContext& context,
    const SoundLuaNumber index) {
  return GetEnumeratedGameInputDriverName(
      context, index,
      "Usage: Sound_GameSystem_GetInputDriverNameByIndex(InputDriverIndex)");
}

int SoundGameSystemGetNumInputDrivers(
    SoundLuaContext& context) {
  return GetEnumeratedGameInputDriverCount(context);
}

std::optional<double> GetNumChannelMembers(
    SoundLuaContext& context,
    const SoundLuaNumber display_index) {
  using openwow::game::ChatSystem;
  using openwow::game::DisplayChannelKind;
  using openwow::game::GroupSystem;

  if (!display_index.value) {
    return std::nullopt;
  }

  const auto one_based =
      openwow::ui::SaturateLuaNumberToU32(*display_index.value);
  const auto resolved =
      ChatSystem::Get().ResolveDisplayChannel(static_cast<std::size_t>(one_based - 1u));
  if (!resolved || resolved->kind == DisplayChannelKind::kInvalid) {
    return std::nullopt;
  }

  auto &chat_system = ChatSystem::Get();

  switch (resolved->kind) {
  case DisplayChannelKind::kJoinedChannel: {
    if (!resolved->channel) {
      return std::nullopt;
    }

    if (chat_system.IsWatchingJoinedChannel(resolved->channel->name)) {
      if (chat_system.GetWatchedChannelRosterPendingQueries() != 0) {
        return std::nullopt;
      }

      const auto roster_size = chat_system.GetWatchedChannelRosterSize();
      if (roster_size == 0) {
        return std::nullopt;
      }

      return static_cast<double>(roster_size);
    }

    chat_system.SelectWatchedJoinedChannel(resolved->channel->name);

    if (auto* session = context.world_session) {
      session->interaction().SendChannelCommand(
          static_cast<std::uint16_t>(
              openwow::net::wotlk::Opcode::CMSG_SET_CHANNEL_WATCH),
          resolved->channel->name);
      session->interaction().SendChannelCommand(
          static_cast<std::uint16_t>(
              openwow::net::wotlk::Opcode::CMSG_CHANNEL_DISPLAY_LIST),
          resolved->channel->name);
    }

    return std::nullopt;
  }

  case DisplayChannelKind::kSpecialSlot2: {

    const auto party_count = GroupSystem::Get().GetTrackedPartyMemberCount();
    const std::uint32_t count = (party_count != 0) ? (party_count + 1u) : 0u;
    if (count == 0) {
      return std::nullopt;
    }
    return static_cast<double>(count);
  }

  case DisplayChannelKind::kSpecialSlot1:
  case DisplayChannelKind::kSpecialSlot3: {

    const auto count = GroupSystem::Get().GetRealRaidMemberCount();
    if (count == 0) {
      return std::nullopt;
    }
    return static_cast<double>(count);
  }

  default:
    return std::nullopt;
  }
}

int GetNumVoiceSessions() {
  return static_cast<int>(VoiceChat::Get().GetSessionCount());
}

std::optional<double> GetActiveVoiceChannel() {
  const auto active_slot = VoiceChat::Get().GetActiveVoiceDisplaySlot();
  if (!active_slot.has_value() || *active_slot == 0u) {
    return std::nullopt;
  }

  return static_cast<double>(*active_slot + 1u);
}

void SetActiveVoiceChannel(
    SoundLuaContext& context,
    const SoundLuaNumber display_index) {
  if (!display_index.value) {
    return;
  }

  auto* session = context.world_session;
  if (session == nullptr) {
    return;
  }

  const auto requested_display_slot =
      openwow::ui::SaturateLuaNumberToU32(*display_index.value);
  if (requested_display_slot == 0u) {
    ApplyActiveVoiceChannelSelection(*session, std::nullopt);
    return;
  }

  const auto target_ordinal = ResolveVoiceSessionOrdinalForDisplaySlot(requested_display_slot - 1u);
  if (!target_ordinal.has_value()) {
    return;
  }

  ApplyActiveVoiceChannelSelection(*session, *target_ordinal);
}

std::optional<double> GetVoiceCurrentSessionID() {
  const auto current_session = VoiceChat::Get().GetCurrentSessionOrdinal();
  if (!current_session.has_value()) {
    return std::nullopt;
  }

  return static_cast<double>(*current_session);
}

VoiceSessionInfoResult GetVoiceSessionInfo(const SoundLuaNumber session_id_argument) {
  if (!session_id_argument.value) {
    return openwow::ui::lua::LuaNil{};
  }

  const int session_id = ReadVoiceSessionIndexArgument(*session_id_argument.value);
  if (session_id < 0 || session_id >= kVoiceSessionCapacity) {
    return openwow::ui::lua::LuaNil{};
  }

  const auto session_info = VoiceChat::Get().GetSessionByOrdinal(
      static_cast<std::uint32_t>(session_id));
  if (!session_info.has_value()) {
    return openwow::ui::lua::LuaNil{};
  }

  return openwow::ui::lua::LuaReturns<std::string,
                                      openwow::ui::lua::LuaTruthy>(
      ResolveVoiceSessionDisplayName(*session_info),
      openwow::ui::lua::LuaTruthy{session_info->active});
}

OptionalNumberResult GetNumVoiceSessionMembersBySessionID(
    const SoundLuaNumber session_id_argument) {
  if (!session_id_argument.value) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const int session_id = ReadVoiceSessionIndexArgument(*session_id_argument.value);
  if (session_id < 0 || session_id >= kVoiceSessionCapacity) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto member_count =
      VoiceChat::Get().GetSessionMemberCountByOrdinal(static_cast<std::uint32_t>(session_id));
  if (!member_count.has_value()) {
    return openwow::ui::lua::NoLuaResults{};
  }

  return static_cast<double>(*member_count);
}

VoiceSessionMemberInfoResult GetVoiceSessionMemberInfoBySessionID(
    SoundLuaContext& context,
    const SoundLuaNumber session_id_argument,
    const SoundLuaNumber member_id_argument) {
  if (!session_id_argument.value || !member_id_argument.value) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const int session_id = ReadVoiceSessionIndexArgument(*session_id_argument.value);
  const int member_id = ReadVoiceSessionIndexArgument(*member_id_argument.value);
  if (session_id <= 0 || session_id >= kVoiceSessionCapacity || member_id <= 0 ||
      member_id > kVoiceSessionMemberCapacity) {
    return openwow::ui::lua::NoLuaResults{};
  }

  auto* session = context.world_session;
  if (session == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto member = VoiceChat::Get().GetSessionMemberByOrdinal(
      static_cast<std::uint32_t>(session_id), static_cast<std::uint32_t>(member_id));
  if (!member.has_value()) {
    return openwow::ui::lua::NoLuaResults{};
  }

  return openwow::ui::lua::LuaReturns<
      std::string, openwow::ui::lua::LuaTruthy,
      openwow::ui::lua::LuaTruthy, openwow::ui::lua::LuaTruthy,
      openwow::ui::lua::LuaTruthy>(
      ResolveVoiceSessionMemberName(session, member->guid),
      openwow::ui::lua::LuaTruthy{
          (member->status_flags & kVoiceStatusTalking) != 0},
      openwow::ui::lua::LuaTruthy{
          (member->status_flags & kVoiceStatusConnected) != 0},
      openwow::ui::lua::LuaTruthy{
          VoiceChat::Get().IsPlayerMuted(member->guid)},
      openwow::ui::lua::LuaTruthy{
          (member->status_flags & kVoiceStatusMuted) != 0});
}

openwow::ui::lua::LuaTruthy SetActiveVoiceChannelBySessionID(
    SoundLuaContext& context,
    const SoundLuaNumber session_id_argument) {
  if (!session_id_argument.value) {
    return {false};
  }

  auto* session = context.world_session;
  if (session == nullptr) {
    return {false};
  }

  const int session_id = ReadVoiceSessionIndexArgument(*session_id_argument.value);
  if (session_id <= 0) {
    ApplyActiveVoiceChannelSelection(*session, std::nullopt);
    return {true};
  }

  if (session_id >= kVoiceSessionCapacity) {
    return {false};
  }

  const auto session_info = VoiceChat::Get().GetSessionByOrdinal(
      static_cast<std::uint32_t>(session_id));
  if (!session_info.has_value()) {
    return {false};
  }

  ApplyActiveVoiceChannelSelection(*session, static_cast<std::uint32_t>(session_id));
  return {true};
}

OptionalTruthyResult UnitIsSilenced(
    SoundLuaContext& context,
    const SoundLuaString target, const VoiceSelectorArgument selector_argument) {
  if (!target.value) {
    return openwow::ui::lua::NoLuaResults{};
  }

  auto* session = context.world_session;
  if (session == nullptr) {
    return openwow::ui::lua::LuaTruthy{false};
  }

  const auto guid = ResolveVoiceTargetGuid(session, target.value->c_str());
  if (!guid.has_value()) {
    return openwow::ui::lua::LuaTruthy{false};
  }

  const auto selector = selector_argument.kind == VoiceSelectorArgumentKind::kAbsent
                            ? VoiceSessionSelector{}
                            : ParseVoiceSessionSelector(
                                  selector_argument.kind ==
                                          VoiceSelectorArgumentKind::kString
                                      ? selector_argument.value.c_str()
                                      : nullptr);
  return openwow::ui::lua::LuaTruthy{
      IsVoiceSessionPlayerMuted(VoiceChat::Get(), selector, *guid)};
}

OptionalTruthyResult GetMuteStatus(
    SoundLuaContext& context,
    const SoundLuaString target, const SoundLuaString selector_argument) {
  if (!target.value) {
    return openwow::ui::lua::NoLuaResults{};
  }

  auto* session = context.world_session;
  if (session == nullptr) {
    return openwow::ui::lua::LuaTruthy{false};
  }
  const auto guid = ResolveVoiceTargetGuid(session, target.value->c_str());
  if (!guid.has_value()) {
    return openwow::ui::lua::LuaTruthy{false};
  }

  bool muted = session->social().IsMuted(*guid);
  if (!muted) {
    const auto selector = selector_argument.value
                              ? ParseVoiceSessionSelector(
                                    selector_argument.value->c_str())
                              : VoiceSessionSelector{};
    muted = IsVoiceSessionPlayerMuted(VoiceChat::Get(), selector, *guid);
  }

  return openwow::ui::lua::LuaTruthy{muted};
}

OptionalTruthyResult GetVoiceStatus(
    SoundLuaContext& context,
    const SoundLuaString target, const SoundLuaString selector_argument) {
  if (!target.value) {
    return openwow::ui::lua::NoLuaResults{};
  }

  auto* session = context.world_session;
  if (session == nullptr) {
    return openwow::ui::lua::NoLuaResults{};
  }
  const auto guid = ResolveVoiceTargetGuid(session, target.value->c_str());
  if (!guid.has_value()) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto& voice_chat = VoiceChat::Get();
  if (selector_argument.value) {
    const auto status_flags =
        ResolveVoiceSessionMemberStatusFlags(voice_chat,
                                             ParseVoiceSessionSelector(
                                                 selector_argument.value->c_str()),
                                             *guid);
    return openwow::ui::lua::LuaTruthy{
        status_flags.has_value() &&
        (*status_flags & kVoiceStatusConnected) != 0u};
  }

  const auto status_flags =
      ResolveVoiceSessionMemberStatusFlags(voice_chat, VoiceSessionSelector{}, *guid);
  return openwow::ui::lua::LuaTruthy{
      status_flags.has_value() &&
      (*status_flags & kVoiceStatusTalking) != 0u};
}

openwow::ui::lua::LuaTruthy IsVoiceChatEnabled() {
  return {openwow::game::VoiceChat::Get().IsEnabledAndActive()};
}

openwow::ui::lua::LuaTruthy IsVoiceChatAllowed() {
  return {VoiceChat::Get().IsAllowedAndEnabled()};
}

openwow::ui::lua::LuaTruthy IsVoiceChatAllowedByServer() {
  return {VoiceChat::Get().IsServerAllowed()};
}

openwow::ui::lua::LuaTruthy VoiceIsDisabledByClient() {
  return {!VoiceChat::Get().IsEnabled()};
}

std::optional<openwow::ui::lua::LuaTruthy> UnitIsTalking(
    SoundLuaContext& context,
    const SoundLuaString target) {
  if (!target.value) {
    return std::nullopt;
  }

  auto* session = context.world_session;
  if (session == nullptr) {
    return std::nullopt;
  }

  const auto guid = ResolveVoiceTargetGuid(session, target.value->c_str());
  if (!guid.has_value()) {
    return std::nullopt;
  }

  const auto status_flags =
      ResolveVoiceSessionMemberStatusFlags(VoiceChat::Get(), VoiceSessionSelector{}, *guid);
  return openwow::ui::lua::LuaTruthy{
      status_flags.has_value() &&
      (*status_flags & kVoiceStatusTalking) != 0u};
}

void VoiceChatStopPlayingLoopbackSound(
    SoundLuaContext& context) {
  VoiceLoopback(context).StopPlaying();
}

OptionalStringResult VoiceEnumerateOutputDevices(
    SoundLuaContext& context,
    const SoundLuaNumber index_argument) {
  if (!index_argument.value) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto snapshot =
      openwow::game::VoiceChat_GetComSatRuntimeStateSnapshot();
  if (!snapshot.driver_created) {
    return openwow::ui::lua::LuaNil{};
  }

  const int index = static_cast<int>(*index_argument.value);
  auto& sound = SoundRuntime(context);
  const int count =
      sound.GetEnumeratedOutputDriverCount(true);

  if (index < 0 || index >= count) {
    return openwow::ui::lua::LuaNil{};
  }

  return sound.GetEnumeratedOutputDriverName(
      index, true);
}

OptionalStringResult VoiceEnumerateCaptureDevices(
    SoundLuaContext& context,
    const SoundLuaNumber index_argument) {
  if (!index_argument.value) {
    return openwow::ui::lua::NoLuaResults{};
  }

  const auto snapshot =
      openwow::game::VoiceChat_GetComSatRuntimeStateSnapshot();
  if (!snapshot.driver_created) {
    return openwow::ui::lua::LuaNil{};
  }

  const int index = static_cast<int>(*index_argument.value);
  auto& sound = SoundRuntime(context);
  const int count = sound.GetEnumeratedGameInputDriverCount();
  if (index < 0 || index >= count) {
    return openwow::ui::lua::LuaNil{};
  }

  return sound.GetEnumeratedGameInputDriverName(index);
}

void VoiceSelectOutputDevice(const SoundLuaNumber index) {
  if (!index.value) {
    return;
  }

  openwow::audio::SetVoiceChatOutputDriverIndexCVar(
      static_cast<int>(*index.value));
}

void VoiceSelectCaptureDevice(const SoundLuaNumber index) {
  if (!index.value) {
    return;
  }

  openwow::audio::SetVoiceChatInputDriverIndexCVar(
      static_cast<int>(*index.value));
}

std::optional<std::string> VoiceGetCurrentOutputDevice(
    SoundLuaContext& context) {
  const std::string& name =
      SoundEngine(context).GetCurrentVoiceOutputDeviceName();
  return name.empty() ? std::nullopt : std::optional<std::string>{name};
}

std::optional<std::string> VoiceGetCurrentCaptureDevice(
    SoundLuaContext& context) {
  const std::string& name = SoundEngine(context).GetCurrentInputDeviceName();
  return name.empty() ? std::nullopt : std::optional<std::string>{name};
}

void VoicePushToTalkStart(
    SoundLuaContext& context) {
  auto& engine = SoundEngine(context);
  if (engine.IsVoiceChatEnabled()) {
    engine.StartCapture();
  } else {
    engine.DisableCapture();
  }
}

void VoicePushToTalkStop(
    SoundLuaContext& context) {
  SoundEngine(context).DisableCapture();
}

void VoiceChatStartCapture(
    SoundLuaContext& context) {

  const bool enable_microphone =
      openwow::game::ReadVoiceChatCVarBool("EnableMicrophone");
  const bool enable_voice_chat =
      openwow::game::ReadVoiceChatCVarBool("EnableVoiceChat");

  if (enable_voice_chat && enable_microphone && VoiceChat::Get().IsAllowedAndEnabled()) {
    auto& engine = SoundEngine(context);
    if (engine.IsVoiceChatEnabled()) {
      engine.StartCapture();
    }
  }

}

void VoiceChatStopCapture(
    SoundLuaContext& context) {
  if (!SoundEngine(context).IsInitialized()) {
    return;
  }
  (void)SoundRuntime(context).StopScriptMusicImmediately();
}

void ActivatePrimaryVoiceCaptureCallbackWhenGameSoundIsInitialized(
    SoundLuaContext& context) {
  if (!SoundEngine(context).IsInitialized()) {
    return;
  }
  VoiceLoopback(context).ActivatePrimaryCaptureCallback();
}

void VoiceChatStopRecordingLoopbackSound(
    SoundLuaContext& context) {
  ActivatePrimaryVoiceCaptureCallbackWhenGameSoundIsInitialized(context);
}

SoundVoidResult VoiceChatRecordLoopbackSound(
    SoundLuaContext& context,
    const SoundLuaNumber requested_seconds_argument) {
  if (!requested_seconds_argument.value) {
    return openwow::ui::lua::LuaUsageError{
        "Usage: VoiceChat_RecordLoopbackSound(MaxRecordTimeInSeconds)"};
  }

  const std::uint32_t max_seconds = openwow::ui::SaturateLuaNumberToU32(
      *requested_seconds_argument.value);
  openwow::audio::VoiceChat_RecordLoopbackSound(VoiceLoopback(context),
                                                 max_seconds);
  return openwow::ui::lua::NoLuaResults{};
}

void VoiceChatPlayLoopbackSound(
    SoundLuaContext& context) {
  (void)VoiceLoopback(context).Play();
}

double VoiceChatIsRecordingLoopbackSound(
    SoundLuaContext& context) {
  return VoiceLoopback(context).IsRecording() ? 1.0 : 0.0;
}

double VoiceChatIsPlayingLoopbackSound(
    SoundLuaContext& context) {
  return VoiceLoopback(context).IsPlaying() ? 1.0 : 0.0;
}

double VoiceChatGetCurrentMicrophoneSignalLevel(
    SoundLuaContext& context) {
  auto& engine = SoundEngine(context);

  return static_cast<double>(
      engine.IsInitialized() ? engine.GetMicrophoneSignalLevel() : 0);
}

void VoiceChatActivatePrimaryCaptureCallback(
    SoundLuaContext& context) {
  ActivatePrimaryVoiceCaptureCallbackWhenGameSoundIsInitialized(context);
}

}

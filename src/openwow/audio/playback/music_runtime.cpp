#include "openwow/audio/playback/sound_runtime_internal.h"

namespace openwow::audio {
namespace {

constexpr float kZoneAmbienceCrossfadeSeconds = 5.0f;
constexpr float kZoneAmbienceImmediateFadeSeconds = 0.0f;

constexpr float kZoneMusicCrossfadeSeconds = 4.0f;
constexpr float kZoneMusicColdStartFadeInSeconds = 0.01f;

constexpr float kUseStoredFadeOutSeconds = -1.0f;
}

void SoundRuntime::SetZoneAmbienceSelectionForPriority(const std::uint32_t priority,
                                                         const std::int32_t sound_ambience_id) {
  if (priority >= zone_ambience_selection_entries_.size()) {
    return;
  }

  auto &entry = zone_ambience_selection_entries_[priority];
  if (sound_ambience_id < 0) {
    entry.sound_kit_ids = {-1, -1};
    return;
  }

  const auto *const ambience =
      GetSoundAmbienceTableEntry(static_cast<std::uint32_t>(sound_ambience_id));
  if (ambience == nullptr) {
    entry.sound_kit_ids = {-1, -1};
    return;
  }

  entry.sound_kit_ids = ambience->sound_kit_ids;
}

void SoundRuntime::SetWeatherSoundKit(const std::int32_t sound_kit_id) {

  const std::int32_t selected_id = sound_kit_id > 0 ? sound_kit_id : -1;
  zone_ambience_selection_entries_[kWeatherAudioSelectionPriority]
      .sound_kit_ids = {selected_id, selected_id};
}

void SoundRuntime::SetZoneMusicSelectionForPriority(const std::uint32_t priority,
                                                      const std::int32_t zone_music_id) {
  if (priority >= zone_music_selection_entries_.size()) {
    return;
  }

  auto &entry = zone_music_selection_entries_[priority];
  const auto *const zone_music =
      zone_music_id < 0 ? nullptr
                        : GetZoneMusicTableEntry(static_cast<std::uint32_t>(zone_music_id));
  if (zone_music == nullptr) {
    entry = {};
    entry.sound_kit_ids = {-1, -1};
    if (zone_music_id > 0) {
      zone_music_delay_deadline_ms_ = 0;
    }
    return;
  }

  entry.sound_kit_ids = zone_music->sound_kit_ids;
  entry.repeat_delay_min_ms = zone_music->repeat_delay_min_ms;
  entry.repeat_delay_max_ms = zone_music->repeat_delay_max_ms;

  const std::int32_t selected_sound_kit_id = SelectZoneMusic(nullptr, nullptr, nullptr);
  if (selected_sound_kit_id != GetCurrentZoneMusicSoundKitId() &&
      (priority > 2 || selected_sound_kit_id != -1111)) {
    zone_music_delay_deadline_ms_ = 0;
  }
}

void SoundRuntime::SetZoneIntroMusicSelectionForPriority(const std::uint32_t priority,
                                                           const std::int32_t zone_intro_music_id) {
  if (priority >= zone_music_selection_entries_.size()) {
    return;
  }

  auto &entry = zone_music_selection_entries_[priority];
  const auto *const zone_intro =
      zone_intro_music_id < 0
          ? nullptr
          : GetZoneIntroMusicTableEntry(static_cast<std::uint32_t>(zone_intro_music_id));
  if (zone_intro == nullptr) {
    entry = {};
    entry.sound_kit_ids = {-1, -1};
    return;
  }

  entry.sound_kit_ids = {zone_intro->sound_kit_id, zone_intro->sound_kit_id};
  entry.repeat_delay_min_ms = {zone_intro->min_delay_ms, zone_intro->min_delay_ms};
}

void SoundRuntime::ApplyScreenEffectAudioSelections(
    const std::int32_t sound_ambience_id, const std::int32_t zone_music_id) {
  SetZoneAmbienceSelectionForPriority(kScreenEffectAudioSelectionPriority,
                                      sound_ambience_id);
  SetZoneMusicSelectionForPriority(kScreenEffectAudioSelectionPriority,
                                   zone_music_id);
}

std::uint32_t SoundRuntime::SelectZoneAmbienceSoundKit(int *priority_out) {
  if (cvar_get_bool_cb_) {
    if (!cvar_get_bool_cb_("Sound_EnableAllSound")) {
      if (priority_out)
        *priority_out = 14;
      return 0;
    }
    if (!cvar_get_bool_cb_("Sound_EnableAmbience")) {
      if (priority_out)
        *priority_out = 14;
      return 0;
    }
  }

  if (IsMovieAudioPlaying()) {
    if (priority_out)
      *priority_out = 14;
    return 0;
  }

  const std::size_t time_of_day_index = ResolveTimeOfDayIndex();
  for (int priority = static_cast<int>(zone_ambience_selection_entries_.size()) - 1; priority >= 0;
       --priority) {
    if (priority == 8 && skip_priority_8_sound_provider_selection_) {
      continue;
    }

    const auto sound_kit_id = zone_ambience_selection_entries_[static_cast<std::size_t>(priority)]
                                  .sound_kit_ids[time_of_day_index];
    if (sound_kit_id < 0) {
      continue;
    }

    if (priority_out)
      *priority_out = priority;
    return static_cast<std::uint32_t>(sound_kit_id);
  }

  return 0;
}

std::int32_t SoundRuntime::GetCurrentZoneMusicPriority() const {
  return zone_music_player_.ActiveSlot().zone_sound_type;
}

std::int32_t SoundRuntime::GetCurrentZoneMusicSoundKitId() const {
  if (!IsCurrentZoneMusicPlaying()) {
    return 0;
  }
  return static_cast<std::int32_t>(zone_music_player_.ActiveSlot().sound_kit_id);
}

bool SoundRuntime::CanSelectZoneMusicCandidate(const std::int32_t sound_kit_id,
                                                 const int priority) const {
  if (sound_kit_id == GetCurrentZoneMusicSoundKitId() || !IsSpecialZoneMusicPriority(priority)) {
    return true;
  }

  if (IsSpecialZoneMusicPriority(GetCurrentZoneMusicPriority())) {
    return false;
  }

  if (sound_kit_id < 0 ||
      static_cast<std::size_t>(sound_kit_id) >= zone_music_repeat_delay_deadlines_ms_.size()) {
    return false;
  }

  return openwow::core::GameClock::GetTickCount32() >=
         zone_music_repeat_delay_deadlines_ms_[static_cast<std::size_t>(sound_kit_id)];
}

int SoundRuntime::SelectZoneMusic(int *priority_out, std::uint32_t *repeat_delay_min_out,
                                    std::uint32_t *repeat_delay_max_out) {
  if (cvar_get_bool_cb_ &&
      (!cvar_get_bool_cb_("Sound_EnableAllSound") || !cvar_get_bool_cb_("Sound_EnableMusic"))) {
    if (priority_out)
      *priority_out = 14;
    if (repeat_delay_min_out)
      *repeat_delay_min_out = 0;
    if (repeat_delay_max_out)
      *repeat_delay_max_out = 0;
    return 0;
  }

  if (IsMovieAudioPlaying()) {
    if (priority_out)
      *priority_out = 14;
    if (repeat_delay_min_out)
      *repeat_delay_min_out = 0;
    if (repeat_delay_max_out)
      *repeat_delay_max_out = 0;
    return 0;
  }

  const std::size_t time_of_day_index = ResolveTimeOfDayIndex();
  for (int priority = static_cast<int>(zone_music_selection_entries_.size()) - 1; priority >= 0;
       --priority) {
    if (IsSpecialZoneMusicPriority(priority) && IsCurrentZoneMusicPlaying() &&
        IsSpecialZoneMusicPriority(GetCurrentZoneMusicPriority())) {
      if (priority_out)
        *priority_out = GetCurrentZoneMusicPriority();
      if (repeat_delay_min_out)
        *repeat_delay_min_out = 0;
      if (repeat_delay_max_out)
        *repeat_delay_max_out = 0;
      return GetCurrentZoneMusicSoundKitId();
    }

    const auto &entry = zone_music_selection_entries_[static_cast<std::size_t>(priority)];
    const std::int32_t sound_kit_id = entry.sound_kit_ids[time_of_day_index];
    if (sound_kit_id < 0) {
      continue;
    }

    const bool skip_delay =
        !UsesZoneMusicDelayGate(priority) ||
        (cvar_get_bool_cb_ && cvar_get_bool_cb_("Sound_ZoneMusicNoDelay")) ||
        openwow::core::GameClock::GetTickCount32() >= zone_music_delay_deadline_ms_;
    if (!skip_delay) {
      if (priority_out)
        *priority_out = 12;
      if (repeat_delay_min_out)
        *repeat_delay_min_out = 0;
      if (repeat_delay_max_out)
        *repeat_delay_max_out = 0;
      return -1111;
    }

    if (!CanSelectZoneMusicCandidate(sound_kit_id, priority)) {
      continue;
    }

    if (priority_out)
      *priority_out = priority;
    if (repeat_delay_min_out)
      *repeat_delay_min_out = entry.repeat_delay_min_ms[time_of_day_index];
    if (repeat_delay_max_out)
      *repeat_delay_max_out = entry.repeat_delay_max_ms[time_of_day_index];
    return sound_kit_id;
  }

  return 0;
}

int SoundRuntime::UpdateZoneMusic() {

  int ambience_priority = 13;
  const std::uint32_t ambience_kit = SelectZoneAmbienceSoundKit(&ambience_priority);

  int music_priority = 13;
  std::uint32_t repeat_delay_min_ms = 0;
  std::uint32_t repeat_delay_max_ms = 0;
  int music_kit = SelectZoneMusic(&music_priority, &repeat_delay_min_ms, &repeat_delay_max_ms);

  if (script_music_playing_ || IsCinematicSoundPlaying())
    return 1;

  if (!IsDualSlotActivePlaying(zone_ambience_player_) ||
      ambience_kit != GetDualSlotActiveSoundKitId(zone_ambience_player_)) {
    SoundKitPlaybackOptions ambience_options;
    ambience_options.sound_type = 2;

    const bool immediate = indoor_sound_area_changed_;

    ambience_options.fade_in_seconds =
        immediate ? kZoneAmbienceImmediateFadeSeconds : kZoneAmbienceCrossfadeSeconds;

    (void)UpdateTrackedSlotPlayback(zone_ambience_player_, ambience_kit, ambience_priority,
                                    ambience_options, immediate, kZoneAmbienceCrossfadeSeconds);
  }

  if (script_music_playing_ ||
      (IsDualSlotActivePlaying(zone_music_player_) &&
       GetDualSlotActiveSoundKitId(zone_music_player_) == static_cast<std::uint32_t>(music_kit)) ||
      music_kit == -1111 || IsCinematicSoundPlaying()) {
    return 1;
  }

  SoundKitPlaybackOptions music_options;
  music_options.sound_type = 1;
  music_options.loop_mode = SoundLoopMode::kForceOneShot;

  music_options.fade_in_seconds = IsDualSlotActivePlaying(zone_music_player_)
                                      ? kZoneMusicCrossfadeSeconds
                                      : kZoneMusicColdStartFadeInSeconds;
  music_options.fade_out_seconds = kZoneMusicCrossfadeSeconds;

  const int result = UpdateTrackedSlotPlayback(
      zone_music_player_, static_cast<std::uint32_t>(music_kit), music_priority, music_options,
      false, kUseStoredFadeOutSeconds);
  if (result != 0)
    return 1;

  if (UsesZoneMusicDelayGate(music_priority)) {
    ArmZoneMusicSelectionDelayStopAction(music_kit, repeat_delay_min_ms, repeat_delay_max_ms);
  } else if (music_priority == 3) {
    ArmZoneMusicRuntimeResetStopAction();
  } else if (IsSpecialZoneMusicPriority(music_priority)) {
    ArmZoneMusicRepeatDelayDeadlineStopAction(music_kit, repeat_delay_min_ms);
  } else {
    ClearZoneMusicStopAction();
  }

  return 1;
}

}

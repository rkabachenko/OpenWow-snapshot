#include "openwow/ui/game/world_state_ui_sync.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/world_map_system.h"

#include <cstdint>

namespace openwow::ui::game {
namespace {

openwow::game::WorldSession *GetActiveUISession() {
  auto *manager = runtime::WorldUiRuntimeContext::FromActiveLua();
  return manager != nullptr ? manager->world_session() : nullptr;
}

}

void NotifyWorldStatesChanged() {
  if (auto* const manager = runtime::WorldUiRuntimeContext::FromActiveLua(); manager != nullptr) {
    (void)manager->world_map().RefreshCurrentSelection();
  }
  ScriptEventDispatch::Get().FireEvent(events::UPDATE_WORLD_STATES);
}

void RefreshZoneSoundsForActiveMover() {
  auto *session = GetActiveUISession();
  if (session == nullptr) {
    return;
  }

  const auto *dbc = session->GetDbcLoader();
  if (dbc == nullptr) {
    return;
  }

  const std::uint32_t area_id = session->objects().GetAreaId();
  if (area_id == 0) {
    return;
  }

  const auto *area_entry = dbc->area_table().LookupEntry(area_id);
  if (area_entry == nullptr) {
    return;
  }

  const openwow::data::dbc::AreaTableEntry *parent_entry = area_entry;
  const openwow::data::dbc::AreaTableEntry *current_entry = nullptr;
  if (area_entry->parent_area != 0) {
    parent_entry = dbc->area_table().LookupEntry(area_entry->parent_area);
    if (parent_entry == nullptr) {
      parent_entry = area_entry;
    } else {
      current_entry = area_entry;
    }
  }

  const auto cascade = [](const std::uint32_t current_value,
                          const std::uint32_t parent_value) -> std::int32_t {
    if (current_value != 0) {
      return static_cast<std::int32_t>(current_value);
    }
    return parent_value != 0 ? static_cast<std::int32_t>(parent_value) : -1;
  };

  const auto current_value = [current_entry](const auto member) {
    return current_entry != nullptr ? current_entry->*member : 0u;
  };

  const std::int32_t sound_provider_pref =
      cascade(current_value(&openwow::data::dbc::AreaTableEntry::sound_provider_pref),
              parent_entry->sound_provider_pref);
  const std::int32_t sound_provider_uw =
      cascade(current_value(&openwow::data::dbc::AreaTableEntry::sound_provider_pref_uw),
              parent_entry->sound_provider_pref_uw);
  const std::int32_t ambience =
      cascade(current_value(&openwow::data::dbc::AreaTableEntry::sound_ambience_id),
              parent_entry->sound_ambience_id);
  const std::int32_t zone_music =
      cascade(current_value(&openwow::data::dbc::AreaTableEntry::zone_music_id),
              parent_entry->zone_music_id);
  const std::int32_t intro_music =
      cascade(current_value(&openwow::data::dbc::AreaTableEntry::zone_intro_music_id),
              parent_entry->zone_intro_music_id);

  const std::uint32_t current_area_id = current_entry != nullptr ? current_entry->id : 0;
  const auto &world_states = session->world_states();
  auto &sound = session->sound_runtime();

  openwow::audio::WorldAudioBindingValue world_state_binding{};
  (void)sound.ResolveWorldStateZoneSoundBinding(
      parent_entry->id, current_area_id, 0, 0, false,
      [&world_states](const std::uint32_t variable_id) {
        return static_cast<std::uint32_t>(
            world_states.GetWorldState(static_cast<std::int32_t>(variable_id)));
      },
      &world_state_binding);

  sound.SetSoundProviderPreferenceForPriority(
      1, world_state_binding.sound_provider_preferences_id);
  sound.SetZoneAmbienceSelectionForPriority(1, world_state_binding.sound_ambience_id);
  sound.SetZoneIntroMusicSelectionForPriority(6, world_state_binding.zone_intro_music_id);
  sound.SetZoneMusicSelectionForPriority(1, world_state_binding.zone_music_id);

  sound.SetSoundProviderPreferenceForPriority(0, sound_provider_pref);
  sound.SetSoundProviderPreferenceForPriority(9, sound_provider_uw);
  sound.SetZoneAmbienceSelectionForPriority(0, ambience);
  sound.SetZoneIntroMusicSelectionForPriority(5, intro_music);
  sound.SetZoneMusicSelectionForPriority(0, zone_music);
}

}

#include "openwow/audio/playback/sound_runtime_internal.h"
#include "openwow/game/char_create_helpers.h"

#include <cstdio>

namespace openwow::audio {

std::uint32_t EmotesTextSoundTable::Lookup(std::uint32_t emote_text_id, std::uint32_t race_id,
                                           std::uint32_t gender_id) const {
  if (emote_text_id >= max_emote_text_id_)
    return 0;
  if (race_id >= kMaxRaces)
    return 0;
  if (gender_id >= kMaxGenders)
    return 0;

  auto it = table_.find(emote_text_id);
  if (it == table_.end())
    return 0;

  return it->second[gender_id + 2 * race_id];
}

std::int32_t EmotesTextSoundTable::LookupAndSelectVariant(SoundRuntime& sound_runtime,
                                                           std::uint32_t emote_text_id,
                                                          std::uint32_t race_id,
                                                          std::uint32_t gender_id) const {
  std::uint32_t sound_kit_id = Lookup(emote_text_id, race_id, gender_id);
  if (sound_kit_id == 0)
    return -1;

  const auto *data = sound_runtime.GetSoundKitData(sound_kit_id);
  if (!data || data->file_count == 0)
    return -1;

  const auto selected_file = sound_runtime.SelectSoundKitFileForPlayback(
      sound_kit_id, SoundKitVariationSelectionMode::kConsumeFrequenciesAcrossCalls, std::nullopt,
      5);
  if (!selected_file.has_value()) {
    return -1;
  }

  return selected_file->index;
}

LiquidQueryResultBuffer::LiquidQueryResultBuffer(const std::size_t capacity) : entries_(capacity) {
  Reset();
}

void LiquidQueryResultBuffer::Reset() {
  for (auto &entry : entries_) {
    entry = LiquidQueryResultEntry{};
  }
}

void LiquidQueryResultBuffer::ShiftDownFrom(const std::size_t insert_index) {
  if (insert_index >= entries_.size()) {
    return;
  }

  for (std::size_t index = entries_.size(); index-- > insert_index + 1;) {
    entries_[index] = entries_[index - 1];
  }
}

bool LiquidQueryResultBuffer::InsertSorted(const LiquidQueryResultEntry &entry) {
  if (entries_.empty()) {
    return false;
  }

  std::size_t insert_index = 0;
  while (insert_index < entries_.size() && !entries_[insert_index].IsEmpty() &&
         entries_[insert_index].distance_squared <= entry.distance_squared) {
    ++insert_index;
  }

  if (insert_index >= entries_.size()) {
    return false;
  }

  ShiftDownFrom(insert_index);
  entries_[insert_index] = entry;
  return true;
}

bool LiquidQueryResultBuffer::InsertSorted(const float distance_squared, const float x,
                                           const float y, const float z,
                                           const std::uint32_t sound_kit_id) {
  return InsertSorted(LiquidQueryResultEntry{distance_squared, {x, y, z}, sound_kit_id});
}

void WoundDeathSoundTable::Load(
    const std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> &rows) {
  entries.fill(0);
  for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
    const auto &[sound_class, critical_variant, sound_kit_id] = *it;
    if (sound_class < static_cast<std::uint32_t>(kSoundClasses) &&
        critical_variant < static_cast<std::uint32_t>(kVariantsPerClass)) {
      entries[2 * sound_class + critical_variant] = sound_kit_id;
    }
  }
}

std::uint32_t WoundDeathSoundTable::Get(const std::uint32_t sound_class,
                                        const bool critical_variant) const {
  if (sound_class >= static_cast<std::uint32_t>(kSoundClasses)) {
    return 0;
  }
  return entries[2 * sound_class + (critical_variant ? 1u : 0u)];
}

std::uint32_t GetUnitGender(SoundRuntime& sound_runtime, std::uint64_t guid) {
  const auto *unit = sound_runtime.GetUnitForSound(guid);
  if (unit == nullptr) {
    return 0;
  }
  return unit->Presentation().DisplayGender();
}

namespace {

bool IsDeadAnimationFamily(const std::uint16_t animation_id) {

  switch (animation_id) {
    case render::AnimId::kDeath:
    case render::AnimId::kDead:
    case 131:
    case 132:
    case 466:
    case 467:
    case 468:
    case 472:
      return true;
    default:
      return false;
  }
}

bool CannotPlayAmbientIdleSoundForAnimationState(
    const openwow::game::CGUnit_C &unit) {
  const auto current_id = unit.Animation().GetCurrentAnimationId();
  if (current_id.has_value() && IsDeadAnimationFamily(*current_id)) {
    return true;
  }
  return false;
}

int AdvanceAmbientIdleSoundSelection(
    SoundRuntime& sound_runtime,
    const data::dbc::NPCSoundsEntry &npc_sounds, const int counter,
    std::uint32_t &out_sound_kit, std::int32_t &out_variation) {
  const auto primary_kit = npc_sounds.sound[0];
  if (primary_kit == 0) {
    return -1;
  }

  if (counter < 5) {
    out_sound_kit = primary_kit;
    out_variation = -1;
    return counter + 1;
  }

  const auto variant_kit = npc_sounds.sound[2];
  const auto variation_index = counter - 5;
  const auto variation_count = static_cast<int>(
      sound_runtime.GetSoundKitVariationCount(variant_kit));

  if (variation_index < variation_count) {
    out_sound_kit = variant_kit;
    out_variation = variation_index;
    return counter + 1;
  }

  out_sound_kit = primary_kit;
  out_variation = -1;
  return 0;
}

}

void SoundRuntime::PlayAmbientIdleSound(std::uint64_t unit_guid) {
  auto& sound = *this;
  const auto *dbc = detail::GetDbcLoaderForAudio();
  if (dbc == nullptr) {
    return;
  }

  const auto *unit = sound.GetUnitForSound(unit_guid);
  if (unit == nullptr) {
    return;
  }

  if (ambient_idle_handle_valid_ &&
      IsSoundHandlePlaying(ambient_idle_handle_id_)) {
    return;
  }
  ambient_idle_handle_valid_ = false;

  if (CannotPlayAmbientIdleSoundForAnimationState(*unit)) {
    return;
  }

  const auto *player = sound.GetActivePlayerForSound();
  if (player == nullptr || player->State().IsDead()) {
    return;
  }

  if (unit->Interaction().IsHostileTo(*player) ||
      player->Interaction().IsHostileTo(*unit)) {
    return;
  }

  if (unit_guid != ambient_idle_last_guid_) {
    ambient_idle_selection_counter_ = 0;
  }
  ambient_idle_last_guid_ = unit_guid;

  const auto *display =
      dbc->creature_display_info().LookupEntry(unit->Presentation().CurrentDisplayId());
  if (display == nullptr || display->sound_id == 0) {
    return;
  }
  const auto *npc_sounds = dbc->npc_sounds().LookupEntry(display->sound_id);
  if (npc_sounds == nullptr) {
    return;
  }

  std::uint32_t sound_kit_id = 0;
  std::int32_t variation_index = -1;
  const int new_counter = AdvanceAmbientIdleSoundSelection(
      *this,
      *npc_sounds, ambient_idle_selection_counter_, sound_kit_id,
      variation_index);
  if (new_counter == -1) {
    return;
  }

  const auto position = unit->GetPosition();
  const float sound_position[3] = {position.x, position.y, position.z};

  SoundKitPlaybackOptions options{};
  if (variation_index >= 0) {
    options.forced_file_index = variation_index;
  }

  std::uint32_t handle_id = 0;
  PlaySoundKit(sound_kit_id, sound_position, &handle_id, options);

  if (IsSoundHandlePlaying(handle_id)) {
    ambient_idle_selection_counter_ = new_counter;
    ambient_idle_handle_id_ = handle_id;
    ambient_idle_handle_valid_ = true;
  }
}

void PlayEmoteSound(SoundRuntime& sound_runtime, const std::uint64_t unit_guid,
                    const std::uint32_t sound_kit_id, const bool is_npc,
                    const float explicit_volume) {
  if (sound_kit_id == 0) {
    return;
  }

  const auto *unit = sound_runtime.GetUnitForSound(unit_guid);
  if (unit == nullptr) {
    return;
  }

  if (unit->IsPlayer() &&
      static_cast<std::int32_t>(unit->State().GetHealth()) < 1) {
    return;
  }

  auto &cvars = openwow::ui::game::CVarSystem::Instance();

  if (is_npc) {
    if (!cvars.GetCVarBool("Sound_EnableEmoteSounds")) {
      return;
    }
    if (!cvars.GetCVarBool("Sound_EnablePetSounds")) {
      if (unit->Casts().GetCreatedBySpell(*unit) != 0) {
        return;
      }
    }
  }

  const bool listener_at_character = cvars.GetCVarBool("Sound_ListenerAtCharacter");

  const bool is_active_mover = unit->IsActivePlayer();
  const bool play_2d = is_active_mover && listener_at_character;

  const auto position = unit->GetPosition();

  const float sound_position[3] = {
      position.x, position.y, position.z + 2.0f};

  SoundKitPlaybackOptions options{};
  options.sound_type = 2;
  options.explicit_volume = explicit_volume;

  if (is_active_mover) {
    options.playback_priority = 110;
    if (play_2d) {
      options.volume_scale = 0.65f;
    }
  }

  if (sound_kit_id != 0x19b0u && unit->State().GetClass() == 6u) {
    options.sound_model_override =
        openwow::game::CharCreate_GetDeathKnightModelName(
            unit->State().GetRace(), unit->State().GetGender());
  }

  std::uint32_t handle_id = 0u;
  if (sound_runtime.PlaySoundKit(sound_kit_id,
                                 play_2d ? nullptr : sound_position,
                                 play_2d ? nullptr : &handle_id,
                                 options) == 0 &&
      !play_2d && handle_id != 0u) {
    (void)sound_runtime.BindSoundHandleToObjectGuid(handle_id, unit_guid);
  }
}

void PlayEmoteSoundForUnit(SoundRuntime& sound_runtime, std::uint64_t unit_guid, std::uint32_t emote_text_id,
                           float explicit_volume) {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.GetCVarBool("Sound_EnableEmoteSounds")) {
    return;
  }

  const auto *unit = sound_runtime.GetUnitForSound(unit_guid);
  if (unit == nullptr) {
    return;
  }

  if (!unit->IsActivePlayer()) {
    if (!game::GroupSystem::Get().IsActivePlayerOrPartyMemberGuid(unit_guid)) {
      return;
    }
  }

  const auto gender = static_cast<std::uint32_t>(unit->Presentation().DisplayGender());
  const auto race = static_cast<std::uint32_t>(unit->Presentation().DisplayRace());

  const auto sound_kit_id =
      sound_runtime.GetEmotesTextSoundTable().Lookup(
          emote_text_id, race, gender);
  if (sound_kit_id == 0) {
    return;
  }

  if (unit->Interaction().CurrentShapeshiftFormRequiresTurnSensitiveUse()) {
    const auto form_id = unit->Animation().GetShapeshiftForm();
    if (form_id != 0) {
      const auto *dbc = detail::GetDbcLoaderForAudio();
      if (dbc != nullptr) {
        const auto *form_entry =
            dbc->spell_shapeshift_form().LookupEntry(form_id);
        if (form_entry != nullptr &&
            (form_entry->flags &
             data::dbc::kShapeshiftFormFlagSuppressEmoteSound) != 0) {
          return;
        }
      }
    }
  }

  PlayEmoteSound(sound_runtime, unit_guid, sound_kit_id, false,
                 explicit_volume);
}

void PlayEmoteAmbientSound(SoundRuntime& sound_runtime, std::uint64_t unit_guid) {
  const auto *unit = sound_runtime.GetUnitForSound(unit_guid);
  if (unit == nullptr) {
    return;
  }

  const auto emote_state = unit->Animation().GetEmoteState();
  if (emote_state == 0) {
    return;
  }

  const auto *dbc = detail::GetDbcLoaderForAudio();
  if (dbc == nullptr) {
    return;
  }

  const auto *emote_entry = dbc->emotes().LookupEntry(emote_state);
  if (emote_entry == nullptr) {
    return;
  }

  if (emote_entry->event_sound_id == 0) {
    return;
  }

  PlayEmoteSound(sound_runtime, unit_guid, emote_entry->event_sound_id, true, 1.0f);
}

void PlayMaterialFoleySound(SoundRuntime& sound_runtime, std::uint32_t material_id, const float* position, bool is_self) {
  const auto* dbc = detail::GetDbcLoaderForAudio();
  if (!dbc) return;

  const auto* entry = dbc->material().LookupEntry(material_id);
  if (!entry || entry->foley_sound == 0) return;

  const float adjusted_pos[3] = {
    position[0],
    position[1],
    position[2] + 2.0f,
  };

  auto& si = sound_runtime;

  if (is_self) {
    auto& cvars = openwow::ui::game::CVarSystem::Instance();
    const bool listener_at_character = cvars.GetCVarBool("Sound_ListenerAtCharacter");

    SoundKitPlaybackOptions options;
    if (listener_at_character) {
      options.volume_scale = 0.65f;
    }
    options.playback_priority = kSelfUnitSoundPlaybackPriority;

    si.PlaySoundKit(entry->foley_sound,
                    listener_at_character ? nullptr : adjusted_pos,
                    nullptr, options);
  } else {
    si.PlaySoundKit(entry->foley_sound, adjusted_pos);
  }
}

void PlayMaterialSheatheSound(SoundRuntime& sound_runtime, std::uint32_t material_id, bool sheathe,
                              const float* position, bool is_self) {
  const auto* dbc = detail::GetDbcLoaderForAudio();
  if (!dbc) return;

  const auto* entry = dbc->material().LookupEntry(material_id);
  if (!entry) return;

  const std::uint32_t sound_kit_id = sheathe ? entry->sheathe_sound
                                             : entry->unsheathe_sound;

  const float adjusted_pos[3] = {
    position[0],
    position[1],
    position[2] + 2.0f,
  };

  SoundKitPlaybackOptions options;
  if (is_self) {
    options.playback_priority = kSelfUnitSoundPlaybackPriority;
  }

  sound_runtime.PlaySoundKit(sound_kit_id, adjusted_pos,
                                          nullptr, options);
}

void PlayArmorFoleySound(SoundRuntime& sound_runtime, std::uint64_t unit_guid, std::uint64_t active_player_guid) {
  constexpr std::uint8_t kVisibleChestSlot = 4u;
  const bool is_self = (unit_guid == active_player_guid);

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  if (is_self) {
    if (!cvars.GetCVarBool("Sound_EnableArmorFoleySoundForSelf")) {
      return;
    }
  } else {
    if (!cvars.GetCVarBool("Sound_EnableArmorFoleySoundForOthers")) {
      return;
    }
  }

  const auto* player = sound_runtime.GetPlayerForSound(unit_guid);
  if (player == nullptr) {
    return;
  }

  const auto material =
      player->GetVisibleItemTemplateMetadata(kVisibleChestSlot);
  if (!material.has_value()) {
    return;
  }

  const auto position = player->GetPosition();
  const float sound_position[3] = {position.x, position.y, position.z};
  PlayMaterialFoleySound(sound_runtime, static_cast<std::uint32_t>(material->material),
                         sound_position, is_self);
}

void SoundInterface_RegisterCVars(SoundRuntime& sound_runtime) {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  RegisterSoundInterfaceCVarDefaults(cvars);
  RegisterSoundPlaybackCVarDefaults(cvars);
  sound_runtime.BindRegisteredCvars(cvars);
}

namespace {

void SetDriverIndexCVarOnce(const char* cvar_name, bool& init_flag,
                           bool& cvar_found, int driver_index) {
  auto& cvars = openwow::ui::game::CVarSystem::Instance();

  if (!init_flag) {
    init_flag = true;
    cvar_found = cvars.Exists(cvar_name);
  }

  if (!cvar_found) {
    return;
  }

  char buf[32];
  std::snprintf(buf, sizeof(buf), "%d", driver_index);
  cvars.SetCVar(cvar_name, buf);
}

}

void SetVoiceChatOutputDriverIndexCVar(int driver_index) {
  static bool s_initialized = false;
  static bool s_cvar_found = false;
  SetDriverIndexCVarOnce("Sound_VoiceChatOutputDriverIndex", s_initialized,
                         s_cvar_found, driver_index);
}

void SetVoiceChatInputDriverIndexCVar(int driver_index) {
  static bool s_initialized = false;
  static bool s_cvar_found = false;
  SetDriverIndexCVarOnce("Sound_VoiceChatInputDriverIndex", s_initialized,
                         s_cvar_found, driver_index);
}

}

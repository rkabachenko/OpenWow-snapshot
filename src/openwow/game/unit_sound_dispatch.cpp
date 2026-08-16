
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/player_control_runtime.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/audio/effects/unit_sounds.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/character_animation.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/vehicle.h"
#include "openwow/ui/game/cvar_system.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {

namespace {

constexpr std::size_t kVehicleDescriptorDirtyFlagsOffset = 8u;
constexpr std::size_t kVehicleDescriptorDirtyMaskOffset = 22u * sizeof(std::uint32_t);
constexpr std::size_t kVehicleDescriptorSeatSlotsOffset = 24u * sizeof(std::uint32_t);
constexpr std::size_t kVehicleDescriptorSeatSlotStride = 4u * sizeof(std::uint32_t);
constexpr std::size_t kVehicleDescriptorSeatSlotCount = 16u;
constexpr std::size_t kVehicleDescriptorTransferStateOffset = 88u * sizeof(std::uint32_t);
constexpr std::size_t kVehicleDescriptorSeatIndexLimit = 0x23u;
constexpr std::uint32_t kVehicleDescriptorAnySeatIndex = 26u;
constexpr std::uint32_t kVehicleDescriptorDirtyFlag = 0x1u;
constexpr std::uint32_t kVehicleSeatFlagTurnWhileMoveAndSteer = 0x00200000u;
constexpr std::uint32_t kFourCC_FD1 = 0x31444624u;
constexpr std::uint32_t kFourCC_FD2 = 0x32444624u;
constexpr std::uint32_t kFourCC_FD3 = 0x33444624u;
constexpr std::uint32_t kFourCC_FD4 = 0x34444624u;
constexpr std::uint32_t kFourCC_FD5 = 0x35444624u;
constexpr std::uint32_t kFourCC_FD6 = 0x36444624u;
constexpr std::uint32_t kFourCC_FD9 = 0x39444624u;
constexpr std::uint32_t kFourCC_FDX = 0x58444624u;
constexpr std::uint32_t kFourCC_CSD = 0x44534324u;
constexpr std::uint32_t kFourCC_ESD = 0x44534524u;
constexpr std::uint32_t kFourCC_FSD = 0x44534624u;
constexpr std::uint32_t kFourCC_SCD = 0x44435324u;
constexpr std::uint32_t kFourCC_SMD = 0x444D5324u;
constexpr std::uint32_t kFourCC_SMG = 0x474D5324u;
constexpr std::uint32_t kFourCC_BRT = 0x54524224u;
constexpr std::uint32_t kFourCC_WGG = 0x47475724u;
constexpr std::uint32_t kFourCC_WNG = 0x474E5724u;
constexpr std::uint32_t kLocalStandSoundPlaybackPriority = 110u;
constexpr float kLocalStandSoundVolumeScale = 0.65f;
constexpr std::uint32_t kDeathThudSizeClassCount = 5u;
constexpr std::string_view kFootstepSoundsCVarName = "FootstepSounds";
constexpr std::string_view kSpiritWolfPushToTalkSoundKitName =
    "SpiritWolf (DONOTRENAME)";

VehicleDescriptorRenderReadyCallback g_vehicle_descriptor_render_ready_callback =
    nullptr;
void* g_vehicle_descriptor_render_ready_context = nullptr;
UnitSoundGroundStateCallback g_unit_sound_ground_state_callback = nullptr;
void* g_unit_sound_ground_state_context = nullptr;
std::uint32_t g_spirit_wolf_push_to_talk_sound_kit_id = 0u;

struct DeathThudSoundKitPair {
  std::uint32_t land_sound_kit_id = 0u;
  std::uint32_t water_sound_kit_id = 0u;
};

struct DeathThudSoundRuntimeState {
  void Clear() {
    for (auto& entries : sound_kits_by_size_class) {
      entries.clear();
    }
  }

  void Rebuild(const openwow::data::dbc::DbcLoader* const dbc) {
    Clear();
    if (dbc == nullptr || dbc->terrain_type_sounds().empty()) {
      return;
    }

    const auto terrain_type_sound_slot_count =
        static_cast<std::size_t>(dbc->terrain_type_sounds().max_id()) + 1u;
    for (auto& entries : sound_kits_by_size_class) {
      entries.assign(terrain_type_sound_slot_count, {});
    }

    const auto& rows = dbc->death_thud_lookups().entries();
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
      if (it->size_class >= kDeathThudSizeClassCount) {
        continue;
      }

      const auto terrain_type_sound_id =
          static_cast<std::size_t>(it->terrain_type_sound);
      auto& entries = sound_kits_by_size_class[it->size_class];
      if (terrain_type_sound_id >= entries.size()) {
        continue;
      }

      entries[terrain_type_sound_id] = {it->sound_entry, it->sound_entry_water};
    }
  }

  [[nodiscard]] std::uint32_t GetSoundKit(const std::uint32_t size_class,
                                          const std::uint32_t terrain_type_sound_id,
                                          const bool use_water_variant) const {
    if (size_class >= kDeathThudSizeClassCount) {
      return 0u;
    }

    const auto& entries = sound_kits_by_size_class[size_class];
    const auto slot = static_cast<std::size_t>(terrain_type_sound_id);
    if (slot >= entries.size()) {
      return 0u;
    }

    const auto& entry = entries[slot];
    return use_water_variant ? entry.water_sound_kit_id : entry.land_sound_kit_id;
  }

  std::array<std::vector<DeathThudSoundKitPair>, kDeathThudSizeClassCount>
      sound_kits_by_size_class;
};

DeathThudSoundRuntimeState g_death_thud_sound_runtime_state;

void RebuildFootstepSoundLookup(
    openwow::audio::SoundRuntime& sound,
    const openwow::data::dbc::DbcLoader* const dbc) {
  if (dbc == nullptr || dbc->terrain_type().empty()) {
    sound.ConfigureUnitSoundDisplayInfoRange(1u, 0u);
    sound.RebuildUnitSoundKitLookup({}, 0u);
    return;
  }

  sound.ConfigureUnitSoundDisplayInfoRange(0u, dbc->terrain_type().max_id());
  for (const auto& terrain : dbc->terrain_type().entries()) {
    (void)sound.SetUnitSoundDisplayInfo(terrain.id, terrain.sound_id);
  }

  std::vector<openwow::audio::UnitSoundLookupRow> rows;
  rows.reserve(dbc->footstep_terrain_lookup().entries().size());
  for (const auto& row : dbc->footstep_terrain_lookup().entries()) {
    rows.push_back({row.creature_footstep_id,
                    static_cast<std::int32_t>(row.terrain_sound_id),
                    row.sound_id, row.sound_id_splash});
  }
  sound.RebuildUnitSoundKitLookup(rows, dbc->terrain_type_sounds().max_id());
}

[[nodiscard]] UnitSoundGroundState ResolveUnitSoundGroundState(
    const CGUnit_C& unit, const float* const event_position) {
  if (g_unit_sound_ground_state_callback == nullptr) {
    return {};
  }
  return g_unit_sound_ground_state_callback(
      unit, event_position, g_unit_sound_ground_state_context);
}

template <typename T>
[[nodiscard]] T LoadVehicleDescriptorField(const void* base, const std::size_t offset) {
  T value{};
  if (base == nullptr) {
    return value;
  }

  std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(T));
  return value;
}

template <typename T>
void StoreVehicleDescriptorField(void* base, const std::size_t offset, const T& value) {
  if (base == nullptr) {
    return;
  }

  std::memcpy(static_cast<std::byte*>(base) + offset, &value, sizeof(T));
}

[[nodiscard]] std::uint32_t ClampVehicleDescriptorSeatIndex(
    const std::uint32_t seat_index) {
  return seat_index >= kVehicleDescriptorSeatIndexLimit
             ? kVehicleDescriptorAnySeatIndex
             : seat_index;
}

[[nodiscard]] bool IsVehicleDescriptorRenderReady(const CGUnit_C& unit) {
  if (g_vehicle_descriptor_render_ready_callback == nullptr) {
    return false;
  }

  return g_vehicle_descriptor_render_ready_callback(
      unit, g_vehicle_descriptor_render_ready_context);
}

void ClearVehicleDescriptorDirtyState(void* descriptor) {
  const std::uint32_t zero = 0;
  StoreVehicleDescriptorField(descriptor, kVehicleDescriptorDirtyMaskOffset, zero);
  StoreVehicleDescriptorField(descriptor,
                              kVehicleDescriptorDirtyMaskOffset + sizeof(std::uint32_t),
                              zero);

  for (std::size_t slot_index = 0; slot_index < kVehicleDescriptorSeatSlotCount;
       ++slot_index) {
    const auto slot_offset =
        kVehicleDescriptorSeatSlotsOffset + slot_index * kVehicleDescriptorSeatSlotStride;
    StoreVehicleDescriptorField(descriptor, slot_offset, std::uint32_t{0});
    StoreVehicleDescriptorField(descriptor, slot_offset + sizeof(std::uint32_t),
                                std::uint32_t{0});
  }

  auto dirty_flags = LoadVehicleDescriptorField<std::uint32_t>(
      descriptor, kVehicleDescriptorDirtyFlagsOffset);
  dirty_flags &= ~kVehicleDescriptorDirtyFlag;
  StoreVehicleDescriptorField(descriptor, kVehicleDescriptorDirtyFlagsOffset,
                              dirty_flags);

  StoreVehicleDescriptorField(descriptor, kVehicleDescriptorTransferStateOffset, zero);
  StoreVehicleDescriptorField(descriptor,
                              kVehicleDescriptorTransferStateOffset +
                                  sizeof(std::uint32_t),
                              zero);
}

void ProcessVehicleDescriptorSeatAnimation(WorldSession& session,
                                            CGUnit_C& unit,
                                            void* vehicleData,
                                            const std::int32_t m2_instance_id,
                                            const std::uint32_t seat_index) {
  vehicle::Vehicle_ProcessDirtySeatAnimation(session, unit, vehicleData,
                                                  m2_instance_id, seat_index);
}

[[nodiscard]] const openwow::data::dbc::CreatureSoundDataEntry*
ResolveActiveCreatureSoundDataEntry(const CGUnit_C& unit) {
  return unit.Sound().ResolveActive(unit);
}

void EnsureFootstepSoundsCVarRegistered() {
  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.Exists(std::string(kFootstepSoundsCVarName))) {
    cvars.RegisterCVar(std::string(kFootstepSoundsCVarName), "1");
  }
}

[[nodiscard]] std::uint32_t ResolveSpiritWolfPushToTalkSoundKitId(
    openwow::audio::SoundRuntime& sound,
    const openwow::data::dbc::DbcLoader& dbc) {
  const auto loaded_sound_kit_id =
      sound.LookupSoundKitIdByName(kSpiritWolfPushToTalkSoundKitName);
  if (loaded_sound_kit_id != 0u) {
    return loaded_sound_kit_id;
  }

  const auto* const entry =
      dbc.sound_entries().LookupByNameCaseInsensitive(kSpiritWolfPushToTalkSoundKitName);
  return entry != nullptr ? entry->id : 0u;
}

[[nodiscard]] bool IsCreatureFidgetSoundFourCC(const std::uint32_t fourcc) {
  return fourcc >= kFourCC_FD1 && fourcc <= kFourCC_FD5;
}

[[nodiscard]] bool IsCreatureFidgetResolveOnlyFourCC(
    const std::uint32_t fourcc) {
  return fourcc >= kFourCC_FD6 && fourcc <= kFourCC_FD9;
}

[[nodiscard]] bool IsUnitSoundAnimationRoute(
    const UnitAnimationEventRoute route) {
  switch (route) {
    case UnitAnimationEventRoute::kCreatureFidget:
    case UnitAnimationEventRoute::kFootstepSound:
    case UnitAnimationEventRoute::kCustomSound:
    case UnitAnimationEventRoute::kEmoteSound:
    case UnitAnimationEventRoute::kDirectCreatureSound:
    case UnitAnimationEventRoute::kCreatureVocal:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] std::uint32_t ResolveDirectCreatureSoundKitId(
    const openwow::data::dbc::CreatureSoundDataEntry& sound_data,
    const std::uint32_t fourcc) {
  switch (fourcc) {
    case kFourCC_BRT:
      return sound_data.birth_sound_id;
    case kFourCC_SCD:
      return sound_data.spell_cast_directed_sound_id;
    case kFourCC_SMG:
      return sound_data.submerge_sound_id;
    case kFourCC_SMD:
      return sound_data.submerged_sound_id;
    default:
      return 0u;
  }
}

int PlayDirectAnimationSound(openwow::audio::SoundRuntime& sound_runtime,
                              const std::uint32_t sound_kit_id,
                             const float* const position) {
  if (sound_kit_id == 0u) {
    return 0;
  }

  return sound_runtime.PlaySoundKit(
      sound_kit_id, position);
}

int PlayEmoteStateAnimationSound(const CGUnit_C& unit,
                                 const float* const position) {
  const auto* const dbc = unit.dbc_loader();
  if (dbc == nullptr) {
    return 0;
  }

  const auto* const emote = dbc->emotes().LookupEntry(unit.Animation().GetEmoteState());
  if (emote == nullptr || emote->spec != 2u) {
    return 0;
  }

  return PlayDirectAnimationSound(unit.sound_runtime(), emote->event_sound_id, position);
}

int PlayCustomAnimationSound(CGUnit_C& unit, const std::int32_t sound_kit_id) {
  if (sound_kit_id <= 0 || (unit.IsPlayer() && unit.State().GetLevel() == 0u)) {
    return 0;
  }

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.GetCVarBool("Sound_EnableEmoteSounds")) {
    return 0;
  }
  if (!cvars.GetCVarBool("Sound_EnablePetSounds") &&
      !unit.State().GetSummonedBy().IsEmpty()) {
    return 0;
  }

  const Position position = unit.GetPosition();
  const float sound_position[3] = {position.x, position.y, position.z};
  auto& sound = unit.sound_runtime();

  if (unit.IsActivePlayer() && sound.IsListenerAtCharacter()) {
    openwow::audio::SoundKitPlaybackOptions options;
    options.playback_priority = kLocalStandSoundPlaybackPriority;
    options.volume_scale = kLocalStandSoundVolumeScale;
    return sound.PlaySoundKit(static_cast<std::uint32_t>(sound_kit_id),
                              nullptr, nullptr, options);
  }

  return sound.PlaySoundKit(static_cast<std::uint32_t>(sound_kit_id),
                            sound_position);
}

[[nodiscard]] std::uint32_t ResolveCreatureFidgetSoundKitId(
    const openwow::data::dbc::CreatureSoundDataEntry& sound_data,
    const std::uint32_t fourcc) {
  switch (fourcc) {
    case kFourCC_FD1:
      return sound_data.sound_fidget0;
    case kFourCC_FD2:
      return sound_data.sound_fidget1;
    case kFourCC_FD3:
      return sound_data.sound_fidget2;
    case kFourCC_FD4:
      return sound_data.sound_fidget3;
    case kFourCC_FD5:
      return sound_data.sound_fidget4;
    default:
      return 0u;
  }
}

int PlayCreatureFidgetSound(const CGUnit_C& unit, const std::uint32_t sound_kit_id) {
  if (sound_kit_id == 0u) {
    return 0;
  }

  const Position position = unit.GetPosition();
  float sound_position[3] = {position.x, position.y, position.z + 2.0f};
  return unit.sound_runtime().PlaySoundKit(sound_kit_id,
                                                                 sound_position);
}

}

bool UnitSound_IsHandledAnimFourCC(const std::uint32_t fourcc) {
  return IsUnitSoundAnimationRoute(
      ClassifyUnitAnimationEvent(fourcc).route);
}

int UnitSound_DispatchAnimFourCC(void* unit, const std::uint32_t fourcc,
                                 const int param, const float* const position) {
  auto* const creature = static_cast<CGUnit_C*>(unit);
  if (creature == nullptr || !UnitSound_IsHandledAnimFourCC(fourcc)) {
    return 0;
  }

  if (IsCreatureFidgetSoundFourCC(fourcc)) {
    const auto* const sound_data = ResolveActiveCreatureSoundDataEntry(*creature);
    if (sound_data == nullptr) {
      return 0;
    }

    return PlayCreatureFidgetSound(
        *creature, ResolveCreatureFidgetSoundKitId(*sound_data, fourcc));
  }

  if (IsCreatureFidgetResolveOnlyFourCC(fourcc)) {
    (void)ResolveActiveCreatureSoundDataEntry(*creature);
    return 0;
  }

  if (fourcc == kFourCC_FDX) {
    if (creature->IsActivePlayer()) {
      return 0;
    }

    creature->Sound().PlayCreatureSound(*creature, 5u, false);
    return 1;
  }

  if (fourcc == kFourCC_CSD) {
    return PlayCustomAnimationSound(*creature, param);
  }

  if (fourcc == kFourCC_ESD) {
    return PlayEmoteStateAnimationSound(*creature, position);
  }

  if (fourcc == kFourCC_BRT || fourcc == kFourCC_SCD ||
      fourcc == kFourCC_SMG || fourcc == kFourCC_SMD) {
    const auto* const sound_data = ResolveActiveCreatureSoundDataEntry(*creature);
    return sound_data != nullptr
               ? PlayDirectAnimationSound(creature->sound_runtime(),
                     ResolveDirectCreatureSoundKitId(*sound_data, fourcc), position)
               : 0;
  }

  if (fourcc == kFourCC_FSD) {
    UnitSound_PlayAtPosition(creature, position);
    return 1;
  }

  if (fourcc == kFourCC_WNG || fourcc == kFourCC_WGG) {
    creature->Sound().PlayCreatureSound(
        *creature, fourcc == kFourCC_WNG ? 7u : 10u, false);
    return 1;
  }

  return 0;
}

int UnitSound_PlayPetActionSound(const CGUnit_C& unit,
                                  const std::uint32_t action_type) {

  switch (action_type) {
    case 0:
      return unit.Sound().PlayPrioritySound(unit, 1u);
    case 1:
      return unit.Sound().PlayPrioritySound(unit, 2u);
    default:
      return 0;
  }
}

void UnitSound_PlayAtPosition(void* unit_ptr, const float* position) {
  auto* const unit = static_cast<CGUnit_C*>(unit_ptr);
  if (unit == nullptr || position == nullptr) {
    return;
  }

  if ((unit->State().GetVisFlags() & 0x02u) != 0u ||
      (unit->Movement().UpdateFlags() & 0x40000000u) != 0u) {
    return;
  }
  if (const auto* const passenger = unit->Vehicle().GetVehiclePassengerComponent();
      passenger != nullptr &&
      passenger->GetTransitionState() != VehiclePassengerTransitionType::kExit &&
      passenger->GetTransitionState() != VehiclePassengerTransitionType::kAttached) {
    return;
  }

  const auto* const sound_data = ResolveActiveCreatureSoundDataEntry(*unit);
  if (sound_data == nullptr || sound_data->sound_footstep_id == 0u) {
    return;
  }

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.Exists(std::string(kFootstepSoundsCVarName)) ||
      !cvars.GetCVarBool(std::string(kFootstepSoundsCVarName))) {
    return;
  }

  const UnitSoundGroundState ground = ResolveUnitSoundGroundState(*unit, position);
  const bool use_wet_variant =
      ground.has_liquid_surface && ground.liquid_type_id != 0u &&
      ground.liquid_surface_z > position[2] + 0.01f &&
      !unit->Movement().IsSwimming();

  auto& sound = unit->sound_runtime();
  openwow::audio::UnitSoundCallbacks callbacks;
  callbacks.resolve_sound_kit = [&sound](const std::uint32_t terrain_type_id,
                                         const std::uint32_t lookup_id,
                                         const bool wet) {
    return sound.ResolveUnitSoundKit(terrain_type_id, lookup_id, wet);
  };
  callbacks.is_player_unit = [unit](const std::uintptr_t) {
    return unit->IsPlayer();
  };
  callbacks.is_active_player_object = [unit](const std::uintptr_t) {
    return unit->IsActivePlayer();
  };
  callbacks.query_listener_at_character_cvar = [&cvars]()
      -> std::optional<bool> {
    if (!cvars.Exists("Sound_ListenerAtCharacter")) {
      return std::nullopt;
    }
    return cvars.GetCVarBool("Sound_ListenerAtCharacter");
  };
  callbacks.play_sound_kit = [&sound](
      const std::uint32_t sound_kit_id, const float* const sound_position,
      const openwow::audio::UnitSoundPlaybackParams& params) {
    openwow::audio::SoundKitPlaybackOptions options;
    options.sound_type = params.sound_type_override;
    if (params.playback_priority !=
        std::numeric_limits<std::uint32_t>::max()) {
      options.playback_priority = params.playback_priority;
    }
    options.volume_scale = params.volume_scale;

    options.max_distance_override = params.max_distance_override;
    options.max_audible_behavior =
        params.channel_play_state_action == 0
            ? openwow::audio::SoundKitMaxAudibleBehavior::kMuteAndContinue
            : openwow::audio::SoundKitMaxAudibleBehavior::kStealLowest;
    (void)sound.PlaySoundKit(sound_kit_id, sound_position, nullptr, options);
  };

  openwow::audio::PlayUnitSound(
      sound_data->sound_footstep_id, position, ground.terrain_type_id,
      use_wet_variant, unit->Presentation().SizeClass() > 2u,
      reinterpret_cast<std::uintptr_t>(unit), callbacks);
}

void UnitSound_PlayDeathThud(CGUnit_C& unit) {
  const Position position = unit.GetPosition();
  const float sound_position[3] = {position.x, position.y, position.z};
  const UnitSoundGroundState ground =
      ResolveUnitSoundGroundState(unit, sound_position);

  if (ground.has_liquid_surface &&
      ground.liquid_surface_z - position.z > 2.0f) {
    return;
  }

  const std::uint32_t size_class = unit.Presentation().SizeClass();
  if (size_class >= kDeathThudSizeClassCount) {
    return;
  }

  const auto* const dbc = unit.dbc_loader();
  if (dbc == nullptr) {
    return;
  }
  const auto* const terrain =
      dbc->terrain_type().LookupEntry(ground.terrain_type_id);
  if (terrain == nullptr) {
    return;
  }

  std::uint32_t sound_kit_id = UnitSound_GetDeathThudSoundKit(
      size_class, terrain->sound_id, ground.has_liquid_surface);
  if (sound_kit_id == 0u) {
    static constexpr std::array<std::uint32_t, kDeathThudSizeClassCount>
        kDefaultSoundKitIds{0x38Bu, 0x390u, 0x395u, 0x39Au, 0x39Fu};
    sound_kit_id = kDefaultSoundKitIds[size_class];
  }

  (void)unit.sound_runtime().PlaySoundKit(
      sound_kit_id, sound_position);
}

int PlayerSound_PlayKit(void* const unit, const int sound_type,
                        const bool force) {
  if (unit == nullptr || sound_type < 0) {
    return 0;
  }

  auto *const creature = static_cast<CGUnit_C *>(unit);
  creature->Sound().PlayCreatureSound(
      *creature, static_cast<std::uint32_t>(sound_type), force);
  return 0;
}

void UnitSound_InitializeFootsteps(
    openwow::audio::SoundRuntime& sound_runtime,
    const openwow::data::dbc::DbcLoader& dbc) {
  g_death_thud_sound_runtime_state.Rebuild(&dbc);
  RebuildFootstepSoundLookup(sound_runtime, &dbc);
  EnsureFootstepSoundsCVarRegistered();
  UnitSoundComponent::SetCreatureStandSoundThrottle(
      openwow::core::GameClock::GetTickCount32());
  g_spirit_wolf_push_to_talk_sound_kit_id =
      ResolveSpiritWolfPushToTalkSoundKitId(sound_runtime, dbc);
}

void UnitSound_FreeDeathThudTables() {
  g_death_thud_sound_runtime_state.Clear();
}

std::uint32_t UnitSound_GetDeathThudSoundKit(const std::uint32_t size_class,
                                             const std::uint32_t terrain_type_sound_id,
                                             const bool use_water_variant) {
  return g_death_thud_sound_runtime_state.GetSoundKit(size_class, terrain_type_sound_id,
                                                      use_water_variant);
}

std::uint32_t UnitSound_GetSpiritWolfPushToTalkSoundKitId() {
  return g_spirit_wolf_push_to_talk_sound_kit_id;
}

void SetUnitSoundGroundStateCallback(
    const UnitSoundGroundStateCallback callback, void* const context) {
  g_unit_sound_ground_state_callback = callback;
  g_unit_sound_ground_state_context = context;
}

void ClearUnitSoundGroundStateCallback() {
  g_unit_sound_ground_state_callback = nullptr;
  g_unit_sound_ground_state_context = nullptr;
}

bool UnitSound_QueryGroundState(const CGUnit_C& unit,
                                const float* const event_position,
                                UnitSoundGroundState& out_state) {
  if (g_unit_sound_ground_state_callback == nullptr) {
    out_state = {};
    return false;
  }
  out_state = g_unit_sound_ground_state_callback(
      unit, event_position, g_unit_sound_ground_state_context);
  return true;
}

static bool s_vehicle_update_pending = false;

void VehiclePassenger_SetUpdateFlag() {
  s_vehicle_update_pending = true;
}

bool VehiclePassenger_IsUpdatePending() {
  return s_vehicle_update_pending;
}

void VehiclePassenger_ClearUpdateFlag() {
  s_vehicle_update_pending = false;
}

void InputControl_SetMouseYaw(void* passenger_component, const float yaw_angle) {
  if (passenger_component == nullptr) {
    return;
  }

  static_cast<VehiclePassengerC*>(passenger_component)->SetMouseYawOverride(
      yaw_angle);
}

int Vehicle_RelinkOpaqueNode(void* unit, void* node, int ) {
  auto* const owner = static_cast<CGUnit_C*>(unit);
  auto* const vehicle_data = owner->Vehicle().GetVehicleData();
  if (vehicle_data == nullptr) {
    return 0;
  }

  return vehicle::Vehicle_C_UnlinkAndReinsertNode(vehicle_data, node);
}

void Vehicle_ProcessDescriptorDirty(WorldSession& session, void* unit) {
  auto* const owner = static_cast<CGUnit_C*>(unit);
  if (owner == nullptr) {
    return;
  }

  auto* const descriptor = owner->Vehicle().GetVehicleData();
  if (descriptor == nullptr) {
    return;
  }

  const auto dirty_flags = LoadVehicleDescriptorField<std::uint32_t>(
      descriptor, kVehicleDescriptorDirtyFlagsOffset);
  if ((dirty_flags & kVehicleDescriptorDirtyFlag) == 0u) {
    return;
  }

  if (IsVehicleDescriptorRenderReady(*owner)) {
    const auto m2_instance_id =
        static_cast<std::int32_t>(owner->GetPrimaryM2InstanceId());
    for (std::uint32_t seat_index = 0; seat_index < kVehicleDescriptorSeatIndexLimit;
         ++seat_index) {
      const auto mask_offset = kVehicleDescriptorDirtyMaskOffset +
                               (seat_index >> 5) * sizeof(std::uint32_t);
      const auto mask_word =
          LoadVehicleDescriptorField<std::uint32_t>(descriptor, mask_offset);
      const auto seat_bit = 1u << (seat_index & 0x1Fu);
      if ((mask_word & seat_bit) == 0u) {
        continue;
      }

      ProcessVehicleDescriptorSeatAnimation(session, *owner, descriptor,
                                            m2_instance_id,
                                            ClampVehicleDescriptorSeatIndex(seat_index));
    }
  }

  ClearVehicleDescriptorDirtyState(descriptor);
}

void SetVehicleDescriptorRenderReadyCallback(
    VehicleDescriptorRenderReadyCallback callback, void* context) {
  g_vehicle_descriptor_render_ready_callback = callback;
  g_vehicle_descriptor_render_ready_context = context;
}

void ClearVehicleDescriptorRenderReadyCallback() {
  g_vehicle_descriptor_render_ready_callback = nullptr;
  g_vehicle_descriptor_render_ready_context = nullptr;
}

void Unit_ResetVehicleCameraAttachmentCache() {
}

void Unit_ClearPassengerComponent(void* unit) {
  auto* const owner = static_cast<CGUnit_C*>(unit);
  if (owner == nullptr) {
    return;
  }

  UnitVehicle_ReleasePassengerForUnit(*owner);
}

int InputControl_CheckTurnFlag(const void* unit) {
  const auto* mover = static_cast<const CGUnit_C*>(unit);
  if (mover == nullptr) {
    return 0;
  }

  const auto* const objects = mover->object_manager();
  if (objects == nullptr) {
    return 0;
  }

  const auto* player = objects->GetLocalPlayerTyped();
  if (player == nullptr) {
    return 0;
  }

  const auto* passenger = player->Vehicle().GetVehiclePassengerComponent();
  if (passenger == nullptr || !passenger->IsAttachedToVehicle()) {
    return 0;
  }

  const auto* vehicle_unit = passenger->GetVehicleUnit();
  if (vehicle_unit == nullptr || vehicle_unit != mover) {
    return 0;
  }

  const auto* seat_entry = passenger->GetSeatEntry();
  if (seat_entry == nullptr) {
    return 0;
  }

  return (seat_entry->flags & kVehicleSeatFlagTurnWhileMoveAndSteer) != 0u
             ? 1
             : 0;
}

}

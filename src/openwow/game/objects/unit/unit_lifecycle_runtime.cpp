#include "openwow/game/objects/cgunit.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/ceffect_c.h"
#include "openwow/game/chat_bubble.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/skill_dbc_helpers.h"
#include "openwow/game/spell_missile.h"
#include "openwow/game/unit_combat.h"
#include "openwow/game/unit_descriptor_callbacks.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/vehicle.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/world_environment_state.h"
#include "openwow/game/world_session.h"

#include <cstdint>
#include <cstring>

namespace openwow::game {

CGUnit_C::~CGUnit_C() {
  CleanupUnitResources();
}

void CGUnit_C::InitUnitData() {
  for (int i = 0; i < kLinkedListNodeCount; ++i) {
    linked_list_counts_[i] = 0;
  }

  vehicle_.Cleanup(*this);

  State().ResetCreatureMetadata();

  Movement().ResetState();
  Presentation().ResetRuntimeState();
}

void CGUnit_C::Initialize(WorldSession& session) {
  const auto *const dbc = session.GetDbcLoader();
  if (dbc != nullptr) {
    UnitSound_InitializeFootsteps(session.sound_runtime(), *dbc);
  }

  SkillDbcHelpers::RegisterSpellOpcodeHandlers();
  unit_combat::CombatLog_Initialize();
  RegisterUnitDescriptorCallbacks(session);

  if (dbc != nullptr) {
    UnitAnimationRuntime::AssignAnimationSlotsByFlags(*dbc);
  } else {
    UnitAnimationRuntime::ClearAnimationSlots();
  }

  power_display_id_by_type_.fill(0);
  if (dbc != nullptr) {
    const auto &pd_store = dbc->power_display();
    const auto &rows = pd_store.entries();
    for (auto row = rows.rbegin(); row != rows.rend(); ++row) {
      if (row->actual_type < power_display_id_by_type_.size()) {
        power_display_id_by_type_[row->actual_type] = row->id;
      }
    }
  }

  auto &control = session.player_control_runtime();
  control.combat_focus_guid = 0;
  control.combat_focus_locked = false;
  control.combat_focus_enabled = 1;
  UnitMovementRuntime::ResetCanFlyGroundContactRuntimeForTesting();

  CMissile_RegisterTypeAndLifecycleCallback();
  vehicle::Vehicle_C_Init();
  VehiclePassengerC::Initialize();
  Unit_ResetVehicleCameraAttachmentCache();

  unit_threat_type_registered_ = true;
}

void CGUnit_C::UnregisterOpcodes() {
}

void CGUnit_C::PerFrameWorldUpdate(std::uint32_t tick_count) {
  UpdateSceneEnvironmentCache(tick_count);
  (void)SpellVisuals().UpdateVisibleHumanoidDisplay(GetEntry(), 0u);

  Presentation().UpdateIdleAnimationLatch();
  SpellVisuals().AdvanceFrame(tick_count);

}

void CGUnit_C::UpdateSceneEnvironmentCache(const std::uint32_t tick_count) {
  RefreshSceneEnvironmentCache(tick_count);
}

bool CGUnit_C::IsSceneSubmergedBelowLiquidSurface() const noexcept {
  return (scene_environment_flags_ & 0x20u) != 0u;
}

bool CGUnit_C::IsSceneInSnowArea() const noexcept {
  return (scene_environment_flags_ & 0x40u) != 0u;
}

void CGUnit_C::RefreshSceneEnvironmentCache(const std::uint32_t tick_count) {
  constexpr std::uint32_t kRefreshIntervalMs = 10000u;
  constexpr std::uint32_t kSubmergedBelowLiquidSurface = 0x20u;
  constexpr std::uint32_t kSnowArea = 0x40u;
  constexpr float kLiquidClearanceAboveModel = 5.0f;

  if (static_cast<std::int32_t>(tick_count - scene_environment_refresh_tick_) < 0) {
    return;
  }

  scene_environment_flags_ &= ~(kSubmergedBelowLiquidSurface | kSnowArea);
  const auto* const environment = world_environment();
  if (environment != nullptr) {
    const auto position = GetPosition();
    if (const auto liquid_surface = environment->QueryLiquidSurfaceHeight(
            position.x, position.y, position.z);
        liquid_surface.has_value() &&
        Presentation().ModelHeight() + kLiquidClearanceAboveModel <
            *liquid_surface - position.z) {
      scene_environment_flags_ |= kSubmergedBelowLiquidSurface;
      scene_environment_refresh_tick_ = tick_count + kRefreshIntervalMs;
      return;
    }
    if (const auto snow = environment->QuerySnowStateAtWorldPosition(
            position.x, position.y, position.z);
        snow.value_or(false)) {
      scene_environment_flags_ |= kSnowArea;
    }
  }
  scene_environment_refresh_tick_ = tick_count + kRefreshIntervalMs;
}

void CGUnit_C::PrepareForWorldRemoval() {
  CGObject_C::PrepareForWorldRemoval();
  CleanupUnitResources();
}

void CGUnit_C::CleanupUnitResources() {
  vehicle_.Cleanup(*this);
  SpellVisuals().Cleanup();

  Mount().ReleaseOverlayM2Instance(*this);

  CEffect_C::DetachAllFromOwner(GetGuid());
  Movement().Cleanup();
  aura_.SetAuras({});
  Casts().ClearSpellTracking();
  Animation().ResetInternalEmoteStorage();

  ChatBubbleSystem::Get().RemoveBubblesForUnit(GetGuid());

  Presentation().PreserveOrReleaseCharacterVisualOnCleanup();
}

}

#pragma once

#include "openwow/game/missile_node.h"
#include "openwow/game/missile_trajectory.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/unit/unit_aura.h"
#include "openwow/game/objects/unit/unit_aura_info.h"
#include "openwow/game/objects/unit/unit_cast_runtime.h"
#include "openwow/game/objects/unit/unit_animation_runtime.h"
#include "openwow/game/objects/unit/unit_interaction_runtime.h"
#include "openwow/game/objects/unit/unit_loot.h"
#include "openwow/game/objects/unit/unit_mount.h"
#include "openwow/game/objects/unit/unit_movement_runtime.h"
#include "openwow/game/objects/unit/unit_nameplate.h"
#include "openwow/game/objects/unit/unit_predicted_power.h"
#include "openwow/game/objects/unit/unit_presentation_runtime.h"
#include "openwow/game/objects/unit/unit_sound.h"
#include "openwow/game/objects/unit/unit_spell_visual_runtime.h"
#include "openwow/game/objects/unit/unit_state_runtime.h"
#include "openwow/game/objects/unit/unit_vehicle.h"
#include "openwow/game/unit_defines.h"
#include "openwow/game/unit_tooltip_info.h"
#include "openwow/game/vec3.h"
#include "openwow/render/m2/m2_public_types.h"
#include "openwow/render/api/math/render_math_types.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace openwow::game {

class WorldSession;
class VehiclePassengerC;
class CGCorpse_C;
class CGGameObject_C;
class QueryCache;
class ItemDefinitions;
class PacketReader;

}

namespace openwow::data::dbc {
class DbcLoader;
template <typename T> class DbcStore;
struct AreaGroupEntry;
struct AreaTableEntry;
struct CreatureDisplayInfoEntry;
struct CreatureDisplayInfoExtraEntry;
struct CreatureModelDataEntry;
struct CreatureSoundDataEntry;
struct EmotesEntry;
struct SpellEntry;
struct SpellVisualEntry;
struct SpellVisualKitEntry;
struct VehicleSeatEntry;
struct VehicleEntry;
}

namespace openwow::world {
class WorldCamera;
}

namespace openwow::game {

struct DisplayInfoEntry;

enum class ProcTriggerType : std::uint8_t;

namespace vehicle {
int Vehicle_ProcessDirtySeatAnimation(WorldSession& session,
                                      class CGUnit_C& ownerUnit,
                                      void* vehicleData,
                                      std::int32_t m2InstanceId,
                                      std::uint32_t seatIndex);
void Vehicle_RefreshOwnerStandAnimation(WorldSession& session,
                                        class CGUnit_C& ownerUnit);
}

class CGUnit_C : public CGObject_C {
  friend class UnitAnimationRuntime;
  friend class UnitCastRuntime;
  friend class UnitInteractionRuntime;
  friend class UnitMovementRuntime;
  friend class UnitPresentationRuntime;
  friend struct PlayerControlRuntime;
  friend class UnitVehicleComponent;
  friend class CGPlayer_C;
  friend class CMissileNode_C;
  friend class WorldSession;
  friend int vehicle::Vehicle_ProcessDirtySeatAnimation(
                                               WorldSession& session,
                                               CGUnit_C& ownerUnit,
                                               void* vehicleData,
                                               std::int32_t m2InstanceId,
                                               std::uint32_t seatIndex);
  friend void vehicle::Vehicle_RefreshOwnerStandAnimation(
      WorldSession& session, CGUnit_C& ownerUnit);
  friend void FinalizeSpellEffectsOnUnit(
      const WorldSession& session, CGUnit_C& unit,
                                         std::uint32_t spell_id,
                                         const data::dbc::SpellEntry* spell_rec);
  friend void ClearActivePlayerAutoRepeatState(WorldSession &session);
public:
  CGUnit_C(ItemDefinitions& item_definitions,
           const data::dbc::DbcLoader& dbc_loader,
           TypeID type_id = TypeID::kUnit);
  CGUnit_C(ItemDefinitions& item_definitions,
           const data::dbc::DbcLoader& dbc_loader, ObjectGuid guid,
           TypeID type_id = TypeID::kUnit);
  CGUnit_C(ObjectManager& objects, ItemDefinitions& item_definitions,
           const data::dbc::DbcLoader& dbc_loader, ObjectGuid guid,
           TypeID type_id = TypeID::kUnit);
  ~CGUnit_C() override;

  [[nodiscard]] UnitVehicleComponent &Vehicle() noexcept { return vehicle_; }
  [[nodiscard]] const UnitVehicleComponent &Vehicle() const noexcept { return vehicle_; }

  CGUnit_C(const CGUnit_C &) = delete;
  CGUnit_C &operator=(const CGUnit_C &) = delete;
  CGUnit_C(CGUnit_C &&) = delete;
  CGUnit_C &operator=(CGUnit_C &&) = delete;

  [[nodiscard]] const data::dbc::DbcLoader* dbc_loader() const noexcept { return &dbc_loader_; }
  [[nodiscard]] UnitAnimationRuntime &Animation() noexcept { return animation_; }
  [[nodiscard]] UnitMovementRuntime &Movement() noexcept { return movement_; }
  [[nodiscard]] const UnitMovementRuntime &Movement() const noexcept { return movement_; }
  [[nodiscard]] UnitInteractionRuntime &Interaction() noexcept { return interaction_; }
  [[nodiscard]] const UnitInteractionRuntime &Interaction() const noexcept { return interaction_; }
  [[nodiscard]] UnitPresentationRuntime &Presentation() noexcept { return presentation_; }
  [[nodiscard]] const UnitPresentationRuntime &Presentation() const noexcept { return presentation_; }
  [[nodiscard]] UnitSpellVisualRuntime& SpellVisuals() noexcept { return spell_visuals_; }
  [[nodiscard]] const UnitSpellVisualRuntime& SpellVisuals() const noexcept { return spell_visuals_; }
  [[nodiscard]] const UnitAnimationRuntime &Animation() const noexcept { return animation_; }
  [[nodiscard]] UnitStateRuntime& State() noexcept { return state_; }
  [[nodiscard]] const UnitStateRuntime& State() const noexcept { return state_; }

  [[nodiscard]] bool CannotBeResurrected() const noexcept {
    return (client_state_flags_ & kClientStateCannotBeResurrected) != 0u;
  }

  [[nodiscard]] bool ShouldFadeOnShow() const override;

  [[nodiscard]] float GetModelOpacity() const override;

  [[nodiscard]] bool CanBeTransportParent() const override;

  void GetWorldMatrix(float* out_matrix) const override;

  bool UpdateModelNodeTransform(float dt, std::uint32_t current_tick_ms) override;

  std::vector<std::uint16_t> ApplyCreateUpdate(const CreateObjectUpdate &upd) override;
  void FinalizeCreateUpdate(const CreateObjectUpdate &upd) override;

  void FinalizePacketUpdatePromotion() override;
  std::vector<std::uint16_t> ApplyValuesUpdate(const ValuesUpdate &upd) override;
  bool ApplyMovementUpdate(const MovementOnlyUpdate &upd) override;

  std::uint32_t UpdateOverlayModel() override;

  [[nodiscard]] std::uint32_t GetHealth() const override;
  [[nodiscard]] std::uint32_t GetMaxHealth() const override;
  [[nodiscard]] std::uint32_t GetLevel() const override;
  [[nodiscard]] std::uint32_t GetFactionTemplate() const override;
  [[nodiscard]] ObjectGuid GetTransportGUID() const override;

  [[nodiscard]] UnitAuraComponent &Auras() noexcept;
  [[nodiscard]] const UnitAuraComponent &Auras() const noexcept;
  [[nodiscard]] UnitCastRuntime &Casts() noexcept;
  [[nodiscard]] const UnitCastRuntime &Casts() const noexcept;

  [[nodiscard]] UnitLootComponent &Loot() noexcept;
  [[nodiscard]] const UnitLootComponent &Loot() const noexcept;
  [[nodiscard]] UnitMountComponent &Mount() noexcept;
  [[nodiscard]] const UnitMountComponent &Mount() const noexcept;

  static constexpr std::uint8_t kHighlightTypeTarget =
      UnitNameplateComponent::kHighlightTypeTarget;
  static constexpr std::uint8_t kHighlightTypeMouseover =
      UnitNameplateComponent::kHighlightTypeMouseover;
  static constexpr std::uint8_t kHighlightTypeAlphaPreserving =
      UnitNameplateComponent::kHighlightTypeAlphaPreserving;
  static constexpr std::uint32_t kNameplateHighlightBitBase =
      UnitNameplateComponent::kHighlightBitBase;
  static constexpr std::uint32_t kNameplateHighlightMask =
      UnitNameplateComponent::kHighlightMask;
  static constexpr std::uint32_t kNameplateHighlightAlphaPreservingOnlyMask =
      UnitNameplateComponent::kAlphaPreservingOnlyMask;

  [[nodiscard]] UnitNameplateComponent &Nameplate() noexcept;
  [[nodiscard]] const UnitNameplateComponent &Nameplate() const noexcept;

  [[nodiscard]] std::uint32_t GetDisplayId() const override;

  void OnLevelChanged(const WorldSession& session);

  [[nodiscard]] const char *GetPortraitTextureName() const override;

  [[nodiscard]] UnitSoundComponent &Sound() noexcept;
  [[nodiscard]] const UnitSoundComponent &Sound() const noexcept;

  [[nodiscard]] std::uint32_t GetBasePowerByType(std::int32_t power_type) const;

  [[nodiscard]] std::uint32_t GetPowerOrHealth(std::int32_t power_type) const;

  [[nodiscard]] UnitPredictedPowerComponent &Vitals() noexcept;
  [[nodiscard]] const UnitPredictedPowerComponent &Vitals() const noexcept;

  [[nodiscard]] float GetPowerRegenRate(std::uint8_t power_type) const;
  [[nodiscard]] float GetPowerRegenRateInterrupted(std::uint8_t power_type) const;
  void OnRightClickInteract(WorldSession *session, TargetingSystem *targeting) const override;
  [[nodiscard]] UnitTooltipInfo BuildTooltipInfo(const WorldSession &session) const;

  [[nodiscard]] std::string ResolveRetailName(
      const WorldSession &session,
      std::string* out_realm = nullptr,
      bool follow_name_override_aura = true) const;

  [[nodiscard]] std::uint32_t FormatNameWithPvpTitle(
      const WorldSession &session,
      bool include_title, std::string &out) const;

  [[nodiscard]] std::uint32_t BuildTooltipNameText(
      const WorldSession &session,
      std::uint32_t flags, std::string &out,
      bool check_vehicle_auras = false) const;
  [[nodiscard]] bool IsFacingTargetByGuid(ObjectGuid target_guid) const;

  void ShowMissTypeFCT(std::uint32_t miss_index, bool is_player_target, bool is_pet) const;

  static int OnModelScaleChanged(ObjectManager &objects, std::uint64_t guid);

  static int HandleLootListOpcode(ObjectManager &objects, std::uint64_t guid,
                                  int field_offset, int old_val,
                                  const std::uint8_t *packet);
  static int OnPowerValueChanged(ObjectManager &objects, WorldSession &session,
                                 std::uint64_t guid, int field_offset);

  static int OnMaxPowerChanged(ObjectManager &objects, WorldSession &session,
                               std::uint64_t guid, int field_offset);

  static int OnPowerTypeChanged(ObjectManager &objects, WorldSession &session,
                                std::uint64_t guid);

  virtual void OnDestroyEffectNode(const WorldSession& session,
                                   const UnitSpellVisualRuntime::AttachedEffectNode& node);

  void ResetMatchingSpellVisualNodes(
      const WorldSession& session,
      std::uint32_t spell_id,
      std::uint32_t visual_kit_param) override;

  static int OnCastingSpellChanged(std::uint64_t guid, int field_offset, int old_val,
                                   const std::uint32_t *new_val);

  static int OnVisFlagsChanged(ObjectManager &objects, WorldSession &session,
                               std::uint64_t guid,
                               int field_offset, int old_val,
                               const std::uint8_t *new_val);

  static int OnShapeshiftFormChanged(std::uint64_t guid);

  static int OnChannelObjectOrSpellChanged(
      ObjectManager& objects, WorldSession& session, std::uint64_t guid,
      const std::uint32_t* new_value);

  static int OnObjectScaleChanged(ObjectManager &objects, std::uint64_t guid,
                                  int field_offset, int old_val,
                                  const float *new_val);

  static int OnMountDisplayChanged(ObjectManager &objects,
                                   const WorldSession& session,
                                   std::uint64_t guid);

  static int OnGroupMemberUpdate(std::uint64_t guid, int opcode, int a3,
                                 const std::uint8_t *packet);

  static void InitVf0_ScalarDeletingDestructor(void *block, bool free_mem);

  static void *InitVf67_AllocInventoryArt(int extra_bytes, bool clear);

  static void CleanupInventoryArtBlock(void *block);

  static void InitVf68_DestroyInventoryArt(void *block, bool free_mem);

  static void InitVf69_ResetInventoryArt(void *block);

  static void InitVf70_FreeItemByName(void *block);

  static void *InitVf71_AllocItemByName(int extra_bytes, bool clear);

  static void CleanupItemByNameBlock(void *block);

  static void InitVf72_DestroyItemByName(void *block, bool free_mem);

  static void InitVf73_ResetItemByName(void *block);

  void DisplayPowerGain(std::int32_t amount) const;

  void DisplayHealing(std::int32_t amount) const;

  static int HandleUnitFunc3(ObjectManager &objects, std::uint64_t guid);

  static int HandleUnitFunc4(ObjectManager &objects, WorldSession &session,
                             std::uint64_t guid);

  static int OnTargetFieldChanged(ObjectManager &objects, std::uint64_t guid);

  [[nodiscard]] std::uint32_t GetTargetChangeTimeMs() const {
    return target_change_time_ms_;
  }
  void SetTargetChangeTimeMs(std::uint32_t tick_ms) {
    target_change_time_ms_ = tick_ms;
  }

  static constexpr std::uint32_t kTargetChangeRingDurationMs = 1000u;

  static int HandleUnitFunc6(const std::uint8_t *packet_data);

  static int HandleUnitFunc7(const std::uint8_t *packet_data);

  static int HandleUnitFunc8(ObjectManager &objects, const std::uint8_t *packet_data);

  static int HandleUnitFunc9(const std::uint8_t *packet_data);

  static int HandleUnitFunc10(const std::uint8_t *packet_data);

  static int HandleUnitFunc11(const std::uint8_t *packet_data);

  void InitUnitData();

  static void Initialize(WorldSession& session);

  static void UnregisterOpcodes();

  static int DescriptorCallback_LevelChanged(ObjectManager &objects,
                                            const WorldSession& session,
                                            std::uint64_t guid,
                                            const std::uint32_t *new_val);
  void HandleAppearanceUpdatePacket(const std::uint8_t *packet);
  void PerFrameWorldUpdate(std::uint32_t tick_count);
  void UpdateSceneEnvironmentCache(std::uint32_t tick_count);
  [[nodiscard]] bool IsSceneSubmergedBelowLiquidSurface() const noexcept;
  [[nodiscard]] bool IsSceneInSnowArea() const noexcept;

  void OnVirtualItemDisplayChanged(std::uint32_t weapon_slot);

  static int OpcodeHandler_AppearanceUpdate(ObjectManager &objects,
                                             std::int32_t opcode,
                                             const std::uint8_t *packet);
  void PrepareForWorldRemoval() override;
  void CleanupUnitResources();

  bool GetGroupMemberStatus(const std::uint64_t *member_guid, std::uint8_t *status_plus_one,
                            std::uint8_t *raw_percent, float *scaled_percent,
                            std::uint32_t *threat_value) const;

  void OnUnitFlags2Changed(WorldSession &session, std::uint32_t old_flags);

  void HandleVisFlagsUpdate(WorldSession &session, std::uint8_t new_vis_flags);
  static int CombatLogUpdateHandler(WorldSession &session,
                                    std::int32_t opcode,
                                    const std::uint8_t *packet);

protected:
  ItemDefinitions& item_definitions_;
  const data::dbc::DbcLoader& dbc_loader_;

private:
  friend class UnitMountComponent;
  friend class CEffect_C;

  static constexpr std::uint32_t kClientStateCannotBeResurrected = 0x04000000u;

  std::uint32_t client_state_flags_ = 0u;

  std::uint32_t target_change_time_ms_ = 0u;

  void SeedTargetChangeTime();

  void FinalizeUnitCreateState(const CreateObjectUpdate &upd,
                               std::uint32_t previous_movement_flags,
                               bool base_post_init_complete);
  void RefreshSceneEnvironmentCache(std::uint32_t tick_count);

  UnitAuraComponent aura_;
  UnitCastRuntime casts_;
  UnitAnimationRuntime animation_;
  UnitMovementRuntime movement_;
  UnitInteractionRuntime interaction_;
  UnitPresentationRuntime presentation_;
  UnitSpellVisualRuntime spell_visuals_;
  UnitStateRuntime state_;
  UnitLootComponent loot_;

  std::uint32_t scene_environment_flags_{0};
  std::uint32_t scene_environment_refresh_tick_{0};

  UnitSoundComponent sound_;

  static inline std::array<std::uint32_t, 6> power_display_id_by_type_{};

  static inline bool unit_threat_type_registered_{false};

  UnitNameplateComponent nameplate_;

  static constexpr std::uint32_t kSpellStateDeadOnInit = 0x100000u;

  static constexpr std::uint32_t kSpellStateClientControlGranted = 0x400u;

  UnitMountComponent mount_;

  UnitPredictedPowerComponent predicted_power_;

  UnitVehicleComponent vehicle_;

  static constexpr std::uint32_t kSpellStateSuppressMountFootprint = 0x10000000u;

  static constexpr int kLinkedListNodeCount = 142;
  std::uint32_t linked_list_counts_[kLinkedListNodeCount]{};

  std::uint32_t group_status_guid_low_{0};
  std::uint32_t group_status_guid_high_{0};

  bool has_active_procs_{false};
};

using JumpLiquidSurfaceHeightCallback = std::optional<float> (*)(
    const CGUnit_C &unit, void *context);

struct UnitWaterRippleSpawn {
  std::array<float, 3> position{};
  float rotation_radians{};
  float initial_extent{};
  float duration_seconds{};
  float opacity_base{};
  float extent_rate{};
  bool use_splash_texture{};
  bool use_local_player_pool{};
};

using WaterRippleSpawnCallback = void (*)(const UnitWaterRippleSpawn& spawn,
                                          void* context);

void SetJumpLiquidSurfaceHeightCallback(
    JumpLiquidSurfaceHeightCallback callback, void *context);
void ClearJumpLiquidSurfaceHeightCallback();
void SetWaterRippleSpawnCallback(WaterRippleSpawnCallback callback,
                                 void* context);
void ClearWaterRippleSpawnCallback();

}

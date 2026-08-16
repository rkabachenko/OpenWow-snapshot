#pragma once

#include "openwow/game/objects/cgunit.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace openwow::data::dbc {
struct ItemDisplayInfoEntry;
struct SpellEntry;
struct SpellVisualEntry;
}

namespace openwow::game {

class ObjectManager;
class CGItem_C;
class WorldSession;
class CGPlayer_C;
struct CreatureTemplateInfo;

using PlayerAnimationProgressCallback = float (*)(const CGPlayer_C& player, void* context);

void SetPlayerAnimationProgressCallback(PlayerAnimationProgressCallback callback, void* context);
void ClearPlayerAnimationProgressCallback();

struct PendingItemEnchantTimeUpdate {
  ObjectGuid item_guid{};
  std::uint32_t enchant_slot{0};
  std::int32_t duration_seconds{0};
};

struct PendingAuraVisualEntry {
  std::uint32_t spell_id{0};
  std::uint32_t visual_kit_id{0};
  std::uint32_t dispatch_type{0};
  std::array<float, 3> world_position{};
  std::uint32_t target_data_1{0};
  std::uint32_t target_data_2{0};
  std::uint32_t visual_kit_param{0};

  static constexpr std::uint32_t kFlagHasPosition  = 0x1u;
  static constexpr std::uint32_t kFlagMirrorEffect = 0x2u;
  static constexpr std::uint32_t kFlagInstant      = 0x4u;
  static constexpr std::uint32_t kFlagPendingInit  = 0x8u;
  std::uint32_t flags{0};

  std::uint32_t status{0};
  std::uint32_t reserved{0};
};

struct EquipmentSlotInfo {
  std::uint64_t item_guid = 0;
  std::uint32_t item_id = 0;
  std::uint32_t enchant_id = 0;

  std::uint16_t suffix_factor = 0;

  bool is_broken = false;
};

struct EquippedGemColorCounts {
  std::uint32_t meta = 0;
  std::uint32_t red = 0;
  std::uint32_t yellow = 0;
  std::uint32_t blue = 0;

  void Reset() { meta = red = yellow = blue = 0; }
};

struct VisibleItemTemplateMetadata {
  std::uint32_t entry = 0;
  std::uint32_t item_class = 0;
  std::uint32_t subclass = 0;
  std::int32_t sound_override = 0;
  std::int32_t material = -1;
  std::uint32_t inventory_type = 0;
  std::uint32_t sheath = 0;
  std::uint32_t display_id = 0;
};

class CGPlayer_C : public CGUnit_C {
public:
  CGPlayer_C(ItemDefinitions& item_definitions,
             const data::dbc::DbcLoader& dbc_loader);
  CGPlayer_C(ItemDefinitions& item_definitions,
             const data::dbc::DbcLoader& dbc_loader, ObjectGuid guid);
  CGPlayer_C(ObjectManager& objects, ItemDefinitions& item_definitions,
             const data::dbc::DbcLoader& dbc_loader, ObjectGuid guid);
  CGPlayer_C(CGPlayer_C&&) noexcept = delete;
  CGPlayer_C(const CGPlayer_C&) = delete;
  CGPlayer_C& operator=(const CGPlayer_C&) = delete;
  ~CGPlayer_C() override;

  std::vector<std::uint16_t> ApplyCreateUpdate(const CreateObjectUpdate& upd) override;
  void FinalizeCreateUpdate(const CreateObjectUpdate& upd) override;
  void FinalizePacketUpdatePromotion() override;

  [[nodiscard]] bool IsActivePlayer() const {
    return CGObject_C::IsActivePlayer();
  }

  [[nodiscard]] std::uint8_t GetCachedStandState() const {
    return cached_stand_state_;
  }

  [[nodiscard]] std::uint8_t GetPlayerStandState() const;

  [[nodiscard]] std::uint32_t GetVisualModelState() const {
    return visual_model_state_;
  }

  [[nodiscard]] std::uint32_t GetXP() const;
  [[nodiscard]] std::uint32_t GetNextLevelXP() const;

  [[nodiscard]] std::uint32_t GetMoney() const;
  [[nodiscard]] std::uint32_t GetInternalFlags() const {
    return internal_flags_;
  }
  void SetInternalFlags(std::uint32_t flags) {
    internal_flags_ = flags;
  }

  [[nodiscard]] float GetBlockPercentage() const;
  [[nodiscard]] float GetDodgePercentage() const;
  [[nodiscard]] float GetParryPercentage() const;
  [[nodiscard]] float GetCritPercentage() const;
  [[nodiscard]] float GetRangedCritPercentage() const;
  [[nodiscard]] float GetSpellCritPercentage(std::uint8_t school) const;

  struct SkillInfo {
    std::uint16_t skill_id = 0;
    std::uint16_t step = 0;
    std::uint16_t value = 0;
    std::uint16_t max_value = 0;
    std::int16_t modifier = 0;
    std::int16_t step_modifier = 0;
  };
  struct ActiveSkillValues {
    std::uint32_t adjusted_value = 0;
    std::uint16_t raw_value = 0;
  };
  [[nodiscard]] SkillInfo GetSkill(std::uint16_t index) const;
  [[nodiscard]] std::optional<std::uint16_t> FindSkillSlot(std::uint16_t skill_id) const;

  [[nodiscard]] std::optional<std::uint16_t> FindActiveSkillSlot(
      std::uint16_t skill_id) const;
  [[nodiscard]] std::optional<SkillInfo> FindSkill(std::uint16_t skill_id) const;

  [[nodiscard]] std::optional<ActiveSkillValues> FindActiveSkillValues(
      std::uint16_t skill_id) const;

  [[nodiscard]] std::uint16_t GetSkillValue(std::uint16_t skill_id) const;

  [[nodiscard]] std::uint16_t GetSkillMaxValue(std::uint16_t skill_id) const;

  [[nodiscard]] std::uint16_t GetSkillBonusValue(std::uint16_t skill_id) const;

  [[nodiscard]] std::uint32_t GetSkillValueWithStepModifier(
      std::uint16_t skill_id) const;

  [[nodiscard]] ObjectGuid GetInventorySlotGuid(std::uint8_t slot) const;
  [[nodiscard]] std::uint32_t GetVisibleItemEntry(std::uint8_t slot) const;
  [[nodiscard]] std::uint16_t GetVisibleItemEnchant(std::uint8_t slot) const;

  [[nodiscard]] std::optional<EquipmentSlotInfo>
      GetVisibleEquipSlotInfo(std::uint8_t slot) const;

  [[nodiscard]] std::uint32_t GetVisibleItemAuraVisual(std::uint8_t slot) const;
  [[nodiscard]] std::optional<std::uint32_t>
      GetVisibleItemTemplateEntry(std::uint8_t slot) const;
  [[nodiscard]] std::optional<VisibleItemTemplateMetadata>
      GetVisibleItemTemplateMetadata(std::uint8_t slot) const;

  [[nodiscard]] std::optional<VisibleItemTemplateMetadata>
      GetVisibleWeaponSlotMetadata(std::uint8_t visible_weapon_index,
                                   bool force_visible) const;

  [[nodiscard]] std::optional<std::uint32_t>
      GetVisibleWeaponDisplayIdRaw(std::uint8_t visible_weapon_index) const;

  [[nodiscard]] std::optional<std::uint32_t>
      GetVisibleWeaponDisplayId(std::uint8_t visible_weapon_index) const;

  [[nodiscard]] const openwow::data::dbc::ItemDisplayInfoEntry*
      GetVisibleWeaponDisplayInfo(std::uint8_t visible_weapon_index) const;

  [[nodiscard]] const openwow::data::dbc::ItemDisplayInfoEntry*
      GetEquipSlotItemDisplayRecord(std::uint8_t slot_index) const;

  [[nodiscard]] bool IsVisibleWeaponDisplaySuppressed(
      std::uint8_t visible_weapon_index) const;

  [[nodiscard]] bool IsVisibleWeaponSlotTwoHandWeapon(std::uint8_t slot) const;

  void RefreshWeaponAttachmentVisual(std::uint32_t attachment_id,
                                     std::uint8_t weapon_slot);

  bool SetItemVisual(std::uint8_t visible_item_slot,
                     std::uint32_t attachment_id);

  bool ConsumeItemVisualUpdate(std::uint32_t& out_attachment_id,
                               std::uint32_t& out_aura_visual_id);

  [[nodiscard]] ObjectGuid GetEquippedItem(std::uint8_t slot) const;

  [[nodiscard]] ObjectGuid GetBackpackItem(std::uint8_t slot) const;

  [[nodiscard]] ObjectGuid GetBagItem(std::uint8_t bag_slot, std::uint8_t item_slot) const;

  [[nodiscard]] ObjectGuid GetBagSlotGuid(std::uint8_t bag, std::uint8_t slot) const;

  void QueuePendingItemEnchantTimeUpdate(const ObjectGuid& item_guid,
                                         std::uint32_t enchant_slot,
                                         std::int32_t duration_seconds);
  void ApplyPendingItemEnchantTimeUpdates(CGItem_C& item);

  [[nodiscard]] std::uint32_t GetGuildID() const;
  [[nodiscard]] std::uint32_t GetGuildRank() const;

  struct QuestLogEntry {
    std::uint32_t quest_id = 0;
    std::uint32_t state = 0;
    std::uint32_t counts[4] = {0};
    std::uint32_t timer = 0;
  };
  [[nodiscard]] QuestLogEntry GetQuestLog(std::uint8_t slot) const;

  [[nodiscard]] std::uint32_t GetRestStateExperience() const;

  [[nodiscard]] bool IsRested() const;

  [[nodiscard]] bool IsResting() const;

  [[nodiscard]] bool IsGroupLeader(const WorldSession& session) const;

  [[nodiscard]] bool IsGroupAssistant(const WorldSession& session) const;

  [[nodiscard]] std::uint32_t GetFreeTalentPoints() const;

  [[nodiscard]] std::uint32_t GetWatchedFactionIndex() const;
  void SetWatchedFactionIndex(std::uint32_t index);

  [[nodiscard]] std::uint32_t GetPlayerFlags() const;
  [[nodiscard]] std::uint16_t GetOverrideSpellDataId() const;

  [[nodiscard]] float GetMeleeCritChance() const;
  [[nodiscard]] float GetRangedCritChance() const;
  [[nodiscard]] float GetSpellCritChance(std::uint8_t school = 1) const;
  [[nodiscard]] float GetDodgeChance() const;
  [[nodiscard]] float GetParryChance() const;
  [[nodiscard]] float GetBlockChance() const;
  [[nodiscard]] std::int32_t GetSpellBonusDamage(std::uint8_t school) const;
  [[nodiscard]] std::int32_t GetSpellBonusHealing() const;

  void SetEquipment(std::uint8_t slot, const EquipmentSlotInfo &item);
  [[nodiscard]] const EquipmentSlotInfo *GetEquipment(std::uint8_t slot) const;
  static constexpr std::uint8_t kMaxEquipSlots = 19;

  void RecountEquippedGemColorCounts();

  [[nodiscard]] const EquippedGemColorCounts &GetEquippedGemColorCounts() const {
    return equipped_gem_color_counts_;
  }

  void SetMoney(std::int32_t copper);
  void SetXP(std::uint32_t current, std::uint32_t max);

  [[nodiscard]] std::uint32_t GetHonorableKills() const;
  [[nodiscard]] std::uint32_t GetHonorPoints() const;
  [[nodiscard]] std::uint32_t GetArenaPoints() const;

  [[nodiscard]] std::uint8_t GetComboPoints() const;
  void SetComboPoints(std::uint8_t points);

  [[nodiscard]] bool IsGhost() const;

  [[nodiscard]] bool AutoInteractSuppressesInteractionRange() const;

  [[nodiscard]] bool IsAlive() const;

  [[nodiscard]] bool IsLooting() const;

  [[nodiscard]] std::string GetPlayerName() const;

  [[nodiscard]] std::string GetDisplayName() const;

  [[nodiscard]] ObjectGuid GetPlayerTarget() const;

  [[nodiscard]] bool HasNoPetFamily() const;

  [[nodiscard]] const CreatureTemplateInfo *GetSummonedUnitCreatureData() const;

  [[nodiscard]] std::uint32_t GetSummonedUnitArmorMaterialSoundCategory() const;

  void RenderAutoAttackInteractionIndicator();

  [[nodiscard]] std::string GetModelPath() const;

  [[nodiscard]] Position GetNamePlatePosition() const override;

  void PlayArmorFoleySoundForOthers();

  [[nodiscard]] float GetWalkAnimSpeed() const;

  [[nodiscard]] float GetPlayerCombatReach() const;

  [[nodiscard]] float GetWorldFacing() const override;

  [[nodiscard]] std::uint32_t BuildPlayerTooltipNameText(
      const WorldSession& session, std::uint32_t flags,
      std::string &out) const;

  [[nodiscard]] static bool CanShowResetInstances(const WorldSession* session);

  [[nodiscard]] ObjectGuid GetActiveControlGuid() const;

  [[nodiscard]] const CGUnit_C* GetActiveControlUnit() const;

  [[nodiscard]] bool CheckAllItemsNeedRepair() const;

  void EngageTarget(WorldSession& session, const ObjectGuid& target_guid,
                    bool suppress_range_error = false);

  [[nodiscard]] std::uint32_t GetProfessionPoints() const;
  [[nodiscard]] std::uint32_t GetGlyphSlot(std::uint8_t slot) const;
  [[nodiscard]] std::uint32_t GetGlyph(std::uint8_t slot) const;
  [[nodiscard]] std::uint32_t GetGlyphsEnabled() const;

  [[nodiscard]] float GetRuneRegen(std::uint8_t rune) const;

  [[nodiscard]] bool HasExploredZone(std::uint32_t zone_index) const;

  [[nodiscard]] std::uint32_t GetTrackCreatures() const;
  [[nodiscard]] std::uint32_t GetTrackResources() const;

  [[nodiscard]] std::int32_t GetCombatRating(std::uint8_t rating) const;

  struct ArenaTeamInfo {
    std::uint32_t team_id = 0;
    std::uint32_t unknown_1 = 0;
    std::uint32_t captain_state = 0;
    std::uint32_t weekly_games_played = 0;
    std::uint32_t weekly_games_won = 0;
    std::uint32_t unknown_5 = 0;
    std::uint32_t personal_rating = 0;

    [[nodiscard]] bool IsLocalPlayerCaptain() const {
      return team_id != 0 && captain_state == 0;
    }
  };
  [[nodiscard]] ArenaTeamInfo GetArenaTeamInfo(std::uint8_t team_index) const;

  [[nodiscard]] bool HasTitle(std::uint32_t title_bit) const;
  [[nodiscard]] std::uint32_t GetChosenTitle() const;

  [[nodiscard]] std::uint32_t GetDailyQuestId(std::uint8_t slot) const;
  [[nodiscard]] std::uint8_t GetDailyQuestCount() const;

  [[nodiscard]] ObjectGuid GetFarsightTarget() const;
  void ActivateFarSightFocus(WorldSession& session,
                             const CGObject_C& focus_object);
  void ClearFarSightFocus(WorldSession& session);
  [[nodiscard]] ObjectGuid GetActiveFarSightFocusGuid() const {
    return far_sight_focus_guid_;
  }
  [[nodiscard]] bool HasActiveFarSightFocus() const {
    return far_sight_view_active_;
  }

  [[nodiscard]] std::uint32_t GetMaxLevel() const;

  [[nodiscard]] std::uint8_t GetSkinColor() const;
  [[nodiscard]] std::uint8_t GetFace() const;
  [[nodiscard]] std::uint8_t GetHairStyle() const;
  [[nodiscard]] std::uint8_t GetHairColor() const;
  [[nodiscard]] std::uint8_t GetFacialHair() const;
  [[nodiscard]] std::uint8_t GetBankBagSlotCount() const;
  [[nodiscard]] std::uint8_t GetRestState() const;
  [[nodiscard]] std::uint8_t GetActionBarToggles() const;
  [[nodiscard]] std::uint8_t GetDrunkState() const;
  [[nodiscard]] std::uint8_t GetGenderFromBytes() const;

  [[nodiscard]] std::uint8_t GetPvpMedalRank() const;

  [[nodiscard]] std::uint32_t GetExpertise() const;
  [[nodiscard]] std::uint32_t GetOffhandExpertise() const;

  [[nodiscard]] bool HasActiveInebriation() const;
  [[nodiscard]] float GetNormalizedInebriation() const;
  [[nodiscard]] std::uint32_t GetFakeInebriation() const;

  [[nodiscard]] std::uint32_t GetPetSpellPower() const;

  [[nodiscard]] std::int32_t GetModDamageDonePositive(std::uint8_t school) const;
  [[nodiscard]] std::int32_t GetModDamageDoneNegative(std::uint8_t school) const;
  [[nodiscard]] float GetModDamageDonePercent(std::uint8_t school) const;
  [[nodiscard]] std::int32_t GetModHealingDonePositive() const;

  [[nodiscard]] bool HasKnownCurrency(std::uint32_t bit_index) const;

  [[nodiscard]] bool CanInteractFromVehicleSeat() const;

  [[nodiscard]] bool IsNearGossipNpc() const;
  [[nodiscard]] bool IsNearGossipNpc(const ObjectManager &object_manager) const;

  [[nodiscard]] bool IsNearTalentMasterNpc() const;
  [[nodiscard]] bool IsNearTalentMasterNpc(const ObjectManager &object_manager) const;

  [[nodiscard]] bool IsNearBinderNpc() const;
  [[nodiscard]] bool IsNearBinderNpc(const ObjectManager &object_manager) const;

  static void SetGossipNpcGuid(std::uint64_t guid);
  [[nodiscard]] static std::uint64_t GetGossipNpcGuid();
  static void SetTalentMasterNpcGuid(std::uint64_t guid);
  [[nodiscard]] static std::uint64_t GetTalentMasterNpcGuid();
  static void SetBinderNpcGuid(std::uint64_t guid);
  [[nodiscard]] static std::uint64_t GetBinderNpcGuid();

  void InteractWithBinder(const ObjectGuid &binder_guid);

  [[nodiscard]] std::uint8_t FindContainerSlotByGuid(const ObjectGuid &guid) const;

  void RefreshCharacterModelAndQueuePortraitEvents();

  void ProcessVisualInitGate(std::uint8_t construct_flags,
                             std::uint32_t *out_error_count,
                             std::uint32_t *out_pending);

  [[nodiscard]] std::int32_t GetSpellScalingLevelX5(
      const data::dbc::SpellEntry *spell) const;

  [[nodiscard]] std::int32_t CalcSpellDuration(
      const WorldSession& session,
      const data::dbc::SpellEntry *spell) const;

  [[nodiscard]] std::int32_t CalcRawCastDuration(
      const data::dbc::SpellEntry *spell) const;

  void GetXPRange(std::uint32_t *out_xp, std::uint32_t *out_zero) const;

  void GetXPRangeForLevel(std::int32_t level, std::uint32_t *out_xp, std::uint32_t *out_zero) const;

  void PrepareForWorldRemoval() override;

  void DestroyPlayer(WorldSession& session);

  void CleanupPlayer();

  void CleanupActivePlayerState();

  void CreateWeaponSpellVisualEffects(
      const WorldSession& session,
      std::uint32_t spell_id,
      const data::dbc::SpellVisualKitEntry& kit,
      const float* position,
      std::uint64_t source_guid,
      std::uint32_t group_param,
      std::uint32_t& dispatch_flags,
      std::uintptr_t callback,
      bool& out_created,
      bool has_aura_visual_flag) override;

  void OnDestroyEffectNode(const WorldSession& session,
                           const UnitSpellVisualRuntime::AttachedEffectNode& node) override;

  [[nodiscard]] const data::dbc::SpellVisualEntry* ResolveSpellVisualRecord(
      const data::dbc::SpellEntry& spell,
      data::dbc::SpellVisualEntry& out,
      std::uint32_t kit_visual_id,
      std::uint32_t kit_visual_id_fallback) const override;

  void ResetMatchingSpellVisualNodes(
      const WorldSession& session,
      std::uint32_t spell_id,
      std::uint32_t visual_kit_param) override;

  void SetIdleAnimation() override;

  void PlayerUpdateTick(float elapsed);

  void UpdateModelTintColor(std::uint32_t now_ms);

  [[nodiscard]] ModelTintColor GetModelTintColor() const override;

  float GetPlayerAnimationProgress() const;

  void ReapplyAllAuraSpellVisuals(const WorldSession& session);

private:
  std::array<EquipmentSlotInfo, kMaxEquipSlots> equipment_{};
  std::uint8_t combo_points_{0};
  std::uint32_t watched_faction_index_{0xFFFFFFFF};

  std::uint8_t cached_stand_state_{0};

  std::uint32_t visual_model_state_{0};

  std::uint32_t internal_flags_{0};

  float walk_anim_speed_{1.0f};

  std::string display_name_override_;

  std::vector<PendingItemEnchantTimeUpdate> pending_item_enchant_time_updates_;
  ObjectGuid far_sight_focus_guid_{};
  bool far_sight_view_active_{false};
  static std::uint64_t s_gossip_npc_guid_;
  static std::uint64_t s_talent_master_npc_guid_;
  static std::uint64_t s_binder_npc_guid_;

  void RunPostCreateInitialization();

  std::vector<PendingAuraVisualEntry> pending_aura_visual_entries_;

  std::uint32_t last_item_visual_attachment_id_{0};
  std::uint32_t last_item_visual_aura_id_{0};
  bool last_item_visual_dirty_{false};

  std::int32_t predicted_money_{-1};
  std::uint32_t predicted_xp_current_{0};
  std::uint32_t predicted_xp_max_{0};

  EquippedGemColorCounts equipped_gem_color_counts_{};
};

}

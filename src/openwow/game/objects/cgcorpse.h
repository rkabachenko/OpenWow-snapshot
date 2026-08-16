#pragma once

#include "openwow/game/objects/cgobject.h"
#include "openwow/game/guild_manager.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class WorldSession;

inline constexpr std::uint32_t kCorpseFlagNone      = 0;
inline constexpr std::uint32_t kCorpseFlagBones      = 1;
inline constexpr std::uint32_t kCorpseFlagUnk1       = 2;
inline constexpr std::uint32_t kCorpseFlagPvP        = 4;
inline constexpr std::uint32_t kCorpseFlagHideHelm   = 8;
inline constexpr std::uint32_t kCorpseFlagHideCloak  = 16;

inline constexpr std::uint32_t kCorpseDynFlagLootable = 1;

inline constexpr std::uint16_t kCorpseAnimUndergroundDead = 0x84;
inline constexpr std::uint16_t kCorpseAnimStandingDead   = 6;

inline constexpr std::uint32_t kCorpseRenderFlagUnderground = 0x2u;

inline constexpr std::size_t kCorpseEquipmentSlotCount = 19u;
inline constexpr std::uint8_t kCorpseEquipmentSlotHead = 0u;
inline constexpr std::uint8_t kCorpseEquipmentSlotBack = 14u;
inline constexpr std::uint8_t kCorpseEquipmentSlotMainHand = 15u;
inline constexpr std::uint8_t kCorpseEquipmentSlotOffHand = 16u;
inline constexpr std::uint8_t kCorpseEquipmentSlotRanged = 17u;
inline constexpr std::uint8_t kCorpseEquipmentSlotTabard = 18u;
inline constexpr std::uint32_t kCorpseItemDisplayIdMask = 0x00FFFFFFu;
inline constexpr std::uint32_t kCorpseItemInventoryTypeShift = 24u;
inline constexpr std::uint32_t kCorpseTabardEmblemItemDisplayFlag = 0x1u;
inline constexpr std::uint32_t kCorpseCreatureModelDataCharacterFlag = 0x4u;
inline constexpr std::uint32_t kCorpseLootSparkleAttachmentId = 19u;

enum class CorpseVisualModelKind : std::uint8_t {
  kNone = 0,
  kCharacter,
  kCreatureTextureReplacement,
  kSkeleton,
};

struct CorpseEquipmentVisual {
  std::uint8_t slot{kCorpseEquipmentSlotHead};
  std::uint32_t item_display_id{0};
  std::uint8_t inventory_type{0};
  bool weapon_model{false};
  bool updates_guild_tabard{false};
};

struct CorpseVisualState {
  CorpseVisualModelKind model_kind{CorpseVisualModelKind::kNone};
  std::string model_path;
  bool active_player_corpse{false};
  std::array<CorpseEquipmentVisual, kCorpseEquipmentSlotCount> equipment{};
  std::size_t equipment_count{0};
  bool loot_sparkle_requested{false};
  bool loot_sparkle_active{false};
  std::string loot_sparkle_effect_path;
  std::optional<GuildEmblem> guild_tabard_emblem;
  std::uint32_t render_flags{0};
  std::uint16_t death_animation_id{kCorpseAnimStandingDead};
  std::uint32_t sync_serial{0};
};

class CGCorpse_C : public CGObject_C {
 public:
  CGCorpse_C();
  explicit CGCorpse_C(ObjectGuid guid);
  explicit CGCorpse_C(ObjectManager& objects);
  CGCorpse_C(ObjectManager& objects, ObjectGuid guid);
  ~CGCorpse_C() override = default;

  CGCorpse_C(CGCorpse_C&&) noexcept = default;
  CGCorpse_C& operator=(CGCorpse_C&&) noexcept = default;

  std::vector<std::uint16_t> ApplyCreateUpdate(
      const CreateObjectUpdate& upd) override;
  void FinalizeCreateUpdate(const CreateObjectUpdate& upd) override;
  void FinalizeWorldPublication() override;

  void OnCreate();

  void UpdateGuildTabard(WorldSession& session, bool request_async);

  [[nodiscard]] ObjectGuid GetOwner() const;
  [[nodiscard]] ObjectGuid GetParty() const;

  [[nodiscard]] std::uint32_t GetDisplayId() const override;
  [[nodiscard]] std::uint32_t GetCorpseDisplayID() const;

  [[nodiscard]] std::uint32_t GetFactionTemplate() const override;

  [[nodiscard]] const char* GetPortraitTextureName() const override;

  [[nodiscard]] std::uint32_t GetItemDisplay(std::uint8_t slot) const;
  [[nodiscard]] std::uint32_t GetItemEntry(std::uint8_t slot) const;

  [[nodiscard]] std::uint8_t GetRace() const;
  [[nodiscard]] std::uint8_t GetGender() const;
  [[nodiscard]] std::uint8_t GetSkinColor() const;
  [[nodiscard]] std::uint8_t GetFace() const;

  [[nodiscard]] std::uint8_t GetHairStyle() const;
  [[nodiscard]] std::uint8_t GetHairColor() const;
  [[nodiscard]] std::uint8_t GetFacialHair() const;

  [[nodiscard]] std::uint32_t GetGuildId() const;
  [[nodiscard]] std::uint32_t GetGuildID() const;

  [[nodiscard]] std::uint32_t GetCorpseFlags() const;

  [[nodiscard]] std::uint32_t GetDynamicFlags() const;

  [[nodiscard]] const CorpseVisualState& GetCorpseVisualState() const {
    return visual_state_;
  }

  [[nodiscard]] bool HasCharacterModelVisual() const {
    return visual_state_.model_kind == CorpseVisualModelKind::kCharacter;
  }

  [[nodiscard]] const std::string& GetCorpseModelPath() const {
    return visual_state_.model_path;
  }

  [[nodiscard]] bool HasLootSparkleVisual() const {
    return visual_state_.loot_sparkle_active;
  }

  [[nodiscard]] std::uint32_t GetCorpseRenderFlags() const {
    return visual_state_.render_flags;
  }

  [[nodiscard]] std::uint16_t GetCorpseDeathAnimId() const {
    return visual_state_.death_animation_id;
  }

  [[nodiscard]] const std::string& GetLootSparkleEffectPath() const {
    return visual_state_.loot_sparkle_effect_path;
  }

  [[nodiscard]] bool IsItemVisible() const override;

  void QueryModelRebuildFlags(std::uint8_t flags,
                              std::uint32_t& out_needs_construct,
                              std::uint32_t& out_needs_refresh) override;

  static void RegisterFieldHandlers();

  void OnFlagsChanged(std::uint32_t new_flags);

  void OnDynamicFlagsChanged(std::uint8_t new_dynamic_flags);

 private:
  void ResetVisualState();
  void RebuildVisualState(std::uint32_t corpse_flags,
                          const openwow::data::dbc::DbcLoader& dbc);
  void PopulateEquipmentVisuals(std::uint32_t corpse_flags,
                                const openwow::data::dbc::DbcLoader& dbc);
  void SetLootSparkleVisualFromDbc(const openwow::data::dbc::DbcLoader& dbc,
                                   bool active);

  CorpseVisualState visual_state_{};
};

using CorpseSupportSurfaceHeightCallback =
    std::optional<float> (*)(const CGCorpse_C& corpse, void* context);

using CorpseCharacterAppearanceReadyCallback =
    bool (*)(const CGCorpse_C& corpse, void* context);

void SetCorpseSupportSurfaceHeightCallback(
    CorpseSupportSurfaceHeightCallback callback, void* context);
void ClearCorpseSupportSurfaceHeightCallback();
void SetCorpseCharacterAppearanceReadyCallback(
    CorpseCharacterAppearanceReadyCallback callback, void* context);
void ClearCorpseCharacterAppearanceReadyCallback();

}

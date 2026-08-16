
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game {

enum class ArmorRegion : uint8_t {
  ArmUpper = 0,
  ArmLower = 1,
  Hand = 2,
  TorsoUpper = 3,
  TorsoLower = 4,
  LegUpper = 5,
  LegLower = 6,
  Foot = 7,
  Count = 8
};

enum class AttachmentPoint : uint8_t {
  Head = 0,
  ShoulderLeft = 1,
  ShoulderRight = 2,
  HandRight = 3,
  HandLeft = 4,
  ShieldBack = 5,
  WeaponBackRight = 6,
  WeaponBackLeft = 7,
  QuiverBack = 8,
  Cape = 9,
  Count = 10
};

enum class EquipmentSlot : uint8_t {
  Head = 0,
  Neck = 1,
  Shoulder = 2,
  Shirt = 3,
  Chest = 4,
  Waist = 5,
  Legs = 6,
  Feet = 7,
  Wrist = 8,
  Hands = 9,
  Ring1 = 10,
  Ring2 = 11,
  Trinket1 = 12,
  Trinket2 = 13,
  Back = 14,
  MainHand = 15,
  OffHand = 16,
  Ranged = 17,
  Tabard = 18,
  SlotCount = 19
};

namespace GeosetGroup {
inline constexpr uint32_t Hair = 0;
inline constexpr uint32_t FacialHair1 = 100;
inline constexpr uint32_t FacialHair2 = 200;
inline constexpr uint32_t FacialHair3 = 300;
inline constexpr uint32_t Boots = 500;
inline constexpr uint32_t Shirt = 800;
inline constexpr uint32_t Legs = 1100;
inline constexpr uint32_t Tabard = 1200;
inline constexpr uint32_t Robe = 1300;
inline constexpr uint32_t Cape = 1500;
inline constexpr uint32_t Gloves = 400;
inline constexpr uint32_t Belt = 1800;
inline constexpr uint32_t Wrist = 900;
}

struct ItemDisplayInfoEntry {
  uint32_t id = 0;
  std::string modelName[2];
  std::string modelTexture[2];
  uint32_t geosetGroup[3] = {};
  uint32_t flags = 0;
  uint32_t spellVisualId = 0;
  uint32_t helmetGeosetVis[2] = {};
  std::string componentTexture[8];
  uint32_t itemVisualId = 0;
  uint32_t particleColorId = 0;
};

struct EquipGeosetChange {
  uint32_t groupId = 0;
  uint32_t geosetId = 0;
  bool show = true;
};

struct EquipTextureChange {
  ArmorRegion region = ArmorRegion::ArmUpper;
  std::string texturePath;
};

struct EquipParticleColorRecord {
  std::array<uint32_t, 3> start{};
  std::array<uint32_t, 3> mid{};
  std::array<uint32_t, 3> end{};
};

struct ItemVisualAttachmentEntry {
  uint32_t id = 0;
  std::array<std::string, 5> modelPaths{};
};

struct ParticleColorEntry {
  uint32_t id = 0;
  EquipParticleColorRecord record;
};

struct EquipItemVisualAttachment {
  uint32_t attachmentId = 0;
  std::string modelPath;
};

struct WeaponAuraVisualBinding {
  uint32_t attachmentId = 0;
  uint32_t auraVisualId = 0;
};

struct CharacterModelPathContext {
  std::string_view race_model_token;
  uint8_t gender = 2;
};

struct StoredItemDispatchResult {
  int32_t targetAttachmentId = -1;
  uint32_t primaryAttachmentId = 0;
  uint32_t alternateAttachmentId = 0;
  AttachmentPoint targetPoint = AttachmentPoint::Head;
  std::string modelPath;
  std::string texturePath;
};

struct EquippedItemVisual {
  EquipmentSlot slot = EquipmentSlot::Head;
  uint32_t displayId = 0;
  uint8_t inventoryType = 0;
  uint8_t sheatheType = 0;
};

struct EquipModelAttachment {
  AttachmentPoint point = AttachmentPoint::Head;
  uint32_t attachmentId = 0;
  std::string modelPath;
  std::string texturePath;
  uint32_t replaceableTextureType = 2;
  std::optional<uint32_t> initialAnimationId;
  std::vector<EquipItemVisualAttachment> itemVisualAttachments;
  EquipParticleColorRecord particleColors;
};

enum ItemDisplayFlags : uint32_t {
  ITEM_DISPLAY_FLAG_NONE = 0x0000,
  ITEM_DISPLAY_FLAG_ROBE = 0x0001,
  ITEM_DISPLAY_FLAG_HIDE_PANTS = 0x0002,
  ITEM_DISPLAY_FLAG_HIDE_BOOTS = 0x0004,
  ITEM_DISPLAY_FLAG_HIDE_HELM = 0x0008,
  ITEM_DISPLAY_FLAG_HIDE_CLOAK = 0x0010,
  ITEM_DISPLAY_FLAG_HIDE_SHIRT = 0x0020,
  ITEM_DISPLAY_FLAG_HIDE_TABARD = 0x0040,
};

class EquipmentVisualSystem {
public:
  EquipmentVisualSystem() = default;

  void AddDisplayInfo(const ItemDisplayInfoEntry &entry);
  void ClearDisplayInfoStore();

  [[nodiscard]] std::optional<ItemDisplayInfoEntry> GetDisplayInfo(uint32_t displayId) const;

  [[nodiscard]] size_t GetDisplayInfoCount() const;

  void AddItemVisualEntry(const ItemVisualAttachmentEntry &entry);
  void ClearItemVisualStore();

  [[nodiscard]] std::optional<ItemVisualAttachmentEntry>
  GetItemVisualEntry(uint32_t itemVisualId) const;

  void AddParticleColorEntry(const ParticleColorEntry &entry);
  void ClearParticleColorStore();

  [[nodiscard]] std::optional<ParticleColorEntry>
  GetParticleColorEntry(uint32_t particleColorId) const;

  [[nodiscard]] static std::vector<ArmorRegion> GetCoveredRegions(EquipmentSlot slot);

  [[nodiscard]] static uint32_t GetGeosetGroupForSlot(EquipmentSlot slot);

  [[nodiscard]] static std::optional<AttachmentPoint> GetAttachmentPoint(EquipmentSlot slot,
                                                                         bool sheathed = false);

  [[nodiscard]] static int ResolveSheathedWeaponAttachmentId(uint8_t sheatheType,
                                                             bool usePrimarySide);

  [[nodiscard]] static StoredItemDispatchResult
  DispatchStoredItemModel(uint32_t inventorySlot, uint8_t sheatheType, bool useAlternateSlot,
                          bool isShield, bool rangedUsesMainTable, std::string_view modelName,
                          std::string_view textureStem);

  [[nodiscard]] static bool UsePrimarySheathedWeaponSide(EquipmentSlot slot, uint8_t inventoryType);

  [[nodiscard]] static std::vector<WeaponAuraVisualBinding>
  ResolveWeaponAuraVisualBindings(const EquippedItemVisual &item, uint32_t auraVisualId,
                                  bool sheathed);

  [[nodiscard]] static bool SlotHasModel(EquipmentSlot slot);

  [[nodiscard]] std::vector<EquipGeosetChange> ResolveGeosets(EquipmentSlot slot,
                                                              uint32_t displayId) const;

  [[nodiscard]] std::vector<EquipTextureChange> ResolveTextures(EquipmentSlot slot,
                                                                uint32_t displayId) const;

  [[nodiscard]] EquipModelAttachment
  BuildAttachmentModel(AttachmentPoint point, uint32_t attachmentId, std::string modelPath,
                       std::string texturePath, uint32_t itemVisualId = 0,
                       uint32_t particleColorId = 0) const;

  [[nodiscard]] std::optional<EquipModelAttachment>
  BuildObjectComponentModel(uint32_t attachmentType, std::string_view modelName,
                            std::string_view textureName, uint32_t itemVisualId = 0) const;

  [[nodiscard]] static std::string BuildAmmoModelPath(std::string_view modelName);

  [[nodiscard]] static std::string BuildAmmoTexturePath(std::string_view textureName);

  [[nodiscard]] static std::string BuildShoulderModelPath(std::string_view modelName);

  [[nodiscard]] static std::string BuildShoulderTexturePath(std::string_view textureStem);

  [[nodiscard]] static std::string BuildHeadModelPath(std::string_view itemModelName,
                                                      std::string_view raceModelToken,
                                                      uint8_t gender);

  [[nodiscard]] static std::string NormalizeModelPathToM2(std::string_view path);

  [[nodiscard]] static bool
  IsAttachmentModelCurrent(const std::vector<EquipModelAttachment> &loadedAttachments,
                           uint32_t attachmentId, std::string_view expectedModelPath);

  [[nodiscard]] std::vector<EquipModelAttachment>
  ResolveModelAttachments(EquipmentSlot slot, uint32_t displayId, bool sheathed = false) const;

  [[nodiscard]] std::vector<EquipModelAttachment>
  ResolveModelAttachments(const EquippedItemVisual &item, bool sheathed = false) const;

  [[nodiscard]] std::vector<EquipModelAttachment>
  ResolveModelAttachments(EquipmentSlot slot, uint32_t displayId,
                          const CharacterModelPathContext &pathContext,
                          bool sheathed = false) const;

  [[nodiscard]] std::vector<EquipModelAttachment>
  ResolveModelAttachments(const EquippedItemVisual &item,
                          const CharacterModelPathContext &pathContext,
                          bool sheathed = false) const;

  [[nodiscard]] std::optional<EquipModelAttachment>
  ResolveModel(EquipmentSlot slot, uint32_t displayId, bool sheathed = false) const;

  [[nodiscard]] std::optional<EquipModelAttachment> ResolveModel(const EquippedItemVisual &item,
                                                                 bool sheathed = false) const;

  [[nodiscard]] std::optional<EquipModelAttachment>
  ResolveModel(EquipmentSlot slot, uint32_t displayId, const CharacterModelPathContext &pathContext,
               bool sheathed = false) const;

  [[nodiscard]] std::optional<EquipModelAttachment>
  ResolveModel(const EquippedItemVisual &item, const CharacterModelPathContext &pathContext,
               bool sheathed = false) const;

  [[nodiscard]] std::optional<std::string> ResolveCapeTexturePath(uint32_t displayId) const;

  [[nodiscard]] bool IsRobeDisplay(uint32_t displayId) const;

  [[nodiscard]] std::vector<EquipGeosetChange> GetRobeGeosetOverrides() const;

  [[nodiscard]] std::vector<EquipGeosetChange> GetHelmGeosetOverrides(uint32_t displayId) const;

  struct EquipmentVisuals {
    std::vector<EquipGeosetChange> geosetChanges;
    std::vector<EquipTextureChange> textureChanges;
    std::vector<EquipModelAttachment> modelAttachments;
  };

  [[nodiscard]] EquipmentVisuals
  ResolveFullEquipment(const std::vector<EquippedItemVisual> &equipment,
                       bool sheathedWeapons = false) const;

  [[nodiscard]] EquipmentVisuals
  ResolveFullEquipment(const std::vector<EquippedItemVisual> &equipment,
                       const CharacterModelPathContext &pathContext,
                       bool sheathedWeapons = false) const;

private:
  std::vector<ItemDisplayInfoEntry> displayInfoStore_;
  std::vector<ItemVisualAttachmentEntry> itemVisualStore_;
  std::vector<ParticleColorEntry> particleColorStore_;
};

[[nodiscard]] const char *ArmorRegionName(ArmorRegion region);
[[nodiscard]] const char *AttachmentPointName(AttachmentPoint point);
[[nodiscard]] const char *EquipmentSlotName(EquipmentSlot slot);

}

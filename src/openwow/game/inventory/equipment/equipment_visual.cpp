
#include "openwow/game/inventory/equipment/equipment_visual.h"
#include "openwow/game/inventory/items/item_definitions.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace openwow::game {

namespace {

constexpr std::uint32_t kDefaultParticleColorRawGreen = 0xFF00FF00u;

EquipParticleColorRecord BuildDefaultParticleColorRecord() {
  EquipParticleColorRecord record;
  record.start.fill(kDefaultParticleColorRawGreen);
  record.mid.fill(kDefaultParticleColorRawGreen);
  record.end.fill(kDefaultParticleColorRawGreen);
  return record;
}

constexpr std::string_view kShoulderModelPathPrefix = "Item\\ObjectComponents\\Shoulder\\";
constexpr std::string_view kHeadModelPathPrefix = "Item\\ObjectComponents\\Head\\";
constexpr std::string_view kQuiverModelPathPrefix = "Item\\ObjectComponents\\Quiver\\";
constexpr std::string_view kAmmoModelPathPrefix = "Item\\ObjectComponents\\Ammo\\";
constexpr std::string_view kWeaponModelPathPrefix = "Item\\ObjectComponents\\Weapon\\";
constexpr std::string_view kShieldModelPathPrefix = "Item\\ObjectComponents\\Shield\\";
constexpr std::array<std::string_view, 3> kHeadModelGenderSuffixes = {"M", "F", "U"};

[[nodiscard]] std::string_view ResolveHeadModelGenderSuffix(const uint8_t gender) {
  const auto suffix_index = std::min<std::size_t>(gender, kHeadModelGenderSuffixes.size() - 1);
  return kHeadModelGenderSuffixes[suffix_index];
}

[[nodiscard]] std::string BuildQuiverModelPath(const std::string_view modelName) {
  if (modelName.empty()) {
    return {};
  }

  std::string path(kQuiverModelPathPrefix);
  path.append(modelName);
  return path;
}

[[nodiscard]] std::string BuildQuiverTexturePath(const std::string_view textureStem) {
  std::string path(kQuiverModelPathPrefix);
  path.append(textureStem);
  path.append(".blp");
  return path;
}

[[nodiscard]] bool IsShieldInventoryType(const uint8_t inventoryType) {
  return inventoryType == static_cast<uint8_t>(InventoryType::Shield);
}

[[nodiscard]] AttachmentPoint AttachmentPointFromStoredItemId(const uint32_t attachmentId) {
  switch (attachmentId) {
  case 0:
    return AttachmentPoint::HandLeft;
  case 1:
    return AttachmentPoint::HandRight;
  case 2:
    return AttachmentPoint::HandLeft;
  case 26:
  case 30:
  case 32:
    return AttachmentPoint::WeaponBackRight;
  case 27:
  case 31:
  case 33:
    return AttachmentPoint::WeaponBackLeft;
  case 28:
    return AttachmentPoint::ShieldBack;
  default:
    return AttachmentPoint::Head;
  }
}

}

int EquipmentVisualSystem::ResolveSheathedWeaponAttachmentId(const uint8_t sheatheType,
                                                             const bool usePrimarySide) {
  switch (sheatheType) {
  case 1:
    return usePrimarySide ? 26 : 27;
  case 2:
    return usePrimarySide ? 30 : 31;
  case 3:
    return usePrimarySide ? 32 : 33;
  case 4:
    return 28;
  default:
    return -1;
  }
}

StoredItemDispatchResult EquipmentVisualSystem::DispatchStoredItemModel(
    const uint32_t inventorySlot, const uint8_t sheatheType, const bool useAlternateSlot,
    const bool isShield, const bool rangedUsesMainTable, const std::string_view modelName,
    const std::string_view textureStem) {

  StoredItemDispatchResult result;

  if (inventorySlot > 18) {
    return result;
  }

  uint32_t primary = 0;
  int32_t alternate = -1;
  bool shieldPrefix = false;

  auto resolveAlternateFromSheatheType = [&](bool usePrimarySide) {
    switch (sheatheType) {
    case 1:
      alternate = usePrimarySide ? 26 : 27;
      break;
    case 2:
      alternate = usePrimarySide ? 30 : 31;
      break;
    case 3:
      alternate = usePrimarySide ? 32 : 33;
      break;
    case 4:
      alternate = 28;
      break;
    default:
      alternate = -1;
      break;
    }
  };

  if (inventorySlot == 15) {

    primary = 1;
    resolveAlternateFromSheatheType(true);
  } else if (inventorySlot == 16) {

    primary = 2;
    resolveAlternateFromSheatheType(false);
    if (isShield) {
      shieldPrefix = true;
      primary = 0;
    }
  } else if (inventorySlot == 17) {
    if (rangedUsesMainTable) {
      primary = 1;
      resolveAlternateFromSheatheType(true);
    } else {
      primary = 2;
      resolveAlternateFromSheatheType(false);
    }
  } else {
    return result;
  }

  result.primaryAttachmentId = primary;
  result.alternateAttachmentId =
      (alternate >= 0) ? static_cast<uint32_t>(alternate) : static_cast<uint32_t>(-1);

  if (useAlternateSlot) {
    if (alternate < 0) {
      return result;
    }
    result.targetAttachmentId = alternate;
  } else {
    result.targetAttachmentId = static_cast<int32_t>(primary);
  }

  result.targetPoint =
      AttachmentPointFromStoredItemId(static_cast<uint32_t>(result.targetAttachmentId));

  const auto &prefix = shieldPrefix ? kShieldModelPathPrefix : kWeaponModelPathPrefix;
  if (!modelName.empty()) {
    result.modelPath.reserve(prefix.size() + modelName.size());
    result.modelPath.append(prefix);
    result.modelPath.append(modelName);
  }

  result.texturePath.reserve(prefix.size() + textureStem.size() + 4);
  result.texturePath.append(prefix);
  result.texturePath.append(textureStem);
  result.texturePath.append(".blp");

  return result;
}

bool EquipmentVisualSystem::UsePrimarySheathedWeaponSide(const EquipmentSlot slot,
                                                         const uint8_t inventoryType) {
  switch (slot) {
  case EquipmentSlot::MainHand:
    return true;
  case EquipmentSlot::OffHand:
    return false;
  case EquipmentSlot::Ranged:
    return inventoryType == 25u || inventoryType == 26u;
  default:
    return false;
  }
}

std::vector<WeaponAuraVisualBinding> EquipmentVisualSystem::ResolveWeaponAuraVisualBindings(
    const EquippedItemVisual &item, const uint32_t auraVisualId, const bool sheathed) {
  std::vector<WeaponAuraVisualBinding> bindings;

  const auto pushBinding = [&](const int attachmentId) {
    if (attachmentId < 0) {
      return;
    }

    bindings.push_back({static_cast<uint32_t>(attachmentId), auraVisualId});
  };

  switch (item.slot) {
  case EquipmentSlot::MainHand: {
    const int sheathedAttachmentId = ResolveSheathedWeaponAttachmentId(item.sheatheType, true);
    if (sheathed) {
      pushBinding(sheathedAttachmentId);
      pushBinding(1);
    } else {
      pushBinding(1);
      pushBinding(sheathedAttachmentId);
    }
    break;
  }
  case EquipmentSlot::OffHand:
    if (IsShieldInventoryType(item.inventoryType)) {
      pushBinding(0);
      break;
    }

    if (sheathed) {
      pushBinding(ResolveSheathedWeaponAttachmentId(item.sheatheType, false));
      pushBinding(2);
      break;
    }

    pushBinding(2);
    pushBinding(ResolveSheathedWeaponAttachmentId(item.sheatheType, false));
    break;
  default:
    break;
  }

  return bindings;
}

bool EquipmentVisualSystem::IsRobeDisplay(uint32_t displayId) const {
  auto info = GetDisplayInfo(displayId);
  if (!info)
    return false;
  return (info->flags & ITEM_DISPLAY_FLAG_ROBE) != 0;
}

std::vector<EquipGeosetChange> EquipmentVisualSystem::ResolveGeosets(EquipmentSlot slot,
                                                                     uint32_t displayId) const {
  std::vector<EquipGeosetChange> changes;
  auto info = GetDisplayInfo(displayId);

  if (info && info->geosetGroup[0] != 0) {
    changes.push_back({info->geosetGroup[0], info->geosetGroup[1], true});
  } else {

    uint32_t group = GetGeosetGroupForSlot(slot);
    if (group != 0) {
      changes.push_back({group, 1, true});
    }
  }

  return changes;
}

std::vector<EquipTextureChange> EquipmentVisualSystem::ResolveTextures(EquipmentSlot slot,
                                                                       uint32_t displayId) const {
  (void)slot;
  std::vector<EquipTextureChange> changes;
  auto info = GetDisplayInfo(displayId);

  if (info) {

    for (uint8_t i = 0; i < static_cast<uint8_t>(ArmorRegion::Count); ++i) {
      if (!info->componentTexture[i].empty()) {
        changes.push_back({static_cast<ArmorRegion>(i), info->componentTexture[i]});
      }
    }
  }

  return changes;
}

EquipModelAttachment
EquipmentVisualSystem::BuildAttachmentModel(AttachmentPoint point, uint32_t attachmentId,
                                            std::string modelPath, std::string texturePath,
                                            uint32_t itemVisualId, uint32_t particleColorId) const {
  EquipModelAttachment attachment;
  attachment.point = point;
  attachment.attachmentId = attachmentId;
  attachment.modelPath = std::move(modelPath);
  attachment.texturePath = std::move(texturePath);
  attachment.particleColors = BuildDefaultParticleColorRecord();

  if (attachmentId == 1) {
    attachment.initialAnimationId = 0;
  } else if (attachmentId == 2) {
    attachment.initialAnimationId = 1;
  }

  if (const auto itemVisual = GetItemVisualEntry(itemVisualId)) {
    for (std::size_t index = 0; index < itemVisual->modelPaths.size(); ++index) {
      if (!itemVisual->modelPaths[index].empty()) {
        attachment.itemVisualAttachments.push_back(
            EquipItemVisualAttachment{static_cast<uint32_t>(index), itemVisual->modelPaths[index]});
      }
    }
  }

  if (const auto particleColors = GetParticleColorEntry(particleColorId)) {
    attachment.particleColors = particleColors->record;
  }

  return attachment;
}

inline constexpr uint32_t kAttachmentTypeAmmo = 24;
inline constexpr uint32_t kAttachmentTypeWeapon = 25;

std::optional<EquipModelAttachment> EquipmentVisualSystem::BuildObjectComponentModel(
    const uint32_t attachmentType, const std::string_view modelName,
    const std::string_view textureName, const uint32_t itemVisualId) const {

  if (modelName.empty() || textureName.empty()) {
    return std::nullopt;
  }
  if (attachmentType != kAttachmentTypeAmmo && attachmentType != kAttachmentTypeWeapon) {
    return std::nullopt;
  }

  const auto &prefix =
      (attachmentType == kAttachmentTypeAmmo) ? kAmmoModelPathPrefix : kWeaponModelPathPrefix;

  std::string modelPath(prefix);
  modelPath.append(modelName);

  std::string texturePath(prefix);
  texturePath.append(textureName);

  EquipModelAttachment attachment;
  attachment.attachmentId = attachmentType;
  attachment.modelPath = std::move(modelPath);
  attachment.texturePath = std::move(texturePath);
  attachment.initialAnimationId = std::nullopt;

  if (const auto itemVisual = GetItemVisualEntry(itemVisualId)) {
    for (std::size_t index = 0; index < itemVisual->modelPaths.size(); ++index) {
      if (!itemVisual->modelPaths[index].empty()) {
        attachment.itemVisualAttachments.push_back(
            EquipItemVisualAttachment{static_cast<uint32_t>(index), itemVisual->modelPaths[index]});
      }
    }
  }

  return attachment;
}

std::string EquipmentVisualSystem::BuildAmmoModelPath(const std::string_view modelName) {
  if (modelName.empty()) {
    return {};
  }
  std::string path(kAmmoModelPathPrefix);
  path.append(modelName);
  return path;
}

std::string EquipmentVisualSystem::BuildAmmoTexturePath(const std::string_view textureName) {
  if (textureName.empty()) {
    return {};
  }
  std::string path(kAmmoModelPathPrefix);
  path.append(textureName);
  return path;
}

std::string EquipmentVisualSystem::BuildShoulderModelPath(const std::string_view modelName) {
  if (modelName.empty()) {
    return {};
  }

  std::string path(kShoulderModelPathPrefix);
  path.append(modelName);
  return path;
}

std::string EquipmentVisualSystem::BuildShoulderTexturePath(const std::string_view textureStem) {
  std::string path(kShoulderModelPathPrefix);
  path.append(textureStem);
  path.append(".blp");
  return path;
}

std::string EquipmentVisualSystem::BuildHeadModelPath(const std::string_view itemModelName,
                                                      const std::string_view raceModelToken,
                                                      const uint8_t gender) {
  if (itemModelName.empty() || raceModelToken.empty()) {
    return {};
  }

  std::string path(kHeadModelPathPrefix);
  path.append(itemModelName);

  const auto extension = path.find_last_of('.');
  if (extension == std::string::npos) {
    path.push_back('_');
  } else {
    path[extension] = '_';
    path.resize(extension + 1);
  }

  path.append(raceModelToken);
  path.append(ResolveHeadModelGenderSuffix(gender));
  path.append(".mdx");
  return path;
}

std::string EquipmentVisualSystem::NormalizeModelPathToM2(const std::string_view path) {
  if (path.empty()) {
    return {};
  }

  std::string result(path);

  const auto lastSep = result.find_last_of("\\/");

  const auto lastDot = result.rfind('.');

  if (lastDot != std::string::npos && (lastSep == std::string::npos || lastDot > lastSep)) {
    result.resize(lastDot + 1);
    result.append("m2");
  }

  return result;
}

bool EquipmentVisualSystem::IsAttachmentModelCurrent(
    const std::vector<EquipModelAttachment> &loadedAttachments, const uint32_t attachmentId,
    const std::string_view expectedModelPath) {
  if (expectedModelPath.empty()) {
    return false;
  }

  for (const auto &att : loadedAttachments) {
    if (att.attachmentId != attachmentId) {
      continue;
    }

    const auto normalizedExpected = NormalizeModelPathToM2(expectedModelPath);
    const auto normalizedLoaded = NormalizeModelPathToM2(att.modelPath);

    if (normalizedExpected.size() != normalizedLoaded.size()) {
      return false;
    }

    for (std::size_t i = 0; i < normalizedExpected.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(normalizedExpected[i])) !=
          std::tolower(static_cast<unsigned char>(normalizedLoaded[i]))) {
        return false;
      }
    }

    return true;
  }

  return false;
}

std::vector<EquipModelAttachment>
EquipmentVisualSystem::ResolveModelAttachments(EquipmentSlot slot, uint32_t displayId,
                                               bool sheathed) const {
  EquippedItemVisual item;
  item.slot = slot;
  item.displayId = displayId;
  item.sheatheType = 1;
  if (slot == EquipmentSlot::Ranged) {
    item.inventoryType = 26;
  }

  return ResolveModelAttachments(item, sheathed);
}

std::vector<EquipModelAttachment>
EquipmentVisualSystem::ResolveModelAttachments(const EquippedItemVisual &item,
                                               bool sheathed) const {
  if (item.slot == EquipmentSlot::Head) {
    return {};
  }

  std::vector<EquipModelAttachment> attachments;
  auto info = GetDisplayInfo(item.displayId);
  if (!info || !SlotHasModel(item.slot)) {
    return attachments;
  }

  const auto addAttachment = [&](AttachmentPoint point, uint32_t attachmentId,
                                 const std::string &modelPath, const std::string &texturePath) {
    if (modelPath.empty()) {
      return;
    }

    attachments.push_back(BuildAttachmentModel(point, attachmentId, modelPath, texturePath,
                                               info->itemVisualId, info->particleColorId));
  };

  if (item.slot == EquipmentSlot::Ranged &&
      item.inventoryType == static_cast<uint8_t>(InventoryType::Quiver)) {
    addAttachment(AttachmentPoint::QuiverBack, 26, BuildQuiverModelPath(info->modelName[0]),
                  BuildQuiverTexturePath(info->modelTexture[0]));
    return attachments;
  }

  switch (item.slot) {
  case EquipmentSlot::Shoulder:
    addAttachment(AttachmentPoint::ShoulderLeft, 6, BuildShoulderModelPath(info->modelName[0]),
                  BuildShoulderTexturePath(info->modelTexture[0]));
    addAttachment(AttachmentPoint::ShoulderRight, 5, BuildShoulderModelPath(info->modelName[1]),
                  BuildShoulderTexturePath(info->modelTexture[1]));
    break;
  case EquipmentSlot::MainHand:
  case EquipmentSlot::OffHand:
  case EquipmentSlot::Ranged: {
    const auto dispatch =
        DispatchStoredItemModel(static_cast<uint32_t>(item.slot), item.sheatheType, sheathed,
                                IsShieldInventoryType(item.inventoryType),
                                UsePrimarySheathedWeaponSide(item.slot, item.inventoryType),
                                info->modelName[0], info->modelTexture[0]);
    if (dispatch.targetAttachmentId >= 0) {
      addAttachment(dispatch.targetPoint, static_cast<uint32_t>(dispatch.targetAttachmentId),
                    dispatch.modelPath, dispatch.texturePath);
    }
    break;
  }
  default:
    break;
  }

  return attachments;
}

std::vector<EquipModelAttachment>
EquipmentVisualSystem::ResolveModelAttachments(EquipmentSlot slot, uint32_t displayId,
                                               const CharacterModelPathContext &pathContext,
                                               bool sheathed) const {
  EquippedItemVisual item;
  item.slot = slot;
  item.displayId = displayId;
  item.sheatheType = 1;
  if (slot == EquipmentSlot::Ranged) {
    item.inventoryType = 26;
  }

  return ResolveModelAttachments(item, pathContext, sheathed);
}

std::vector<EquipModelAttachment>
EquipmentVisualSystem::ResolveModelAttachments(const EquippedItemVisual &item,
                                               const CharacterModelPathContext &pathContext,
                                               bool sheathed) const {
  if (item.slot != EquipmentSlot::Head) {
    return ResolveModelAttachments(item, sheathed);
  }

  std::vector<EquipModelAttachment> attachments;
  auto info = GetDisplayInfo(item.displayId);
  if (!info || !SlotHasModel(item.slot)) {
    return attachments;
  }

  const auto model_path =
      BuildHeadModelPath(info->modelName[0], pathContext.race_model_token, pathContext.gender);
  if (model_path.empty()) {
    return attachments;
  }

  attachments.push_back(BuildAttachmentModel(AttachmentPoint::Head, 11, model_path,
                                             info->modelTexture[0], info->itemVisualId,
                                             info->particleColorId));
  return attachments;
}

std::optional<EquipModelAttachment>
EquipmentVisualSystem::ResolveModel(EquipmentSlot slot, uint32_t displayId, bool sheathed) const {
  EquippedItemVisual item;
  item.slot = slot;
  item.displayId = displayId;
  item.sheatheType = 1;
  if (slot == EquipmentSlot::Ranged) {
    item.inventoryType = 26;
  }

  return ResolveModel(item, sheathed);
}

std::optional<EquipModelAttachment>
EquipmentVisualSystem::ResolveModel(const EquippedItemVisual &item, bool sheathed) const {
  auto attachments = ResolveModelAttachments(item, sheathed);
  if (attachments.empty()) {
    return std::nullopt;
  }

  return attachments.front();
}

std::optional<EquipModelAttachment>
EquipmentVisualSystem::ResolveModel(EquipmentSlot slot, uint32_t displayId,
                                    const CharacterModelPathContext &pathContext,
                                    bool sheathed) const {
  EquippedItemVisual item;
  item.slot = slot;
  item.displayId = displayId;
  item.sheatheType = 1;
  if (slot == EquipmentSlot::Ranged) {
    item.inventoryType = 26;
  }

  return ResolveModel(item, pathContext, sheathed);
}

std::optional<EquipModelAttachment>
EquipmentVisualSystem::ResolveModel(const EquippedItemVisual &item,
                                    const CharacterModelPathContext &pathContext,
                                    bool sheathed) const {
  auto attachments = ResolveModelAttachments(item, pathContext, sheathed);
  if (attachments.empty()) {
    return std::nullopt;
  }

  return attachments.front();
}

std::optional<std::string> EquipmentVisualSystem::ResolveCapeTexturePath(uint32_t displayId) const {
  auto info = GetDisplayInfo(displayId);
  if (!info || info->modelTexture[0].empty()) {
    return std::nullopt;
  }
  return info->modelTexture[0];
}

std::vector<EquipGeosetChange> EquipmentVisualSystem::GetRobeGeosetOverrides() const {
  return {
      {GeosetGroup::Legs, 0, false},
      {GeosetGroup::Robe, 1, true},
  };
}

std::vector<EquipGeosetChange>
EquipmentVisualSystem::GetHelmGeosetOverrides(uint32_t displayId) const {
  auto info = GetDisplayInfo(displayId);
  std::vector<EquipGeosetChange> changes;

  if (info) {
    if (info->helmetGeosetVis[0] != 0) {
      changes.push_back({GeosetGroup::Hair, 0, false});
    }
    if (info->helmetGeosetVis[1] != 0) {
      changes.push_back({GeosetGroup::FacialHair1, 0, false});
      changes.push_back({GeosetGroup::FacialHair2, 0, false});
      changes.push_back({GeosetGroup::FacialHair3, 0, false});
    }
  }
  return changes;
}

EquipmentVisualSystem::EquipmentVisuals
EquipmentVisualSystem::ResolveFullEquipment(const std::vector<EquippedItemVisual> &equipment,
                                            bool sheathedWeapons) const {
  CharacterModelPathContext default_path_context{};
  return ResolveFullEquipment(equipment, default_path_context, sheathedWeapons);
}

EquipmentVisualSystem::EquipmentVisuals
EquipmentVisualSystem::ResolveFullEquipment(const std::vector<EquippedItemVisual> &equipment,
                                            const CharacterModelPathContext &pathContext,
                                            bool sheathedWeapons) const {

  EquipmentVisuals result;
  bool hasRobe = false;
  uint32_t helmDisplayId = 0;

  for (const auto &item : equipment) {
    if (item.displayId == 0)
      continue;

    auto geosets = ResolveGeosets(item.slot, item.displayId);
    result.geosetChanges.insert(result.geosetChanges.end(), geosets.begin(), geosets.end());

    auto textures = ResolveTextures(item.slot, item.displayId);
    result.textureChanges.insert(result.textureChanges.end(), textures.begin(), textures.end());

    bool sheathed = sheathedWeapons &&
                    (item.slot == EquipmentSlot::MainHand || item.slot == EquipmentSlot::OffHand ||
                     item.slot == EquipmentSlot::Ranged);
    auto models = (item.slot == EquipmentSlot::Head)
                      ? ResolveModelAttachments(item, pathContext, sheathed)
                      : ResolveModelAttachments(item, sheathed);
    result.modelAttachments.insert(result.modelAttachments.end(), models.begin(), models.end());

    if (item.slot == EquipmentSlot::Chest && IsRobeDisplay(item.displayId)) {
      hasRobe = true;
    }

    if (item.slot == EquipmentSlot::Head) {
      helmDisplayId = item.displayId;
    }
  }

  if (hasRobe) {
    auto robeOverrides = GetRobeGeosetOverrides();
    result.geosetChanges.insert(result.geosetChanges.end(), robeOverrides.begin(),
                                robeOverrides.end());
  }

  if (helmDisplayId != 0) {
    auto helmOverrides = GetHelmGeosetOverrides(helmDisplayId);
    result.geosetChanges.insert(result.geosetChanges.end(), helmOverrides.begin(),
                                helmOverrides.end());
  }

  return result;
}

}

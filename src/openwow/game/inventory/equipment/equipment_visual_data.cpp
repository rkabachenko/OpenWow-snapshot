#include "openwow/game/inventory/equipment/equipment_visual.h"

#include <algorithm>

namespace openwow::game {
const char* ArmorRegionName(const ArmorRegion region) {
  static constexpr const char* kNames[] = {
      "ArmUpper", "ArmLower", "Hand", "TorsoUpper",
      "TorsoLower", "LegUpper", "LegLower", "Foot"};
  const auto index = static_cast<std::size_t>(region);
  return index < std::size(kNames) ? kNames[index] : "Unknown";
}

const char* AttachmentPointName(const AttachmentPoint point) {
  switch (point) {
    case AttachmentPoint::Head: return "Head";
    case AttachmentPoint::ShoulderLeft: return "ShoulderLeft";
    case AttachmentPoint::ShoulderRight: return "ShoulderRight";
    case AttachmentPoint::HandRight: return "HandRight";
    case AttachmentPoint::HandLeft: return "HandLeft";
    case AttachmentPoint::ShieldBack: return "ShieldBack";
    case AttachmentPoint::WeaponBackRight: return "WeaponBackRight";
    case AttachmentPoint::WeaponBackLeft: return "WeaponBackLeft";
    case AttachmentPoint::QuiverBack: return "QuiverBack";
    case AttachmentPoint::Cape: return "Cape";
    default: return "Unknown";
  }
}

const char* EquipmentSlotName(const EquipmentSlot slot) {
  switch (slot) {
    case EquipmentSlot::Head: return "Head";
    case EquipmentSlot::Neck: return "Neck";
    case EquipmentSlot::Shoulder: return "Shoulder";
    case EquipmentSlot::Shirt: return "Shirt";
    case EquipmentSlot::Chest: return "Chest";
    case EquipmentSlot::Waist: return "Waist";
    case EquipmentSlot::Legs: return "Legs";
    case EquipmentSlot::Feet: return "Feet";
    case EquipmentSlot::Wrist: return "Wrist";
    case EquipmentSlot::Hands: return "Hands";
    case EquipmentSlot::Ring1: return "Ring1";
    case EquipmentSlot::Ring2: return "Ring2";
    case EquipmentSlot::Trinket1: return "Trinket1";
    case EquipmentSlot::Trinket2: return "Trinket2";
    case EquipmentSlot::Back: return "Back";
    case EquipmentSlot::MainHand: return "MainHand";
    case EquipmentSlot::OffHand: return "OffHand";
    case EquipmentSlot::Ranged: return "Ranged";
    case EquipmentSlot::Tabard: return "Tabard";
    default: return "Unknown";
  }
}

void EquipmentVisualSystem::AddDisplayInfo(
    const ItemDisplayInfoEntry& entry) {
  displayInfoStore_.push_back(entry);
}

void EquipmentVisualSystem::ClearDisplayInfoStore() {
  displayInfoStore_.clear();
}

std::optional<ItemDisplayInfoEntry>
EquipmentVisualSystem::GetDisplayInfo(const std::uint32_t id) const {
  const auto found = std::find_if(
      displayInfoStore_.begin(), displayInfoStore_.end(),
      [id](const ItemDisplayInfoEntry& entry) { return entry.id == id; });
  return found == displayInfoStore_.end()
             ? std::nullopt
             : std::optional<ItemDisplayInfoEntry>{*found};
}

std::size_t EquipmentVisualSystem::GetDisplayInfoCount() const {
  return displayInfoStore_.size();
}

void EquipmentVisualSystem::AddItemVisualEntry(
    const ItemVisualAttachmentEntry& entry) {
  itemVisualStore_.push_back(entry);
}

void EquipmentVisualSystem::ClearItemVisualStore() {
  itemVisualStore_.clear();
}

std::optional<ItemVisualAttachmentEntry>
EquipmentVisualSystem::GetItemVisualEntry(const std::uint32_t id) const {
  const auto found = std::find_if(
      itemVisualStore_.begin(), itemVisualStore_.end(),
      [id](const ItemVisualAttachmentEntry& entry) {
        return entry.id == id;
      });
  return found == itemVisualStore_.end()
             ? std::nullopt
             : std::optional<ItemVisualAttachmentEntry>{*found};
}

void EquipmentVisualSystem::AddParticleColorEntry(
    const ParticleColorEntry& entry) {
  particleColorStore_.push_back(entry);
}

void EquipmentVisualSystem::ClearParticleColorStore() {
  particleColorStore_.clear();
}

std::optional<ParticleColorEntry>
EquipmentVisualSystem::GetParticleColorEntry(const std::uint32_t id) const {
  const auto found = std::find_if(
      particleColorStore_.begin(), particleColorStore_.end(),
      [id](const ParticleColorEntry& entry) { return entry.id == id; });
  return found == particleColorStore_.end()
             ? std::nullopt
             : std::optional<ParticleColorEntry>{*found};
}

std::vector<ArmorRegion> EquipmentVisualSystem::GetCoveredRegions(
    const EquipmentSlot slot) {
  switch (slot) {
    case EquipmentSlot::Shirt:
    case EquipmentSlot::Chest:
      return {ArmorRegion::TorsoUpper, ArmorRegion::ArmUpper};
    case EquipmentSlot::Waist:
      return {ArmorRegion::TorsoLower};
    case EquipmentSlot::Legs:
      return {ArmorRegion::LegUpper, ArmorRegion::LegLower};
    case EquipmentSlot::Feet:
      return {ArmorRegion::Foot, ArmorRegion::LegLower};
    case EquipmentSlot::Wrist:
      return {ArmorRegion::ArmLower};
    case EquipmentSlot::Hands:
      return {ArmorRegion::Hand, ArmorRegion::ArmLower};
    case EquipmentSlot::Tabard:
      return {ArmorRegion::TorsoUpper, ArmorRegion::TorsoLower};
    default:
      return {};
  }
}

std::uint32_t EquipmentVisualSystem::GetGeosetGroupForSlot(
    const EquipmentSlot slot) {
  switch (slot) {
    case EquipmentSlot::Shirt:
    case EquipmentSlot::Chest: return GeosetGroup::Shirt;
    case EquipmentSlot::Legs: return GeosetGroup::Legs;
    case EquipmentSlot::Feet: return GeosetGroup::Boots;
    case EquipmentSlot::Wrist: return GeosetGroup::Wrist;
    case EquipmentSlot::Hands: return GeosetGroup::Gloves;
    case EquipmentSlot::Waist: return GeosetGroup::Belt;
    case EquipmentSlot::Back: return GeosetGroup::Cape;
    case EquipmentSlot::Tabard: return GeosetGroup::Tabard;
    default: return 0;
  }
}

std::optional<AttachmentPoint> EquipmentVisualSystem::GetAttachmentPoint(
    const EquipmentSlot slot, const bool sheathed) {
  switch (slot) {
    case EquipmentSlot::Head: return AttachmentPoint::Head;
    case EquipmentSlot::Shoulder: return AttachmentPoint::ShoulderLeft;
    case EquipmentSlot::MainHand:
      return sheathed ? AttachmentPoint::WeaponBackRight
                      : AttachmentPoint::HandRight;
    case EquipmentSlot::OffHand:
      return sheathed ? AttachmentPoint::WeaponBackLeft
                      : AttachmentPoint::HandLeft;
    case EquipmentSlot::Ranged:
      return sheathed ? AttachmentPoint::QuiverBack
                      : AttachmentPoint::HandRight;
    case EquipmentSlot::Back: return AttachmentPoint::Cape;
    default: return std::nullopt;
  }
}

bool EquipmentVisualSystem::SlotHasModel(const EquipmentSlot slot) {
  switch (slot) {
    case EquipmentSlot::Head:
    case EquipmentSlot::Shoulder:
    case EquipmentSlot::MainHand:
    case EquipmentSlot::OffHand:
    case EquipmentSlot::Ranged:
    case EquipmentSlot::Back:
      return true;
    default:
      return false;
  }
}

}

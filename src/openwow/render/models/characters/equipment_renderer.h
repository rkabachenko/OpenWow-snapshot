#pragma once
#include "openwow/render/models/characters/equipment_item_visual.h"

#include "openwow/data/formats/dbc/dbc_loader.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::render {

struct EquipmentSlotVisual {
  uint32_t display_id{0};
  std::string model_path;
  uint32_t geoset_group{0};
  uint32_t geoset_value{0};
};

struct ItemVisualChild {
  uint32_t attachment_id{0};
  std::string model_path;

  friend bool operator==(const ItemVisualChild&, const ItemVisualChild&) =
      default;
};

struct WeaponAttachmentVisual {
  uint32_t attachment_id{0};
  std::string model_path;
  std::string texture_path;
  std::uint32_t replaceable_texture_type{2};
  std::vector<ItemVisualChild> item_visual_children;

  bool requires_bowstring{false};
};

struct EquipmentVisuals {

  static constexpr int kMaxGeosetGroups = 20;
  std::array<uint32_t, kMaxGeosetGroups> geoset_groups{};

  std::string main_hand_model;
  std::string off_hand_model;
  std::string ranged_model;

  std::optional<WeaponAttachmentVisual> main_hand_attachment;
  std::optional<WeaponAttachmentVisual> off_hand_attachment;
  std::optional<WeaponAttachmentVisual> ranged_attachment;

  std::optional<WeaponAttachmentVisual> right_shoulder_attachment;
  std::optional<WeaponAttachmentVisual> left_shoulder_attachment;
  std::optional<WeaponAttachmentVisual> helm_attachment;
  std::optional<WeaponAttachmentVisual> cape_attachment;
};

enum EquipmentSlot : uint8_t {
  kSlotHead       = 0,
  kSlotNeck       = 1,
  kSlotShoulders  = 2,
  kSlotBody       = 3,
  kSlotChest      = 4,
  kSlotWaist      = 5,
  kSlotLegs       = 6,
  kSlotFeet       = 7,
  kSlotWrists     = 8,
  kSlotHands      = 9,
  kSlotFinger1    = 10,
  kSlotFinger2    = 11,
  kSlotTrinket1   = 12,
  kSlotTrinket2   = 13,
  kSlotBack       = 14,
  kSlotMainHand   = 15,
  kSlotOffHand    = 16,
  kSlotRanged     = 17,
  kSlotTabard     = 18,
  kMaxEquipSlot   = 19,
};

class EquipmentRenderer {
 public:

  static constexpr std::uint32_t kItemClassWeapon = 2u;
  static constexpr std::uint32_t kItemSubclassWeaponBow = 2u;

  EquipmentRenderer() = default;

  void BindDbc(const openwow::data::dbc::DbcLoader* dbc);

  bool Initialize();

  [[nodiscard]] EquipmentVisuals ComputeVisuals(
      uint8_t race, uint8_t gender,
      const std::array<EquipmentItemVisual, kMaxEquipSlot>& equipped_items,
      uint8_t sheathe_state = 0) const;

  [[nodiscard]] std::optional<WeaponAttachmentVisual> ComputeAmmoVisual(
      std::uint32_t display_id, std::uint32_t attachment_id) const;

  void Shutdown();

 private:

  [[nodiscard]] EquipmentSlotVisual ResolveItemDisplay(
      uint32_t display_id, EquipmentSlot slot) const;

  [[nodiscard]] EquipmentItemVisual ResolveItemVisualMetadata(
      const EquipmentItemVisual& item) const;

  [[nodiscard]] static std::string BuildWeaponModelPath(
      const std::string& model_name);

  const openwow::data::dbc::DbcLoader* dbc_{nullptr};

  std::unordered_map<std::uint32_t, std::vector<EquipmentItemVisual>>
      item_metadata_by_display_;
  bool initialized_{false};
};

}

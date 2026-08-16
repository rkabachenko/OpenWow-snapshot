
#include "openwow/render/models/characters/equipment_renderer.h"

#include "openwow/game/inventory/equipment/equipment_visual.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <utility>

namespace openwow::render {

namespace {

bool IsWeaponSlotOnBody(const EquipmentSlot slot, const std::uint8_t sheathe_state) {
  switch (slot) {
    case kSlotMainHand:
    case kSlotOffHand:
      return sheathe_state != 1;
    case kSlotRanged:
      return sheathe_state != 2;
    default:
      return false;
  }
}

openwow::game::EquipmentSlot ToGameEquipmentSlot(const EquipmentSlot slot) {
  return static_cast<openwow::game::EquipmentSlot>(
      static_cast<std::uint8_t>(slot));
}

void AppendItemVisualChildren(
    WeaponAttachmentVisual& attachment,
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::ItemDisplayInfoEntry& display,
    const EquipmentItemVisual& item) {
  if (!item.item_visuals_enabled) {
    return;
  }

  const std::uint32_t visuals_id =
      item.item_visuals_id != 0u ? item.item_visuals_id
                                 : display.item_visuals_id;
  if (visuals_id == 0u) {
    return;
  }

  const auto* const visuals = dbc.item_visuals().LookupEntry(visuals_id);
  if (visuals == nullptr) {
    return;
  }

  for (std::size_t index = 0; index < visuals->slot.size(); ++index) {
    const std::uint32_t effect_id = visuals->slot[index];
    if (effect_id == 0u) {
      continue;
    }
    const auto* const effect = dbc.item_visual_effects().LookupEntry(effect_id);
    if (effect == nullptr || effect->model.empty()) {
      continue;
    }
    auto path = openwow::game::EquipmentVisualSystem::NormalizeModelPathToM2(
        effect->model);
    if (!path.empty()) {
      attachment.item_visual_children.push_back(
          {.attachment_id = static_cast<std::uint32_t>(index),
           .model_path = std::move(path)});
    }
  }
}

}

bool EquipmentRenderer::Initialize() {
  if (initialized_) return true;

  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "EquipmentRenderer: initialized");
  return true;
}

void EquipmentRenderer::BindDbc(
    const openwow::data::dbc::DbcLoader* const dbc) {
  dbc_ = dbc;
  item_metadata_by_display_.clear();
  if (dbc_ == nullptr) {
    return;
  }

  const auto& entries = dbc_->item().entries();
  item_metadata_by_display_.reserve(entries.size());
  for (const auto& row : entries) {
    if (row.display_info_id == 0u) {
      continue;
    }
    item_metadata_by_display_[row.display_info_id].push_back({
        .display_id = row.display_info_id,
        .class_id = row.class_id,
        .subclass_id = row.subclass_id,
        .inventory_type = static_cast<std::uint8_t>(
            std::min<std::uint32_t>(row.inventory_type, 0xffu)),
        .sheathe_type = static_cast<std::uint8_t>(
            std::min<std::uint32_t>(row.sheathe_type, 0xffu)),
    });
  }
}

void EquipmentRenderer::Shutdown() {
  if (!initialized_) return;
  initialized_ = false;
  item_metadata_by_display_.clear();
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "EquipmentRenderer: shutdown");
}

EquipmentSlotVisual EquipmentRenderer::ResolveItemDisplay(
    uint32_t display_id, EquipmentSlot slot) const {
  EquipmentSlotVisual visual;
  visual.display_id = display_id;

  if (display_id == 0 || dbc_ == nullptr) return visual;

  const auto* entry = dbc_->item_display_info().LookupEntry(display_id);
  if (entry == nullptr) return visual;

  switch (slot) {
    case kSlotMainHand:
    case kSlotOffHand:
    case kSlotRanged: {
      auto name = std::string(entry->model_name_left);
      if (!name.empty()) {
        visual.model_path = BuildWeaponModelPath(name);
      }
      break;
    }

    case kSlotHead:
      visual.geoset_group = 0;
      visual.geoset_value = 0;
      break;
    case kSlotShoulders:

      break;
    case kSlotBody:
      visual.geoset_group = 8;
      visual.geoset_value = 1;
      break;
    case kSlotChest:
      visual.geoset_group = 7;
      visual.geoset_value = 1;
      break;
    case kSlotWaist:

      break;
    case kSlotLegs:
      visual.geoset_group = 5;
      visual.geoset_value = 1;
      break;
    case kSlotFeet:
      visual.geoset_group = 5;
      visual.geoset_value = 2;
      break;
    case kSlotWrists:
      visual.geoset_group = 4;
      visual.geoset_value = 1;
      break;
    case kSlotHands:
      visual.geoset_group = 4;
      visual.geoset_value = 2;
      break;
    case kSlotBack:
      visual.geoset_group = 10;
      visual.geoset_value = 1;
      break;
    case kSlotTabard:
      visual.geoset_group = 6;
      visual.geoset_value = 1;
      break;
    default:
      break;
  }

  return visual;
}

std::string EquipmentRenderer::BuildWeaponModelPath(
    const std::string& model_name) {
  if (model_name.empty()) return {};

  return "Item\\ObjectComponents\\Weapon\\" + model_name + ".m2";
}

EquipmentItemVisual EquipmentRenderer::ResolveItemVisualMetadata(
    const EquipmentItemVisual& item) const {
  EquipmentItemVisual resolved = item;

  if (dbc_ == nullptr || item.display_id == 0) {
    return resolved;
  }

  const auto indexed = item_metadata_by_display_.find(item.display_id);
  if (indexed == item_metadata_by_display_.end() || indexed->second.empty()) {
    return resolved;
  }

  const auto exact = std::find_if(
      indexed->second.begin(), indexed->second.end(), [&](const auto& row) {
        return item.inventory_type != 0u &&
               row.inventory_type == item.inventory_type;
      });
  const auto& fallback =
      exact != indexed->second.end() ? *exact : indexed->second.front();

  if (resolved.inventory_type == 0) {
    resolved.inventory_type = fallback.inventory_type;
  }
  if (resolved.sheathe_type == 0) {
    resolved.sheathe_type = fallback.sheathe_type;
  }
  if (resolved.class_id == 0) {
    resolved.class_id = fallback.class_id;
  }
  if (resolved.subclass_id == 0) {
    resolved.subclass_id = fallback.subclass_id;
  }

  return resolved;
}

EquipmentVisuals EquipmentRenderer::ComputeVisuals(
    const uint8_t race, const uint8_t gender,
    const std::array<EquipmentItemVisual, kMaxEquipSlot>& equipped_items,
    const std::uint8_t sheathe_state) const {
  EquipmentVisuals visuals;
  visuals.geoset_groups.fill(0);

  for (uint8_t slot = 0; slot < kMaxEquipSlot; ++slot) {
    const auto item = ResolveItemVisualMetadata(equipped_items[slot]);
    const uint32_t display_id = item.display_id;
    if (display_id == 0) continue;

    auto slot_enum = static_cast<EquipmentSlot>(slot);
    auto visual = ResolveItemDisplay(display_id, slot_enum);

    if (visual.geoset_group > 0 && visual.geoset_group < EquipmentVisuals::kMaxGeosetGroups) {
      visuals.geoset_groups[visual.geoset_group] = visual.geoset_value;
    }

    switch (slot_enum) {
      case kSlotMainHand:
      case kSlotOffHand:
      case kSlotRanged:
        if (!visual.model_path.empty() && dbc_ != nullptr) {
          const auto* entry = dbc_->item_display_info().LookupEntry(display_id);
          if (entry) {
            const std::string model_name(entry->model_name_left);
            const std::string texture_stem(entry->texture_name_left);
            const bool use_alternate_slot =
                IsWeaponSlotOnBody(slot_enum, sheathe_state);
            const bool is_shield = item.inventory_type == 14u;
            const auto sheathe_type =
                item.sheathe_type != 0 ? item.sheathe_type : (is_shield ? 4u : 1u);
            const bool ranged_uses_main_table =
                openwow::game::EquipmentVisualSystem::UsePrimarySheathedWeaponSide(
                    ToGameEquipmentSlot(slot_enum), item.inventory_type);
            auto dispatch =
                openwow::game::EquipmentVisualSystem::DispatchStoredItemModel(
                    slot, sheathe_type, use_alternate_slot,
                    is_shield, ranged_uses_main_table,
                    model_name, texture_stem);
            if (dispatch.targetAttachmentId >= 0) {
              WeaponAttachmentVisual wav;
              wav.attachment_id =
                  static_cast<uint32_t>(dispatch.targetAttachmentId);
              wav.model_path = dispatch.modelPath;
              wav.texture_path = dispatch.texturePath;
              AppendItemVisualChildren(wav, *dbc_, *entry, item);

              if (slot_enum == kSlotRanged) {
                wav.requires_bowstring =
                    !use_alternate_slot &&
                    item.class_id == kItemClassWeapon &&
                    item.subclass_id == kItemSubclassWeaponBow;
              }

              if (slot_enum == kSlotMainHand) {
                visuals.main_hand_model = dispatch.modelPath;
                visuals.main_hand_attachment = wav;
              } else if (slot_enum == kSlotOffHand) {
                visuals.off_hand_model = dispatch.modelPath;
                visuals.off_hand_attachment = wav;
              } else {
                visuals.ranged_model = dispatch.modelPath;
                visuals.ranged_attachment = wav;
              }
            }
          }
        }
        break;

      case kSlotShoulders: {

        if (dbc_ == nullptr) break;
        const auto* entry = dbc_->item_display_info().LookupEntry(display_id);
        if (!entry) break;

        const std::string left_model_name(entry->model_name_left);
        const std::string right_model_name(entry->model_name_right);
        const std::string left_texture_stem(entry->texture_name_left);
        const std::string right_texture_stem(entry->texture_name_right);

        if (!left_model_name.empty()) {
          WeaponAttachmentVisual wav;
          wav.attachment_id = 6;
          wav.model_path =
              openwow::game::EquipmentVisualSystem::BuildShoulderModelPath(
                  left_model_name);
          wav.texture_path =
              openwow::game::EquipmentVisualSystem::BuildShoulderTexturePath(
                  left_texture_stem);
          visuals.left_shoulder_attachment = wav;
        }
        if (!right_model_name.empty()) {
          WeaponAttachmentVisual wav;
          wav.attachment_id = 5;
          wav.model_path =
              openwow::game::EquipmentVisualSystem::BuildShoulderModelPath(
                  right_model_name);
          wav.texture_path =
              openwow::game::EquipmentVisualSystem::BuildShoulderTexturePath(
                  right_texture_stem);
          visuals.right_shoulder_attachment = wav;
        }
        break;
      }

      case kSlotHead: {

        if (dbc_ == nullptr) break;
        const auto* entry = dbc_->item_display_info().LookupEntry(display_id);
        if (!entry) break;

        const std::string helm_model_name(entry->model_name_left);
        const std::string helm_texture(entry->texture_name_left);
        const auto* race_entry = dbc_->chr_races().LookupEntry(race);
        if (!helm_model_name.empty() && race_entry != nullptr &&
            !race_entry->model_client_prefix.empty()) {
          WeaponAttachmentVisual wav;
          wav.attachment_id = 11;
          wav.model_path =
              openwow::game::EquipmentVisualSystem::NormalizeModelPathToM2(
                  openwow::game::EquipmentVisualSystem::BuildHeadModelPath(
                      helm_model_name, race_entry->model_client_prefix,
                      gender));
          wav.texture_path = helm_texture;
          visuals.helm_attachment = wav;
        }
        break;
      }

      case kSlotBack:

        break;

      case kSlotTabard: {

        break;
      }

      default:
        break;
    }
  }

  return visuals;
}

std::optional<WeaponAttachmentVisual> EquipmentRenderer::ComputeAmmoVisual(
    const std::uint32_t display_id,
    const std::uint32_t attachment_id) const {
  if (dbc_ == nullptr || display_id == 0u) {
    return std::nullopt;
  }

  const auto* const display = dbc_->item_display_info().LookupEntry(display_id);
  if (display == nullptr) {
    return std::nullopt;
  }

  openwow::game::EquipmentVisualSystem component_builder;
  const auto component = component_builder.BuildObjectComponentModel(
      attachment_id, display->model_name_right, display->texture_name_right,
      display->item_visuals_id);
  if (!component.has_value()) {
    return std::nullopt;
  }

  WeaponAttachmentVisual visual{
      .attachment_id = component->attachmentId,
      .model_path = openwow::game::EquipmentVisualSystem::NormalizeModelPathToM2(
          component->modelPath),
      .texture_path = component->texturePath,
      .replaceable_texture_type = component->replaceableTextureType,
  };
  AppendItemVisualChildren(
      visual, *dbc_, *display,
      EquipmentItemVisual{
          .display_id = display_id,
          .item_visuals_id = display->item_visuals_id,
          .item_visuals_enabled = display->item_visuals_id != 0u,
      });
  return visual;
}

}

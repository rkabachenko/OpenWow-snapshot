#include "openwow/game/spell_packet_visual_dispatch.h"

#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/equipment/equipment_visual.h"
#include "openwow/game/inventory/equipment/item_equip_check.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/spell_book.h"
#include "openwow/game/spell_missile_visual_create.h"
#include "openwow/game/spell_visual.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/game/world_session.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <vector>

namespace openwow::game {
namespace {

constexpr std::uint8_t kSpellMissReflect = 11u;
constexpr std::string_view kRetailMissingMissileModel =
    "Spells\\ErrorCube.mdx";

bool AppendDestLocAreaEffect(
    const data::dbc::DbcLoader& dbc,
    const std::uint32_t effect_name_id,
    const std::uint32_t source_field_index,
    SpellVisualPresentationEvent& event) {
  if (effect_name_id == 0u) return false;
  const auto* const effect =
      dbc.spell_visual_effect_name().LookupEntry(effect_name_id);
  if (effect == nullptr) return false;
  if (!effect->file_path.empty()) {
    event.effects.push_back({
        .effect_name_id = effect_name_id,
        .model_path = std::string(effect->file_path),
        .resource_scale = effect->scale,
        .attachment_id = -1,
        .source_field_index = source_field_index,
        .world_space = true,
        .uses_explicit_world_position = true,
    });
  }
  return true;
}

std::vector<SpellVisualPresentationEvent> BuildDestLocAreaEvents(
    const data::dbc::DbcLoader& dbc,
    const std::uint32_t spell_id,
    const std::uint32_t spell_visual_id,
    const std::uint32_t kit_id,
    const std::array<float, 3>& position,
    const std::uint32_t dispatch_type,
    const std::uint32_t spell_missile_id,
    const std::uint64_t effect_owner_guid) {
  std::vector<SpellVisualPresentationEvent> events;
  if (kit_id == 0u) return events;
  const auto* const kit = dbc.spell_visual_kit().LookupEntry(kit_id);
  if (kit == nullptr) return events;

  const auto make_event = [&]() {
    return SpellVisualPresentationEvent{
      .action = SpellVisualLifecycleAction::kTransient,
      .phase = SpellVisualPresentationPhase::kEffect,
      .dispatch_type = dispatch_type,
      .spell_id = spell_id,
      .spell_visual_id = spell_visual_id,
      .kit_id = kit_id,
      .world_position = position,
      .deferred_impact_owner_guid = effect_owner_guid,
      .m2_events_require_owner_resolution = true,
    };
  };

  std::uint32_t raw_flags =
      DestLocAreaEffectFlags::ComputeInitialFlags(spell_missile_id);

  auto base = make_event();
  base.raw_flags = raw_flags;
  if (AppendDestLocAreaEffect(dbc, kit->base_effect, 5u, base)) {
    if (!base.effects.empty()) events.push_back(std::move(base));
    raw_flags = DestLocAreaEffectFlags::AfterPrecastEffect(raw_flags);
  }
  auto world = make_event();
  world.raw_flags = raw_flags;
  if (AppendDestLocAreaEffect(dbc, kit->world_effect, 14u, world)) {
    if (!world.effects.empty()) events.push_back(std::move(world));
  }
  return events;
}

SpellMissilePresentationData BuildDestLocMissilePresentation(
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SpellVisualEntry& visual) {
  SpellMissilePresentationData missile{
      .model_path = std::string(kRetailMissingMissileModel),
      .model_scale = 1.0f,
      .target_attachment_id = visual.missile_destination_attachment,
      .target_attachment_uses_raw_index = (visual.flags & 0x200u) != 0u,
      .target_attachment_offset = {
          visual.missile_impact_offset_x,
          -visual.missile_impact_offset_y,
          visual.missile_impact_offset_z,
      },
      .follow_ground_height_ms = visual.missile_follow_ground_height,
      .follow_ground_drop_speed_ms = visual.missile_follow_ground_drop_speed,
      .follow_ground_approach_ms = visual.missile_follow_ground_approach,
      .follow_ground_flags = visual.missile_follow_ground_flags,
      .motion_id = visual.missile_motion_id,
      .sound_kit_id = visual.missile_sound_id,
  };
  if (visual.missile_model > 0) {
    if (const auto* const effect =
            dbc.spell_visual_effect_name().LookupEntry(
                static_cast<std::uint32_t>(visual.missile_model));
        effect != nullptr && !effect->file_path.empty()) {
      missile.model_path = std::string(effect->file_path);
      missile.model_scale = effect->scale;
    }
  }
  return missile;
}

struct ResolvedPacketVisual {
  std::optional<data::dbc::SpellVisualEntry> visual;
  std::uint32_t visual_id{0};
};

ResolvedPacketVisual ResolvePacketVisual(const CGUnit_C& caster,
                                         const std::uint32_t spell_id) {
  const auto* const dbc = caster.dbc_loader();
  const auto* const spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
  if (spell == nullptr) return {};

  data::dbc::SpellVisualEntry merged{};
  const auto* const visual =
      caster.ResolveSpellVisualRecord(*spell, merged, 0u, 0u);
  if (visual == nullptr) return {};

  const std::uint32_t visual_id = visual->id;
  return {*visual, visual_id};
}

std::optional<SpellMissilePresentationData> ResolveMissilePresentationData(
    const data::dbc::DbcLoader& dbc,
    const CGUnit_C& caster,
    const data::dbc::SpellVisualEntry& visual,
    const SpellGoVisualData& visual_data) {
  if (visual.has_missile == 0u) return std::nullopt;

  SpellMissilePresentationData missile{
      .model_scale = 1.0f,
      .target_attachment_id =
          visual.missile_destination_attachment >= 0
              ? visual.missile_destination_attachment
              : 0x0c,
      .target_attachment_uses_raw_index = (visual.flags & 0x200u) != 0u,
      .target_attachment_offset = {
          visual.missile_impact_offset_x,
          -visual.missile_impact_offset_y,
          visual.missile_impact_offset_z,
      },
      .follow_ground_height_ms = visual.missile_follow_ground_height,
      .follow_ground_drop_speed_ms =
          visual.missile_follow_ground_drop_speed,
      .follow_ground_approach_ms = visual.missile_follow_ground_approach,
      .follow_ground_flags = visual.missile_follow_ground_flags,
      .motion_id = visual.missile_motion_id,
      .sound_kit_id = visual.missile_sound_id,
  };

  if (visual.missile_model > 0) {
    if (const auto* const effect = dbc.spell_visual_effect_name().LookupEntry(
            static_cast<std::uint32_t>(visual.missile_model));
        effect != nullptr) {
      missile.model_path = std::string(effect->file_path);
      missile.model_scale = effect->scale;
    }
  } else {
    const std::optional<SpellVisualItemModelContext>* context = nullptr;
    std::optional<SpellVisualItemModelContext> unit_weapon_context;
    auto slot = EquipmentSlot::MainHand;
    const auto resolve_unit_weapon_context =
        [&](const std::uint8_t weapon_slot,
            const std::optional<SpellVisualItemModelContext>& packet_context)
        -> const std::optional<SpellVisualItemModelContext>* {
      if (packet_context.has_value()) return &packet_context;
      const auto item_entry = caster.State().GetVirtualItemSlotEntry(weapon_slot);
      if (item_entry == 0u) return &unit_weapon_context;

      const auto* const item = dbc.item().LookupEntry(item_entry);
      if (item != nullptr && item->display_info_id != 0u) {
        unit_weapon_context = SpellVisualItemModelContext{
            .display_id = item->display_info_id,
            .inventory_type = item->inventory_type,
        };
      }
      return &unit_weapon_context;
    };
    switch (static_cast<MissileModelSourceType>(visual.missile_model)) {
      case MissileModelSourceType::kMainHandWeapon:
        context = resolve_unit_weapon_context(0u,
                                              visual_data.main_hand_weapon);
        break;
      case MissileModelSourceType::kOffHandWeapon:
        context = resolve_unit_weapon_context(1u,
                                              visual_data.off_hand_weapon);
        slot = EquipmentSlot::OffHand;
        break;
      case MissileModelSourceType::kRangedWeapon:
        context = resolve_unit_weapon_context(2u,
                                              visual_data.ranged_weapon);
        slot = EquipmentSlot::Ranged;
        break;
      case MissileModelSourceType::kItemDisplayInfo:
      case MissileModelSourceType::kItemComponent1:
      case MissileModelSourceType::kItemComponent2:
        context = &visual_data.missile_item;
        if (context->has_value()) {
          const auto inventory_type = context->value().inventory_type;
          if (inventory_type ==
                  static_cast<std::uint32_t>(InventoryType::Ammo) ||
              CanInventoryTypeGoInSlot(
                  inventory_type,
                  static_cast<std::uint32_t>(EquipmentSlot::Ranged))) {
            slot = EquipmentSlot::Ranged;
          } else if (CanInventoryTypeGoInSlot(
                         inventory_type,
                         static_cast<std::uint32_t>(EquipmentSlot::OffHand)) &&
                     !CanInventoryTypeGoInSlot(
                         inventory_type,
                         static_cast<std::uint32_t>(EquipmentSlot::MainHand))) {
            slot = EquipmentSlot::OffHand;
          }
        }
        break;
      default:
        break;
    }

    if (context != nullptr && context->has_value()) {
      const auto& item = context->value();
      if (const auto* const display =
              dbc.item_display_info().LookupEntry(item.display_id);
          display != nullptr) {
        const bool use_right = display->model_name_left.empty();
        const auto model_name = use_right ? display->model_name_right
                                          : display->model_name_left;
        const auto texture_name = use_right ? display->texture_name_right
                                            : display->texture_name_left;
        if (item.inventory_type ==
            static_cast<std::uint32_t>(InventoryType::Ammo)) {

          missile.model_path = EquipmentVisualSystem::NormalizeModelPathToM2(
              EquipmentVisualSystem::BuildAmmoModelPath(
                  display->model_name_right));
          missile.texture_path = EquipmentVisualSystem::BuildAmmoTexturePath(
              display->texture_name_right);
        } else {
          ItemDisplayInfoEntry item_display;
          item_display.id = display->id;
          item_display.modelName[0] = model_name;
          item_display.modelTexture[0] = texture_name;
          item_display.itemVisualId = display->item_visuals_id;
          item_display.particleColorId = display->particle_color_id;

          EquipmentVisualSystem equipment;
          equipment.AddDisplayInfo(item_display);
          const auto component = equipment.ResolveModel(EquippedItemVisual{
              .slot = slot,
              .displayId = item.display_id,
              .inventoryType = static_cast<std::uint8_t>(
                  std::min<std::uint32_t>(item.inventory_type, 0xffu)),
              .sheatheType = 1u,
          });
          if (component.has_value()) {
            missile.model_path =
                EquipmentVisualSystem::NormalizeModelPathToM2(
                    component->modelPath);
            missile.texture_path = component->texturePath;
            missile.replaceable_texture_type =
                component->replaceableTextureType;
          }
        }
      }
    }
  }

  if (missile.model_path.empty()) {
    missile.model_path = "Spells\\ErrorCube.mdx";
    missile.texture_path.clear();
    missile.model_scale = 1.0f;
  }
  return missile;
}

std::array<float, 3> ResolveMissileLaunchPosition(
    const CGUnit_C& caster, const data::dbc::SpellVisualEntry& visual) {
  std::array<float, 3> position{};
  if (caster.Presentation().GetSpellVisualAttachmentPosition(
          position.data(), visual)) {
    return position;
  }
  const std::array<float, 3> offset{
      visual.missile_cast_offset_x,
      -visual.missile_cast_offset_y,
      visual.missile_cast_offset_z,
  };
  SpellVisual_GetAttachSourcePosition(
      position.data(), reinterpret_cast<std::uintptr_t>(&caster),
      visual.missile_attachment_id >= 0
          ? static_cast<std::uint32_t>(visual.missile_attachment_id)
          : std::numeric_limits<std::uint32_t>::max(),
      offset.data(), (visual.flags & 0x200u) != 0u);
  return position;
}

std::array<float, 3> ResolveMissileImpactPosition(
    const CGUnit_C& target, const data::dbc::SpellVisualEntry& visual) {

  const render::RenderVec3 offset{
      visual.missile_impact_offset_x,
      -visual.missile_impact_offset_y,
      visual.missile_impact_offset_z,
  };
  const std::uint32_t attachment =
      visual.missile_destination_attachment >= 0
          ? static_cast<std::uint32_t>(visual.missile_destination_attachment)
          : 0x0cu;
  std::array<float, 3> position{};
  if (target.Presentation().GetMappedAttachmentPosition(
          position.data(), attachment, offset,
          (visual.flags & 0x200u) != 0u)) {
    return position;
  }

  const auto base = target.GetPosition();
  return {base.x + offset[0], base.y + offset[1],
          base.z + target.Presentation().ModelHeight() * 0.75f + offset[2]};
}

std::array<float, 3> ResolveMissileImpactPosition(
    const CGObject_C& target, const data::dbc::SpellVisualEntry& visual) {
  if (target.IsUnit()) {
    return ResolveMissileImpactPosition(
        static_cast<const CGUnit_C&>(target), visual);
  }

  const auto base = target.GetPosition();
  return {
      base.x + visual.missile_impact_offset_x,
      base.y - visual.missile_impact_offset_y,
      base.z + visual.missile_impact_offset_z,
  };
}

}

namespace {

void QueueKit(WorldSession& session, CGUnit_C& owner,
              const std::uint32_t spell_id,
              const std::uint32_t visual_id, const std::uint32_t kit_id,
              const std::uint32_t dispatch_type,
              const std::array<float, 3>* const world_position = nullptr) {
  if (kit_id == 0u) return;
  (void)owner.SpellVisuals().CreateFromKit(
      session, kit_id, dispatch_type, world_position, false, {}, spell_id,
      visual_id, SpellVisualPresentationPhase::kEffect);
}

}

void QueueSpellStartVisual(WorldSession& session, const ObjectGuid caster,
                           const std::uint32_t spell_id) {
  auto* const caster_unit = session.objects().GetMutableUnit(caster);
  if (caster_unit == nullptr) return;

  const auto resolved = ResolvePacketVisual(*caster_unit, spell_id);
  if (!resolved.visual.has_value()) return;
  if (resolved.visual->precast_kit == 0u) return;
  (void)caster_unit->SpellVisuals().CreateFromKit(
      session, resolved.visual->precast_kit, 4u, nullptr,
      false, {}, spell_id, resolved.visual_id,
      SpellVisualPresentationPhase::kEffect,
      SpellVisualLifecycleAction::kCastStart);
}

void QueueSpellStopVisual(WorldSession& session, const ObjectGuid caster,
                          const std::uint32_t spell_id) {
  if (auto* const caster_unit = session.objects().GetMutableUnit(caster);
      caster_unit != nullptr) {
    caster_unit->SpellVisuals().QueueCastVisualStop(spell_id);
    caster_unit->Animation().SetChannelingActionLock(false);
    caster_unit->Animation().EndSpellVisualStandAnimation(session);
  }
}

void QueueSpellGoVisual(
    WorldSession& session, const ObjectGuid caster,
    const std::uint32_t spell_id,
    const std::span<const ObjectGuid> hit_targets,
    const std::optional<std::array<float, 3>>& destination,
    const SpellGoVisualData& visual_data) {
  auto* const caster_unit = session.objects().GetMutableUnit(caster);
  if (caster_unit == nullptr) return;

  caster_unit->SpellVisuals().QueueCastVisualStop(spell_id);
  caster_unit->Animation().EndSpellVisualStandAnimation(session);

  const auto* const spell = caster_unit->dbc_loader() != nullptr
                                ? caster_unit->dbc_loader()->spell().LookupEntry(
                                      spell_id)
                                : nullptr;
  if (spell == nullptr || (spell->attributes_ex & 0x44u) == 0u) {
    caster_unit->Animation().SetChannelingActionLock(false);
  }

  const auto resolved = ResolvePacketVisual(*caster_unit, spell_id);

  const bool is_local_player_caster =
      caster == session.objects().GetLocalPlayerGuid();
  static int spell_go_receipt_count = 0;
  constexpr int kSpellGoReceiptLimit = 5;
  const bool receipt_active =
      is_local_player_caster || spell_go_receipt_count < kSpellGoReceiptLimit;
  if (receipt_active) {
    if (!is_local_player_caster) {
      ++spell_go_receipt_count;
    }
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "SpellGo missile receipt: spell=" + std::to_string(spell_id) +
            " visual=" + std::to_string(resolved.visual_id) +
            " resolved=" + (resolved.visual.has_value() ? "1" : "0") +
            " local_caster=" + std::to_string(is_local_player_caster));
  }
  if (!resolved.visual.has_value()) return;

  QueueKit(session, *caster_unit, spell_id, resolved.visual_id,
           resolved.visual->cast_kit, 0u);

  std::vector<std::uint64_t> missile_target_guids;
  std::vector<std::array<float, 3>> missile_target_positions;
  const auto* const dbc = caster_unit->dbc_loader();
  const auto missile_definition =
      dbc != nullptr
          ? ResolveMissilePresentationData(*dbc, *caster_unit,
                                           *resolved.visual, visual_data)
          : std::nullopt;
  if (missile_definition.has_value()) {

    const float speed = spell != nullptr ? spell->speed : 0.0f;

    const auto missile_source =
        ResolveMissileLaunchPosition(*caster_unit, *resolved.visual);

    const auto find_miss =
        [&visual_data](const std::uint64_t guid)
        -> const SpellVisualMissTargetContext* {
      const auto found = std::find_if(
          visual_data.miss_targets.begin(), visual_data.miss_targets.end(),
          [guid](const SpellVisualMissTargetContext& miss) {
            return miss.guid == guid;
          });
      return found != visual_data.miss_targets.end() ? &*found : nullptr;
    };
    const auto queue_guid_target =
        [&](const std::uint64_t target_guid) {
      if (target_guid == 0u ||
          std::find(missile_target_guids.begin(),
                    missile_target_guids.end(),
                    target_guid) != missile_target_guids.end()) {
        return false;
      }

      const auto* const target =
          session.objects().GetObjectByGUID(ObjectGuid{target_guid});
      if (target == nullptr) return false;

      const auto missile_target =
          ResolveMissileImpactPosition(*target, *resolved.visual);
      const auto* const miss = find_miss(target_guid);
      const bool reflected =
          miss != nullptr && miss->reason == kSpellMissReflect;
      const auto specialized_impact_kit =
          target_guid == caster.GetRawValue()
              ? resolved.visual->caster_impact_kit
              : resolved.visual->target_impact_kit;
      const auto successful_impact_kit =
          specialized_impact_kit != 0u
              ? specialized_impact_kit
              : resolved.visual->impact_kit;
      const auto reflected_impact_kit =
          resolved.visual->caster_impact_kit != 0u
              ? resolved.visual->caster_impact_kit
              : resolved.visual->impact_kit;
      caster_unit->SpellVisuals().QueueMissileVisual(
          spell_id, resolved.visual_id, *missile_definition,
          visual_data.missile_caster_guid, visual_data.cast_count, target_guid,
          missile_source, missile_target, speed,
          miss == nullptr
              ? successful_impact_kit
              : (reflected ? reflected_impact_kit : 0u),
          miss != nullptr ? miss->reason : 0u,
          miss != nullptr ? miss->reflect_result : 0u);
      missile_target_guids.push_back(target_guid);
      return true;
    };
    const auto queue_position_target =
        [&](const std::array<float, 3>& target_position) {
      if (std::find(missile_target_positions.begin(),
                    missile_target_positions.end(),
                    target_position) != missile_target_positions.end()) {
        return false;
      }
      caster_unit->SpellVisuals().QueueMissileVisual(
          spell_id, resolved.visual_id, *missile_definition,
          visual_data.missile_caster_guid, visual_data.cast_count, 0u,
          missile_source, target_position, speed, resolved.visual->impact_kit);
      missile_target_positions.push_back(target_position);
      return true;
    };

    if (visual_data.explicit_target_guid != 0u) {
      (void)queue_guid_target(visual_data.explicit_target_guid);
    } else if (destination.has_value()) {
      (void)queue_position_target(*destination);
    }
    for (const auto& extra : visual_data.extra_targets) {
      (void)queue_position_target(
          {extra.world_x, extra.world_y, extra.world_z});
    }

  }

  if (receipt_active) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "SpellGo missile receipt: spell=" + std::to_string(spell_id) +
            " missile_def=" + (missile_definition.has_value() ? "1" : "0") +
            (missile_definition.has_value()
                 ? " model=" + missile_definition->model_path
                 : std::string{}) +
            " explicit_target=" +
            std::to_string(visual_data.explicit_target_guid != 0u) +
            " dest=" + std::to_string(destination.has_value()) +
            " queued_guid=" + std::to_string(missile_target_guids.size()) +
            " queued_pos=" +
            std::to_string(missile_target_positions.size()));
  }

  for (const auto target : hit_targets) {
    if (std::find(missile_target_guids.begin(), missile_target_guids.end(),
                  target.GetRawValue()) != missile_target_guids.end()) {

      continue;
    }
    auto* const target_unit = session.objects().GetMutableUnit(target);
    if (target_unit == nullptr) continue;
    const auto specialized_kit = target == caster
                                     ? resolved.visual->caster_impact_kit
                                     : resolved.visual->target_impact_kit;
    const auto kit_id = specialized_kit != 0u
                            ? specialized_kit
                            : resolved.visual->impact_kit;
    QueueKit(session, *target_unit, spell_id, resolved.visual_id, kit_id,
             1u);
  }

  if (!missile_definition.has_value()) {
    if (destination.has_value()) {
      QueueKit(session, *caster_unit, spell_id, resolved.visual_id,
               resolved.visual->impact_kit, 1u,
               &*destination);
    }
    for (const auto& extra : visual_data.extra_targets) {
      const std::array<float, 3> position{
          extra.world_x, extra.world_y, extra.world_z};
      QueueKit(session, *caster_unit, spell_id, resolved.visual_id,
               resolved.visual->impact_kit, 1u, &position);
    }
  }

  if (hit_targets.empty() && visual_data.miss_targets.empty() &&
      !destination.has_value() && visual_data.extra_targets.empty() &&
      visual_data.explicit_target_guid == 0u) {
    const auto kit_id = resolved.visual->caster_impact_kit != 0u
                            ? resolved.visual->caster_impact_kit
                            : resolved.visual->impact_kit;
    QueueKit(session, *caster_unit, spell_id, resolved.visual_id, kit_id,
             1u);
  }
}

void QueueChannelStartVisual(WorldSession& session, const ObjectGuid caster,
                             const std::uint32_t spell_id) {
  auto* const caster_unit = session.objects().GetMutableUnit(caster);
  if (caster_unit == nullptr) return;
  const auto resolved = ResolvePacketVisual(*caster_unit, spell_id);
  if (!resolved.visual.has_value()) return;
  if (resolved.visual->channel_kit == 0u) return;
  (void)caster_unit->SpellVisuals().CreateFromKit(
      session, resolved.visual->channel_kit, 0u, nullptr,
      false, {}, spell_id, resolved.visual_id,
      SpellVisualPresentationPhase::kEffect,
      SpellVisualLifecycleAction::kChannelStart);
}

void QueueChannelStopVisual(WorldSession& session, const ObjectGuid caster,
                            const std::uint32_t spell_id) {
  if (auto* const caster_unit = session.objects().GetMutableUnit(caster);
      caster_unit != nullptr) {
    caster_unit->SpellVisuals().QueueChannelVisualStop(spell_id);
  }
}

void QueueDestLocSpellCastVisual(WorldSession& session,
                                 const DestLocSpellCast& record) {
  const auto* const dbc = session.GetDbcLoader();
  if (dbc == nullptr || record.spell_id == 0u) return;
  const auto* const spell = dbc->spell().LookupEntry(record.spell_id);
  if (spell == nullptr || spell->spell_visual[0] == 0u) return;
  const auto* const visual =
      dbc->spell_visual().LookupEntry(spell->spell_visual[0]);
  if (visual == nullptr) return;

  const std::array<float, 3> destination{
      record.destination_x, record.destination_y, record.destination_z};
  const std::uint64_t effect_owner_guid =
      (static_cast<std::uint64_t>(record.payload_word_3) << 32u) |
      record.payload_word_2;
  session.QueueSpellVisualPresentationEvents(BuildDestLocAreaEvents(
      *dbc, record.spell_id, visual->id, visual->instant_area_kit,
      destination, 0u,
      spell->spell_missile_id, effect_owner_guid));

  const bool creates_missile =
      visual->has_missile != 0u && visual->missile_model != 0 &&
      spell->speed > 0.0f;
  if (creates_missile) {
    const std::array<float, 3> source{
        record.source_x,
        record.source_y,
        record.source_z,
    };
    SpellVisualPresentationEvent missile_event{
        .action = SpellVisualLifecycleAction::kTransient,
        .phase = SpellVisualPresentationPhase::kEffect,
        .dispatch_type = 0u,
        .spell_id = record.spell_id,
        .spell_visual_id = visual->id,
        .owner_position = source,
        .missile = BuildDestLocMissilePresentation(*dbc, *visual),
        .missile_source_position = source,
        .missile_caster_guid = record.record_guid.GetRawValue(),

        .missile_cast_count = record.missile_cast_count,
        .missile_target_position = destination,
        .missile_speed = spell->speed,
        .missile_uses_timed_trajectory = record.trajectory_speed > 0.0f,
        .missile_trajectory_pitch = record.trajectory_pitch,
        .missile_trajectory_speed = record.trajectory_speed,
        .missile_trajectory_duration_ms = record.duration_ms,
        .deferred_impact_kit_id = visual->impact_area_kit,
        .deferred_impact_policy =
            SpellVisualDeferredImpactPolicy::kDestLocArea,
        .deferred_impact_raw_flags =
            DestLocAreaEffectFlags::ComputeInitialFlags(
                spell->spell_missile_id),
        .deferred_impact_owner_guid = effect_owner_guid,
    };
    session.QueueSpellVisualPresentationEvent(std::move(missile_event));
    return;
  }

  session.QueueSpellVisualPresentationEvents(
      BuildDestLocAreaEvents(
          *dbc, record.spell_id, visual->id, visual->impact_area_kit,
          destination, 1u,
          spell->spell_missile_id, effect_owner_guid),
      record.duration_ms);
}

}

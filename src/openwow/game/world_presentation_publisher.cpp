#include "openwow/game/world_presentation_publisher.h"

#include "openwow/render/scene/quest_overlay_visual.h"

#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/m2/model_path.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/memory/prefetch.h"
#include "openwow/game/character_animation.h"
#include "openwow/game/group_system.h"
#include "openwow/game/nameplate_damage_flash.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/objects/cgdynamicobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/player_name_desc.h"
#include "openwow/game/threat_system.h"
#include "openwow/game/trivial_level.h"
#include "openwow/game/unit_name_display.h"
#include "openwow/game/transport_manager.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/violence_level.h"
#include "openwow/render/models/characters/character_appearance_geosets.h"
#include "openwow/render/models/characters/character_appearance_texture_baker.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/threat_warning_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

namespace openwow::game {
using namespace openwow::render;
namespace {

constexpr std::uint32_t kNameColorWhiteArgb = 0xFFFFFFFFu;
constexpr std::uint32_t kNameColorMouseoverArgb = 0xFFFFFF00u;
constexpr std::uint32_t kCreatureTypeFlagBoss = 0x4u;

constexpr std::uint32_t kCreatureTypeFlagHideNameplate = 0x00100000u;

constexpr std::uint8_t kMinFlashThreatStatus = 2u;
constexpr std::uint32_t kCharacterComponentModelFlag = 0x4u;

constexpr std::array<std::size_t, 11> kNpcItemDisplayEquipmentSlots{
    render::kSlotHead,  render::kSlotShoulders, render::kSlotBody, render::kSlotChest,
    render::kSlotWaist, render::kSlotLegs,      render::kSlotFeet, render::kSlotWrists,
    render::kSlotHands, render::kSlotTabard,    render::kSlotBack,
};

std::string BuildNpcBakedTexturePath(const std::string_view bake_name) {
  if (bake_name.empty()) {
    return {};
  }
  std::string path;
  if (bake_name.find_first_of("\\/") == std::string_view::npos) {
    path = "Textures\\BakedNpcTextures\\";
  }
  path.append(bake_name);
  if (path.size() < 4u || !openwow::text::EqualsIgnoreCaseAscii(
                              std::string_view(path).substr(path.size() - 4u), ".blp")) {
    path.append(".blp");
  }
  return path;
}

std::string ResolveCastIcon(const game::CastInfo &cast, const data::dbc::DbcLoader *dbc) {
  if (!cast.texture.empty()) {
    return cast.texture;
  }
  if (dbc == nullptr) {
    return {};
  }
  const auto *spell = dbc->spell().LookupEntry(cast.spell_id);
  if (spell == nullptr || spell->spell_icon_id == 0u) {
    return {};
  }
  const auto *icon = dbc->spell_icon().LookupEntry(spell->spell_icon_id);
  return icon != nullptr ? std::string(icon->icon_path) : std::string{};
}

constexpr std::uint32_t kSpellAttributeNoNameplateCastBar = 0x20u;

bool HidesNameplateCastBar(const game::CastInfo &cast,
                           const data::dbc::DbcLoader *dbc) {
  if (dbc == nullptr) {
    return false;
  }
  const auto *const spell = dbc->spell().LookupEntry(cast.spell_id);
  return spell != nullptr &&
         (spell->attributes & kSpellAttributeNoNameplateCastBar) != 0u;
}

std::string ResolveCastName(const game::CastInfo &cast, const data::dbc::DbcLoader *dbc) {
  if (!cast.spell_name.empty()) {
    return cast.spell_name;
  }
  if (dbc != nullptr) {
    if (const auto *spell = dbc->spell().LookupEntry(cast.spell_id)) {
      return std::string(spell->spell_name);
    }
  }
  return cast.spell_text;
}

const ObjectProjection kPristineProjection{};

std::uint32_t ResolveDisplayId(const game::WorldObject &object) {
  if (object.IsDynamicObject()) {
    return static_cast<const game::CGDynamicObject_C &>(object).GetSpellId();
  }
  if (object.IsGameObject()) {
    return static_cast<const game::CGGameObject_C &>(object).GetRenderDisplayId();
  }
  if (object.IsUnit()) {
    return static_cast<const game::CGUnit_C &>(object)
        .Presentation()
        .CreatureModelLookupDisplayId();
  }
  return object.GetDisplayId();
}

std::string ResolveName(const game::WorldObject &object, const game::CGUnit_C &unit,
                        const game::WorldSession &world_session,
                        const game::CreatureTemplateInfo *creature_template) {
  if (unit.IsPlayer()) {

    return unit.ResolveRetailName(world_session);
  }
  if (creature_template != nullptr && !creature_template->name.empty()) {
    return creature_template->name;
  }
  return object.GetName();
}

}

void WorldPresentationPublisher::ApplyEquipmentPresentation(
    const game::EquipmentPresentation &presentation) {

  auto &cache = projection_cache_[presentation.owner];
  cache.equipment = presentation;
  cache.equipment_present = true;
}

ObjectRenderPresentationSnapshot &WorldPresentationPublisher::PublishObjects(
    const game::ObjectManager &objects, const game::ObjectPresentationSnapshot &presentation,
    const game::TransportManager &transports, const game::WorldSession &world_session) {
  ObjectRenderPresentationSnapshot &projected = scratch_snapshot_;

  projected.retired.clear();
  projected.publication_generation = presentation.publication_generation;
  projected.local_player = presentation.local_player;
  projected.retired.reserve(presentation.retired.size());
  for (const auto &retired : presentation.retired) {
    projected.retired.push_back(retired.handle);
    ForgetProjectionCache(retired.handle, retired.presentation_slot);
  }

  const std::int32_t violence_level = game::GetClientViolenceLevel();

  publish_gathered_objects_.clear();
  publish_gathered_objects_.reserve(presentation.active.size());
  for (const auto &record : presentation.active) {
    publish_gathered_objects_.push_back(objects.Get(record.handle.guid));
  }

  publish_targets_.clear();
  publish_targets_.reserve(publish_gathered_objects_.size());
  for (std::size_t index = 0u; index < publish_gathered_objects_.size(); ++index) {
    memory::PrefetchAheadForRead(publish_gathered_objects_.data(), index,
                                 publish_gathered_objects_.size());
    const auto *const object = publish_gathered_objects_[index];
    if (object == nullptr) {
      continue;
    }
    switch (object->GetTypeId()) {
    case game::TypeID::kUnit:
    case game::TypeID::kPlayer:
    case game::TypeID::kGameObject:
    case game::TypeID::kDynamicObject:
    case game::TypeID::kCorpse:
      break;
    default:
      continue;
    }
    const auto &record = presentation.active[index];

    publish_targets_.push_back({
        .handle = record.handle,
        .presentation_slot = record.presentation_slot,
        .object = object,
        .cache = &ResolveProjectionCache(record.handle, record.presentation_slot),
    });
  }

  projected.active.resize(publish_targets_.size());
  for (std::size_t index = 0u; index < publish_targets_.size(); ++index) {
    if (const std::size_t ahead = index + 2u; ahead < publish_targets_.size()) {
      memory::PrefetchForRead(publish_targets_[ahead].object);
      memory::PrefetchForRead(publish_targets_[ahead].cache);
    }
    const auto &target = publish_targets_[index];
    auto &slot = projected.active[index];
    slot = kPristineProjection;
    ProjectObject(target.handle, *target.object, transports, world_session, *target.cache,
                  violence_level, slot);
    slot.presentation_slot = target.presentation_slot;
  }
  return projected;
}

WorldPresentationPublisher::CachedProjection &WorldPresentationPublisher::ResolveProjectionCache(
    const game::ObjectHandle handle, const std::uint32_t presentation_slot) {
  if (presentation_slot == game::kNoPresentationSlot) {
    return projection_cache_[handle];
  }
  if (presentation_slot >= projection_cache_slots_.size()) {
    projection_cache_slots_.resize(static_cast<std::size_t>(presentation_slot) + 1u);
  }
  auto &slot = projection_cache_slots_[presentation_slot];
  if (slot.cache == nullptr || slot.handle != handle) {

    slot.cache = &projection_cache_[handle];
    slot.handle = handle;
  }
  return *slot.cache;
}

void WorldPresentationPublisher::ForgetProjectionCache(const game::ObjectHandle handle,
                                                       const std::uint32_t presentation_slot) {
  projection_cache_.erase(handle);
  if (presentation_slot < projection_cache_slots_.size() &&
      projection_cache_slots_[presentation_slot].handle == handle) {
    projection_cache_slots_[presentation_slot] = {};
  }
}

void WorldPresentationPublisher::ProjectObject(const game::ObjectHandle handle,
                                               const game::WorldObject &object,
                                               const game::TransportManager &transports,
                                               const game::WorldSession &world_session,
                                               ObjectProjection &instance) {

  ProjectObject(handle, object, transports, world_session, projection_cache_[handle],
                game::GetClientViolenceLevel(), instance);
}

void WorldPresentationPublisher::ProjectObject(const game::ObjectHandle handle,
                                               const game::WorldObject &object,
                                               const game::TransportManager &transports,
                                               const game::WorldSession &world_session,
                                               CachedProjection &cache,
                                               const std::int32_t violence_level,
                                               ObjectProjection &instance) {
  instance.handle = handle;

  instance.type_id = object.GetTypeId();
  instance.display_id = ResolveDisplayId(object);
  instance.scale = object.GetScale() > 0.0f ? object.GetScale() : 1.0f;
  instance.visible =
      !object.IsGameObject() ||
      static_cast<const game::CGGameObject_C &>(object).IsVisibleForCurrentInstanceDifficulty(
          world_session);

  const bool mounted_display_model =
      object.IsUnit() &&
      static_cast<const game::CGUnit_C &>(object).Mount().CachedDisplayForSpell() != 0u;
  instance.alpha = std::clamp(mounted_display_model
                                  ? object.GetRenderedOpacityWithoutMaster()
                                  : object.GetEffectiveRenderOpacity(),
                              0.0f, 1.0f);
  const auto tint = object.GetModelTintColor();
  instance.tint_color = {tint.r, tint.g, tint.b, 1.0f};

  if (object.IsCorpse()) {
    const auto &corpse = static_cast<const game::CGCorpse_C &>(object);
    const auto &visual = corpse.GetCorpseVisualState();
    instance.corpse_visual_sync_serial = visual.sync_serial;
    instance.corpse_render_flags = visual.render_flags;
    instance.corpse_model_path = visual.model_path;
    if (visual.loot_sparkle_active) {
      render::WeaponAttachmentVisual sparkle;
      sparkle.attachment_id = game::kCorpseLootSparkleAttachmentId;
      sparkle.model_path =
          openwow::data::m2::NormalizeModelPath(visual.loot_sparkle_effect_path);
      if (!sparkle.model_path.empty()) {
        instance.model_attachments.push_back({
            .role = render::ModelAttachmentRole::kCorpseLootSparkle,
            .visual = std::make_shared<const render::WeaponAttachmentVisual>(
                std::move(sparkle)),
        });
      }
    }
    instance.corpse_creature_texture_replacement =
        visual.model_kind == game::CorpseVisualModelKind::kCreatureTextureReplacement;
  } else if (object.IsDynamicObject()) {
    const auto &dynamic_object = static_cast<const game::CGDynamicObject_C &>(object);
    instance.dynamic_object_type = dynamic_object.GetDynObjType();
    instance.dynamic_object_radius = dynamic_object.GetRadius();
    instance.dynamic_object_static_model = dynamic_object.HasStaticModelFlag();
    if (dbc_ != nullptr) {

      const auto spell_id = dynamic_object.GetSpellId();
      if (!cache.dynamic_object_valid || cache.dynamic_object_spell_id != spell_id ||
          cache.dynamic_object_violence_level != violence_level) {
        cache.dynamic_object_visual = dynamic_object.ResolveVisualState(*dbc_, violence_level);
        cache.dynamic_object_spell_id = spell_id;
        cache.dynamic_object_violence_level = violence_level;
        cache.dynamic_object_valid = true;
      }
      instance.dynamic_object_visual = cache.dynamic_object_visual;
    }
  }

  SyncTransform(instance, object, transports);
  SelectAnimation(instance, object);
  if (object.IsGameObject()) {
    const auto &game_object = static_cast<const game::CGGameObject_C &>(object);
    SyncGameObjectAnimation(instance, game_object);
    SyncGameObjectArtKit(instance, game_object, cache);

    instance.game_object_collision_state = game_object.GetInteractionValue(0u);
    if (game_object.IsDestructibleBuilding()) {
      const auto &visual = game_object.GetDestructibleVisualControlState();
      if (visual.active_state_index < visual.states.size()) {
        instance.has_destructible_area_scene_states = true;
        instance.destructible_area_scene_active_state = visual.active_state_index;
        instance.destructible_area_scene_previous_state =
            visual.previous_active_state_index;
        instance.destructible_rebuild_effect_display_id =
            visual.rebuild_effect_display_id;
        instance.destructible_rebuild_transition_mode =
            visual.rebuild_transition_mode;
        instance.destructible_rebuild_transition_speed =
            visual.rebuild_transition_speed;
        instance.destructible_visual_sync_serial = visual.sync_serial;
        for (std::size_t index = 0u; index < visual.states.size(); ++index) {
          const auto &source = visual.states[index];
          instance.destructible_area_scene_states[index] = {
              .display_id = source.render_display_id,
              .destruction_or_init_doodad_set = source.destruction_or_init_doodad_set,
              .impact_effect_doodad_set = source.impact_effect_doodad_set,
              .ambient_doodad_set = source.ambient_doodad_set,
              .additional_doodad_sets = {
                  0u,
                  0u,
                  source.ambient_doodad_set,
              },
          };
        }
        const auto &state = visual.states[visual.active_state_index];
        instance.area_scene_additional_doodad_sets = {
            0u,
            0u,
            state.ambient_doodad_set,
        };
      }
    }
  }

  equipment_scratch_ = {};
  equipment_scratch_assembled_ = false;
  SyncEquipment(object, cache);
  SyncCharacterAppearance(instance, object, world_session, cache);
  if (equipment_scratch_assembled_) {
    BuildEquipmentAttachments(instance, cache);
  }
  StampEquipmentSyncSerial(instance, cache);
  SyncQuestOverlay(instance, object);

  instance.needs_display_resolve = true;
  instance.needs_model_load = true;
}

void WorldPresentationPublisher::SyncQuestOverlay(
    ObjectProjection &instance, const game::WorldObject &object) const {
  if (!object.IsOverlayModelVisible() || !object.IsOverlayBoneAttached()) {
    return;
  }
  const auto path = render::ResolveQuestOverlayModelPath(
      object.GetActiveOverlayModelIndex());
  if (path.empty()) {
    return;
  }

  constexpr std::uint32_t kPlayerNameAttachment = 0x12u;
  render::WeaponAttachmentVisual visual;
  visual.attachment_id = kPlayerNameAttachment;
  visual.model_path = path;
  instance.model_attachments.push_back({
      .role = render::ModelAttachmentRole::kQuestOverlay,
      .visual = std::make_shared<const render::WeaponAttachmentVisual>(
          std::move(visual)),
      .scale = object.GetOverlayModelScale(),
      .animation_id = object.GetOverlayAnimationId(),
  });
}

void WorldPresentationPublisher::SyncTransform(
    ObjectProjection &instance, const game::WorldObject &object,
    const game::TransportManager &transports) const {

  const auto world_position = object.GetPosition();
  instance.position[0] = world_position.x;
  instance.position[1] = world_position.y;
  instance.position[2] = world_position.z;
  instance.orientation = object.GetWorldFacing();
  if (object.IsUnit()) {

    instance.orientation =
        static_cast<const game::CGUnit_C &>(object).Movement().WorldSmoothBodyFacing();
  }

  if (object.IsGameObject()) {
    const auto &game_object = static_cast<const game::CGGameObject_C &>(object);
    if (game_object.IsAnyTransport()) {
      if (const auto *transport = transports.GetTransport(object.GetGuid())) {
        const auto position = transport->InterpolatePosition();
        instance.position[0] = position.x;
        instance.position[1] = position.y;
        instance.position[2] = position.z;
        instance.orientation = transport->GetFacing();
      }
    }
  }

  if (object.GetVisualModelWorldTransform(instance.world_transform.data())) {
    instance.position[0] = instance.world_transform[12];
    instance.position[1] = instance.world_transform[13];
    instance.position[2] = instance.world_transform[14];
    instance.has_explicit_world_transform = true;
  }

}

void WorldPresentationPublisher::SyncGameObjectAnimation(ObjectProjection &instance,
                                                         const game::CGGameObject_C &game_object) {
  const auto &source = game_object.GetM2GoAnimationControlState();
  auto &target = instance.game_object_m2_animation;
  target.active = source.uses_direct_animation_id ||
                  (source.state_index != game::CGGameObject_C::kGoAnimStateInvalid &&
                   source.animation_id != 0u);
  target.state_index = source.state_index;
  target.previous_state_index = source.previous_state_index;
  target.animation_id = source.animation_id;
  target.uses_direct_animation_id = source.uses_direct_animation_id;
  target.direct_animation_id = source.direct_animation_id;
  target.looping = source.looping;
  target.use_sequence_repeat_count = source.use_sequence_repeat_count;
  target.playback_speed = source.playback_speed;
  target.sync_serial = source.sync_serial;
}

void WorldPresentationPublisher::SyncGameObjectArtKit(ObjectProjection &instance,
                                                      const game::CGGameObject_C &game_object,
                                                      CachedProjection &cache) {
  const auto &state = game_object.GetArtKitVisualControlState();

  instance.art_kit = state.art_kit;
  instance.art_kit_sync_serial = state.sync_serial;
  instance.art_kit_visuals_initialized = true;
  if (dbc_ == nullptr || state.art_kit == 0u) {
    return;
  }

  if (!cache.art_kit_valid || cache.art_kit != state.art_kit) {
    cache.art_kit_texture_paths = {};
    if (const auto *entry = dbc_->gameobject_art_kit().LookupEntry(state.art_kit)) {
      for (std::size_t index = 0; index < cache.art_kit_texture_paths.size(); ++index) {
        cache.art_kit_texture_paths[index] = std::string(entry->strings[index]);
      }
    }
    cache.art_kit = state.art_kit;
    cache.art_kit_valid = true;
  }
  instance.art_kit_texture_paths = cache.art_kit_texture_paths;
}

void WorldPresentationPublisher::SelectAnimation(ObjectProjection &instance,
                                                 const game::WorldObject &object) {
  if (object.IsDynamicObject() || object.IsGameObject()) {
    return;
  }
  if (object.IsCorpse()) {
    const auto &corpse = static_cast<const game::CGCorpse_C &>(object);

    instance.corpse_death_animation_id = corpse.GetCorpseDeathAnimId();
    return;
  }
  auto flags = object.GetMovementInfo().flags;
  const auto &unit = static_cast<const game::CGUnit_C &>(object);
  if (unit.Movement().HasActiveSplineLocomotion()) {

    flags |= unit.Movement().IsSplineLocomotionBackward() ? game::kMoveFlagBackward
                                                          : game::kMoveFlagForward;
  }
  instance.movement_flags = flags;

  instance.is_mounted = object.GetUInt32(game::UNIT_FIELD_MOUNTDISPLAYID) != 0u;

  instance.locomotion_speed = unit.Movement().ComputeCurrentSpeed();

  const auto &request = unit.Animation().GetPlaybackRequest();
  const auto resolved_animation_id =
      unit.Animation().GetResolvedPlaybackAnimationId();
  const auto resolved_base_animation_id =
      unit.Animation().GetResolvedBasePlaybackAnimationId();

  const bool upper_body_only = request.upper_body_only &&
                               resolved_base_animation_id != kNoAnimationRow;
  instance.unit_animation = {
      .animation_id = request.animation_id,
      .resolved_animation_id = resolved_animation_id,
      .looping = request.looping,
      .serial = request.serial,
      .resolved_base_animation_id =
          upper_body_only ? resolved_base_animation_id : resolved_animation_id,
      .base_looping = upper_body_only ? request.base_looping : request.looping,
      .upper_body_only = upper_body_only,

      .zero_blend = request.zero_blend,
  };
}

WorldPresentationPublisher::EquipmentSource
WorldPresentationPublisher::SelectEquipmentSource(
    const game::TypeID type_id, const bool has_inventory_replica_block) noexcept {

  switch (type_id) {
  case game::TypeID::kPlayer:
    return has_inventory_replica_block ? EquipmentSource::kInventoryReplica
                                       : EquipmentSource::kVisibleItemDescriptors;
  case game::TypeID::kUnit:

    return has_inventory_replica_block ? EquipmentSource::kInventoryReplica
                                       : EquipmentSource::kVirtualItemDescriptors;
  case game::TypeID::kCorpse:
    return EquipmentSource::kCorpse;
  default:
    return EquipmentSource::kNone;
  }
}

void WorldPresentationPublisher::SyncEquipment(const game::WorldObject &object,
                                               CachedProjection &cache) {
  switch (SelectEquipmentSource(object.GetTypeId(), cache.equipment_present)) {
  case EquipmentSource::kNone:

    return;
  case EquipmentSource::kCorpse:

    SyncCorpseEquipment(static_cast<const game::CGCorpse_C &>(object));
    equipment_scratch_assembled_ = true;
    return;
  default:
    break;
  }

  const auto source =
      SelectEquipmentSource(object.GetTypeId(), cache.equipment_present);

  const auto &unit = static_cast<const game::CGUnit_C &>(object);
  equipment_scratch_.race = unit.State().GetRace();
  equipment_scratch_.gender = unit.State().GetGender();
  equipment_scratch_.sheathe_state =
      static_cast<std::uint8_t>(std::clamp(unit.Animation().GetCachedSheatheState(), 0, 2));
  if (source == EquipmentSource::kInventoryReplica) {
    for (std::size_t slot = 0; slot < equipment_scratch_.items.size(); ++slot) {
      const auto &replica_slot = cache.equipment.slots[slot];
      equipment_scratch_.items[slot] = {
          .display_id = replica_slot.display_id,
          .item_visuals_id = replica_slot.item_visual,
          .inventory_type = replica_slot.inventory_type,
          .sheathe_type = replica_slot.sheath_type,
          .item_visuals_enabled = replica_slot.item_visual != 0u,
      };
    }
  } else if (source == EquipmentSource::kVisibleItemDescriptors) {
    SyncVisibleItemEquipment(static_cast<const game::CGPlayer_C &>(object), cache);
    equipment_scratch_.items = cache.visible_item_equipment;
  } else {
    SyncVirtualItemWeapons(unit, cache);
    equipment_scratch_.items = cache.virtual_item_weapons;
  }

  equipment_scratch_assembled_ = true;
}

void WorldPresentationPublisher::SyncCorpseEquipment(const game::CGCorpse_C &corpse) {

  const auto &visual = corpse.GetCorpseVisualState();
  if (visual.model_kind != game::CorpseVisualModelKind::kCharacter) {
    return;
  }
  equipment_scratch_.race = corpse.GetRace();
  equipment_scratch_.gender = corpse.GetGender();

  equipment_scratch_.sheathe_state = 0u;
  for (std::size_t index = 0u; index < visual.equipment_count; ++index) {
    const auto &source = visual.equipment[index];
    if (source.slot >= equipment_scratch_.items.size()) {
      continue;
    }
    auto &output = equipment_scratch_.items[source.slot];
    output.display_id = source.item_display_id;
    output.inventory_type = source.inventory_type;
    if (dbc_ != nullptr) {
      if (const auto *const display =
              dbc_->item_display_info().LookupEntry(source.item_display_id);
          display != nullptr) {
        output.item_visuals_id = display->item_visuals_id;
      }
    }
    output.item_visuals_enabled = output.item_visuals_id != 0u;
  }
}

void WorldPresentationPublisher::SyncVirtualItemWeapons(const game::CGUnit_C &unit,
                                                        CachedProjection &cache) {

  static constexpr std::array<std::size_t, 3> kVirtualItemEquipmentSlots{
      render::kSlotMainHand, render::kSlotOffHand, render::kSlotRanged};

  std::array<std::uint32_t, kVirtualItemEquipmentSlots.size()> entries{};
  for (std::size_t slot = 0u; slot < entries.size(); ++slot) {
    entries[slot] = unit.State().GetVirtualItemSlotEntry(static_cast<std::uint8_t>(slot));
  }
  if (cache.virtual_item_weapons_valid && cache.virtual_item_entries == entries) {
    return;
  }
  cache.virtual_item_entries = entries;
  cache.virtual_item_weapons = {};
  cache.virtual_item_weapons_valid = true;
  if (dbc_ == nullptr) {
    return;
  }
  for (std::size_t slot = 0u; slot < entries.size(); ++slot) {
    if (entries[slot] == 0u) {
      continue;
    }
    const auto *const item = dbc_->item().LookupEntry(entries[slot]);
    if (item == nullptr || item->display_info_id == 0u) {
      continue;
    }
    auto &output = cache.virtual_item_weapons[kVirtualItemEquipmentSlots[slot]];
    output.display_id = item->display_info_id;
    output.inventory_type = static_cast<std::uint8_t>(item->inventory_type);
    output.sheathe_type = static_cast<std::uint8_t>(item->sheathe_type);
    output.class_id = item->class_id;
    output.subclass_id = item->subclass_id;
    if (const auto *const display =
            dbc_->item_display_info().LookupEntry(item->display_info_id);
        display != nullptr) {
      output.item_visuals_id = display->item_visuals_id;
    }
    output.item_visuals_enabled = output.item_visuals_id != 0u;
  }
}

void WorldPresentationPublisher::SyncVisibleItemEquipment(const game::CGPlayer_C &player,
                                                          CachedProjection &cache) {

  std::array<std::uint32_t, render::kMaxEquipSlot> entries{};
  for (std::size_t slot = 0u; slot < entries.size(); ++slot) {
    entries[slot] = player.GetVisibleItemEntry(static_cast<std::uint8_t>(slot));
  }
  if (cache.visible_item_equipment_valid && cache.visible_item_equipment_resolved &&
      cache.visible_item_entries == entries) {
    return;
  }

  cache.visible_item_entries = entries;
  cache.visible_item_equipment = {};
  bool resolved = true;
  for (std::size_t slot = 0u; slot < entries.size(); ++slot) {
    if (entries[slot] == 0u) {
      continue;
    }
    const auto metadata =
        player.GetVisibleItemTemplateMetadata(static_cast<std::uint8_t>(slot));
    if (!metadata.has_value()) {

      resolved = false;
      continue;
    }
    auto &output = cache.visible_item_equipment[slot];
    output.display_id = metadata->display_id;
    output.inventory_type = static_cast<std::uint8_t>(metadata->inventory_type);
    output.sheathe_type = static_cast<std::uint8_t>(metadata->sheath);
    if (dbc_ != nullptr) {
      if (const auto *const display =
              dbc_->item_display_info().LookupEntry(metadata->display_id);
          display != nullptr) {
        output.item_visuals_id = display->item_visuals_id;
      }
    }
    output.item_visuals_enabled = output.item_visuals_id != 0u;
  }
  cache.visible_item_equipment_valid = true;
  cache.visible_item_equipment_resolved = resolved;
}

void WorldPresentationPublisher::BuildEquipmentAttachments(ObjectProjection &instance,
                                                           CachedProjection &cache) {
  std::erase_if(instance.model_attachments, [](const auto& spec) {
    return render::IsEquipmentAttachmentRole(spec.role);
  });

  if (!cache.equipment_visuals_valid ||
      cache.equipment_visual_inputs != equipment_scratch_) {
    cache.equipment_visual_inputs = equipment_scratch_;
    cache.equipment_visuals = equipment_renderer_.ComputeVisuals(
        equipment_scratch_.race, equipment_scratch_.gender,
        equipment_scratch_.items, equipment_scratch_.sheathe_state);
    cache.equipment_visuals_valid = true;
    cache.equipment_attachment_specs.clear();
    const auto &computed = cache.equipment_visuals;
    const auto memoize = [&cache](const render::ModelAttachmentRole role,
                                  const auto &visual) {
      if (visual.has_value() && !visual->model_path.empty()) {
        cache.equipment_attachment_specs.push_back(
            {.role = role,
             .visual = std::make_shared<const render::WeaponAttachmentVisual>(
                 *visual)});
      }
    };

    memoize(render::ModelAttachmentRole::kMainHand, computed.main_hand_attachment);
    memoize(render::ModelAttachmentRole::kOffHand, computed.off_hand_attachment);
    memoize(render::ModelAttachmentRole::kRanged, computed.ranged_attachment);

    memoize(render::ModelAttachmentRole::kRightShoulder,
            computed.right_shoulder_attachment);
    memoize(render::ModelAttachmentRole::kLeftShoulder,
            computed.left_shoulder_attachment);
    memoize(render::ModelAttachmentRole::kHelm, computed.helm_attachment);
    memoize(render::ModelAttachmentRole::kCape, computed.cape_attachment);
  }
  instance.model_attachments.reserve(instance.model_attachments.size() +
                                     cache.equipment_attachment_specs.size());
  for (const auto &spec : cache.equipment_attachment_specs) {
    instance.model_attachments.push_back(spec);
  }
}

void WorldPresentationPublisher::StampEquipmentSyncSerial(ObjectProjection &instance,
                                                          CachedProjection &cache) {

  if (cache.published_equipment == equipment_scratch_) {
    instance.equipment_sync_serial = cache.equipment_sync_serial;
    return;
  }
  cache.published_equipment = equipment_scratch_;
  cache.equipment_sync_serial = ++equipment_publication_serial_;
  instance.equipment_sync_serial = cache.equipment_sync_serial;
}

void WorldPresentationPublisher::SyncCharacterAppearance(ObjectProjection &instance,
                                                         const game::WorldObject &object,
                                                         const WorldSession &world_session,
                                                         CachedProjection &cache) {
  if (dbc_ == nullptr) {
    return;
  }
  if (object.IsCorpse()) {
    SyncCorpseCharacterAppearance(instance, static_cast<const game::CGCorpse_C &>(object),
                                  cache);
    return;
  }
  if (!object.IsUnit()) {
    return;
  }

  const bool is_player = object.IsPlayer();
  bool display_facts_rebuilt = false;
  if (!cache.display_facts_valid || cache.display_id != instance.display_id ||
      cache.is_player != is_player) {
    cache.display_id = instance.display_id;
    cache.is_player = is_player;
    cache.component_source = CharacterComponentSource::kNone;
    cache.npc_selection = {};
    cache.npc_prebaked_body_texture.clear();
    cache.display_facts_valid = true;
    display_facts_rebuilt = true;

    const auto *const display = dbc_->creature_display_info().LookupEntry(instance.display_id);
    const auto *const model =
        display != nullptr ? dbc_->creature_model_data().LookupEntry(display->model_id) : nullptr;
    if (model != nullptr && (model->flags & kCharacterComponentModelFlag) != 0u) {
      if (is_player) {
        cache.component_source = CharacterComponentSource::kPlayer;
      } else {
        const auto *const extra =
            display->extra_info != 0u
                ? dbc_->creature_display_info_extra().LookupEntry(display->extra_info)
                : nullptr;
        if (extra != nullptr) {
          cache.component_source = CharacterComponentSource::kNpc;
          cache.npc_selection = {
              .race = static_cast<std::uint8_t>(extra->display_race_id),
              .gender = static_cast<std::uint8_t>(extra->display_sex_id),
              .skin_color = static_cast<std::uint8_t>(extra->skin_id),
              .face = static_cast<std::uint8_t>(extra->face_id),
              .hair_style = static_cast<std::uint8_t>(extra->hair_style_id),
              .hair_color = static_cast<std::uint8_t>(extra->hair_color_id),
              .facial_hair = static_cast<std::uint8_t>(extra->facial_hair_id),
          };
          for (std::size_t index = 0u; index < extra->item_display.size(); ++index) {
            cache.npc_selection.equipment_display_ids[kNpcItemDisplayEquipmentSlots[index]] =
                extra->item_display[index];
          }
          cache.npc_prebaked_body_texture = BuildNpcBakedTexturePath(extra->bake_name);
        }
      }
    }
  }
  if (cache.component_source == CharacterComponentSource::kNone) {
    return;
  }

  render::CharacterAppearanceSelection selection;
  std::optional<GuildEmblem> guild_tabard_emblem;
  if (cache.component_source == CharacterComponentSource::kPlayer) {
    const auto &player = static_cast<const game::CGPlayer_C &>(object);
    selection = {
        .race = player.State().GetRace(),
        .gender = player.State().GetGender(),

        .class_id = player.State().GetClass(),
        .skin_color = player.GetSkinColor(),
        .face = player.GetFace(),
        .hair_style = player.GetHairStyle(),
        .hair_color = player.GetHairColor(),
        .facial_hair = player.GetFacialHair(),
    };
    for (std::size_t slot = 0u; slot < selection.equipment_display_ids.size(); ++slot) {
      selection.equipment_display_ids[slot] = equipment_scratch_.items[slot].display_id;
    }
    if (const auto* const guild_info =
            world_session.guild().FindCachedGuildInfo(player.GetGuildID());
        guild_info != nullptr && HasResolvedGuildEmblem(guild_info->emblem)) {
      guild_tabard_emblem = guild_info->emblem;
    }
  } else {

    selection = cache.npc_selection;
    for (std::size_t index = 0u; index < kNpcItemDisplayEquipmentSlots.size(); ++index) {
      const auto slot = kNpcItemDisplayEquipmentSlots[index];
      if (equipment_scratch_.items[slot].display_id == 0u) {
        equipment_scratch_.items[slot].display_id = selection.equipment_display_ids[slot];
      }
    }
    equipment_scratch_.race = selection.race;
    equipment_scratch_.gender = selection.gender;
    instance.character_prebaked_body_texture = cache.npc_prebaked_body_texture;
  }

  PublishCharacterAppearanceSelection(instance, cache, selection,
                                      guild_tabard_emblem, display_facts_rebuilt);
}

void WorldPresentationPublisher::PublishCharacterAppearanceSelection(
    ObjectProjection &instance, CachedProjection &cache,
    const render::CharacterAppearanceSelection &selection,
    const std::optional<GuildEmblem> &guild_tabard_emblem,
    const bool sources_invalidated) {
  if (!cache.appearance_valid || sources_invalidated ||
      cache.selection != selection ||
      cache.guild_tabard_emblem != guild_tabard_emblem) {
    cache.selection = selection;
    cache.guild_tabard_emblem = guild_tabard_emblem;

    auto texture_sources =
        render::BuildCharacterAppearanceTextureSources(selection, dbc_);
    texture_sources.guild_tabard_emblem = guild_tabard_emblem;
    cache.texture_sources =
        std::make_shared<const render::CharacterAppearanceTextureSources>(
            std::move(texture_sources));
    cache.geosets = render::BuildCharacterAppearanceGeosetState(
        selection, {
                       .hair_geosets = &dbc_->char_hair_geosets(),
                       .facial_hair_styles =
                           &dbc_->character_facial_hair_styles(),
                       .item_display_info = &dbc_->item_display_info(),
                       .helmet_geoset_vis_data =
                           &dbc_->helmet_geoset_vis_data(),
                       .char_sections = &dbc_->char_sections(),
                   });
    cache.appearance_key = std::make_shared<const std::string>(
        !instance.character_prebaked_body_texture.empty()
            ? std::string("npc-baked:") + instance.character_prebaked_body_texture
            : render::BuildCharacterAppearanceTextureCacheKey(*cache.texture_sources));
    cache.appearance_valid = true;
  }

  if (selection.race == 0u) {
    return;
  }

  instance.character_appearance_sources = cache.texture_sources;
  instance.character_appearance_geosets = cache.geosets;
  instance.character_appearance_key = cache.appearance_key;
  instance.character_appearance_declared = true;
  instance.character_appearance_selection_initialized = true;

}

void WorldPresentationPublisher::SyncCorpseCharacterAppearance(
    ObjectProjection &instance, const game::CGCorpse_C &corpse, CachedProjection &cache) {

  const auto &visual = corpse.GetCorpseVisualState();
  if (visual.model_kind != game::CorpseVisualModelKind::kCharacter) {
    return;
  }

  render::CharacterAppearanceSelection selection{
      .race = corpse.GetRace(),
      .gender = corpse.GetGender(),

      .class_id = 0u,
      .skin_color = corpse.GetSkinColor(),
      .face = corpse.GetFace(),
      .hair_style = corpse.GetHairStyle(),
      .hair_color = corpse.GetHairColor(),
      .facial_hair = corpse.GetFacialHair(),
  };

  for (std::size_t slot = 0u; slot < selection.equipment_display_ids.size(); ++slot) {
    selection.equipment_display_ids[slot] = equipment_scratch_.items[slot].display_id;
  }

  PublishCharacterAppearanceSelection(instance, cache, selection,
                                      visual.guild_tabard_emblem,
                                      false);
}

void WorldPresentationPublisher::GatherOverheadUnits(const game::ObjectManager &objects) {
  overhead_units_scratch_.clear();
  objects.ForEach([this](const game::WorldObject &object) {

    if (!object.IsUnit() || object.GetGuid().IsEmpty()) {
      return;
    }
    overhead_units_scratch_.push_back(static_cast<const game::CGUnit_C *>(&object));
  });
}

WorldPresentationPublisher::OverheadTextPresentation
WorldPresentationPublisher::PublishOverheadText(
    const game::ObjectManager &objects, game::WorldSession &world_session,
    const std::uint64_t target_guid, const std::uint64_t mouseover_guid,
    const bool show_world_nameplates, const bool show_class_color_in_nameplate,
    const float player_x, const float player_y, const float player_z) {
  GatherOverheadUnits(objects);
  OverheadTextPresentation result;
  result.nameplates = PublishNameplatesFromUnits(
      overhead_units_scratch_, objects, world_session, target_guid, mouseover_guid,
      show_world_nameplates, show_class_color_in_nameplate, player_x, player_y, player_z);

  result.unit_names = PublishUnitNamesFromUnits(
      overhead_units_scratch_, objects, world_session, target_guid, show_world_nameplates,
      result.nameplates.nameplates, core::GameClock::GetTickCount32());
  return result;
}

NameplatePresentationSnapshot WorldPresentationPublisher::PublishNameplates(
    const game::ObjectManager &objects, game::WorldSession &world_session,
    const std::uint64_t target_guid, const std::uint64_t mouseover_guid,
    const bool show_world_nameplates,
    const bool show_class_color_in_nameplate, const float player_x, const float player_y,
    const float player_z) {
  GatherOverheadUnits(objects);
  return PublishNameplatesFromUnits(overhead_units_scratch_, objects, world_session, target_guid,
                                    mouseover_guid, show_world_nameplates,
                                    show_class_color_in_nameplate, player_x, player_y, player_z);
}

NameplatePresentationSnapshot WorldPresentationPublisher::PublishNameplatesFromUnits(
    const std::span<const game::CGUnit_C *const> units,
    const game::ObjectManager &objects, game::WorldSession &world_session,
    const std::uint64_t target_guid, const std::uint64_t mouseover_guid,
    const bool show_world_nameplates,
    const bool show_class_color_in_nameplate, const float player_x, const float player_y,
    const float player_z) {
  NameplatePresentationSnapshot result;
  result.target = game::ObjectGuid(target_guid);
  const auto *player = objects.GetActivePlayer();

  const auto *const map_entry =
      world_session.LookupMapEntry(world_session.current_map_id());
  const bool threat_warning_enabled =
      openwow::ui::game::ThreatWarningState::Get().IsEnabled();

  const auto &cvars = openwow::ui::game::CVarSystem::Instance();
  const bool show_nameplate_cast_bar =
      !cvars.Exists("showVKeyCastbar") || cvars.GetCVarBool("showVKeyCastbar");
  const bool range_exempt_map =
      map_entry != nullptr &&
      map_entry->map_type ==
          static_cast<std::uint32_t>(data::dbc::MapType::kArena);

  const auto now_ms =
      static_cast<std::uint64_t>(core::GameClock::GetTickCount32());
  const auto local_player_guid = objects.GetLocalPlayerGuid();
  for (std::size_t unit_index = 0u; unit_index < units.size(); ++unit_index) {

    memory::PrefetchAheadForRead(units.data(), unit_index, units.size());
    const auto *const unit = units[unit_index];
    const auto &object = static_cast<const game::WorldObject &>(*unit);
    if (object.GetGuid() == local_player_guid) {
      continue;
    }

    if (player == nullptr || !show_world_nameplates) {
      continue;
    }

    const float dx = object.GetX() - player_x;
    const float dy = object.GetY() - player_y;
    const float dz = object.GetZ() - player_z;
    const float distance_squared = dx * dx + dy * dy + dz * dz;

    if (!unit->Nameplate().PassesRange(distance_squared, range_exempt_map) ||
        !unit->Nameplate().PassesHardEligibility(*unit, *player) ||
        !unit->Nameplate().PassesCvarVisibility(*unit, *player, objects)) {
      continue;
    }

    const game::CreatureTemplateInfo *creature_template = nullptr;
    if (!unit->IsPlayer() && unit->GetEntry() != 0u) {
      creature_template = world_session.query_cache().GetOrRequestCreatureTemplate(
          unit->GetEntry(), unit->GetGuid().GetRawValue());
    }

    if (creature_template != nullptr &&
        (creature_template->type_flags & kCreatureTypeFlagHideNameplate) != 0u) {
      continue;
    }

    const auto anchor = unit->GetNamePlatePosition();
    NameplateInfo plate;
    plate.guid = object.GetGuid().GetRawValue();
    plate.world_x = anchor.x;
    plate.world_y = anchor.y;
    plate.world_z = anchor.z;
    if (object.HasObjectBoundingBox()) {
      object.GetObjectBoundingBox(plate.world_bounds.data());
      plate.has_world_bounds = true;
    }
    plate.name = ResolveName(object, *unit, world_session, creature_template);
    plate.is_player = unit->IsPlayer();
    plate.is_npc = !plate.is_player;
    plate.is_target = plate.guid == target_guid;
    plate.is_mouseover = plate.guid == mouseover_guid;
    plate.frame_alpha = NameplateRenderer::ResolveFrameAlpha(target_guid != 0u, plate.is_target);
    plate.raid_target_icon_index =
        GroupSystem::Get().GetRaidTargetIndex(object.GetGuid().GetRawValue());
    plate.name_color_argb = plate.is_mouseover ? kNameColorMouseoverArgb : kNameColorWhiteArgb;
    if (game::NameplateDamageFlashState::Get().IsActive(object.GetGuid())) {
      plate.name_color_argb = game::NameplateDamageFlashState::kFlashColorArgb;
    }
    plate.health_pct = unit->State().GetMaxHealth() == 0u
                           ? 1.0f
                           : std::clamp(static_cast<float>(unit->State().GetHealth()) /
                                            static_cast<float>(unit->State().GetMaxHealth()),
                                        0.0f, 1.0f);
    plate.is_dead = unit->State().GetHealth() == 0u;
    auto reaction = game::ReactionType::kNeutral;
    if (player != nullptr) {
      reaction = unit->Interaction().GetReaction(*player);
    }

    const bool trivial_level =
        !plate.is_player && player != nullptr &&
        game::IsLevelTrivial(player->State().GetLevel(),
                             unit->State().GetLevel());
    const bool green_bucket =
        reaction > game::ReactionType::kNeutral || trivial_level;
    plate.reaction = reaction <= game::ReactionType::kHostile ? 0u
                     : green_bucket                           ? 2u
                                                              : 1u;
    plate.health_bar_color_argb = NameplateRenderer::ResolveHealthBarColorArgb(
        reaction, plate.is_player, trivial_level, unit->State().GetClass(),
        show_class_color_in_nameplate);
    const bool boss = creature_template != nullptr &&
                      (creature_template->type_flags & kCreatureTypeFlagBoss) != 0u;
    const auto rank = unit->State().GetClassificationRank();
    plate.show_elite = boss || rank == game::ClassificationRank::kElite ||
                       rank == game::ClassificationRank::kRareElite;
    plate.level = static_cast<std::uint8_t>(std::min(unit->State().GetLevel(), 255u));
    if (player != nullptr) {
      plate.level_color_argb =
          NameplateRenderer::ResolveLevelColorArgb(player->State().GetLevel(), plate.level);
      plate.show_level = NameplateRenderer::ShouldShowLevel(player->State().GetLevel(), plate.level,
                                                            reaction, boss);
    } else {
      plate.show_level = !boss;
    }
    plate.show_skull = !plate.show_level;

    if (player != nullptr) {
      game::ThreatQueryData threat;
      if (game::ThreatSystem::Get().TryGetThreatQueryData(object.GetGuid(), player->GetGuid(),
                                                          &threat)) {
        plate.threat_status =
            static_cast<std::uint8_t>(std::min<std::uint32_t>(threat.entry.threat_status + 1u, 4u));
        plate.threat_color_argb = NameplateRenderer::ResolveThreatColorArgb(plate.threat_status);

        plate.show_threat =
            threat_warning_enabled && plate.threat_status >= kMinFlashThreatStatus;
      }
    }

    const game::CastInfo *cast = nullptr;
    if (unit->Casts().IsCasting()) {
      cast = &unit->Casts().GetCurrentCast();
    } else if (unit->Casts().IsChanneling()) {
      cast = &unit->Casts().GetChannelCast();
    }
    if (cast != nullptr &&
        (!show_nameplate_cast_bar || HidesNameplateCastBar(*cast, dbc_))) {
      cast = nullptr;
    }
    if (cast != nullptr && cast->end_time > now_ms && cast->end_time > cast->start_time) {
      const auto duration = cast->end_time - cast->start_time;
      const auto elapsed =
          now_ms <= cast->start_time ? 0u : std::min(now_ms - cast->start_time, duration);
      const float progress = static_cast<float>(elapsed) / static_cast<float>(duration);
      plate.cast_pct = cast->is_channel ? 1.0f - progress : progress;
      plate.cast_alpha = 1.0f;
      plate.cast_name = ResolveCastName(*cast, dbc_);
      plate.cast_icon_texture = ResolveCastIcon(*cast, dbc_);
      plate.cast_not_interruptible = cast->not_interruptible;
    }
    result.nameplates.push_back(std::move(plate));
  }
  return result;
}

render::UnitNamePresentationSnapshot WorldPresentationPublisher::PublishUnitNames(
    const ObjectManager &objects, WorldSession &world_session,
    const std::uint64_t target_guid, const bool ui_visible,
    const std::vector<render::NameplateInfo> &visible_nameplates,
    const std::uint32_t now_ms) {
  GatherOverheadUnits(objects);
  return PublishUnitNamesFromUnits(overhead_units_scratch_, objects, world_session, target_guid,
                                   ui_visible, visible_nameplates, now_ms);
}

render::UnitNamePresentationSnapshot WorldPresentationPublisher::PublishUnitNamesFromUnits(
    const std::span<const game::CGUnit_C *const> units,
    const ObjectManager &objects, WorldSession &world_session,
    const std::uint64_t target_guid, const bool ui_visible,
    const std::vector<render::NameplateInfo> &visible_nameplates,
    const std::uint32_t now_ms) {
  render::UnitNamePresentationSnapshot result;
  const auto display_flags = PlayerName_GetDisplayFlags();
  const auto *const viewer = objects.GetActivePlayer();
  if (viewer == nullptr) {
    return result;
  }

  std::unordered_set<std::uint64_t> plated;
  plated.reserve(visible_nameplates.size());
  for (const auto &plate : visible_nameplates) {
    plated.insert(plate.guid);
  }

  const auto active_mover =
      world_session.player_control_runtime().ActiveMoverGuid().GetRawValue();

  for (std::size_t unit_index = 0u; unit_index < units.size(); ++unit_index) {

    memory::PrefetchAheadForRead(units.data(), unit_index, units.size());
    const auto *const unit = units[unit_index];

    if (unit->GetPrimaryM2InstanceId() == 0u) {
      continue;
    }

    const CreatureTemplateInfo *creature_template = nullptr;
    if (!unit->IsPlayer() && unit->GetEntry() != 0u) {
      creature_template = world_session.query_cache().GetOrRequestCreatureTemplate(
          unit->GetEntry(), unit->GetGuid().GetRawValue());
    }
    const auto raw_guid = unit->GetGuid().GetRawValue();

    UnitNameViewerRelation relation;
    if (!UnitName_ShouldRender(*unit, *viewer, objects, display_flags,
                               ui_visible, plated.contains(raw_guid),
                               target_guid, active_mover, relation)) {
      continue;
    }
    auto text = UnitName_BuildText(*unit, objects, world_session,
                                   display_flags, creature_template, dbc_);
    if (text.text.empty()) {
      continue;
    }

    render::UnitNameDrawEntry entry;
    entry.guid = raw_guid;
    const auto anchor = unit->GetNamePlatePosition();
    entry.world_x = anchor.x;
    entry.world_y = anchor.y;
    entry.world_z = anchor.z;
    entry.text = std::move(text.text);
    entry.lines = text.lines;
    entry.color_argb = UnitName_ResolveColor(*unit, *viewer, &world_session,
                                             target_guid, now_ms, relation);

    entry.scale = UnitName_ComputeScale(unit->GetModelBoundingBoxHeight() *
                                        unit->GetNativeScale());
    result.names.push_back(std::move(entry));
  }
  return result;
}

void WorldPresentationPublisher::PublishSpellVisuals(
    game::ObjectManager &objects, game::ObjectPresentationSnapshot &presentation) {
  std::uint64_t sequence = 0u;

  const auto find_active_record = [&presentation](const game::ObjectGuid guid)
      -> const game::ObjectPresentationRecord * {
    const auto found = std::lower_bound(
        presentation.active.begin(), presentation.active.end(),
        guid.GetRawValue(),
        [](const game::ObjectPresentationRecord &record,
           const std::uint64_t raw_guid) {
          return record.handle.guid.GetRawValue() < raw_guid;
        });
    return found != presentation.active.end() && found->handle.guid == guid
               ? &*found
               : nullptr;
  };

  objects.ForEachUnit([&](const game::ObjectGuid &guid, game::CGUnit_C &unit) {
    const auto *const handle = find_active_record(guid);
    if (handle == nullptr) {
      unit.SpellVisuals().ClearDispatches();
      return;
    }
    const auto owner_world_position = unit.GetPosition();
    for (const auto &dispatch : unit.SpellVisuals().Dispatches()) {

      if (!dispatch.missile.has_value() &&
          dispatch.effects.empty() && dispatch.sound_kit_id == 0u && dispatch.shake_id == 0u &&
          dispatch.lifecycle_action != SpellVisualLifecycleAction::kAuraStop &&
          dispatch.lifecycle_action != SpellVisualLifecycleAction::kCastStop &&
          dispatch.lifecycle_action != SpellVisualLifecycleAction::kChannelStop) {
        continue;
      }
      SpellVisualPresentationEvent event{
          .owner = handle->handle,
          .sequence = ++sequence,
          .action = dispatch.lifecycle_action,
          .phase = dispatch.phase,
          .dispatch_type = dispatch.dispatch_type,
          .raw_flags = dispatch.raw_flags,
          .aura_slot = dispatch.aura_slot,
          .spell_id = dispatch.spell_id,
          .spell_visual_id = dispatch.spell_visual_id,
          .kit_id = dispatch.kit_id,
          .sound_kit_id = dispatch.sound_kit_id,
          .camera_shake_id = dispatch.shake_id,

          .owner_position = {owner_world_position.x, owner_world_position.y,
                             owner_world_position.z},
          .world_position = dispatch.world_position,
          .missile = dispatch.missile,
          .missile_source_position = dispatch.missile_source_position,
          .missile_caster_guid = dispatch.missile_caster_guid,
          .missile_cast_count = dispatch.missile_cast_count,
          .missile_target_guid = dispatch.missile_target_guid,
          .missile_target_position = dispatch.missile_target_position,
          .missile_speed = dispatch.missile_speed,
          .missile_impact_result = dispatch.missile_impact_result,
          .missile_reflect_result = dispatch.missile_reflect_result,
          .deferred_impact_kit_id = dispatch.deferred_impact_kit_id,
      };
      event.effects.reserve(dispatch.effects.size());
      for (const auto &effect : dispatch.effects) {
        event.effects.push_back({
            .effect_name_id = effect.effect_name_id,
            .model_path = effect.model_path,
            .resource_scale = effect.resource_scale,
            .attachment_id = effect.attachment_id,
            .source_field_index = effect.source_field_index,
            .transform_key = effect.transform_key,
            .world_space = effect.world_space,
            .uses_explicit_world_position = effect.uses_explicit_world_position,
            .from_model_attach = effect.from_model_attach,
            .offset = effect.offset,
            .rotation = effect.rotation,
        });
      }
      presentation.spell_visual_events.push_back(std::move(event));
    }
    unit.SpellVisuals().ClearDispatches();
  });
}

}

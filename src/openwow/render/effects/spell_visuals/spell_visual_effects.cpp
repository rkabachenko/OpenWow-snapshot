#include "openwow/render/effects/spell_visuals/spell_visual_effects.h"

#include "openwow/game/spell_book.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/inventory/equipment/equipment_visual.h"
#include "openwow/game/inventory/equipment/item_equip_check.h"
#include "openwow/game/spell_missile.h"
#include "openwow/game/spell_visual.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace openwow::render {

namespace {

bool ResolveItemMissileModel(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::game::SpellVisualItemModelContext& context,
    const openwow::game::EquipmentSlot slot,
    ParticleEmitterConfig& config) {
  const auto* display = dbc.item_display_info().LookupEntry(context.display_id);
  if (display == nullptr) return false;

  const bool use_right_component = display->model_name_left.empty();
  const auto model_name = use_right_component ? display->model_name_right
                                               : display->model_name_left;
  if (model_name.empty()) return false;

  std::string model_path;
  if (context.inventory_type == static_cast<std::uint32_t>(
                                    openwow::game::InventoryType::Ammo)) {

    model_path = openwow::game::EquipmentVisualSystem::BuildAmmoModelPath(
        display->model_name_right);
  } else {
    openwow::game::ItemDisplayInfoEntry item_display;
    item_display.id = display->id;
    item_display.modelName[0] = model_name;
    item_display.modelTexture[0] =
        use_right_component ? display->texture_name_right
                            : display->texture_name_left;
    item_display.itemVisualId = display->item_visuals_id;
    item_display.particleColorId = display->particle_color_id;

    openwow::game::EquipmentVisualSystem equipment;
    equipment.AddDisplayInfo(item_display);
    const auto model = equipment.ResolveModel({
        .slot = slot,
        .displayId = context.display_id,
        .inventoryType = static_cast<std::uint8_t>(
            std::min<std::uint32_t>(context.inventory_type, 0xffu)),
        .sheatheType = 1,
    });
    if (!model.has_value() || model->modelPath.empty()) return false;
    model_path = model->modelPath;
  }
  if (model_path.empty()) return false;

  const openwow::data::dbc::SpellVisualEffectNameEntry preload{
      .id = 0,
      .name = {},
      .file_path = model_path,
      .area_effect_size = 0.0f,
      .scale = 1.0f,
      .min_allowed_scale = 1.0f,
      .max_allowed_scale = 1.0f,
  };
  (void)openwow::game::RequestSpellVisualEffectModelPreload(preload);
  config = {};
  return true;
}

float DefaultDuration(VisualPhase phase) {
  switch (phase) {
    case VisualPhase::kPrecast:  return 1.0f;
    case VisualPhase::kCast:     return 2.0f;
    case VisualPhase::kImpact:   return 0.5f;
    case VisualPhase::kState:    return 0.0f;
    case VisualPhase::kStateDone:return 0.5f;
    case VisualPhase::kChannel:  return 0.0f;
    case VisualPhase::kCasterImpact: return 0.5f;
    case VisualPhase::kTargetImpact: return 0.5f;
    case VisualPhase::kEffect:   return 2.0f;
  }
  return 1.0f;
}

float PhaseEmissionMultiplier(VisualPhase phase) {
  switch (phase) {
    case VisualPhase::kPrecast:  return 0.5f;
    case VisualPhase::kCast:     return 1.0f;
    case VisualPhase::kImpact:   return 3.0f;
    case VisualPhase::kState:    return 0.7f;
    case VisualPhase::kStateDone:return 2.0f;
    case VisualPhase::kChannel:  return 0.8f;
    case VisualPhase::kCasterImpact: return 3.0f;
    case VisualPhase::kTargetImpact: return 3.0f;
    case VisualPhase::kEffect:   return 1.0f;
  }
  return 1.0f;
}

const char* PhaseName(VisualPhase p) {
  switch (p) {
    case VisualPhase::kPrecast:  return "precast";
    case VisualPhase::kCast:     return "cast";
    case VisualPhase::kImpact:   return "impact";
    case VisualPhase::kState:    return "state";
    case VisualPhase::kStateDone:return "stateDone";
    case VisualPhase::kChannel:  return "channel";
    case VisualPhase::kCasterImpact: return "casterImpact";
    case VisualPhase::kTargetImpact: return "targetImpact";
    case VisualPhase::kEffect:   return "effect";
  }
  return "unknown";
}

}

bool SpellVisualEffects::Initialize(ParticleSystem* particles,
    const game::ObjectPresentationSnapshot* objects) {
  if (initialized_) return true;
  if (!particles || !objects) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                       "SpellVisualEffects: null particle system or object manager");
    return false;
  }
  particles_ = particles;
  objects_ = objects;
  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "SpellVisualEffects: initialized");
  return true;
}

void SpellVisualEffects::BindDbc(const openwow::data::dbc::DbcLoader* dbc) {
  dbc_ = dbc;
  pipeline_.Clear();

  openwow::game::SpellMissileMotionRegistry::Get().BindStore(nullptr);
  if (dbc_) {
    pipeline_.LoadData(dbc_->spell_visual().entries(),
                       dbc_->spell_visual_kit().entries(),
                       dbc_->spell_visual_effect_name().entries());
    openwow::game::SpellMissileMotionRegistry::Get().BindStore(
        &dbc_->spell_missile_motion());
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "SpellVisualEffects: DBC data bound ("
                           + std::to_string(dbc_->spell_visual().size())
                           + " visuals, "
                           + std::to_string(dbc_->spell_visual_kit().size())
                           + " kits, "
                           + std::to_string(dbc_->spell_visual_effect_name().size())
                           + " effect names)");
  }
}

void SpellVisualEffects::Shutdown() {
  Clear();
  openwow::game::SpellMissileMotionRegistry::Get().BindStore(nullptr);
  particles_ = nullptr;
  objects_ = nullptr;
  dbc_ = nullptr;
  pipeline_.Clear();
  initialized_ = false;
}

void SpellVisualEffects::OnSpellStart(std::uint64_t caster_guid,
                                      std::uint32_t spell_id) {
  if (!initialized_ || !particles_) return;

  float cx = 0.0f, cy = 0.0f, cz = 0.0f;
  if (!GetObjectPosition(caster_guid, cx, cy, cz)) return;

  auto config = MakeConfigForPhase(spell_id, VisualPhase::kCast);
  if (!config.has_value()) {
    return;
  }

  SpellVisualEffect effect;
  effect.spell_id = spell_id;
  effect.phase = VisualPhase::kCast;
  effect.x = cx;
  effect.y = cy;
  effect.z = cz;
  effect.source_guid = caster_guid;
  effect.duration = DefaultDuration(VisualPhase::kCast);
  effect.emitter_handle = CreateEffectEmitter(*config, cx, cy, cz);
  effect.has_emitter = true;

  effects_.push_back(effect);

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                     "SpellVisualEffects: OnSpellStart spell="
                         + std::to_string(spell_id));
}

void SpellVisualEffects::OnSpellGo(std::uint64_t caster_guid,
                                   std::uint32_t spell_id,
                                   const game::SpellGoVisualData& visual_data) {
  if (!initialized_ || !particles_) return;
  const bool has_missile = UsesMissileVisual(spell_id);
  for (std::size_t i = 0; i < visual_data.hit_targets.size(); ++i) {
    if (has_missile) {
      LaunchMissileToGuid(spell_id, caster_guid, visual_data.hit_targets[i],
                          visual_data);
      continue;
    }

    float tx = 0.0f;
    float ty = 0.0f;
    float tz = 0.0f;
    if (!GetObjectPosition(visual_data.hit_targets[i], tx, ty, tz)) {
      continue;
    }
    SpawnImpactEffect(spell_id, caster_guid, visual_data.hit_targets[i], {tx, ty, tz});
  }

  if (visual_data.hit_targets.empty() && visual_data.has_destination) {

    const Vec3 destination = {visual_data.destination_x, visual_data.destination_y,
                              visual_data.destination_z};
    if (has_missile) {
      LaunchMissileToPoint(spell_id, caster_guid, 0, destination, visual_data);
    } else {
      SpawnImpactEffect(spell_id, caster_guid, 0, destination);
    }
  } else if (visual_data.hit_targets.empty()) {
    float cx = 0.0f;
    float cy = 0.0f;
    float cz = 0.0f;
    if (GetObjectPosition(caster_guid, cx, cy, cz)) {
      SpawnImpactEffect(spell_id, caster_guid, caster_guid, {cx, cy, cz});
    }
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                     "SpellVisualEffects: OnSpellGo spell="
                         + std::to_string(spell_id) + " targets="
                         + std::to_string(visual_data.hit_targets.size()) + " missile="
                         + (has_missile ? "yes" : "no"));
}

void SpellVisualEffects::OnPlaySpellVisual(std::uint64_t target_guid,
                                           std::uint32_t spell_visual_kit_id) {
  if (!initialized_ || !particles_) return;

  float x = 0.0f, y = 0.0f, z = 0.0f;
  if (!GetObjectPosition(target_guid, x, y, z)) return;

  ParticleEmitterConfig config;
  if (!ResolveKit(spell_visual_kit_id, config)) {
    return;
  }

  SpellVisualEffect effect;
  effect.spell_visual_kit_id = spell_visual_kit_id;
  effect.phase = VisualPhase::kCast;
  effect.x = x;
  effect.y = y;
  effect.z = z;
  effect.source_guid = target_guid;
  effect.target_guid = target_guid;
  effect.duration = 2.0f;
  effect.emitter_handle = CreateEffectEmitter(config, x, y, z);
  effect.has_emitter = true;

  effects_.push_back(effect);

  PlaySpellVisualKitSound(spell_visual_kit_id, x, y, z);
}

void SpellVisualEffects::OnPlaySpellImpact(std::uint64_t target_guid,
                                           std::uint32_t spell_visual_kit_id) {
  if (!initialized_ || !particles_) return;

  float x = 0.0f, y = 0.0f, z = 0.0f;
  if (!GetObjectPosition(target_guid, x, y, z)) return;

  ParticleEmitterConfig config;
  if (!ResolveKit(spell_visual_kit_id, config)) {
    return;
  }

  config.emission_rate *= 3.0f;
  config.lifespan *= 0.5f;

  SpellVisualEffect effect;
  effect.spell_visual_kit_id = spell_visual_kit_id;
  effect.phase = VisualPhase::kImpact;
  effect.x = x;
  effect.y = y;
  effect.z = z;
  effect.source_guid = target_guid;
  effect.target_guid = target_guid;
  effect.duration = 0.5f;
  effect.emitter_handle = CreateEffectEmitter(config, x, y, z);
  effect.has_emitter = true;

  effects_.push_back(effect);

  PlaySpellVisualKitSound(spell_visual_kit_id, x, y, z);
}

void SpellVisualEffects::Update(float dt) {
  if (!initialized_) return;

  for (auto& e : effects_) {
    e.elapsed += dt;
    if (e.duration > 0.0f && e.elapsed >= e.duration) {
      e.active = false;
    }
  }

  UpdateEffectPositions();

  UpdateMissiles(dt);

  CleanupExpired();
}

void SpellVisualEffects::Clear() {

  for (auto& e : effects_) {
    if (e.has_emitter && particles_) {
      particles_->RemoveEmitter(e.emitter_handle);
    }
  }
  effects_.clear();

  for (auto& m : missiles_) {
    if (m.has_emitter && particles_) {
      particles_->RemoveEmitter(m.emitter_handle);
    }
  }
  missiles_.clear();
}

std::uint32_t SpellVisualEffects::GetSpellVisualId(std::uint32_t spell_id) const {
  if (!dbc_) return 0;
  const auto* spell = dbc_->spell().LookupEntry(spell_id);
  if (!spell) return 0;

  return spell->spell_visual[0];
}

bool SpellVisualEffects::UsesMissileVisual(const std::uint32_t spell_id) const {
  const auto visual_id = GetSpellVisualId(spell_id);
  if (!dbc_ || visual_id == 0) {
    return false;
  }

  const auto* visual = dbc_->spell_visual().LookupEntry(visual_id);
  return visual != nullptr && visual->has_missile != 0;
}

bool SpellVisualEffects::ResolveKit(std::uint32_t kit_id,
                                    ParticleEmitterConfig& out_config) const {
  if (!dbc_ || kit_id == 0) return false;

  const auto* kit = dbc_->spell_visual_kit().LookupEntry(kit_id);
  if (!kit) return false;

  (void)pipeline_.RequestKitEffectModelPreloads(kit_id);

  const std::uint32_t effect_ids[] = {
      kit->base_effect, kit->chest_effect, kit->head_effect,
      kit->left_hand_effect, kit->right_hand_effect,
      kit->breath_effect, kit->left_weapon_effect, kit->right_weapon_effect};

  for (const auto effect_id : effect_ids) {
    if (ResolveEffectName(effect_id, out_config)) {
      return true;
    }
  }

  return false;
}

bool SpellVisualEffects::ResolveEffectName(
    std::uint32_t effect_name_id,
    ParticleEmitterConfig& out_config) const {
  if (!dbc_ || effect_name_id == 0) return false;

  const auto* effect_name =
      dbc_->spell_visual_effect_name().LookupEntry(effect_name_id);
  if (!effect_name || effect_name->file_path.empty()) return false;

  (void)openwow::game::RequestSpellVisualEffectModelPreload(*effect_name);
  out_config = {};
  out_config.scales[0] *= effect_name->scale;
  out_config.scales[1] *= effect_name->scale;
  return true;
}

std::optional<ParticleEmitterConfig> SpellVisualEffects::MakeConfigForPhase(
    std::uint32_t spell_id, VisualPhase phase) const {
  const auto visual_id = GetSpellVisualId(spell_id);
  if (dbc_ && visual_id != 0) {
    const auto* visual = dbc_->spell_visual().LookupEntry(visual_id);
    if (visual) {
      if (phase == VisualPhase::kEffect) {
        const auto resolved =
            pipeline_.ResolvePhase(visual_id, openwow::game::VisualPhase::kEffect);
        if (const auto world_effect =
                resolved.GetEffect(openwow::game::VisualAttachmentPoint::kWorld);
            world_effect.has_value()) {
          ParticleEmitterConfig config;
          if (ResolveEffectName(world_effect->effect_name_id, config)) {
            config.emission_rate *= PhaseEmissionMultiplier(phase);
            return config;
          }
        }
      }

      std::uint32_t kit_id = 0;
      switch (phase) {
        case VisualPhase::kPrecast:  kit_id = visual->precast_kit; break;
        case VisualPhase::kCast:     kit_id = visual->cast_kit; break;
        case VisualPhase::kImpact:   kit_id = visual->impact_kit; break;
        case VisualPhase::kState:    kit_id = visual->state_kit; break;
        case VisualPhase::kStateDone:kit_id = visual->state_done_kit; break;
        case VisualPhase::kChannel:  kit_id = visual->channel_kit; break;
        case VisualPhase::kCasterImpact: kit_id = visual->caster_impact_kit; break;
        case VisualPhase::kTargetImpact: kit_id = visual->target_impact_kit; break;
        case VisualPhase::kEffect:   kit_id = visual->persistent_area_kit; break;
      }

      ParticleEmitterConfig config;
      if (kit_id != 0 && ResolveKit(kit_id, config)) {

        config.emission_rate *= PhaseEmissionMultiplier(phase);

        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace,
                           "SpellVisualEffects: resolved kit="
                               + std::to_string(kit_id) + " for phase="
                               + PhaseName(phase));
        return config;
      }
    }
  }

  return std::nullopt;
}

void SpellVisualEffects::PlaySpellVisualKitSound(
    std::uint32_t kit_id, float x, float y, float z) {
  if (!dbc_ || kit_id == 0) return;

  const auto* kit = dbc_->spell_visual_kit().LookupEntry(kit_id);
  if (!kit || kit->sound_id == 0) return;

  const float position[3] = {x, y, z};
  if (sound_kit_sink_) {
    sound_kit_sink_(kit->sound_id, position);
  }
}

bool SpellVisualEffects::GetObjectPosition(std::uint64_t guid,
                                           float& x, float& y, float& z) const {
  if (!objects_ || guid == 0) return false;
  const auto object = std::lower_bound(
      objects_->active.begin(), objects_->active.end(), guid,
      [](const game::ObjectPresentationRecord& record,
         const std::uint64_t raw_guid) {
        return record.handle.guid.GetRawValue() < raw_guid;
      });
  if (object == objects_->active.end() ||
      object->handle.guid.GetRawValue() != guid) return false;
  x = object->x;
  y = object->y;
  z = object->z;
  return true;
}

std::uint32_t SpellVisualEffects::CreateEffectEmitter(
    const ParticleEmitterConfig& config, float x, float y, float z) {
  auto id = particles_->CreateEmitter(config);
  auto* emitter = particles_->GetEmitter(id);
  if (emitter) {
    emitter->SetPosition(x, y, z);
    emitter->SetDirection(0.0f, 1.0f, 0.0f);
  }
  return id;
}

void SpellVisualEffects::LaunchMissileToGuid(const std::uint32_t spell_id,
                                             const std::uint64_t caster_guid,
                                             const std::uint64_t target_guid,
                                             const game::SpellGoVisualData& visual_data) {
  float tx = 0.0f;
  float ty = 0.0f;
  float tz = 0.0f;
  if (!GetObjectPosition(target_guid, tx, ty, tz)) {
    return;
  }

  LaunchMissileToPoint(spell_id, caster_guid, target_guid, {tx, ty, tz},
                       visual_data);
}

void SpellVisualEffects::LaunchMissileToPoint(const std::uint32_t spell_id,
                                               const std::uint64_t caster_guid,
                                               const std::uint64_t target_guid,
                                               const Vec3& target_position,
                                               const game::SpellGoVisualData& visual_data) {
  float cx = 0.0f;
  float cy = 0.0f;
  float cz = 0.0f;
  if (!GetObjectPosition(caster_guid, cx, cy, cz)) {
    return;
  }

  if (!dbc_) {
    return;
  }

  const auto* visual = dbc_->spell_visual().LookupEntry(GetSpellVisualId(spell_id));
  if (!visual || visual->has_missile == 0) return;

  ParticleEmitterConfig config;
  bool has_config = false;
  if (visual->missile_model > 0) {
    has_config = ResolveEffectName(
        static_cast<std::uint32_t>(visual->missile_model), config);
  } else {
    const std::optional<game::SpellVisualItemModelContext>* context = nullptr;
    auto slot = openwow::game::EquipmentSlot::MainHand;
    switch (static_cast<game::MissileModelSourceType>(visual->missile_model)) {
      case game::MissileModelSourceType::kItemComponent2:
      case game::MissileModelSourceType::kItemComponent1:
      case game::MissileModelSourceType::kItemDisplayInfo:
        context = &visual_data.missile_item;
        if (context->has_value()) {
          const auto inventory_type = context->value().inventory_type;
          if (openwow::game::CanInventoryTypeGoInSlot(
                  inventory_type,
                  static_cast<std::uint32_t>(
                      openwow::game::EquipmentSlot::Ranged)) ||
              inventory_type == static_cast<std::uint32_t>(
                                     openwow::game::InventoryType::Quiver)) {
            slot = openwow::game::EquipmentSlot::Ranged;
          } else if (openwow::game::CanInventoryTypeGoInSlot(
                         inventory_type,
                         static_cast<std::uint32_t>(
                             openwow::game::EquipmentSlot::OffHand)) &&
                     !openwow::game::CanInventoryTypeGoInSlot(
                         inventory_type,
                         static_cast<std::uint32_t>(
                             openwow::game::EquipmentSlot::MainHand))) {
            slot = openwow::game::EquipmentSlot::OffHand;
          }
        }
        break;
      case game::MissileModelSourceType::kRangedWeapon:
        context = &visual_data.ranged_weapon;
        slot = openwow::game::EquipmentSlot::Ranged;
        break;
      case game::MissileModelSourceType::kOffHandWeapon:
        context = &visual_data.off_hand_weapon;
        slot = openwow::game::EquipmentSlot::OffHand;
        break;
      case game::MissileModelSourceType::kMainHandWeapon:
        context = &visual_data.main_hand_weapon;
        break;
      default:
        break;
    }
    if (context != nullptr && context->has_value()) {
      has_config = ResolveItemMissileModel(*dbc_, context->value(), slot, config);
    }
  }
  if (!has_config) {
    constexpr std::string_view kErrorMissileModel = "Spells\\ErrorCube.mdx";
    const openwow::data::dbc::SpellVisualEffectNameEntry error_effect{
        .id = 0,
        .name = {},
        .file_path = kErrorMissileModel,
        .area_effect_size = 0.0f,
        .scale = 1.0f,
        .min_allowed_scale = 1.0f,
        .max_allowed_scale = 1.0f,
    };
    (void)openwow::game::RequestSpellVisualEffectModelPreload(error_effect);
    config = {};
  }
  config.emission_rate *= 0.5f;
  config.max_particles = 100;
  config.area_width = 0.2f;
  config.area_height = 0.2f;

  const auto* const spell_entry = dbc_->spell().LookupEntry(spell_id);
  if (spell_entry == nullptr || spell_entry->speed <= 0.0f) {
    return;
  }
  float missile_speed = spell_entry->speed;

  if (visual->missile_motion_id != 0u) {
    const game::SpellMissileMotionInputs inputs{
        .spell_id = static_cast<double>(spell_id)};
    game::SpellMissileMotionOutputs outputs;
    if (game::SpellMissileMotionRegistry::Get().Evaluate(
            visual->missile_motion_id, inputs, outputs)) {
      const auto speed_override =
          game::ResolveSpellMissileMotionSpeed(missile_speed, outputs);
      if (speed_override.has_override) {
        missile_speed = speed_override.speed;
      }
    }
  }

  SpellMissile missile;
  missile.spell_id = spell_id;
  missile.caster_guid = caster_guid;
  missile.target_guid = target_guid;
  missile.start = {cx, cy, cz};
  missile.target = target_position;
  missile.current = missile.start;
  missile.direction = Normalize(missile.target - missile.start);
  missile.has_direction = HasLength(missile.direction);

  const float dist = Length(missile.target - missile.start);
  missile.duration = dist / missile_speed;

  missile.emitter_handle = CreateEffectEmitter(config, cx, cy, cz);
  missile.has_emitter = true;
  if (missile.has_emitter && missile.has_direction) {
    if (auto* emitter = particles_->GetEmitter(missile.emitter_handle)) {
      emitter->SetDirection(missile.direction.x, missile.direction.y,
                            missile.direction.z);
    }
  }

  missiles_.push_back(missile);
}

void SpellVisualEffects::SpawnImpactEffect(const std::uint32_t spell_id,
                                           const std::uint64_t caster_guid,
                                           const std::uint64_t target_guid,
                                           const Vec3& position) {
  auto config = MakeConfigForPhase(spell_id, VisualPhase::kImpact);
  if (!config.has_value()) {
    return;
  }

  SpellVisualEffect effect;
  effect.spell_id = spell_id;
  effect.phase = VisualPhase::kImpact;
  effect.x = position.x;
  effect.y = position.y;
  effect.z = position.z;
  effect.source_guid = caster_guid;
  effect.target_guid = target_guid;
  effect.duration = DefaultDuration(VisualPhase::kImpact);
  effect.tracks_guid_position = target_guid != 0;
  effect.emitter_handle = CreateEffectEmitter(*config, position.x, position.y,
                                              position.z);
  effect.has_emitter = true;

  effects_.push_back(effect);
}

void SpellVisualEffects::UpdateEffectPositions() {
  for (auto& e : effects_) {
    if (!e.active || !e.has_emitter || !e.tracks_guid_position) continue;

    const auto track_guid =
        (e.phase == VisualPhase::kImpact && e.target_guid != 0)
            ? e.target_guid
            : e.source_guid;

    float nx = 0.0f, ny = 0.0f, nz = 0.0f;
    if (GetObjectPosition(track_guid, nx, ny, nz)) {
      e.x = nx;
      e.y = ny;
      e.z = nz;
      auto* emitter = particles_->GetEmitter(e.emitter_handle);
      if (emitter) {
        emitter->SetPosition(nx, ny, nz);
      }
    }
  }
}

void SpellVisualEffects::UpdateMissiles(float dt) {
  for (auto& m : missiles_) {
    if (!m.active) continue;

    m.elapsed += dt;

    const float t = std::clamp(m.elapsed / std::max(m.duration, 0.01f), 0.0f, 1.0f);

    Vec3 target = m.target;
    if (m.target_guid != 0) {
      float new_tx = 0.0f, new_ty = 0.0f, new_tz = 0.0f;
      if (GetObjectPosition(m.target_guid, new_tx, new_ty, new_tz)) {
        target = {new_tx, new_ty, new_tz};
        m.target = target;
      }
    }

    const Vec3 position = {
        m.start.x + (target.x - m.start.x) * t,
        m.start.y + (target.y - m.start.y) * t,
        m.start.z + (target.z - m.start.z) * t,
    };
    const Vec3 delta = position - m.current;
    const Vec3 direction =
        ResolveTravelDirection(delta, m.direction, m.has_direction);
    m.current = position;
    if (HasLength(direction)) {
      m.direction = direction;
      m.has_direction = true;
    }

    if (m.has_emitter) {
      auto* emitter = particles_->GetEmitter(m.emitter_handle);
      if (emitter) {
        emitter->SetPosition(position.x, position.y, position.z);
        if (m.has_direction) {
          emitter->SetDirection(m.direction.x, m.direction.y, m.direction.z);
        }
      }
    }

    if (t >= 1.0f) {
      m.active = false;
      SpawnImpactEffect(m.spell_id, m.caster_guid, m.target_guid, target);

      if (dbc_) {
        const auto& dbc = *dbc_;
        const auto visual_id = GetSpellVisualId(m.spell_id);
        if (visual_id != 0) {
          const auto* visual = dbc.spell_visual().LookupEntry(visual_id);
          if (visual && visual->impact_kit != 0) {
            PlaySpellVisualKitSound(visual->impact_kit,
                                    target.x, target.y, target.z);
          }
        }
      }

      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace,
                         "SpellVisualEffects: missile arrived, spell="
                             + std::to_string(m.spell_id));
    }
  }
}

void SpellVisualEffects::CleanupExpired() {

  for (auto it = effects_.begin(); it != effects_.end();) {
    if (!it->active) {
      if (it->has_emitter && particles_) {
        particles_->RemoveEmitter(it->emitter_handle);
      }
      it = effects_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = missiles_.begin(); it != missiles_.end();) {
    if (!it->active) {
      if (it->has_emitter && particles_) {
        particles_->RemoveEmitter(it->emitter_handle);
      }
      it = missiles_.erase(it);
    } else {
      ++it;
    }
  }
}

}

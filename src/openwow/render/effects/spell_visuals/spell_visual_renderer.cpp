
#include "openwow/render/effects/spell_visuals/spell_visual_renderer.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/core/client_init.h"
#include "openwow/foundation/hashing/retail_adler_seed.h"
#include "openwow/game/spell_missile.h"
#include "openwow/game/spell_missile_visual_create.h"
#include "openwow/game/spell_visual_attachment.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/scene/m2_instance_render_cost.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <optional>
#include <string>

namespace openwow::render {

namespace {

constexpr float kModelMissileDuration = 0.0f;
constexpr float kCastEffectDuration = 0.0f;
constexpr float kImpactEffectDuration = 1.5f;
constexpr float kPrecastEffectDuration = 1.0f;
constexpr float kStateEffectDuration = 0.0f;
constexpr float kStateDoneEffectDuration = 0.5f;
constexpr float kChannelEffectDuration = 0.0f;
constexpr float kAreaEffectDuration = 0.0f;
constexpr float kMin3DDistance = 0.01f;
constexpr float kRadiansToDegrees = 57.29577951308232f;
constexpr float kRetailMissileGravity = 19.29110336303711f;
constexpr float kMinimumTimedTrajectoryDuration = 0.0001f;
constexpr std::uint8_t kMissileImpactNone =
    static_cast<std::uint8_t>(game::MissileImpactResult::kNone);
constexpr std::uint8_t kMissileImpactReflect =
    static_cast<std::uint8_t>(game::MissileImpactResult::kReflect);

constexpr std::uint32_t kEffectSoundModeLoop = 0x00000001u;
constexpr std::uint32_t kEffectSoundSuppressed = 0x00000400u;
constexpr std::uint32_t kEffectSoundDoNotBindToOwner = 0x00200000u;

[[nodiscard]] float NormalizeTimedTrajectoryPitch(float pitch) {
  constexpr float kHalfPi = 1.5707963705062866f;
  if (pitch > kHalfPi) {
    pitch = 3.1415927410125732f - pitch;
  } else if (pitch < -kHalfPi) {
    pitch = -3.1415927410125732f - pitch;
  }
  return pitch;
}

[[nodiscard]] RenderVec3 ToRenderVec3(const RenderVec3View position) {
  return {position[0], position[1], position[2]};
}

std::uint32_t GetKitIdFromVisual(
    const data::dbc::SpellVisualEntry& visual, VisualPhase phase) {
  switch (phase) {
    case VisualPhase::kPrecast:      return visual.precast_kit;
    case VisualPhase::kCast:         return visual.cast_kit;
    case VisualPhase::kImpact:       return visual.impact_kit;
    case VisualPhase::kState:        return visual.state_kit;
    case VisualPhase::kStateDone:    return visual.state_done_kit;
    case VisualPhase::kChannel:      return visual.channel_kit;
    case VisualPhase::kCasterImpact: return visual.caster_impact_kit;
    case VisualPhase::kTargetImpact: return visual.target_impact_kit;
    case VisualPhase::kEffect:       return visual.persistent_area_kit;
    default:                         return visual.cast_kit;
  }
}

float DefaultPhaseDuration(VisualPhase phase) {
  switch (phase) {
    case VisualPhase::kPrecast:      return kPrecastEffectDuration;
    case VisualPhase::kCast:         return kCastEffectDuration;
    case VisualPhase::kImpact:       return kImpactEffectDuration;
    case VisualPhase::kState:        return kStateEffectDuration;
    case VisualPhase::kStateDone:    return kStateDoneEffectDuration;
    case VisualPhase::kChannel:      return kChannelEffectDuration;
    case VisualPhase::kCasterImpact: return kImpactEffectDuration;
    case VisualPhase::kTargetImpact: return kImpactEffectDuration;
    case VisualPhase::kEffect:       return kAreaEffectDuration;
    default:                         return 2.0f;
  }
}

const char* PhaseName(VisualPhase p) {
  switch (p) {
    case VisualPhase::kPrecast:      return "precast";
    case VisualPhase::kCast:         return "cast";
    case VisualPhase::kImpact:       return "impact";
    case VisualPhase::kState:        return "state";
    case VisualPhase::kStateDone:    return "stateDone";
    case VisualPhase::kChannel:      return "channel";
    case VisualPhase::kCasterImpact: return "casterImpact";
    case VisualPhase::kTargetImpact: return "targetImpact";
    case VisualPhase::kEffect:       return "effect";
  }
  return "unknown";
}

void LogSpellVisualM2Failure(const char* const operation,
                             const std::string& model_path,
                             const m2::M2ModelInstanceLoadResult& result) {
  diagnostics::Log(diagnostics::LogLevel::kWarn,
            std::string("SpellVisualRenderer: ") + operation + " failed for " +
                model_path + " status=" + m2::M2ResultStatusName(result.status) +
                " reason=" + m2::M2ResultReasonName(result.reason) +
                (result.detail.empty() ? std::string() : " detail=" + result.detail));
}

}

bool SpellVisualRenderer::Initialize(
    m2::M2System* m2_system,
    const game::ObjectPresentationSnapshot* objects,
    std::function<std::uint32_t(game::ObjectHandle)>
        owner_m2_instance_resolver) {
  if (initialized_) return true;
  if (!m2_system || !objects) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
        "SpellVisualRenderer: missing m2_system or object manager");
    return false;
  }

  m2_system_ = m2_system;
  objects_ = objects;
  owner_m2_instance_resolver_ = std::move(owner_m2_instance_resolver);
  initialized_ = true;

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
      "SpellVisualRenderer: initialized");
  return true;
}

void SpellVisualRenderer::BindDbc(const openwow::data::dbc::DbcLoader* dbc) {
  dbc_ = dbc;
  if (dbc_ != nullptr) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
        "SpellVisualRenderer: DBC data bound");
  }
}

void SpellVisualRenderer::Shutdown() {
  Clear();
  m2_system_ = nullptr;
  objects_ = nullptr;

  owner_m2_instance_resolver_ = {};
  initialized_ = false;
}

std::uint32_t SpellVisualRenderer::CreateSpellVisualEffect(
    std::uint32_t spell_visual_id,
    std::uint32_t kit_id,
    std::uint64_t caster_guid,
    std::uint64_t target_guid,
    const float* position,
    VisualPhase phase,
    float duration_override) {
  if (!initialized_) return 0;

  const std::uint32_t effect_id = NextEffectId();
  const float duration = duration_override > 0.0f
                             ? duration_override
                             : DefaultPhaseDuration(phase);

  std::uint32_t resolved_kit = kit_id;
  if (resolved_kit == 0 && dbc_ != nullptr && spell_visual_id != 0) {
    const auto* visual = dbc_->spell_visual().LookupEntry(spell_visual_id);
    if (visual != nullptr) {
      resolved_kit = GetKitIdFromVisual(*visual, phase);
    }
  }

  SpawnKitModels(resolved_kit, caster_guid, target_guid, position, phase,
                 effect_id, duration);

  if (position != nullptr && dbc_ != nullptr && resolved_kit != 0u) {
    if (const auto* const kit =
            dbc_->spell_visual_kit().LookupEntry(resolved_kit);
        kit != nullptr && kit->sound_id != 0u) {
      if (effect_sound_start_sink_) {
        (void)effect_sound_start_sink_(
            kit->sound_id, position,
            SpellSoundPlaybackMode::kForceOneShot,
            target_guid);
      } else {
        PlaySoundKit(kit->sound_id, position);
      }
    }
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
      "SpellVisualRenderer: created effect id=" + std::to_string(effect_id) +
      " spell_visual=" + std::to_string(spell_visual_id) +
      " kit=" + std::to_string(resolved_kit) +
      " phase=" + PhaseName(phase));

  return effect_id;
}

std::uint32_t SpellVisualRenderer::CreateImpactEffect(
    std::uint32_t spell_visual_id,
    std::uint32_t kit_id,
    std::uint64_t caster_guid,
    std::uint64_t target_guid,
    const float* position) {
  return CreateSpellVisualEffect(spell_visual_id, kit_id,
                                 caster_guid, target_guid,
                                 position, VisualPhase::kImpact,
                                 kImpactEffectDuration);
}

void SpellVisualRenderer::CreateDestLocAreaImpact(
    const std::uint32_t spell_visual_id,
    const std::uint32_t spell_id,
    const std::uint32_t kit_id,
    const std::uint32_t initial_raw_flags,
    const std::uint64_t owner_guid,
    const float* const position) {
  if (!initialized_ || dbc_ == nullptr || m2_system_ == nullptr ||
      kit_id == 0u || position == nullptr) {
    return;
  }
  const auto* const kit = dbc_->spell_visual_kit().LookupEntry(kit_id);
  if (kit == nullptr) return;

  const auto emit_world_slot =
      [this, spell_visual_id, spell_id, kit_id, owner_guid, position](
          const std::uint32_t effect_name_id,
          const std::uint32_t source_field_index,
          const std::uint32_t raw_flags) {
        if (effect_name_id == 0u) return false;
        const auto* const effect =
            dbc_->spell_visual_effect_name().LookupEntry(effect_name_id);
        if (effect == nullptr) return false;

        if (!effect->file_path.empty()) {
          game::SpellVisualPresentationEvent event{};
          event.action = game::SpellVisualLifecycleAction::kTransient;
          event.phase = game::SpellVisualPresentationPhase::kEffect;
          event.dispatch_type = 1u;
          event.raw_flags = raw_flags;
          event.spell_id = spell_id;
          event.spell_visual_id = spell_visual_id;
          event.kit_id = kit_id;
          event.owner = ResolveObjectHandle(owner_guid);
          event.world_position =
              std::array<float, 3>{position[0], position[1], position[2]};
          event.deferred_impact_owner_guid = owner_guid;
          event.m2_events_require_owner_resolution = true;
          event.effects.push_back(game::SpellVisualPresentationEffect{
              .effect_name_id = effect_name_id,
              .model_path = std::string(effect->file_path),
              .resource_scale = effect->scale,
              .attachment_id = -1,
              .source_field_index = source_field_index,
              .world_space = true,
              .uses_explicit_world_position = true,
          });
          SpawnPresentationModels(event, NextEffectId(),
                                  kImpactEffectDuration);
        }

        return true;
      };

  std::uint32_t raw_flags = initial_raw_flags;
  if (emit_world_slot(kit->base_effect, 5u, raw_flags)) {
    raw_flags = game::DestLocAreaEffectFlags::AfterPrecastEffect(raw_flags);
  }
  (void)emit_world_slot(kit->world_effect, 14u, raw_flags);
}

void SpellVisualRenderer::DispatchGenericDeferredImpact(
    const game::ObjectHandle caster_handle,
    const game::ObjectHandle target_handle,
    const std::uint64_t target_guid,
    const std::uint32_t spell_id,
    const std::uint32_t spell_visual_id,
    const std::uint32_t kit_id,
    const float* const position) {
  if (kit_id == 0u || position == nullptr || !deferred_impact_sink_) {
    return;
  }
  deferred_impact_sink_({
      .caster = caster_handle,
      .target = target_handle,
      .target_guid = target_guid,
      .spell_id = spell_id,
      .spell_visual_id = spell_visual_id,
      .kit_id = kit_id,
      .world_position = {position[0], position[1], position[2]},
  });
}

std::uint32_t SpellVisualRenderer::CreatePersistentAreaEffect(
    std::uint32_t spell_visual_id,
    std::uint32_t kit_id,
    const float* position,
    float radius,
    float duration) {
  if (!initialized_ || position == nullptr) return 0;

  const std::uint32_t effect_id = NextEffectId();

  PersistentAreaEffect area;
  area.effect_id = effect_id;
  area.spell_visual_id = spell_visual_id;
  area.position[0] = position[0];
  area.position[1] = position[1];
  area.position[2] = position[2];
  area.radius = radius;
  area.duration = duration;
  area.active = true;

  std::uint32_t resolved_kit = kit_id;
  if (resolved_kit == 0u && dbc_ != nullptr && spell_visual_id != 0u) {
    if (const auto* const visual = dbc_->spell_visual().LookupEntry(spell_visual_id);
        visual != nullptr) {
      resolved_kit = GetKitIdFromVisual(*visual, VisualPhase::kEffect);
    }
  }

  if (resolved_kit != 0u) {
    SpawnKitModels(resolved_kit, 0, 0, position, VisualPhase::kEffect,
                   effect_id, duration);
    if (dbc_ != nullptr) {
      if (const auto* const kit =
              dbc_->spell_visual_kit().LookupEntry(resolved_kit);
          kit != nullptr && kit->sound_id != 0u) {
        if (effect_sound_start_sink_) {
          const auto handle = effect_sound_start_sink_(
              kit->sound_id, position,
              SpellSoundPlaybackMode::kUseSoundKit, 0u);
          if (handle != 0u) {
            effect_sounds_[effect_id] = EffectSound{.handle = handle};
          }
        } else {
          PlaySoundKit(kit->sound_id, position);
        }
      }
    }
  }

  area_effects_[effect_id] = std::move(area);

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
      "SpellVisualRenderer: created area effect id=" + std::to_string(effect_id));

  return effect_id;
}

std::uint32_t SpellVisualRenderer::CreatePersistentAreaModel(
    const std::string_view model_path,
    const float* position,
    const float radius,
    const float duration) {
  if (!initialized_ || m2_system_ == nullptr || position == nullptr ||
      model_path.empty()) {
    return 0u;
  }

  auto model = m2_system_->LoadModel(std::string(model_path));
  if (model.status != m2::M2ResultStatus::kReady || model.model_id == 0u) {
    model = m2_system_->LoadModel(
        std::string(m2::kDefaultM2FallbackModelPath));
  }
  if (model.status != m2::M2ResultStatus::kReady || model.model_id == 0u) {
    return 0u;
  }

  const std::uint32_t effect_id = NextEffectId();
  PersistentAreaEffect area{};
  area.effect_id = effect_id;
  area.shard_model_id = model.model_id;
  area.position[0] = position[0];
  area.position[1] = position[1];
  area.position[2] = position[2];
  area.radius = radius;
  area.duration = duration;
  area.active = true;
  area.child_positions_owned_externally = true;
  area_effects_[effect_id] = std::move(area);

  return effect_id;
}

std::uint32_t SpellVisualRenderer::CreateAreaModelShard(
    const std::uint32_t effect_id,
    const std::string_view model_path,
    const float* const position) {
  if (!initialized_ || m2_system_ == nullptr || position == nullptr ||
      model_path.empty()) {
    return 0u;
  }
  auto area_it = area_effects_.find(effect_id);
  if (area_it == area_effects_.end()) {
    return 0u;
  }

  const auto result = m2_system_->CreateInstance(area_it->second.shard_model_id);
  if (result.status != m2::M2ResultStatus::kReady || result.instance_id == 0u) {
    return 0u;
  }

  if (m2_system_->SetTransform(
          result.instance_id, ToRenderVec3(RenderVec3View{position, 3u}),
          std::nullopt, 1.0f) != m2::M2ResultStatus::kReady) {
    std::uint32_t failed = result.instance_id;
    DestroyM2Instance(failed);
    return 0u;
  }

  const auto animation =
      BindDefaultModelSequence(area_it->second.shard_model_id,
                               result.instance_id);
  if (m2::IsTerminalM2ResultStatus(animation)) {
    std::uint32_t failed = result.instance_id;
    DestroyM2Instance(failed);
    return 0u;
  }

  const auto visibility = m2_system_->SetVisible(result.instance_id, false);
  const auto emitters =
      m2_system_->SetEffectEmittersEnabled(result.instance_id, false);
  if (visibility != m2::M2ResultStatus::kReady ||
      emitters != m2::M2ResultStatus::kReady) {
    std::uint32_t failed = result.instance_id;
    DestroyM2Instance(failed);
    return 0u;
  }

  area_it->second.instance_ids.push_back(result.instance_id);
  return result.instance_id;
}

void SpellVisualRenderer::DestroyAreaModelShard(
    const std::uint32_t effect_id,
    const std::uint32_t instance_id) {
  auto area_it = area_effects_.find(effect_id);
  if (area_it == area_effects_.end()) {
    return;
  }
  auto& instances = area_it->second.instance_ids;
  const auto it = std::find(instances.begin(), instances.end(), instance_id);
  if (it == instances.end()) {
    return;
  }
  std::uint32_t owned_instance = *it;
  DestroyM2Instance(owned_instance);
  instances.erase(it);
}

bool SpellVisualRenderer::SetAreaModelShardVisible(
    const std::uint32_t instance_id,
    const bool visible) {
  return initialized_ && m2_system_ != nullptr && instance_id != 0u &&
         m2_system_->SetVisible(instance_id, visible) ==
             m2::M2ResultStatus::kReady;
}

bool SpellVisualRenderer::SetAreaModelShardWorldTransform(
    const std::uint32_t instance_id,
    const float* const matrix) {
  if (!initialized_ || m2_system_ == nullptr || instance_id == 0u ||
      matrix == nullptr) {
    return false;
  }
  RenderMatrix4x4 transform{};
  std::copy_n(matrix, transform.size(), transform.begin());
  return m2_system_->SetWorldTransformMatrix(instance_id, transform) ==
         m2::M2ResultStatus::kReady;
}

bool SpellVisualRenderer::SetPersistentAreaEffectPosition(
    const std::uint32_t effect_id,
    const float* const position) {
  if (!initialized_ || position == nullptr || m2_system_ == nullptr) {
    return false;
  }

  auto area_it = area_effects_.find(effect_id);
  if (area_it == area_effects_.end()) {
    return false;
  }

  auto& area = area_it->second;
  area.position[0] = position[0];
  area.position[1] = position[1];
  area.position[2] = position[2];

  bool updated_any = true;
  if (!area.child_positions_owned_externally) {
    for (const std::uint32_t instance_id : area.instance_ids) {
      if (instance_id == 0u) {
        continue;
      }
      if (m2_system_->SetPosition(
              instance_id, ToRenderVec3(RenderVec3View{position, 3u})) ==
          m2::M2ResultStatus::kReady) {
        updated_any = true;
      }
    }
  }

  for (auto& [_, inst] : model_instances_) {
    if (inst.effect_id != effect_id || inst.parent_guid != 0u) {
      continue;
    }
    std::memcpy(inst.position, position, sizeof(inst.position));
    if (m2_system_->SetPosition(inst.instance_id, ToRenderVec3(RenderVec3View{position, 3u})) ==
        m2::M2ResultStatus::kReady) {
      updated_any = true;
    }
  }

  return updated_any;
}

std::uint32_t SpellVisualRenderer::CreateMissileEffect(
    const game::SpellMissilePresentationData& missile,
    std::uint32_t spell_visual_id,
    std::uint32_t spell_id,
    std::uint64_t missile_caster_guid,
    std::uint8_t missile_cast_count,
    std::uint64_t caster_guid,
    std::uint64_t target_guid,
    const float* start_pos,
    const float* end_pos,
    float speed,
    std::uint32_t impact_kit_id,
    std::uint8_t impact_result,
    std::uint8_t reflect_result,
    bool uses_timed_trajectory,
    float trajectory_pitch,
    float trajectory_speed,
    std::uint32_t trajectory_duration_ms,
    game::SpellVisualDeferredImpactPolicy deferred_impact_policy,
    std::uint32_t deferred_impact_raw_flags,
    std::uint64_t deferred_impact_owner_guid,
    game::ObjectHandle caster_handle,
    game::ObjectHandle target_handle) {
  if (!initialized_ || !start_pos || !end_pos) return 0;

  const bool is_local_player_caster =
      objects_ != nullptr && caster_handle.guid == objects_->local_player.guid;
  static int missile_receipt_count = 0;
  constexpr int kMissileReceiptLimit = 5;
  const bool receipt_active =
      is_local_player_caster || missile_receipt_count < kMissileReceiptLimit;
  if (receipt_active) {
    if (!is_local_player_caster) {
      ++missile_receipt_count;
    }
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "CreateMissileEffect receipt: spell=" + std::to_string(spell_id) +
            " visual=" + std::to_string(spell_visual_id) +
            " model=" + missile.model_path +
            " speed=" + std::to_string(speed) +
            " local_caster=" + std::to_string(is_local_player_caster));
  }

  if (speed <= 0.0f) {
    if (impact_kit_id != 0u) {
      if (deferred_impact_policy ==
          game::SpellVisualDeferredImpactPolicy::kDestLocArea) {
        CreateDestLocAreaImpact(spell_visual_id, spell_id, impact_kit_id,
                                deferred_impact_raw_flags,
                                deferred_impact_owner_guid, end_pos);
      } else {
        DispatchGenericDeferredImpact(
            caster_handle, target_handle, target_guid, spell_id,
            spell_visual_id, impact_kit_id, end_pos);
      }
    }
    return 0;
  }

  ResolvedKitModel kit_model;
  kit_model.model_path = missile.model_path;
  kit_model.scale = missile.model_scale;
  kit_model.sound_kit_id = missile.sound_kit_id;

  if (kit_model.model_path.empty()) {
    if (impact_kit_id != 0) {
      if (deferred_impact_policy ==
          game::SpellVisualDeferredImpactPolicy::kDestLocArea) {
        CreateDestLocAreaImpact(spell_visual_id, spell_id, impact_kit_id,
                                deferred_impact_raw_flags,
                                deferred_impact_owner_guid, end_pos);
      } else {
        DispatchGenericDeferredImpact(
            caster_handle, target_handle, target_guid, spell_id,
            spell_visual_id, impact_kit_id, end_pos);
      }
    }
    return 0;
  }

  const std::uint32_t motion_id = missile.motion_id;
  const std::uint32_t salvo_count =
      game::SpellMissileMotionRegistry::Get().ResolveInstanceCount(motion_id);
  const float missile_speed = speed;
  std::uint32_t first_effect_id = 0u;

  for (std::uint32_t salvo_index = 0u; salvo_index < salvo_count;
       ++salvo_index) {
    auto instance_result = CreateModelInstance(kit_model, start_pos);
    if (instance_result.status != m2::M2ResultStatus::kReady ||
        instance_result.instance_id == 0u) {
      continue;
    }

    if (!missile.texture_path.empty()) {
      const auto texture_status = m2_system_->SetReplaceableTexturePath(
          instance_result.instance_id, missile.replaceable_texture_type,
          missile.texture_path);
      if (m2::IsTerminalM2ResultStatus(texture_status)) {
        DestroyM2Instance(instance_result.instance_id);
        continue;
      }
    }

    const std::uint32_t effect_id = NextEffectId();
    const auto flight_index = RegisterMissileFlight(
        effect_id, instance_result.instance_id, missile_caster_guid,
        missile_cast_count, caster_guid, target_guid, start_pos, end_pos,
        missile_speed, spell_visual_id, spell_id, motion_id, salvo_index,
        salvo_count, impact_kit_id, impact_result, reflect_result,
        deferred_impact_policy, deferred_impact_raw_flags,
        deferred_impact_owner_guid, caster_handle, target_handle);
    auto& flight = missile_flights_[flight_index];
    flight.model_scale = kit_model.scale;
    auto& random = core::GetClientStartupAdlerSeedState();
    flight.random_motion = {
        foundation::hashing::AdlerSeedNextUnitFloat(random),
        foundation::hashing::AdlerSeedNextUnitFloat(random),
        foundation::hashing::AdlerSeedNextUnitFloat(random),
    };
    flight.target_attachment_id = missile.target_attachment_id;
    flight.target_attachment_uses_raw_index =
        missile.target_attachment_uses_raw_index;
    flight.target_attachment_offset = missile.target_attachment_offset;
    flight.follow_ground_height = std::max(
        static_cast<float>(missile.follow_ground_height_ms) * 0.001f, 0.25f);
    flight.follow_ground_drop_speed = std::max(
        static_cast<float>(missile.follow_ground_drop_speed_ms) * 0.001f,
        0.01f);
    flight.follow_ground_approach = std::clamp(
        static_cast<float>(missile.follow_ground_approach_ms) * 0.001f,
        0.0f, 1.0f);
    flight.follow_ground_flags = missile.follow_ground_flags;

    flight.missile_flags = 1u;
    if ((missile.follow_ground_flags & 1u) != 0u) {
      flight.missile_flags |= 0x00007800u;
    }
    if (motion_id != 0u) {
      flight.missile_flags |= 0x0000B800u;
    }
    if (uses_timed_trajectory) {
      flight.missile_flags |= 0x00010800u;
    }
    if (uses_timed_trajectory && std::isfinite(trajectory_pitch)) {
      float pitch = NormalizeTimedTrajectoryPitch(trajectory_pitch);
      float gravity = kRetailMissileGravity;
      const data::dbc::SpellMissileEntry* missile_entry = nullptr;
      if (dbc_ != nullptr) {
        if (const auto* const spell = dbc_->spell().LookupEntry(spell_id);
            spell != nullptr) {
          missile_entry =
              dbc_->spell_missile().LookupEntry(spell->spell_missile_id);
          if (missile_entry != nullptr) {
            gravity = missile_entry->gravity;
          }
        }
      }

      const game::MissileVec3 delta{
          end_pos[0] - start_pos[0], end_pos[1] - start_pos[1],
          end_pos[2] - start_pos[2]};
      auto pitch_solution =
          game::missile_math::SolveBallisticLaunchAtPitch(
              pitch, delta, gravity);
      if (pitch_solution.result ==
          game::missile_math::PitchSolveResult::kFailed) {

        pitch = NormalizeTimedTrajectoryPitch(
            trajectory_pitch + (gravity < 0.0f ? -0.01f : 0.01f));
        pitch_solution = game::missile_math::SolveBallisticLaunchAtPitch(
            pitch, delta, 0.0f);
      }

      std::optional<float> selected_natural_duration;
      if (pitch_solution.result ==
          game::missile_math::PitchSolveResult::kSolved) {

        if (std::isfinite(pitch_solution.travel_time) &&
            pitch_solution.travel_time >= kMinimumTimedTrajectoryDuration) {
          selected_natural_duration = pitch_solution.travel_time;
        }
      } else if (pitch_solution.result ==
                     game::missile_math::PitchSolveResult::kDegenerate &&
                 missile_entry != nullptr) {

        const std::array<float, 3> candidate_speeds{
            trajectory_speed,
            missile_entry->default_speed_min,
            missile_entry->default_speed_max,
        };
        for (const float candidate_speed : candidate_speeds) {
          if (!std::isfinite(candidate_speed) || candidate_speed <= 0.0f) {
            continue;
          }
          const float candidate_duration =
              game::missile_math::ComputeBallisticTravelTime(
                  pitch, candidate_speed, delta, gravity);
          if (!std::isfinite(candidate_duration) ||
              candidate_duration < kMinimumTimedTrajectoryDuration) {
            continue;
          }
          selected_natural_duration = candidate_duration;
          break;
        }
      }

      if (selected_natural_duration.has_value()) {
        const float natural_duration = *selected_natural_duration;

        flight.timed_initial_velocity = {
            delta.x / natural_duration,
            delta.y / natural_duration,
            delta.z / natural_duration,
        };
        const float deadline_seconds = std::max(
            static_cast<float>(trajectory_duration_ms) * 0.001f,
            kMinimumTimedTrajectoryDuration);
        flight.timed_gravity = gravity;
        flight.timed_natural_duration = natural_duration;
        flight.timed_server_duration = deadline_seconds;
        flight.timed_time_scale = std::clamp(
            natural_duration / deadline_seconds, 0.0f, 2.0f);
        flight.uses_timed_trajectory = true;
      }
    }
    if (uses_timed_trajectory && !flight.uses_timed_trajectory) {

      flight.missile_flags &= ~0x00010000u;
    }
    if (const auto* target = FindObject(target_handle); target != nullptr) {
      flight.target_fallback_offset = {
          end_pos[0] - target->x,
          end_pos[1] - target->y,
          end_pos[2] - target->z,
      };
    }

    SpellVisualModelInstance inst;
    inst.effect_id = effect_id;
    inst.model_id = instance_result.model_id;
    inst.instance_id = instance_result.instance_id;
    inst.model_path = kit_model.model_path;
    inst.scale = kit_model.scale;
    inst.raw_flags = flight.missile_flags;
    inst.m2_event_kind = SpellVisualM2EventKind::kMissile;

    inst.m2_event_owner = target_handle.guid.IsEmpty()
                              ? caster_handle
                              : target_handle;
    inst.position[0] = start_pos[0];
    inst.position[1] = start_pos[1];
    inst.position[2] = start_pos[2];
    inst.elapsed = 0.0f;
    inst.duration = kModelMissileDuration;
    inst.parent_guid = caster_guid;
    inst.parent_handle = caster_handle;
    inst.active = true;
    inst.visible = true;
    model_instances_[instance_result.instance_id] = std::move(inst);
    if (!BindM2EventCallback(instance_result.instance_id)) {
      flight.active = false;
      auto failed = model_instances_.find(instance_result.instance_id);
      if (failed != model_instances_.end()) {
        DestroyM2Instance(failed->second.instance_id);
        model_instances_.erase(failed);
      }
      continue;
    }
    if (first_effect_id == 0u) {
      first_effect_id = effect_id;
    }

    if (kit_model.sound_kit_id != 0) {
      if (missile_sound_start_sink_) {
        flight.sound_handle =
            missile_sound_start_sink_(kit_model.sound_kit_id, start_pos);
      } else {
        PlaySoundKit(kit_model.sound_kit_id, start_pos);
      }
    }
  }

  if (first_effect_id == 0u && impact_kit_id != 0u) {
    if (deferred_impact_policy ==
        game::SpellVisualDeferredImpactPolicy::kDestLocArea) {
      CreateDestLocAreaImpact(spell_visual_id, spell_id, impact_kit_id,
                              deferred_impact_raw_flags,
                              deferred_impact_owner_guid, end_pos);
    } else {
      DispatchGenericDeferredImpact(
          caster_handle, target_handle, target_guid, spell_id,
          spell_visual_id, impact_kit_id, end_pos);
    }
  }

  if (receipt_active) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "CreateMissileEffect receipt: spell=" + std::to_string(spell_id) +
            " flight_effect_id=" + std::to_string(first_effect_id) +
            " salvo_count=" + std::to_string(salvo_count));
  }
  return first_effect_id;
}

std::uint32_t SpellVisualRenderer::RegisterMissileFlight(
    std::uint32_t effect_id,
    std::uint32_t instance_id,
    std::uint64_t missile_caster_guid,
    std::uint8_t missile_cast_count,
    std::uint64_t caster_guid,
    std::uint64_t target_guid,
    const float* start_pos,
    const float* end_pos,
    float speed,
    std::uint32_t spell_visual_id,
    std::uint32_t spell_id,
    std::uint32_t motion_id,
    std::uint32_t salvo_index,
    std::uint32_t salvo_count,
    std::uint32_t impact_kit_id,
    std::uint8_t impact_result,
    std::uint8_t reflect_result,
    game::SpellVisualDeferredImpactPolicy deferred_impact_policy,
    std::uint32_t deferred_impact_raw_flags,
    std::uint64_t deferred_impact_owner_guid,
    game::ObjectHandle caster_handle,
    game::ObjectHandle target_handle) {
  MissileFlight flight;
  flight.effect_id = effect_id;
  flight.instance_id = instance_id;
  flight.missile_caster_guid = missile_caster_guid;
  flight.missile_cast_count = missile_cast_count;
  flight.caster_guid = caster_guid;
  flight.target_guid = target_guid;
  flight.caster_handle = caster_handle;
  flight.target_handle = target_handle;
  std::memcpy(flight.start_position, start_pos, sizeof(flight.start_position));
  std::memcpy(flight.end_position, end_pos, sizeof(flight.end_position));
  std::memcpy(flight.current_position, start_pos, sizeof(flight.current_position));
  flight.speed = speed;
  flight.base_speed = speed;
  const float initial_dx = end_pos[0] - start_pos[0];
  const float initial_dy = end_pos[1] - start_pos[1];
  const float initial_dz = end_pos[2] - start_pos[2];
  flight.initial_distance = std::sqrt(initial_dx * initial_dx +
                                      initial_dy * initial_dy +
                                      initial_dz * initial_dz);
  flight.spell_visual_id = spell_visual_id;
  flight.spell_id = spell_id;
  flight.motion_id = motion_id;
  flight.salvo_index = salvo_index;
  flight.salvo_count = std::max(salvo_count, 1u);
  flight.impact_kit_id = impact_kit_id;
  flight.deferred_impact_policy = deferred_impact_policy;
  flight.deferred_impact_raw_flags = deferred_impact_raw_flags;
  flight.deferred_impact_owner_guid = deferred_impact_owner_guid;
  flight.impact_result = impact_result;
  flight.reflect_result = reflect_result;

  flight.active = true;

  const std::uint32_t index = static_cast<std::uint32_t>(missile_flights_.size());
  missile_flights_.push_back(std::move(flight));
  return index;
}

void SpellVisualRenderer::ApplyMissilePositionCorrection(
    const game::SpellMissilePositionCorrection& correction) {

  for (auto& flight : missile_flights_) {
    if (!flight.active ||
        flight.missile_caster_guid != correction.caster_guid ||
        flight.missile_cast_count != correction.cast_count) {
      continue;
    }
    flight.collision_position = correction.world_position;
    flight.has_collision_position = true;
  }
}

void SpellVisualRenderer::UpdateMissileFlights(float dt) {
  for (auto& flight : missile_flights_) {
    if (!flight.active) continue;

    static bool logged_missile_flight_update = false;
    if (!logged_missile_flight_update) {
      logged_missile_flight_update = true;
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "Missile flight receipt: spell=" + std::to_string(flight.spell_id) +
              " effect_id=" + std::to_string(flight.effect_id) +
              " instance=" + std::to_string(flight.instance_id));
    }

    (void)ResolveMissileTargetPosition(flight, flight.end_position);

    const bool first_update = !flight.has_advanced;
    const float frame_seconds = first_update ? 0.0f : std::max(dt, 0.0f);
    flight.has_advanced = true;
    flight.elapsed += frame_seconds;
    const float dx = flight.end_position[0] - flight.current_position[0];
    const float dy = flight.end_position[1] - flight.current_position[1];
    const float dz = flight.end_position[2] - flight.current_position[2];
    const float remaining = std::sqrt(dx * dx + dy * dy + dz * dz);
    const std::array<float, 3> previous_position{
        flight.current_position[0], flight.current_position[1],
        flight.current_position[2]};
    float travel = 0.0f;
    float fraction = 0.0f;
    float flight_progress = 0.0f;
    bool arrived = false;
    if (flight.uses_timed_trajectory) {
      const float trajectory_time =
          flight.elapsed * flight.timed_time_scale;

      arrived = !first_update &&
                flight.elapsed >= flight.timed_server_duration;
      if (!arrived) {
        flight.current_position[0] =
            flight.start_position[0] +
            flight.timed_initial_velocity[0] * trajectory_time;
        flight.current_position[1] =
            flight.start_position[1] +
            flight.timed_initial_velocity[1] * trajectory_time;
        flight.current_position[2] =
            flight.start_position[2] +
            flight.timed_initial_velocity[2] * trajectory_time -
            0.5f * flight.timed_gravity * trajectory_time * trajectory_time;
        const float step_x =
            flight.current_position[0] - previous_position[0];
        const float step_y =
            flight.current_position[1] - previous_position[1];
        const float step_z =
            flight.current_position[2] - previous_position[2];
        travel = std::sqrt(step_x * step_x + step_y * step_y +
                           step_z * step_z);
        flight.distance_traveled += travel;
      }
      flight_progress =
          arrived ? 1.0f
                  : std::clamp(trajectory_time /
                                   flight.timed_natural_duration,
                               0.0f, 1.0f);
    } else {
      travel = std::max(0.0f, flight.speed * frame_seconds);
      arrived = !first_update &&
                remaining <= std::max(travel, kMin3DDistance);
      fraction =
          arrived ? 1.0f
                  : (remaining > kMin3DDistance ? travel / remaining : 0.0f);
      flight.current_position[0] += dx * fraction;
      flight.current_position[1] += dy * fraction;
      flight.current_position[2] += dz * fraction;
      flight.distance_traveled += arrived ? remaining : travel;
      flight_progress = flight.initial_distance > kMin3DDistance
                            ? std::clamp(flight.distance_traveled /
                                             flight.initial_distance,
                                         0.0f, 1.0f)
                            : 1.0f;
    }

    if (!flight.uses_timed_trajectory && !arrived &&
        (flight.follow_ground_flags & 1u) != 0u &&
        ground_height_sink_) {
      if (const auto ground = ground_height_sink_(
              flight.current_position[0], flight.current_position[1],
              flight.current_position[2]);
          ground.has_value()) {
        const float support_z = *ground + flight.follow_ground_height;
        flight.current_position[2] = std::max(
            support_z,
            flight.current_position[2] -
                travel * (flight.current_position[2] - support_z) *
                    flight.follow_ground_drop_speed);
      }
      if (flight.follow_ground_approach < 1.0f &&
          flight_progress > flight.follow_ground_approach) {
        float blend = (flight_progress - flight.follow_ground_approach) /
                      (1.0f - flight.follow_ground_approach);
        blend *= blend;
        std::array<float, 3> approach_target{
            flight.end_position[0], flight.end_position[1],
            flight.end_position[2]};
        if ((flight.follow_ground_flags & 4u) != 0u) {
          approach_target = {
              previous_position[0] + 2.0f * dx * fraction,
              previous_position[1] + 2.0f * dy * fraction,
              previous_position[2] + 2.0f * dz * fraction,
          };
        }
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
          flight.current_position[axis] =
              (1.0f - blend) * flight.current_position[axis] +
              blend * approach_target[axis];
        }
      }
    }

    if (!arrived && flight.has_collision_position) {
      const auto squared_distance = [](const auto& position,
                                       const auto& target) {
        const float dx = position[0] - target[0];
        const float dy = position[1] - target[1];
        const float dz = position[2] - target[2];
        return dx * dx + dy * dy + dz * dz;
      };
      if (squared_distance(flight.current_position,
                           flight.collision_position) >
          squared_distance(previous_position, flight.collision_position)) {
        std::copy(previous_position.begin(), previous_position.end(),
                  flight.current_position);
        arrived = true;
      }
    }

    game::SpellMissileMotionOutputs motion_outputs;
    bool has_motion_outputs = false;
    if (flight.motion_id != 0u) {
      game::SpellMissileMotionInputs inputs;
      inputs.progress = flight_progress;
      inputs.time = flight.elapsed;
      inputs.missile_index = flight.salvo_index;
      inputs.missile_count = flight.salvo_count;
      const auto distance_between = [](const float* const a,
                                       const float* const b) {
        const float x = a[0] - b[0];
        const float y = a[1] - b[1];
        const float z = a[2] - b[2];
        return std::sqrt(x * x + y * y + z * z);
      };
      inputs.distance_to_fire_position =
          distance_between(flight.current_position, flight.start_position);
      inputs.distance_to_impact_position =
          distance_between(flight.current_position, flight.end_position);
      inputs.start_distance = inputs.distance_to_fire_position;
      inputs.total_distance = flight.initial_distance;
      inputs.random_value_1 = flight.random_motion[0];
      inputs.random_value_2 = flight.random_motion[1];
      inputs.random_value_3 = flight.random_motion[2];
      inputs.spell_id = flight.spell_id;
      has_motion_outputs =
          game::SpellMissileMotionRegistry::Get().Evaluate(
              flight.motion_id, inputs, motion_outputs);
      if (has_motion_outputs) {
        const auto speed_override = game::ResolveSpellMissileMotionSpeed(
            flight.base_speed, motion_outputs);
        if (speed_override.has_override) {
          flight.speed = speed_override.speed;
        }
      }
    }

    if (m2_system_ && flight.instance_id != 0) {
      std::array<float, 3> visual_position{
          flight.current_position[0], flight.current_position[1],
          flight.current_position[2]};
      const float full_dx = flight.end_position[0] - flight.start_position[0];
      const float full_dy = flight.end_position[1] - flight.start_position[1];
      const float horizontal = std::sqrt(full_dx * full_dx + full_dy * full_dy);
      const float forward_x = horizontal > kMin3DDistance
                                  ? full_dx / horizontal
                                  : 1.0f;
      const float forward_y = horizontal > kMin3DDistance
                                  ? full_dy / horizontal
                                  : 0.0f;
      if (has_motion_outputs) {
        const float angle = static_cast<float>(motion_outputs.trans_angle) /
                            kRadiansToDegrees;
        const float radial =
            static_cast<float>(motion_outputs.trans_magnitude);
        const float radial_right = std::sin(angle) * radial;
        const float radial_up = std::cos(angle) * radial;
        const float right =
            static_cast<float>(motion_outputs.trans_right) + radial_right;
        const float front = static_cast<float>(motion_outputs.trans_front);
        visual_position[0] += -forward_y * right + forward_x * front;
        visual_position[1] += forward_x * right + forward_y * front;
        visual_position[2] +=
            radial_up + static_cast<float>(motion_outputs.trans_up);
      }

      float heading_x = dx;
      float heading_y = dy;
      float heading_z = dz;
      if (flight.uses_timed_trajectory) {
        const float step_x =
            flight.current_position[0] - previous_position[0];
        const float step_y =
            flight.current_position[1] - previous_position[1];
        const float step_z =
            flight.current_position[2] - previous_position[2];
        if (step_x * step_x + step_y * step_y + step_z * step_z >
            kMin3DDistance * kMin3DDistance) {
          heading_x = step_x;
          heading_y = step_y;
          heading_z = step_z;
        }
      }
      const float current_horizontal =
          std::sqrt(heading_x * heading_x + heading_y * heading_y);
      RenderVec3 rotation{
          0.0f,
          -std::atan2(heading_z, current_horizontal) * kRadiansToDegrees,
          std::atan2(heading_y, heading_x) * kRadiansToDegrees,
      };
      float scale = flight.model_scale;
      if (has_motion_outputs) {
        rotation[0] += static_cast<float>(motion_outputs.model_roll);
        rotation[1] += static_cast<float>(motion_outputs.model_pitch);
        rotation[2] += static_cast<float>(motion_outputs.model_yaw);
        if (motion_outputs.model_scale > 0.0) {
          scale *= static_cast<float>(motion_outputs.model_scale);
        }
      }
      const auto transform_status = m2_system_->SetTransform(
          flight.instance_id,
          ToRenderVec3(RenderVec3View{visual_position}), rotation, scale);
      if (m2::IsTerminalM2ResultStatus(transform_status)) {
        const auto instance_id = flight.instance_id;
        StopMissileSound(flight);
        DestroyM2Instance(flight.instance_id);
        model_instances_.erase(instance_id);
        flight.active = false;
        continue;
      }
    }
    if (flight.sound_handle != 0u && sound_position_sink_) {
      sound_position_sink_(flight.sound_handle, flight.current_position);
    }

    if (arrived) {
      if (!flight.reflected &&
          flight.impact_result == kMissileImpactReflect) {

        const std::array<float, 3> original_fire_position{
            flight.start_position[0], flight.start_position[1],
            flight.start_position[2]};
        std::copy(flight.current_position, flight.current_position + 3,
                  flight.start_position);
        std::copy(original_fire_position.begin(), original_fire_position.end(),
                  flight.end_position);
        flight.target_guid = flight.caster_guid;
        flight.target_handle = flight.caster_handle;
        if (const auto* const caster = FindObject(flight.caster_handle);
            caster != nullptr) {
          flight.target_fallback_offset = {
              original_fire_position[0] - caster->x,
              original_fire_position[1] - caster->y,
              original_fire_position[2] - caster->z,
          };
        }
        (void)ResolveMissileTargetPosition(flight, flight.end_position);

        const float return_dx =
            flight.end_position[0] - flight.start_position[0];
        const float return_dy =
            flight.end_position[1] - flight.start_position[1];
        const float return_dz =
            flight.end_position[2] - flight.start_position[2];
        flight.initial_distance =
            std::sqrt(return_dx * return_dx + return_dy * return_dy +
                      return_dz * return_dz);
        flight.distance_traveled = 0.0f;
        flight.elapsed = 0.0f;
        flight.speed = flight.base_speed;
        flight.uses_timed_trajectory = false;
        flight.timed_initial_velocity = {};
        flight.timed_gravity = 0.0f;
        flight.timed_natural_duration = 0.0f;
        flight.timed_server_duration = 0.0f;
        flight.timed_time_scale = 1.0f;
        flight.missile_flags &= ~0x00010000u;
        flight.impact_result = flight.reflect_result;
        flight.reflected = true;
        if (const auto model = model_instances_.find(flight.instance_id);
            model != model_instances_.end()) {
          model->second.m2_event_owner = flight.target_handle;
          model->second.raw_flags = flight.missile_flags;
        }

        flight.collision_position = {};
        flight.has_collision_position = false;
        flight.has_advanced = false;

        if (flight.impact_result != kMissileImpactNone) {
          flight.impact_kit_id = 0u;
        }
        auto& random = core::GetClientStartupAdlerSeedState();
        flight.random_motion = {
            foundation::hashing::AdlerSeedNextUnitFloat(random),
            foundation::hashing::AdlerSeedNextUnitFloat(random),
            foundation::hashing::AdlerSeedNextUnitFloat(random),
        };
        continue;
      }

      flight.active = false;

      const auto instance_id = flight.instance_id;
      StopMissileSound(flight);
      DestroyM2Instance(flight.instance_id);
      model_instances_.erase(instance_id);

      if (flight.impact_kit_id != 0) {
        if (flight.deferred_impact_policy ==
            game::SpellVisualDeferredImpactPolicy::kDestLocArea) {
          CreateDestLocAreaImpact(
              flight.spell_visual_id, flight.spell_id,
              flight.impact_kit_id, flight.deferred_impact_raw_flags,
              flight.deferred_impact_owner_guid,
              flight.current_position);
        } else {
          DispatchGenericDeferredImpact(
              flight.caster_handle, flight.target_handle,
              flight.target_guid, flight.spell_id,
              flight.spell_visual_id, flight.impact_kit_id,
              flight.current_position);
        }
      }
    }
  }
}

void SpellVisualRenderer::DestroyEffect(std::uint32_t effect_id) {
  StopEffectSound(effect_id);

  for (auto it = model_instances_.begin(); it != model_instances_.end();) {
    if (it->second.effect_id == effect_id) {
      DestroyM2Instance(it->second.instance_id);
      it = model_instances_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto& flight : missile_flights_) {
    if (flight.effect_id == effect_id) {
      StopMissileSound(flight);
      flight.active = false;
    }
  }

  auto area_it = area_effects_.find(effect_id);
  if (area_it != area_effects_.end()) {
    for (auto inst_id : area_it->second.instance_ids) {
      DestroyM2Instance(inst_id);
    }
    area_effects_.erase(area_it);
  }
  std::erase_if(aura_effects_, [effect_id](const auto& entry) {
    return entry.second == effect_id;
  });
  std::erase_if(channel_effects_, [effect_id](const auto& entry) {
    return entry.second == effect_id;
  });
  std::erase_if(cast_effects_, [effect_id](const auto& entry) {
    return entry.second == effect_id;
  });
}

void SpellVisualRenderer::ConsumePresentationEvent(
    const game::SpellVisualPresentationEvent& event) {
  const AuraVisualKey aura_key{.owner = event.owner, .slot = event.aura_slot};
  const SpellLifecycleVisualKey lifecycle_key{
      .owner = event.owner,
      .spell_id = event.spell_id,
      .dispatch_type = event.dispatch_type,
      .kit_id = event.kit_id};
  if (event.action == game::SpellVisualLifecycleAction::kAuraStop) {
    if (const auto found = aura_effects_.find(aura_key);
        found != aura_effects_.end()) {
      const auto effect_id = found->second;
      aura_effects_.erase(found);
      DestroyEffect(effect_id);
    }
    return;
  }
  if (event.action == game::SpellVisualLifecycleAction::kChannelStop) {
    std::vector<std::uint32_t> stopped;
    for (const auto& [key, id] : channel_effects_) {
      if (key.owner == event.owner && key.spell_id == event.spell_id) {
        stopped.push_back(id);
      }
    }
    for (const auto id : stopped) {
      DestroyEffect(id);
    }
    return;
  }
  if (event.action == game::SpellVisualLifecycleAction::kCastStop) {
    std::vector<std::uint32_t> stopped;
    for (const auto& [key, id] : cast_effects_) {
      if (key.owner == event.owner && key.spell_id == event.spell_id) {
        stopped.push_back(id);
      }
    }
    for (const auto id : stopped) {
      DestroyEffect(id);
    }
    return;
  }

  if (event.action == game::SpellVisualLifecycleAction::kAuraStart) {
    if (const auto found = aura_effects_.find(aura_key);
        found != aura_effects_.end()) {
      const auto old_effect_id = found->second;
      aura_effects_.erase(found);
      DestroyEffect(old_effect_id);
    }
  }
  if (event.action == game::SpellVisualLifecycleAction::kChannelStart) {
    if (const auto found = channel_effects_.find(lifecycle_key);
        found != channel_effects_.end()) {

      return;
    }
    std::vector<std::uint32_t> stale_channel_effects;
    for (const auto& [key, id] : channel_effects_) {

      if (key.owner == event.owner &&
          (key.spell_id != event.spell_id ||
           (key.dispatch_type == event.dispatch_type &&
            key.kit_id != event.kit_id))) {
        stale_channel_effects.push_back(id);
      }
    }
    for (const auto id : stale_channel_effects) {
      DestroyEffect(id);
    }
  }
  if (event.action == game::SpellVisualLifecycleAction::kCastStart) {
    std::vector<std::uint32_t> stale_cast_effects;
    for (const auto& [key, id] : cast_effects_) {
      if (key.owner == event.owner) {
        stale_cast_effects.push_back(id);
      }
    }
    for (const auto id : stale_cast_effects) {
      DestroyEffect(id);
    }
  }

  const auto effect_id = NextEffectId();
  float duration = 0.0f;
  if (event.phase == game::SpellVisualPresentationPhase::kStateDone) {
    duration = kStateDoneEffectDuration;
  } else if (event.action == game::SpellVisualLifecycleAction::kTransient) {
    duration = event.dispatch_type == 1u ? kImpactEffectDuration
               : event.dispatch_type == 2u ? kPrecastEffectDuration
                                           : 1.0f;
  }
  SpawnPresentationModels(event, effect_id, duration);

  if (event.missile.has_value()) {
    const auto caster_handle =
        event.owner.guid.IsEmpty()
            ? ResolveObjectHandle(event.missile_caster_guid)
            : event.owner;
    const auto caster_guid =
        !caster_handle.guid.IsEmpty()
            ? caster_handle.guid.GetRawValue()
            : event.missile_caster_guid;
    (void)CreateMissileEffect(*event.missile,
                              event.spell_visual_id,
                              event.spell_id,
                              event.missile_caster_guid,
                              event.missile_cast_count,
                              caster_guid,
                              event.missile_target_guid,
                              event.missile_source_position.data(),
                              event.missile_target_position.data(),
                              event.missile_speed,
                              event.deferred_impact_kit_id,
                              event.missile_impact_result,
                              event.missile_reflect_result,
                              event.missile_uses_timed_trajectory,
                              event.missile_trajectory_pitch,
                              event.missile_trajectory_speed,
                              event.missile_trajectory_duration_ms,
                              event.deferred_impact_policy,
                              event.deferred_impact_raw_flags,
                              event.deferred_impact_owner_guid,
                              caster_handle,
                              ResolveObjectHandle(event.missile_target_guid));
  }
  StartEffectSound(effect_id, event);
  if (event.camera_shake_id != 0u && camera_shake_sink_) {
    const auto& position = event.world_position.has_value()
                               ? *event.world_position
                               : event.owner_position;
    camera_shake_sink_(event.camera_shake_id, position);
  }
  if (event.action == game::SpellVisualLifecycleAction::kAuraStart) {
    aura_effects_.emplace(aura_key, effect_id);
  } else if (event.action ==
             game::SpellVisualLifecycleAction::kChannelStart) {
    channel_effects_.emplace(lifecycle_key, effect_id);
  } else if (event.action == game::SpellVisualLifecycleAction::kCastStart) {
    cast_effects_.emplace(lifecycle_key, effect_id);
  }
}

void SpellVisualRenderer::DestroyEffectsForObject(
    const game::ObjectHandle owner) {
  std::vector<std::uint32_t> to_remove;
  for (const auto& [_, inst] : model_instances_) {
    if (inst.parent_handle == owner) {
      to_remove.push_back(inst.effect_id);
    }
  }
  for (const auto& [key, id] : aura_effects_) {
    if (key.owner == owner) {
      to_remove.push_back(id);
    }
  }
  for (const auto& [key, id] : channel_effects_) {
    if (key.owner == owner) {
      to_remove.push_back(id);
    }
  }
  for (const auto& [key, id] : cast_effects_) {
    if (key.owner == owner) {
      to_remove.push_back(id);
    }
  }
  for (const auto& [id, sound] : effect_sounds_) {
    if (sound.owner == owner) {
      to_remove.push_back(id);
    }
  }

  std::sort(to_remove.begin(), to_remove.end());
  to_remove.erase(std::unique(to_remove.begin(), to_remove.end()), to_remove.end());

  for (const auto id : to_remove) {
    DestroyEffect(id);
  }
  std::erase_if(aura_effects_, [owner](const auto& entry) {
    return entry.first.owner == owner;
  });
  std::erase_if(channel_effects_, [owner](const auto& entry) {
    return entry.first.owner == owner;
  });
  std::erase_if(cast_effects_, [owner](const auto& entry) {
    return entry.first.owner == owner;
  });
}

void SpellVisualRenderer::Update(float dt) {
  if (!initialized_) return;

  std::vector<std::uint32_t> dead_lifecycle_effects;
  const auto collect_dead_owner =
      [this, &dead_lifecycle_effects](const auto& effects) {
        for (const auto& [key, id] : effects) {
          const auto* const owner = FindObject(key.owner);
          if (owner != nullptr && owner->health == 0u) {
            dead_lifecycle_effects.push_back(id);
          }
        }
      };
  collect_dead_owner(cast_effects_);
  collect_dead_owner(channel_effects_);
  std::sort(dead_lifecycle_effects.begin(), dead_lifecycle_effects.end());
  dead_lifecycle_effects.erase(
      std::unique(dead_lifecycle_effects.begin(),
                  dead_lifecycle_effects.end()),
      dead_lifecycle_effects.end());
  for (const auto effect_id : dead_lifecycle_effects) {
    DestroyEffect(effect_id);
  }

  UpdateMissileFlights(dt);

  missile_flights_.erase(
      std::remove_if(missile_flights_.begin(), missile_flights_.end(),
          [](const MissileFlight& f) { return !f.active; }),
      missile_flights_.end());

  for (auto it = model_instances_.begin(); it != model_instances_.end();) {
    auto& inst = it->second;
    if (!inst.active) {
      DestroyM2Instance(inst.instance_id);
      it = model_instances_.erase(it);
      continue;
    }

    inst.elapsed += dt;

    if (inst.instance_id != 0u) {
      const auto animation_status =
          m2_system_->UpdateAnimation(inst.instance_id, std::max(dt, 0.0f));
      if (m2::IsTerminalM2ResultStatus(animation_status)) {
        for (auto& flight : missile_flights_) {
          if (flight.active && flight.instance_id == inst.instance_id) {
            StopMissileSound(flight);
            flight.active = false;
          }
        }
        DestroyM2Instance(inst.instance_id);
        inst.active = false;
        ++it;
        continue;
      }
    }

    if (inst.parent_guid != 0 && inst.attached) {
      UpdateModelInstancePosition(inst);
    }

    if (inst.duration > 0.0f && inst.elapsed >= inst.duration) {
      if (!inst.fading) {
        FadeOutModelInstance(inst);
      }
    }

    if (inst.fading) {
      if (inst.elapsed >= inst.duration + inst.fade_out_duration) {
        inst.active = false;
      }
    }

    ++it;
  }

  for (auto it = area_effects_.begin(); it != area_effects_.end();) {
    auto& area = it->second;
    area.elapsed += dt;

    for (auto& instance_id : area.instance_ids) {
      if (instance_id == 0u) {
        continue;
      }
      const auto animation_status =
          m2_system_->UpdateAnimation(instance_id, std::max(dt, 0.0f));
      if (m2::IsTerminalM2ResultStatus(animation_status)) {
        DestroyM2Instance(instance_id);

        instance_id = 0u;
      }
    }

    if (area.duration > 0.0f && area.elapsed >= area.duration) {
      area.active = false;
    }

    if (!area.active) {
      StopEffectSound(area.effect_id);
      for (auto inst_id : area.instance_ids) {
        DestroyM2Instance(inst_id);
      }
      it = area_effects_.erase(it);
    } else {
      ++it;
    }
  }
}

m2::M2RenderFrameResult SpellVisualRenderer::Render(
    const std::uint16_t view_id, const float* const view_matrix,
    const m2::M2RenderPassScope pass_scope,
    m2::M2TransparentDrawOrder* const transparent_draw_order) {
  m2::M2RenderFrameResult frame_result{};
  if (!initialized_ || m2_system_ == nullptr) {
    frame_result.status = m2::M2ResultStatus::kNotReady;
    frame_result.reason = m2::M2ResultReason::kInvalidHandle;
    frame_result.detail = "spell visual renderer is not initialized";
    return frame_result;
  }

  render_batch_ids_scratch_.clear();
  render_walk_rows_scratch_.clear();
  const auto queue_instance = [&](const std::uint32_t instance_id,
                                  const std::string_view model_path) {
    const auto uniforms = m2_system_->SetBatchUniforms(instance_id, world_batch_uniforms_);
    if (uniforms != m2::M2ResultStatus::kReady) {
      render_walk_rows_scratch_.push_back(
          {.batch_index = kNotBatched,
           .setup_result = {.status = uniforms,
                            .reason = m2::M2ResultReason::kInvalidHandle,
                            .detail = std::string(model_path)}});
      return;
    }
    render_walk_rows_scratch_.push_back(
        {.batch_index = render_batch_ids_scratch_.size(), .setup_result = {}});
    render_batch_ids_scratch_.push_back(instance_id);
  };
  for (const auto& [_, inst] : model_instances_) {
    if (!inst.active || !inst.visible || inst.instance_id == 0u) {
      continue;
    }
    queue_instance(inst.instance_id, inst.model_path);
  }
  for (const auto& [_, area] : area_effects_) {
    if (!area.active) {
      continue;
    }
    for (const std::uint32_t instance_id : area.instance_ids) {
      if (instance_id != 0u) {
        queue_instance(instance_id, {});
      }
    }
  }

  render_batch_draw_ordinals_scratch_.clear();
  if (!render_batch_ids_scratch_.empty()) {
    if (m2::M2RenderPassScopeIncludesTransparent(pass_scope) &&
        transparent_draw_order != nullptr) {
      const std::uint32_t first_ordinal = transparent_draw_order->Reserve(
          static_cast<std::uint32_t>(render_batch_ids_scratch_.size()));
      render_batch_draw_ordinals_scratch_.resize(render_batch_ids_scratch_.size());
      std::iota(render_batch_draw_ordinals_scratch_.begin(),
                render_batch_draw_ordinals_scratch_.end(), first_ordinal);
    }
    render_batch_results_scratch_.assign(render_batch_ids_scratch_.size(), {});
    const m2::M2TransparentDrawOrdinalScope draw_order_scope(
        render_batch_draw_ordinals_scratch_);
    m2_system_->RenderInstanceBatch(view_id, render_batch_ids_scratch_,
                                    RenderMatrix4x4View{view_matrix, 16u}, pass_scope,
                                    m2_system_->frame_job_system(),
                                    kSpellVisualInstanceRenderMicroseconds,
                                    render_batch_results_scratch_);
  }
  for (const RenderWalkRow& row : render_walk_rows_scratch_) {
    frame_result.AddInstanceResult(row.batch_index == kNotBatched
                                       ? row.setup_result
                                       : render_batch_results_scratch_[row.batch_index]);
  }
  return frame_result;
}

void SpellVisualRenderer::Clear() {
  while (!effect_sounds_.empty()) {
    StopEffectSound(effect_sounds_.begin()->first);
  }
  for (auto& flight : missile_flights_) {
    StopMissileSound(flight);
  }
  for (auto& [_, inst] : model_instances_) {
    DestroyM2Instance(inst.instance_id);
  }
  model_instances_.clear();

  for (auto& [_, area] : area_effects_) {
    for (auto inst_id : area.instance_ids) {
      DestroyM2Instance(inst_id);
    }
  }
  area_effects_.clear();

  missile_flights_.clear();
  aura_effects_.clear();
  channel_effects_.clear();
  cast_effects_.clear();
}

bool SpellVisualRenderer::ResolveKit(
    std::uint32_t kit_id,
    ResolvedKitModel& out) const {
  if (!dbc_ || kit_id == 0) return false;

  const auto* kit = dbc_->spell_visual_kit().LookupEntry(kit_id);
  if (!kit) return false;

  out.sound_kit_id = kit->sound_id;

  const auto& model_attach_entries = dbc_->spell_visual_kit_model_attach().entries();
  for (const auto& attach : model_attach_entries) {
    if (attach.parent_spell_visual_kit_id == kit_id) {
      std::string model_path;
      float model_scale = 1.0f;
      if (ResolveEffectName(attach.spell_visual_effect_name_id,
                            model_path, model_scale)) {
        out.model_path = model_path;
        out.scale = model_scale;
        out.attachment_id = attach.attachment_id;
        out.offset[0] = attach.offset_x;
        out.offset[1] = attach.offset_y;
        out.offset[2] = attach.offset_z;
        out.rotation[0] = attach.pitch;
        out.rotation[1] = attach.yaw;
        out.rotation[2] = attach.roll;
        out.has_model_attach = true;
        return true;
      }
    }
  }

  const std::uint32_t effect_ids[] = {
      kit->base_effect, kit->chest_effect, kit->head_effect,
      kit->left_hand_effect, kit->right_hand_effect,
      kit->breath_effect, kit->left_weapon_effect, kit->right_weapon_effect,
      kit->special1_effect, kit->special2_effect, kit->special3_effect,
      kit->world_effect};

  for (const auto effect_id : effect_ids) {
    std::string model_path;
    float model_scale = 1.0f;
    if (ResolveEffectName(effect_id, model_path, model_scale)) {
      out.model_path = model_path;
      out.scale = model_scale;
      return true;
    }
  }

  return false;
}

bool SpellVisualRenderer::ResolveEffectName(
    std::uint32_t effect_name_id,
    std::string& out_model_path,
    float& out_scale) const {
  if (!dbc_ || effect_name_id == 0) return false;

  const auto* effect_name =
      dbc_->spell_visual_effect_name().LookupEntry(effect_name_id);
  if (!effect_name) {
    out_model_path = std::string(m2::kDefaultM2FallbackModelPath);
    out_scale = 1.0f;
    return true;
  }

  out_model_path = std::string(effect_name->file_path);
  out_scale = effect_name->scale;
  if (out_model_path.empty()) {
    out_model_path = std::string(m2::kDefaultM2FallbackModelPath);
    out_scale = 1.0f;
  }
  return true;
}

m2::M2ModelInstanceLoadResult SpellVisualRenderer::CreateModelInstance(
    const ResolvedKitModel& kit_model,
    const float* position) {
  if (!m2_system_ || kit_model.model_path.empty() || !position) {
    return {.status = m2::M2ResultStatus::kFailed,
            .reason = m2::M2ResultReason::kInvalidHandle,
            .detail = "spell visual model instance request is incomplete"};
  }

  auto instance_result = m2_system_->LoadModelInstanceWithFallback(kit_model.model_path);
  if (instance_result.status != m2::M2ResultStatus::kReady ||
      instance_result.instance_id == 0) {
    LogSpellVisualM2Failure("load/create", kit_model.model_path, instance_result);
    return instance_result;
  }
  std::uint32_t instance_id = instance_result.instance_id;

  m2::M2ResultStatus presentation = m2_system_->SetVisible(instance_id, true);
  presentation = m2::MergeM2ResultStatus(
      presentation, m2_system_->SetEffectEmittersEnabled(instance_id, true));
  presentation = m2::MergeM2ResultStatus(
      presentation, m2_system_->SetAlpha(instance_id, 1.0f));
  if (presentation != m2::M2ResultStatus::kReady) {
    DestroyM2Instance(instance_id);
    return {.status = presentation,
            .reason = m2::M2ResultReason::kInvalidHandle,
            .detail = kit_model.model_path,
            .model_id = instance_result.model_id};
  }

  const bool has_rotation =
      kit_model.rotation[0] != 0.0f || kit_model.rotation[1] != 0.0f ||
      kit_model.rotation[2] != 0.0f;
  const std::optional<RenderVec3> rotation_degrees =
      has_rotation ? std::optional<RenderVec3>{ToRenderVec3(RenderVec3View{kit_model.rotation, 3u})}
                   : std::nullopt;
  if (m2_system_->SetTransform(instance_id,
                               ToRenderVec3(RenderVec3View{position, 3u}),
                               rotation_degrees,
                               kit_model.scale) != m2::M2ResultStatus::kReady) {
    DestroyM2Instance(instance_id);
    return {.status = m2::M2ResultStatus::kFailed,
            .reason = m2::M2ResultReason::kInvalidTransform,
            .detail = kit_model.model_path,
            .model_id = instance_result.model_id};
  }

  const auto animation_status =
      BindDefaultModelSequence(instance_result.model_id, instance_id);
  if (m2::IsTerminalM2ResultStatus(animation_status)) {
    DestroyM2Instance(instance_id);
    return {.status = animation_status,
            .reason = m2::M2ResultReason::kInvalidQuery,
            .detail = kit_model.model_path,
            .model_id = instance_result.model_id};
  }

  return instance_result;
}

m2::M2ResultStatus SpellVisualRenderer::BindDefaultModelSequence(
    const std::uint32_t model_id, const std::uint32_t instance_id) {
  if (m2_system_ == nullptr || model_id == 0u || instance_id == 0u) {
    return m2::M2ResultStatus::kFailed;
  }

  const auto animations = m2_system_->QueryModelAnimationList(model_id);
  if (animations.status != m2::M2ResultStatus::kReady) {
    return animations.status;
  }
  if (animations.animations.empty()) {
    return m2::M2ResultStatus::kReady;
  }

  return m2_system_->SetAnimationSequenceSample(
      instance_id, animations.animations.front().sequence_index, 0u, 1.0f);
}

void SpellVisualRenderer::DestroyM2Instance(std::uint32_t& instance_id) {
  if (instance_id == 0u) {
    return;
  }
  if (m2_system_ == nullptr) {
    instance_id = 0u;
    return;
  }

  if (m2_instance_retired_sink_) {
    m2_instance_retired_sink_(instance_id);
  }
  const auto clear_status =
      m2_system_->ClearTriggeredEventCallback(instance_id);
  const auto destroy_status = m2_system_->DestroyInstance(instance_id);
  const auto status = m2::MergeM2ResultStatus(clear_status, destroy_status);
  if (status != m2::M2ResultStatus::kReady) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
              std::string("SpellVisualRenderer: M2 instance destroy ") +
                  m2::M2ResultStatusName(status));
  }
  instance_id = 0u;
}

bool SpellVisualRenderer::BindM2EventCallback(
    const std::uint32_t instance_id) {
  if (m2_system_ == nullptr || instance_id == 0u) {
    return false;
  }
  const auto status = m2_system_->SetTriggeredEventCallback(
      instance_id,
      [this, instance_id](const m2::M2TriggeredEvent& event) {
        if (!m2_event_sink_) {
          return;
        }
        const auto found = model_instances_.find(instance_id);
        if (found == model_instances_.end() || !found->second.active ||
            found->second.instance_id != instance_id) {
          return;
        }
        const auto& instance = found->second;
        m2_event_sink_({.effect_id = instance.effect_id,
                        .instance_id = instance_id,
                        .owner = instance.m2_event_owner,
                        .raw_flags = instance.raw_flags,
                        .kind = instance.m2_event_kind,
                        .required_owner_guid =
                            instance.required_m2_event_owner_guid,
                        .requires_owner_resolution =
                            instance.m2_events_require_owner_resolution,
                        .event = event});
      });
  return status == m2::M2ResultStatus::kReady;
}

void SpellVisualRenderer::UpdateModelInstancePosition(
    SpellVisualModelInstance& inst) {
  if (inst.parent_guid == 0 || !m2_system_) return;

  if (inst.attached && inst.attachment_id >= 0) {
    (void)ApplyOwnerAttachmentTransform(inst);
    return;
  }

  float px = 0.0f, py = 0.0f, pz = 0.0f;
  if (!GetObjectPosition(inst.parent_guid, px, py, pz)) return;

  inst.position[0] = px + inst.offset[0];
  inst.position[1] = py + inst.offset[1];
  inst.position[2] = pz + inst.offset[2];

  const auto position_status = m2_system_->SetPosition(
      inst.instance_id, ToRenderVec3(RenderVec3View{inst.position, 3u}));
  if (m2::IsTerminalM2ResultStatus(position_status)) {
    DestroyM2Instance(inst.instance_id);
    inst.active = false;
  }
}

bool SpellVisualRenderer::ApplyOwnerAttachmentTransform(
    SpellVisualModelInstance& inst) {
  if (m2_system_ == nullptr || !owner_m2_instance_resolver_ ||
      inst.attachment_id < 0) {
    return false;
  }
  const auto owner_instance = owner_m2_instance_resolver_(inst.parent_handle);
  if (owner_instance == 0u) {
    return false;
  }
  const auto attachment = m2_system_->QueryAttachmentTransformMatrix(
      owner_instance, static_cast<std::uint32_t>(inst.attachment_id));
  if (attachment.status != m2::M2ResultStatus::kReady) {
    return false;
  }

  auto local = kRenderIdentityMatrix4x4;
  local = MultiplyMatrix4x4(
      BuildRotationMatrix4x4X(inst.rotation[0]), local);
  local = MultiplyMatrix4x4(
      BuildRotationMatrix4x4Y(inst.rotation[1]), local);
  local = MultiplyMatrix4x4(
      BuildRotationMatrix4x4Z(inst.rotation[2]), local);
  local = ScaleMatrix4x4BasisRows(
      local, RenderVec3{inst.scale, inst.scale, inst.scale});
  local[12] = inst.offset[0];
  local[13] = inst.offset[1];
  local[14] = inst.offset[2];
  const auto world = MultiplyMatrix4x4(local, attachment.matrix);
  const auto status = m2_system_->SetWorldTransformMatrix(inst.instance_id, world);
  if (m2::IsTerminalM2ResultStatus(status)) {
    DestroyM2Instance(inst.instance_id);
    inst.active = false;
  }
  inst.position[0] = world[12];
  inst.position[1] = world[13];
  inst.position[2] = world[14];
  inst.attachment_resolved = status == m2::M2ResultStatus::kReady;
  inst.visible = inst.attachment_resolved;
  return status == m2::M2ResultStatus::kReady;
}

void SpellVisualRenderer::FadeOutModelInstance(
    SpellVisualModelInstance& inst) {
  inst.fading = true;
  DestroyM2Instance(inst.instance_id);
  inst.active = false;
}

void SpellVisualRenderer::PlaySoundKit(
    std::uint32_t sound_kit_id,
    const float* position) {
  if (sound_kit_id == 0) return;

  if (sound_kit_sink_) {
    sound_kit_sink_(sound_kit_id, position);
  }
}

void SpellVisualRenderer::StartEffectSound(
    const std::uint32_t effect_id,
    const game::SpellVisualPresentationEvent& event) {
  if (event.sound_kit_id == 0u || event.sound_kit_id == 0xFFFFFFFFu ||
      (event.raw_flags & kEffectSoundSuppressed) != 0u) {
    return;
  }

  const auto* const position = event.world_position.has_value()
                                   ? event.world_position->data()
                                   : event.owner_position.data();
  if (!effect_sound_start_sink_) {
    PlaySoundKit(event.sound_kit_id, position);
    return;
  }

  const auto playback_mode =
      (event.raw_flags & kEffectSoundModeLoop) != 0u
          ? SpellSoundPlaybackMode::kForceLoop
          : SpellSoundPlaybackMode::kForceOneShot;
  const bool bind_to_owner =
      (event.raw_flags & kEffectSoundDoNotBindToOwner) == 0u &&
      !event.owner.guid.IsEmpty();
  const auto handle = effect_sound_start_sink_(
      event.sound_kit_id, position, playback_mode,
      bind_to_owner ? event.owner.guid.GetRawValue() : 0u);
  if (handle == 0u) {
    return;
  }

  if (event.action != game::SpellVisualLifecycleAction::kTransient) {
    effect_sounds_[effect_id] = EffectSound{
        .handle = handle,
        .owner = event.owner,
    };
  }
}

void SpellVisualRenderer::StopEffectSound(const std::uint32_t effect_id) {
  const auto found = effect_sounds_.find(effect_id);
  if (found == effect_sounds_.end()) {
    return;
  }
  if (found->second.handle != 0u && sound_stop_sink_) {

    sound_stop_sink_(found->second.handle, 0.15F);
  }
  effect_sounds_.erase(found);
}

void SpellVisualRenderer::StopMissileSound(MissileFlight& flight) {
  if (flight.sound_handle == 0u) {
    return;
  }
  if (sound_stop_sink_) {
    sound_stop_sink_(flight.sound_handle, 0.15F);
  }
  flight.sound_handle = 0u;
}

std::uint32_t SpellVisualRenderer::NextEffectId() {
  return next_effect_id_++;
}

bool SpellVisualRenderer::GetObjectPosition(
    std::uint64_t guid,
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

const game::ObjectPresentationRecord* SpellVisualRenderer::FindObject(
    const game::ObjectHandle handle) const {
  if (objects_ == nullptr || handle.guid.IsEmpty()) {
    return nullptr;
  }
  const auto raw_guid = handle.guid.GetRawValue();
  const auto object = std::lower_bound(
      objects_->active.begin(), objects_->active.end(), raw_guid,
      [](const game::ObjectPresentationRecord& record,
         const std::uint64_t candidate) {
        return record.handle.guid.GetRawValue() < candidate;
      });
  return object != objects_->active.end() && object->handle == handle
             ? &*object
             : nullptr;
}

game::ObjectHandle SpellVisualRenderer::ResolveObjectHandle(
    const std::uint64_t guid) const {
  if (objects_ == nullptr || guid == 0u) {
    return {};
  }
  const auto object = std::lower_bound(
      objects_->active.begin(), objects_->active.end(), guid,
      [](const game::ObjectPresentationRecord& record,
         const std::uint64_t candidate) {
        return record.handle.guid.GetRawValue() < candidate;
      });
  return object != objects_->active.end() &&
                 object->handle.guid.GetRawValue() == guid
             ? object->handle
             : game::ObjectHandle{};
}

bool SpellVisualRenderer::ResolveMissileTargetPosition(
    const MissileFlight& flight, float* const output) const {
  if (output == nullptr) {
    return false;
  }
  const auto* target = FindObject(flight.target_handle);
  if (target == nullptr) {
    return false;
  }

  if (m2_system_ != nullptr && owner_m2_instance_resolver_ &&
      flight.target_attachment_id >= 0) {
    const auto target_instance =
        owner_m2_instance_resolver_(flight.target_handle);
    if (target_instance != 0u) {
      const auto attachment = m2_system_->QueryAttachmentPosition(
          target_instance,
          game::ResolveSpellAttachmentLookupIndex(
              static_cast<std::uint32_t>(flight.target_attachment_id),
              flight.target_attachment_uses_raw_index),
          ToRenderVec3(RenderVec3View{flight.target_attachment_offset}));
      if (attachment.status == m2::M2ResultStatus::kReady) {
        std::copy(attachment.position.begin(), attachment.position.end(),
                  output);
        return true;
      }
    }
  }

  output[0] = target->x + flight.target_fallback_offset[0];
  output[1] = target->y + flight.target_fallback_offset[1];
  output[2] = target->z + flight.target_fallback_offset[2];
  return true;
}

void SpellVisualRenderer::SpawnKitModels(
    std::uint32_t kit_id,
    std::uint64_t caster_guid,
    std::uint64_t target_guid,
    const float* position,
    VisualPhase phase,
    std::uint32_t effect_id,
    float duration) {
  if (kit_id == 0u || m2_system_ == nullptr || dbc_ == nullptr) return;
  const auto* const kit = dbc_->spell_visual_kit().LookupEntry(kit_id);
  if (kit == nullptr) return;

  const bool target_owned =
      phase == VisualPhase::kImpact || phase == VisualPhase::kTargetImpact;
  const auto owner_guid = target_owned && target_guid != 0u
                              ? target_guid
                              : caster_guid;
  const auto owner_handle = ResolveObjectHandle(owner_guid);

  game::SpellVisualPresentationEvent event{};
  event.owner = owner_handle;
  event.kit_id = kit_id;
  if (const auto* const owner = FindObject(owner_handle); owner != nullptr) {
    event.owner_position = {owner->x, owner->y, owner->z};
  } else if (position != nullptr) {
    event.owner_position = {position[0], position[1], position[2]};
  } else {
    return;
  }
  if (position != nullptr) {
    event.world_position =
        std::array<float, 3>{position[0], position[1], position[2]};
  }

  const auto append_effect =
      [this, &event](const std::uint32_t effect_name_id,
                     const std::int32_t attachment_id,
                     const std::uint32_t source_field_index,
                     const bool world_space, const bool from_model_attach,
                     const std::uintptr_t transform_key,
                     const std::array<float, 3>& offset = {},
                     const std::array<float, 3>& rotation = {}) {
        if (effect_name_id == 0u) return;
        const auto* const effect =
            dbc_->spell_visual_effect_name().LookupEntry(effect_name_id);
        if (effect == nullptr || effect->file_path.empty()) return;
        event.effects.push_back(game::SpellVisualPresentationEffect{
            .effect_name_id = effect_name_id,
            .model_path = std::string(effect->file_path),
            .resource_scale = effect->scale,
            .attachment_id = attachment_id,
            .source_field_index = source_field_index,
            .transform_key = transform_key,
            .world_space = world_space,
            .uses_explicit_world_position = world_space &&
                                            event.world_position.has_value(),
            .from_model_attach = from_model_attach,
            .offset = offset,
            .rotation = rotation,
        });
      };

  append_effect(kit->head_effect, 20, 3, false, false, 0u);
  append_effect(kit->chest_effect, 34, 4, false, false, 0u);
  append_effect(kit->base_effect, 19, 5, false, false, 0u);
  append_effect(kit->left_hand_effect, 21, 6, false, false, 0u);
  append_effect(kit->right_hand_effect, 22, 7, false, false, 0u);
  append_effect(kit->breath_effect, 17, 8, false, false, 0u);
  if (const auto* const owner = FindObject(owner_handle);
      owner != nullptr && owner->type_id == game::TypeID::kPlayer) {
    append_effect(kit->left_weapon_effect, 2, 9, false, false, 0u);
    append_effect(kit->right_weapon_effect, 1, 10, false, false, 0u);
  }
  append_effect(kit->special1_effect, 23, 11, false, false, 0u);
  append_effect(kit->special2_effect, 24, 12, false, false, 0u);
  append_effect(kit->special3_effect, 25, 13, false, false, 0u);
  append_effect(kit->world_effect, -1, 14, true, false, 0u);
  for (const auto& attach : dbc_->spell_visual_kit_model_attach().entries()) {
    if (attach.parent_spell_visual_kit_id != kit_id) continue;
    const bool world_space =
        static_cast<std::int32_t>(attach.attachment_id) == -1;
    append_effect(attach.spell_visual_effect_name_id,
                  world_space
                      ? -1
                      : static_cast<std::int32_t>(attach.attachment_id),
                  0u, world_space, true, attach.id,
                  {attach.offset_x, attach.offset_y, attach.offset_z},
                  {attach.yaw, attach.pitch, attach.roll});
  }
  SpawnPresentationModels(event, effect_id, duration);
}

void SpellVisualRenderer::SpawnPresentationModels(
    const game::SpellVisualPresentationEvent& event,
    const std::uint32_t effect_id, const float duration) {
  if (m2_system_ == nullptr) {
    return;
  }

  const float* event_position = event.world_position.has_value()
                                    ? event.world_position->data()
                                    : event.owner_position.data();
  for (const auto& effect : event.effects) {
    if (effect.model_path.empty()) {
      continue;
    }

    ResolvedKitModel model;
    model.model_path = effect.model_path;
    model.scale = effect.resource_scale;
    model.attachment_id =
        effect.attachment_id >= 0
            ? static_cast<std::uint32_t>(effect.attachment_id)
            : 0u;
    model.offset[0] = effect.offset[0];
    model.offset[1] = effect.offset[1];
    model.offset[2] = effect.offset[2];
    model.rotation[0] = effect.rotation[0];
    model.rotation[1] = effect.rotation[1];
    model.rotation[2] = effect.rotation[2];

    std::array<float, 3> spawn_position{
        event_position[0], event_position[1], event_position[2]};
    if (effect.world_space) {
      for (std::size_t axis = 0; axis < spawn_position.size(); ++axis) {
        spawn_position[axis] += effect.offset[axis];
      }
    }
    const auto loaded = CreateModelInstance(model, spawn_position.data());
    if (loaded.status != m2::M2ResultStatus::kReady ||
        loaded.instance_id == 0u) {
      continue;
    }

    SpellVisualModelInstance instance;
    instance.effect_id = effect_id;
    instance.spell_id = event.spell_id;
    instance.spell_visual_id = event.spell_visual_id;
    instance.spell_visual_kit_id = event.kit_id;
    instance.effect_name_id = effect.effect_name_id;
    instance.raw_flags = event.raw_flags;
    instance.presentation_phase = event.phase;
    instance.dispatch_type = event.dispatch_type;
    instance.m2_event_kind = SpellVisualM2EventKind::kEffect;
    instance.m2_event_owner =
        event.owner.guid.IsEmpty() && event.deferred_impact_owner_guid != 0u
            ? ResolveObjectHandle(event.deferred_impact_owner_guid)
            : event.owner;
    instance.required_m2_event_owner_guid =
        event.deferred_impact_owner_guid;
    instance.m2_events_require_owner_resolution =
        event.m2_events_require_owner_resolution;
    instance.model_id = loaded.model_id;
    instance.instance_id = loaded.instance_id;
    instance.model_path = effect.model_path;
    instance.scale = model.scale;
    instance.attachment_id = effect.attachment_id;
    instance.parent_guid = effect.world_space
                               ? 0u
                               : event.owner.guid.GetRawValue();
    instance.parent_handle = event.owner;
    instance.attached = !effect.world_space && effect.attachment_id >= 0;
    instance.visible = !instance.attached;
    instance.duration = duration;
    std::copy(effect.offset.begin(), effect.offset.end(), instance.offset);
    std::copy(effect.rotation.begin(), effect.rotation.end(),
              instance.rotation);
    std::copy(spawn_position.begin(), spawn_position.end(), instance.position);

    if (instance.attached) {
      (void)ApplyOwnerAttachmentTransform(instance);
    } else if (!effect.world_space) {
      instance.position[0] += effect.offset[0];
      instance.position[1] += effect.offset[1];
      instance.position[2] += effect.offset[2];
      (void)m2_system_->SetPosition(
          loaded.instance_id,
          ToRenderVec3(RenderVec3View{instance.position, 3u}));
    }
    if (instance.active && instance.instance_id != 0u) {
      model_instances_[loaded.instance_id] = std::move(instance);
      if (!BindM2EventCallback(loaded.instance_id)) {
        auto failed = model_instances_.find(loaded.instance_id);
        if (failed != model_instances_.end()) {
          DestroyM2Instance(failed->second.instance_id);
          model_instances_.erase(failed);
        }
      }
    }
  }
}

}

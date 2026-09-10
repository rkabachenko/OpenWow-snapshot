
#include "openwow/render/models/characters/mount_renderer.h"

#include "openwow/game/movement_info.h"
#include "openwow/game/update_fields.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/scene/m2_instance_render_cost.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/models/characters/character_appearance_geosets.h"
#include "openwow/render/models/animation/model_instance_transform.h"
#include "openwow/render/api/math/render_matrix_math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>

namespace openwow::render {

namespace {

[[nodiscard]] constexpr bool IsSpeedScaledMountAnimation(
    const std::uint32_t animation_id) noexcept {
  switch (animation_id) {
  case AnimId::kWalk:
  case AnimId::kRun:
  case AnimId::kShuffleLeft:
  case AnimId::kShuffleRight:
  case AnimId::kWalkBackwards:
  case AnimId::kJumpStart:
  case AnimId::kJump:
  case AnimId::kJumpEnd:
  case AnimId::kSwim:
  case AnimId::kSwimLeft:
  case AnimId::kSwimRight:
  case AnimId::kSwimBackwards:
  case AnimId::kStealthWalk:
  case AnimId::kFly:
  case AnimId::kSprint:
  case AnimId::kJumpLandRun:
  case AnimId::kStealthRun:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] float ResolveMountAnimationPlaybackRate(
    const m2::M2System& system, const MountInstance& instance) {
  constexpr std::uint32_t kDirectionalLocomotionFlags =
      game::kMoveFlagForward | game::kMoveFlagBackward |
      game::kMoveFlagStrafeLeft | game::kMoveFlagStrafeRight |
      game::kMoveFlagAscending | game::kMoveFlagDescending;
  if (instance.m2_model_id == 0u ||
      !IsSpeedScaledMountAnimation(instance.animation.current_anim()) ||
      (instance.movement_flags & kDirectionalLocomotionFlags) == 0u ||
      !(instance.locomotion_speed > 0.0f)) {
    return 1.0f;
  }
  const auto sequence = system.QueryModelAnimationSequence(
      instance.m2_model_id, instance.animation.current_anim());
  if (sequence.status != m2::M2ResultStatus::kReady ||
      !sequence.has_sequence || sequence.sequence.move_speed == 0.0f) {
    return 1.0f;
  }
  return instance.locomotion_speed / std::abs(sequence.sequence.move_speed);
}

}

MountRenderer::~MountRenderer() {
  Shutdown();
}

bool MountRenderer::Initialize() {
  if (initialized_) return true;
  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "MountRenderer: initialized");
  return true;
}

void MountRenderer::Shutdown() {
  if (!initialized_) return;

  Clear();

  initialized_ = false;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "MountRenderer: shutdown");
}

void MountRenderer::Clear() {
  for (auto& [guid, inst] : mounts_) {
    (void)guid;
    ClearM2Binding(inst);
  }
  mounts_.clear();
}

void MountRenderer::BindDisplayInfo(DisplayInfoResolver* display_info) {
  display_info_ = display_info;
}

void MountRenderer::SetFileLoader(
    std::function<std::vector<std::uint8_t>(const std::string&)> loader) {
  file_loader_ = std::move(loader);
  m2_system_.SetFileLoader(file_loader_);
}

void MountRenderer::SetMount(game::ObjectGuid guid,
                             std::uint32_t mount_display_id) {
  if (mount_display_id == 0) {
    ClearMount(guid);
    return;
  }

  auto& inst = mounts_[guid];
  if (inst.mount_display_id == mount_display_id && inst.mount_loaded) {
    return;
  }

  inst.rider_guid = guid;
  inst.mount_display_id = mount_display_id;
  inst.mount_loaded = false;
  inst.needs_resolve = true;
  ClearM2Binding(inst);
  inst.mount_model_path.clear();
  inst.animation.Reset();
  inst.animation_request_serial = 0u;
  inst.completed_animation_request_serial = 0u;
  inst.animation_playback_rate = 1.0f;
  inst.movement_flags = 0u;
  inst.locomotion_speed = 0.0f;
  inst.display_texture_paths = {};
  inst.display_particle_colors.reset();
  inst.display_geoset_data = 0u;
  inst.mount_scale = 1.0f;
  inst.mount_opacity = 1.0f;
  inst.mount_height = 0.0f;
}

void MountRenderer::ClearMount(game::ObjectGuid guid) {
  if (auto it = mounts_.find(guid); it != mounts_.end()) {
    ClearM2Binding(it->second);
    mounts_.erase(it);
  }
}

bool MountRenderer::HasMount(game::ObjectGuid guid) const {
  return mounts_.find(guid) != mounts_.end();
}

void MountRenderer::SyncFromSnapshot(
    const game::ObjectPresentationSnapshot& objects) {
  if (!initialized_) return;

  std::vector<game::ObjectGuid> stale;

  for (auto& [guid, inst] : mounts_) {
    const auto unit = std::lower_bound(
        objects.active.begin(), objects.active.end(), guid.GetRawValue(),
        [](const game::ObjectPresentationRecord& record,
           const std::uint64_t raw_guid) {
          return record.handle.guid.GetRawValue() < raw_guid;
        });
    if (unit == objects.active.end() || unit->handle.guid != guid ||
        unit->handle != inst.rider) {
      stale.push_back(guid);
      continue;
    }

    const std::uint32_t mount_display = unit->mount_display_id;
    inst.movement_flags = unit->locomotion.movement_flags;
    inst.locomotion_speed = unit->locomotion.current_speed;
    if (mount_display == 0) {
      stale.push_back(guid);
    } else if (mount_display != inst.mount_display_id) {

      inst.mount_display_id = mount_display;
      inst.mount_loaded = false;
      inst.needs_resolve = true;
      ClearM2Binding(inst);
      inst.mount_model_path.clear();
      inst.animation.Reset();
      inst.animation_request_serial = 0u;
      inst.completed_animation_request_serial = 0u;
      inst.animation_playback_rate = 1.0f;
      inst.display_texture_paths = {};
      inst.display_particle_colors.reset();
      inst.display_geoset_data = 0u;
      inst.mount_scale = 1.0f;
      inst.mount_opacity = 1.0f;
      inst.mount_height = 0.0f;
    }
  }

  for (const auto& guid : stale) {
    ClearMount(guid);
  }

  for (const auto& unit : objects.active) {
    if (unit.type_id != game::TypeID::kUnit &&
        unit.type_id != game::TypeID::kPlayer) {
      continue;
    }
    const auto guid = unit.handle.guid;
    const std::uint32_t mount_display = unit.mount_display_id;
    if (mount_display != 0 && mounts_.find(guid) == mounts_.end()) {
      SetMount(guid, mount_display);
      auto& mount = mounts_.at(guid);
      mount.rider = unit.handle;
      mount.movement_flags = unit.locomotion.movement_flags;
      mount.locomotion_speed = unit.locomotion.current_speed;
    }
  }
}

void MountRenderer::ClearM2Binding(MountInstance& inst) {
  if (inst.m2_instance_id != 0u) {
    const auto status = m2_system_.DestroyInstance(inst.m2_instance_id);
    if (status != m2::M2ResultStatus::kReady) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         std::string("MountRenderer: M2 instance destroy ") +
                             m2::M2ResultStatusName(status));
    }
  }
  inst.m2_model_id = 0u;
  inst.m2_instance_id = 0u;
  inst.render_ready_latched_instance_id = 0u;
  inst.mount_loaded = false;
  inst.rider_attachment_transform_valid = false;
  inst.rider_attachment_failure_reported = false;
  inst.mount_world_transform_valid = false;
  inst.event_callback_installed = false;
  inst.display_overrides_applied = false;
  inst.visible_submeshes_applied = false;
}

void MountRenderer::LoadModelForMount(MountInstance& inst) {
  if (inst.mount_model_path.empty()) return;

  auto& system = m2_system_;
  const auto instance_result = system.LoadModelInstance(inst.mount_model_path);
  if (instance_result.status != m2::M2ResultStatus::kReady ||
      instance_result.model_id == 0u || instance_result.instance_id == 0u) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "MountRenderer: M2System load/create " +
                           std::string(m2::M2ResultStatusName(instance_result.status)) +
                           " reason=" + m2::M2ResultReasonName(instance_result.reason) +
                           ": " + inst.mount_model_path +
                           (instance_result.detail.empty() ? std::string()
                                                           : " detail=" + instance_result.detail));
    return;
  }

  const std::uint32_t model_id = instance_result.model_id;
  ClearM2Binding(inst);
  inst.m2_model_id = model_id;
  inst.m2_instance_id = instance_result.instance_id;
  inst.mount_loaded = true;
}

void MountRenderer::ApplyDisplayOverrides(MountInstance& inst) {
  if (inst.display_overrides_applied || inst.m2_instance_id == 0u) {
    return;
  }
  std::optional<m2::M2ParticleColorRecord> particle_colors;
  if (inst.display_particle_colors.has_value()) {
    particle_colors = m2::M2ParticleColorRecord{
        .start = inst.display_particle_colors->start,
        .mid = inst.display_particle_colors->mid,
        .end = inst.display_particle_colors->end,
    };
  }
  const auto status = m2_system_.ApplyCreatureDisplayRecordOverrides(
      inst.m2_instance_id, inst.display_texture_paths,
      std::move(particle_colors));
  if (status == m2::M2ResultStatus::kReady) {
    inst.display_overrides_applied = true;
  } else if (m2::IsTerminalM2ResultStatus(status)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "MountRenderer: display overrides failed display=" +
            std::to_string(inst.mount_display_id) +
            " status=" + m2::M2ResultStatusName(status));
    ClearM2Binding(inst);
  }
}

void MountRenderer::ApplyVisibleSubmeshes(MountInstance& inst) {
  if (inst.visible_submeshes_applied || inst.m2_instance_id == 0u ||
      inst.m2_model_id == 0u) {
    return;
  }
  const auto sections =
      m2_system_.QueryModelSubmeshSectionIds(inst.m2_model_id);
  if (sections.status != m2::M2ResultStatus::kReady) {
    if (m2::IsTerminalM2ResultStatus(sections.status)) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "MountRenderer: geoset query failed display=" +
              std::to_string(inst.mount_display_id) +
              " status=" + m2::M2ResultStatusName(sections.status));
      ClearM2Binding(inst);
    }
    return;
  }

  std::vector<std::size_t> visible_indices;
  visible_indices.reserve(sections.section_ids.size());
  for (std::size_t index = 0u; index < sections.section_ids.size(); ++index) {
    if (IsCreatureGeosetSectionVisible(sections.section_ids[index],
                                       inst.display_geoset_data)) {
      visible_indices.push_back(index);
    }
  }
  const auto status = m2_system_.SetVisibleSubmeshIndices(
      inst.m2_instance_id, std::move(visible_indices));
  if (status == m2::M2ResultStatus::kReady) {
    inst.visible_submeshes_applied = true;
  } else if (m2::IsTerminalM2ResultStatus(status)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "MountRenderer: geoset apply failed display=" +
            std::to_string(inst.mount_display_id) +
            " status=" + m2::M2ResultStatusName(status));
    ClearM2Binding(inst);
  }
}

void MountRenderer::ApplyM2EventCallback(MountInstance& inst) {
  if (inst.event_callback_installed || inst.m2_instance_id == 0u ||
      m2_event_sink_ == nullptr) {
    return;
  }
  const auto status = m2_system_.SetTriggeredEventCallback(
      inst.m2_instance_id,
      [this, owner = inst.rider](const m2::M2TriggeredEvent& event) {
        if (m2_event_sink_ != nullptr) {
          m2_event_sink_({.owner = owner, .event = event});
        }
      });
  if (status == m2::M2ResultStatus::kReady) {
    inst.event_callback_installed = true;
  } else if (m2::IsTerminalM2ResultStatus(status)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "MountRenderer: animation event callback failed display=" +
            std::to_string(inst.mount_display_id) +
            " status=" + m2::M2ResultStatusName(status));
    ClearM2Binding(inst);
  }
}

void MountRenderer::Update(float dt, int max_loads_per_frame) {
  if (!initialized_) return;

  int loads_this_frame = 0;
  auto& system = m2_system_;

  for (auto& [guid, inst] : mounts_) {

    if (inst.needs_resolve && display_info_ && display_info_->IsReady()) {
      const auto visual =
          display_info_->ResolveCreatureDisplay(inst.mount_display_id);
      inst.mount_model_path = visual.model_path;
      inst.mount_scale = visual.model_scale;
      inst.mount_opacity = visual.model_opacity;
      inst.mount_height = visual.mount_height;
      inst.display_texture_paths = visual.texture_paths;
      inst.display_particle_colors = visual.particle_colors;
      inst.display_geoset_data = visual.geoset_data;
      inst.needs_resolve = false;
    }

    if (!inst.mount_loaded && !inst.mount_model_path.empty() &&
        loads_this_frame < max_loads_per_frame) {
      LoadModelForMount(inst);
      ++loads_this_frame;
    }

    ApplyDisplayOverrides(inst);
    ApplyVisibleSubmeshes(inst);
    ApplyM2EventCallback(inst);

    std::uint32_t anim_duration_ms = 0u;
    if (inst.m2_model_id != 0u) {
      const auto readiness = system.QueryModelReadiness(inst.m2_model_id);
      if (readiness.status != m2::M2ResultStatus::kReady || !readiness.loaded) {
        inst.animation.Update(dt, anim_duration_ms);
        continue;
      }
      const auto sequence = system.QueryModelAnimationSequence(
          inst.m2_model_id, inst.animation.current_anim());
      if (sequence.status == m2::M2ResultStatus::kReady && sequence.has_sequence) {
        anim_duration_ms = sequence.sequence.duration_ms;
      }
    }
    inst.animation_playback_rate =
        ResolveMountAnimationPlaybackRate(system, inst);
    inst.animation.Update(dt * inst.animation_playback_rate, anim_duration_ms);
    if (!inst.animation.is_looping() && inst.animation.DidAnimationComplete() &&
        inst.animation_request_serial != 0u &&
        inst.completed_animation_request_serial !=
            inst.animation_request_serial) {
      inst.completed_animation_request_serial =
          inst.animation_request_serial;
      if (animation_completion_sink_ != nullptr) {
        animation_completion_sink_({
            .owner = inst.rider,
            .animation_id = static_cast<std::uint16_t>(
                inst.animation.current_anim()),
            .request_serial = inst.animation_request_serial,
        });
      }
    }
  }
}

void MountRenderer::Render(std::uint8_t view_id, const float* view_mtx,
                           const float* proj_mtx,
                           const game::ObjectPresentationSnapshot& objects,
                           m2::M2TransparentDrawOrder& transparent_draw_order) {
  if (!initialized_) return;

  static_cast<void>(proj_mtx);

  auto& system = m2_system_;
  m2::M2BatchUniforms world_uniforms;
  ApplyWorldM2SceneState(world_m2_scene_state_, &world_uniforms);

  render_batch_mounts_scratch_.clear();
  render_batch_ids_scratch_.clear();
  for (auto& [guid, inst] : mounts_) {

    if (!inst.mount_loaded || inst.m2_instance_id == 0u ||
        !inst.display_overrides_applied ||
        !inst.visible_submeshes_applied) {
      continue;
    }
    if (inst.render_ready_latched_instance_id != inst.m2_instance_id) {
      const auto readiness = system.QueryInstanceReadiness(inst.m2_instance_id);
      if (readiness.status != m2::M2ResultStatus::kReady ||
          !readiness.render_ready) {
        continue;
      }
      inst.render_ready_latched_instance_id = inst.m2_instance_id;
    }

    const auto unit = std::lower_bound(
        objects.active.begin(), objects.active.end(), guid.GetRawValue(),
        [](const game::ObjectPresentationRecord& record,
           const std::uint64_t raw_guid) {
          return record.handle.guid.GetRawValue() < raw_guid;
        });
    if (unit == objects.active.end() || unit->handle != inst.rider) continue;

    if (PrepareMountInstance(inst, *unit, world_uniforms)) {
      render_batch_mounts_scratch_.push_back(&inst);
      render_batch_ids_scratch_.push_back(inst.m2_instance_id);
    }
  }

  if (render_batch_ids_scratch_.empty()) {
    return;
  }
  const std::uint32_t first_ordinal = transparent_draw_order.Reserve(
      static_cast<std::uint32_t>(render_batch_ids_scratch_.size()));
  render_batch_draw_ordinals_scratch_.resize(render_batch_ids_scratch_.size());
  std::iota(render_batch_draw_ordinals_scratch_.begin(),
            render_batch_draw_ordinals_scratch_.end(), first_ordinal);
  render_batch_results_scratch_.assign(render_batch_ids_scratch_.size(), {});
  {
    const m2::M2TransparentDrawOrdinalScope draw_order_scope(
        render_batch_draw_ordinals_scratch_);
    system.RenderInstanceBatch(view_id, render_batch_ids_scratch_,
                               RenderMatrix4x4View{view_mtx, 16u},
                               m2::M2RenderPassScope::kAll, system.frame_job_system(),
                               kMountInstanceRenderMicroseconds,
                               render_batch_results_scratch_);
  }

  for (std::size_t i = 0; i < render_batch_mounts_scratch_.size(); ++i) {
    if (m2::IsTerminalM2ResultStatus(render_batch_results_scratch_[i].status)) {
      ClearM2Binding(*render_batch_mounts_scratch_[i]);
    }
  }
}

void MountRenderer::RenderShadowCasters(
    const std::uint8_t view_id, const float* const view_mtx,
    const float* const proj_mtx,
    const game::ObjectPresentationSnapshot& objects,
    const std::span<const std::uint64_t> rider_entity_ids) {
  if (!initialized_) {
    return;
  }
  static_cast<void>(proj_mtx);

  auto& system = m2_system_;
  m2::M2BatchUniforms world_uniforms;
  ApplyWorldM2SceneState(world_m2_scene_state_, &world_uniforms);
  world_uniforms.world_shadow_receiver = {};

  render_batch_mounts_scratch_.clear();
  render_batch_ids_scratch_.clear();
  for (auto& [guid, inst] : mounts_) {
    if (!std::binary_search(rider_entity_ids.begin(), rider_entity_ids.end(),
                            guid.GetRawValue()) ||
        !inst.mount_loaded || inst.m2_instance_id == 0u ||
        !inst.display_overrides_applied ||
        !inst.visible_submeshes_applied) {
      continue;
    }
    if (inst.render_ready_latched_instance_id != inst.m2_instance_id) {
      const auto readiness = system.QueryInstanceReadiness(inst.m2_instance_id);
      if (readiness.status != m2::M2ResultStatus::kReady ||
          !readiness.render_ready) {
        continue;
      }
      inst.render_ready_latched_instance_id = inst.m2_instance_id;
    }
    const auto unit = std::lower_bound(
        objects.active.begin(), objects.active.end(), guid.GetRawValue(),
        [](const game::ObjectPresentationRecord& record,
           const std::uint64_t raw_guid) {
          return record.handle.guid.GetRawValue() < raw_guid;
        });
    if (unit == objects.active.end() || unit->handle != inst.rider) {
      continue;
    }
    if (PrepareMountInstance(inst, *unit, world_uniforms)) {
      render_batch_mounts_scratch_.push_back(&inst);
      render_batch_ids_scratch_.push_back(inst.m2_instance_id);
    }
  }
  if (render_batch_ids_scratch_.empty()) {
    return;
  }
  render_batch_results_scratch_.assign(render_batch_ids_scratch_.size(), {});
  system.RenderInstanceBatch(
      view_id, render_batch_ids_scratch_, RenderMatrix4x4View{view_mtx, 16u},
      m2::M2RenderPassScope::kShadowCaster, system.frame_job_system(),
      kMountInstanceRenderMicroseconds, render_batch_results_scratch_);
  for (std::size_t index = 0u; index < render_batch_mounts_scratch_.size();
       ++index) {
    if (m2::IsTerminalM2ResultStatus(
            render_batch_results_scratch_[index].status)) {
      ClearM2Binding(*render_batch_mounts_scratch_[index]);
    }
  }
}

void MountRenderer::RefreshRiderAttachmentTransform(MountInstance& inst) {
  inst.rider_attachment_transform_valid = false;
  if (inst.m2_instance_id == 0u) {
    return;
  }

  const auto attachment = m2_system_.QueryAttachmentTransformMatrix(
      inst.m2_instance_id, kRiderAttachmentLookupIndex);
  if (attachment.status == m2::M2ResultStatus::kReady) {
    inst.rider_attachment_transform = attachment.matrix;
    inst.rider_attachment_transform_valid = true;
    inst.rider_attachment_failure_reported = false;
    return;
  }
  if (attachment.status != m2::M2ResultStatus::kNotReady &&
      !inst.rider_attachment_failure_reported) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "MountRenderer: rider attachment transform failed display=" +
            std::to_string(inst.mount_display_id) +
            " status=" + m2::M2ResultStatusName(attachment.status) +
            " reason=" + m2::M2ResultReasonName(attachment.reason) +
            (attachment.detail.empty() ? std::string()
                                       : " detail=" + attachment.detail));
    inst.rider_attachment_failure_reported = true;
  }
}

game::CharacterLocomotionAnimation MountRenderer::SelectMountLocomotionAnimation(
    const game::ObjectPresentationRecord& unit) {

  game::CharacterLocomotionState state = unit.locomotion;

  state.mounted = false;
  return game::ResolveCharacterLocomotionAnimation(state);
}

bool MountRenderer::PrepareMountInstance(MountInstance& inst,
                                         const game::ObjectPresentationRecord& unit,
                                         const m2::M2BatchUniforms& world_uniforms) {

  auto& system = m2_system_;
  if (!PrepareMountPose(inst, unit)) {
    return false;
  }

  m2::M2ResultStatus setup_status = m2::M2ResultStatus::kReady;
  const auto merge_setup_status = [&setup_status](const m2::M2ResultStatus status) {
    setup_status = m2::MergeM2ResultStatus(setup_status, status);
  };
  merge_setup_status(system.SetVisible(inst.m2_instance_id, true));

  merge_setup_status(system.SetAlpha(
      inst.m2_instance_id,
      std::clamp(unit.render_opacity * inst.mount_opacity, 0.0f, 1.0f)));
  merge_setup_status(system.SetBatchUniforms(inst.m2_instance_id, world_uniforms));
  if (m2::IsTerminalM2ResultStatus(setup_status)) {
    ClearM2Binding(inst);
    return false;
  }
  if (setup_status != m2::M2ResultStatus::kReady) {
    return false;
  }
  return true;
}

bool MountRenderer::PrepareMountPose(
    MountInstance& inst, const game::ObjectPresentationRecord& unit) {
  inst.rider_attachment_transform_valid = false;
  inst.mount_world_transform_valid = false;
  if (inst.m2_instance_id == 0u) {
    return false;
  }

  const bool request_changed =
      inst.animation_request_serial != unit.mount_animation_serial;
  if (request_changed) {
    inst.animation.Restart(unit.mount_animation_id,
                           unit.mount_animation_looping);
    inst.animation_request_serial = unit.mount_animation_serial;
    inst.completed_animation_request_serial = 0u;
  } else if (inst.completed_animation_request_serial ==
             unit.mount_animation_serial) {
    const auto locomotion = SelectMountLocomotionAnimation(unit);
    inst.animation.SetAnimation(locomotion.animation_id, locomotion.looping);
  }
  inst.animation_playback_rate =
      ResolveMountAnimationPlaybackRate(m2_system_, inst);
  const auto model_matrix = BuildM2ModelInstanceTransform(
      unit.x, unit.y, unit.z, unit.facing, inst.mount_scale);
  inst.mount_world_transform = model_matrix;

  auto status = m2_system_.SetWorldTransformMatrix(inst.m2_instance_id,
                                                   model_matrix);
  status = m2::MergeM2ResultStatus(
      status, m2_system_.SetAnimationSample(
                  inst.m2_instance_id, inst.animation.current_anim(),
                  inst.animation.current_time_ms(),
                  inst.animation_playback_rate, false, request_changed));
  if (m2::IsTerminalM2ResultStatus(status)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "MountRenderer: pose setup failed display=" +
            std::to_string(inst.mount_display_id) +
            " status=" + m2::M2ResultStatusName(status));
    ClearM2Binding(inst);
    return false;
  }
  if (status != m2::M2ResultStatus::kReady) {
    return false;
  }

  inst.mount_world_transform_valid = true;
  RefreshRiderAttachmentTransform(inst);
  return inst.rider_attachment_transform_valid;
}

void MountRenderer::PrepareRiderAttachments(
    const std::span<const game::ObjectPresentationRecord> objects) {
  for (auto& [guid, inst] : mounts_) {
    const auto unit = std::lower_bound(
        objects.begin(), objects.end(), guid.GetRawValue(),
        [](const game::ObjectPresentationRecord& record,
           const std::uint64_t raw_guid) {
          return record.handle.guid.GetRawValue() < raw_guid;
        });
    if (unit == objects.end() || unit->handle != inst.rider ||
        unit->mount_display_id != inst.mount_display_id ||
        !inst.mount_loaded) {
      inst.rider_attachment_transform_valid = false;
      continue;
    }
    static_cast<void>(PrepareMountPose(inst, *unit));
  }
}

bool MountRenderer::GetRiderWorldTransform(
    const game::ObjectGuid guid, const float rider_scale,
    RenderMatrix4x4& out_transform) const {
  const auto it = mounts_.find(guid);
  if (it == mounts_.end() ||
      !it->second.rider_attachment_transform_valid) {
    return false;
  }

  const auto& inst = it->second;
  const float parent_scale = inst.mount_scale > 0.0f ? inst.mount_scale : 1.0f;
  const float child_scale =
      (rider_scale > 0.0f ? rider_scale : 1.0f) / parent_scale;
  const RenderVec3 scale{child_scale, child_scale, child_scale};
  out_transform = ScaleMatrix4x4BasisRows(
      inst.rider_attachment_transform, scale);
  return true;
}

bool MountRenderer::HasRiderAttachmentTransform(
    const game::ObjectGuid guid) const {
  const auto it = mounts_.find(guid);
  return it != mounts_.end() &&
         it->second.rider_attachment_transform_valid;
}

bool MountRenderer::QueryMountSpatialState(
    const game::ObjectGuid guid, MountSpatialState& out) const {
  const auto it = mounts_.find(guid);
  if (it == mounts_.end() || !it->second.mount_world_transform_valid ||
      it->second.m2_instance_id == 0u ||
      !std::isfinite(it->second.mount_height)) {
    return false;
  }
  out.world_transform = it->second.mount_world_transform;
  out.mount_height = it->second.mount_height;
  return true;
}

std::uint32_t MountRenderer::QueryMountM2InstanceId(
    const game::ObjectHandle rider) const noexcept {
  const auto it = mounts_.find(rider.guid);
  return it != mounts_.end() && it->second.rider == rider
             ? it->second.m2_instance_id
             : 0u;
}

}

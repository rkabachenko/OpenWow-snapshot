
#include "openwow/render/models/characters/mount_renderer.h"

#include "openwow/game/movement_info.h"
#include "openwow/game/update_fields.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/scene/m2_instance_render_cost.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>

namespace openwow::render {

namespace {

[[nodiscard]] RenderMatrix4x4 BuildMountModelMatrix(
    const float x,
    const float y,
    const float z,
    const float yaw,
    const float scale) {
  const float cosine = std::cos(yaw);
  const float sine = std::sin(yaw);

  RenderMatrix4x4 matrix{};
  matrix[0] = cosine * scale;
  matrix[2] = -sine * scale;
  matrix[5] = scale;
  matrix[8] = sine * scale;
  matrix[10] = cosine * scale;
  matrix[12] = x;
  matrix[13] = y;
  matrix[14] = z;
  matrix[15] = 1.0f;
  return matrix;
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
    if (mount_display == 0) {
      stale.push_back(guid);
    } else if (mount_display != inst.mount_display_id) {

      inst.mount_display_id = mount_display;
      inst.mount_loaded = false;
      inst.needs_resolve = true;
      ClearM2Binding(inst);
      inst.mount_model_path.clear();
      inst.animation.Reset();
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
      mounts_.at(guid).rider = unit.handle;
    }
  }
}

std::string MountRenderer::ResolveMountModel(std::uint32_t display_id) {
  if (!display_info_ || !display_info_->IsReady()) return {};
  return display_info_->ResolveCreatureModel(display_id);
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

  inst.rider_world_pos_valid = false;

  const auto attachment =
      system.QueryModelAttachmentInfo(model_id, kRiderAttachmentLookupIndex);
  if (attachment.status != m2::M2ResultStatus::kReady) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "MountRenderer: MOUNTDISPLAYIDNOMOUNTATTACHMENT|" +
            std::to_string(inst.mount_display_id));
  }

  if (display_info_ && display_info_->IsReady()) {
    inst.mount_scale =
        display_info_->GetCreatureModelScale(inst.mount_display_id);
  }
}

void MountRenderer::Update(float dt, int max_loads_per_frame) {
  if (!initialized_) return;

  int loads_this_frame = 0;
  auto& system = m2_system_;

  for (auto& [guid, inst] : mounts_) {

    if (inst.needs_resolve && display_info_ && display_info_->IsReady()) {
      inst.mount_model_path = ResolveMountModel(inst.mount_display_id);
      inst.needs_resolve = false;
    }

    if (!inst.mount_loaded && !inst.mount_model_path.empty() &&
        loads_this_frame < max_loads_per_frame) {
      LoadModelForMount(inst);
      ++loads_this_frame;
    }

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
    inst.animation.Update(dt, anim_duration_ms);
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

    if (!inst.mount_loaded || inst.m2_instance_id == 0u) {
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

bool MountRenderer::GetRiderOffset(game::ObjectGuid guid, float& ox,
                                   float& oy, float& oz) const {
  auto it = mounts_.find(guid);
  if (it == mounts_.end()) return false;

  const auto& inst = it->second;
  if (inst.rider_world_pos_valid) {
    ox = inst.rider_world_offset[0];
    oy = inst.rider_world_offset[1];
    oz = inst.rider_world_offset[2];
  } else {
    return false;
  }
  return true;
}

bool MountRenderer::GetRiderWorldPos(game::ObjectGuid guid, float& ox,
                                      float& oy, float& oz) const {
  auto it = mounts_.find(guid);
  if (it == mounts_.end()) return false;

  const auto& inst = it->second;
  if (!inst.rider_world_pos_valid) return false;

  ox = inst.rider_world_pos[0];
  oy = inst.rider_world_pos[1];
  oz = inst.rider_world_pos[2];
  return true;
}

float MountRenderer::GetMountScale(game::ObjectGuid guid) const {
  auto it = mounts_.find(guid);
  if (it == mounts_.end()) return 1.0f;
  return it->second.mount_scale;
}

void MountRenderer::UpdateRiderAttachmentFromM2System(MountInstance& inst) {
  inst.rider_world_pos_valid = false;
  if (inst.m2_instance_id == 0u) {
    return;
  }

  auto& system = m2_system_;
  const auto attachment_query =
      system.QueryAttachmentPosition(inst.m2_instance_id, kRiderAttachmentLookupIndex);
  const auto origin_query = system.QueryModelWorldPoint(inst.m2_instance_id);
  if (attachment_query.status != m2::M2ResultStatus::kReady ||
      origin_query.status != m2::M2ResultStatus::kReady) {
    return;
  }

  for (std::size_t axis = 0; axis < 3u; ++axis) {
    inst.rider_world_pos[axis] = attachment_query.position[axis];
    inst.rider_world_offset[axis] =
        attachment_query.position[axis] - origin_query.position[axis];
  }
  inst.rider_world_pos_valid = true;
}

game::CharacterLocomotionAnimation MountRenderer::SelectMountAnimation(
    const game::ObjectPresentationRecord& unit) {

  game::CharacterLocomotionState state = unit.locomotion;

  state.mounted = false;
  return game::ResolveCharacterLocomotionAnimation(state);
}

bool MountRenderer::PrepareMountInstance(MountInstance& inst,
                                         const game::ObjectPresentationRecord& unit,
                                         const m2::M2BatchUniforms& world_uniforms) {

  auto& system = m2_system_;

  const auto locomotion = SelectMountAnimation(unit);
  inst.animation.SetAnimation(locomotion.animation_id, locomotion.looping);

  const float pos_x = unit.x;
  const float pos_y = unit.y;
  const float pos_z = unit.z;
  const float orientation = unit.facing;

  const RenderMatrix4x4 model_matrix =
      BuildMountModelMatrix(pos_x, pos_y, pos_z, orientation, inst.mount_scale);

  const std::uint32_t anim_time = inst.animation.current_time_ms();

  m2::M2ResultStatus setup_status = m2::M2ResultStatus::kReady;
  const auto merge_setup_status = [&setup_status](const m2::M2ResultStatus status) {
    setup_status = m2::MergeM2ResultStatus(setup_status, status);
  };
  merge_setup_status(
      system.SetWorldTransformMatrix(inst.m2_instance_id, model_matrix));
  merge_setup_status(
      system.SetAnimationSample(inst.m2_instance_id, inst.animation.current_anim(), anim_time));
  merge_setup_status(system.SetVisible(inst.m2_instance_id, true));

  merge_setup_status(system.SetAlpha(
      inst.m2_instance_id, std::clamp(unit.render_opacity, 0.0f, 1.0f)));
  merge_setup_status(system.ClearVisibleSubmeshIndices(inst.m2_instance_id));
  merge_setup_status(system.SetBatchUniforms(inst.m2_instance_id, world_uniforms));
  if (m2::IsTerminalM2ResultStatus(setup_status)) {
    ClearM2Binding(inst);
    return false;
  }
  if (setup_status != m2::M2ResultStatus::kReady) {
    return false;
  }
  UpdateRiderAttachmentFromM2System(inst);
  return true;
}

}

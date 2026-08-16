#include "openwow/render/m2/m2_spatial_queries.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/render/m2/m2_animation_selection.h"
#include "openwow/render/m2/m2_animator.h"
#include "openwow/render/m2/m2_cpu_skinning.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>

namespace openwow::render::m2 {
namespace {

constexpr float kRaycastSegmentEpsilon = 9.9999997e-6f;
constexpr float kTriangleIntersectionEpsilon = 1.0e-6f;
constexpr std::uint32_t kSelectionTriangleTopTag = 0x54545724u;
constexpr std::uint32_t kSelectionTriangleBottomTag = 0x42545724u;

std::optional<M2AnimationAliasInfo> LookupAnimationAliasInfo(
    const std::uint32_t animation_id, const data::dbc::DbcLoader* dbc) {
  if (dbc == nullptr) return std::nullopt;
  const auto* entry = dbc->animation_data().LookupEntry(animation_id);
  if (entry == nullptr) return std::nullopt;
  return M2AnimationAliasInfo{.flags = entry->flags,
                              .target_animation_id = entry->fallback};
}

std::uint32_t AnimationTimeMs(const detail::M2Instance& instance) noexcept {
  return static_cast<std::uint32_t>(std::max(instance.animation_time, 0.0f) * 1000.0f);
}

std::uint32_t SequenceDurationMs(const data::model::M2Model& model,
                                 const std::uint16_t sequence_index) noexcept {
  if (sequence_index == kInvalidM2AnimationSequenceIndex ||
      sequence_index >= model.animation_durations_ms.size()) return 0u;
  return model.animation_durations_ms[sequence_index];
}

RenderVec3 TransformPoint(const RenderVec3View point,
                          const RenderMatrix4x4View matrix) noexcept {
  return {matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12],
          matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13],
          matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14]};
}

RenderAabb TransformAabb(const RenderAabbView aabb,
                         const RenderMatrix4x4View matrix) {
  RenderAabb result{};
  math::row_major_mat4x4::TransformAABBByRowMajorAffine4x4(
      result.data(), aabb.data(), matrix.data());
  return result;
}

RenderMatrix4x4 MultiplyMatrix4x4(const RenderMatrix4x4View lhs,
                                  const RenderMatrix4x4View rhs) {
  RenderMatrix4x4 result{};
  bx::mtxMul(result.data(), lhs.data(), rhs.data());
  return result;
}

std::optional<RenderMatrix4x4View> BoneMatrixAt(
    const std::vector<float>& matrices, const std::size_t bone_index) noexcept {
  constexpr std::size_t kFloatCount = 16u;
  if (bone_index > std::numeric_limits<std::size_t>::max() / kFloatCount) return std::nullopt;
  const std::size_t offset = bone_index * kFloatCount;
  if (offset > matrices.size() || matrices.size() - offset < kFloatCount) return std::nullopt;
  return RenderMatrix4x4View{matrices.data() + offset, kFloatCount};
}

const data::model::M2Event* FindEventByIdentifier(
    const data::model::M2Model& model, const std::uint32_t identifier) {
  const auto it = std::find_if(model.events.begin(), model.events.end(),
                               [identifier](const auto& event) {
                                 return event.identifier == identifier;
                               });
  return it == model.events.end() ? nullptr : &*it;
}

RenderVec3 ResolveEventPosition(const data::model::M2Event& event,
                                const std::vector<float>& bone_matrices,
                                const std::size_t bone_count) {
  RenderVec3 position{event.position[0], event.position[1], event.position[2]};
  if (event.bone >= 0 && static_cast<std::size_t>(event.bone) < bone_count) {
    const auto matrix = BoneMatrixAt(bone_matrices, static_cast<std::size_t>(event.bone));
    if (matrix.has_value()) position = TransformPoint(RenderVec3View{position}, *matrix);
  }
  return position;
}

struct ResolvedAttachment {
  const data::model::M2Attachment* attachment = nullptr;
  std::uint16_t attachment_index = std::numeric_limits<std::uint16_t>::max();
};

ResolvedAttachment ResolveAttachmentLookup(
    const data::model::M2Model& model,
    const std::uint32_t lookup_index) noexcept {
  if (lookup_index >= model.attachment_lookups.size()) return {};
  const std::int16_t index = model.attachment_lookups[lookup_index];
  if (index < 0 || static_cast<std::size_t>(index) >= model.attachments.size()) return {};
  return {.attachment = &model.attachments[static_cast<std::size_t>(index)],
          .attachment_index = static_cast<std::uint16_t>(index)};
}

RenderVec3 AttachmentLocalPosition(
    const data::model::M2Attachment& attachment) noexcept {
  return {attachment.position[0], attachment.position[1], attachment.position[2]};
}

bool ModelHasKeyBone(const data::model::M2Model& model,
                     const std::uint32_t lookup_index) noexcept {
  if (lookup_index >= model.key_bone_lookup.size()) return false;
  const std::int16_t index = model.key_bone_lookup[lookup_index];
  return index >= 0 && static_cast<std::size_t>(index) < model.bones.size();
}

M2ModelSpatialInfo BuildSpatialInfo(const detail::M2ModelResource& resource,
                                    const std::uint16_t sequence_index) noexcept {
  M2ModelSpatialInfo info{};
  const auto& header = resource.model_data.header;
  info.local_bounds = {header.bounding_box_min[0], header.bounding_box_min[1],
                       header.bounding_box_min[2], header.bounding_box_max[0],
                       header.bounding_box_max[1], header.bounding_box_max[2]};
  const auto set_sphere = [&info](const auto& min, const auto& max, const float radius) {
    info.local_bounding_sphere = {(min[0] + max[0]) * 0.5f,
                                  (min[1] + max[1]) * 0.5f,
                                  (min[2] + max[2]) * 0.5f, radius};
  };
  set_sphere(header.bounding_box_min, header.bounding_box_max,
             header.bounding_sphere_radius);
  if (sequence_index != kInvalidM2AnimationSequenceIndex &&
      sequence_index < resource.model_data.animation_sequences.size()) {
    const auto& sequence = resource.model_data.animation_sequences[sequence_index];
    if (sequence.bounding_box_min[0] < sequence.bounding_box_max[0] &&
        sequence.bounding_box_min[1] < sequence.bounding_box_max[1] &&
        sequence.bounding_box_min[2] < sequence.bounding_box_max[2]) {
      info.local_bounds = {sequence.bounding_box_min[0], sequence.bounding_box_min[1],
                           sequence.bounding_box_min[2], sequence.bounding_box_max[0],
                           sequence.bounding_box_max[1], sequence.bounding_box_max[2]};
      set_sphere(sequence.bounding_box_min, sequence.bounding_box_max,
                 sequence.bounding_radius);
    }
  }
  return info;
}

struct PreparedSegment {
  bx::Vec3 start{0.0f, 0.0f, 0.0f};
  bx::Vec3 end{0.0f, 0.0f, 0.0f};
  bx::Vec3 direction{0.0f, 0.0f, 0.0f};
  float max_distance = 0.0f;
};

std::optional<PreparedSegment> PrepareSegment(const RenderVec3& start,
                                              const RenderVec3& end) {
  const float dx = end[0] - start[0];
  const float dy = end[1] - start[1];
  const float dz = end[2] - start[2];
  const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (!std::isfinite(length) || length < kRaycastSegmentEpsilon) return std::nullopt;
  const float inverse = 1.0f / length;
  return PreparedSegment{.start = {start[0], start[1], start[2]},
                         .end = {end[0], end[1], end[2]},
                         .direction = {dx * inverse, dy * inverse, dz * inverse},
                         .max_distance = length};
}

bool SegmentIntersectsBounds(const bx::Vec3& start, const bx::Vec3& end,
                             const detail::M2ModelResource& resource) {
  const auto& h = resource.model_data.header;
  if (h.bounding_box_min[0] > h.bounding_box_max[0] ||
      h.bounding_box_min[1] > h.bounding_box_max[1] ||
      h.bounding_box_min[2] > h.bounding_box_max[2]) return true;
  const bx::Vec3 delta = bx::sub(end, start);
  float t_min = 0.0f, t_max = 1.0f;
  const auto test = [&](const float origin, const float direction,
                        const float min, const float max) {
    if (std::fabs(direction) < kRaycastSegmentEpsilon) return origin >= min && origin <= max;
    float enter = (min - origin) / direction;
    float exit = (max - origin) / direction;
    if (enter > exit) std::swap(enter, exit);
    t_min = std::max(t_min, enter);
    t_max = std::min(t_max, exit);
    return t_min <= t_max;
  };
  return test(start.x, delta.x, h.bounding_box_min[0], h.bounding_box_max[0]) &&
         test(start.y, delta.y, h.bounding_box_min[1], h.bounding_box_max[1]) &&
         test(start.z, delta.z, h.bounding_box_min[2], h.bounding_box_max[2]) &&
         t_max >= 0.0f && t_min <= 1.0f;
}

std::optional<float> IntersectTriangle(const bx::Vec3& start,
                                       const bx::Vec3& delta,
                                       const bx::Vec3& a, const bx::Vec3& b,
                                       const bx::Vec3& c) {
  const bx::Vec3 ab = bx::sub(b, a), ac = bx::sub(c, a);
  const bx::Vec3 p = bx::cross(delta, ac);
  const float determinant = bx::dot(ab, p);
  if (std::fabs(determinant) < kTriangleIntersectionEpsilon) return std::nullopt;
  const float inverse = 1.0f / determinant;
  const bx::Vec3 relative = bx::sub(start, a);
  const float u = bx::dot(relative, p) * inverse;
  if (u < 0.0f || u > 1.0f) return std::nullopt;
  const bx::Vec3 q = bx::cross(relative, ab);
  const float v = bx::dot(delta, q) * inverse;
  if (v < 0.0f || u + v > 1.0f) return std::nullopt;
  const float fraction = bx::dot(ac, q) * inverse;
  return fraction < 0.0f || fraction > 1.0f ? std::nullopt
                                            : std::optional<float>{fraction};
}

template <typename ResolveVertex>
std::optional<float> FindClosestIntersection(
    const data::model::M2Skin& skin, const M2SkinGeometry& geometry,
    const std::vector<std::size_t>* visible_submeshes,
    const ResolveVertex& resolve_vertex, const bx::Vec3& start,
    const bx::Vec3& end) {
  const bx::Vec3 delta = bx::sub(end, start);
  float best = std::numeric_limits<float>::infinity();
  bool hit = false;
  const auto test_range = [&](const std::size_t start_index, const std::size_t count) {
    const auto range_end = std::min(start_index + count, geometry.indices.size());
    for (std::size_t i = start_index; i + 2u < range_end; i += 3u) {
      const std::size_t i0 = geometry.indices[i], i1 = geometry.indices[i + 1u],
                        i2 = geometry.indices[i + 2u];
      if (i0 >= geometry.vertices.size() || i1 >= geometry.vertices.size() ||
          i2 >= geometry.vertices.size()) continue;
      const auto fraction = IntersectTriangle(start, delta, resolve_vertex(i0),
                                               resolve_vertex(i1), resolve_vertex(i2));
      if (fraction.has_value() && *fraction < best) {
        best = *fraction;
        hit = true;
      }
    }
  };
  if (visible_submeshes == nullptr) test_range(0u, geometry.indices.size());
  else {
    bool tested = false;
    for (const auto index : *visible_submeshes) {
      if (index >= skin.submeshes.size()) continue;
      tested = true;
      const auto& submesh = skin.submeshes[index];
      test_range(submesh.index_start, submesh.index_count);
    }
    if (!tested) return std::nullopt;
  }
  return hit ? std::optional<float>{best} : std::nullopt;
}

float UniformScale(const RenderMatrix4x4View matrix) noexcept {
  return std::sqrt(matrix[0] * matrix[0] + matrix[1] * matrix[1] +
                   matrix[2] * matrix[2]);
}

bool Vec3IsFinite(const RenderVec3& value) noexcept {
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

[[nodiscard]] bool ModelPoseIsTimeInvariantAt(const data::model::M2Model& model,
                                              const int animation_index) noexcept {
  using Index = data::model::M2BonePoseIndex;
  const Index& index = model.bone_pose_index;
  const std::uint64_t* const rows = index.KeylessRowsFor(animation_index);
  if (rows == nullptr) {
    return false;
  }
  const std::size_t bone_count = model.bones.size();
  const std::size_t words = index.words_per_row;

  if (bone_count == 0u ||
      words != (bone_count + Index::kBonesPerWord - 1u) / Index::kBonesPerWord) {
    return false;
  }
  const std::size_t tail_bits = bone_count % Index::kBonesPerWord;
  const std::uint64_t tail_mask =
      tail_bits == 0u ? ~std::uint64_t{0} : ((std::uint64_t{1} << tail_bits) - 1u);
  for (std::size_t word = 0; word < words; ++word) {
    const std::uint64_t mask = word + 1u == words ? tail_mask : ~std::uint64_t{0};
    for (std::size_t row = 0; row < Index::kRowsPerAnimation; ++row) {
      if ((rows[row * words + word] & mask) != mask) {
        return false;
      }
    }
  }
  return true;
}

}

RenderMatrix4x4 ComputeM2ModelMatrix(const detail::M2Instance& instance) {
  if (instance.has_explicit_world_transform) return instance.world_transform;
  constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
  RenderMatrix4x4 translate{}, rx{}, ry{}, rz{}, rot_temp{}, rotation{}, scale{}, temp{}, out{};
  bx::mtxTranslate(translate.data(), instance.position[0], instance.position[1], instance.position[2]);
  bx::mtxRotateY(ry.data(), instance.rotation[1] * kDegToRad);
  bx::mtxRotateX(rx.data(), instance.rotation[0] * kDegToRad);
  bx::mtxRotateZ(rz.data(), instance.rotation[2] * kDegToRad);
  bx::mtxMul(rot_temp.data(), rx.data(), rz.data());
  bx::mtxMul(rotation.data(), ry.data(), rot_temp.data());
  bx::mtxScale(scale.data(), instance.scale, instance.scale, instance.scale);

  bx::mtxMul(temp.data(), scale.data(), rotation.data());
  bx::mtxMul(out.data(), temp.data(), translate.data());
  return out;
}

int ResolveM2RenderableAnimationIndex(const detail::M2Instance& instance) {
  return instance.animation_sequence_index != kInvalidM2AnimationSequenceIndex
             ? static_cast<int>(instance.animation_sequence_index)
             : static_cast<int>(instance.animation_id);
}

const std::vector<float>* SampleM2InstanceBoneMatricesCached(
    const detail::M2Instance& instance,
    const detail::M2ModelResource& resource,
    const std::optional<RenderMatrix4x4View>& camera_inverse_view) {
  if (!resource.has_bones) return nullptr;

  auto& cache = (camera_inverse_view.has_value() && resource.has_billboard_bones)
                    ? instance.render_pose_cache
                    : instance.query_pose_cache;
  const int animation_index = ResolveM2RenderableAnimationIndex(instance);
  const auto animation_time_ms =
      static_cast<std::uint32_t>(instance.animation_time * 1000.0f);
  const auto blend_source_index = instance.IsPoseBlending()
                                      ? instance.pose_blend_source_sequence_index
                                      : kInvalidM2AnimationSequenceIndex;
  const auto blend_source_time_ms = static_cast<std::uint32_t>(
      instance.pose_blend_source_time * 1000.0f);
  const float blend_factor = instance.PoseBlendFactor();

  const bool camera_matches =
      !resource.has_billboard_bones ||
      (cache.camera_inverse_view.has_value() == camera_inverse_view.has_value() &&
       (!camera_inverse_view.has_value() ||
        std::equal(cache.camera_inverse_view->begin(), cache.camera_inverse_view->end(),
                   camera_inverse_view->begin(), camera_inverse_view->end())));

  const bool pose_time_invariant =
      instance.active_animation_slot_count == 0u && !instance.IsPoseBlending() &&
      ModelPoseIsTimeInvariantAt(resource.model_data, animation_index);
  if (cache.valid && cache.model_id == instance.model_id &&
      cache.animation_index == animation_index &&
      ((pose_time_invariant && cache.pose_time_invariant) ||
       cache.animation_time_ms == animation_time_ms) &&
      cache.blend_source_sequence_index == blend_source_index &&
      cache.blend_source_time_ms == blend_source_time_ms &&
      cache.blend_factor == blend_factor && camera_matches &&
      cache.animation_state_generation == instance.animation_state_generation) {
    return &cache.bone_matrices;
  }

  M2Animator animator(&resource.model_data);
  const std::optional<int> blend_source =
      instance.IsPoseBlending()
          ? std::optional<int>{static_cast<int>(instance.pose_blend_source_sequence_index)}
          : std::nullopt;
  if (!animator.ComputeLayeredBoneMatricesInto(
          &cache.bone_matrices, animation_index, animation_time_ms,
          instance.animation_slots, blend_source, blend_source_time_ms,
          blend_factor, camera_inverse_view, instance.bone_basis_overrides)) {
    cache.valid = false;
    return nullptr;
  }
  cache.valid = true;
  cache.model_id = instance.model_id;
  cache.animation_index = animation_index;
  cache.animation_time_ms = animation_time_ms;
  cache.pose_time_invariant = pose_time_invariant;
  cache.blend_source_sequence_index = blend_source_index;
  cache.blend_source_time_ms = blend_source_time_ms;
  cache.blend_factor = blend_factor;
  cache.animation_state_generation = instance.animation_state_generation;

  if (camera_inverse_view.has_value()) {
    if (!cache.camera_inverse_view.has_value()) {
      cache.camera_inverse_view.emplace();
    }
    std::copy(camera_inverse_view->begin(), camera_inverse_view->end(),
              cache.camera_inverse_view->begin());
  } else {
    cache.camera_inverse_view.reset();
  }
  return &cache.bone_matrices;
}

std::optional<RenderMatrix4x4> SampleM2InstanceAttachmentBoneMatrix(
    const detail::M2Instance& instance,
    const detail::M2ModelResource& resource,
    const std::uint32_t bone_index) {
  if (!resource.has_bones) {
    return std::nullopt;
  }
  M2Animator animator(&resource.model_data);
  const int animation_index = ResolveM2RenderableAnimationIndex(instance);
  const auto animation_time_ms =
      static_cast<std::uint32_t>(instance.animation_time * 1000.0f);
  const std::optional<int> blend_source =
      instance.IsPoseBlending()
          ? std::optional<int>{static_cast<int>(instance.pose_blend_source_sequence_index)}
          : std::nullopt;
  const auto blend_source_time_ms =
      static_cast<std::uint32_t>(instance.pose_blend_source_time * 1000.0f);

  auto single = animator.ComputeSingleBoneMatrix(
      bone_index, animation_index, animation_time_ms, instance.animation_slots,
      blend_source, blend_source_time_ms, instance.PoseBlendFactor(),
      std::nullopt, instance.bone_basis_overrides);
  if (single.has_value()) {
    return single;
  }

  const auto* const matrices = SampleM2InstanceBoneMatricesCached(instance, resource);
  if (matrices == nullptr) {
    return std::nullopt;
  }
  const auto bone = BoneMatrixAt(*matrices, bone_index);
  if (!bone.has_value()) {
    return std::nullopt;
  }
  RenderMatrix4x4 result{};
  std::copy(bone->begin(), bone->end(), result.begin());
  return result;
}

std::optional<std::vector<float>> ComputeM2InstanceBoneMatricesForQuery(
    const detail::M2Instance& instance,
    const detail::M2ModelResource& resource,
    const std::optional<RenderMatrix4x4View>& camera_inverse_view) {
  const auto* const cached = SampleM2InstanceBoneMatricesCached(
      instance, resource, camera_inverse_view);
  if (cached == nullptr) {
    return std::nullopt;
  }
  return *cached;
}

M2SpatialQueries::M2SpatialQueries(
    M2SystemMutex& mutex, const M2ModelStore& models,
    const M2InstanceStore& instances,
    const data::dbc::DbcLoader* const& dbc) noexcept
    : mutex_(mutex), models_(models), instances_(instances), dbc_(dbc) {}

M2InstanceReadinessQuery M2SpatialQueries::QueryReadinessScratchLocked(
    const std::uint32_t instance_id) const {

  readiness_visited_scratch_.clear();
  return QueryReadinessLocked(instance_id, readiness_visited_scratch_);
}

M2InstanceReadinessQuery M2SpatialQueries::QueryReadiness(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  return QueryReadinessScratchLocked(instance_id);
}

M2InstanceReadinessQuery M2SpatialQueries::QueryReadinessLocked(
    const std::uint32_t instance_id, std::vector<std::uint32_t>& visited) const {

  if (std::find(visited.begin(), visited.end(), instance_id) != visited.end()) {
    return {.status = M2ResultStatus::kFailed, .reason = M2ResultReason::kInvalidHandle,
            .detail = "attachment cycle at instance_id=" + std::to_string(instance_id)};
  }
  visited.push_back(instance_id);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  return QueryReadinessResolvedLocked(*instance->second, visited);
}

M2InstanceReadinessQuery M2SpatialQueries::QueryReadinessOfLocked(
    const std::uint32_t instance_id, const detail::M2Instance& instance) const {

  readiness_visited_scratch_.clear();
  readiness_visited_scratch_.push_back(instance_id);
  return QueryReadinessResolvedLocked(instance, readiness_visited_scratch_);
}

M2InstanceReadinessQuery M2SpatialQueries::QueryReadinessResolvedLocked(
    const detail::M2Instance& instance, std::vector<std::uint32_t>& visited) const {
  const auto model = models_.find(instance.model_id);
  if (model == models_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "model_id=" + std::to_string(instance.model_id)};
  if (!model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = model->second->model_path};
  if (!model->second->IsReadyForRender()) return {.status = M2ResultStatus::kReady,
      .reason = M2ResultReason::kGpuGeometryNotReady, .detail = model->second->model_path,
      .render_ready = false};
  for (const auto& child : instance.child_links) {
    const auto readiness = QueryReadinessLocked(child.instance_id, visited);
    if (readiness.status != M2ResultStatus::kReady) return readiness;
    if (!readiness.render_ready) return {.status = M2ResultStatus::kReady,
        .reason = readiness.reason, .detail = "child_instance_id=" + std::to_string(child.instance_id) +
            (readiness.detail.empty() ? "" : ": " + readiness.detail), .render_ready = false};
  }

  visited.pop_back();
  return {.status = M2ResultStatus::kReady, .render_ready = true};
}

M2InstanceAnimationInfoQuery M2SpatialQueries::QueryAnimationInfo(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto it = instances_.find(instance_id);
  if (it == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(it->second->model_id);
  if (model == models_.end() || !model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = "model_id=" + std::to_string(it->second->model_id)};
  return {.status = M2ResultStatus::kReady,
          .info = BuildAnimationInfoLocked(*it->second, *model->second)};
}

M2InstanceAnimationInfo M2SpatialQueries::BuildAnimationInfoLocked(
    const detail::M2Instance& instance,
    const detail::M2ModelResource& resource) noexcept {
  return {.requested_animation_id = instance.animation_id,
          .resolved_animation_id = instance.resolved_animation_id,
          .sequence_index = instance.animation_sequence_index,
          .time_ms = AnimationTimeMs(instance),
          .duration_ms = SequenceDurationMs(resource.model_data,
                                            instance.animation_sequence_index)};
}

M2ModelSpatialInfo M2SpatialQueries::BuildSpatialInfoLocked(
    const detail::M2Instance& instance,
    const detail::M2ModelResource& resource) {
  return BuildSpatialInfo(resource, instance.animation_sequence_index);
}

M2InstanceEventQuery M2SpatialQueries::QueryEvent(
    const std::uint32_t instance_id, const std::uint32_t animation_id,
    const std::uint32_t event_identifier) const {
  std::lock_guard lock(mutex_);
  const auto instance_it = instances_.find(instance_id);
  if (instance_it == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model_it = models_.find(instance_it->second->model_id);
  if (model_it == models_.end() || !model_it->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = "model_id=" + std::to_string(instance_it->second->model_id)};
  const auto& model = model_it->second->model_data;
  const auto resolved_id = ResolveM2AnimationId(model, animation_id, [this](const auto id) {
    return LookupAnimationAliasInfo(id, dbc_);
  });
  if (resolved_id >= kInvalidM2AnimationBehaviorId) return {.status = M2ResultStatus::kReady};
  const auto sequence_index = FindM2AnimationSequenceIndex(model, resolved_id, 0u);
  if (sequence_index == kInvalidM2AnimationSequenceIndex) return {.status = M2ResultStatus::kReady};
  const auto* event = FindEventByIdentifier(model, event_identifier);
  if (event == nullptr) return {.status = M2ResultStatus::kReady};
  std::uint32_t event_time = 0u;
  if (!event->timings.times_ms.empty()) {
    std::size_t set = event->timings.global_sequence < 0 ? sequence_index : 0u;
    if (set >= event->timings.times_ms.size()) set = 0u;
    if (!event->timings.times_ms[set].empty()) event_time = event->timings.times_ms[set].front();
  }
  std::vector<float> matrices;
  if (model_it->second->has_bones) {
    M2Animator animator(&model);
    auto sampled = animator.ComputeBoneMatrices(sequence_index, event_time);
    if (!sampled.has_value()) return {.status = M2ResultStatus::kFailed,
        .reason = M2ResultReason::kBonePoseFailed, .detail = "instance_id=" + std::to_string(instance_id) +
            " animation_id=" + std::to_string(resolved_id)};
    matrices = std::move(*sampled);
  }
  const auto model_position = ResolveEventPosition(*event, matrices, model.bones.size());
  const auto matrix = ComputeM2ModelMatrix(*instance_it->second);
  return {.status = M2ResultStatus::kReady, .has_event = true,
          .event = {.identifier = event_identifier, .animation_id = resolved_id,
                    .sequence_index = sequence_index, .time_ms = event_time,
                    .model_position = model_position,
                    .world_position = TransformPoint(RenderVec3View{model_position},
                                                     RenderMatrix4x4View{matrix})}};
}

M2InstanceSpatialInfoQuery M2SpatialQueries::QuerySpatialInfo(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end() || !model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = "model_id=" + std::to_string(instance->second->model_id)};
  return {.status = M2ResultStatus::kReady,
          .spatial = BuildSpatialInfoLocked(*instance->second, *model->second)};
}

M2SegmentIntersectionQuery M2SpatialQueries::QueryClosestSegmentIntersection(
    const std::uint32_t instance_id, const RenderVec3& segment_start,
    const RenderVec3& segment_end) const {
  const auto segment = PrepareSegment(segment_start, segment_end);
  if (!segment.has_value()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidQuery, .detail = "invalid or degenerate segment"};
  std::lock_guard lock(mutex_);
  const auto instance_it = instances_.find(instance_id);
  if (instance_it == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  if (!instance_it->second->visible) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kNotVisible, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto& instance = *instance_it->second;
  const auto model_it = models_.find(instance.model_id);
  if (model_it == models_.end() || !model_it->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = "model_id=" + std::to_string(instance.model_id)};
  const auto& resource = *model_it->second;
  if (resource.skin_data.indices.empty() || resource.skin_data.triangles.empty()) return {
      .status = M2ResultStatus::kUnsupported, .reason = M2ResultReason::kNoDrawableGeometry,
      .detail = "model_id=" + std::to_string(instance.model_id)};
  const auto world = ComputeM2ModelMatrix(instance);
  RenderMatrix4x4 inverse{};
  bx::mtxInverse(inverse.data(), world.data());
  if (std::any_of(inverse.begin(), inverse.end(), [](const float value) { return !std::isfinite(value); }))
    return {.status = M2ResultStatus::kFailed, .reason = M2ResultReason::kInvalidTransform,
            .detail = "instance_id=" + std::to_string(instance_id)};
  const bx::Vec3 local_start = bx::mul(segment->start, inverse.data());
  const bx::Vec3 local_end = bx::mul(segment->end, inverse.data());
  if (!SegmentIntersectsBounds(local_start, local_end, resource)) return {.status = M2ResultStatus::kReady};
  const auto& geometry = resource.skin_geometry;
  if (geometry.vertices.empty() || geometry.indices.empty()) return {
      .status = M2ResultStatus::kUnsupported, .reason = M2ResultReason::kNoDrawableGeometry,
      .detail = "model_id=" + std::to_string(instance.model_id)};
  const auto* visible = instance.has_visible_submesh_filter ? &instance.visible_submesh_indices : nullptr;
  std::optional<float> best;
  if (!resource.has_bones) {
    best = FindClosestIntersection(resource.skin_data, geometry, visible,
        [&geometry](const std::uint16_t index) {
          const auto& vertex = geometry.vertices[index];
          return bx::Vec3{vertex.position[0], vertex.position[1], vertex.position[2]};
        }, local_start, local_end);
  } else {
    const auto* const matrices = SampleM2InstanceBoneMatricesCached(instance, resource);
    if (matrices == nullptr) return {.status = M2ResultStatus::kFailed,
        .reason = M2ResultReason::kBonePoseFailed, .detail = "instance_id=" + std::to_string(instance_id)};
    const auto streams = ComputeCpuSkinnedVertexStreams(
        std::span<const data::model::M2Vertex>(geometry.query_vertices.data(),
                                               geometry.query_vertices.size()), *matrices);
    best = FindClosestIntersection(resource.skin_data, geometry, visible,
        [&streams](const std::uint16_t index) { return streams.positions[index]; },
        local_start, local_end);
  }
  if (!best.has_value()) return {.status = M2ResultStatus::kReady};
  const float distance = segment->max_distance * *best;
  const auto point = bx::add(segment->start, bx::mul(segment->direction, distance));
  return {.status = M2ResultStatus::kReady, .has_intersection = true,
          .intersection = {.fraction = *best, .distance = distance,
                           .point = {point.x, point.y, point.z}}};
}

bool M2SpatialQueries::ModelHasAnimation(const std::uint32_t instance_id,
                                         const std::uint32_t animation_id) const {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return false;
  const auto model = models_.find(instance->second->model_id);
  return model != models_.end() && M2ModelHasAnimationId(
      model->second->model_data, animation_id,
      [this](const auto id) { return LookupAnimationAliasInfo(id, dbc_); });
}

M2BoneMatricesQuery M2SpatialQueries::QueryBoneMatrices(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "model_id=" + std::to_string(instance->second->model_id)};
  if (!model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = model->second->model_path};
  if (!model->second->has_bones) return {.status = M2ResultStatus::kReady};
  auto matrices = ComputeM2InstanceBoneMatricesForQuery(*instance->second, *model->second);
  if (!matrices.has_value()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kBonePoseFailed, .detail = "instance_id=" + std::to_string(instance_id)};
  return {.status = M2ResultStatus::kReady, .bone_matrices = std::move(*matrices)};
}

M2SampleBoneMatricesQuery M2SpatialQueries::QuerySampleBoneMatrices(
    const std::uint32_t instance_id,
    const std::optional<RenderMatrix4x4View>& camera_inverse_view) const {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "model_id=" + std::to_string(instance->second->model_id)};
  if (!model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = model->second->model_path};
  if (!model->second->has_bones) return {.status = M2ResultStatus::kReady};
  auto matrices = ComputeM2InstanceBoneMatricesForQuery(*instance->second, *model->second,
                                                        camera_inverse_view);
  if (!matrices.has_value()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kBonePoseFailed, .detail = "instance_id=" + std::to_string(instance_id)};
  return {.status = M2ResultStatus::kReady, .bone_matrices = std::move(*matrices)};
}

M2CameraSampleQuery M2SpatialQueries::QueryCameraSample(
    const std::uint32_t instance_id, const int selector,
    const M2CameraLookupKind kind) const {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "model_id=" + std::to_string(instance->second->model_id)};
  if (!model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = model->second->model_path};
  int camera_index = selector;
  if (kind == M2CameraLookupKind::kType) {
    const auto& cameras = model->second->model_data.cameras;
    const auto camera = std::find_if(cameras.begin(), cameras.end(), [selector](const auto& value) {
      return value.type == static_cast<std::uint32_t>(selector);
    });
    camera_index = camera == cameras.end() ? -1 : static_cast<int>(std::distance(cameras.begin(), camera));
  }
  if (camera_index < 0 || static_cast<std::size_t>(camera_index) >= model->second->model_data.cameras.size())
    return {.status = M2ResultStatus::kUnsupported, .reason = M2ResultReason::kMissingCamera,
            .detail = std::string(kind == M2CameraLookupKind::kType ? "camera_type=" : "camera_index=") +
                      std::to_string(selector)};
  M2Animator animator(&model->second->model_data);
  const auto& inst = *instance->second;
  const auto pose = inst.IsPoseBlending()
      ? animator.SampleBlendedCamera(camera_index, ResolveM2RenderableAnimationIndex(inst),
            AnimationTimeMs(inst), static_cast<int>(inst.pose_blend_source_sequence_index),
            static_cast<std::uint32_t>(inst.pose_blend_source_time * 1000.0f), inst.PoseBlendFactor())
      : animator.SampleCamera(camera_index, ResolveM2RenderableAnimationIndex(inst), AnimationTimeMs(inst));
  if (!pose.has_value()) return {.status = M2ResultStatus::kUnsupported,
      .reason = M2ResultReason::kMissingCamera, .detail = "camera_index=" + std::to_string(camera_index)};
  return {.status = M2ResultStatus::kReady, .pose = *pose};
}

M2LightSampleQuery M2SpatialQueries::QueryLightSample(
    const std::uint32_t instance_id, const int light_index,
    std::span<const float> matrices) const {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "model_id=" + std::to_string(instance->second->model_id)};
  if (!model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = model->second->model_path};
  if (light_index < 0 || static_cast<std::size_t>(light_index) >= model->second->model_data.lights.size())
    return {.status = M2ResultStatus::kUnsupported, .reason = M2ResultReason::kMissingLight,
            .detail = "light_index=" + std::to_string(light_index)};
  M2Animator animator(&model->second->model_data);
  return {.status = M2ResultStatus::kReady,
          .light = animator.SampleLight(light_index, ResolveM2RenderableAnimationIndex(*instance->second),
                                        AnimationTimeMs(*instance->second), matrices)};
}

std::vector<M2TriggeredEvent> M2SpatialQueries::CollectTriggeredEvents(
    const std::uint32_t instance_id, const std::uint32_t previous_time_ms,
    const std::uint32_t current_time_ms, std::span<const float> matrices,
    const std::optional<RenderMatrix4x4>& model_matrix) const {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end() || !instance->second) return {};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end() || !model->second->loaded) return {};
  M2Animator animator(&model->second->model_data);
  return animator.CollectTriggeredEvents(ResolveM2RenderableAnimationIndex(*instance->second),
                                          previous_time_ms, current_time_ms, matrices, model_matrix);
}

M2KeyBoneQuery M2SpatialQueries::QueryKeyBone(
    const std::uint32_t instance_id, const std::uint32_t lookup_index) const {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "model_id=" + std::to_string(instance->second->model_id)};
  if (!model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = model->second->model_path};
  return {.status = M2ResultStatus::kReady,
          .present = ModelHasKeyBone(model->second->model_data, lookup_index)};
}

M2AttachmentInfoQuery M2SpatialQueries::QueryAttachmentInfo(
    const std::uint32_t instance_id, const std::uint32_t lookup_index) const {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end() || !model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = "model_id=" + std::to_string(instance->second->model_id)};
  const auto resolved = ResolveAttachmentLookup(model->second->model_data, lookup_index);
  if (resolved.attachment == nullptr) return {.status = M2ResultStatus::kUnsupported,
      .reason = M2ResultReason::kMissingAttachment, .detail = "attachment_lookup_index=" + std::to_string(lookup_index)};
  if (resolved.attachment->bone >= model->second->model_data.bones.size()) return {
      .status = M2ResultStatus::kFailed, .reason = M2ResultReason::kBonePoseFailed,
      .detail = "attachment_bone=" + std::to_string(resolved.attachment->bone)};
  return {.status = M2ResultStatus::kReady,
          .attachment = {.attachment_index = resolved.attachment_index,
                         .bone_index = resolved.attachment->bone,
                         .local_position = AttachmentLocalPosition(*resolved.attachment)}};
}

M2AttachmentPositionQuery M2SpatialQueries::QueryAttachmentPosition(
    const std::uint32_t instance_id, const std::uint32_t lookup_index,
    const std::optional<RenderVec3>& offset) const {
  if (offset.has_value() && !Vec3IsFinite(*offset)) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidTransform, .detail = "local_offset"};
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end() || !model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = "model_id=" + std::to_string(instance->second->model_id)};
  const auto resolved = ResolveAttachmentLookup(model->second->model_data, lookup_index);
  if (resolved.attachment == nullptr) return {.status = M2ResultStatus::kUnsupported,
      .reason = M2ResultReason::kMissingAttachment, .detail = "attachment_lookup_index=" + std::to_string(lookup_index)};

  const auto bone = SampleM2InstanceAttachmentBoneMatrix(
      *instance->second, *model->second, resolved.attachment->bone);
  if (!bone.has_value()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kBonePoseFailed, .detail = "attachment_bone=" + std::to_string(resolved.attachment->bone)};
  auto point = AttachmentLocalPosition(*resolved.attachment);
  if (offset.has_value()) std::transform(point.begin(), point.end(), offset->begin(), point.begin(), std::plus<>{});
  point = TransformPoint(RenderVec3View{point}, RenderMatrix4x4View{*bone});
  const auto matrix = ComputeM2ModelMatrix(*instance->second);
  return {.status = M2ResultStatus::kReady,
          .position = TransformPoint(RenderVec3View{point}, RenderMatrix4x4View{matrix})};
}

M2AttachmentTransformQuery
M2SpatialQueries::QueryAttachmentTransformMatrixLocked(
    const std::uint32_t instance_id, const std::uint32_t lookup_index) const {
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end() || !model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = "model_id=" + std::to_string(instance->second->model_id)};
  const auto resolved = ResolveAttachmentLookup(model->second->model_data, lookup_index);
  if (resolved.attachment == nullptr) return {.status = M2ResultStatus::kUnsupported,
      .reason = M2ResultReason::kMissingAttachment, .detail = "attachment_lookup_index=" + std::to_string(lookup_index)};

  const auto bone = SampleM2InstanceAttachmentBoneMatrix(
      *instance->second, *model->second, resolved.attachment->bone);
  if (!bone.has_value()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kBonePoseFailed, .detail = "attachment_bone=" + std::to_string(resolved.attachment->bone)};
  RenderMatrix4x4 attachment{};
  std::copy(bone->begin(), bone->end(), attachment.begin());
  const auto local = AttachmentLocalPosition(*resolved.attachment);
  const auto position = TransformPoint(RenderVec3View{local}, RenderMatrix4x4View{attachment});
  std::copy(position.begin(), position.end(), attachment.begin() + 12);
  const auto world = ComputeM2ModelMatrix(*instance->second);
  return {.status = M2ResultStatus::kReady,
          .matrix = MultiplyMatrix4x4(RenderMatrix4x4View{attachment}, RenderMatrix4x4View{world})};
}

M2AttachmentTransformQuery M2SpatialQueries::QueryAttachmentTransformMatrix(
    const std::uint32_t instance_id, const std::uint32_t lookup_index) const {
  std::lock_guard lock(mutex_);
  return QueryAttachmentTransformMatrixLocked(instance_id, lookup_index);
}

void M2SpatialQueries::QueryAttachmentPlacements(
    const std::span<const M2AttachmentPlacementRequest> requests,
    const std::span<M2AttachmentPlacementQuery> out_results) const {
  std::lock_guard lock(mutex_);
  for (std::size_t index = 0; index < requests.size(); ++index) {
    const M2AttachmentPlacementRequest& request = requests[index];
    M2AttachmentPlacementQuery& result = out_results[index];
    result = {};
    result.readiness = QueryReadinessScratchLocked(request.child_instance_id);

    if (result.readiness.status != M2ResultStatus::kReady ||
        !result.readiness.render_ready) {
      continue;
    }
    result.transform = QueryAttachmentTransformMatrixLocked(
        request.parent_instance_id, request.attachment_lookup_index);
  }
}

M2ModelWorldPointQuery M2SpatialQueries::QueryModelWorldPoint(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end() || !model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = "model_id=" + std::to_string(instance->second->model_id)};
  const auto matrix = ComputeM2ModelMatrix(*instance->second);
  const RenderVec3 origin{};
  return {.status = M2ResultStatus::kReady,
          .position = TransformPoint(RenderVec3View{origin}, RenderMatrix4x4View{matrix})};
}

M2ModelWorldTransformMatrixQuery M2SpatialQueries::QueryModelWorldTransformMatrix(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end() || !model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = "model_id=" + std::to_string(instance->second->model_id)};
  return {.status = M2ResultStatus::kReady, .matrix = ComputeM2ModelMatrix(*instance->second)};
}

M2RootBoneWorldMatrixQuery M2SpatialQueries::QueryRootBoneWorldMatrix(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end() || !model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = "model_id=" + std::to_string(instance->second->model_id)};
  const auto* const matrices = SampleM2InstanceBoneMatricesCached(
      *instance->second, *model->second);
  if (matrices == nullptr) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kBonePoseFailed, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto root = BoneMatrixAt(*matrices, 0u);
  if (!root.has_value()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kBonePoseFailed, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto world = ComputeM2ModelMatrix(*instance->second);
  return {.status = M2ResultStatus::kReady,
          .matrix = MultiplyMatrix4x4(*root, RenderMatrix4x4View{world})};
}

std::optional<SelectionTriangleVertices>
M2SpatialQueries::BuildSelectionTriangleVerticesForSample(
    const std::uint32_t model_id, const std::vector<float>& matrices,
    const std::size_t bone_count, const RenderMatrix4x4& model_matrix) const {
  std::lock_guard lock(mutex_);
  const auto model = models_.find(model_id);
  if (model == models_.end() || !model->second->loaded) return std::nullopt;
  const auto* top = FindEventByIdentifier(model->second->model_data, kSelectionTriangleTopTag);
  const auto* bottom = FindEventByIdentifier(model->second->model_data, kSelectionTriangleBottomTag);
  if (top == nullptr || bottom == nullptr) return std::nullopt;
  const auto top_model = ResolveEventPosition(*top, matrices, bone_count);
  const auto bottom_model = ResolveEventPosition(*bottom, matrices, bone_count);
  SelectionTriangleVertices triangle;
  triangle.positions[0] = TransformPoint(RenderVec3View{top_model}, RenderMatrix4x4View{model_matrix});
  triangle.positions[1] = TransformPoint(RenderVec3View{bottom_model}, RenderMatrix4x4View{model_matrix});
  const RenderVec3 midpoint{(top_model[0] + bottom_model[0]) * 0.5f,
                            (top_model[1] + bottom_model[1]) * 0.5f,
                            (top_model[2] + bottom_model[2]) * 0.5f};
  triangle.positions[2] = TransformPoint(RenderVec3View{midpoint}, RenderMatrix4x4View{model_matrix});
  return triangle;
}

M2WorldBoundingSphereQuery M2SpatialQueries::QueryWorldBoundingSphere(
    const std::uint32_t instance_id) const {
  M2WorldBoundingSphereQuery result;
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) { result.status = M2ResultStatus::kFailed;
    result.reason = M2ResultReason::kInvalidHandle; result.detail = "instance_id=" + std::to_string(instance_id); return result; }
  if (!instance->second->visible) { result.status = M2ResultStatus::kNotReady;
    result.reason = M2ResultReason::kNotVisible; result.detail = "instance_id=" + std::to_string(instance_id); return result; }
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end()) { result.status = M2ResultStatus::kFailed;
    result.reason = M2ResultReason::kInvalidHandle; result.detail = "model_id=" + std::to_string(instance->second->model_id); return result; }
  if (!model->second->loaded) { result.status = M2ResultStatus::kNotReady;
    result.reason = M2ResultReason::kMissingFile; result.detail = model->second->model_path; return result; }
  const auto local = model->second->GetTransitionBoundingSphere(
      instance->second->animation_sequence_index, instance->second->GetBlendSourceSequenceIndex());
  const auto matrix = ComputeM2ModelMatrix(*instance->second);
  const RenderVec3 center{local[0], local[1], local[2]};
  const auto world = TransformPoint(RenderVec3View{center}, RenderMatrix4x4View{matrix});
  std::copy(world.begin(), world.end(), result.sphere.begin());
  result.sphere[3] = UniformScale(RenderMatrix4x4View{matrix}) * local[3];
  result.status = M2ResultStatus::kReady;
  return result;
}

M2WorldBoundingBoxQuery M2SpatialQueries::QueryWorldBoundingBox(
    const std::uint32_t instance_id) const {
  M2WorldBoundingBoxQuery result;
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) { result.status = M2ResultStatus::kFailed;
    result.reason = M2ResultReason::kInvalidHandle; result.detail = "instance_id=" + std::to_string(instance_id); return result; }
  if (!instance->second->visible) { result.status = M2ResultStatus::kNotReady;
    result.reason = M2ResultReason::kNotVisible; result.detail = "instance_id=" + std::to_string(instance_id); return result; }
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end()) { result.status = M2ResultStatus::kFailed;
    result.reason = M2ResultReason::kInvalidHandle; result.detail = "model_id=" + std::to_string(instance->second->model_id); return result; }
  if (!model->second->loaded) { result.status = M2ResultStatus::kNotReady;
    result.reason = M2ResultReason::kMissingFile; result.detail = model->second->model_path; return result; }
  const auto local = model->second->GetTransitionBoundingBox(
      instance->second->animation_sequence_index, instance->second->GetBlendSourceSequenceIndex());
  const auto matrix = ComputeM2ModelMatrix(*instance->second);
  result.box = TransformAabb(RenderAabbView{local}, RenderMatrix4x4View{matrix});
  result.status = M2ResultStatus::kReady;
  return result;
}

M2ModelBoundingSphereQuery M2SpatialQueries::QueryModelBoundingSphere(
    const std::uint32_t instance_id) const {
  std::lock_guard lock(mutex_);
  const auto instance = instances_.find(instance_id);
  if (instance == instances_.end()) return {.status = M2ResultStatus::kFailed,
      .reason = M2ResultReason::kInvalidHandle, .detail = "instance_id=" + std::to_string(instance_id)};
  const auto model = models_.find(instance->second->model_id);
  if (model == models_.end() || !model->second->loaded) return {.status = M2ResultStatus::kNotReady,
      .reason = M2ResultReason::kMissingFile, .detail = "model_id=" + std::to_string(instance->second->model_id)};
  return {.status = M2ResultStatus::kReady, .sphere = model->second->GetBoundingSphere()};
}

}

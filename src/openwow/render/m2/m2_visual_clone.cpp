#include "openwow/render/m2/m2_visual_clone.h"

#include "openwow/render/m2/m2_instance_store.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace openwow::render::m2 {

struct M2InstanceStoreLease::State {
  ~State() {
    if (auto callback = destroy.lock(); callback && root_instance_id != 0u) {
      (*callback)(root_instance_id);
    }
  }

  std::weak_ptr<std::function<void(std::uint32_t)>> destroy;
  std::uint32_t root_instance_id = 0;
};

M2InstanceStoreLease::M2InstanceStoreLease() = default;
M2InstanceStoreLease::~M2InstanceStoreLease() = default;
M2InstanceStoreLease::M2InstanceStoreLease(M2InstanceStoreLease&&) noexcept =
    default;
M2InstanceStoreLease& M2InstanceStoreLease::operator=(
    M2InstanceStoreLease&&) noexcept = default;
M2InstanceStoreLease::M2InstanceStoreLease(std::unique_ptr<State> state)
    : state_(std::move(state)) {}

M2InstanceStoreLease M2InstanceStoreLease::Create(
    std::weak_ptr<std::function<void(std::uint32_t)>> destroy,
    const std::uint32_t root_instance_id) {
  auto state = std::make_unique<State>();
  state->destroy = std::move(destroy);
  state->root_instance_id = root_instance_id;
  return M2InstanceStoreLease(std::move(state));
}

struct M2VisualCloneLease::State {
  struct Node {
    std::uint32_t source_instance_id = 0;
    std::uint32_t clone_instance_id = 0;
    std::size_t parent_index = 0;
    std::int32_t attachment_slot = kM2NoAttachmentLookupIndex;
    bool has_parent = false;
    bool visible = true;
  };
  M2InstanceStoreLease store_lease;
  std::uint64_t visual_revision = 0;
  std::vector<Node> nodes;
};

M2VisualCloneLease::M2VisualCloneLease() = default;
M2VisualCloneLease::~M2VisualCloneLease() = default;
M2VisualCloneLease::M2VisualCloneLease(M2VisualCloneLease&&) noexcept = default;
M2VisualCloneLease& M2VisualCloneLease::operator=(M2VisualCloneLease&&) noexcept = default;
M2VisualCloneLease::M2VisualCloneLease(std::unique_ptr<State> state)
    : state_(std::move(state)) {}
bool M2VisualCloneLease::valid() const noexcept {
  return state_ != nullptr && !state_->nodes.empty() &&
         state_->nodes.front().clone_instance_id != 0u;
}
std::uint64_t M2VisualCloneLease::visual_revision() const noexcept {
  return state_ ? state_->visual_revision : 0u;
}
std::size_t M2VisualCloneLease::node_count() const noexcept {
  return state_ ? state_->nodes.size() : 0u;
}

namespace {

class VisualCloneRollback {
 public:
  explicit VisualCloneRollback(M2System& system) noexcept : system_(system) {}
  ~VisualCloneRollback() {
    for (auto found = roots_.rbegin(); found != roots_.rend(); ++found) {
      (void)system_.DestroyInstance(*found);
    }
  }

  void Own(const std::uint32_t instance_id) {
    try {
      roots_.push_back(instance_id);
    } catch (...) {
      (void)system_.DestroyInstance(instance_id);
      throw;
    }
  }

  void AttachToOwnedParent(const std::uint32_t instance_id) noexcept {
    const auto found = std::find(roots_.begin(), roots_.end(), instance_id);
    if (found != roots_.end()) roots_.erase(found);
  }

  void Commit() noexcept { roots_.clear(); }

 private:
  M2System& system_;
  std::vector<std::uint32_t> roots_;
};

}

M2VisualCloneCreateResult M2System::CreateVisualClone(
    const std::uint32_t source_instance_id,
    const std::uint64_t visual_revision) {
  if (source_instance_id == 0u || visual_revision == 0u) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "visual clone source identity is incomplete"};
  }
  const auto readiness = QueryInstanceReadiness(source_instance_id);
  if (readiness.status != M2ResultStatus::kReady) {
    return {.status = readiness.status,
            .reason = readiness.reason,
            .detail = readiness.detail};
  }
  if (!readiness.render_ready) {
    return {.status = M2ResultStatus::kNotReady,
            .reason = readiness.reason,
            .detail = readiness.detail};
  }

  struct PendingNode {
    std::uint32_t source = 0;
    std::size_t parent = std::numeric_limits<std::size_t>::max();
    std::int32_t slot = kM2NoAttachmentLookupIndex;
  };
  std::vector<PendingNode> pending{{.source = source_instance_id}};
  std::vector<M2VisualCloneLease::State::Node> nodes;
  std::unordered_set<std::uint32_t> visited;
  VisualCloneRollback rollback(*this);
  const auto failure = [](M2ResultStatus status, std::string operation,
                          std::uint32_t source) {
    return M2VisualCloneCreateResult{
        .status = status,
        .reason = M2ResultReason::kInvalidQuery,
        .detail = std::move(operation) + " source_instance_id=" +
                  std::to_string(source)};
  };

  for (std::size_t pending_index = 0; pending_index < pending.size();
       ++pending_index) {
    const PendingNode request = pending[pending_index];
    if (!visited.insert(request.source).second) {
      return {.status = M2ResultStatus::kFailed,
              .reason = M2ResultReason::kInvalidHandle,
              .detail = "visual clone attachment cycle at source_instance_id=" +
                        std::to_string(request.source)};
    }
    const auto source_query = QueryInstanceInfo(request.source);
    if (source_query.status != M2ResultStatus::kReady) {
      return {.status = source_query.status,
              .reason = source_query.reason,
              .detail = source_query.detail};
    }
    const auto& source = source_query.info;
    const auto created = CreateInstance(source.model_id);
    if (created.status != M2ResultStatus::kReady || created.instance_id == 0u) {
      return {.status = created.status,
              .reason = created.reason,
              .detail = created.detail};
    }
    rollback.Own(created.instance_id);
    const std::uint32_t clone = created.instance_id;
    const auto require = [&](const M2ResultStatus status,
                             const char* operation)
        -> std::optional<M2VisualCloneCreateResult> {
      if (status == M2ResultStatus::kReady) return std::nullopt;
      return failure(status, operation, request.source);
    };

    if (auto error = require(ClearReplaceableTexturePaths(clone),
                             "ClearReplaceableTexturePaths")) return std::move(*error);
    for (const auto& texture : source.texture_overrides) {
      if (texture.texture_path.empty()) continue;
      if (auto error = require(
              SetReplaceableTexturePath(clone, texture.type_id,
                                        texture.texture_path),
               "SetReplaceableTexturePath")) return std::move(*error);
    }
    const auto geosets = source.has_visible_submesh_filter
                             ? SetVisibleSubmeshIndices(
                                   clone, source.visible_submesh_indices)
                             : ClearVisibleSubmeshIndices(clone);
    if (auto error = require(geosets, "SetVisibleSubmeshIndices")) return std::move(*error);

    const bool visible = request.parent == std::numeric_limits<std::size_t>::max()
                             ? true
                             : source.visible;
    if (auto error = require(SetVisible(clone, visible), "SetVisible"))
      return std::move(*error);
    if (auto error = require(
            SetEffectEmittersEnabled(clone, source.effect_emitters_enabled),
            "SetEffectEmittersEnabled")) return std::move(*error);
    if (auto error = require(SetTintColor(clone, source.tint_color),
                             "SetTintColor")) return std::move(*error);
    if (auto error = require(SetAlpha(clone, source.alpha), "SetAlpha"))
      return std::move(*error);
    if (auto error = require(
            SetSelectionGlowColor(clone, source.selection_glow_color),
            "SetSelectionGlowColor")) return std::move(*error);
    if (auto error = require(SetInstanceAttachmentVisualState(
                                 clone, source.attachment_visual_state),
                             "SetInstanceAttachmentVisualState")) return std::move(*error);
    if (auto error = require(SetInstanceEffectContext(clone,
                                                       source.effect_context),
                             "SetInstanceEffectContext")) return std::move(*error);
    if (auto error = require(ClearBatchUniforms(clone), "ClearBatchUniforms"))
      return std::move(*error);
    for (const auto& color : source.particle_color_overrides) {
      if (auto error = require(
              SetParticleColorOverride(clone, color.color_index,
                                       color.start_raw, color.mid_raw,
                                       color.end_raw),
               "SetParticleColorOverride")) return std::move(*error);
    }
    const auto colors = source.has_replacement_colors
                            ? SetReplacementColors(clone,
                                                   source.replacement_colors)
                            : ClearReplacementColors(clone);
    if (auto error = require(colors, "SetReplacementColors")) return std::move(*error);
    if (source.animation_sequence_index != kInvalidM2AnimationSequenceIndex ||
        source.pose_playback.has_value()) {
      if (auto error = require(CopyActiveAnimationState(request.source, clone),
                               "CopyActiveAnimationState")) return std::move(*error);
    }

    const std::size_t clone_index = nodes.size();
    nodes.push_back({.source_instance_id = request.source,
                     .clone_instance_id = clone,
                     .parent_index = request.parent ==
                                             std::numeric_limits<std::size_t>::max()
                                         ? 0u
                                         : request.parent,
                     .attachment_slot = request.slot,
                     .has_parent = request.parent !=
                                   std::numeric_limits<std::size_t>::max(),
                     .visible = visible});
    if (nodes.back().has_parent) {
      if (request.parent >= clone_index) {
        return {.status = M2ResultStatus::kFailed,
                .reason = M2ResultReason::kInvalidHandle,
                .detail = "visual clone parent ordering is invalid"};
      }
      if (auto error = require(
              AttachChildInstance(nodes[request.parent].clone_instance_id,
                                   clone, request.slot,
                                   M2ChildDestroyPolicy::kDestroyWithParent),
               "AttachChildInstance")) return std::move(*error);
      rollback.AttachToOwnedParent(clone);
    }
    for (const auto& child : source.child_links) {
      if (child.instance_id != 0u) {
        pending.push_back({.source = child.instance_id,
                           .parent = clone_index,
                           .slot = child.attachment_slot});
      }
    }
  }

  auto state = std::make_unique<M2VisualCloneLease::State>();
  state->store_lease = AcquireInstanceStoreLease(nodes.front().clone_instance_id);
  state->visual_revision = visual_revision;
  state->nodes = std::move(nodes);
  rollback.Commit();
  return {.status = M2ResultStatus::kReady,
          .lease = M2VisualCloneLease(std::move(state))};
}

M2RenderInstanceResult M2System::RenderVisualClone(
    const std::uint16_t view, const M2VisualCloneLease& lease,
    const RenderMatrix4x4& root, const RenderMatrix4x4View view_matrix) {
  if (!lease.valid()) {
    return {.status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle,
            .detail = "visual clone lease is invalid"};
  }
  M2RenderInstanceResult result{.status = M2ResultStatus::kReady};
  std::optional<M2RenderInstanceResult> unsupported;
  for (std::size_t index = 0; index < lease.state_->nodes.size(); ++index) {
    const auto& node = lease.state_->nodes[index];
    RenderMatrix4x4 matrix = root;
    if (node.has_parent) {
      if (node.parent_index >= index) {
        return {.status = M2ResultStatus::kFailed,
                .reason = M2ResultReason::kInvalidHandle,
                .detail = "visual clone render parent ordering is invalid"};
      }
      const auto& parent = lease.state_->nodes[node.parent_index];
      if (node.attachment_slot >= 0) {
        const auto attachment = QueryAttachmentTransformMatrix(
            parent.clone_instance_id,
            static_cast<std::uint32_t>(node.attachment_slot));
        if (attachment.status == M2ResultStatus::kReady) {
          matrix = attachment.matrix;
        } else if (attachment.status != M2ResultStatus::kUnsupported ||
                   attachment.reason != M2ResultReason::kMissingAttachment) {
          return {.status = attachment.status,
                  .reason = attachment.reason,
                  .detail = attachment.detail};
        } else {
          const auto transform =
              QueryModelWorldTransformMatrix(parent.clone_instance_id);
          if (transform.status != M2ResultStatus::kReady) {
            return {.status = transform.status,
                    .reason = transform.reason,
                    .detail = transform.detail};
          }
          matrix = transform.matrix;
        }
      } else {
        const auto transform =
            QueryModelWorldTransformMatrix(parent.clone_instance_id);
        if (transform.status != M2ResultStatus::kReady) {
          return {.status = transform.status,
                  .reason = transform.reason,
                  .detail = transform.detail};
        }
        matrix = transform.matrix;
      }
    }
    if (SetWorldTransformMatrix(node.clone_instance_id, matrix) !=
        M2ResultStatus::kReady) {
      return {.status = M2ResultStatus::kFailed,
              .reason = M2ResultReason::kInvalidTransform,
              .detail = "visual clone transform source_instance_id=" +
                        std::to_string(node.source_instance_id)};
    }
    if (!node.visible) continue;
    const auto draw = RenderInstance(view, node.clone_instance_id, view_matrix);
    result.submitted_draw_count += draw.submitted_draw_count;
    result.submitted_geometry_draw_count += draw.submitted_geometry_draw_count;
    if (draw.status == M2ResultStatus::kUnsupported) {
      if (!unsupported) unsupported = draw;
      continue;
    }
    result.status = MergeM2ResultStatus(result.status, draw.status);
    if (result.reason == M2ResultReason::kNone &&
        draw.status != M2ResultStatus::kReady) {
      result.reason = draw.reason;
      result.detail = draw.detail;
    }
  }
  if (unsupported && M2RenderResultBlocksComposite(*unsupported, result)) {
    result.status = MergeM2ResultStatus(result.status, unsupported->status);
    if (result.reason == M2ResultReason::kNone) {
      result.reason = unsupported->reason;
      result.detail = unsupported->detail;
    }
  }
  if (result.status == M2ResultStatus::kReady &&
      result.submitted_draw_count == 0u) {
    result.status = M2ResultStatus::kUnsupported;
    result.reason = M2ResultReason::kNoDrawableGeometry;
    result.detail = "visual clone submitted no draws";
  }
  return result;
}

M2ResultStatus M2System::SetVisualCloneAnimation(
    const M2VisualCloneLease& lease, const std::uint32_t animation,
    const float speed) {
  return lease.valid() ? SetAnimation(lease.state_->nodes.front().clone_instance_id,
                                      animation, speed)
                       : M2ResultStatus::kFailed;
}

M2ResultStatus M2System::SetVisualCloneAnimationSample(
    const M2VisualCloneLease& lease, const std::uint32_t animation,
    const std::uint32_t time, const float speed) {
  return lease.valid()
             ? SetAnimationSample(lease.state_->nodes.front().clone_instance_id,
                                  animation, time, speed)
             : M2ResultStatus::kFailed;
}

M2CameraSampleQuery M2System::QueryVisualCloneCamera(
    const M2VisualCloneLease& lease, const int camera) const {
  if (!lease.valid()) return {.status = M2ResultStatus::kFailed,
                              .reason = M2ResultReason::kInvalidHandle,
                              .detail = "visual clone lease is invalid"};
  return QueryInstanceCameraSample(lease.state_->nodes.front().clone_instance_id,
                                   camera);
}

M2CameraSampleQuery M2System::QueryVisualCloneCameraByType(
    const M2VisualCloneLease& lease, const std::uint32_t type) const {
  if (!lease.valid()) return {.status = M2ResultStatus::kFailed,
                              .reason = M2ResultReason::kInvalidHandle,
                              .detail = "visual clone lease is invalid"};
  return QueryInstanceCameraSampleByType(
      lease.state_->nodes.front().clone_instance_id, type);
}

M2InstanceSpatialInfoQuery M2System::QueryVisualCloneSpatialInfo(
    const M2VisualCloneLease& lease) const {
  if (!lease.valid()) return {.status = M2ResultStatus::kFailed,
                              .reason = M2ResultReason::kInvalidHandle,
                              .detail = "visual clone lease is invalid"};
  return QueryInstanceSpatialInfo(lease.state_->nodes.front().clone_instance_id);
}

M2VisualCloneSnapshotQuery M2System::QueryVisualCloneSnapshot(
    const M2VisualCloneLease& lease) const {
  if (!lease.valid()) return {.status = M2ResultStatus::kFailed,
                              .reason = M2ResultReason::kInvalidHandle,
                              .detail = "visual clone lease is invalid"};
  M2VisualCloneSnapshotQuery result{.status = M2ResultStatus::kReady};
  for (const auto& node : lease.state_->nodes) {
    const auto query = QueryInstanceInfo(node.clone_instance_id);
    if (query.status != M2ResultStatus::kReady) {
      return {.status = query.status,
              .reason = query.reason,
              .detail = query.detail};
    }
    result.nodes.push_back({
        .model_id = query.info.model_id,
        .has_parent = node.has_parent,
        .parent_index = node.parent_index,
        .attachment_slot = node.attachment_slot,
        .visible = query.info.visible,
        .texture_overrides = query.info.texture_overrides,
        .particle_color_overrides = query.info.particle_color_overrides,
        .has_replacement_colors = query.info.has_replacement_colors,
        .replacement_colors = query.info.replacement_colors,
        .has_visible_submesh_filter = query.info.has_visible_submesh_filter,
        .visible_submesh_indices = query.info.visible_submesh_indices,
        .tint_color = query.info.tint_color,
        .alpha = query.info.alpha});
  }
  return result;
}

}

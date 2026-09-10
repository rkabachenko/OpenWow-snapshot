#include "openwow/render/models/characters/portrait_renderer.h"

#include "openwow/render/m2/m2_system.h"
#include "openwow/render/models/characters/model_portrait.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace openwow::render {
namespace {

constexpr std::uint16_t kPortraitExtent = 64u;

struct PortraitEntry {
  std::unique_ptr<ModelPortrait> target;
  std::weak_ptr<const void> request_owner;
  std::uint32_t model_instance_id{};
  std::uint64_t visual_revision{};
  m2::M2ResultStatus status{m2::M2ResultStatus::kNotReady};
  m2::M2ResultReason reason{m2::M2ResultReason::kNone};
  std::string detail;
  bool dirty{true};
  bool has_content{};
};

PortraitAcquireResult Failure(const m2::M2ResultStatus status,
                              const m2::M2ResultReason reason,
                              std::string detail) {
  return {.status = status, .reason = reason, .detail = std::move(detail)};
}

}

struct PortraitRenderer::Impl {
  explicit Impl(m2::M2System& model_system) : models(model_system) {}

  PortraitAcquireResult AcquireFrom(
      const std::shared_ptr<const void>& request_owner,
      const std::uint32_t display_id,
      const std::uint32_t model_instance_id,
      std::uint8_t& next_view_id,
      const std::uint16_t view_id_limit) {
    if (!request_owner) {
      return Failure(m2::M2ResultStatus::kFailed,
                     m2::M2ResultReason::kInvalidHandle,
                     "portrait request identity is incomplete");
    }

    for (auto it = snapshots.begin(); it != snapshots.end();) {
      if (it->second.request_owner.expired()) {
        it = snapshots.erase(it);
      } else {
        ++it;
      }
    }

    const void* const key = request_owner.get();
    auto [it, inserted] = snapshots.try_emplace(key);
    PortraitEntry& entry = it->second;
    if (inserted) entry.request_owner = request_owner;

    if (!entry.has_content) {
      if (display_id == 0u || model_instance_id == 0u) {
        return Failure(m2::M2ResultStatus::kFailed,
                       m2::M2ResultReason::kInvalidHandle,
                       "portrait source identity is incomplete");
      }
      const auto visual = models.QueryVisualTreeRevision(model_instance_id);
      if (visual.status != m2::M2ResultStatus::kReady) {
        return Failure(visual.status, visual.reason, visual.detail);
      }
      if (entry.model_instance_id != model_instance_id ||
          entry.visual_revision != visual.revision) {
        entry.model_instance_id = model_instance_id;
        entry.visual_revision = visual.revision;
        entry.status = m2::M2ResultStatus::kNotReady;
        entry.reason = m2::M2ResultReason::kNone;
        entry.detail.clear();
        entry.dirty = true;
      }
    }

    if (!entry.target) {
      entry.target = std::make_unique<ModelPortrait>(models);
      if (!entry.target->Initialize(kPortraitExtent, kPortraitExtent)) {
        entry.target.reset();
        return Failure(m2::M2ResultStatus::kNotReady,
                       m2::M2ResultReason::kGpuGeometryNotReady,
                       "portrait framebuffer creation failed");
      }
    }
    entry.target->SetSourceInstance(entry.model_instance_id,
                                    entry.visual_revision);
    if (!bgfx::isValid(entry.target->GetTexture())) {
      return Failure(m2::M2ResultStatus::kNotReady,
                     m2::M2ResultReason::kGpuGeometryNotReady,
                     "portrait color target is not ready");
    }

    if (entry.dirty) {
      if (next_view_id >= view_id_limit) {
        if (!entry.has_content) {
          return Failure(m2::M2ResultStatus::kNotReady,
                         m2::M2ResultReason::kGpuGeometryNotReady,
                         "portrait view budget exhausted");
        }
      } else {
        const ModelPortraitResult rendered =
            entry.target->RenderToTexture(next_view_id++);
        entry.dirty = rendered.CanRetry();
        entry.has_content =
            entry.has_content || rendered.SubmittedCompleteVisual();
        entry.status = rendered.status;
        entry.reason = rendered.reason;
        entry.detail = rendered.detail;
      }
    }

    if (!entry.has_content) {
      return Failure(entry.status, entry.reason,
                     entry.detail.empty()
                         ? "portrait visual has no submitted content"
                         : entry.detail);
    }
    entry.status = m2::M2ResultStatus::kReady;
    entry.reason = m2::M2ResultReason::kNone;
    entry.detail.clear();
    return {
        .status = m2::M2ResultStatus::kReady,
        .texture = PortraitTexture{
            .handle = entry.target->GetTexture(),
            .width = kPortraitExtent,
            .height = kPortraitExtent,
        },
    };
  }

  m2::M2System& models;
  std::unordered_map<const void*, PortraitEntry> snapshots;
  bool initialized{};
};

PortraitRenderer::PortraitRenderer(m2::M2System& models)
    : impl_(std::make_unique<Impl>(models)) {}

PortraitRenderer::~PortraitRenderer() = default;

void PortraitRenderer::Initialize() {
  impl_->initialized = true;
}

void PortraitRenderer::Shutdown() {
  impl_->snapshots.clear();
  impl_->initialized = false;
}

void PortraitRenderer::InvalidateAll() {
  impl_->snapshots.clear();
}

PortraitAcquireResult PortraitRenderer::Acquire(
    std::shared_ptr<const void> request_owner,
    const std::uint32_t display_id,
    const std::uint32_t model_instance_id, std::uint8_t& next_view_id,
    std::uint16_t view_id_limit) {
  if (!impl_->initialized) {
    return Failure(m2::M2ResultStatus::kNotReady,
                   m2::M2ResultReason::kGpuGeometryNotReady,
                   "portrait renderer is not initialized");
  }
  view_id_limit = std::min<std::uint16_t>(view_id_limit, 255u);
  return impl_->AcquireFrom(request_owner, display_id, model_instance_id,
                            next_view_id, view_id_limit);
}

}

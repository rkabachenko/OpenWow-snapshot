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
  std::uint32_t display_id{};
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

  template <typename Key>
  PortraitAcquireResult AcquireFrom(
      std::unordered_map<Key, PortraitEntry>& cache, const Key key,
      const std::uint32_t display_id,
      const std::uint32_t model_instance_id,
      const std::uint64_t visual_revision, std::uint8_t& next_view_id,
      const std::uint16_t view_id_limit) {
    if (display_id == 0u || model_instance_id == 0u) {
      return Failure(m2::M2ResultStatus::kFailed,
                     m2::M2ResultReason::kInvalidHandle,
                     "portrait source identity is incomplete");
    }

    auto& entry = cache[key];
    if (entry.display_id != display_id ||
        entry.model_instance_id != model_instance_id ||
        entry.visual_revision != visual_revision) {
      entry.display_id = display_id;
      entry.model_instance_id = model_instance_id;
      entry.visual_revision = visual_revision;
      entry.status = m2::M2ResultStatus::kNotReady;
      entry.reason = m2::M2ResultReason::kNone;
      entry.detail.clear();
      entry.dirty = true;
      entry.has_content = false;
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
    entry.target->SetSourceInstance(model_instance_id, visual_revision);
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
  std::unordered_map<std::uint64_t, PortraitEntry> players;
  std::unordered_map<std::uint32_t, PortraitEntry> units;
  bool initialized{};
};

PortraitRenderer::PortraitRenderer(m2::M2System& models)
    : impl_(std::make_unique<Impl>(models)) {}

PortraitRenderer::~PortraitRenderer() = default;

void PortraitRenderer::Initialize() {
  impl_->initialized = true;
}

void PortraitRenderer::Shutdown() {
  impl_->players.clear();
  impl_->units.clear();
  impl_->initialized = false;
}

void PortraitRenderer::InvalidatePlayer(const game::ObjectGuid guid) {
  if (const auto found = impl_->players.find(guid.GetRawValue());
      found != impl_->players.end()) {
    found->second.dirty = true;
  }
}

void PortraitRenderer::InvalidateAll() {
  for (auto& [key, entry] : impl_->players) {
    static_cast<void>(key);
    entry.dirty = true;
  }
  for (auto& [key, entry] : impl_->units) {
    static_cast<void>(key);
    entry.dirty = true;
  }
}

PortraitAcquireResult PortraitRenderer::Acquire(
    const game::ObjectGuid guid, const std::uint32_t display_id,
    const std::uint32_t model_instance_id, std::uint8_t& next_view_id,
    std::uint16_t view_id_limit) {
  if (!impl_->initialized) {
    return Failure(m2::M2ResultStatus::kNotReady,
                   m2::M2ResultReason::kGpuGeometryNotReady,
                   "portrait renderer is not initialized");
  }
  const auto visual = impl_->models.QueryVisualTreeRevision(model_instance_id);
  if (visual.status != m2::M2ResultStatus::kReady) {
    return Failure(visual.status, visual.reason, visual.detail);
  }
  view_id_limit = std::min<std::uint16_t>(view_id_limit, 255u);
  if (guid.IsPlayer()) {
    return impl_->AcquireFrom(impl_->players, guid.GetRawValue(), display_id,
                              model_instance_id, visual.revision, next_view_id,
                              view_id_limit);
  }
  return impl_->AcquireFrom(impl_->units, display_id, display_id,
                            model_instance_id, visual.revision, next_view_id,
                            view_id_limit);
}

}

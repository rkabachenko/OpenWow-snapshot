#include "openwow/render/effects/particles/particle_renderer.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace openwow::render {

namespace {

auto MakeBatchKey(const ParticleRenderInstance& instance) {
  return std::tie(instance.sortLayer, instance.type, instance.blendMode,
                  instance.textureId, instance.secondaryTextureId,
                  instance.shaderVariantId, instance.renderStateId);
}

float SquaredDistanceToCamera(const ParticleRenderInstance& instance,
                              const float cam_x,
                              const float cam_y,
                              const float cam_z) {
  return (instance.posX - cam_x) * (instance.posX - cam_x) +
         (instance.posY - cam_y) * (instance.posY - cam_y) +
         (instance.posZ - cam_z) * (instance.posZ - cam_z);
}

}

void ParticleRenderer::Submit(ParticleRenderInstance particle) {
  if (pending_.size() < maxParticles_) {
    pending_.push_back(std::move(particle));
  }
}

size_t ParticleRenderer::GetPendingCount() const {
  return pending_.size();
}

std::vector<ParticleRenderBatch> ParticleRenderer::SortAndBatch() {

  std::sort(pending_.begin(), pending_.end(),
            [this](const ParticleRenderInstance& a,
                   const ParticleRenderInstance& b) {
              const auto key_a = MakeBatchKey(a);
              const auto key_b = MakeBatchKey(b);
              if (key_a != key_b) {
                return key_a < key_b;
              }

              return SquaredDistanceToCamera(a, camX_, camY_, camZ_) >
                     SquaredDistanceToCamera(b, camX_, camY_, camZ_);
            });

  std::vector<ParticleRenderBatch> batches;
  uint32_t rendered = 0;

  if (pending_.empty()) {
    lastBatchCount_ = 0;
    lastStats_.particlesRendered = 0;
    lastStats_.batchCount = 0;
    lastStats_.particlesSkipped = 0;
    return batches;
  }

  ParticleRenderBatch current;
  current.sortLayer = pending_[0].sortLayer;
  current.textureId = pending_[0].textureId;
  current.secondaryTextureId = pending_[0].secondaryTextureId;
  current.blendMode = pending_[0].blendMode;
  current.shaderVariantId = pending_[0].shaderVariantId;
  current.renderStateId = pending_[0].renderStateId;
  current.type = pending_[0].type;

  for (auto& p : pending_) {
    if (MakeBatchKey(p) !=
        std::tie(current.sortLayer, current.type, current.blendMode,
                 current.textureId, current.secondaryTextureId,
                 current.shaderVariantId, current.renderStateId)) {
      rendered += static_cast<uint32_t>(current.instances.size());
      batches.push_back(std::move(current));
      current = ParticleRenderBatch{};
      current.sortLayer = p.sortLayer;
      current.textureId = p.textureId;
      current.secondaryTextureId = p.secondaryTextureId;
      current.blendMode = p.blendMode;
      current.shaderVariantId = p.shaderVariantId;
      current.renderStateId = p.renderStateId;
      current.type = p.type;
    }
    current.instances.push_back(std::move(p));
  }

  if (!current.instances.empty()) {
    rendered += static_cast<uint32_t>(current.instances.size());
    batches.push_back(std::move(current));
  }

  lastBatchCount_ = batches.size();
  lastStats_.particlesRendered = rendered;
  lastStats_.batchCount = static_cast<uint32_t>(batches.size());
  lastStats_.particlesSkipped = 0;
  lastStats_.sortTimeMs = 0.0f;
  lastStats_.drawTimeMs = 0.0f;

  return batches;
}

void ParticleRenderer::SetMaxParticles(uint32_t max) {
  maxParticles_ = max > 0 ? max : 1;
}

uint32_t ParticleRenderer::GetMaxParticles() const {
  return maxParticles_;
}

bool ParticleRenderer::IsAtCapacity() const {
  return pending_.size() >= maxParticles_;
}

size_t ParticleRenderer::GetActiveCount() const {
  return pending_.size();
}

void ParticleRenderer::SetCameraPosition(float x, float y, float z) {
  camX_ = x;
  camY_ = y;
  camZ_ = z;
}

ParticleRendererStats ParticleRenderer::GetStats() const {
  return lastStats_;
}

void ParticleRenderer::SetSoftParticles(bool enabled) {
  softParticles_ = enabled;
}

bool ParticleRenderer::AreSoftParticlesEnabled() const {
  return softParticles_;
}

void ParticleRenderer::Clear() {
  pending_.clear();
  lastBatchCount_ = 0;
}

void ParticleRenderer::BeginFrame() {
  pending_.clear();
  lastBatchCount_ = 0;
}

size_t ParticleRenderer::GetBatchCount() const {
  return lastBatchCount_;
}

}

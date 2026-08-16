#pragma once

#include <cstdint>
#include <vector>

namespace openwow::render {

enum class ParticleRenderType : uint8_t {
  Billboard = 0,
  Trail = 1,
  Model = 2,
  WorldSpace = 3
};

struct ParticleRenderInstance {
  uint32_t particleId{0};
  uint32_t emitterId{0};
  uint32_t emitterParticleIndex{0};
  float posX{0.0f};
  float posY{0.0f};
  float posZ{0.0f};
  float sizeX{1.0f};
  float sizeY{1.0f};
  float rotation{0.0f};
  float r{1.0f};
  float g{1.0f};
  float b{1.0f};
  float a{1.0f};
  uint32_t textureId{0};
  uint32_t secondaryTextureId{0};
  uint32_t blendMode{0};
  uint32_t shaderVariantId{0};
  uint32_t renderStateId{0};
  uint32_t sortLayer{0};
  ParticleRenderType type{ParticleRenderType::Billboard};
  float age{0.0f};
  float lifetime{1.0f};
};

struct ParticleRenderBatch {
  uint32_t sortLayer{0};
  uint32_t textureId{0};
  uint32_t secondaryTextureId{0};
  uint32_t blendMode{0};
  uint32_t shaderVariantId{0};
  uint32_t renderStateId{0};
  ParticleRenderType type{ParticleRenderType::Billboard};
  std::vector<ParticleRenderInstance> instances;
};

struct ParticleRendererStats {
  uint32_t particlesRendered{0};
  uint32_t batchCount{0};
  uint32_t particlesSkipped{0};
  float sortTimeMs{0.0f};
  float drawTimeMs{0.0f};
};

class ParticleRenderer {
 public:
  ParticleRenderer() = default;
  ~ParticleRenderer() = default;

  void Submit(ParticleRenderInstance particle);
  [[nodiscard]] size_t GetPendingCount() const;

  std::vector<ParticleRenderBatch> SortAndBatch();

  void SetMaxParticles(uint32_t max);
  [[nodiscard]] uint32_t GetMaxParticles() const;
  [[nodiscard]] bool IsAtCapacity() const;
  [[nodiscard]] size_t GetActiveCount() const;

  void SetCameraPosition(float x, float y, float z);

  [[nodiscard]] ParticleRendererStats GetStats() const;

  void SetSoftParticles(bool enabled);
  [[nodiscard]] bool AreSoftParticlesEnabled() const;

  void Clear();
  void BeginFrame();
  [[nodiscard]] size_t GetBatchCount() const;

 private:
  std::vector<ParticleRenderInstance> pending_;
  uint32_t maxParticles_{10000};

  float camX_{0.0f};
  float camY_{0.0f};
  float camZ_{0.0f};

  bool softParticles_{false};
  size_t lastBatchCount_{0};

  ParticleRendererStats lastStats_{};
};

}

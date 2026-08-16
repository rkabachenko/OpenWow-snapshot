#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace bgfx {
struct Encoder;
}

namespace openwow::render {

enum class ShadowQuality : std::uint8_t {
    Off    = 0,
    Low    = 1,
    Medium = 2,
    High   = 3,
    Ultra  = 4,
};

enum class ShadowType : std::uint8_t {
    Blob      = 0,
    ShadowMap = 1,
};

struct ShadowCasterEntry {
    std::uint32_t entityId = 0;
    float         worldX   = 0.0f;
    float         worldY   = 0.0f;
    float         worldZ   = 0.0f;
    float         radius   = 1.0f;
    float         height   = 2.0f;
    bool          isValid  = true;
};

class ShadowRenderData {
public:
    ShadowRenderData();
    ~ShadowRenderData();

    ShadowRenderData(const ShadowRenderData&) = delete;
    ShadowRenderData& operator=(const ShadowRenderData&) = delete;

    void          SetQuality(ShadowQuality q);
    [[nodiscard]] ShadowQuality GetQuality() const;

    void          SetType(ShadowType t);
    [[nodiscard]] ShadowType GetType() const;

    void          SetShadowMapResolution(std::uint32_t res);
    [[nodiscard]] std::uint32_t GetShadowMapResolution() const;

    void          SetShadowDistance(float dist);
    [[nodiscard]] float GetShadowDistance() const;

    void          SetShadowBias(float bias);
    [[nodiscard]] float GetShadowBias() const;

    void          SetSplitLambda(float lambda);
    [[nodiscard]] float GetSplitLambda() const;

    bool CreateShadowMap();

    void DestroyShadowMap();

    [[nodiscard]] bool IsShadowMapValid() const;

    [[nodiscard]] const float* GetLightViewProj() const { return light_view_proj_; }
    [[nodiscard]] const float *GetLightView() const {
      return light_view_;
    }

    [[nodiscard]] const float *GetLightProj() const {
      return light_proj_;
    }

    void BindShadowState(bgfx::Encoder* encoder = nullptr) const;

    void AddCaster(ShadowCasterEntry entry);
    void RemoveCaster(std::uint32_t entityId);
    void SetCasters(std::span<const ShadowCasterEntry> casters);
    void ClearCasters() noexcept;

    [[nodiscard]] std::vector<ShadowCasterEntry> GetCasters() const;
    [[nodiscard]] std::uint32_t GetCasterCount() const;

    [[nodiscard]] std::vector<ShadowCasterEntry> GetCastersInRange(float x, float y, float z,
                                                                   float range) const;

    void SetLightDirection(float x, float y, float z);

    struct LightDir { float x, y, z; };
    [[nodiscard]] LightDir GetLightDirection() const;

    bool PrepareShadowPass(const float* camera_mtx,
                           const float* proj_mtx,
                           float cam_near,
                           float cam_far);

    void BeginShadowDepthPass(std::uint8_t view_id);

    void          SetEnabled(bool enabled);
    [[nodiscard]] bool IsEnabled() const;

    [[nodiscard]] static std::string GetQualityName(ShadowQuality q);

    void Reset();

private:
    struct BackendResources;

    void BuildLightMatrices(const float* camera_mtx,
                            const float* proj_mtx,
                            float cam_near,
                            float cam_far,
                            float out_light_view[16],
                            float out_light_proj[16]);

    std::vector<ShadowCasterEntry> casters_;

    ShadowQuality quality_   = ShadowQuality::Medium;
    ShadowType    type_      = ShadowType::Blob;
    std::uint32_t resolution_ = 1024;
    float         distance_  = 40.0f;
    float         bias_      = 0.005f;
    float         split_lambda_ = 0.5f;

    float lightX_    = 0.0f;
    float lightY_    = -1.0f;
    float lightZ_    = 0.0f;

    std::unique_ptr<BackendResources> backend_;
    float light_view_[16]{};
    float light_proj_[16]{};
    float light_view_proj_[16]{};

    bool enabled_       = true;
    bool shadow_map_valid_ = false;
};

}

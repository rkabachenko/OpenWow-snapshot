
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

#include "openwow/core/window_manager.h"

namespace openwow::core {

enum class GraphicsQualityLevel : uint8_t {
    Low    = 0,
    Fair   = 1,
    Good   = 2,
    High   = 3,
    Ultra  = 4,
    Custom = 5,
};

struct DisplaySettingsData {
    uint32_t             resolutionWidth       = 1024;
    uint32_t             resolutionHeight      = 768;
    WindowMode           windowMode            = WindowMode::Windowed;
    bool                 vSync                 = true;
    float                gamma                 = 1.0f;
    uint32_t             multiSampleLevel      = 0;
    uint32_t             anisotropicLevel       = 1;
    float                viewDistance           = 727.0f;
    float                environmentDetail      = 1.0f;
    float                groundClutter          = 64.0f;
    uint32_t             shadowQuality          = 0;
    uint32_t             liquidDetail           = 2;
    uint32_t             sunshafts              = 0;
    uint32_t             projectedTextures      = 1;
    uint32_t             particleDensity        = 100;
    uint32_t             textureResolution      = 2;
    uint32_t             textureFilteringLevel  = 1;
    bool                 trilinearFiltering     = true;
    bool                 fullscreenGlow         = true;
    bool                 deathEffect            = true;
    GraphicsQualityLevel qualityLevel           = GraphicsQualityLevel::Good;
    float                uiScale                = 1.0f;
};

class DisplaySettingsController {
public:
    static DisplaySettingsController& Instance();

    void                         SetData(const DisplaySettingsData& data);
    [[nodiscard]] DisplaySettingsData GetData() const;

    void SetQualityPreset(GraphicsQualityLevel level);
    [[nodiscard]] GraphicsQualityLevel GetQualityLevel() const;
    [[nodiscard]] static std::string   GetQualityName(GraphicsQualityLevel level);

    void SetResolution(uint32_t w, uint32_t h);
    [[nodiscard]] std::pair<uint32_t, uint32_t> GetResolution() const;

    void  SetViewDistance(float d);
    [[nodiscard]] float GetViewDistance() const;

    void     SetShadowQuality(uint32_t q);
    [[nodiscard]] uint32_t GetShadowQuality() const;

    void     SetTextureResolution(uint32_t r);
    [[nodiscard]] uint32_t GetTextureResolution() const;

    void  SetUIScale(float s);
    [[nodiscard]] float GetUIScale() const;

    [[nodiscard]] size_t GetVRAMEstimate() const;

    void Apply();
    [[nodiscard]] bool NeedsApply() const;
    void RevertToSaved();

    void Reset();

private:
    DisplaySettingsController() = default;

    mutable std::mutex mutex_;

    DisplaySettingsData current_;
    DisplaySettingsData saved_;
    bool                needsApply_ = false;
};

}

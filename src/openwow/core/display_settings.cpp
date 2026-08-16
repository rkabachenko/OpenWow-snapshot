
#include "openwow/core/display_settings.h"

#include <algorithm>

namespace openwow::core {

DisplaySettingsController& DisplaySettingsController::Instance() {
    static DisplaySettingsController instance;
    return instance;
}

void DisplaySettingsController::SetData(const DisplaySettingsData& data) {
    std::lock_guard lock(mutex_);
    current_    = data;
    needsApply_ = true;
}

DisplaySettingsData DisplaySettingsController::GetData() const {
    std::lock_guard lock(mutex_);
    return current_;
}

void DisplaySettingsController::SetQualityPreset(GraphicsQualityLevel level) {
    std::lock_guard lock(mutex_);
    current_.qualityLevel = level;
    needsApply_ = true;

    switch (level) {
        case GraphicsQualityLevel::Low:
            current_.viewDistance          = 400.0f;
            current_.environmentDetail    = 0.5f;
            current_.groundClutter        = 20.0f;
            current_.shadowQuality        = 0;
            current_.liquidDetail         = 0;
            current_.sunshafts            = 0;
            current_.projectedTextures    = 0;
            current_.particleDensity      = 25;
            current_.textureResolution    = 0;
            current_.textureFilteringLevel = 0;
            current_.trilinearFiltering   = false;
            current_.fullscreenGlow       = false;
            current_.deathEffect          = false;
            current_.multiSampleLevel     = 0;
            current_.anisotropicLevel     = 1;
            break;

        case GraphicsQualityLevel::Fair:
            current_.viewDistance          = 550.0f;
            current_.environmentDetail    = 0.75f;
            current_.groundClutter        = 40.0f;
            current_.shadowQuality        = 1;
            current_.liquidDetail         = 1;
            current_.sunshafts            = 0;
            current_.projectedTextures    = 1;
            current_.particleDensity      = 50;
            current_.textureResolution    = 1;
            current_.textureFilteringLevel = 1;
            current_.trilinearFiltering   = true;
            current_.fullscreenGlow       = false;
            current_.deathEffect          = true;
            current_.multiSampleLevel     = 0;
            current_.anisotropicLevel     = 1;
            break;

        case GraphicsQualityLevel::Good:
            current_.viewDistance          = 727.0f;
            current_.environmentDetail    = 1.0f;
            current_.groundClutter        = 64.0f;
            current_.shadowQuality        = 2;
            current_.liquidDetail         = 2;
            current_.sunshafts            = 0;
            current_.projectedTextures    = 1;
            current_.particleDensity      = 100;
            current_.textureResolution    = 2;
            current_.textureFilteringLevel = 1;
            current_.trilinearFiltering   = true;
            current_.fullscreenGlow       = true;
            current_.deathEffect          = true;
            current_.multiSampleLevel     = 0;
            current_.anisotropicLevel     = 4;
            break;

        case GraphicsQualityLevel::High:
            current_.viewDistance          = 1000.0f;
            current_.environmentDetail    = 1.5f;
            current_.groundClutter        = 100.0f;
            current_.shadowQuality        = 3;
            current_.liquidDetail         = 2;
            current_.sunshafts            = 1;
            current_.projectedTextures    = 1;
            current_.particleDensity      = 100;
            current_.textureResolution    = 3;
            current_.textureFilteringLevel = 2;
            current_.trilinearFiltering   = true;
            current_.fullscreenGlow       = true;
            current_.deathEffect          = true;
            current_.multiSampleLevel     = 2;
            current_.anisotropicLevel     = 8;
            break;

        case GraphicsQualityLevel::Ultra:
            current_.viewDistance          = 1300.0f;
            current_.environmentDetail    = 2.0f;
            current_.groundClutter        = 128.0f;
            current_.shadowQuality        = 4;
            current_.liquidDetail         = 2;
            current_.sunshafts            = 2;
            current_.projectedTextures    = 1;
            current_.particleDensity      = 100;
            current_.textureResolution    = 3;
            current_.textureFilteringLevel = 3;
            current_.trilinearFiltering   = true;
            current_.fullscreenGlow       = true;
            current_.deathEffect          = true;
            current_.multiSampleLevel     = 4;
            current_.anisotropicLevel     = 16;
            break;

        case GraphicsQualityLevel::Custom:

            break;
    }
}

GraphicsQualityLevel DisplaySettingsController::GetQualityLevel() const {
    std::lock_guard lock(mutex_);
    return current_.qualityLevel;
}

std::string DisplaySettingsController::GetQualityName(GraphicsQualityLevel level) {
    switch (level) {
        case GraphicsQualityLevel::Low:    return "Low";
        case GraphicsQualityLevel::Fair:   return "Fair";
        case GraphicsQualityLevel::Good:   return "Good";
        case GraphicsQualityLevel::High:   return "High";
        case GraphicsQualityLevel::Ultra:  return "Ultra";
        case GraphicsQualityLevel::Custom: return "Custom";
    }
    return "Unknown";
}

void DisplaySettingsController::SetResolution(uint32_t w, uint32_t h) {
    std::lock_guard lock(mutex_);
    current_.resolutionWidth  = w;
    current_.resolutionHeight = h;
    needsApply_ = true;
}

std::pair<uint32_t, uint32_t> DisplaySettingsController::GetResolution() const {
    std::lock_guard lock(mutex_);
    return {current_.resolutionWidth, current_.resolutionHeight};
}

void DisplaySettingsController::SetViewDistance(float d) {
    std::lock_guard lock(mutex_);
    current_.viewDistance = std::clamp(d, 100.0f, 2000.0f);
    current_.qualityLevel = GraphicsQualityLevel::Custom;
    needsApply_ = true;
}

float DisplaySettingsController::GetViewDistance() const {
    std::lock_guard lock(mutex_);
    return current_.viewDistance;
}

void DisplaySettingsController::SetShadowQuality(uint32_t q) {
    std::lock_guard lock(mutex_);
    current_.shadowQuality = std::min(q, 4u);
    current_.qualityLevel  = GraphicsQualityLevel::Custom;
    needsApply_ = true;
}

uint32_t DisplaySettingsController::GetShadowQuality() const {
    std::lock_guard lock(mutex_);
    return current_.shadowQuality;
}

void DisplaySettingsController::SetTextureResolution(uint32_t r) {
    std::lock_guard lock(mutex_);
    current_.textureResolution = std::min(r, 3u);
    current_.qualityLevel      = GraphicsQualityLevel::Custom;
    needsApply_ = true;
}

uint32_t DisplaySettingsController::GetTextureResolution() const {
    std::lock_guard lock(mutex_);
    return current_.textureResolution;
}

void DisplaySettingsController::SetUIScale(float s) {
    std::lock_guard lock(mutex_);
    current_.uiScale = std::clamp(s, 0.64f, 1.0f);
    needsApply_ = true;
}

float DisplaySettingsController::GetUIScale() const {
    std::lock_guard lock(mutex_);
    return current_.uiScale;
}

size_t DisplaySettingsController::GetVRAMEstimate() const {
    std::lock_guard lock(mutex_);

    size_t framebufferBytes = static_cast<size_t>(current_.resolutionWidth)
                            * current_.resolutionHeight * 4u;

    uint32_t msaa = std::max(current_.multiSampleLevel, 1u);
    framebufferBytes *= msaa;

    framebufferBytes += static_cast<size_t>(current_.resolutionWidth)
                      * current_.resolutionHeight * 4u * msaa;

    size_t texBudget = size_t{64} * 1024u * 1024u * (size_t{1} << current_.textureResolution);

    return framebufferBytes + texBudget;
}

void DisplaySettingsController::Apply() {
    std::lock_guard lock(mutex_);
    saved_      = current_;
    needsApply_ = false;
}

bool DisplaySettingsController::NeedsApply() const {
    std::lock_guard lock(mutex_);
    return needsApply_;
}

void DisplaySettingsController::RevertToSaved() {
    std::lock_guard lock(mutex_);
    current_    = saved_;
    needsApply_ = false;
}

void DisplaySettingsController::Reset() {
    std::lock_guard lock(mutex_);
    current_    = DisplaySettingsData{};
    saved_      = DisplaySettingsData{};
    needsApply_ = false;
}

}

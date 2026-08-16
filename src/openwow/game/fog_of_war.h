#pragma once

#include <cstdint>
#include <vector>

namespace openwow::game {

struct FogChunk {
    std::uint32_t x          = 0;
    std::uint32_t y          = 0;
    bool          isRevealed = false;
    float         revealTime = 0.0f;
};

class FogOfWarOverlay {
 public:
    FogOfWarOverlay() = default;

    static constexpr float kDefaultUnrevealedAlpha = 0.7f;

    static constexpr float kFadeDuration = 0.5f;

    void SetGridSize(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] std::uint32_t GetGridWidth() const;
    [[nodiscard]] std::uint32_t GetGridHeight() const;

    void RevealChunk(std::uint32_t x, std::uint32_t y);
    [[nodiscard]] bool IsRevealed(std::uint32_t x, std::uint32_t y) const;

    void RevealArea(std::uint32_t centerX, std::uint32_t centerY,
                    std::uint32_t radius);

    [[nodiscard]] std::uint32_t GetRevealedCount() const;
    [[nodiscard]] std::uint32_t GetTotalChunks() const;
    [[nodiscard]] float         GetRevealPercent() const;

    [[nodiscard]] float GetOverlayAlpha(std::uint32_t x, std::uint32_t y) const;

    void SetOverlayColor(std::uint32_t argb);
    [[nodiscard]] std::uint32_t GetOverlayColor() const;

    void Update(float dt);

    void Reset();

 private:
    [[nodiscard]] bool InBounds(std::uint32_t x, std::uint32_t y) const;
    [[nodiscard]] std::size_t Index(std::uint32_t x, std::uint32_t y) const;

    std::uint32_t              width_  = 0;
    std::uint32_t              height_ = 0;
    std::vector<FogChunk>      chunks_;
    std::uint32_t              overlayColor_ = 0xFF000000;
    float                      elapsed_      = 0.0f;
    std::uint32_t              revealedCount_ = 0;
};

}


#include "openwow/game/fog_of_war.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

void FogOfWarOverlay::SetGridSize(std::uint32_t width, std::uint32_t height) {
    width_  = width;
    height_ = height;
    chunks_.clear();
    chunks_.reserve(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            FogChunk c;
            c.x = x;
            c.y = y;
            chunks_.push_back(c);
        }
    }
    revealedCount_ = 0;
    elapsed_       = 0.0f;
}

std::uint32_t FogOfWarOverlay::GetGridWidth() const { return width_; }
std::uint32_t FogOfWarOverlay::GetGridHeight() const { return height_; }

void FogOfWarOverlay::RevealChunk(std::uint32_t x, std::uint32_t y) {
    if (!InBounds(x, y)) return;
    auto& c = chunks_[Index(x, y)];
    if (!c.isRevealed) {
        c.isRevealed  = true;
        c.revealTime  = elapsed_;
        ++revealedCount_;
    }
}

bool FogOfWarOverlay::IsRevealed(std::uint32_t x, std::uint32_t y) const {
    if (!InBounds(x, y)) return false;
    return chunks_[Index(x, y)].isRevealed;
}

void FogOfWarOverlay::RevealArea(std::uint32_t centerX, std::uint32_t centerY,
                                 std::uint32_t radius) {
    const auto r  = static_cast<std::int64_t>(radius);
    const auto r2 = r * r;
    const auto cx = static_cast<std::int64_t>(centerX);
    const auto cy = static_cast<std::int64_t>(centerY);

    const std::int64_t minX = std::max<std::int64_t>(0, cx - r);
    const std::int64_t maxX = std::min<std::int64_t>(static_cast<std::int64_t>(width_) - 1, cx + r);
    const std::int64_t minY = std::max<std::int64_t>(0, cy - r);
    const std::int64_t maxY = std::min<std::int64_t>(static_cast<std::int64_t>(height_) - 1, cy + r);

    for (std::int64_t iy = minY; iy <= maxY; ++iy) {
        for (std::int64_t ix = minX; ix <= maxX; ++ix) {
            const std::int64_t dx = ix - cx;
            const std::int64_t dy = iy - cy;
            if (dx * dx + dy * dy <= r2) {
                RevealChunk(static_cast<std::uint32_t>(ix),
                            static_cast<std::uint32_t>(iy));
            }
        }
    }
}

std::uint32_t FogOfWarOverlay::GetRevealedCount() const {
    return revealedCount_;
}

std::uint32_t FogOfWarOverlay::GetTotalChunks() const {
    return width_ * height_;
}

float FogOfWarOverlay::GetRevealPercent() const {
    const auto total = GetTotalChunks();
    if (total == 0) return 0.0f;
    return (static_cast<float>(revealedCount_) / static_cast<float>(total)) *
           100.0f;
}

float FogOfWarOverlay::GetOverlayAlpha(std::uint32_t x,
                                       std::uint32_t y) const {
    if (!InBounds(x, y)) return kDefaultUnrevealedAlpha;
    const auto& c = chunks_[Index(x, y)];
    if (!c.isRevealed) return kDefaultUnrevealedAlpha;

    const float sinceFade = elapsed_ - c.revealTime;
    if (sinceFade >= kFadeDuration) return 0.0f;
    const float t = sinceFade / kFadeDuration;
    return kDefaultUnrevealedAlpha * (1.0f - t);
}

void FogOfWarOverlay::SetOverlayColor(std::uint32_t argb) {
    overlayColor_ = argb;
}

std::uint32_t FogOfWarOverlay::GetOverlayColor() const {
    return overlayColor_;
}

void FogOfWarOverlay::Update(float dt) { elapsed_ += dt; }

void FogOfWarOverlay::Reset() {
    width_  = 0;
    height_ = 0;
    chunks_.clear();
    overlayColor_  = 0xFF000000;
    elapsed_       = 0.0f;
    revealedCount_ = 0;
}

bool FogOfWarOverlay::InBounds(std::uint32_t x, std::uint32_t y) const {
    return x < width_ && y < height_;
}

std::size_t FogOfWarOverlay::Index(std::uint32_t x, std::uint32_t y) const {
    return static_cast<std::size_t>(y) * width_ + x;
}

}

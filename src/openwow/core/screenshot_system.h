
#pragma once

#include "openwow/core/screenshot_watermark.h"
#include "openwow/render/api/screenshot_readback.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openwow::core {

enum class ImageFormat : uint8_t {
    TGA  = 0,
    JPEG = 1,
    PNG  = 2,
};

enum class ScreenshotRequestDomain : uint8_t {
    None   = 0,
    GlueUi = 1,
    GameUi = 2,
};

struct ScreenshotCompletionResult {
    ScreenshotRequestDomain domain{ScreenshotRequestDomain::None};
    bool                    succeeded{false};
};

class ScreenshotSystem final : public openwow::render::api::ScreenshotReadbackTarget {
public:
    static ScreenshotSystem& Instance();

    bool CaptureScreenshot(
        ScreenshotRequestDomain domain = ScreenshotRequestDomain::None,
        std::optional<ImageFormat> request_format = std::nullopt);
    void FailCapture() override;
    [[nodiscard]] std::vector<ScreenshotCompletionResult>
    DrainCompletedRequests();

    [[nodiscard]] std::vector<ScreenshotCompletionResult>
    DrainCompletedRequestsForDomain(ScreenshotRequestDomain domain);

    void        SetFormat(ImageFormat fmt);
    [[nodiscard]] ImageFormat GetFormat() const;
    void        SetFormatCVarValue(std::string_view value);

    void        SetQuality(uint32_t quality);
    [[nodiscard]] uint32_t GetQuality() const;
    void        SetQualityCVarValue(std::string_view value);

    void        SetSavePath(const std::string& path);
    [[nodiscard]] std::string GetSavePath() const;

    [[nodiscard]] std::string GetLastScreenshotPath() const;
    [[nodiscard]] uint32_t    GetScreenshotCount() const;
    [[nodiscard]] bool        HasPendingCapture() const override;
    [[nodiscard]] bool        IsCapturing() const;

    void SetResolution(uint32_t w, uint32_t h);
    [[nodiscard]] std::pair<uint32_t, uint32_t> GetResolution() const;

    void CompleteCapture(std::vector<std::uint8_t> bgraData,
                         std::uint32_t w,
                         std::uint32_t h) override;

    [[nodiscard]] std::string GenerateFilename() const;

    void Reset();

private:
    ScreenshotSystem() = default;

    [[nodiscard]] std::string GenerateFilenameLocked() const;
    [[nodiscard]] ImageFormat ResolveFormatLocked() const;
    [[nodiscard]] bool ShouldApplyJpegWatermarkLocked() const;
    [[nodiscard]] std::string FormatExtensionLocked() const;

    mutable std::mutex mutex_;
    std::optional<ImageFormat> format_override_;
    std::string format_cvar_value_ = "jpeg";
    uint32_t    quality_   = 90;
    std::string savePath_  = "Screenshots/";
    std::string lastPath_;
    uint32_t    count_     = 0;
    bool        pending_   = false;
    bool        capturing_ = false;
    ScreenshotRequestDomain pending_domain_ = ScreenshotRequestDomain::None;
    std::optional<ImageFormat> pending_format_;
    std::array<std::uint8_t, kWotlkScreenshotWatermarkPayloadSize>
        pending_metadata_payload_{};
    std::vector<ScreenshotCompletionResult> completed_requests_;

    uint32_t resW_ = 1024;
    uint32_t resH_ = 768;

    std::vector<uint8_t> frameBuffer_;
    uint32_t fbWidth_  = 0;
    uint32_t fbHeight_ = 0;
};

}

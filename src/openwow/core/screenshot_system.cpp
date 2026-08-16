
#include "openwow/core/screenshot_system.h"

#include "openwow/core/decimal_parse.h"
#include "openwow/core/screenshot_jpeg_writer.h"
#include "openwow/core/screenshot_watermark.h"

#include "openwow/data/image/tga_loader.h"
#include "openwow/game/frame_timer.h"
#include "openwow/net/client_services.h"
#include "stb/stb_image_write.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>

namespace openwow::core {

namespace {

constexpr std::size_t kScreenshotFormatBufferSize = 16;

std::string CopyFormatCVarValue(std::string_view value) {

    if (value.size() >= kScreenshotFormatBufferSize) {
        value = value.substr(0, kScreenshotFormatBufferSize - 1);
    }

    return std::string(value);
}

std::vector<uint8_t> ConvertBgraToRgba(const std::vector<uint8_t>& bgraData) {
    std::vector<uint8_t> rgbaData(bgraData.size());
    for (std::size_t i = 0; i + 3 < bgraData.size(); i += 4) {
        rgbaData[i]     = bgraData[i + 2];
        rgbaData[i + 1] = bgraData[i + 1];
        rgbaData[i + 2] = bgraData[i];
        rgbaData[i + 3] = bgraData[i + 3];
    }
    return rgbaData;
}

std::array<std::uint8_t, kWotlkScreenshotWatermarkPayloadSize>
BuildRuntimeScreenshotWatermarkPayload() {
    const auto& client_services = openwow::net::ClientServices::Instance();
    const auto& account_name = client_services.GetAccountName();
    const auto& realm_address = client_services.GetSelectedRealmAddress();

    return BuildWotlkScreenshotWatermarkPayload(
        account_name,
        realm_address,
        openwow::game::FrameTimer::Get().GetServerTime());
}

ImageFormat ResolveScreenshotFormat(std::string_view stored_value) {

    if (stored_value == "png") {
        return ImageFormat::PNG;
    }
    if (stored_value == "tga" || stored_value == "targa") {
        return ImageFormat::TGA;
    }
    return ImageFormat::JPEG;
}

std::string_view FormatExtension(ImageFormat format) {
    switch (format) {
        case ImageFormat::TGA:
            return "tga";
        case ImageFormat::JPEG:
            return "jpeg";
        case ImageFormat::PNG:
            return "png";
    }
    return "jpg";
}

uint32_t ResolveJpegQualityFromCVar(std::string_view value) {
    uint32_t level = ParseSignedDecimalLikeSub76F0D0(value);
    if (level == 0u) {
        level = 1u;
    } else if (level > 10u) {
        level = 10u;
    }

    return 45u + (level * 11u) / 2u;
}

bool WriteWotlkScreenshotTga(const std::string& path,
                             const std::vector<std::uint8_t>& bgraData,
                             const std::uint32_t width,
                             const std::uint32_t height) {
    if (width == 0u || height == 0u) {
        return false;
    }

    if (width > std::numeric_limits<std::uint16_t>::max()
        || height > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }

    return openwow::data::WriteBgraScreenshotTga(
        path, bgraData, static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height));
}

}

ScreenshotSystem& ScreenshotSystem::Instance() {
    static ScreenshotSystem instance;
    return instance;
}

bool ScreenshotSystem::CaptureScreenshot(
    ScreenshotRequestDomain domain,
    std::optional<ImageFormat> request_format) {
    std::lock_guard lock(mutex_);

    if (pending_ || capturing_) return false;
    pending_ = true;
    pending_domain_ = domain;
    pending_format_ = request_format;
    pending_metadata_payload_ = BuildRuntimeScreenshotWatermarkPayload();
    return true;
}

void ScreenshotSystem::FailCapture() {
    std::lock_guard lock(mutex_);
    if (!pending_) {
        return;
    }

    pending_ = false;
    completed_requests_.push_back(
        ScreenshotCompletionResult{pending_domain_, false});
    pending_domain_ = ScreenshotRequestDomain::None;
    pending_format_.reset();
    pending_metadata_payload_.fill(0);
}

std::vector<ScreenshotCompletionResult>
ScreenshotSystem::DrainCompletedRequests() {
    std::lock_guard lock(mutex_);
    std::vector<ScreenshotCompletionResult> completed;
    completed.swap(completed_requests_);
    return completed;
}

std::vector<ScreenshotCompletionResult>
ScreenshotSystem::DrainCompletedRequestsForDomain(
    const ScreenshotRequestDomain domain) {
    std::lock_guard lock(mutex_);
    std::vector<ScreenshotCompletionResult> completed;
    std::vector<ScreenshotCompletionResult> retained;
    completed.reserve(completed_requests_.size());
    retained.reserve(completed_requests_.size());

    for (const ScreenshotCompletionResult& result : completed_requests_) {
        if (result.domain == domain) {
            completed.push_back(result);
        } else {
            retained.push_back(result);
        }
    }
    completed_requests_.swap(retained);
    return completed;
}

void ScreenshotSystem::SetFormat(ImageFormat fmt) {
    std::lock_guard lock(mutex_);
    format_override_ = fmt;
}

ImageFormat ScreenshotSystem::GetFormat() const {
    std::lock_guard lock(mutex_);
    return ResolveFormatLocked();
}

void ScreenshotSystem::SetFormatCVarValue(std::string_view value) {
    std::lock_guard lock(mutex_);
    format_override_.reset();
    format_cvar_value_ = CopyFormatCVarValue(value);
}

void ScreenshotSystem::SetQuality(uint32_t quality) {
    std::lock_guard lock(mutex_);
    quality_ = std::clamp(quality, 1u, 100u);
}

uint32_t ScreenshotSystem::GetQuality() const {
    std::lock_guard lock(mutex_);
    return quality_;
}

void ScreenshotSystem::SetQualityCVarValue(std::string_view value) {
    std::lock_guard lock(mutex_);
    quality_ = ResolveJpegQualityFromCVar(value);
}

void ScreenshotSystem::SetSavePath(const std::string& path) {
    std::lock_guard lock(mutex_);
    savePath_ = path;
}

std::string ScreenshotSystem::GetSavePath() const {
    std::lock_guard lock(mutex_);
    return savePath_;
}

std::string ScreenshotSystem::GetLastScreenshotPath() const {
    std::lock_guard lock(mutex_);
    return lastPath_;
}

uint32_t ScreenshotSystem::GetScreenshotCount() const {
    std::lock_guard lock(mutex_);
    return count_;
}

bool ScreenshotSystem::HasPendingCapture() const {
    std::lock_guard lock(mutex_);
    return pending_;
}

bool ScreenshotSystem::IsCapturing() const {
    std::lock_guard lock(mutex_);
    return capturing_;
}

void ScreenshotSystem::SetResolution(uint32_t w, uint32_t h) {
    std::lock_guard lock(mutex_);
    resW_ = w;
    resH_ = h;
}

std::pair<uint32_t, uint32_t> ScreenshotSystem::GetResolution() const {
    std::lock_guard lock(mutex_);
    return {resW_, resH_};
}

void ScreenshotSystem::CompleteCapture(std::vector<std::uint8_t> bgraData,
                                       const std::uint32_t w,
                                       const std::uint32_t h) {
    std::lock_guard lock(mutex_);
    if (!pending_) return;
    const ImageFormat format = pending_format_.value_or(ResolveFormatLocked());
    const bool apply_jpeg_watermark =
        format == ImageFormat::JPEG && quality_ < 100u;
    const ScreenshotRequestDomain active_domain = pending_domain_;
    const auto active_metadata_payload = pending_metadata_payload_;
    pending_   = false;
    capturing_ = true;
    pending_domain_ = ScreenshotRequestDomain::None;
    pending_format_.reset();
    pending_metadata_payload_.fill(0);

    frameBuffer_ = std::move(bgraData);
    fbWidth_     = w;
    fbHeight_    = h;

    const bool has_readback =
        !frameBuffer_.empty() && fbWidth_ > 0 && fbHeight_ > 0;

    if (has_readback) {
        std::error_code ec;
        std::filesystem::create_directories(savePath_, ec);

        auto filename_path = std::filesystem::path(GenerateFilenameLocked());
        filename_path.replace_extension(FormatExtension(format));
        const std::string filename = filename_path.string();
        std::string path = savePath_;
        if (!path.empty() && path.back() != '/' && path.back() != '\\') {
            path += '/';
        }
        path += filename;

        bool writeOk = false;

        if (format == ImageFormat::JPEG || format == ImageFormat::PNG) {
            if (format == ImageFormat::JPEG) {
                std::vector<std::uint8_t> output_frame = frameBuffer_;
                if (apply_jpeg_watermark) {
                    if (!ApplyWotlkScreenshotWatermark(
                            output_frame, fbWidth_, fbHeight_,
                            active_metadata_payload.data(),
                            active_metadata_payload.size())) {
                        output_frame = frameBuffer_;
                    }
                }

                writeOk = WriteWotlkScreenshotJpeg(path, output_frame,
                                                   fbWidth_, fbHeight_, quality_);
            } else {
                const std::vector<uint8_t> rgbaFrameBuffer = ConvertBgraToRgba(frameBuffer_);
                const int stride = static_cast<int>(fbWidth_ * 4);
                writeOk = stbi_write_png(path.c_str(),
                                         static_cast<int>(fbWidth_),
                                         static_cast<int>(fbHeight_),
                                         4, rgbaFrameBuffer.data(), stride) != 0;
            }
        } else {
            writeOk = WriteWotlkScreenshotTga(path, frameBuffer_, fbWidth_, fbHeight_);
        }

        if (writeOk) {
            lastPath_ = path;
            ++count_;
        }
    }

    capturing_ = false;

    completed_requests_.push_back(
        ScreenshotCompletionResult{active_domain, has_readback});
}

std::string ScreenshotSystem::GenerateFilename() const {
    std::lock_guard lock(mutex_);
    return GenerateFilenameLocked();
}

std::string ScreenshotSystem::GenerateFilenameLocked() const {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    std::ostringstream oss;
    oss << "ScreenShot_"
        << std::put_time(&tm_buf, "%m%d%y_%H%M%S")
        << '.' << FormatExtensionLocked();
    return oss.str();
}

ImageFormat ScreenshotSystem::ResolveFormatLocked() const {
    if (format_override_.has_value()) {
        return *format_override_;
    }
    return ResolveScreenshotFormat(format_cvar_value_);
}

bool ScreenshotSystem::ShouldApplyJpegWatermarkLocked() const {
    if (quality_ >= 100u) {
        return false;
    }

    if (format_override_.has_value()) {
        return *format_override_ == ImageFormat::JPEG;
    }

    return ResolveScreenshotFormat(format_cvar_value_) == ImageFormat::JPEG;
}

std::string ScreenshotSystem::FormatExtensionLocked() const {
    return std::string(FormatExtension(ResolveFormatLocked()));
}

void ScreenshotSystem::Reset() {
    std::lock_guard lock(mutex_);
    format_override_.reset();
    format_cvar_value_ = "jpeg";
    quality_   = 90;
    savePath_  = "Screenshots/";
    lastPath_.clear();
    count_     = 0;
    pending_   = false;
    capturing_ = false;
    pending_domain_ = ScreenshotRequestDomain::None;
    pending_format_.reset();
    pending_metadata_payload_.fill(0);
    completed_requests_.clear();
    resW_      = 1024;
    resH_      = 768;
    frameBuffer_.clear();
    fbWidth_   = 0;
    fbHeight_  = 0;
}

}

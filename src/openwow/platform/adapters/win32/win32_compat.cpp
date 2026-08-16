
#include "openwow/platform/adapters/win32/win32_compat.h"

#include "openwow/core/storm_utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  pragma comment(lib, "version.lib")
#elif defined(__APPLE__)
#  include <CoreFoundation/CoreFoundation.h>
#endif

namespace openwow::platform {

void StormEvent::Set() {
    std::lock_guard lock(mutex_);
    if (closed_) return;
    signaled_ = true;
    cv_.notify_all();
}

void StormEvent::Wait() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this]() { return signaled_ || closed_; });
    signaled_ = false;
}

bool StormEvent::WaitFor(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    bool result = cv_.wait_for(lock, timeout, [this]() { return signaled_ || closed_; });
    if (result && signaled_) {
        signaled_ = false;
        return true;
    }
    return false;
}

void StormEvent::Reset() {
    std::lock_guard lock(mutex_);
    signaled_ = false;
}

void StormEvent::Close() {

    std::lock_guard lock(mutex_);
    closed_ = true;
    signaled_ = true;
    cv_.notify_all();
}

void StormEventWithData::SetWithData(int32_t data) {
    data_ = data;
    Set();
}

void StormCriticalSection::Initialize() {
    initialized_ = true;

}

void StormCriticalSection::Enter() {
    mutex_.lock();
}

void StormCriticalSection::Leave() {
    mutex_.unlock();
}

void StormCriticalSection::Delete() {

    initialized_ = false;
}

bool StormSleep(uint32_t milliseconds) {
    if (milliseconds == 0) {
        std::this_thread::yield();
        return true;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    return true;
}

int SetupUnhandledExceptionFilter() {

    return 0;
}

namespace {
    thread_local uint32_t t_lastError = 0;
}

void SetPlatformLastError(uint32_t error) {
    t_lastError = error;
#if defined(_WIN32)
    SetLastError(error);
#endif
}

uint32_t GetPlatformLastError() {
#if defined(_WIN32)
    return GetLastError();
#else
    return t_lastError;
#endif
}

namespace {

int ParseDottedVersionComponents(
    const std::string_view text,
    std::array<std::int32_t, 4>& components) noexcept {
    std::size_t cursor = 0;
    int converted = 0;
    for (std::size_t component_index = 0;
         component_index < components.size(); ++component_index) {
        while (cursor < text.size()
               && std::isspace(static_cast<unsigned char>(text[cursor]))) {
            ++cursor;
        }

        bool negative = false;
        if (cursor < text.size()
            && (text[cursor] == '+' || text[cursor] == '-')) {
            negative = text[cursor] == '-';
            ++cursor;
        }
        if (cursor >= text.size() || text[cursor] < '0' || text[cursor] > '9') {
            return converted;
        }

        constexpr std::uint64_t kNegativeLimit =
            static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())
            + 1u;
        const std::uint64_t limit = negative
                                        ? kNegativeLimit
                                        : std::numeric_limits<std::int32_t>::max();
        std::uint64_t magnitude = 0;
        while (cursor < text.size()
               && text[cursor] >= '0' && text[cursor] <= '9') {
            const std::uint64_t digit =
                static_cast<unsigned char>(text[cursor]) - '0';
            if (magnitude <= (limit - digit) / 10u) {
                magnitude = magnitude * 10u + digit;
            } else {
                magnitude = limit;
            }
            ++cursor;
        }

        components[component_index] =
            negative
                ? magnitude == kNegativeLimit
                      ? std::numeric_limits<std::int32_t>::min()
                      : -static_cast<std::int32_t>(magnitude)
                : static_cast<std::int32_t>(magnitude);
        ++converted;

        if (component_index + 1u == components.size()) {
            return converted;
        }
        if (cursor >= text.size() || text[cursor] != '.') {
            return converted;
        }
        ++cursor;
    }
    return converted;
}

#if defined(__APPLE__)
std::optional<std::string> CopyBundleDictionaryString(
    const CFDictionaryRef dictionary,
    const CFStringRef key) {
    const void* const raw_value = CFDictionaryGetValue(dictionary, key);
    if (raw_value == nullptr
        || CFGetTypeID(raw_value) != CFStringGetTypeID()) {
        return std::nullopt;
    }

    const auto value = reinterpret_cast<CFStringRef>(raw_value);
    const CFIndex capacity =
        CFStringGetMaximumSizeForEncoding(
            CFStringGetLength(value), kCFStringEncodingUTF8)
        + 1;
    constexpr CFIndex kMaxBundleVersionStringBytes = 64 * 1024;
    if (capacity <= 0 || capacity > kMaxBundleVersionStringBytes) {
        return std::nullopt;
    }

    std::string utf8(static_cast<std::size_t>(capacity), '\0');
    if (!CFStringGetCString(value, utf8.data(), capacity,
                            kCFStringEncodingUTF8)) {
        return std::nullopt;
    }
    utf8.resize(std::char_traits<char>::length(utf8.c_str()));
    return utf8;
}
#endif

}

bool ParseBundleVersionStrings(
    const std::optional<std::string_view> blizzard_file_version,
    const std::optional<std::string_view> cf_bundle_version,
    BundleVersionInfo& out_version) noexcept {
    std::array<std::int32_t, 4> components{};
    bool success = false;
    if (blizzard_file_version
        && ParseDottedVersionComponents(*blizzard_file_version, components) == 4) {
        success = true;
    } else if (cf_bundle_version
               && ParseDottedVersionComponents(*cf_bundle_version, components) > 0) {
        success = true;
    }

    out_version = {
        .major = components[0],
        .minor = components[1],
        .patch = components[2],
        .build = components[3],
    };
    return success;
}

bool GetBundleVersionInfo(const std::string& bundle_path,
                          BundleVersionInfo& out_version) {
    out_version = {};
#if defined(__APPLE__)
    std::string normalized_path = bundle_path;
    std::replace(normalized_path.begin(), normalized_path.end(), '\\', '/');
    if (normalized_path.empty()
        || normalized_path.size()
               > static_cast<std::size_t>(std::numeric_limits<CFIndex>::max())) {
        return false;
    }

    const CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(normalized_path.data()),
        static_cast<CFIndex>(normalized_path.size()), true);
    if (url == nullptr) {
        return false;
    }
    const CFDictionaryRef dictionary = CFBundleCopyInfoDictionaryForURL(url);
    CFRelease(url);
    if (dictionary == nullptr) {
        return false;
    }

    const std::optional<std::string> blizzard_version =
        CopyBundleDictionaryString(dictionary, CFSTR("BlizzardFileVersion"));
    const std::optional<std::string> bundle_version =
        CopyBundleDictionaryString(dictionary, CFSTR("CFBundleVersion"));
    const bool success = ParseBundleVersionStrings(
        blizzard_version
            ? std::optional<std::string_view>(*blizzard_version)
            : std::nullopt,
        bundle_version
            ? std::optional<std::string_view>(*bundle_version)
            : std::nullopt,
        out_version);
    CFRelease(dictionary);
    return success;
#else
    (void)bundle_path;
    return false;
#endif
}

FixedVersionQuad DecodeFixedVersionQuad(std::uint32_t version_ms,
                                        std::uint32_t version_ls) {
    return FixedVersionQuad{
        .major = static_cast<std::uint16_t>(version_ms >> 16),
        .minor = static_cast<std::uint16_t>(version_ms & 0xFFFFu),
        .patch = static_cast<std::uint16_t>(version_ls >> 16),
        .build = static_cast<std::uint16_t>(version_ls & 0xFFFFu),
    };
}

FixedVersionQuad SelectFixedVersionQuad(std::uint32_t file_version_ms,
                                        std::uint32_t file_version_ls,
                                        std::uint32_t product_version_ms,
                                        std::uint32_t product_version_ls,
                                        int selector) {
    if (selector == 0) {
        return DecodeFixedVersionQuad(product_version_ms, product_version_ls);
    }
    if (selector == 1) {
        return DecodeFixedVersionQuad(file_version_ms, file_version_ls);
    }
    return {};
}

bool GetFixedVersionQuad(const std::string& filePath,
                         int selector,
                         FixedVersionQuad& outVersion) {
    outVersion = {};

#if defined(_WIN32)
    const std::string ansi_path =
        openwow::core::Utf8ToCurrentCodePageString(filePath.c_str());

    DWORD dummy = 0;
    const DWORD versionSize = GetFileVersionInfoSizeA(ansi_path.c_str(), &dummy);
    if (versionSize == 0) return false;

    std::vector<std::uint8_t> versionData(versionSize);
    if (!::GetFileVersionInfoA(ansi_path.c_str(), 0, versionSize,
                               versionData.data())) {
        return false;
    }

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT fileInfoLen = 0;
    if (!VerQueryValueA(versionData.data(), "\\",
                        reinterpret_cast<void**>(&fileInfo), &fileInfoLen) ||
        fileInfo == nullptr || fileInfoLen < 0x34) {
        return false;
    }

    outVersion = SelectFixedVersionQuad(
        fileInfo->dwFileVersionMS, fileInfo->dwFileVersionLS,
        fileInfo->dwProductVersionMS, fileInfo->dwProductVersionLS, selector);
    return true;
#else
    (void)filePath;
    (void)selector;
    return false;
#endif
}

bool ReadFileVersionInfo(const std::string& filePath, FileVersionInfo& outInfo) {

    outInfo = {};

#if defined(__APPLE__)
    BundleVersionInfo bundle_version;
    if (!GetBundleVersionInfo(filePath, bundle_version)) {
        return false;
    }
    outInfo.major = static_cast<std::uint16_t>(bundle_version.major);
    outInfo.minor = static_cast<std::uint16_t>(bundle_version.minor);
    outInfo.patch = static_cast<std::uint16_t>(bundle_version.patch);
    outInfo.build = static_cast<std::uint16_t>(bundle_version.build);
    return true;
#else

    FixedVersionQuad version;
    if (!GetFixedVersionQuad(filePath, 1, version)) {
        return false;
    }

    outInfo.major = version.major;
    outInfo.minor = version.minor;
    outInfo.patch = version.patch;
    outInfo.build = version.build;
    return true;
#endif
}

}

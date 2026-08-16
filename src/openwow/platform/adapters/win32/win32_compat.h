
#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::platform {

class StormEvent {
public:
    StormEvent() = default;
    ~StormEvent() = default;

    StormEvent(const StormEvent&) = delete;
    StormEvent& operator=(const StormEvent&) = delete;

    void Set();

    void Wait();

    bool WaitFor(std::chrono::milliseconds timeout);

    void Reset();

    void Close();

    [[nodiscard]] bool IsClosed() const { return closed_; }

private:
    std::mutex              mutex_;
    std::condition_variable cv_;
    bool                    signaled_ = false;
    bool                    closed_   = false;
};

class StormEventWithData : public StormEvent {
public:

    void SetWithData(int32_t data);

    [[nodiscard]] int32_t GetData() const { return data_; }

private:
    int32_t data_ = 0;
};

class StormCriticalSection {
public:

    void Initialize();

    void Enter();

    void Leave();

    void Delete();

    [[nodiscard]] bool IsInitialized() const { return initialized_; }

private:
    std::recursive_mutex mutex_;
    bool initialized_ = false;
};

bool StormSleep(uint32_t milliseconds);

int SetupUnhandledExceptionFilter();

inline constexpr uint32_t kErrorCallNotImplemented = 0x78;
inline constexpr uint32_t kErrorProcNotFound       = 0x7F;

void SetPlatformLastError(uint32_t error);
uint32_t GetPlatformLastError();

struct FixedVersionQuad {
    uint16_t major = 0;
    uint16_t minor = 0;
    uint16_t patch = 0;
    uint16_t build = 0;
};

[[nodiscard]] FixedVersionQuad DecodeFixedVersionQuad(std::uint32_t version_ms,
                                                      std::uint32_t version_ls);

[[nodiscard]] FixedVersionQuad SelectFixedVersionQuad(
    std::uint32_t file_version_ms,
    std::uint32_t file_version_ls,
    std::uint32_t product_version_ms,
    std::uint32_t product_version_ls,
    int selector);

bool GetFixedVersionQuad(const std::string& filePath,
                         int selector,
                         FixedVersionQuad& outVersion);

struct FileVersionInfo {
    uint16_t major = 0;
    uint16_t minor = 0;
    uint16_t patch = 0;
    uint16_t build = 0;
    std::string productName;
    std::string fileDescription;
};

struct BundleVersionInfo {
    std::int32_t major = 0;
    std::int32_t minor = 0;
    std::int32_t patch = 0;
    std::int32_t build = 0;
};

[[nodiscard]] bool ParseBundleVersionStrings(
    std::optional<std::string_view> blizzard_file_version,
    std::optional<std::string_view> cf_bundle_version,
    BundleVersionInfo& out_version) noexcept;

[[nodiscard]] bool GetBundleVersionInfo(
    const std::string& bundle_path,
    BundleVersionInfo& out_version);

bool ReadFileVersionInfo(const std::string& filePath, FileVersionInfo& outInfo);

}


#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace openwow::core {

enum class CpuVendor : uint32_t {
    Unknown         = 0,
    Intel           = 1,
    AMD             = 2,
    DetectionFailed = 4,
};

enum CpuFeatures : uint32_t {
    kCpuFeature_RDTSC         = 0x01,
    kCpuFeature_MMX           = 0x02,
    kCpuFeature_SSE           = 0x04,
    kCpuFeature_3DNow         = 0x08,
    kCpuFeature_SSE2          = 0x10,
    kCpuFeature_HyperThread   = 0x40,
};

struct OsSystemInfo {
    uint32_t  processorCount       = 0;
    uint32_t  socketCount          = 1;
    uint32_t  logicalCpuPerPackage = 1;
    CpuVendor cpuVendor            = CpuVendor::DetectionFailed;
    uint32_t  cpuFeatures          = 0;
};

enum class StormCpuIdQueryLevel : uint32_t {
    Unavailable = 0,
    Standard    = 1,
    Extended    = 2,
};

struct StormCpuIdSnapshot {
    std::array<char, 12> vendorId{};
    uint32_t maxStandardLeaf = 0;
    uint32_t leaf1Ebx = 0;
    uint32_t leaf1Edx = 0;
    uint32_t leaf4Eax = 0;
    uint32_t maxExtendedLeaf = 0;
    uint32_t ext1Ecx = 0;
    uint32_t ext1Edx = 0;
    uint32_t ext8Ecx = 0;
    std::array<char, 48> brandString{};
};
static_assert(sizeof(StormCpuIdSnapshot) == 0x5C);

[[nodiscard]] StormCpuIdQueryLevel QueryStormCpuIdSnapshot(StormCpuIdSnapshot& snapshot);

[[nodiscard]] OsSystemInfo DetectStormSystemInfo(
    uint32_t processor_count,
    StormCpuIdQueryLevel query_level,
    const StormCpuIdSnapshot* snapshot);

void LogCpuDetectionResults(
    bool logging_enabled,
    const OsSystemInfo& info,
    const StormCpuIdSnapshot* snapshot);

class OsSystemInfoDetector {
public:
    static OsSystemInfoDetector& Instance();

    uint32_t Init();

    [[nodiscard]] const OsSystemInfo& GetInfo() const { return info_; }

    [[nodiscard]] bool IsInitialized() const { return initialized_.load(std::memory_order_acquire); }

private:
    OsSystemInfoDetector() = default;

    void Detect();

    OsSystemInfo         info_;
    std::atomic<bool>    initialized_{false};
    std::atomic<int32_t> initLock_{0};
};

}


#include "openwow/platform/system/os_system_info.h"

#include "openwow/core/legacy_buffered_log_file.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <string_view>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <intrin.h>
#  include <windows.h>
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#  include <unistd.h>
#  if defined(__x86_64__) || defined(__i386__)
#    include <cpuid.h>
#  endif
#elif defined(__linux__)
#  include <unistd.h>
#  if defined(__x86_64__) || defined(__i386__)
#    include <cpuid.h>
#  endif
#endif

namespace openwow::core {

namespace {

constexpr std::uint32_t kLeaf1RtdscBit = 0x00000010u;
constexpr std::uint32_t kLeaf1MmxBit = 0x00800000u;
constexpr std::uint32_t kLeaf1SseBit = 0x02000000u;
constexpr std::uint32_t kLeaf1Sse2Bit = 0x04000000u;
constexpr std::uint32_t kExt1ThreeDNowBit = 0x80000000u;

struct CpuIdRegisters {
    std::uint32_t eax = 0;
    std::uint32_t ebx = 0;
    std::uint32_t ecx = 0;
    std::uint32_t edx = 0;
};

void CopyCpuIdStringRegisters(char* destination, const CpuIdRegisters& registers) {
    std::memcpy(destination + 0, &registers.eax, sizeof(registers.eax));
    std::memcpy(destination + 4, &registers.ebx, sizeof(registers.ebx));
    std::memcpy(destination + 8, &registers.ecx, sizeof(registers.ecx));
    std::memcpy(destination + 12, &registers.edx, sizeof(registers.edx));
}

std::uint32_t DetectProcessorCount() {
#if defined(_WIN32)
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    const auto processor_count = static_cast<std::uint32_t>(system_info.dwNumberOfProcessors);
#elif defined(__linux__)
    const long processor_count = sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(__APPLE__)
    int processor_count = 0;
    size_t length = sizeof(processor_count);
    if (sysctlbyname("hw.logicalcpu", &processor_count, &length, nullptr, 0) != 0) {
        processor_count = 0;
    }
#else
    const int processor_count = 0;
#endif

    if (processor_count <= 0) {
        return 1;
    }
    return static_cast<std::uint32_t>(processor_count);
}

bool CanExecuteCpuId() {
#if defined(_M_X64) || defined(__x86_64__)
    return true;
#elif defined(_M_IX86)
    const unsigned long original_flags = __readeflags();
    __writeeflags(original_flags ^ 0x200000UL);
    const unsigned long toggled_flags = __readeflags();
    __writeeflags(original_flags);
    return original_flags != toggled_flags;
#elif defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
    std::uint32_t original_flags = 0;
    std::uint32_t toggled_flags = 0;
    asm volatile("pushfl\n\t"
                 "popl %0"
                 : "=r"(original_flags));
    const std::uint32_t patched_flags = original_flags ^ 0x00200000u;
    asm volatile("pushl %0\n\t"
                 "popfl"
                 :
                 : "r"(patched_flags)
                 : "cc");
    asm volatile("pushfl\n\t"
                 "popl %0"
                 : "=r"(toggled_flags));
    asm volatile("pushl %0\n\t"
                 "popfl"
                 :
                 : "r"(original_flags)
                 : "cc");
    return original_flags != toggled_flags;
#else
    return false;
#endif
}

bool QueryCpuId(std::uint32_t leaf, CpuIdRegisters& registers) {
#if defined(_WIN32) && (defined(_M_X64) || defined(_M_IX86))
    int values[4] = {};
    __cpuidex(values, static_cast<int>(leaf), 0);
    registers.eax = static_cast<std::uint32_t>(values[0]);
    registers.ebx = static_cast<std::uint32_t>(values[1]);
    registers.ecx = static_cast<std::uint32_t>(values[2]);
    registers.edx = static_cast<std::uint32_t>(values[3]);
    return true;
#elif (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
    __cpuid_count(
        leaf,
        0,
        registers.eax,
        registers.ebx,
        registers.ecx,
        registers.edx);
    return true;
#else
    (void)leaf;
    registers = {};
    return false;
#endif
}

}

OsSystemInfoDetector& OsSystemInfoDetector::Instance() {
    static OsSystemInfoDetector inst;
    return inst;
}

StormCpuIdQueryLevel QueryStormCpuIdSnapshot(StormCpuIdSnapshot& snapshot) {
    snapshot = {};

    if (!CanExecuteCpuId()) {
        return StormCpuIdQueryLevel::Unavailable;
    }

    CpuIdRegisters registers{};
    if (!QueryCpuId(0, registers) || registers.eax == 0) {
        return StormCpuIdQueryLevel::Unavailable;
    }

    snapshot.maxStandardLeaf = registers.eax;
    std::memcpy(snapshot.vendorId.data() + 0, &registers.ebx, sizeof(registers.ebx));
    std::memcpy(snapshot.vendorId.data() + 4, &registers.edx, sizeof(registers.edx));
    std::memcpy(snapshot.vendorId.data() + 8, &registers.ecx, sizeof(registers.ecx));

    auto query_level = StormCpuIdQueryLevel::Standard;

    if (snapshot.maxStandardLeaf >= 4) {
        if (!QueryCpuId(4, registers)) {
            snapshot = {};
            return StormCpuIdQueryLevel::Unavailable;
        }
        snapshot.leaf4Eax = registers.eax;
    }

    if (!QueryCpuId(1, registers)) {
        snapshot = {};
        return StormCpuIdQueryLevel::Unavailable;
    }
    snapshot.leaf1Ebx = registers.ebx;
    snapshot.leaf1Edx = registers.edx;

    if (!QueryCpuId(0x80000000u, registers) || registers.eax <= 0x80000000u) {
        return query_level;
    }

    query_level = StormCpuIdQueryLevel::Extended;
    snapshot.maxExtendedLeaf = registers.eax;

    if (snapshot.maxExtendedLeaf >= 0x80000008u) {
        if (!QueryCpuId(0x80000008u, registers)) {
            snapshot = {};
            return StormCpuIdQueryLevel::Unavailable;
        }
        snapshot.ext8Ecx = registers.ecx;
    }

    if (snapshot.maxExtendedLeaf >= 0x80000002u) {
        if (!QueryCpuId(0x80000002u, registers)) {
            snapshot = {};
            return StormCpuIdQueryLevel::Unavailable;
        }
        CopyCpuIdStringRegisters(snapshot.brandString.data(), registers);
    }

    if (snapshot.maxExtendedLeaf >= 0x80000003u) {
        if (!QueryCpuId(0x80000003u, registers)) {
            snapshot = {};
            return StormCpuIdQueryLevel::Unavailable;
        }
        CopyCpuIdStringRegisters(snapshot.brandString.data() + 16, registers);
    }

    if (snapshot.maxExtendedLeaf >= 0x80000004u) {
        if (!QueryCpuId(0x80000004u, registers)) {
            snapshot = {};
            return StormCpuIdQueryLevel::Unavailable;
        }
        CopyCpuIdStringRegisters(snapshot.brandString.data() + 32, registers);
    }

    if (!QueryCpuId(0x80000001u, registers)) {
        snapshot = {};
        return StormCpuIdQueryLevel::Unavailable;
    }
    snapshot.ext1Ecx = registers.ecx;
    snapshot.ext1Edx = registers.edx;
    return query_level;
}

OsSystemInfo DetectStormSystemInfo(
    std::uint32_t processor_count,
    const StormCpuIdQueryLevel query_level,
    const StormCpuIdSnapshot* snapshot) {
    OsSystemInfo info{};
    info.processorCount = processor_count == 0 ? 1 : processor_count;
    info.logicalCpuPerPackage = 1;

    if (query_level == StormCpuIdQueryLevel::Unavailable || snapshot == nullptr) {
        return info;
    }

    std::uint32_t features = 0;
    if ((snapshot->leaf1Edx & kLeaf1RtdscBit) != 0) {
        features |= kCpuFeature_RDTSC;
    }
    if ((snapshot->leaf1Edx & kLeaf1MmxBit) != 0) {
        features |= kCpuFeature_MMX;
    }
    if ((snapshot->leaf1Edx & kLeaf1SseBit) != 0) {
        features |= kCpuFeature_SSE;
    }
    if (query_level == StormCpuIdQueryLevel::Extended
        && (snapshot->ext1Edx & kExt1ThreeDNowBit) != 0) {
        features |= kCpuFeature_3DNow;
    }

    const std::string_view vendor(snapshot->vendorId.data(), snapshot->vendorId.size());
    if (vendor == "GenuineIntel") {
        info.cpuVendor = CpuVendor::Intel;
        if ((snapshot->leaf1Edx & kLeaf1Sse2Bit) != 0) {
            features |= kCpuFeature_SSE2;
        }
        if (snapshot->maxStandardLeaf >= 4) {
            const std::uint32_t logical_cpus_minus_one = snapshot->leaf4Eax >> 26;
            if (logical_cpus_minus_one != 0) {
                features |= kCpuFeature_HyperThread;
                info.logicalCpuPerPackage = logical_cpus_minus_one + 1;
            }
        }
    } else if (vendor == "AuthenticAMD") {
        info.cpuVendor = CpuVendor::AMD;
        if ((snapshot->leaf1Edx & kLeaf1Sse2Bit) != 0) {
            features |= kCpuFeature_SSE2;
        }
        const std::uint8_t logical_cpus_minus_one =
            static_cast<std::uint8_t>(snapshot->ext8Ecx & 0xFFu);
        if (logical_cpus_minus_one != 0) {
            features |= kCpuFeature_HyperThread;
            info.logicalCpuPerPackage =
                static_cast<std::uint32_t>(logical_cpus_minus_one) + 1;
        }
    } else {
        info.cpuVendor = CpuVendor::Unknown;
    }

    info.cpuFeatures = features;
    return info;
}

void LogCpuDetectionResults(
    const bool logging_enabled,
    const OsSystemInfo& info,
    const StormCpuIdSnapshot* snapshot) {
    if (!logging_enabled) {
        return;
    }

    LegacyBufferedLogFile log("Logs\\cpu.log", LegacyBufferedLogOpenMode::kTruncate);
    if (!log.IsOpen()) {
        return;
    }

    if (info.cpuVendor == CpuVendor::DetectionFailed) {
        log.AppendLine("UNABLE TO IDENTIFY CPU");
        return;
    }

    std::array<char, 256> line_buf{};

    std::snprintf(line_buf.data(), line_buf.size(), "vendor: %d",
                  static_cast<int>(info.cpuVendor));
    log.AppendLine(line_buf.data());

    std::snprintf(line_buf.data(), line_buf.size(), "features: %08X",
                  info.cpuFeatures & 0x7FFFFFFFu);
    log.AppendLine(line_buf.data());

    std::snprintf(line_buf.data(), line_buf.size(), "sockets: %d",
                  info.socketCount);
    log.AppendLine(line_buf.data());

    std::snprintf(line_buf.data(), line_buf.size(), "cores: %d",
                  info.logicalCpuPerPackage);
    log.AppendLine(line_buf.data());

    std::snprintf(line_buf.data(), line_buf.size(), "processors: %d",
                  info.processorCount);
    log.AppendLine(line_buf.data());

    if (snapshot != nullptr) {
        char vendor_cstr[13]{};
        std::memcpy(vendor_cstr, snapshot->vendorId.data(),
                    std::min<std::size_t>(snapshot->vendorId.size(), 12));
        vendor_cstr[12] = '\0';
        std::snprintf(line_buf.data(), line_buf.size(),
                      "vendor id string= %s", vendor_cstr);
        log.AppendLine(line_buf.data());

        std::snprintf(line_buf.data(), line_buf.size(),
                      "standard (%d): 1b=%08X 1d=%08x 4a=%08X",
                      snapshot->maxStandardLeaf,
                      snapshot->leaf1Ebx,
                      snapshot->leaf1Edx,
                      snapshot->leaf4Eax);
        log.AppendLine(line_buf.data());

        std::snprintf(line_buf.data(), line_buf.size(),
                      "extended (%d): 1c=%08X 1d=%08x 8c=%08X",
                      snapshot->maxExtendedLeaf & 0x7FFFFFFFu,
                      snapshot->ext1Ecx,
                      snapshot->ext1Edx,
                      snapshot->ext8Ecx);
        log.AppendLine(line_buf.data());

        const char* brand = snapshot->brandString.data();
        const char* brand_end = brand + snapshot->brandString.size();
        while (brand < brand_end && *brand == ' ') {
            ++brand;
        }
        std::snprintf(line_buf.data(), line_buf.size(),
                      "processor brand string= %s", brand);
        log.AppendLine(line_buf.data());
    }

}

uint32_t OsSystemInfoDetector::Init() {
    if (initialized_.load(std::memory_order_acquire)) {
        return info_.cpuFeatures;
    }

    int32_t prev = initLock_.fetch_add(1, std::memory_order_acq_rel);
    if (prev == 0) {

        Detect();
        initialized_.store(true, std::memory_order_release);
    } else {

        while (!initialized_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    return info_.cpuFeatures;
}

void OsSystemInfoDetector::Detect() {
    StormCpuIdSnapshot cpu_id_snapshot{};
    const auto processor_count = DetectProcessorCount();
    const auto query_level = QueryStormCpuIdSnapshot(cpu_id_snapshot);
#if defined(__aarch64__) || defined(_M_ARM64)
    if (query_level == StormCpuIdQueryLevel::Unavailable) {

        info_ = OsSystemInfo{};
        info_.processorCount = processor_count == 0 ? 1 : processor_count;
        info_.logicalCpuPerPackage = 1;
        info_.cpuVendor = CpuVendor::Unknown;
        info_.cpuFeatures = kCpuFeature_SSE | kCpuFeature_SSE2;
        return;
    }
#endif
    const StormCpuIdSnapshot* snapshot =
        query_level == StormCpuIdQueryLevel::Unavailable ? nullptr : &cpu_id_snapshot;
    info_ = DetectStormSystemInfo(processor_count, query_level, snapshot);
}

}

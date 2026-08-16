#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::core {

enum class PlatformOS : uint8_t {
    Windows = 0,
    MacOS   = 1,
    Linux   = 2,
    Unknown = 3
};

enum class PlatformArch : uint8_t {
    x86_64  = 0,
    ARM64   = 1,
    x86     = 2,
    ARM     = 3,
    Unknown = 4
};

struct PlatformInfo {
    PlatformOS          os                  = PlatformOS::Unknown;
    PlatformArch        arch                = PlatformArch::Unknown;
    std::string         osVersion;
    std::string         cpuName;
    uint32_t            cpuCoreCount        = 0;
    uint64_t            totalRAM_MB         = 0;
    std::string         gpuName;
    uint64_t            gpuVRAM_MB          = 0;
};

struct DisplayAdapterIdentity {
    std::uint16_t vendor_id = 0;
    std::uint16_t device_id = 0;
    std::uint32_t driver_hi = 0;
    std::uint32_t driver_lo = 0;
    std::uint64_t video_memory_bytes = 0;
};

[[nodiscard]] std::uint16_t ParseFixedWidthHexWord(std::string_view text);

[[nodiscard]] std::uint32_t PackMacDisplayDriverVersion(
    std::string_view source_version);

[[nodiscard]] bool TryParseWindowsDisplayDevicePciIdentity(
    std::string_view device_id,
    DisplayAdapterIdentity& out);

class PlatformLayer {
public:
    PlatformLayer() = delete;

    static PlatformInfo     DetectPlatform();

    static bool             TryGetPrimaryDisplayAdapterIdentity(
        DisplayAdapterIdentity& out);

    static PlatformOS       GetOS();

    static PlatformArch     GetArch();

    static std::string      GetOSName();

    static std::string      GetArchName();

    static std::string      GetUserDataPath();

    static std::string      GetExecutablePath();

    static uint32_t         GetCPUCoreCount();

    static uint32_t         GetPageSize();

    static bool             IsDebuggerAttached();

    static void             SetPrimaryDisplayAdapterIdentityOverrideForTests(
        const std::optional<DisplayAdapterIdentity>& identity);
    static void             ClearPrimaryDisplayAdapterIdentityOverrideForTests();
};

}

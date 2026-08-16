
#include "openwow/platform/adapters/sdl/platform_layer.h"

#include "openwow/platform/process/os_platform.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <d3d9.h>
#  include <windows.h>
#elif defined(__APPLE__)
#  include <CoreFoundation/CoreFoundation.h>
#  include <IOKit/IOKitLib.h>
#  include <mach-o/dyld.h>
#  include <sys/sysctl.h>
#  include <sys/types.h>
#  include <unistd.h>
#elif defined(__linux__)
#  include <filesystem>
#  include <fstream>
#  include <sys/sysinfo.h>
#  include <unistd.h>
#endif

namespace openwow::core {

namespace {

std::mutex s_displayAdapterOverrideMutex;
std::optional<DisplayAdapterIdentity> s_displayAdapterOverride;

#if defined(__linux__)

std::string TrimAsciiWhitespace(std::string text) {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front()))) {
        text.erase(text.begin());
    }
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    return text;
}

std::optional<std::string> ReadSmallTextFile(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream.is_open()) {
        return std::nullopt;
    }

    std::string text;
    std::getline(stream, text);
    return TrimAsciiWhitespace(text);
}

bool IsBaseDrmCardNode(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    if (name.size() <= 4 || name.rfind("card", 0) != 0) {
        return false;
    }

    return std::all_of(name.begin() + 4, name.end(),
                       [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

bool TryParseLinuxSysfsPciWord(const std::filesystem::path& path,
                               std::uint16_t& out) {
    const auto text = ReadSmallTextFile(path);
    if (!text.has_value()) {
        return false;
    }

    std::string_view value = *text;
    if (value.size() >= 2 && value[0] == '0' &&
        (value[1] == 'x' || value[1] == 'X')) {
        value.remove_prefix(2);
    }
    if (value.size() < 4) {
        return false;
    }

    out = ParseFixedWidthHexWord(value.substr(value.size() - 4, 4));
    return out != 0;
}

bool TryReadLinuxDisplayAdapterIdentity(const std::filesystem::path& device_path,
                                        DisplayAdapterIdentity& out) {
    DisplayAdapterIdentity candidate;
    if (!TryParseLinuxSysfsPciWord(device_path / "vendor",
                                   candidate.vendor_id) ||
        !TryParseLinuxSysfsPciWord(device_path / "device",
                                   candidate.device_id)) {
        return false;
    }

    out = candidate;
    return true;
}
#endif

#if defined(__APPLE__)
class ScopedIoObject final {
public:
    ScopedIoObject() = default;
    explicit ScopedIoObject(io_object_t object) : object_(object) {}
    ~ScopedIoObject() {
        if (object_ != IO_OBJECT_NULL) {
            IOObjectRelease(object_);
        }
    }

    ScopedIoObject(const ScopedIoObject&) = delete;
    ScopedIoObject& operator=(const ScopedIoObject&) = delete;

    ScopedIoObject(ScopedIoObject&& other) noexcept
        : object_(other.Release()) {}
    ScopedIoObject& operator=(ScopedIoObject&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    [[nodiscard]] io_object_t Get() const { return object_; }
    [[nodiscard]] io_object_t* Put() {
        Reset();
        return &object_;
    }
    [[nodiscard]] io_object_t Release() {
        const io_object_t object = object_;
        object_ = IO_OBJECT_NULL;
        return object;
    }
    void Reset(io_object_t object = IO_OBJECT_NULL) {
        if (object_ != IO_OBJECT_NULL) {
            IOObjectRelease(object_);
        }
        object_ = object;
    }

private:
    io_object_t object_ = IO_OBJECT_NULL;
};

template <typename T>
class ScopedCfRef final {
public:
    ScopedCfRef() = default;
    explicit ScopedCfRef(T value) : value_(value) {}
    ~ScopedCfRef() {
        if (value_ != nullptr) {
            CFRelease(value_);
        }
    }

    ScopedCfRef(const ScopedCfRef&) = delete;
    ScopedCfRef& operator=(const ScopedCfRef&) = delete;

    ScopedCfRef(ScopedCfRef&& other) noexcept : value_(other.Release()) {}
    ScopedCfRef& operator=(ScopedCfRef&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    [[nodiscard]] T Get() const { return value_; }
    [[nodiscard]] T Release() {
        const T value = value_;
        value_ = nullptr;
        return value;
    }
    void Reset(T value = nullptr) {
        if (value_ != nullptr) {
            CFRelease(value_);
        }
        value_ = value;
    }

private:
    T value_ = nullptr;
};

ScopedCfRef<CFTypeRef> CopyRegistryProperty(io_registry_entry_t entry,
                                            CFStringRef name) {
    return ScopedCfRef<CFTypeRef>(IORegistryEntryCreateCFProperty(
        entry, name, kCFAllocatorDefault, 0));
}

bool TryReadPciWord(CFTypeRef property, std::uint16_t& out) {
    if (property == nullptr) {
        return false;
    }

    std::uint32_t value = 0;
    if (CFGetTypeID(property) == CFDataGetTypeID()) {
        const auto data = static_cast<CFDataRef>(property);
        if (CFDataGetLength(data) < 2) {
            return false;
        }
        UInt8 bytes[2]{};
        CFDataGetBytes(data, CFRangeMake(0, 2), bytes);
        value = static_cast<std::uint32_t>(bytes[0]) |
                (static_cast<std::uint32_t>(bytes[1]) << 8u);
    } else if (CFGetTypeID(property) == CFNumberGetTypeID()) {
        if (!CFNumberGetValue(static_cast<CFNumberRef>(property),
                              kCFNumberSInt32Type, &value)) {
            return false;
        }
    } else {
        return false;
    }

    out = static_cast<std::uint16_t>(value);
    return out != 0;
}

bool TryReadUnsignedRegistryValue(CFTypeRef property, std::uint64_t& out) {
    if (property == nullptr) {
        return false;
    }

    std::uint64_t value = 0;
    if (CFGetTypeID(property) == CFDataGetTypeID()) {
        const auto data = static_cast<CFDataRef>(property);
        const CFIndex length = std::min<CFIndex>(CFDataGetLength(data), 8);
        if (length <= 0) {
            return false;
        }
        UInt8 bytes[8]{};
        CFDataGetBytes(data, CFRangeMake(0, length), bytes);
        for (CFIndex index = 0; index < length; ++index) {
            value |= static_cast<std::uint64_t>(bytes[index])
                     << (static_cast<unsigned int>(index) * 8u);
        }
    } else if (CFGetTypeID(property) == CFNumberGetTypeID()) {
        if (!CFNumberGetValue(static_cast<CFNumberRef>(property),
                              kCFNumberSInt64Type, &value)) {
            return false;
        }
    } else {
        return false;
    }

    out = value;
    return true;
}

std::uint64_t ReadMacVideoMemoryBytes(io_registry_entry_t accelerator,
                                      io_registry_entry_t provider) {
    for (const io_registry_entry_t entry : {accelerator, provider}) {
        const auto total_bytes = CopyRegistryProperty(
            entry, CFSTR("VRAM,totalsize"));
        std::uint64_t value = 0;
        if (TryReadUnsignedRegistryValue(total_bytes.Get(), value) && value != 0) {
            return value;
        }

        const auto total_mb = CopyRegistryProperty(entry, CFSTR("VRAM,totalMB"));
        if (TryReadUnsignedRegistryValue(total_mb.Get(), value) && value != 0) {
            return value * 1024u * 1024u;
        }
    }
    return 0;
}

std::optional<std::string> TryCopyUtf8String(CFTypeRef property) {
    if (property == nullptr || CFGetTypeID(property) != CFStringGetTypeID()) {
        return std::nullopt;
    }

    const auto string = static_cast<CFStringRef>(property);
    const CFIndex length = CFStringGetLength(string);
    const CFIndex capacity = CFStringGetMaximumSizeForEncoding(
        length, kCFStringEncodingUTF8) + 1;
    if (capacity <= 1) {
        return std::string{};
    }

    std::string utf8(static_cast<std::size_t>(capacity), '\0');
    if (!CFStringGetCString(string, utf8.data(), capacity,
                            kCFStringEncodingUTF8)) {
        return std::nullopt;
    }
    utf8.resize(std::strlen(utf8.c_str()));
    return utf8;
}

bool TryReadMacDisplayAdapterIdentity(DisplayAdapterIdentity& out) {
    ScopedIoObject iterator;
    CFMutableDictionaryRef matching = IOServiceMatching("IOAccelerator");
    if (matching == nullptr ||
        IOServiceGetMatchingServices(kIOMainPortDefault, matching,
                                     iterator.Put()) != KERN_SUCCESS) {
        return false;
    }

    while (const io_object_t next = IOIteratorNext(iterator.Get())) {
        ScopedIoObject accelerator(next);
        ScopedIoObject provider;
        if (IORegistryEntryGetParentEntry(accelerator.Get(), kIOServicePlane,
                                          provider.Put()) != KERN_SUCCESS) {
            continue;
        }

        const auto vendor = CopyRegistryProperty(provider.Get(), CFSTR("vendor-id"));
        const auto device = CopyRegistryProperty(provider.Get(), CFSTR("device-id"));
        DisplayAdapterIdentity candidate;
        if (!TryReadPciWord(vendor.Get(), candidate.vendor_id) ||
            !TryReadPciWord(device.Get(), candidate.device_id)) {
            continue;
        }

        const auto source_version = CopyRegistryProperty(
            accelerator.Get(), CFSTR("IOSourceVersion"));
        if (const auto text = TryCopyUtf8String(source_version.Get())) {
            candidate.driver_lo = PackMacDisplayDriverVersion(*text);
        }
        candidate.video_memory_bytes =
            ReadMacVideoMemoryBytes(accelerator.Get(), provider.Get());

        out = candidate;
        return true;
    }
    return false;
}
#endif

#if defined(_WIN32)
bool TryGetD3d9FallbackDisplayAdapterIdentity(DisplayAdapterIdentity& out) {
    using Direct3DCreate9Fn = IDirect3D9* (WINAPI*)(UINT);

    HMODULE d3d9 = LoadLibraryA("d3d9.dll");
    if (d3d9 == nullptr) {
        return false;
    }

    const auto create_d3d9 = reinterpret_cast<Direct3DCreate9Fn>(
        GetProcAddress(d3d9, "Direct3DCreate9"));
    if (create_d3d9 == nullptr) {
        FreeLibrary(d3d9);
        return false;
    }

    bool success = false;
    if (IDirect3D9* const d3d = create_d3d9(D3D_SDK_VERSION)) {
        D3DADAPTER_IDENTIFIER9 identifier{};
        if (SUCCEEDED(d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0,
                                                &identifier))) {
            out.vendor_id = static_cast<std::uint16_t>(identifier.VendorId);
            out.device_id = static_cast<std::uint16_t>(identifier.DeviceId);
            out.driver_hi = identifier.DriverVersion.HighPart;
            out.driver_lo = identifier.DriverVersion.LowPart;
            success = out.vendor_id != 0 && out.device_id != 0;
        }
        d3d->Release();
    }

    FreeLibrary(d3d9);
    return success;
}
#endif

}

std::uint16_t ParseFixedWidthHexWord(std::string_view text) {
    std::uint32_t value = 0;
    for (const char ch : text) {
        value <<= 4u;

        const auto uch = static_cast<unsigned char>(ch);
        if (!std::isxdigit(uch)) {
            continue;
        }

        if (std::isdigit(uch)) {
            value += static_cast<std::uint32_t>(uch - '0');
        } else {
            value += static_cast<std::uint32_t>(
                std::toupper(uch) - 'A' + 10);
        }
    }

    return static_cast<std::uint16_t>(value);
}

std::uint32_t PackMacDisplayDriverVersion(std::string_view source_version) {
    const std::string terminated(source_version);
    int component[4]{};
    const int count = std::sscanf(terminated.c_str(), "%d.%d.%d.%d",
                                  &component[0], &component[1],
                                  &component[2], &component[3]);
    if (count < 4) {
        component[0] = 1;
        component[1] = 0;
        component[2] = 0;
        component[3] = 0;
        std::sscanf(terminated.c_str(), "%d.%d.%d", &component[1],
                    &component[2], &component[3]);
    }

    for (int& value : component) {
        value = std::clamp(value, 0, 0x100);
    }

    return static_cast<std::uint32_t>(component[0] * 0x1000000u +
                                      component[1] * 0x10000u +
                                      component[2] * 0x100u + component[3]);
}

bool TryParseWindowsDisplayDevicePciIdentity(std::string_view device_id,
                                             DisplayAdapterIdentity& out) {
    if (device_id.size() < 0x15) {
        return false;
    }

    DisplayAdapterIdentity candidate;
    candidate.vendor_id = ParseFixedWidthHexWord(device_id.substr(8, 4));
    candidate.device_id = ParseFixedWidthHexWord(device_id.substr(17, 4));
    if (candidate.vendor_id == 0 || candidate.device_id == 0) {
        return false;
    }

    out = candidate;
    return true;
}

PlatformOS PlatformLayer::GetOS() {
#if defined(_WIN32)
    return PlatformOS::Windows;
#elif defined(__APPLE__)
    return PlatformOS::MacOS;
#elif defined(__linux__)
    return PlatformOS::Linux;
#else
    return PlatformOS::Unknown;
#endif
}

PlatformArch PlatformLayer::GetArch() {
#if defined(__x86_64__) || defined(_M_X64)
    return PlatformArch::x86_64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return PlatformArch::ARM64;
#elif defined(__i386__) || defined(_M_IX86)
    return PlatformArch::x86;
#elif defined(__arm__) || defined(_M_ARM)
    return PlatformArch::ARM;
#else
    return PlatformArch::Unknown;
#endif
}

std::string PlatformLayer::GetOSName() {
    switch (GetOS()) {
        case PlatformOS::Windows: return "Windows";
        case PlatformOS::MacOS:   return "macOS";
        case PlatformOS::Linux:   return "Linux";
        default:                  return "Unknown";
    }
}

std::string PlatformLayer::GetArchName() {
    switch (GetArch()) {
        case PlatformArch::x86_64: return "x86_64";
        case PlatformArch::ARM64:  return "ARM64";
        case PlatformArch::x86:    return "x86";
        case PlatformArch::ARM:    return "ARM";
        default:                   return "Unknown";
    }
}

std::string PlatformLayer::GetUserDataPath() {
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    if (appdata) return std::string(appdata) + "\\WTF";
    return "WTF";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home) return std::string(home) + "/Library/Application Support/OpenWoW";
    return "OpenWoW";
#else
    const char* home = std::getenv("HOME");
    if (home) return std::string(home) + "/.config/openwow";
    return ".config/openwow";
#endif
}

std::string PlatformLayer::GetExecutablePath() {
    return openwow::platform::OS_GetModulePath();
}

uint32_t PlatformLayer::GetCPUCoreCount() {
    unsigned c = std::thread::hardware_concurrency();
    return c > 0 ? static_cast<uint32_t>(c) : 1u;
}

uint32_t PlatformLayer::GetPageSize() {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return static_cast<uint32_t>(si.dwPageSize);
#else
    long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? static_cast<uint32_t>(ps) : 4096u;
#endif
}

bool PlatformLayer::IsDebuggerAttached() {
#if defined(_WIN32)
    return IsDebuggerPresent() != 0;
#elif defined(__linux__)

    std::ifstream status("/proc/self/status");
    if (!status.is_open()) return false;
    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, 10, "TracerPid:") == 0) {
            const char* p = line.c_str() + 10;
            while (*p == ' ' || *p == '\t') ++p;
            return std::atoi(p) != 0;
        }
    }
    return false;
#elif defined(__APPLE__)

    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
    struct kinfo_proc info{};
    size_t size = sizeof(info);
    if (sysctl(mib, 4, &info, &size, nullptr, 0) == 0) {
        return (info.kp_proc.p_flag & P_TRACED) != 0;
    }
    return false;
#else
    return false;
#endif
}

static std::string QueryCPUName() {
#if defined(__linux__)
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo.is_open()) {
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.compare(0, 10, "model name") == 0) {
                auto pos = line.find(':');
                if (pos != std::string::npos) {
                    pos++;
                    while (pos < line.size() && line[pos] == ' ') pos++;
                    return line.substr(pos);
                }
            }
        }
    }
    return "Unknown";
#elif defined(__APPLE__)
    char buf[256]{};
    size_t len = sizeof(buf);
    if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0)
        return std::string(buf);
    return "Unknown";
#elif defined(_WIN32)

    return "Unknown";
#else
    return "Unknown";
#endif
}

static uint64_t QueryTotalRAM_MB() {
#if defined(__linux__)
    struct sysinfo si{};
    if (sysinfo(&si) == 0) {
        return static_cast<uint64_t>(si.totalram) * si.mem_unit / (1024u * 1024u);
    }
    return 0;
#elif defined(__APPLE__)
    int mib[2] = { CTL_HW, HW_MEMSIZE };
    uint64_t mem = 0;
    size_t len = sizeof(mem);
    if (sysctl(mib, 2, &mem, &len, nullptr, 0) == 0)
        return mem / (1024u * 1024u);
    return 0;
#elif defined(_WIN32)
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
        return ms.ullTotalPhys / (1024u * 1024u);
    return 0;
#else
    return 0;
#endif
}

PlatformInfo PlatformLayer::DetectPlatform() {
    PlatformInfo info;
    info.os                  = GetOS();
    info.arch                = GetArch();
    info.osVersion           = GetOSName();
    info.cpuName             = QueryCPUName();
    info.cpuCoreCount        = GetCPUCoreCount();
    info.totalRAM_MB         = QueryTotalRAM_MB();
    info.gpuName             = "Unknown";
    info.gpuVRAM_MB          = 0;
    DisplayAdapterIdentity adapter;
    if (TryGetPrimaryDisplayAdapterIdentity(adapter)) {
        info.gpuVRAM_MB = adapter.video_memory_bytes / (1024u * 1024u);
    }
    return info;
}

bool PlatformLayer::TryGetPrimaryDisplayAdapterIdentity(
    DisplayAdapterIdentity& out) {
    {
        std::lock_guard lock(s_displayAdapterOverrideMutex);
        if (s_displayAdapterOverride.has_value()) {
            out = *s_displayAdapterOverride;
            return true;
        }
    }

#if defined(_WIN32)
    for (DWORD index = 0;; ++index) {
        DISPLAY_DEVICEA display_device{};
        display_device.cb = sizeof(display_device);
        if (!EnumDisplayDevicesA(nullptr, index, &display_device, 0)) {
            break;
        }

        if ((display_device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) == 0) {
            continue;
        }

        if (TryParseWindowsDisplayDevicePciIdentity(display_device.DeviceID,
                                                    out)) {
            return true;
        }
        break;
    }

    return TryGetD3d9FallbackDisplayAdapterIdentity(out);
#elif defined(__linux__)
    namespace fs = std::filesystem;

    std::error_code ec;
    std::vector<fs::path> card_device_paths;
    for (fs::directory_iterator it("/sys/class/drm", ec), end; !ec && it != end;
         it.increment(ec)) {
        const fs::path card_path = it->path();
        if (!IsBaseDrmCardNode(card_path)) {
            continue;
        }

        const fs::path device_path = card_path / "device";
        if (fs::exists(device_path, ec)) {
            card_device_paths.push_back(device_path);
        }
    }
    if (ec || card_device_paths.empty()) {
        return false;
    }

    std::sort(card_device_paths.begin(), card_device_paths.end());
    for (const fs::path& device_path : card_device_paths) {
        const auto boot_vga = ReadSmallTextFile(device_path / "boot_vga");
        if (boot_vga.has_value() && *boot_vga == "1" &&
            TryReadLinuxDisplayAdapterIdentity(device_path, out)) {
            return true;
        }
    }

    for (const fs::path& device_path : card_device_paths) {
        if (TryReadLinuxDisplayAdapterIdentity(device_path, out)) {
            return true;
        }
    }

    return false;
#elif defined(__APPLE__)
    return TryReadMacDisplayAdapterIdentity(out);
#else
    (void)out;
    return false;
#endif
}

void PlatformLayer::SetPrimaryDisplayAdapterIdentityOverrideForTests(
    const std::optional<DisplayAdapterIdentity>& identity) {
    std::lock_guard lock(s_displayAdapterOverrideMutex);
    s_displayAdapterOverride = identity;
}

void PlatformLayer::ClearPrimaryDisplayAdapterIdentityOverrideForTests() {
    SetPrimaryDisplayAdapterIdentityOverrideForTests(std::nullopt);
}

}

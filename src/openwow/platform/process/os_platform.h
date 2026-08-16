
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace openwow::platform {

enum class MessageBoxButtons {
  kOk = 0,
  kOkCancel = 1,
  kYesNo = 2,
  kYesNoCancel = 3,
};

[[nodiscard]] std::string OS_GetOSVersionString();

[[nodiscard]] int OS_GetOSVersionId();

[[nodiscard]] std::string OS_GetComputerName();

[[nodiscard]] std::string OS_GetUserName();

[[nodiscard]] uint64_t OS_GetPhysicalMemory();

[[nodiscard]] std::uint64_t OS_GetProcessorFrequency();

[[nodiscard]] bool IsRemoteDesktopSession();

[[nodiscard]] std::string OS_GetModuleDirectory();

[[nodiscard]] std::string OS_GetModulePath();

void StripFilenameFromPath(std::string &path);

[[nodiscard]] std::string BuildFontPath(const std::string &fontName);

[[nodiscard]] std::string OS_GetCommandLine();

[[nodiscard]] std::string BuildModulePath(const std::string &relativePath);

[[nodiscard]] void *OS_GetActiveWindow(int mode = 0);

int ShowMessageBox(const std::string &text, const std::string &title = "",
                   MessageBoxButtons buttons = MessageBoxButtons::kOk);

[[nodiscard]] std::string FormatScreenshotTimestamp();

void OsSecureRandom(void *buffer, size_t length);

bool VerifyPlatformEndianness();

inline constexpr bool kHostIsLittleEndian =
    std::endian::native == std::endian::little;

static_assert(kHostIsLittleEndian,
              "OpenWoW requires a little-endian host. "
              "The WoW protocol and data files use little-endian encoding.");

inline constexpr std::uint16_t ByteSwap16(std::uint16_t v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap16(v);
#elif defined(_MSC_VER)

    if (std::is_constant_evaluated()) {
        return static_cast<std::uint16_t>((v >> 8) | (v << 8));
    }
    return _byteswap_ushort(v);
#else
    return (v >> 8) | (v << 8);
#endif
}

inline constexpr std::uint32_t ByteSwap32(std::uint32_t v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(v);
#elif defined(_MSC_VER)
    if (std::is_constant_evaluated()) {
        return (v >> 24) | ((v >> 8) & 0x0000FF00u)
             | ((v << 8) & 0x00FF0000u) | (v << 24);
    }
    return _byteswap_ulong(v);
#else
    return (v >> 24) | ((v >> 8) & 0x0000FF00u)
         | ((v << 8) & 0x00FF0000u) | (v << 24);
#endif
}

inline constexpr std::uint64_t ByteSwap64(std::uint64_t v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(v);
#elif defined(_MSC_VER)
    if (std::is_constant_evaluated()) {
        const std::uint32_t lo = static_cast<std::uint32_t>(v);
        const std::uint32_t hi = static_cast<std::uint32_t>(v >> 32);
        return (static_cast<std::uint64_t>(ByteSwap32(lo)) << 32)
             | static_cast<std::uint64_t>(ByteSwap32(hi));
    }
    return _byteswap_uint64(v);
#else
    std::uint32_t lo = static_cast<std::uint32_t>(v);
    std::uint32_t hi = static_cast<std::uint32_t>(v >> 32);
    return (static_cast<std::uint64_t>(ByteSwap32(lo)) << 32)
         | static_cast<std::uint64_t>(ByteSwap32(hi));
#endif
}

template <typename T>
inline T LittleEndianToNative(T v) noexcept {
    if constexpr (kHostIsLittleEndian) {
        return v;
    } else {
        if constexpr (sizeof(T) == 1) return v;
        else if constexpr (sizeof(T) == 2) return ByteSwap16(static_cast<std::uint16_t>(v));
        else if constexpr (sizeof(T) == 4) return ByteSwap32(static_cast<std::uint32_t>(v));
        else if constexpr (sizeof(T) == 8) return ByteSwap64(static_cast<std::uint64_t>(v));
        else return v;
    }
}

template <typename T>
inline T BigEndianToNative(T v) noexcept {
    if constexpr (kHostIsLittleEndian) {
        if constexpr (sizeof(T) == 1) return v;
        else if constexpr (sizeof(T) == 2) return ByteSwap16(static_cast<std::uint16_t>(v));
        else if constexpr (sizeof(T) == 4) return ByteSwap32(static_cast<std::uint32_t>(v));
        else if constexpr (sizeof(T) == 8) return ByteSwap64(static_cast<std::uint64_t>(v));
        else return v;
    } else {
        return v;
    }
}

inline constexpr std::uint32_t LoadLittleEndian32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0])
         | (static_cast<std::uint32_t>(bytes[1]) << 8)
         | (static_cast<std::uint32_t>(bytes[2]) << 16)
         | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

inline constexpr std::uint32_t LoadBigEndian32(const std::uint8_t* bytes) noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 24)
         | (static_cast<std::uint32_t>(bytes[1]) << 16)
         | (static_cast<std::uint32_t>(bytes[2]) << 8)
         | static_cast<std::uint32_t>(bytes[3]);
}

}

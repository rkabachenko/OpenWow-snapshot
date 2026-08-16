
#include "openwow/platform/process/os_platform.h"
#include "openwow/platform/process/os_platform_internal.h"

#include "openwow/core/storm_path.h"
#include "openwow/core/storm_utils.h"
#include "openwow/platform/window/window_manager.h"

#include <SDL2/SDL.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <ctime>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")
#elif defined(__linux__)
#include <climits>
#include <fcntl.h>
#include <pwd.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <pwd.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace openwow::platform {

namespace {

constexpr std::array<std::string_view, 16> kLegacyOsVersionStrings = {
    "Unknown",     "Win95",      "Win95OSR2", "Win98",
    "Win98SE",     "WinME",      "WinNT4",    "Win2000",
    "WinXP",       "Win2003",    "Win9X_Other",
    "WinNT_Other", "MacOS9",     "MacOSX",    "Linux",
    "WinVista",
};

constexpr int kWin32IdOk = 1;
constexpr int kWin32IdCancel = 2;
constexpr int kWin32IdYes = 6;
constexpr int kWin32IdNo = 7;

constexpr SDL_MessageBoxButtonData kOkButtons[] = {
    {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kWin32IdOk, "OK"},
};

constexpr SDL_MessageBoxButtonData kOkCancelButtons[] = {
    {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kWin32IdOk, "OK"},
    {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, kWin32IdCancel, "Cancel"},
};

constexpr SDL_MessageBoxButtonData kYesNoButtons[] = {
    {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kWin32IdYes, "Yes"},
    {0, kWin32IdNo, "No"},
};

constexpr SDL_MessageBoxButtonData kYesNoCancelButtons[] = {
    {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kWin32IdYes, "Yes"},
    {0, kWin32IdNo, "No"},
    {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, kWin32IdCancel, "Cancel"},
};

struct MessageBoxButtonLayout {
  const SDL_MessageBoxButtonData *buttons = nullptr;
  int button_count = 0;
};

[[nodiscard]] MessageBoxButtonLayout LookupMessageBoxButtonLayout(const MessageBoxButtons buttons) {
  switch (buttons) {
  case MessageBoxButtons::kOk:
    return {kOkButtons, static_cast<int>(SDL_arraysize(kOkButtons))};
  case MessageBoxButtons::kOkCancel:
    return {kOkCancelButtons, static_cast<int>(SDL_arraysize(kOkCancelButtons))};
  case MessageBoxButtons::kYesNo:
    return {kYesNoButtons, static_cast<int>(SDL_arraysize(kYesNoButtons))};
  case MessageBoxButtons::kYesNoCancel:
    return {kYesNoCancelButtons, static_cast<int>(SDL_arraysize(kYesNoCancelButtons))};
  }

  return {kOkButtons, static_cast<int>(SDL_arraysize(kOkButtons))};
}

#if defined(_WIN32)
int QueryLegacyWindowsVersionId() {
  OSVERSIONINFOEXA version_info{};
  version_info.dwOSVersionInfoSize = sizeof(version_info);
  if (GetVersionExA(reinterpret_cast<LPOSVERSIONINFOA>(&version_info))) {
    return detail::ClassifyLegacyWindowsVersion(
        {version_info.dwPlatformId, version_info.dwMajorVersion, version_info.dwMinorVersion,
         version_info.szCSDVersion[1]});
  }

  OSVERSIONINFOA legacy_version_info{};
  legacy_version_info.dwOSVersionInfoSize = sizeof(legacy_version_info);
  if (!GetVersionExA(&legacy_version_info)) {
    return 0;
  }

  return detail::ClassifyLegacyWindowsVersion(
      {legacy_version_info.dwPlatformId, legacy_version_info.dwMajorVersion,
       legacy_version_info.dwMinorVersion, legacy_version_info.szCSDVersion[1]});
}

std::string QueryCurrentModulePathUtf8() {
  std::vector<wchar_t> wide_path(512, L'\0');
  for (;;) {
    const DWORD copied =
        GetModuleFileNameW(nullptr, wide_path.data(), static_cast<DWORD>(wide_path.size()));
    if (copied == 0) {
      return {};
    }

    if (static_cast<std::size_t>(copied) + 1u < wide_path.size()) {
      wide_path[static_cast<std::size_t>(copied)] = L'\0';

      std::vector<char> utf8_path(static_cast<std::size_t>(copied) * 4u + 1u, '\0');
      static_assert(sizeof(wchar_t) == sizeof(char16_t));
      const int conversion_result = openwow::core::StormUtf16ToUtf8Bounded(
          utf8_path.data(), static_cast<std::uint32_t>(utf8_path.size()),
          reinterpret_cast<const char16_t *>(wide_path.data()), -1, nullptr, nullptr);
      if (conversion_result != 0) {
        return {};
      }

      return std::string(utf8_path.data());
    }

    wide_path.resize(wide_path.size() * 2u, L'\0');
  }
}
#endif

}

namespace detail {

int ClassifyLegacyWindowsVersion(const LegacyWindowsVersionProbe &probe) {
  if (probe.platform_id == 1u) {
    if (probe.major_version != 4u) {
      return 10;
    }

    if (probe.minor_version == 0u) {
      if (probe.windows9x_csd_marker == 'B' || probe.windows9x_csd_marker == 'C') {
        return 2;
      }
      return 1;
    }

    if (probe.minor_version == 10u) {
      if (probe.windows9x_csd_marker == 'A') {
        return 4;
      }
      return 3;
    }

    if (probe.minor_version == 90u) {
      return 5;
    }
    return 10;
  }

  if (probe.platform_id != 2u) {
    return 0;
  }

  if (probe.major_version == 4u) {
    return 6;
  }

  if (probe.major_version == 5u) {
    switch (probe.minor_version) {
    case 0u:
      return 7;
    case 1u:
      return 8;
    case 2u:
      return 9;
    default:
      return 11;
    }
  }

  if (probe.major_version == 6u && probe.minor_version == 0u) {
    return 15;
  }

  return 11;
}

std::string_view LookupLegacyOsVersionString(const int version_id) {
  if (version_id < 0 || static_cast<std::size_t>(version_id) >= kLegacyOsVersionStrings.size()) {
    return kLegacyOsVersionStrings[0];
  }

  const std::string_view label = kLegacyOsVersionStrings[static_cast<std::size_t>(version_id)];
  return label.empty() ? kLegacyOsVersionStrings[0] : label;
}

MessageBoxButtons DecodeLegacyMessageBoxButtons(const int mode) {
  switch (mode) {
  case 1:
    return MessageBoxButtons::kOkCancel;
  case 2:
    return MessageBoxButtons::kYesNo;
  case 3:
    return MessageBoxButtons::kYesNoCancel;
  default:
    return MessageBoxButtons::kOk;
  }
}

int MapLegacyMessageBoxResult(const int win32_result) {
  if (win32_result == kWin32IdOk || win32_result == kWin32IdYes) {
    return 0;
  }

  if (win32_result == kWin32IdCancel) {
    return 1;
  }

  return 2;
}

}

int OS_GetOSVersionId() {
#if defined(_WIN32)
  return QueryLegacyWindowsVersionId();
#elif defined(__linux__)
  return 14;
#elif defined(__APPLE__)
  return 13;
#else
  return 0;
#endif
}

std::string OS_GetOSVersionString() {
  return std::string(detail::LookupLegacyOsVersionString(OS_GetOSVersionId()));
}

std::string OS_GetComputerName() {
  char hostname[256]{};
#if defined(_WIN32)
  DWORD size = sizeof(hostname);
  if (GetComputerNameA(hostname, &size)) {
    return openwow::core::CurrentCodePageToUtf8String(hostname);
  }
#else
  if (gethostname(hostname, sizeof(hostname)) == 0)
    return hostname;
#endif
  return "UNKNOWN";
}

std::string OS_GetUserName() {
#if defined(_WIN32)
  char buf[256]{};
  DWORD size = sizeof(buf);
  if (GetUserNameA(buf, &size))
    return buf;
#else
  const char *user = getenv("USER");
  if (user)
    return user;
  struct passwd *pw = getpwuid(getuid());
  if (pw)
    return pw->pw_name;
#endif
  return "UNKNOWN";
}

uint64_t OS_GetPhysicalMemory() {
#if defined(_WIN32)
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms))
    return ms.ullTotalPhys;
  return 0;
#elif defined(__linux__)
  struct sysinfo si{};
  if (sysinfo(&si) == 0) {
    return static_cast<uint64_t>(si.totalram) * si.mem_unit;
  }
  return 0;
#elif defined(__APPLE__)
  int64_t memsize = 0;
  size_t len = sizeof(memsize);
  if (sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0) == 0) {
    return static_cast<uint64_t>(memsize);
  }
  return 0;
#else
  return 0;
#endif
}

std::uint64_t OS_GetProcessorFrequency() {
#if defined(_WIN32)
  HKEY key = nullptr;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                    "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                    0, KEY_READ, &key) != ERROR_SUCCESS) {
    return 0;
  }

  DWORD mhz = 0;
  DWORD size = sizeof(mhz);
  const LONG status = RegQueryValueExA(key, "~MHz", nullptr, nullptr,
                                       reinterpret_cast<LPBYTE>(&mhz), &size);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS) {
    return 0;
  }

  return static_cast<std::uint64_t>(mhz) * 1000000ull;
#elif defined(__linux__)
  if (FILE *freq_file = std::fopen(
          "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
      freq_file != nullptr) {
    unsigned long long khz = 0;
    const int scanned = std::fscanf(freq_file, "%llu", &khz);
    std::fclose(freq_file);
    if (scanned == 1) {
      return khz * 1000ull;
    }
  }

  if (FILE *cpuinfo = std::fopen("/proc/cpuinfo", "r"); cpuinfo != nullptr) {
    char line[256]{};
    while (std::fgets(line, sizeof(line), cpuinfo) != nullptr) {
      if (std::strncmp(line, "cpu MHz", 7) != 0) {
        continue;
      }

      const char *separator = std::strchr(line, ':');
      if (separator == nullptr) {
        continue;
      }

      const double mhz = std::strtod(separator + 1, nullptr);
      std::fclose(cpuinfo);
      if (mhz > 0.0) {
        return static_cast<std::uint64_t>(mhz * 1000000.0);
      }
      return 0;
    }

    std::fclose(cpuinfo);
  }

  return 0;
#elif defined(__APPLE__)
  std::uint64_t hz = 0;
  size_t len = sizeof(hz);
  if (sysctlbyname("hw.cpufrequency", &hz, &len, nullptr, 0) == 0) {
    return hz;
  }
  return 0;
#else
  return 0;
#endif
}

bool IsRemoteDesktopSession() {
#if defined(_WIN32)
  return GetSystemMetrics(SM_REMOTESESSION) != 0;
#else

  return getenv("SSH_CONNECTION") != nullptr || getenv("SSH_CLIENT") != nullptr ||
         getenv("DISPLAY") != nullptr;
#endif
}

void StripFilenameFromPath(std::string &path) {
  const char *const leaf = openwow::core::FindStormPathLeafName(path.c_str());
  if (leaf != path.c_str()) {
    path.erase(static_cast<std::size_t>(leaf - path.c_str()));
  }
}

std::string OS_GetModuleDirectory() {
  std::string path = OS_GetModulePath();
  StripFilenameFromPath(path);
  return path;
}

std::string OS_GetModulePath() {

  std::string path;
#if defined(_WIN32)
  path = QueryCurrentModulePathUtf8();
#elif defined(__linux__)
  char buf[PATH_MAX]{};
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len > 0) {
    buf[len] = '\0';
    path = buf;
  }
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  if (size != 0) {
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) == 0) {
      buf.resize(std::strlen(buf.c_str()));
      path = buf;
    }
  }
#endif
  return path;
}

std::string BuildFontPath(const std::string &fontName) {
  return std::string("Fonts\\") + fontName;
}

std::string OS_GetCommandLine() {

#if defined(_WIN32)
  return GetCommandLineA();
#elif defined(__linux__)
  std::string result;
  FILE *f = fopen("/proc/self/cmdline", "r");
  if (f) {
    char buf[4096]{};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    for (size_t i = 0; i < n; ++i) {
      result += (buf[i] == '\0') ? ' ' : buf[i];
    }
  }
  return result;
#else
  return "";
#endif
}

std::string BuildModulePath(const std::string &relativePath) {
  std::string dir = OS_GetModuleDirectory();
  return dir + relativePath;
}

void *OS_GetActiveWindow(int mode) {
  auto &window_manager = WindowManager::Get();
  if (!window_manager.IsInitialized()) {
    return nullptr;
  }

#if defined(_WIN32)
  if (mode == 1) {
    if (HWND hwnd = ::GetActiveWindow(); hwnd != nullptr) {
      return hwnd;
    }
  } else if (mode == 2) {
    if (HWND hwnd = ::GetForegroundWindow(); hwnd != nullptr) {
      return hwnd;
    }
  }
#else
  (void)mode;
#endif

  return window_manager.GetNativeHandle();
}

int ShowMessageBox(const std::string &text, const std::string &title,
                   const MessageBoxButtons buttons) {
  const MessageBoxButtonLayout layout = LookupMessageBoxButtonLayout(buttons);
  const SDL_MessageBoxData dialog{
      0,
      SDL_GL_GetCurrentWindow(),
      title.c_str(),
      text.c_str(),
      layout.button_count,
      layout.buttons,
      nullptr,
  };

  int button_id = 0;
  if (SDL_ShowMessageBox(&dialog, &button_id) != 0) {
    return detail::MapLegacyMessageBoxResult(0);
  }

  return detail::MapLegacyMessageBoxResult(button_id);
}

std::string FormatScreenshotTimestamp() {
  std::time_t now = std::time(nullptr);
  std::tm *tm = std::localtime(&now);
  char buf[64]{};
  std::strftime(buf, sizeof(buf), "%m%d%y_%H%M%S", tm);
  return buf;
}

void OsSecureRandom(void *buffer, size_t length) {
  if (!buffer || length == 0)
    return;

#if defined(_WIN32)
  HCRYPTPROV hProv = 0;
  if (CryptAcquireContextA(&hProv, nullptr, nullptr, PROV_RSA_FULL,
                           CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
    CryptGenRandom(hProv, static_cast<DWORD>(length), static_cast<BYTE *>(buffer));
    CryptReleaseContext(hProv, 0);
    return;
  }

#endif

#if defined(__linux__) || defined(__APPLE__)

  FILE* urandom = std::fopen("/dev/urandom", "rb");
  if (urandom) {
    const std::size_t n = std::fread(buffer, 1, length, urandom);
    std::fclose(urandom);
    if (n == length)
      return;
  }
#endif

  std::memset(buffer, 0, length);
}

bool VerifyPlatformEndianness() {

  const std::uint32_t probe_value = 0x01020304u;
  const bool runtime_is_le =
      *reinterpret_cast<const std::uint8_t*>(&probe_value) == 0x04u;

  if (runtime_is_le != kHostIsLittleEndian) {

#if !defined(NDEBUG)
    std::abort();
#else
    return false;
#endif
  }

  return true;
}

}

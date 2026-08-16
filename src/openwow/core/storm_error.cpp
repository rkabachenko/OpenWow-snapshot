
#include "storm_error.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/runtime/time/game_time.h"
#include "storm_string.h"
#include "storm_utils.h"
#include "openwow/platform/adapters/win32/win32_error_log.h"
#include "openwow/platform/process/os_platform.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <dbghelp.h>
#include <io.h>
#else
#include <csignal>
#include <unistd.h>
#ifdef __APPLE__
#include <pthread.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

namespace openwow::core {

FatalErrorIntercepted::FatalErrorIntercepted(std::uint32_t message_id, std::string message)
    : std::runtime_error(message), message_id_(message_id) {}

std::uint32_t FatalErrorIntercepted::message_id() const noexcept {
  return message_id_;
}

static std::atomic_int32_t g_critInitCounter{-1};

static std::recursive_mutex g_errMutex1;
static std::recursive_mutex g_errMutex2;

static char g_appName[128] = {};

static void *g_crashDumpCallback = nullptr;

static char g_lastLogPath[260] = {};

static std::atomic_uint32_t g_errorFileSequence{0};

static uint32_t g_noMinidumps = 0;

static int g_minidumpInitialized = 0;

static int g_displayErrorActive = 0;

static int g_suppressDuplicateErrors = 0;

static const char *g_lastDisplayedSource = nullptr;
static int g_lastDisplayedExitCode = 0;

static int g_moduleRegistered = 0;

static uint32_t g_debugMemory = 0;

static int g_handleCheckInitialized = 0;

static int g_exitCode = 0;

static const char *g_fatalSourceFile = nullptr;

static int g_fatalSourceLine = 0;

static uint32_t g_fatalThreadId = 0;

struct RegisteredErrorModule {
  std::uint16_t module_id = 0;
  void *module_handle = nullptr;
};

namespace {

constexpr std::uint32_t kSErrLocalizedStringBaseId = 0x5100u;
constexpr std::size_t kSErrLocalizedStringBufferSize = 256;

struct SErrCriticalSectionToken {};

SErrCriticalSectionToken g_stateCriticalSectionToken;
SErrCriticalSectionToken g_displayCriticalSectionToken;

std::function<void(std::uint32_t, const std::string &)> &FatalErrorInterceptorStorage() {
  static std::function<void(std::uint32_t, const std::string &)> interceptor;
  return interceptor;
}

detail::SErrLocalizedStringLoader &SErrLocalizedStringLoaderStorage() {
  static detail::SErrLocalizedStringLoader loader = nullptr;
  return loader;
}

detail::DebuggerAttachmentLibraryLoader &DebuggerAttachmentLibraryLoaderStorage() {
  static detail::DebuggerAttachmentLibraryLoader loader = nullptr;
  return loader;
}

detail::DebuggerAttachmentProbeResolver &DebuggerAttachmentProbeResolverStorage() {
  static detail::DebuggerAttachmentProbeResolver resolver = nullptr;
  return resolver;
}

detail::DebuggerAttachmentLibraryReleaser &DebuggerAttachmentLibraryReleaserStorage() {
  static detail::DebuggerAttachmentLibraryReleaser releaser = nullptr;
  return releaser;
}

detail::SErrAssertSignalHandler &SErrAssertDebugBreakHandlerStorage() {
  static detail::SErrAssertSignalHandler handler = nullptr;
  return handler;
}

detail::SErrAssertSignalHandler &SErrAssertAbortHandlerStorage() {
  static detail::SErrAssertSignalHandler handler = nullptr;
  return handler;
}

bool &SErrAssertAbortSuppressedStorage() {
  static bool suppressed = true;
  return suppressed;
}

std::vector<RegisteredErrorModule> &RegisteredErrorModules() {
  static std::vector<RegisteredErrorModule> modules;
  return modules;
}

using CrashNotifyCallback = int (*)(std::uint32_t message_id, char *resolved_message,
                                    const char *source, int exit_code,
                                    const char *extra);

std::list<void *> &RegisteredCrashCallbacks() {
  static std::list<void *> callbacks;
  return callbacks;
}

void ClearRegisteredCrashCallbacksLocked() {
  RegisteredCrashCallbacks().clear();
}

void ClearRegisteredErrorModulesUnlocked() {
  RegisteredErrorModules().clear();
}

[[nodiscard]] bool InvokeRegisteredCrashCallbacksLocked(const std::uint32_t message_id,
                                                        char *resolved_message,
                                                        const char *source,
                                                        const int exit_code,
                                                        const char *extra) {
  const char *const callback_extra = extra != nullptr ? extra : "";
  for (void *const raw_callback : RegisteredCrashCallbacks()) {
    const auto callback = reinterpret_cast<CrashNotifyCallback>(raw_callback);
    if (callback(message_id, resolved_message, source, exit_code, callback_extra) == 0) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] void *GetCurrentErrorModuleHandle() {
#ifdef _WIN32
  return ::GetModuleHandleA(nullptr);
#else
  return nullptr;
#endif
}

void RegisterErrorModuleUnlocked(const std::uint16_t module_id) {
  RegisteredErrorModules().push_back(
      RegisteredErrorModule{.module_id = module_id, .module_handle = GetCurrentErrorModuleHandle()});
}

void EnsureSErrCriticalSectionsInitialized() {
  const auto next = g_critInitCounter.fetch_add(1, std::memory_order_acq_rel) + 1;
  if (next != 0) {
    (void)g_critInitCounter.fetch_sub(1, std::memory_order_acq_rel);
  }
}

std::recursive_mutex &ResolveSErrCriticalSection(void *critical_section) {
  if (critical_section == &g_displayCriticalSectionToken) {
    return g_errMutex2;
  }

  return g_errMutex1;
}

void *StateSErrCriticalSectionToken() {
  return &g_stateCriticalSectionToken;
}

void *DisplaySErrCriticalSectionToken() {
  return &g_displayCriticalSectionToken;
}

void LeaveSErrCritical(void *critical_section) {
  ResolveSErrCriticalSection(critical_section).unlock();
}

class ScopedSErrCriticalSection {
public:
  explicit ScopedSErrCriticalSection(void *critical_section) : critical_section_(critical_section) {
    EnsureSErrCriticalSectionsInitialized();
    ResolveSErrCriticalSection(critical_section_).lock();
  }

  ScopedSErrCriticalSection(const ScopedSErrCriticalSection &) = delete;
  ScopedSErrCriticalSection &operator=(const ScopedSErrCriticalSection &) = delete;

  ~ScopedSErrCriticalSection() {
    LeaveSErrCritical(critical_section_);
  }

private:
  void *critical_section_;
};

#if defined(_WIN32)

[[nodiscard]] void *LookupRegisteredErrorModuleSourceLocked(const std::uint32_t message_id) {
  const auto module_id = static_cast<std::uint16_t>((message_id >> 16) & 0x0FFFu);
  for (const auto &registered_module : RegisteredErrorModules()) {
    if (registered_module.module_id == module_id) {
      return registered_module.module_handle;
    }
  }

  return nullptr;
}
#endif

[[nodiscard]] const char *LookupFallbackSystemErrorString(const std::uint32_t error_code) {
  switch (error_code) {
  case 2:
    return "The system cannot find the file specified.\r\n";
  case 5:
    return "Access is denied.\r\n";
  case 38:
    return "Reached the end of the file.\r\n";
  case 87:
    return "The parameter is incorrect.\r\n";
  default:
    return nullptr;
  }
}

void DefaultSErrAssertDebugBreak() {
#if defined(_WIN32)
  ::DebugBreak();
#elif defined(SIGTRAP)
  std::raise(SIGTRAP);
#endif
}

void DefaultSErrAssertAbort() {
  std::abort();
}

detail::SErrAssertSignalHandler ResolveSErrAssertDebugBreakHandler() {
  return SErrAssertDebugBreakHandlerStorage() ? SErrAssertDebugBreakHandlerStorage()
                                              : &DefaultSErrAssertDebugBreak;
}

detail::SErrAssertSignalHandler ResolveSErrAssertAbortHandler() {
  return SErrAssertAbortHandlerStorage() ? SErrAssertAbortHandlerStorage()
                                         : &DefaultSErrAssertAbort;
}

void *DefaultDebuggerAttachmentLibraryLoader() {
#ifdef _WIN32
  return ::LoadLibraryA("KERNEL32.DLL");
#else
  return nullptr;
#endif
}

detail::DebuggerAttachmentProbe DefaultDebuggerAttachmentProbeResolver(void *module,
                                                                      const char *name) {
#ifdef _WIN32
  if (module == nullptr || name == nullptr) {
    return nullptr;
  }

  return reinterpret_cast<detail::DebuggerAttachmentProbe>(
      ::GetProcAddress(reinterpret_cast<HMODULE>(module), name));
#else
  (void)module;
  (void)name;
  return nullptr;
#endif
}

void DefaultDebuggerAttachmentLibraryReleaser(void *module) {
#ifdef _WIN32
  if (module != nullptr) {
    ::FreeLibrary(reinterpret_cast<HMODULE>(module));
  }
#else
  (void)module;
#endif
}

detail::DebuggerAttachmentLibraryLoader ResolveDebuggerAttachmentLibraryLoader() {
  return DebuggerAttachmentLibraryLoaderStorage() ? DebuggerAttachmentLibraryLoaderStorage()
                                                  : &DefaultDebuggerAttachmentLibraryLoader;
}

detail::DebuggerAttachmentProbeResolver ResolveDebuggerAttachmentProbeResolver() {
  return DebuggerAttachmentProbeResolverStorage() ? DebuggerAttachmentProbeResolverStorage()
                                                  : &DefaultDebuggerAttachmentProbeResolver;
}

detail::DebuggerAttachmentLibraryReleaser ResolveDebuggerAttachmentLibraryReleaser() {
  return DebuggerAttachmentLibraryReleaserStorage()
             ? DebuggerAttachmentLibraryReleaserStorage()
             : &DefaultDebuggerAttachmentLibraryReleaser;
}

std::filesystem::path BuildSErrAssertLogPath() {
  char exe_directory[512]{};
  if (!GetExeDirectory(exe_directory, sizeof(exe_directory)) || exe_directory[0] == '\0') {
    return {};
  }

  return std::filesystem::path(exe_directory) / "Errors" / "Assert.log";
}

bool TryLoadSErrLocalizedString(const std::uint32_t resource_id, char *buffer,
                                const std::size_t buffer_size) {
  if (buffer_size == 0) {
    return false;
  }

  buffer[0] = '\0';
  if (const auto loader = SErrLocalizedStringLoaderStorage()) {
    return loader(resource_id, buffer, buffer_size);
  }

#ifdef _WIN32
  return ::LoadStringA(::GetModuleHandleA(nullptr), resource_id, buffer,
                       static_cast<int>(buffer_size)) != 0;
#else
  (void)resource_id;
  return false;
#endif
}

}

static constexpr std::array<const char *, 18> g_fallbackStrings = {
    "ERROR #%u (0x%08x) %s",
    "This application has encountered a critical error:\n\n%s\n",
    "Program:\t%s\n",
    "File:\t%s\nLine:\t%d\n",
    "Function:\t%s\n",
    "Object:\t%s\n",
    "Handle:\t%s\n",
    "Expr:\t%s\n\n",
    "\n%s\n\n",
    "Press OK to terminate the application.",
    "Do you wish to keep running anyway?\n\tOK:\tKeep running\n\tCancel:\tTerminate application",
    "File:\t%s\n",
    "Do you wish to break to the debugger?\n\tYes:\tBreak to debugger\n\tNo:\tTerminate application",
    "Do you wish to break to the debugger?\n\tYes:\tBreak to debugger\n\tNo:\tTerminate "
    "application\n\tCancel:\tKeep running",
    "Exception:\t%s\n",
    "The instruction at \"0x%p\" referenced memory at \"0x%p\".\nThe memory could not be \"%s\".",
    "read",
    "written",
};
static char g_seErrLocalizedStringBuffer[kSErrLocalizedStringBufferSize] = {};

const char *SErrGetLocalizedString(int index) {
  if (index < 0 || index >= static_cast<int>(g_fallbackStrings.size())) {
    return "";
  }

  if (TryLoadSErrLocalizedString(static_cast<std::uint32_t>(index) + kSErrLocalizedStringBaseId,
                                 g_seErrLocalizedStringBuffer,
                                 sizeof(g_seErrLocalizedStringBuffer))) {
    return g_seErrLocalizedStringBuffer;
  }

  return g_fallbackStrings[static_cast<std::size_t>(index)];
}

void SErrRegisterModule(int16_t moduleId) {
  RegisterErrorModuleUnlocked(static_cast<std::uint16_t>(moduleId));
}

bool SErrGetErrorString(const std::uint32_t messageId, char *buffer, const std::size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) {
    SErrSetLastError(87);
    return false;
  }

#if defined(_WIN32)
  void *module_source = nullptr;
#endif
  {
    ScopedSErrCriticalSection lock(StateSErrCriticalSectionToken());
    if (g_moduleRegistered == 0) {
      g_moduleRegistered = 1;
      SErrRegisterModule(0x510);
      SErrRegisterModule(0x876);
      SErrRegisterModule(0x878);
    }

#if defined(_WIN32)

    module_source = LookupRegisteredErrorModuleSourceLocked(messageId);
#endif
  }

  buffer[0] = '\0';
  if (messageId == 108u) {
    SStrPrintf(buffer, bufferSize, "%s", "Invalid or corrupt archive.\n");
    return true;
  }

#if defined(_WIN32)
  const DWORD flags =
      module_source != nullptr ? FORMAT_MESSAGE_FROM_HMODULE : FORMAT_MESSAGE_FROM_SYSTEM;
  return ::FormatMessageA(flags, module_source, messageId, 0x400u, buffer,
                          static_cast<DWORD>(bufferSize), nullptr) != 0;
#else
  if (const char *const fallback = LookupFallbackSystemErrorString(messageId); fallback != nullptr) {
    SStrPrintf(buffer, bufferSize, "%s", fallback);
    return true;
  }

  return false;
#endif
}

void SErrEnterCritical(void *criticalSection) {
  EnsureSErrCriticalSectionsInitialized();
  ResolveSErrCriticalSection(criticalSection).lock();
}

namespace {

uint32_t PortableGetCurrentThreadId() {
#if defined(_WIN32)
  return ::GetCurrentThreadId();
#elif defined(__APPLE__)
  std::uint64_t tid = 0;
  ::pthread_threadid_np(nullptr, &tid);
  return static_cast<uint32_t>(tid);
#elif defined(SYS_gettid)
  return static_cast<uint32_t>(::syscall(SYS_gettid));
#else
  return static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

}

void SErrPrepareAppAbort(const char *file, int line) {
  EnsureSErrCriticalSectionsInitialized();

  std::lock_guard lock(g_errMutex1);
  g_fatalSourceFile = file;
  g_fatalSourceLine = line;
  g_fatalThreadId = PortableGetCurrentThreadId();
}

void InitMinidumpSettings() {
  g_minidumpInitialized = 1;
  ReadRegistryValue("Internal", "No Minidumps", 0, &g_noMinidumps);
}

bool IsDebuggerAttached() {
  void *const kernel32 = ResolveDebuggerAttachmentLibraryLoader()();
  if (kernel32 == nullptr) {
    return false;
  }

  const auto is_debugger_present =
      ResolveDebuggerAttachmentProbeResolver()(kernel32, "IsDebuggerPresent");
  const bool attached = is_debugger_present != nullptr && is_debugger_present() != 0;
  ResolveDebuggerAttachmentLibraryReleaser()(kernel32);
  return attached;
}

const char *SErrGetErrorTitle(uint32_t errorCode) {
  switch (errorCode) {
  case 0x85100000:
    return "Assertion Failure";
  case 0x8510006C:
    return "Not Archive";
  case 0x85100079:
    return "Version Mismatch";
  case 0x8510007A:
    return "Memory Already Freed";
  case 0x8510007B:
    return "Memory Corrupt";
  case 0x8510007C:
    return "Memory Invalid Block";
  case 0x8510007E:
    return "Memory Never Released";
  case 0x8510007F:
    return "Handle Never Released";
  case 0x85100080:
    return "Access Out Of Bounds";
  case 0x85100083:
    return "File Corrupt";
  case 0x85100084:
    return "Fatal Exception";
  case 0x85100086:
    return "Fatal Condition";
  default:
    return "";
  }
}

void ExceptionCodeToString(char *buf, uint32_t bufSize, uint32_t code) {
  struct CodeMapping {
    uint32_t code;
    const char *name;
  };
  static const CodeMapping mappings[] = {
      {0x80000001, "GUARD_PAGE"},
      {0x80000002, "DATATYPE_MISALIGNMENT"},
      {0x80000003, "BREAKPOINT"},
      {0x80000004, "SINGLE_STEP"},
      {0xC0000005, "ACCESS_VIOLATION"},
      {0xC0000006, "IN_PAGE_ERROR"},
      {0xC0000008, "INVALID_HANDLE"},
      {0xC000001D, "ILLEGAL_INSTRUCTION"},
      {0xC0000025, "NONCONTINUABLE_EXCEPTION"},
      {0xC0000026, "INVALID_DISPOSITION"},
      {0xC000008C, "ARRAY_BOUNDS_EXCEEDED"},
      {0xC000008D, "FLT_DENORMAL_OPERAND"},
      {0xC000008E, "FLT_DIVIDE_BY_ZERO"},
      {0xC000008F, "FLT_INEXACT_RESULT"},
      {0xC0000090, "FLT_INVALID_OPERATION"},
      {0xC0000091, "FLT_OVERFLOW"},
      {0xC0000092, "FLT_STACK_CHECK"},
      {0xC0000093, "FLT_UNDERFLOW"},
      {0xC0000094, "INT_DIVIDE_BY_ZERO"},
      {0xC0000095, "INT_OVERFLOW"},
      {0xC0000096, "PRIV_INSTRUCTION"},
      {0xC00000FD, "STACK_OVERFLOW"},
  };

  for (const auto &m : mappings) {
    if (m.code == code) {
      SStrCopy(buf, m.name, bufSize);
      return;
    }
  }
  SStrCopy(buf, "unknown exception", bufSize);
}

void SetApplicationName(const char *name) {
  ScopedSErrCriticalSection lock(StateSErrCriticalSectionToken());
  SStrCopy(g_appName, name, 128);
}

void SetCrashDumpCallback(void *callback) {
  ScopedSErrCriticalSection lock(StateSErrCriticalSectionToken());
  g_crashDumpCallback = callback;
}

bool SErrGetLastLogPath(char *buf, int bufSize) {
  ScopedSErrCriticalSection lock(StateSErrCriticalSectionToken());
  if (g_lastLogPath[0]) {
    SStrCopy(buf, g_lastLogPath, static_cast<size_t>(bufSize));
    return true;
  }
  return false;
}

bool ParseDecoratedName(int maxLen, char *output, const char *decorated) {
  if (SStrLen(decorated) < 6)
    return false;
  if (decorated[0] != '.')
    return false;
  if (decorated[3] != 'U')
    return false;

  if (!std::strpbrk(decorated + 4, "?@"))
    return false;

  SStrCopy(output, decorated + 4, static_cast<size_t>(maxLen));

  char *end = std::strpbrk(output, "?@");
  if (!end)
    return false;
  *end = '\0';

  for (char *p = end - 1; p >= output && *p == '_'; --p)
    *p = '\0';

  SStrCat(output, " (", static_cast<size_t>(maxLen));
  SStrCat(output, decorated, static_cast<size_t>(maxLen));
  SStrCat(output, ")", static_cast<size_t>(maxLen));
  return true;
}

void RegisterCrashCallback(void *callback) {
  ScopedSErrCriticalSection lock(StateSErrCriticalSectionToken());
  RegisteredCrashCallbacks().push_front(callback);
}

void SErrShutdown() {
  const bool critical_sections_initialized =
      g_critInitCounter.load(std::memory_order_acquire) != -1;

  if (critical_sections_initialized) {
    ScopedSErrCriticalSection lock(StateSErrCriticalSectionToken());
    ClearRegisteredCrashCallbacksLocked();
    ClearRegisteredErrorModulesUnlocked();
    g_moduleRegistered = 0;
  } else {
    ClearRegisteredCrashCallbacksLocked();
    ClearRegisteredErrorModulesUnlocked();
    g_moduleRegistered = 0;
  }

  g_critInitCounter.store(-1, std::memory_order_release);
}

void SErrAssertHandler(const char *expression, const char *message, const char *file, int line) {
  const auto calendar_fields =
      ida::CalendarTimeBreakdownFromNsSince2000(GameClock::GetCurrentTimeNsSince2000());

  char normalized_file[256]{};
  SErrAssertHandler_NormalizePath(file, normalized_file, sizeof(normalized_file));

  char formatted[2048]{};
  std::snprintf(formatted, sizeof(formatted),
                "%04d/%02d/%02d %02d:%02d:%02d  %s(%d) : Assertion failed: "
                "%s  \"%s\"\n",
                calendar_fields.year, calendar_fields.month, calendar_fields.day,
                calendar_fields.hour, calendar_fields.minute, calendar_fields.second,
                normalized_file, line, expression ? expression : "", message ? message : "");

  std::fprintf(stderr, "%s", formatted);

  const auto log_path = BuildSErrAssertLogPath();
  if (!log_path.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(log_path.parent_path(), ec);

    std::ofstream log_stream(log_path, std::ios::app);
    if (log_stream) {
      log_stream << formatted;
    }
  }

  ResolveSErrAssertDebugBreakHandler()();
  if (!SErrAssertAbortSuppressedStorage()) {
    ResolveSErrAssertAbortHandler()();
  }
}

namespace {

int FormatErrorSegment(char *dst, size_t size, const char *format, ...) {
  std::va_list args;
  va_start(args, format);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
  const int result = std::vsnprintf(dst, size, format, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  va_end(args);
  return result;
}

}

int SErrDisplayError(uint32_t messageId, const char *source, int exitCode, const char *extra,
                     int , uint32_t , int ) {

  if (g_suppressDuplicateErrors) {
    if (source == g_lastDisplayedSource && exitCode == g_lastDisplayedExitCode)
      return 1;
    g_lastDisplayedSource = source;
    g_lastDisplayedExitCode = exitCode;
  }

  if (g_displayErrorActive)
    return 0;
  g_displayErrorActive = 1;

  const char *appCaption = g_appName[0] ? g_appName : "(unknown)";

  char messageText[256]{};
  const char *title = SErrGetErrorTitle(messageId);
  if (!SErrGetErrorString(messageId, messageText, sizeof(messageText)) || messageText[0] == '\0') {
    FormatErrorSegment(messageText, sizeof(messageText), SErrGetLocalizedString(0),
                       static_cast<unsigned int>(static_cast<std::uint16_t>(messageId)), messageId,
                       title);
  }

  char errorBuf[4096];
  char *cursor = errorBuf;
  const char *const end = errorBuf + sizeof(errorBuf);

  const char *displaySource = source;
  char decoratedBuf[256]{};
  if (source && *source && (exitCode == -2 || exitCode == -3)) {
    if (ParseDecoratedName(256, decoratedBuf, source))
      displaySource = decoratedBuf;
  }
  if (displaySource && *displaySource) {
    cursor += std::snprintf(cursor, static_cast<size_t>(end - cursor), "%s", displaySource);
  }

  if (exitCode > 0) {
    cursor += std::snprintf(cursor, static_cast<size_t>(end - cursor), "(%d)", exitCode);
  }

  cursor += std::snprintf(cursor, static_cast<size_t>(end - cursor), " : error %u: %s",
                          static_cast<unsigned>(static_cast<uint16_t>(messageId)), messageText);

  std::fprintf(stderr, "%s\n", errorBuf);

  cursor = errorBuf;

  cursor += FormatErrorSegment(cursor, static_cast<size_t>(end - cursor),
                               SErrGetLocalizedString(1), messageText);

  cursor += FormatErrorSegment(cursor, static_cast<size_t>(end - cursor),
                               SErrGetLocalizedString(2), appCaption);

  if (displaySource && *displaySource) {
    switch (exitCode) {
    case -5:
      cursor += FormatErrorSegment(cursor, static_cast<size_t>(end - cursor),
                                   SErrGetLocalizedString(14), displaySource);
      break;
    case -4:
      cursor += FormatErrorSegment(cursor, static_cast<size_t>(end - cursor),
                                   SErrGetLocalizedString(11), displaySource);
      break;
    case -3:
      cursor += FormatErrorSegment(cursor, static_cast<size_t>(end - cursor),
                                   SErrGetLocalizedString(6), displaySource);
      break;
    case -2:
      cursor += FormatErrorSegment(cursor, static_cast<size_t>(end - cursor),
                                   SErrGetLocalizedString(5), displaySource);
      break;
    case -1:
      cursor += FormatErrorSegment(cursor, static_cast<size_t>(end - cursor),
                                   SErrGetLocalizedString(4), displaySource);
      break;
    default:
      cursor += FormatErrorSegment(cursor, static_cast<size_t>(end - cursor),
                                   SErrGetLocalizedString(3), displaySource, exitCode);
      break;
    }
  }

  const char *displayExtra = extra ? extra : "";
  if (messageId == 0x85100000) {
    cursor += FormatErrorSegment(cursor, static_cast<size_t>(end - cursor),
                                 SErrGetLocalizedString(7), displayExtra);
  } else {
    cursor += FormatErrorSegment(cursor, static_cast<size_t>(end - cursor),
                                 SErrGetLocalizedString(8), displayExtra);
  }

  std::FILE *f = CreateErrorLogFile(nullptr, "Error", errorBuf);
  if (f)
    std::fclose(f);

  if (g_suppressDuplicateErrors) {
    g_displayErrorActive = 0;
    return 1;
  }

  {
    ScopedSErrCriticalSection lock(StateSErrCriticalSectionToken());

    (void)InvokeRegisteredCrashCallbacksLocked(messageId, messageText, source, exitCode, extra);
  }

  g_displayErrorActive = 0;
  return 1;
}

bool IsSErrDisplayErrorActive() {
  return g_displayErrorActive != 0;
}

[[noreturn]] void SErrFatalError(uint32_t messageId, const char *format, va_list args) {

  const char *source = nullptr;
  int exitCode = 0;
  {
    EnsureSErrCriticalSectionsInitialized();
    std::lock_guard lock(g_errMutex1);
    if (g_fatalThreadId == PortableGetCurrentThreadId()) {
      source = g_fatalSourceFile;
      exitCode = g_fatalSourceLine;
      g_fatalSourceFile = nullptr;
      g_fatalSourceLine = 0;
      g_fatalThreadId = 0;
    }
  }

  char buf[2048];
  std::vsnprintf(buf, sizeof(buf) - 1, format, args);
  buf[2047] = '\0';

  SErrDisplayError(messageId, source, exitCode, buf, 0, 1, 0x11111111);
  if (auto &interceptor = FatalErrorInterceptorStorage(); interceptor) {
    interceptor(messageId, buf);
    throw FatalErrorIntercepted(messageId, buf);
  }
  ExitProcessWithCode(1);
}

[[noreturn]] void SErrFatalError_VArgs(uint32_t messageId, const char *format, ...) {
  va_list args;
  va_start(args, format);
  SErrFatalError(messageId, format, args);

}

[[noreturn]] void SErrFatalCondition(const char *format, ...) {
  va_list args;
  va_start(args, format);
  SErrFatalError(0x85100086, format, args);
}

int SErrDisplayError_Fmt(uint32_t messageId, std::intptr_t source, int exitCode, int canRetry,
                         uint32_t exitParam, const char *format, ...) {

  char buf[2048];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  buf[2047] = '\0';

  return SErrDisplayError(messageId, reinterpret_cast<const char *>(source),
                          exitCode, buf, canRetry, exitParam, 0x11111111);
}

void SetExceptionFilter() {
#ifdef _WIN32

#else

#endif
}

[[noreturn]] void ExitWithCode(int code) {
  g_exitCode = code;
  std::exit(code);
}

[[noreturn]] void ExitProcessWithCode(int code) {
  g_exitCode = code;
#ifdef _WIN32
  ExitProcess(static_cast<UINT>(code));
#else
  _exit(code);
#endif
}

int GetLastExitCode() {
  return g_exitCode;
}

void CheckHandleRelease(const char *handleName, const char *context) {
  if (!g_handleCheckInitialized) {
    g_handleCheckInitialized = 1;
    ReadRegistryValue("Internal", "Debug Memory", 0, &g_debugMemory);
  }

  if (!g_debugMemory)
    return;

  char msg[256];
  if (context && *context)
    SStrPrintf(msg, 256, "%s (%s)", handleName, context);
  else
    SStrPrintf(msg, 256, "%s", handleName);

  if (g_minidumpInitialized) {
    char debugStr[200];
    SStrPrintf(debugStr, 200, "Storm Error : handle never released -- %s\n", msg);
    std::fputs(debugStr, stderr);
  } else {
    SErrDisplayError(0x8510007F, msg, -3, nullptr, 1, 1, 0x11111111);
  }
}

std::string FormatErrorLogLine(const char *format, std::va_list args) {

  if (!format) {
    format = "";
  }
  char buffer[512];
  std::va_list args_copy;
  va_copy(args_copy, args);
  std::vsnprintf(buffer, 510, format, args_copy);
  va_end(args_copy);
  buffer[509] = '\0';
  SStrCat(buffer, "\r\n", 512);
  return std::string(buffer, SStrLen(buffer));
}

#if defined(_WIN32)
static int WINAPIV WriteErrorLogLine(int handle, const char *format, ...) {
  va_list args;
  va_start(args, format);
  std::string line = FormatErrorLogLine(format, args);
  va_end(args);
  DWORD written = 0;
  return WriteFile(
      reinterpret_cast<HANDLE>(static_cast<uintptr_t>(static_cast<unsigned>(handle))),
      line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
}
#endif

std::FILE *CreateErrorFilePath(const struct tm *timeInfo, char *pathOut,
                               const char *typeName, const char *extension,
                               int maxPath) {
  if (!typeName)
    typeName = " Log";

  std::string exePath = openwow::platform::OS_GetModulePath();
  SStrCopy(pathOut, exePath.c_str(), static_cast<size_t>(maxPath));

  char *lastSep = nullptr;
  for (char *p = pathOut; *p; ++p) {
    if (*p == '/' || *p == '\\')
      lastSep = p;
  }
  if (lastSep) {
    lastSep[1] = '\0';
  }

#ifdef _WIN32
  SStrCat(pathOut, "Errors\\", static_cast<size_t>(maxPath));
#else
  SStrCat(pathOut, "Errors/", static_cast<size_t>(maxPath));
#endif

  std::error_code ec;
  std::filesystem::create_directories(pathOut, ec);
  if (ec && !std::filesystem::is_directory(pathOut)) {
    if (lastSep)
      lastSep[1] = '\0';
  }

  char buf[260];
  const std::uint32_t sequence =
      g_errorFileSequence.fetch_add(1, std::memory_order_relaxed);
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d.%02d.%02d.%02u %s.%s",
                timeInfo->tm_year + 1900, timeInfo->tm_mon + 1,
                timeInfo->tm_mday, timeInfo->tm_hour, timeInfo->tm_min,
                timeInfo->tm_sec, sequence, typeName, extension);
  SStrCat(pathOut, buf, static_cast<size_t>(maxPath));

  return std::fopen(pathOut, "wb");
}

std::FILE *CreateErrorLogFile(const struct tm *timeInfo, const char *typeName,
                              const char *errorText) {
  struct tm localTime{};
  if (!timeInfo) {
    auto now = std::time(nullptr);
#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    timeInfo = &localTime;
  }

  char pathBuf[260];
  std::FILE *file = CreateErrorFilePath(timeInfo, pathBuf, typeName, "txt", 260);
  if (!file)
    return nullptr;

#if defined(_WIN32)
  {
    int fd = _fileno(file);
    intptr_t osHandle = _get_osfhandle(fd);
    openwow::platform::WriteErrorLogHeader(
        1,
        reinterpret_cast<openwow::platform::LogWriterFn>(WriteErrorLogLine),
        static_cast<int>(osHandle), g_appName, nullptr);
  }
#else

  std::fprintf(file,
               "==============================================================================\r\n"
               "%s\r\n\r\n"
               "Time: %04d-%02d-%02d %02d:%02d:%02d\r\n"
               "------------------------------------------------------------------------------\r\n",
               g_appName[0] ? g_appName : "(unknown)",
               timeInfo->tm_year + 1900, timeInfo->tm_mon + 1,
               timeInfo->tm_mday, timeInfo->tm_hour, timeInfo->tm_min,
               timeInfo->tm_sec);
#endif

  if (errorText)
    WriteTextToLogFile(file, errorText);

  if (g_crashDumpCallback) {
    char crashBuf[5200] = {};
    using CrashDumpFn = void (*)(char *, int);
    reinterpret_cast<CrashDumpFn>(g_crashDumpCallback)(crashBuf, 5200);
    if (crashBuf[0] != '\0')
      WriteTextToLogFile(file, crashBuf);
  }

  SStrCopy(g_lastLogPath, pathBuf, 260);

  return file;
}

std::string NormalizeTextForErrorLog(const char *text) {
  const std::size_t len = text ? SStrLen(text) : 0;

  std::string out;
  out.reserve(2 * len + 4);

  out += '\r';
  out += '\n';

  char prev = '\0';
  for (std::size_t i = 0; i < len; ++i) {
    const char ch = text[i];
    if (ch == '\n' && prev != '\r') {
      out += '\r';
    }
    out += ch;
    prev = ch;
  }

  if (len == 0 || text[len - 1] != '\n') {
    out += '\r';
    out += '\n';
  }

  return out;
}

bool WriteTextToLogFile(std::FILE *file, const char *text) {
  if (!file || !text) {
    return false;
  }

  const std::string normalized = NormalizeTextForErrorLog(text);
  const std::size_t written =
      std::fwrite(normalized.data(), 1, normalized.size(), file);
  return written == normalized.size();
}

void SetFatalErrorInterceptorForTests(
    std::function<void(std::uint32_t, const std::string &)> interceptor) {
  FatalErrorInterceptorStorage() = std::move(interceptor);
}

namespace detail {

void SetSErrAssertDebugBreakHandlerForTests(SErrAssertSignalHandler handler) {
  SErrAssertDebugBreakHandlerStorage() = handler;
}

void SetSErrAssertAbortHandlerForTests(SErrAssertSignalHandler handler) {
  SErrAssertAbortHandlerStorage() = handler;
}

void SetSErrAssertAbortSuppressedForTests(bool suppressed) {
  SErrAssertAbortSuppressedStorage() = suppressed;
}

void ResetSErrAssertHandlersForTests() {
  SErrAssertDebugBreakHandlerStorage() = nullptr;
  SErrAssertAbortHandlerStorage() = nullptr;
  SErrAssertAbortSuppressedStorage() = true;
}

void SetSErrLocalizedStringLoaderForTests(SErrLocalizedStringLoader loader) {
  SErrLocalizedStringLoaderStorage() = loader;
}

void ResetSErrLocalizedStringLoaderForTests() {
  SErrLocalizedStringLoaderStorage() = nullptr;
}

void SetDebuggerAttachmentApiForTests(DebuggerAttachmentLibraryLoader loader,
                                      DebuggerAttachmentProbeResolver resolver,
                                      DebuggerAttachmentLibraryReleaser releaser) {
  DebuggerAttachmentLibraryLoaderStorage() = loader;
  DebuggerAttachmentProbeResolverStorage() = resolver;
  DebuggerAttachmentLibraryReleaserStorage() = releaser;
}

void ResetDebuggerAttachmentApiForTests() {
  DebuggerAttachmentLibraryLoaderStorage() = nullptr;
  DebuggerAttachmentProbeResolverStorage() = nullptr;
  DebuggerAttachmentLibraryReleaserStorage() = nullptr;
}

void *SErrStateCriticalSectionTokenForTests() {
  return StateSErrCriticalSectionToken();
}

void *SErrDisplayCriticalSectionTokenForTests() {
  return DisplaySErrCriticalSectionToken();
}

void SErrLeaveCriticalForTests(void *criticalSection) {
  LeaveSErrCritical(criticalSection);
}

void ClearRegisteredCrashCallbacksForTests() {
  ScopedSErrCriticalSection lock(StateSErrCriticalSectionToken());
  ClearRegisteredCrashCallbacksLocked();
}

int SErrCriticalInitCounterForTests() {
  return g_critInitCounter.load(std::memory_order_acquire);
}

std::size_t RegisteredErrorModuleCountForTests() {
  return RegisteredErrorModules().size();
}

void SetSuppressDuplicateErrorsForTests(bool enabled) {
  g_suppressDuplicateErrors = enabled ? 1 : 0;
}

void ResetSuppressDuplicateErrorStateForTests() {
  g_suppressDuplicateErrors = 0;
  g_lastDisplayedSource = nullptr;
  g_lastDisplayedExitCode = 0;
}

}

}

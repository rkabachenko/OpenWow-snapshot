
#include "openwow/platform/diagnostics/crash_handler.h"

#include "openwow/core/client_misc.h"
#include "openwow/platform/adapters/win32/win32_error_log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <dbghelp.h>
#  pragma comment(lib, "dbghelp.lib")
#elif defined(__APPLE__) || defined(__linux__)
#  include <csignal>
#  include <cstdlib>
#  include <execinfo.h>
#  include <unistd.h>
#endif

namespace openwow::platform {

ErrorRingBuffer::ErrorRingBuffer(std::size_t capacity)
    : capacity_(capacity > 0 ? capacity : kDefaultCapacity) {
  buffer_.resize(capacity_);
}

void ErrorRingBuffer::Push(std::string_view message, std::uint8_t severity) {
  std::lock_guard lock(mutex_);
  auto& entry = buffer_[head_];
  entry.timestamp = std::chrono::steady_clock::now();
  entry.message = std::string(message);
  entry.severity = severity;
  head_ = (head_ + 1) % capacity_;
  if (count_ < capacity_) {
    ++count_;
  }
}

std::vector<RingBufferEntry> ErrorRingBuffer::Snapshot() const {
  std::lock_guard lock(mutex_);
  std::vector<RingBufferEntry> result;
  result.reserve(count_);
  if (count_ == 0) return result;

  std::size_t start = (count_ < capacity_) ? 0 : head_;
  for (std::size_t i = 0; i < count_; ++i) {
    std::size_t idx = (start + i) % capacity_;
    result.push_back(buffer_[idx]);
  }
  return result;
}

std::size_t ErrorRingBuffer::Size() const {
  std::lock_guard lock(mutex_);
  return count_;
}

void ErrorRingBuffer::Clear() {
  std::lock_guard lock(mutex_);
  head_ = 0;
  count_ = 0;
}

CrashHandler& CrashHandler::Get() {
  static CrashHandler instance;
  return instance;
}

#if defined(_WIN32)

static LPTOP_LEVEL_EXCEPTION_FILTER s_prev_filter = nullptr;

static LONG WINAPI CrashExceptionFilter(EXCEPTION_POINTERS* exception_info) {
  auto& handler = CrashHandler::Get();

  if (handler.IsInstalled()) {
    std::string sig_info = "Windows exception 0x";
    {
      std::ostringstream oss;
      oss << std::hex << std::uppercase
          << exception_info->ExceptionRecord->ExceptionCode;
      sig_info += oss.str();
    }

    auto stack = CrashHandler::CaptureStackTrace(64);
    (void)handler.WriteCrashReport(sig_info, stack);

    {
      auto ctx = handler.GetContext();
      std::filesystem::create_directories(ctx.logs_directory);

      auto now = std::chrono::system_clock::now();
      auto tt = std::chrono::system_clock::to_time_t(now);
      std::tm tm_buf{};
      localtime_s(&tm_buf, &tt);

      std::ostringstream fname;
      fname << ctx.logs_directory << "/minidump_"
            << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << ".dmp";

      HANDLE file = CreateFileA(fname.str().c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = exception_info;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          MiniDumpNormal, &mei, nullptr, nullptr);
        CloseHandle(file);
      }
    }
  }

  if (s_prev_filter) {
    return s_prev_filter(exception_info);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

#elif defined(__APPLE__) || defined(__linux__)

static struct sigaction s_prev_sigsegv;
static struct sigaction s_prev_sigabrt;
static struct sigaction s_prev_sigfpe;

static void CrashSignalHandler(int sig, siginfo_t* info, void* ) {
  auto& handler = CrashHandler::Get();

  if (handler.IsInstalled()) {
    std::string sig_info = CrashHandler::SignalName(sig);
    if (info && info->si_addr) {
      std::ostringstream oss;
      oss << " at address 0x" << std::hex
          << reinterpret_cast<std::uintptr_t>(info->si_addr);
      sig_info += oss.str();
    }

    auto stack = CrashHandler::CaptureStackTrace(64);
    (void)handler.WriteCrashReport(sig_info, stack);
  }

  struct sigaction sa{};
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sigaction(sig, &sa, nullptr);
  raise(sig);
}

#endif

void CrashHandler::Install(const CrashContext& context) {
  std::lock_guard lock(mutex_);
  context_ = context;
  if (context_.logs_directory.empty()) {
    context_.logs_directory = "Logs";
  }

#if defined(_WIN32)
  s_prev_filter = SetUnhandledExceptionFilter(CrashExceptionFilter);
#elif defined(__APPLE__) || defined(__linux__)
  struct sigaction sa{};
  sa.sa_sigaction = CrashSignalHandler;
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigemptyset(&sa.sa_mask);

  sigaction(SIGSEGV, &sa, &s_prev_sigsegv);
  sigaction(SIGABRT, &sa, &s_prev_sigabrt);
  sigaction(SIGFPE, &sa, &s_prev_sigfpe);
#endif

  installed_ = true;
}

void CrashHandler::Uninstall() {
  std::lock_guard lock(mutex_);

#if defined(_WIN32)
  if (installed_) {
    SetUnhandledExceptionFilter(s_prev_filter);
    s_prev_filter = nullptr;
  }
#elif defined(__APPLE__) || defined(__linux__)
  if (installed_) {
    sigaction(SIGSEGV, &s_prev_sigsegv, nullptr);
    sigaction(SIGABRT, &s_prev_sigabrt, nullptr);
    sigaction(SIGFPE, &s_prev_sigfpe, nullptr);
  }
#endif

  installed_ = false;
}

void CrashHandler::SetBuildVersion(std::string_view version) {
  std::lock_guard lock(mutex_);
  context_.build_version = std::string(version);
}

void CrashHandler::SetGpuInfo(std::string_view info) {
  std::lock_guard lock(mutex_);
  context_.gpu_info = std::string(info);
}

void CrashHandler::SetActiveState(std::string_view state) {
  std::lock_guard lock(mutex_);
  context_.active_state = std::string(state);
}

void CrashHandler::SetRealmInfo(std::string_view realm_name,
                                std::string_view realm_type) {
  std::lock_guard lock(mutex_);
  context_.realm_name = std::string(realm_name);
  context_.realm_type = std::string(realm_type);
}

void CrashHandler::ClearRealmInfo() {
  std::lock_guard lock(mutex_);
  context_.realm_name.clear();
  context_.realm_type.clear();
}

void CrashHandler::SetCurrentMap(std::string_view map) {
  std::lock_guard lock(mutex_);
  context_.current_map = std::string(map);
}

CrashContext CrashHandler::GetContext() const {
  std::lock_guard lock(mutex_);
  return context_;
}

std::string CrashHandler::FormatCrashReport(
    std::string_view signal_info,
    const std::vector<std::string>& stack) const {
  std::lock_guard lock(mutex_);

  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf{};
#if defined(_WIN32)
  localtime_s(&tm_buf, &tt);
#else
  localtime_r(&tt, &tm_buf);
#endif

  std::ostringstream oss;
  oss << "═══════════════════════════════════════════════════════════\n"
      << "  OpenWoW Crash Report\n"
      << "═══════════════════════════════════════════════════════════\n\n";

  oss << "Timestamp:     " << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << "\n";

  if (!context_.build_version.empty()) {
    oss << "Build Version: " << context_.build_version << "\n";
  }

  if (!context_.gpu_info.empty()) {
    oss << "GPU Info:      " << context_.gpu_info << "\n";
  }

  if (!context_.active_state.empty()) {
    oss << "Active State:  " << context_.active_state << "\n";
  }

  if (!context_.realm_name.empty() && !context_.realm_type.empty()) {
    oss << "Realm: " << context_.realm_name << " [" << context_.realm_type
        << "]\n";
  } else {
    oss << "Realm: ???\n";
  }

  std::array<char, 512> local_zone_line{};
  openwow::core::AppendLocalZoneInfoToCrashDump(nullptr, local_zone_line.data(),
                                                static_cast<int>(local_zone_line.size()));
  std::string_view trimmed_local_zone(local_zone_line.data());
  while (!trimmed_local_zone.empty() &&
         (trimmed_local_zone.back() == '\r' || trimmed_local_zone.back() == '\n')) {
    trimmed_local_zone.remove_suffix(1);
  }
  oss << trimmed_local_zone << "\n";

  if (!context_.current_map.empty()) {
    oss << "Current Map:   " << context_.current_map << "\n";
  }

  oss << "\n── Signal ──────────────────────────────────────────────────\n"
      << signal_info << "\n";

  oss << "\n── Stack Trace ─────────────────────────────────────────────\n";
  if (stack.empty()) {
    oss << "  (no stack trace available)\n";
  } else {
    for (std::size_t i = 0; i < stack.size(); ++i) {
      oss << "  #" << i << "  " << stack[i] << "\n";
    }
  }

  auto entries = ring_buffer_.Snapshot();
  if (!entries.empty()) {
    oss << "\n── Recent Messages (" << entries.size() << ") ──────────────────────────────\n";
    static constexpr const char* kSeverityLabels[] = {
        "INFO", "WARN", "ERROR", "FATAL"};
    for (const auto& entry : entries) {
      const char* sev_label =
          entry.severity < 4 ? kSeverityLabels[entry.severity] : "UNKNOWN";
      oss << "  [" << sev_label << "] " << entry.message << "\n";
    }
  }

  oss << "\n═══════════════════════════════════════════════════════════\n"
      << "  End of crash report\n"
      << "═══════════════════════════════════════════════════════════\n";

  return oss.str();
}

std::string CrashHandler::WriteCrashReport(
    std::string_view signal_info,
    const std::vector<std::string>& stack) const {

  if (pre_crash_callback_) {
    pre_crash_callback_();
  }

  std::string report = FormatCrashReport(signal_info, stack);

  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf{};
#if defined(_WIN32)
  localtime_s(&tm_buf, &tt);
#else
  localtime_r(&tt, &tm_buf);
#endif

  std::string logs_dir;
  {
    std::lock_guard lock(mutex_);
    logs_dir = context_.logs_directory;
  }

  std::error_code ec;
  std::filesystem::create_directories(logs_dir, ec);

  std::ostringstream fname;
  fname << logs_dir << "/crash_"
        << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << ".txt";
  std::string path = fname.str();

  std::ofstream ofs(path, std::ios::out | std::ios::trunc);
  if (ofs) {
    ofs << report;
    ofs.flush();
    return path;
  }
  return {};
}

std::vector<std::string> CrashHandler::CaptureStackTrace(int max_frames) {
  std::vector<std::string> result;

#if defined(__APPLE__) || defined(__linux__)
  constexpr int kMaxStackFrames = 128;
  int frame_count = std::min(max_frames, kMaxStackFrames);
  std::vector<void*> frames(static_cast<std::size_t>(frame_count));

  int count = backtrace(frames.data(), frame_count);
  if (count <= 0) return result;

  char** symbols = backtrace_symbols(frames.data(), count);
  if (!symbols) return result;

  result.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    result.emplace_back(symbols[i]);
  }
  free(symbols);

#elif defined(_WIN32)
  constexpr int kMaxStackFrames = 128;
  int frame_count = std::min(max_frames, kMaxStackFrames);
  std::vector<void*> frames(static_cast<std::size_t>(frame_count));

  USHORT count = CaptureStackBackTrace(0, static_cast<DWORD>(frame_count),
                                       frames.data(), nullptr);

  HANDLE process = GetCurrentProcess();
  const bool symbols_initialized = SymInit(process) != 0;

  constexpr std::size_t kMaxNameLen = 256;
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(
      malloc(sizeof(SYMBOL_INFO) + kMaxNameLen));
  if (!symbol) return result;

  symbol->MaxNameLen = kMaxNameLen - 1;
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

  result.reserve(count);
  for (USHORT i = 0; i < count; ++i) {
    auto address = reinterpret_cast<DWORD64>(frames[i]);
    if (symbols_initialized && SymFromAddr(process, address, nullptr, symbol)) {
      std::ostringstream oss;
      oss << symbol->Name << " [0x" << std::hex << address << "]";
      result.push_back(oss.str());
    } else {
      std::ostringstream oss;
      oss << "[0x" << std::hex << address << "]";
      result.push_back(oss.str());
    }
  }
  free(symbol);
  if (symbols_initialized) {
    SymCleanup(process);
  }
#endif

  return result;
}

std::string CrashHandler::SignalName(int signal_number) {
  switch (signal_number) {
#if defined(__APPLE__) || defined(__linux__)
    case SIGSEGV: return "SIGSEGV (Segmentation fault)";
    case SIGABRT: return "SIGABRT (Abort)";
    case SIGFPE:  return "SIGFPE (Floating-point exception)";
    case SIGBUS:  return "SIGBUS (Bus error)";
    case SIGILL:  return "SIGILL (Illegal instruction)";
#endif
    default: {
      std::ostringstream oss;
      oss << "Signal " << signal_number;
      return oss.str();
    }
  }
}

void CrashHandler::SetPreCrashCallback(PreCrashCallback cb) {
  std::lock_guard lock(mutex_);
  pre_crash_callback_ = std::move(cb);
}

void CrashHandler::Reset() {
  Uninstall();
  std::lock_guard lock(mutex_);
  context_ = {};
  ring_buffer_.Clear();
  pre_crash_callback_ = nullptr;
}

}

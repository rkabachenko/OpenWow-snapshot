
#include "openwow/core/client_init.h"
#include "openwow/core/client_init_internal.h"

#include "openwow/audio/lifecycle/audio_system_shutdown.h"
#include "openwow/audio/codecs/ogg/ogg_decompress.h"
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/client_misc.h"
#include "openwow/core/cobject_heap.h"
#include "openwow/core/console.h"
#include "openwow/core/cvar.h"
#include "openwow/runtime/scheduling/evt_sched.h"
#include "openwow/core/fpu_control.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/game_subsystems.h"
#include "openwow/core/gxcvar.h"
#include "openwow/core/legacy_buffered_log_file.h"
#include "openwow/platform/system/os_system_info.h"
#include "openwow/core/storm_alloc.h"
#include "openwow/core/storm_cmd.h"
#include "openwow/core/storm_comp.h"
#include "openwow/core/storm_component.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_path.h"
#include "openwow/core/storm_region.h"
#include "openwow/core/storm_string.h"
#include "openwow/core/storm_sync.h"
#include "openwow/core/streaming_storage.h"
#include "openwow/data/archive_system.h"
#include "openwow/data/dbc_loader.h"
#include "openwow/data/login_resource_validator.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/data/streaming_init.h"
#include "openwow/debug/diagnostics/debug_console.h"
#include "openwow/game/client_config.h"
#include "openwow/platform/diagnostics/crash_handler.h"
#include "openwow/platform/process/os_platform.h"
#include "openwow/platform/process/os_platform_internal.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/vfs/sfile_core.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
#include <StormLib.h>
#endif

#if defined(_MSC_VER)
using OpenWoWInvalidParameterHandler = void(__cdecl *)(const wchar_t *, const wchar_t *,
                                                       const wchar_t *, unsigned int,
                                                       std::uintptr_t);
extern "C" OpenWoWInvalidParameterHandler
_set_invalid_parameter_handler(OpenWoWInvalidParameterHandler handler);
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <imm.h>
#include <windows.h>
#include "openwow/foundation/compiler/printf_format.h"
#endif

namespace openwow::core {

static int TimingInfoCommandHandler();
static void ConsoleLog(const char *fmt, ...);

namespace {

struct WoWMainInitRuntimeState {
  bool storm_setting_10 = false;
  bool storm_setting_11 = false;
  bool os_initialized = false;
  bool timer_initialized = false;
  bool blizzard_base_components_initialized = false;
  std::int64_t timer_baseline = 0;
  std::int64_t timer_offset = 0;
#if defined(_WIN32)
  HANDLE startup_event_handle = nullptr;
#endif
};

struct StartupFiberRuntimeState {
  void *main_fiber = nullptr;
  std::function<int()> entry_point;
};

struct LoginSurveyTelemetryBootstrapState {
  bool loaded = false;
  LoginSurveyTelemetryBootstrapRecord record{};
};

WoWMainInitRuntimeState &MutableWoWMainInitRuntimeState() {
  static WoWMainInitRuntimeState state;
  return state;
}

StartupFiberRuntimeState &MutableStartupFiberRuntimeState() {
  static StartupFiberRuntimeState state;
  return state;
}

LoginSurveyTelemetryBootstrapState &MutableLoginSurveyTelemetryBootstrapState() {
  static LoginSurveyTelemetryBootstrapState state;
  return state;
}

constexpr std::size_t kLoginSurveyTelemetryPayloadBytes = 28;
constexpr std::uint16_t kLoginSurveyTelemetryRecordTag = 20;

void ResetLoginSurveyTelemetryBootstrapState() {
  MutableLoginSurveyTelemetryBootstrapState() = {};
}

void LoadLoginSurveyTelemetryBootstrapState() {
  ResetLoginSurveyTelemetryBootstrapState();

  void *archive = nullptr;
  if (!openwow::vfs::SFileOpenArchiveWrapped("Data\\base.MPQ", 100, 0x800u, &archive) ||
      archive == nullptr) {
    return;
  }

  void *telemetry_data = nullptr;
  std::size_t telemetry_size = kLoginSurveyTelemetryPayloadBytes;
  if (openwow::vfs::SFileOpenFileAndLoadData(archive, "telemetry.dat", &telemetry_data,
                                             &telemetry_size, 0, 0, 0) &&
      telemetry_data != nullptr) {
    if (telemetry_size == kLoginSurveyTelemetryPayloadBytes) {
      auto &state = MutableLoginSurveyTelemetryBootstrapState();
      state.loaded = true;
      state.record[0] = static_cast<std::uint8_t>(kLoginSurveyTelemetryRecordTag & 0xFFu);
      state.record[1] = static_cast<std::uint8_t>((kLoginSurveyTelemetryRecordTag >> 8) & 0xFFu);
      std::memcpy(state.record.data() + 4, telemetry_data, kLoginSurveyTelemetryPayloadBytes);
    }
    (void)openwow::vfs::SFileFreeLoadedData(telemetry_data);
  }

  (void)openwow::vfs::SFileCloseArchiveWrapped(archive);
}

openwow::data::WowClientDB &StartupErrorTable() {
  static openwow::data::WowClientDB table = [] {
    openwow::data::WowClientDB db{};
    db.max_id = -1;
    db.min_id = 0x0FFFFFFF;
    return db;
  }();
  return table;
}

std::int64_t ReadSharedTimerNowNsSince2000() {
  return GameClock::GetCurrentTimeNsSince2000();
}

std::function<std::int64_t()> &InitTimerTimeSource() {
  static std::function<std::int64_t()> source = []() { return ReadSharedTimerNowNsSince2000(); };
  return source;
}

std::int64_t ReadInitTimerTimeNsSince2000() {
  return InitTimerTimeSource()();
}

std::string GetCommandLineArgument(std::string_view command_line, int index) {
  int current = 0;
  std::size_t pos = 0;

  while (pos < command_line.size()) {
    while (pos < command_line.size() &&
           std::isspace(static_cast<unsigned char>(command_line[pos]))) {
      ++pos;
    }
    if (pos >= command_line.size()) {
      break;
    }

    bool quoted = false;
    std::string token;
    while (pos < command_line.size()) {
      const char ch = command_line[pos];
      if (ch == '"') {
        quoted = !quoted;
        ++pos;
        continue;
      }
      if (!quoted && std::isspace(static_cast<unsigned char>(ch))) {
        break;
      }
      token.push_back(ch);
      ++pos;
    }

    if (current == index) {
      return token;
    }

    while (pos < command_line.size() &&
           std::isspace(static_cast<unsigned char>(command_line[pos]))) {
      ++pos;
    }
    ++current;
  }

  return {};
}

std::filesystem::path BuildNativePath(std::string path) {

  std::replace(path.begin(), path.end(), '\\',
               static_cast<char>(std::filesystem::path::preferred_separator));
  return std::filesystem::path(std::move(path));
}

std::filesystem::path ResolveRunOnceFilesystemPath(const std::string &filename) {
  const std::filesystem::path path(filename);
  if (path.is_absolute()) {
    return path;
  }

  const auto &startup_state = openwow::data::GetStartupFileSystemState();
  if (!startup_state.executable_base_path.empty()) {
    return (BuildNativePath(startup_state.executable_base_path) / path).lexically_normal();
  }

  std::error_code ec;
  const std::filesystem::path current_directory = std::filesystem::current_path(ec);
  if (ec) {
    return path;
  }

  return (current_directory / path).lexically_normal();
}

std::filesystem::path ResolveStartupFilesystemPath(const std::string &path_text) {
  const std::filesystem::path path = BuildNativePath(path_text);
  if (path.is_absolute()) {
    return path;
  }

  const auto &startup_state = openwow::data::GetStartupFileSystemState();
  if (!startup_state.executable_base_path.empty()) {
    return (BuildNativePath(startup_state.executable_base_path) / path).lexically_normal();
  }

  std::error_code ec;
  const auto current_directory = std::filesystem::current_path(ec);
  if (ec) {
    return path;
  }

  return (current_directory / path).lexically_normal();
}

std::array<char, 5> &MutableClientInitLocaleTag() {
  static std::array<char, 5> locale = {'e', 'n', 'U', 'S', '\0'};
  return locale;
}

int ParseSignedDecimalPrefix(std::string_view text) {
  if (text.empty()) {
    return 0;
  }

  char *end = nullptr;
  std::string owned(text);
  const long value = std::strtol(owned.c_str(), &end, 10);
  if (end == owned.c_str()) {
    return 0;
  }
  return static_cast<int>(value);
}

const char *TimingMethodNameFromValue(const int value) {
  return openwow::core::TimingMethodNameFromIdaValue(value).data();
}

void StoreClientInitLocaleTag(const std::string_view locale) {
  auto &tag = MutableClientInitLocaleTag();
  tag.fill('\0');
  constexpr std::string_view fallback = "enUS";
  const std::string_view source = locale.size() >= 4 ? locale.substr(0, 4) : fallback;
  std::copy_n(source.begin(), 4, tag.begin());
}

std::string CurrentClientInitLocaleTag() {
  return std::string(MutableClientInitLocaleTag().data());
}

std::string GetClientInitCVar(const std::string &name) {
  return openwow::ui::game::CVarSystem::Instance().GetCVar(name);
}

void SetClientInitCVar(const std::string &name, const std::string &value, const bool force = true) {
  openwow::ui::game::CVarSystem::Instance().SetCVar(name, value, force);
}

std::uint32_t GetLogicalProcessorCount() {
  auto &detector = openwow::core::OsSystemInfoDetector::Instance();
  detector.Init();
  const auto count = detector.GetInfo().processorCount;
  return count == 0 ? 1u : count;
}

bool ValidateClientInitTimingMethod(const std::string &, const std::string &old_value,
                                    const std::string &new_value) {
  const int requested = ParseSignedDecimalPrefix(new_value);
  if (requested < 0 || requested > 2) {
    ConsoleLog("'%s' is not a valid timing method. Valid methods are:", new_value.c_str());
    for (int i = 0; i <= 2; ++i) {
      ConsoleLog("  %d - %s", i, TimingMethodNameFromValue(i));
    }
    return false;
  }

  if (requested != ParseSignedDecimalPrefix(old_value) &&
      GetClientInitCVar("timingTestError") != "0") {
    SetClientInitCVar("timingTestError", "0");
  }

  return true;
}

bool ValidateClientInitProcessAffinityMask(const std::string &, const std::string &,
                                           const std::string &new_value) {
  const std::uint32_t processor_count = GetLogicalProcessorCount();
  const std::uint32_t shift = (32u - processor_count) & 31u;
  const std::uint32_t max_mask = 0xFFFFFFFFu >> shift;
  const std::uint32_t requested_mask =
      static_cast<std::uint32_t>(ParseSignedDecimalPrefix(new_value));
  if (requested_mask <= max_mask) {
    return true;
  }

  ConsoleLog("Specified mask %08x is greater than the maximum allowable value of %08x",
             requested_mask, max_mask);
  return false;
}

void SyncClientInitLocaleState(const std::string &locale) {
  StoreClientInitLocaleTag(locale);
  SetClientInitCVar("locale", locale);
  SetClientInitCVar("textLocale", locale);

  const std::string audio_locale = GetClientInitCVar("useEnglishAudio") == "0" ? locale : "enUS";
  SetClientInitCVar("audioLocale", audio_locale);
  openwow::game::ClientConfig::Get().SetLocale(locale);
}

std::vector<std::string> CollectRunOnceEntryNames() {
  std::vector<std::string> entry_names;
  openwow::vfs::SFileFindFiles(
      "WTF\\", "RunOnce*.wtf",
      [&entry_names](const openwow::vfs::SFileFindData &entry) {
        if (entry.entry_name) {
          entry_names.emplace_back(entry.entry_name);
        }
        return false;
      },
      true);

  std::sort(entry_names.begin(), entry_names.end(),
            [](const std::string &lhs, const std::string &rhs) {
              return SStrCmpNoCaseCollate(lhs.c_str(), rhs.c_str(), 0x7FFFFFFF) < 0;
            });
  return entry_names;
}

void LoadRunOnceConfigFile(const std::string &filename) {
  ida::CVar_LoadFromFile(filename);
}

void ApplyWoWMainInitStormSetting(int command, int value) {
  auto &state = MutableWoWMainInitRuntimeState();
  switch (command) {
  case 10:
    state.storm_setting_10 = value != 0;
    break;
  case 11:
    state.storm_setting_11 = value != 0;
    break;
  default:
    break;
  }
}

int ExecuteCurrentStartupEntryPoint() {
  auto &state = MutableStartupFiberRuntimeState();
  if (state.entry_point) {
    return state.entry_point();
  }
  return WoW_GameEntry();
}

#if defined(_WIN32)
VOID CALLBACK StartupFiberEntryDispatcher(void *parameter) {
  WoW_FiberEntry(static_cast<int *>(parameter));
}
#endif

}

namespace detail {

void ProcessRunOnceFilesWithCallback(const std::function<void(const std::string &)> &callback) {
  if (!callback) {
    return;
  }

  const auto entry_names = CollectRunOnceEntryNames();
  for (const auto &entry_name : entry_names) {
    callback(entry_name);
  }
}

bool DeleteRunOnceFileIfPresent(const std::string &filename) {
  if (filename.empty()) {
    return false;
  }

  auto file_exists = [](const std::filesystem::path &path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
  };
  auto delete_file_ignoring_result = [](const std::filesystem::path &path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  };

  const std::filesystem::path direct_path = ResolveRunOnceFilesystemPath(filename);
  if (file_exists(direct_path)) {
    delete_file_ignoring_result(direct_path);
    return true;
  }

  const auto wtf_path = ResolveRunOnceFilesystemPath("WTF/" + filename);
  if (!file_exists(wtf_path)) {
    return false;
  }

  delete_file_ignoring_result(wtf_path);
  return true;
}

}

static uint32_t dword_B2F9A4 = 0;

static uint32_t dword_B2F9D8 = 0;

static uint32_t dword_B2F994 = 0;

static uint32_t dword_AB6360 = 0;

static uint32_t dword_AB635C = 0;

static std::uintptr_t dword_AB6370 = 0;

static std::atomic<bool> g_client_shutdown_requested{false};

static int mainCRTStartup() {

  return WoW_GameEntry();
}

static void BaseSystemInit() {

  openwow::core::InitMemorySystem();
  auto &runtime_state = MutableWoWMainInitRuntimeState();
  if (!runtime_state.blizzard_base_components_initialized) {
    auto &components = openwow::core::BlizzardComponent::Instance();
    components.InitMemory();
    components.InitLog();
    runtime_state.blizzard_base_components_initialized = true;
  }
  openwow::core::InitMinidumpSettings();
  openwow::core::InitCriticalSections();
  openwow::vfs::SetGenericErrorDisplayCallback(openwow::vfs::GenericErrorDisplay);
}

static void OS_CreateEventA(const char * , int ) {

#if defined(_WIN32)
  auto &state = MutableWoWMainInitRuntimeState();
  if (!state.startup_event_handle) {
    state.startup_event_handle =
        ::CreateEventA(nullptr, FALSE, FALSE, "Blizzard Entertainment World of Warcraft");
  }
#endif
}

static void SetOSInitializedFlag() {
  MutableWoWMainInitRuntimeState().os_initialized = true;
}

static void InitFileSystem() {
  const auto command_line = openwow::platform::OS_GetCommandLine();
  const auto base_path_from_args = GetCommandLineArgument(command_line, 2);
  openwow::data::InitializeStartupFileSystem(
      {
          .command_line_base_path = base_path_from_args,
          .module_directory = openwow::platform::OS_GetModuleDirectory(),
          .archive_data_path = "Data",
      },
      [](const std::string &path) {
        return openwow::vfs::FileSystem_SetWorkingDirectoryChecked(path.c_str());
      });
}

static bool ReadRegistryDword(const char *key, const char *value, int type, uint32_t *out) {
  return openwow::core::ReadRegistryValue(key, value, type, out);
}

static void WriteRegistryDword(const char *key, const char *value, int type, uint32_t data) {
  (void)openwow::core::WriteRegistryValue(key, value, static_cast<std::uint8_t>(type), data);
}

static void RunEventScheduler() {
  openwow::core::InitEventScheduler_Thunk();
}

static void FinalProcessCleanup() {
  openwow::core::SetCrashDumpCallback(nullptr);
  auto &runtime_state = MutableWoWMainInitRuntimeState();
  if (runtime_state.blizzard_base_components_initialized) {
    auto &components = openwow::core::BlizzardComponent::Instance();
    components.Shutdown(openwow::core::BlizzardComponents::kLog);
    components.Shutdown(openwow::core::BlizzardComponents::kMemory);
    runtime_state.blizzard_base_components_initialized = false;
  } else {

    openwow::core::LegacyBufferedLogFile::ShutdownAll();
  }
  openwow::core::SErrShutdown();
  openwow::platform::CrashHandler::Get().Uninstall();
}

static void RegisterAtExitHandlers() {

  openwow::core::RegionSystem::Instance().ClearAll();
  openwow::core::StormCmd::Instance().Shutdown();
  openwow::core::SCompPool::Instance().Shutdown();

  static std::atomic_bool final_cleanup_registered{false};
  if (!final_cleanup_registered.exchange(true, std::memory_order_acq_rel)) {
    (void)std::atexit(&FinalProcessCleanup);
  }
}

static bool IsOnlineMode() {

  return openwow::data::IsOnlineModeActive();
}

static void WoW_MainInit__callee_41D0B0() {
  openwow::vfs::SoundCache_Shutdown();
}

static void InitTimerBaseline(const int value) {
  auto &state = MutableWoWMainInitRuntimeState();
  if (!state.timer_initialized) {
    state.timer_initialized = true;
    state.timer_baseline = ReadInitTimerTimeNsSince2000();
  }
  if (value == 0) {
    state.timer_offset = ReadInitTimerTimeNsSince2000() - state.timer_baseline;
  }
}

static void nullsub_3() {
}

static void SetInvalidParameterHandler(void (*handler)()) {
#if defined(_MSC_VER)
  _set_invalid_parameter_handler(reinterpret_cast<OpenWoWInvalidParameterHandler>(handler));
#else
  (void)handler;
#endif
}

static void InitFPU_ClearAndControl() {
  (void)openwow::core::InitFPU();
}

static void ret_zero_427A90() {

}

static constexpr std::array<openwow::core::CmdDefInitEntry, 17> kClientInitCommandLineDefinitions{{
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kResolution800x600),
     "800x600", nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kResolution1024x768),
     "1024x768", nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kResolution1280x960),
     "1280x960", nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kResolution1280x1024),
     "1280x1024", nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kResolution1600x1200),
     "1600x1200", nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kBitDepth16), "16bit",
     nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kUpToDate), "uptodate",
     nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kNoSound), "nosound", nullptr,
     nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kSoundChaos), "soundchaos",
     nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kNoFixLag), "nofixlag",
     nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kDepth16), "d16", nullptr,
     nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kDepth24), "d24", nullptr,
     nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kDepth32), "d32", nullptr,
     nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kWindowed), "windowed",
     nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kHwDetect), "hwdetect",
     nullptr, nullptr},
    {0x00000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kConsole), "console", nullptr,
     nullptr},
    {0x20000u, static_cast<uint32_t>(openwow::core::StartupCommandId::kGxOverride), "gxoverride",
     nullptr, nullptr},
}};

static void RegisterClientInitCommandLineDefinitions() {
  openwow::core::StormCmd::Instance().InitErrorStrings(kClientInitCommandLineDefinitions);
}

static void InitCommandLine() {
  openwow::core::InitCommandLineRetail();
}

static void InitPatchList(const char *filename) {
  openwow::data::ArchiveSystemCallbacks callbacks{};
  callbacks.is_online_mode = []() { return IsOnlineMode(); };
  callbacks.init_timer_baseline = InitTimerBaseline;
  callbacks.init_online_sound_cache = []() {
    openwow::vfs::SFile2_InitSoundCache(
        reinterpret_cast<void *>(openwow::audio::OggVorbis_DecodeToWAV));
  };

  if (!filename) {
    openwow::data::InitPatchList(nullptr, callbacks);
    return;
  }

  std::string manifest_file = filename;
  openwow::data::InitPatchList(manifest_file.data(), callbacks);
}

static bool SubDirectoryExists(const char *path) {
  if (!path || !*path) {
    return false;
  }

  std::error_code ec;
  return std::filesystem::is_directory(ResolveStartupFilesystemPath(path), ec);
}

static void ClientInit__callee_6B0190() {
  LoadLoginSurveyTelemetryBootstrapState();
}

static void Console_RegisterBasicCommands() {
  openwow::core::ida::Console_RegisterBasicCommands();
}

static void LocalStrCopy(char *dest, const char *src, int maxlen) {

  if (!dest || !src || maxlen <= 0) {
    return;
  }

  std::size_t copy_len = std::strlen(src);
  const std::size_t max_copy = static_cast<std::size_t>(maxlen - 1);
  if (copy_len > max_copy) {
    copy_len = max_copy;
  }

  std::memcpy(dest, src, copy_len);
  dest[copy_len] = '\0';
}

static void ClientInit__callee_421B50(const char *path) {
  openwow::vfs::ClientInit_SetLocaleDataPath(path);
}

static void sub_423D70() {

}

static void LoadAllArchives() {
  openwow::data::ArchiveSystemCallbacks callbacks{};
  callbacks.cvar_get_string = [](const std::string &name) { return GetClientInitCVar(name); };
  callbacks.cvar_set_string = [](const std::string &name, const std::string &value) {
    SetClientInitCVar(name, value);
  };
  callbacks.is_online_mode = []() { return IsOnlineMode(); };
  callbacks.read_registry_value = [](const char *key, const char *value_name, int type, void *out) {
    auto *dword_out = static_cast<uint32_t *>(out);
    return dword_out != nullptr && ReadRegistryDword(key, value_name, type, dword_out);
  };
  openwow::data::LoadAllArchives(callbacks);
}

static void InitSCritical(int ) {
  openwow::core::InitSCriticalRetail();
}

static void InitEvtSchedulerConfig_ClientInit(int a1, int a2) {
  openwow::core::InitEvtSchedulerConfig(static_cast<std::uint32_t>(a1), a2);
}

static void OS_SetTimingMethod(uint32_t method) {
  const std::string method_text = std::to_string(method);
  auto &timing_cvars = openwow::ui::game::CVarSystem::Instance();
  if (!timing_cvars.Exists("timingMethod")) {
    timing_cvars.RegisterCVar("timingMethod", method_text,
                              openwow::ui::game::CVarFlags::Archive,
                              "Performance timing method");
  } else {
    (void)timing_cvars.SetCVar("timingMethod", method_text, true);
  }

  auto &clock = GameClock::Instance();
  clock.Init(openwow::core::TimingMethodFromCVarValue(static_cast<int>(method)));
}

static void Console_RegisterCommand(const char *name, void *handler, int , int ) {
  if (!name || !handler) {
    return;
  }

  auto &console = openwow::debug::DebugConsole::Get();
  if (std::strcmp(name, "timingInfo") == 0 &&
      handler == reinterpret_cast<void *>(TimingInfoCommandHandler)) {
    console.RegisterCommand("timingInfo", "Show timing method diagnostics",
                            [](const std::vector<std::string> &) -> std::string {
                              TimingInfoCommandHandler();
                              return {};
                            });
  }
}

static int OS_GetTimerError() {
  return GameClock::Instance().ValidationResult();
}

static void InitErrorTableAndCacheBounds(void *table, const char * , uint32_t line) {
  auto *db = table ? static_cast<openwow::data::WowClientDB *>(table) : &StartupErrorTable();
  openwow::data::InitErrorTable(db, ".\\Client.cpp", line);
  dword_AB635C = static_cast<uint32_t>(db->max_id);
  dword_AB6360 = static_cast<uint32_t>(db->min_id);
  dword_AB6370 = reinterpret_cast<std::uintptr_t>(db->index);
}

static void ConsoleDeviceInitialize(const char *title) {
  openwow::core::ida::ConsoleDeviceInitialize(title);
}

static void OS_InitPerfCounters(const openwow::core::TimingMethod requested_method) {
  GameClock::Instance().Init(requested_method);
}

static uint32_t CreateMainEventContext(int a1, int init_callback, int shutdown_callback, int a4,
                                       int a5) {
  return static_cast<uint32_t>(openwow::core::CreateEventContext_Thunk(
      a1, init_callback, shutdown_callback, a4, static_cast<uint32_t>(a5)));
}

static void ClientInit__callee_4036B0(int a1, int , int , int ) {
  openwow::data::ArchiveSystemCallbacks callbacks{};
  callbacks.cvar_get_string = [](const std::string &name) { return GetClientInitCVar(name); };
  callbacks.is_online_mode = []() { return IsOnlineMode(); };
  callbacks.read_registry_value = [](const char *key, const char *value_name, int type, void *out) {
    auto *dword_out = static_cast<uint32_t *>(out);
    return dword_out != nullptr && ReadRegistryDword(key, value_name, type, dword_out);
  };
  openwow::data::InitStreamingSubsystem(static_cast<char>(a1), callbacks);
}

static void ApplyPostInitLogFlags() {
  ::openwow::core::SetLogFlags(static_cast<std::uint32_t>(0),
                               static_cast<std::uint8_t>(8));
}

static void PostInitErrorCheck__callee_86D0C0() {
  void *active_window = openwow::platform::OS_GetActiveWindow(0);
#if defined(_WIN32)
  if (active_window != nullptr) {
    ::ImmAssociateContextEx(static_cast<HWND>(active_window), nullptr, 0x10u);
  }
#else
  (void)active_window;
#endif
}

static void PostInitErrorCheck__callee_86D440() {

}

static void AudioSystem_Shutdown() {
  ::openwow::audio::AudioSystem_Shutdown();
}

static void PostInitErrorCheck__callee_769D40() {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  cvars.ApplyPendingValue("videoOptionsVersion");

}

static void CVar_Cleanup() {
  ::openwow::core::ida::CVar_Cleanup();
}

static void PostInitErrorCheck__callee_7685C0() {
  auto &console = openwow::debug::DebugConsole::Get();
  const auto command_names = console.GetCommandNames();
  for (const auto &name : command_names) {
    console.UnregisterCommand(name);
  }
}

static void Cleanup_FinalizeDataPreloadRuntime() {

  openwow::vfs::StopDataPreloadRuntimeWorkerIfActive();
}

static void sub_457680() {
  openwow::core::StreamingStorage::Instance().Shutdown();
}

static int ShowMessageBox(int , int a2, const char *text, const char *title) {
  return openwow::platform::ShowMessageBox(
      text ? text : "", title ? title : "",
      openwow::platform::detail::DecodeLegacyMessageBoxButtons(a2));
}

static std::optional<std::string> ReadBlizzardComponentXmlResource() {
#if defined(_WIN32)
  HRSRC resource = FindResourceExA(nullptr, "BLIZZARDCOMPONENT", "BLIZZARDCOMPONENT", 0);
  if (!resource) {
    return std::nullopt;
  }

  HGLOBAL loaded = LoadResource(nullptr, resource);
  if (!loaded) {
    return std::nullopt;
  }

  const void *bytes = LockResource(loaded);
  if (!bytes) {
    return std::nullopt;
  }

  const DWORD size = SizeofResource(nullptr, resource);
  return std::string(static_cast<const char *>(bytes), static_cast<std::size_t>(size));
#elif defined(__APPLE__)

  const std::filesystem::path executable_path =
      openwow::platform::OS_GetModulePath();
  const std::filesystem::path contents_path =
      executable_path.parent_path().parent_path();
  if (contents_path.filename() != "Contents") {
    return std::nullopt;
  }

  const std::filesystem::path resource_path =
      contents_path / "Resources" / "BlizzardComponent.xml";
  std::error_code error;
  const std::uintmax_t resource_size =
      std::filesystem::file_size(resource_path, error);
  constexpr std::uintmax_t kMaxBlizzardComponentXmlBytes = 16u * 1024u * 1024u;
  if (error || resource_size > kMaxBlizzardComponentXmlBytes) {
    return std::nullopt;
  }

  std::ifstream input(resource_path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::string xml(static_cast<std::size_t>(resource_size), '\0');
  if (resource_size != 0
      && !input.read(xml.data(), static_cast<std::streamsize>(xml.size()))) {
    return std::nullopt;
  }
  return xml;
#else
  return std::nullopt;
#endif
}

static bool fn_IsWoWTProduct() {

  return openwow::data::IsWoWTProduct(ReadBlizzardComponentXmlResource);
}

static std::string GetModuleFilePath() {
  return openwow::platform::OS_GetModulePath();
}

#if defined(_WIN32)
static std::wstring WidenAnsiString(const char *text) {
  if (!text) {
    return {};
  }

  const int wide_chars = ::MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
  if (wide_chars <= 0) {
    return {};
  }

  std::wstring wide(static_cast<std::size_t>(wide_chars), L'\0');
  if (::MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), wide_chars) != wide_chars) {
    return {};
  }

  wide.pop_back();
  return wide;
}
#endif

static bool WriteRegistryString(const char *key, const char *value, std::uint8_t flags,
                                const char *data) {
#if defined(_WIN32)
  if (!key || !*key || !value || !*value || !data) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return false;
  }

  std::string full_path;
  if ((flags & 0x10u) == 0) {
    full_path =
        (flags & 0x02u) != 0 ? "Software\\Battle.net\\" : "Software\\Blizzard Entertainment\\";
  }
  full_path += key;

  const std::wstring wide_path = WidenAnsiString(full_path.c_str());
  const std::wstring wide_value = WidenAnsiString(value);
  const std::wstring wide_data = WidenAnsiString(data);
  if (wide_path.empty() || wide_value.empty() || wide_data.empty()) {
    return false;
  }

  HKEY handle = nullptr;
  const HKEY root = (flags & 0x04u) != 0 ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
  LSTATUS status = ::RegCreateKeyExW(root, wide_path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                                     &handle, nullptr);
  if (status != ERROR_SUCCESS) {
    ::SetLastError(status);
    return false;
  }

  const DWORD byte_count = static_cast<DWORD>((wide_data.size() + 1) * sizeof(wchar_t));
  status = ::RegSetValueExW(handle, wide_value.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE *>(wide_data.c_str()), byte_count);
  if (status == ERROR_SUCCESS && (flags & 0x08u) != 0) {
    status = ::RegFlushKey(handle);
  }

  const LSTATUS close_status = ::RegCloseKey(handle);
  if (status == ERROR_SUCCESS) {
    status = close_status;
  }
  if (status == ERROR_SUCCESS) {
    return true;
  }

  ::SetLastError(status);
  return false;
#else
  (void)key;
  (void)value;
  (void)flags;
  (void)data;
  return false;
#endif
}

static bool ReadRegistryString(const char *key, const char *value_name, std::uint8_t flags,
                               std::string *out) {
#if defined(_WIN32)
  if (!key || !*key || !value_name || !*value_name || !out) {
    if (out != nullptr) {
      out->clear();
    }
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return false;
  }

  out->clear();

  std::string full_path;
  if ((flags & 0x10u) == 0) {
    full_path =
        (flags & 0x02u) != 0 ? "Software\\Battle.net\\" : "Software\\Blizzard Entertainment\\";
  }
  full_path += key;

  const std::wstring wide_path = WidenAnsiString(full_path.c_str());
  const std::wstring wide_value_name = WidenAnsiString(value_name);
  if (wide_path.empty() || wide_value_name.empty()) {
    return false;
  }

  auto try_root = [&](HKEY root, LSTATUS *status_out) {
    HKEY handle = nullptr;
    LSTATUS status = ::RegOpenKeyExW(root, wide_path.c_str(), 0, KEY_READ, &handle);
    if (status != ERROR_SUCCESS) {
      *status_out = status;
      return false;
    }

    DWORD reg_type = 0;
    DWORD byte_count = 0;
    status = ::RegQueryValueExW(handle, wide_value_name.c_str(), nullptr, &reg_type, nullptr,
                                &byte_count);
    if (status == ERROR_SUCCESS && reg_type != REG_SZ && reg_type != REG_EXPAND_SZ) {
      status = ERROR_DATATYPE_MISMATCH;
    }

    if (status == ERROR_SUCCESS) {
      std::wstring wide_data(
          std::max<std::size_t>(1u, static_cast<std::size_t>(byte_count / sizeof(wchar_t)) + 1u),
          L'\0');
      DWORD read_size = static_cast<DWORD>(wide_data.size() * sizeof(wchar_t));
      status = ::RegQueryValueExW(handle, wide_value_name.c_str(), nullptr, &reg_type,
                                  reinterpret_cast<BYTE *>(wide_data.data()), &read_size);
      if (status == ERROR_SUCCESS) {
        wide_data.back() = L'\0';
        const int narrow_size =
            ::WideCharToMultiByte(CP_ACP, 0, wide_data.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (narrow_size > 0) {
          std::string narrow(static_cast<std::size_t>(narrow_size), '\0');
          if (::WideCharToMultiByte(CP_ACP, 0, wide_data.c_str(), -1, narrow.data(), narrow_size,
                                    nullptr, nullptr) == narrow_size) {
            narrow.pop_back();
            *out = std::move(narrow);
          } else {
            status = static_cast<LSTATUS>(::GetLastError());
          }
        } else {
          status = static_cast<LSTATUS>(::GetLastError());
        }
      }
    }

    const LSTATUS close_status = ::RegCloseKey(handle);
    if (status == ERROR_SUCCESS) {
      status = close_status;
    }
    *status_out = status;
    return status == ERROR_SUCCESS;
  };

  LSTATUS status = ERROR_FILE_NOT_FOUND;
  if ((flags & 0x04u) == 0 && try_root(HKEY_CURRENT_USER, &status)) {
    return true;
  }
  if ((flags & 0x01u) == 0 && try_root(HKEY_LOCAL_MACHINE, &status)) {
    return true;
  }

  ::SetLastError(status);
  return false;
#else
  (void)key;
  (void)value_name;
  (void)flags;
  if (out != nullptr) {
    out->clear();
  }
  return false;
#endif
}

static void Client_BuildBugReport(char* buf, int buf_size) {
  if (!buf || buf_size <= 0) return;
  buf[0] = '\0';

  const std::string main_report = BuildBugReport();

  std::string full_report;
  full_report.reserve(256 + main_report.size());

  full_report += "WoWBuild: ";
  full_report += std::to_string(kWoWBuild);
  full_report += "\r\n";

  full_report += "OS: ";
  full_report += openwow::platform::OS_GetOSVersionString();
  full_report += "\r\n";

  {

    std::size_t skip_end = 0;
    if (main_report.compare(0, 9, "WoWBuild:") == 0) {
      skip_end = main_report.find("\r\n");
      if (skip_end != std::string::npos) {
        skip_end += 2;
      }
    }
    full_report.append(main_report, skip_end, std::string::npos);
  }

  full_report += "Error: Unhandled exception\r\n";

  const int copy_len =
      std::min(static_cast<int>(full_report.size()), buf_size - 1);
  std::memcpy(buf, full_report.data(), static_cast<std::size_t>(copy_len));
  buf[copy_len] = '\0';
}

static void GameSubsystemsInit() {
  openwow::core::RegisterVideoDefaultsModeCallback(&openwow::core::DispatchDisplaySettingsCallback);
  if (openwow::core::ida::ShouldReplayStartupDisplaySettings()) {
    for (std::uint32_t mode = 0; mode < 3; ++mode) {
      openwow::core::DispatchDisplaySettingsCallback(mode);
    }
  }

}

static void GameSubsystemsShutdown() {
  openwow::core::UnregisterVideoDefaultsModeCallback(
      &openwow::core::DispatchDisplaySettingsCallback);

  openwow::core::HeapUsage_UnregisterConsoleCmd();

}

static int GameSubsystemsInit_EventCallback(void * , int ) {
  GameSubsystemsInit();
  return 1;
}

static int GameSubsystemsShutdown_EventCallback(void * , int ) {
  GameSubsystemsShutdown();
  return 1;
}

static int TimingInfoCommandHandler() {
  return openwow::core::fn_timingMethod();
}

OPENWOW_PRINTF_FORMAT(1, 2) static void ConsoleLog(const char *fmt, ...) {
  if (!fmt) {
    return;
  }

  std::array<char, 4096> buffer{};
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
  va_end(args);

  openwow::core::ida::ConsoleAddLine(buffer.data(), openwow::core::ida::COLOR_DEFAULT);
}

namespace detail {

void RegisterClientInitCVars() {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  using Flags = openwow::ui::game::CVarFlags;

  cvars.RegisterCVar("dbCompress", "-1", Flags::Archive, "Database compression");
  cvars.RegisterCVar("locale", "****", Flags::Archive, "Set the game locale");
  cvars.RegisterCVar("useEnglishAudio", "0", Flags::Archive,
                     "Override the locale and use English audio");
  cvars.RegisterCVar(
      "processAffinityMask", "0", Flags::Archive,
      "Sets which core(s) WoW may execute on - changes require restart to take effect");
  cvars.RegisterCVar("timingTestError", "0", Flags::ReadOnly | Flags::NoSave,
                     "Error reported by the timing validation system");
  cvars.RegisterCVar("timingMethod", "0", Flags::Archive, "Desired method for game timing");
  cvars.RegisterCVar("textLocale", "enUS", Flags::ReadOnly | Flags::NoSave, "Text locale");
  cvars.RegisterCVar("audioLocale", "enUS", Flags::ReadOnly | Flags::NoSave, "Audio locale");

  cvars.SetValidationCallback(
      "locale", [](const std::string &, const std::string &, const std::string &new_value) {
        StoreClientInitLocaleTag(new_value);
        return true;
      });
  cvars.SetValidationCallback("processAffinityMask", ValidateClientInitProcessAffinityMask);
  cvars.SetValidationCallback("timingMethod", ValidateClientInitTimingMethod);

  StoreClientInitLocaleTag(cvars.GetCVar("locale"));
}

std::string ResolveClientInitLocale(const std::string &preferred_locale) {
  const auto &startup_state = openwow::data::GetStartupFileSystemState();
  return openwow::data::DetectLocaleRing(preferred_locale, startup_state.archive_data_path,
                                         startup_state.retail_install_path_cache);
}

std::string BuildClientInitLocaleDataPath(const std::string &locale) {
  const auto &startup_state = openwow::data::GetStartupFileSystemState();
  const std::string base_path = startup_state.archive_data_path.empty()
                                    ? std::string("Data\\")
                                    : startup_state.archive_data_path;
  return base_path + locale;
}

void SetInitTimerTimeSourceForTests(std::function<std::int64_t()> source) {
  InitTimerTimeSource() =
      source ? std::move(source) : []() { return ReadSharedTimerNowNsSince2000(); };
}

void InitTimerBaselineForTests(const int value) {
  InitTimerBaseline(value);
}

InitTimerBaselineStateForTests GetInitTimerBaselineStateForTests() {
  const auto &state = MutableWoWMainInitRuntimeState();
  return {
      .initialized = state.timer_initialized,
      .baseline_time_ns_since_2000 = state.timer_baseline,
      .startup_elapsed_time_ns = state.timer_offset,
  };
}

void ResetClientInitStateForTests() {
  auto &startup_error_table = StartupErrorTable();
  if (startup_error_table.record_data != nullptr || startup_error_table.index != nullptr ||
      startup_error_table.string_table != nullptr) {
    openwow::data::WowClientDB_Unload(&startup_error_table, ".\\Client.cpp");
  }
  startup_error_table = {};
  startup_error_table.max_id = -1;
  startup_error_table.min_id = 0x0FFFFFFF;

  StoreClientInitLocaleTag("enUS");
  ResetLoginSurveyTelemetryBootstrapState();
  dword_B2F9A4 = 0;
  dword_B2F9D8 = 0;
  dword_B2F994 = 0;
  dword_AB6360 = 0;
  dword_AB635C = 0;
  dword_AB6370 = 0;
  g_client_shutdown_requested.store(false, std::memory_order_release);
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  if (cvars.Exists("dbCompress")) {
    cvars.SetCVar("dbCompress", "-1", true);
  }
  if (cvars.Exists("locale")) {
    cvars.SetCVar("locale", "****", true);
  }
  if (cvars.Exists("useEnglishAudio")) {
    cvars.SetCVar("useEnglishAudio", "0", true);
  }
  if (cvars.Exists("processAffinityMask")) {
    cvars.SetCVar("processAffinityMask", "0", true);
  }
  if (cvars.Exists("timingMethod")) {
    cvars.SetCVar("timingMethod", "0", true);
  }
  if (cvars.Exists("timingTestError")) {
    cvars.SetCVar("timingTestError", "0", true);
  }
  if (cvars.Exists("textLocale")) {
    cvars.SetCVar("textLocale", "enUS", true);
  }
  if (cvars.Exists("audioLocale")) {
    cvars.SetCVar("audioLocale", "enUS", true);
  }
  openwow::game::ClientConfig::Get().SetLocale("enUS");
  MutableWoWMainInitRuntimeState() = {};
  SetInitTimerTimeSourceForTests({});
}

void ReloadLoginSurveyTelemetryBootstrapForTests() {
  LoadLoginSurveyTelemetryBootstrapState();
}

std::string GetClientInitLocaleTagForTests() {
  return CurrentClientInitLocaleTag();
}

bool HasLoginSurveyTelemetryBootstrapRecordImpl() {
  return MutableLoginSurveyTelemetryBootstrapState().loaded;
}

std::optional<LoginSurveyTelemetryBootstrapRecord> GetLoginSurveyTelemetryBootstrapRecordImpl() {
  const auto &state = MutableLoginSurveyTelemetryBootstrapState();
  if (!state.loaded) {
    return std::nullopt;
  }
  return state.record;
}

bool AppendLoginSurveyTelemetryBootstrapRecordImpl(
    std::vector<LoginSurveyTelemetryBootstrapRecord> &records) {
  const auto &state = MutableLoginSurveyTelemetryBootstrapState();
  if (!state.loaded) {
    return false;
  }
  records.push_back(state.record);
  return true;
}

void RefreshStartupErrorTableForTests() {
  InitErrorTableAndCacheBounds(&StartupErrorTable(), ".\\Client.cpp", 0x12E3u);
}

}

namespace {

detail::PostInitErrorDialogForTests ResolvePostInitErrorDialog(std::uint32_t error_code) {
  detail::PostInitErrorDialogForTests dialog;
  if (error_code == 0) {
    return dialog;
  }

  dialog.should_show = true;
  dialog.title =
      openwow::data::ResolveStartupWindowTitle(&StartupErrorTable(), "World of Warcraft");

  if (const char *text = openwow::data::LookupErrorTableText(&StartupErrorTable(), error_code)) {
    dialog.text = text;
  } else {
    dialog.text = "Unknown Error";
  }

  return dialog;
}

}

namespace detail {

detail::PostInitErrorDialogForTests
ResolvePostInitErrorDialogForTests(const std::uint32_t error_code) {
  return ResolvePostInitErrorDialog(error_code);
}

int ExecuteWoWGameEntry(const WoWGameEntryDependencies &deps) {
  if (deps.set_invalid_parameter_handler) {
    deps.set_invalid_parameter_handler(deps.invalid_parameter_handler);
  }
  if (deps.init_fpu_clear_and_control) {
    deps.init_fpu_clear_and_control();
  }
  if (deps.ret_zero_427a90) {
    deps.ret_zero_427a90();
  }
  if (deps.wow_main_init) {
    return deps.wow_main_init();
  }
  return 0;
}

void ExecuteWoWFiberEntry(int *fiber_parameter, const WoWFiberEntryDependencies &deps) {
  const int result = deps.entry_point ? deps.entry_point() : 0;
  if (fiber_parameter) {
    *fiber_parameter = result;
  }
  if (!deps.switch_to_fiber || !deps.get_main_fiber) {
    return;
  }

  void *const main_fiber = deps.get_main_fiber();
  if (main_fiber) {
    deps.switch_to_fiber(main_fiber);
  }
}

int ExecuteWinMainFiberBootstrap(const WinMainFiberBootstrapDependencies &deps) {
  if (!deps.convert_thread_to_fiber || !deps.create_game_fiber || !deps.switch_to_fiber ||
      !deps.delete_fiber) {
    return -1;
  }

  void *const main_fiber = deps.convert_thread_to_fiber();
  if (!main_fiber) {
    return -1;
  }
  if (deps.set_main_fiber) {
    deps.set_main_fiber(main_fiber);
  }

  int fiber_parameter = -1;
  void *const game_fiber =
      deps.create_game_fiber(kLegacyWinMainFiberStackReserveBytes, &fiber_parameter);
  if (!game_fiber) {
    return -1;
  }

  deps.switch_to_fiber(game_fiber);
  deps.delete_fiber(game_fiber);
  return fiber_parameter;
}

int ExecuteWoWMainInit(const WoWMainInitDependencies &deps) {
  if (deps.base_system_init) {
    deps.base_system_init();
  }
  if (deps.install_exception_filter) {
    deps.install_exception_filter();
  }
  if (deps.create_startup_event) {
    deps.create_startup_event("Blizzard Entertainment World of Warcraft", 0);
  }
  if (deps.dispatch_storm_setting) {
    deps.dispatch_storm_setting(10, 1);
    deps.dispatch_storm_setting(11, 1);
  }
  if (deps.set_os_initialized) {
    deps.set_os_initialized();
  }
  if (deps.init_file_system) {
    deps.init_file_system();
  }

  std::uint32_t send_error_logs = 1;
  if (!deps.read_send_error_logs || !deps.read_send_error_logs(send_error_logs)) {
    send_error_logs = 1;
    if (deps.write_send_error_logs) {
      deps.write_send_error_logs(1);
    }
  }

  if (deps.set_application_name) {
    deps.set_application_name("World of WarCraft (build 12340)");
  }
  if (deps.set_crash_dump_callback) {
    deps.set_crash_dump_callback(deps.crash_dump_callback);
  }
  if (send_error_logs != 0 && deps.register_crash_callback) {
    deps.register_crash_callback(deps.crash_notify_callback);
  }

  if (deps.client_init && deps.client_init()) {
    if (deps.run_event_scheduler) {
      deps.run_event_scheduler();
    }
    if (deps.post_init_error_check) {
      deps.post_init_error_check();
    }
  }

  if (deps.register_at_exit_handlers) {
    deps.register_at_exit_handlers();
  }
  if (deps.is_online_mode && deps.is_online_mode() && deps.online_cleanup) {
    deps.online_cleanup();
  }
  if (deps.init_timer_baseline) {
    deps.init_timer_baseline(0);
  }
  if (deps.is_online_mode && deps.is_online_mode() && deps.report_streaming_stats) {
    deps.report_streaming_stats(true, false);
  }
  if (deps.no_op) {
    deps.no_op();
  }
  return 0;
}

void ExecuteWriteInstallPathToRegistry(const WriteInstallPathToRegistryDependencies &deps) {
  const char *reg_key = "World of Warcraft";
  if (deps.is_wowt_product && deps.is_wowt_product()) {
    reg_key = "World of Warcraft\\PTR";
  }
  if (deps.is_online_mode && deps.is_online_mode()) {
    reg_key = "World of Warcraft Trial";
  }

  if (!deps.get_module_file_path || !deps.write_registry_string) {
    return;
  }

  std::string module_path = deps.get_module_file_path();
  if (module_path.empty()) {
    return;
  }

  const char *const leaf = openwow::core::FindStormPathLeafName(module_path.c_str());
  if (leaf == module_path.c_str()) {
    return;
  }
  module_path.resize(static_cast<std::size_t>(leaf - module_path.c_str()));

  const std::uint8_t flags = deps.is_online_mode && deps.is_online_mode() ? 1u : 4u;
  deps.write_registry_string(reg_key, "InstallPath", flags, module_path.c_str());
}

void PopulateStartupRetailInstallPathCache(const StartupRetailInstallPathDependencies &deps) {
  if (!deps.is_online_mode || !deps.is_online_mode()) {
    return;
  }

  const auto &startup_state = openwow::data::GetStartupFileSystemState();
  if (!startup_state.retail_install_path_cache.empty()) {
    return;
  }

  const bool ptr_product = deps.is_wowt_product && deps.is_wowt_product();
  (void)openwow::data::ResolveStartupRetailInstallPath(
      true, ptr_product, [&](const std::string &company_key, const std::string &value_name) {
        if (!deps.read_registry_string) {
          return std::string{};
        }
        return deps.read_registry_string(company_key.c_str(), value_name.c_str(), 1u);
      });
}

}

namespace detail {

void RunFinalProcessCleanupForTests() {
  FinalProcessCleanup();
}

}

std::int64_t GetInitTimerElapsedTimeNs() {
  return MutableWoWMainInitRuntimeState().timer_offset;
}

bool HasLoginSurveyTelemetryBootstrapRecord() {
  return detail::HasLoginSurveyTelemetryBootstrapRecordImpl();
}

std::optional<LoginSurveyTelemetryBootstrapRecord> GetLoginSurveyTelemetryBootstrapRecord() {
  return detail::GetLoginSurveyTelemetryBootstrapRecordImpl();
}

bool AppendLoginSurveyTelemetryBootstrapRecord(
    std::vector<LoginSurveyTelemetryBootstrapRecord> &records) {
  return detail::AppendLoginSurveyTelemetryBootstrapRecordImpl(records);
}

int WoWStart() {

  WoW_PreCRTInit();

  return mainCRTStartup();
}

int WoW_GameEntry() {
  return detail::ExecuteWoWGameEntry({
      .set_invalid_parameter_handler = SetInvalidParameterHandler,
      .init_fpu_clear_and_control = InitFPU_ClearAndControl,
      .ret_zero_427a90 = ret_zero_427A90,
      .wow_main_init = WoW_MainInit,
      .invalid_parameter_handler = nullsub_3,
  });
}

void WoW_FiberEntry(int *fiber_parameter) {
  detail::ExecuteWoWFiberEntry(
      fiber_parameter,
      {
          .entry_point = []() { return ExecuteCurrentStartupEntryPoint(); },
          .get_main_fiber = []() { return MutableStartupFiberRuntimeState().main_fiber; },
#if defined(_WIN32)
          .switch_to_fiber = [](void *fiber) { ::SwitchToFiber(fiber); },
#else
          .switch_to_fiber = {},
#endif
      });
}

int RunLegacyStartupFiberBootstrap(std::function<int()> entry_point) {
#if defined(_WIN32)
  auto &state = MutableStartupFiberRuntimeState();
  state.entry_point = std::move(entry_point);

  const int result = detail::ExecuteWinMainFiberBootstrap({
      .convert_thread_to_fiber = []() { return ::ConvertThreadToFiber(nullptr); },
      .set_main_fiber = [](void *fiber) { MutableStartupFiberRuntimeState().main_fiber = fiber; },
      .create_game_fiber =
          [](std::size_t stack_reserve_bytes, int *fiber_parameter) {
            return ::CreateFiberEx(0, stack_reserve_bytes, 0, StartupFiberEntryDispatcher,
                                   fiber_parameter);
          },
      .switch_to_fiber = [](void *fiber) { ::SwitchToFiber(fiber); },
      .delete_fiber = [](void *fiber) { ::DeleteFiber(fiber); },
  });

  state.entry_point = {};
  state.main_fiber = nullptr;
  return result;
#else
  if (entry_point) {
    return entry_point();
  }
  return WoW_GameEntry();
#endif
}

int WoW_MainInit() {
  return detail::ExecuteWoWMainInit({
      .base_system_init = BaseSystemInit,
      .install_exception_filter = []() { openwow::core::SetExceptionFilter(); },
      .create_startup_event = OS_CreateEventA,
      .dispatch_storm_setting = ApplyWoWMainInitStormSetting,
      .set_os_initialized = SetOSInitializedFlag,
      .init_file_system = InitFileSystem,
      .read_send_error_logs =
          [](std::uint32_t &value) {
            return ReadRegistryDword("World of Warcraft\\Client", "SendErrorLogs", 0, &value);
          },
      .write_send_error_logs =
          [](std::uint32_t value) {
            WriteRegistryDword("World of Warcraft\\Client", "SendErrorLogs", 0, value);
          },
      .set_application_name = [](const char *name) { openwow::core::SetApplicationName(name); },
      .set_crash_dump_callback =
          [](void *callback) { openwow::core::SetCrashDumpCallback(callback); },
      .register_crash_callback =
          [](void *callback) { openwow::core::RegisterCrashCallback(callback); },
      .client_init = ClientInit,
      .run_event_scheduler = RunEventScheduler,
      .post_init_error_check = []() { PostInitErrorCheck(); },
      .register_at_exit_handlers = RegisterAtExitHandlers,
      .is_online_mode = IsOnlineMode,
      .online_cleanup = WoW_MainInit__callee_41D0B0,
      .init_timer_baseline = InitTimerBaseline,
      .report_streaming_stats =
          [](bool is_startup, bool has_new_account) {
            openwow::data::Streaming_ReportStats(is_startup, has_new_account);
          },
      .no_op = nullsub_3,
      .crash_dump_callback = reinterpret_cast<void *>(Client_BuildBugReport),
      .crash_notify_callback = reinterpret_cast<void *>(&LaunchWowError),
  });
}

bool ClientInit() {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();

  RegisterClientInitCommandLineDefinitions();

  InitCommandLine();

  InitPatchList("WoW.mfil");

  if (openwow::data::GetStartupFileSystemState().client_init_archive_gate != 2) {
    WriteInstallPathToRegistry();
  }

  bool v23 = false;
  if (IsOnlineMode()) {
    v23 = !SubDirectoryExists("WTF/Account");
  }

  ClientInit__callee_6B0190();

  Console_RegisterBasicCommands();

  ida::CVar_LoadConfig("Config.wtf");

  detail::RegisterClientInitCVars();

  ida::CVar_SetDirtyFlag();

  detail::ProcessRunOnceFilesWithCallback(LoadRunOnceConfigFile);

  std::string locale = cvars.GetCVar("locale");
  if (locale == "****") {
    SyncClientInitLocaleState("enUS");
    locale = "enUS";
  }

  if (IsOnlineMode()) {
    ClientInit__callee_4036B0(static_cast<int>(v23), 0, 0, static_cast<int>(v23));
  }

  detail::PopulateStartupRetailInstallPathCache({
      .is_online_mode = IsOnlineMode,
      .is_wowt_product = fn_IsWoWTProduct,
      .read_registry_string =
          [](const char *key, const char *value_name, std::uint8_t flags) {
            std::string value;
            if (!ReadRegistryString(key, value_name, flags, &value)) {
              return std::string{};
            }
            return value;
          },
  });

  locale = detail::ResolveClientInitLocale(locale);
  SyncClientInitLocaleState(locale);

  const std::string locale_path = detail::BuildClientInitLocaleDataPath(locale);
  ClientInit__callee_421B50(locale_path.c_str());

  sub_423D70();

  LoadAllArchives();
  if (const std::string final_locale = cvars.GetCVar("locale"); !final_locale.empty()) {
    SyncClientInitLocaleState(final_locale);
  }

  const std::string video_options_version = cvars.GetCVar("videoOptionsVersion");
  if (video_options_version.empty() || ParseSignedDecimalPrefix(video_options_version) < 3) {
    SetClientInitCVar("processAffinityMask", "0");
  }

  (void)ParseSignedDecimalPrefix(cvars.GetCVar("processAffinityMask"));

  InitSCritical(0);

  InitEvtSchedulerConfig_ClientInit(1, 0);

  OS_SetTimingMethod(
      static_cast<uint32_t>(ParseSignedDecimalPrefix(cvars.GetCVar("timingMethod"))));

  Console_RegisterCommand("timingInfo", reinterpret_cast<void *>(TimingInfoCommandHandler), 0, 0);

  int timerError = OS_GetTimerError();
  dword_B2F9D8 = static_cast<uint32_t>(timerError);
  if (ParseSignedDecimalPrefix(cvars.GetCVar("timingTestError")) != timerError) {
    SetClientInitCVar("timingTestError", std::to_string(timerError));
    ConsoleLog("Timing test error: %d", timerError);
  }

  InitErrorTableAndCacheBounds(&StartupErrorTable(), ".\\Client.cpp", 0x12E3u);

  const char *windowTitle =
      openwow::data::ResolveStartupWindowTitle(&StartupErrorTable(), "World of Warcraft");
  char titleBuf[260] = {};
  LocalStrCopy(titleBuf, windowTitle, static_cast<int>(sizeof(titleBuf)));

  ConsoleDeviceInitialize(titleBuf);

  OS_InitPerfCounters(openwow::core::TimingMethodFromCVarValue(
      ParseSignedDecimalPrefix(cvars.GetCVar("timingMethod"))));
  InitializeClientStartupAdlerSeedState();

  const int game_subsystems_init_callback = openwow::core::EvtSched_RegisterLegacyCallback(
      reinterpret_cast<std::intptr_t>(&GameSubsystemsInit_EventCallback));
  const int game_subsystems_shutdown_callback = openwow::core::EvtSched_RegisterLegacyCallback(
      reinterpret_cast<std::intptr_t>(&GameSubsystemsShutdown_EventCallback));
  dword_B2F994 = CreateMainEventContext(1, game_subsystems_init_callback,
                                        game_subsystems_shutdown_callback, 0, 0);

  return true;
}

void InitializeClientStartupAdlerSeedState() {
  GetClientStartupAdlerSeedState() =
      foundation::hashing::MakeAdlerSeedState(GameClock::GetTickCount32());
}

foundation::hashing::AdlerSeedState& GetClientStartupAdlerSeedState() {
  static foundation::hashing::AdlerSeedState state{};
  return state;
}

int PostInitErrorCheck() {

  ApplyPostInitLogFlags();

  PostInitErrorCheck__callee_86D0C0();

  PostInitErrorCheck__callee_86D440();

  AudioSystem_Shutdown();

  PostInitErrorCheck__callee_769D40();

  detail::ProcessRunOnceFilesWithCallback(
      [](const std::string &filename) { detail::DeleteRunOnceFileIfPresent(filename); });

  CVar_Cleanup();

  PostInitErrorCheck__callee_7685C0();

  Cleanup_FreeAllRegisteredObjects();

  Cleanup_FinalizeDataPreloadRuntime();

  sub_457680();

  int result = static_cast<int>(dword_B2F9A4);
  if (const auto dialog = ResolvePostInitErrorDialog(dword_B2F9A4); dialog.should_show) {
    return ShowMessageBox(0, 0, dialog.text.c_str(), dialog.title.c_str());
  }

  return result;
}

int RequestClientShutdownWithErrorCode(std::uint32_t error_code) {
  dword_B2F9A4 = error_code;
  EvtContext_RequestShutdown(0);
  g_client_shutdown_requested.store(true, std::memory_order_release);
  return 0;
}

bool ConsumeClientShutdownRequest(std::uint32_t *error_code) {
  const bool requested = g_client_shutdown_requested.exchange(false, std::memory_order_acq_rel);
  if (!requested) {
    return false;
  }
  if (error_code != nullptr) {
    *error_code = dword_B2F9A4;
  }
  return true;
}

void ClearClientShutdownRequest() {
  g_client_shutdown_requested.store(false, std::memory_order_release);
  dword_B2F9A4 = 0;
}

void Cleanup_FreeAllRegisteredObjects() {
  openwow::data::CleanupRegisteredArchiveHandlesForShutdown();
}

void WriteInstallPathToRegistry() {
  detail::ExecuteWriteInstallPathToRegistry({
      .is_wowt_product = fn_IsWoWTProduct,
      .is_online_mode = IsOnlineMode,
      .get_module_file_path = GetModuleFilePath,
      .write_registry_string =
          [](const char *key, const char *value_name, std::uint8_t flags, const char *data) {
            WriteRegistryString(key, value_name, flags, data);
          },
  });
}

}

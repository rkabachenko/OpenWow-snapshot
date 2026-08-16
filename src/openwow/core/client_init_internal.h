#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openwow::core::detail {

struct WoWGameEntryDependencies {
  std::function<void(void (*handler)())> set_invalid_parameter_handler;
  std::function<void()> init_fpu_clear_and_control;
  std::function<void()> ret_zero_427a90;
  std::function<int()> wow_main_init;
  void (*invalid_parameter_handler)() = nullptr;
};

inline constexpr std::size_t kLegacyWinMainFiberStackReserveBytes =
    8u * 1024u * 1024u;

struct WoWFiberEntryDependencies {
  std::function<int()> entry_point;
  std::function<void*()> get_main_fiber;
  std::function<void(void* fiber)> switch_to_fiber;
};

struct WinMainFiberBootstrapDependencies {
  std::function<void*()> convert_thread_to_fiber;
  std::function<void(void* fiber)> set_main_fiber;
  std::function<void*(std::size_t stack_reserve_bytes, int* fiber_parameter)>
      create_game_fiber;
  std::function<void(void* fiber)> switch_to_fiber;
  std::function<void(void* fiber)> delete_fiber;
};

struct WoWMainInitDependencies {
  std::function<void()> base_system_init;
  std::function<void()> install_exception_filter;
  std::function<void(const char *name, int flags)> create_startup_event;
  std::function<void(int command, int value)> dispatch_storm_setting;
  std::function<void()> set_os_initialized;
  std::function<void()> init_file_system;
  std::function<bool(std::uint32_t &value)> read_send_error_logs;
  std::function<void(std::uint32_t value)> write_send_error_logs;
  std::function<void(const char *name)> set_application_name;
  std::function<void(void *callback)> set_crash_dump_callback;
  std::function<void(void *callback)> register_crash_callback;
  std::function<bool()> client_init;
  std::function<void()> run_event_scheduler;
  std::function<void()> post_init_error_check;
  std::function<void()> register_at_exit_handlers;
  std::function<bool()> is_online_mode;
  std::function<void()> online_cleanup;
  std::function<void(int value)> init_timer_baseline;
  std::function<void(bool is_startup, bool has_new_account)> report_streaming_stats;
  std::function<void()> no_op;
  void *crash_dump_callback{nullptr};
  void *crash_notify_callback{nullptr};
};

int ExecuteWoWGameEntry(const WoWGameEntryDependencies &deps);
void ExecuteWoWFiberEntry(int* fiber_parameter,
                          const WoWFiberEntryDependencies& deps);
int ExecuteWinMainFiberBootstrap(
    const WinMainFiberBootstrapDependencies& deps);
int ExecuteWoWMainInit(const WoWMainInitDependencies &deps);
void RegisterClientInitCVars();
std::string ResolveClientInitLocale(const std::string &preferred_locale);
std::string BuildClientInitLocaleDataPath(const std::string &locale);
struct InitTimerBaselineStateForTests {
  bool initialized{false};
  std::int64_t baseline_time_ns_since_2000{0};
  std::int64_t startup_elapsed_time_ns{0};
};

void SetInitTimerTimeSourceForTests(std::function<std::int64_t()> source);
void InitTimerBaselineForTests(int value);
InitTimerBaselineStateForTests GetInitTimerBaselineStateForTests();
void ResetClientInitStateForTests();
void ReloadLoginSurveyTelemetryBootstrapForTests();
std::string GetClientInitLocaleTagForTests();
void RefreshStartupErrorTableForTests();

struct PostInitErrorDialogForTests {
  bool should_show{false};
  std::string title;
  std::string text;
};

PostInitErrorDialogForTests ResolvePostInitErrorDialogForTests(
    std::uint32_t error_code);

struct WriteInstallPathToRegistryDependencies {
  std::function<bool()> is_wowt_product;
  std::function<bool()> is_online_mode;
  std::function<std::string()> get_module_file_path;
  std::function<void(const char *key, const char *value_name, std::uint8_t flags, const char *data)>
      write_registry_string;
};

void ExecuteWriteInstallPathToRegistry(const WriteInstallPathToRegistryDependencies &deps);

struct StartupRetailInstallPathDependencies {
  std::function<bool()> is_online_mode;
  std::function<bool()> is_wowt_product;
  std::function<std::string(const char *key, const char *value_name, std::uint8_t flags)>
      read_registry_string;
};

void PopulateStartupRetailInstallPathCache(
    const StartupRetailInstallPathDependencies &deps);

using ArchiveIntegrityLookup = std::function<bool(const std::string &)>;

bool LookupArchiveIntegrityDigestForPath(const char* path,
                                         std::uint8_t out_digest[16]);
bool ShouldResolveArchiveIntegrityDigestForPath(const char* path);
void ResetArchiveIntegrityForTests();
void SetArchiveIntegrityLookupForTests(ArchiveIntegrityLookup lookup);
bool HasArchiveIntegritySignatureForTests(const std::string &path);
bool IsArchiveIntegrityFileMarkedForTests(const std::string &path);
std::int32_t GetStreamingIntegrityFlagForTests();
std::int32_t GetStreamingIntegrityCallbackForTests();
void SetSignatureFileContentsForTests(std::vector<std::uint8_t> bytes);
void SetSignatureVerificationKeyForTests(std::vector<std::uint8_t> modulus,
                                         std::vector<std::uint8_t> exponent);
void ProcessRunOnceFilesWithCallback(const std::function<void(const std::string &)> &callback);
bool DeleteRunOnceFileIfPresent(const std::string &filename);

void RunFinalProcessCleanupForTests();

}

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openwow::data {

using STORM_HANDLE = void*;
using ArchiveCloseCallbackForTests = bool (*)(STORM_HANDLE handle);

struct PatchFileList {
  char** array{nullptr};
  int count{0};
};

static constexpr int kMaxArchiveSlots = 28;

static constexpr std::uint32_t kSFileOpenFlags      = 0xC00;
static constexpr int           kOpenArchiveRetries   = 3;
static constexpr int           kPatchBasePriority    = 64;
static constexpr std::uint32_t kClientBuild          = 12340;

static constexpr std::uint32_t kStormErrCorrupt      = 131;
static constexpr std::uint32_t kStormErrAccessDenied = 5;
static constexpr std::uint32_t kStormErrNotReady     = 38;
static constexpr std::uint32_t kStormErrDiskFull     = 108;
static constexpr std::uint32_t kStormErrBadFormat    = static_cast<std::uint32_t>(-2062548884);
static constexpr std::uint32_t kStormErrBadSignature = static_cast<std::uint32_t>(-2062548861);

enum ArchiveType : std::uint32_t {
  kArchiveTypeBase      = 0,
  kArchiveTypeSplit     = 1,
  kArchiveTypeExpansion = 2,
  kArchiveTypeLocale    = 3,
  kArchiveTypeStreaming = 4,
};

struct ArchiveLocaleInfo {
  std::string language;
  std::string country;
  std::string region;
  int locale_index{-1};
};

struct ArchiveOpenFailureContext {
  std::string archive_name;
  std::string resolved_data_path;
  std::string locale_token;
  std::string streaming_status_text;
  std::uint32_t storm_error{0};
  bool streaming_manifest_active{false};
};

struct ArchiveSystemCallbacks {

  std::function<bool(const char* path, std::int32_t priority,
                     std::uint32_t flags, void** out_handle,
                     std::uint32_t* out_error)>
      open_archive;
  std::function<bool(void* base_archive, const char* path,
                     std::int32_t priority, int flags, void** out_handle,
                     std::uint32_t* out_error)>
      open_patch_archive;

  std::function<bool(int attempt, int buf_size, const char* name,
                     char* path_out, const char* locale)>
      build_archive_probe_path;

  std::function<int()> has_common_archive_layout;

  std::function<std::string(const std::string& name)> cvar_get_string;
  std::function<void(const std::string& name, const std::string& value)>
      cvar_set_string;

  std::function<bool(const char* key, const char* value_name,
                     int type, void* out)>
      read_registry_value;

  std::function<bool()> is_online_mode;

  std::function<void(int value)> init_timer_baseline;

  std::function<void()> init_online_sound_cache;

  std::function<bool(const char* path)> file_exists;
  std::function<void(const char* path)> file_delete;

  std::function<std::string(const std::string& key)> read_wow_ini;

  std::function<bool()> should_open_streaming_archives;

  std::function<void(int mode)> set_font_system_mode;

  std::function<void(int reload, const char* locale)> load_login_configs;

  std::function<void(std::uint32_t locale_tag)> load_signature_file;

  std::function<void(const ArchiveOpenFailureContext& failure)>
      handle_archive_open_failure;
};

bool OpenArchiveByName(const char* name,
                       std::int32_t priority,
                       int slot_index,
                       const char* locale_ptr,
                       const ArchiveSystemCallbacks& callbacks = {});

PatchFileList BuildPatchMPQList(const char* locale,
                                bool include_secondary_patch_set = false);

void PatchFileList_Destroy(PatchFileList* list);

bool LoadAllArchives(const ArchiveSystemCallbacks& callbacks = {});

void CleanupRegisteredArchiveHandlesForShutdown();

int InitPatchList(char* manifest_file,
                  const ArchiveSystemCallbacks& callbacks = {});

int InitStreamingSubsystem(char has_new_account,
                           const ArchiveSystemCallbacks& callbacks = {});

const STORM_HANDLE* GetArchiveSlots();

std::size_t GetArchiveHandleCount();

std::size_t GetArchiveTableBaseIndex();

const ArchiveLocaleInfo& GetCurrentLocaleInfo();

std::uint32_t GetStreamingFlags();

const std::string& GetStreamingErrorText();

int GetStartupLevel();

void SetStartupLevel(int startup_level);

std::size_t GetArchiveHandleCapacityForTests();
bool ResizeRegisteredArchiveHandleArrayForTests(std::size_t new_capacity);
bool SetArchiveHandleForTests(std::size_t index, STORM_HANDLE handle);
void SetArchiveTableBaseIndexForTests(std::size_t index);
void SetArchiveCloseCallbackForTests(ArchiveCloseCallbackForTests callback);

void SortPatchWildcardEntriesForTests(std::vector<std::string>* entries);

void ResetArchiveSystemForTests();

void DefaultLoadLoginConfigs(int reload, const char* locale);

}

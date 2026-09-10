#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace openwow::vfs {
class VirtualFileSystem;
}

namespace openwow::data {

using STORM_HANDLE = void*;
using ArchiveCloseCallbackForTests = bool (*)(STORM_HANDLE handle);

static constexpr std::uint32_t kClientBuild = 12340;

struct ArchiveLocaleInfo {
  std::string language;
  std::string country;
  std::string region;
  int locale_index{-1};
};

struct ArchiveSystemCallbacks {

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
};

void DefaultLoadLoginConfigs(
    int reload, const char* locale,
    const openwow::vfs::VirtualFileSystem* content_vfs = nullptr);

std::string ResolveWowIniArchiveLocale(
    const std::string& locale_token,
    bool has_common_archive_layout,
    const ArchiveSystemCallbacks& callbacks = {});

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

void ResetArchiveSystemForTests();

}

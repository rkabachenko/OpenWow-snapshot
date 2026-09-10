#include "openwow/data/archive_system.h"
#include "openwow/audio/codecs/ogg/ogg_decompress.h"
#include "openwow/core/cvar.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/startup_archive_mount.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/data/streaming_init.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/storage/persistence/profile_reader.h"
#include "openwow/vfs/virtual_file_system.h"
#include "openwow/vfs/retail/sfile_archive.h"
#include "openwow/vfs/retail/sfile_configuration.h"
#include "openwow/vfs/retail/sound_cache/sound_cache.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <new>
#include <string>

namespace openwow::data {

namespace {

STORM_HANDLE* g_registered_archive_handles = nullptr;
std::size_t g_registered_archive_handle_capacity = 0;
std::size_t g_registered_archive_handle_count = 0;
std::size_t g_registered_archive_table_base_index = 0;
ArchiveCloseCallbackForTests g_archive_close_callback_for_tests = nullptr;

std::uint32_t g_streaming_flags = 0;

std::string g_streaming_error_text;

ArchiveLocaleInfo g_current_locale = {};

int g_startup_level = 0;

constexpr std::array<const char*, 9> kWowIniLocaleTable = {
    "enUS", "koKR", "frFR", "deDE", "zhCN",
    "zhTW", "esES", "esMX", "ruRU",
};

constexpr std::uint32_t kStormWholeStringCompareLength = 0x7FFFFFFFu;

constexpr std::size_t kWowIniLocaleTagLength = 4;

int FindWowLocaleIndex(const std::string& locale) {
  const bool en_gb =
      openwow::core::SStrCmpNoCase(locale.c_str(), "enGB",
                                   kStormWholeStringCompareLength) == 0;
  for (std::size_t index = 0; index < kWowIniLocaleTable.size(); ++index) {
    const bool matches_locale =
        openwow::core::SStrCmpNoCase(locale.c_str(), kWowIniLocaleTable[index],
                                     kStormWholeStringCompareLength) == 0;
    const bool matches_en_gb_alias =
        en_gb && openwow::core::SStrCmpNoCase(
                     kWowIniLocaleTable[index], "enUS",
                     kStormWholeStringCompareLength) == 0;
    if (matches_locale || matches_en_gb_alias) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool ResizeRegisteredArchiveHandleArray(const std::size_t new_capacity) {
  g_registered_archive_handle_capacity = new_capacity;

  STORM_HANDLE* const old_handles = g_registered_archive_handles;
  STORM_HANDLE* new_handles = nullptr;
  if (new_capacity != 0) {
    new_handles = new (std::nothrow) STORM_HANDLE[new_capacity]{};
  }

  if (new_handles != nullptr) {
    const std::size_t handles_to_copy =
        std::min(new_capacity, g_registered_archive_handle_count);
    for (std::size_t index = 0; index < handles_to_copy; ++index) {
      new_handles[index] = old_handles[index];
    }
  }

  g_registered_archive_handles = new_handles;
  delete[] old_handles;
  return new_handles != nullptr || new_capacity == 0;
}

bool CloseRegisteredArchiveHandle(const STORM_HANDLE handle) {
  if (handle == nullptr) {
    return false;
  }

  if (g_archive_close_callback_for_tests != nullptr) {
    return g_archive_close_callback_for_tests(handle);
  }

  return openwow::vfs::SFileCloseArchiveWrapped(handle);
}

std::filesystem::path StartupStatePathToNative(std::string path) {
#if !defined(_WIN32)
  std::replace(path.begin(), path.end(), '\\',
               std::filesystem::path::preferred_separator);
#endif
  return std::filesystem::path(path);
}

std::filesystem::path ResolveStartupClientRoot() {
  const auto& state = GetStartupFileSystemState();
  if (!state.executable_base_path.empty()) {
    return StartupStatePathToNative(state.executable_base_path);
  }
  return std::filesystem::current_path();
}

std::string ReadWowIniValue(const std::string& key) {
  const std::string wow_ini_path =
      (ResolveStartupClientRoot() / "Wow.ini").string();
  const auto value =
      openwow::storage::persistence::ReadFirstProfileValueFromFile(
          openwow::storage::persistence::ProfileFilePath(wow_ini_path),
          {
              .section = openwow::storage::persistence::ProfileSectionName(
                  "WoW Config"),
              .key = openwow::storage::persistence::ProfileKeyName(key),
          });
  if (!value) {
    return {};
  }

  return std::string(value->Text());
}

void DefaultSetCVarString(const std::string& name, const std::string& value) {
  openwow::ui::game::CVarSystem::Instance().SetCVar(name, value, true);
}

}

void DefaultLoadLoginConfigs(
    const int reload, const char* locale,
    const openwow::vfs::VirtualFileSystem* content_vfs) {
  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  cvars.RegisterCVar("decorateAccountName", "0",
                     openwow::ui::game::CVarFlags::None,
                     "Decorate account names");
  cvars.RegisterCVar("realmListbn", "",
                     openwow::ui::game::CVarFlags::None,
                     "Address of Battle.net server");
  cvars.RegisterCVar("portal", "",
                     openwow::ui::game::CVarFlags::None,
                     "Name of Battle.net portal to use");
  cvars.RegisterCVar("serverAlert", "SERVER_ALERT_URL",
                     openwow::ui::game::CVarFlags::None,
                     "Get the glue-string tag for the URL");
  cvars.RegisterCVar("realmList", "us.logon.worldofwarcraft.com:3724",
                     openwow::ui::game::CVarFlags::None,
                     "Address of realm list server");

  if (!reload) {
    return;
  }

  const auto load_config = [content_vfs](const std::string& path) {
    if (content_vfs) {
      if (const auto content = content_vfs->ReadTextFile(path);
          content.has_value()) {
        openwow::core::ida::CVar_ParseConfigBuffer(*content);
        return true;
      }
    }
    return openwow::core::ida::CVar_LoadFromFile(path) != 0;
  };

  std::string locale_prefix;
  if (locale && *locale) {
    locale_prefix = "data\\";
    locale_prefix += locale;
    locale_prefix += "\\";
  }

  if (!locale_prefix.empty()) {
    const std::string battlenet_path = locale_prefix + "realmlistbn.wtf";
    if (!load_config(battlenet_path)) {
      load_config("realmlistbn.wtf");
    }
  } else {
    load_config("realmlistbn.wtf");
  }

  bool loaded_realm_list = false;
  if (!locale_prefix.empty()) {
    const std::string realm_list_path = locale_prefix + "realmlist.wtf";
    loaded_realm_list = load_config(realm_list_path);
    if (!loaded_realm_list) {
      loaded_realm_list = load_config("realmlist.wtf");
    }
  } else {
    loaded_realm_list = load_config("realmlist.wtf");
  }
  if (!loaded_realm_list) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "Login realm list configuration not found: locale=" +
            std::string(locale && *locale ? locale : "<none>") +
            "; using registered realmList default");
  }
}

std::string ResolveWowIniArchiveLocale(const std::string& locale_token,
                                       const bool has_common_archive_layout,
                                       const ArchiveSystemCallbacks& callbacks) {

  const std::uint32_t layout_flags = has_common_archive_layout
                                         ? kArchiveLayoutFlagsCommon
                                         : kArchiveLayoutFlagsSplit;

  g_current_locale = {};
  const auto read_wow_ini = [&](const std::string& key) {
    return callbacks.read_wow_ini ? callbacks.read_wow_ini(key)
                                  : ReadWowIniValue(key);
  };
  g_current_locale.region = read_wow_ini("Region");
  g_current_locale.language = read_wow_ini("Language");
  g_current_locale.country = read_wow_ini("Country");

  if (g_current_locale.language.empty() || g_current_locale.country.empty()) {
    g_current_locale.locale_index = FindWowLocaleIndex(locale_token);
    return locale_token;
  }

  const std::string wow_ini_locale =
      (g_current_locale.language + g_current_locale.country)
          .substr(0, kWowIniLocaleTagLength);
  g_current_locale.locale_index = FindWowLocaleIndex(wow_ini_locale);
  if (g_current_locale.locale_index >= 0) {
    if ((layout_flags & kArchiveLayoutFlagsSplit) != 0) {
      if (callbacks.cvar_set_string) {
        callbacks.cvar_set_string("locale", wow_ini_locale);
      } else {
        DefaultSetCVarString("locale", wow_ini_locale);
      }
      return wow_ini_locale;
    }
  }

  return locale_token;
}

void CleanupRegisteredArchiveHandlesForShutdown() {
  if (g_registered_archive_handles == nullptr ||
      g_registered_archive_handle_count == 0) {
    return;
  }

  for (std::size_t index = g_registered_archive_handle_count; index > 0;
       --index) {
    const auto handle = g_registered_archive_handles[index - 1];
    if (handle == nullptr) {
      continue;
    }

    (void)CloseRegisteredArchiveHandle(handle);
  }
}

int InitPatchList(char* manifest_file,
                  const ArchiveSystemCallbacks& callbacks) {
  if (callbacks.init_timer_baseline) {
    callbacks.init_timer_baseline(1);
  }

  bool manifest_exists = false;
  if (callbacks.file_exists) {
    manifest_exists = callbacks.file_exists(manifest_file);
  } else if (manifest_file) {
    std::error_code ec;
    manifest_exists = std::filesystem::exists(manifest_file, ec);
  }

  if (manifest_exists && manifest_file) {
    g_streaming_flags |= 1u;

    const int result = openwow::vfs::InitStreamingFromManifest(manifest_file);

    if (callbacks.file_delete) {
      callbacks.file_delete("WoW.stor");
    } else {
      std::error_code ec;
      std::filesystem::remove("WoW.stor", ec);
    }

    if (result) {
      g_streaming_flags |= 2u;
    } else {
      g_streaming_error_text = openwow::vfs::GetStreamingStatusMessageText();
    }
  }

  bool online = false;
  if (callbacks.is_online_mode) {
    online = callbacks.is_online_mode();
  }
  if (online) {
    if (callbacks.init_online_sound_cache) {
      callbacks.init_online_sound_cache();
    } else {
      openwow::vfs::SFile2_InitSoundCache(
          reinterpret_cast<void*>(openwow::audio::OggVorbis_DecodeToWAV));
    }
  }

  openwow::vfs::SetStreamingFlags(
      static_cast<std::uint8_t>(g_streaming_flags & 1u),
      static_cast<std::uint8_t>((g_streaming_flags & 2u) != 0),
      static_cast<std::uint8_t>((g_streaming_flags & 4u) != 0),
      static_cast<std::uint8_t>((g_streaming_flags & 8u) != 0),
      kClientBuild);

  return 0;
}

int InitStreamingSubsystem(char has_new_account,
                           const ArchiveSystemCallbacks& callbacks) {

  int speed_test = 0;
  {
    bool reg_ok = false;
    if (callbacks.read_registry_value) {
      reg_ok = callbacks.read_registry_value(
          "World of Warcraft Trial", "SpeedTest", 1, &speed_test);
    }
    if (!reg_ok) {
      speed_test = 0;
    }
  }

  Streaming_ConfigureBgPreloadSleep(speed_test);

  Streaming_SetSpeedTest(speed_test);

  std::string locale;
  if (callbacks.cvar_get_string) {
    locale = callbacks.cvar_get_string("locale");
  }

  Streaming_SetLocale(locale.c_str());

  Streaming_SetBuildNumber(kClientBuild);

  Streaming_ReportStats(true, has_new_account != 0);

  return Streaming_RegisterFrameCallback(Streaming_RecordFrameDeltaHistogram);
}

const STORM_HANDLE* GetArchiveSlots() {
  return g_registered_archive_handles;
}

std::size_t GetArchiveHandleCount() {
  return g_registered_archive_handle_count;
}

std::size_t GetArchiveTableBaseIndex() {
  return g_registered_archive_table_base_index;
}

const ArchiveLocaleInfo& GetCurrentLocaleInfo() {
  return g_current_locale;
}

std::uint32_t GetStreamingFlags() {
  return g_streaming_flags;
}

const std::string& GetStreamingErrorText() {
  return g_streaming_error_text;
}

int GetStartupLevel() {
  return g_startup_level;
}

void SetStartupLevel(const int startup_level) {
  g_startup_level = startup_level;
}

std::size_t GetArchiveHandleCapacityForTests() {
  return g_registered_archive_handle_capacity;
}

bool ResizeRegisteredArchiveHandleArrayForTests(const std::size_t new_capacity) {
  return ResizeRegisteredArchiveHandleArray(new_capacity);
}

bool SetArchiveHandleForTests(const std::size_t index, STORM_HANDLE handle) {
  if (index >= g_registered_archive_handle_capacity ||
      g_registered_archive_handles == nullptr) {
    return false;
  }

  g_registered_archive_handles[index] = handle;
  g_registered_archive_handle_count = std::max(
      g_registered_archive_handle_count, index + 1);
  return true;
}

void SetArchiveTableBaseIndexForTests(const std::size_t index) {
  g_registered_archive_table_base_index = index;
}

void SetArchiveCloseCallbackForTests(const ArchiveCloseCallbackForTests callback) {
  g_archive_close_callback_for_tests = callback;
}

void ResetArchiveSystemForTests() {
  delete[] g_registered_archive_handles;
  g_registered_archive_handles = nullptr;
  g_registered_archive_handle_capacity = 0;
  g_registered_archive_handle_count = 0;
  g_registered_archive_table_base_index = 0;
  g_archive_close_callback_for_tests = nullptr;
  g_streaming_flags = 0;
  g_streaming_error_text.clear();
  g_current_locale = {};
  g_startup_level = 0;
}

}

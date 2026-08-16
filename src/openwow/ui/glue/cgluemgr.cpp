
#include "openwow/ui/glue/cgluemgr.h"
#include "openwow/ui/glue/glue_background_controller.h"
#include "openwow/ui/glue/glue_charselect_scene.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/game/localization.h"
#include "openwow/game/name_declension.h"
#include "openwow/game/name_validation.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/net/client_services.h"
#include "openwow/net/login_patch_download.h"
#include "openwow/net/os_url_download.h"
#include "openwow/platform/adapters/win32/win32_compat.h"
#include "openwow/net/wotlk/realm_connection.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace openwow::ui::glue {

namespace {

struct CGlueMgrInternalState {
  GlueState current_state{GlueState::kIdle};
  bool disconnect_pending{false};
  bool reconnect_after_disconnect{false};
  int status_counter{0};
  std::uint32_t disconnect_reason{0};
  bool ffx_initialized{false};
  float patch_download_progress{0.0f};
  float patch_download_elapsed{0.0f};
  bool patch_download_timer_armed{false};
};

constexpr std::size_t kScanDllVersionBufferLength = 0x18;
constexpr int kScanDllDownloadTimeoutMs = 5000;
constexpr std::string_view kScanDllVersionMarker = "Scan";

struct ScanDllRuntimeState {
  std::mutex mutex;
  std::optional<std::future<ScanDllExecutionResult>> execution_future;
  std::optional<std::future<void>> update_future;
  ScanDllExecutorForTests executor;
};

[[nodiscard]] std::filesystem::path ResolveClientRootPath() {
  const auto& startup_state = openwow::data::GetStartupFileSystemState();
  if (!startup_state.executable_base_path.empty()) {
    return std::filesystem::path(startup_state.executable_base_path);
  }
  return std::filesystem::current_path();
}

[[nodiscard]] std::filesystem::path ResolveScanDllModulePath() {
  return (ResolveClientRootPath() / "Scan.dll").lexically_normal();
}

[[nodiscard]] std::filesystem::path ResolveScanDllStagedModulePath() {
  return (ResolveClientRootPath() / "Scan.dll.new").lexically_normal();
}

void DeletePathIfPresent(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

[[nodiscard]] std::optional<std::string> TryReadScanDllModuleVersionString(
    const std::filesystem::path& module_path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(module_path, ec)) {
    return std::nullopt;
  }

  openwow::platform::FileVersionInfo version_info;
  if (!openwow::platform::ReadFileVersionInfo(module_path.string(), version_info)) {
    return std::nullopt;
  }

  std::string version =
      std::to_string(version_info.major) + "." + std::to_string(version_info.minor) + "."
      + std::to_string(version_info.patch) + "." + std::to_string(version_info.build);
  if (version.size() >= kScanDllVersionBufferLength) {
    return std::nullopt;
  }

  return version;
}

[[nodiscard]] std::optional<std::string> PromoteStagedScanDllIfPresent(
    const std::filesystem::path& current_module_path,
    const std::filesystem::path& staged_module_path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(staged_module_path, ec)) {
    return std::nullopt;
  }

  const auto staged_version = TryReadScanDllModuleVersionString(staged_module_path);
  if (!staged_version) {
    DeletePathIfPresent(staged_module_path);
    return std::nullopt;
  }

  DeletePathIfPresent(current_module_path);
  std::filesystem::rename(staged_module_path, current_module_path, ec);
  if (ec) {
    DeletePathIfPresent(staged_module_path);
    return std::nullopt;
  }

  return staged_version;
}

[[nodiscard]] std::optional<std::string> PrepareScanDllModuleVersion(
    const std::filesystem::path& current_module_path,
    const std::filesystem::path& staged_module_path) {
  if (const auto promoted_version =
          PromoteStagedScanDllIfPresent(current_module_path, staged_module_path);
      promoted_version) {
    return promoted_version;
  }

  return TryReadScanDllModuleVersionString(current_module_path);
}

[[nodiscard]] std::optional<std::string> ParseScanDllVersionResponse(
    const std::string_view response_body) {
  std::size_t line_start = 0;
  while (line_start < response_body.size()) {
    std::size_t line_end = line_start;
    while (line_end < response_body.size()
           && response_body[line_end] != '\r'
           && response_body[line_end] != '\n') {
      ++line_end;
    }

    if (line_end == response_body.size()) {
      return std::nullopt;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(
            response_body.substr(line_start, line_end - line_start),
            kScanDllVersionMarker)) {
      std::size_t next_line_start = line_end + 1;
      if (response_body[line_end] == '\r'
          && next_line_start < response_body.size()
          && response_body[next_line_start] == '\n') {
        ++next_line_start;
      }

      std::size_t next_line_end = next_line_start;
      while (next_line_end < response_body.size()
             && response_body[next_line_end] != '\r'
             && response_body[next_line_end] != '\n') {
        ++next_line_end;
      }

      if (next_line_end == response_body.size()) {
        return std::nullopt;
      }

      if (next_line_end - next_line_start >= kScanDllVersionBufferLength) {
        return std::nullopt;
      }

      return std::string(response_body.substr(next_line_start,
                                              next_line_end - next_line_start));
    }

    line_start = line_end + 1;
    if (response_body[line_end] == '\r'
        && line_start < response_body.size()
        && response_body[line_start] == '\n') {
      ++line_start;
    }
  }

  return std::nullopt;
}

[[nodiscard]] bool WriteStagedScanDllModule(
    const std::filesystem::path& current_module_path,
    const std::filesystem::path& staged_module_path,
    const std::string_view bytes,
    const openwow::net::OsUrlDownloadCompletionCode completion_code) {
  std::ofstream staged_output(staged_module_path,
                              std::ios::binary | std::ios::trunc | std::ios::out);
  if (!staged_output.is_open()) {
    return false;
  }

  staged_output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  const bool write_ok = static_cast<bool>(staged_output);
  staged_output.close();

  if (!write_ok) {
    std::error_code ec;
    std::filesystem::remove(current_module_path, ec);
    return false;
  }

  if (completion_code != openwow::net::OsUrlDownloadCompletionCode::kSuccess) {
    std::error_code ec;
    std::filesystem::remove(current_module_path, ec);
  }

  return true;
}

void RunScanDllUpdateWorker(const std::filesystem::path& current_module_path,
                            const std::filesystem::path& staged_module_path,
                            std::string version_url,
                            std::string dll_url,
                            std::optional<std::string> current_version) {
  std::string version_body;
  openwow::net::OsUrlDownloadCompletionCode version_completion =
      openwow::net::OsUrlDownloadCompletionCode::kFailure;
  if (!openwow::net::DownloadUrlToStringWithResult(
          version_url.c_str(),
          &version_body,
          static_cast<std::uint32_t>(kScanDllDownloadTimeoutMs),
          &version_completion)
      || version_completion != openwow::net::OsUrlDownloadCompletionCode::kSuccess) {
    return;
  }

  const auto remote_version = ParseScanDllVersionResponse(version_body);
  if (!remote_version) {
    return;
  }

  if (current_version
      && openwow::text::EqualsIgnoreCaseAscii(*current_version, *remote_version)) {
    return;
  }

  std::string dll_body;
  openwow::net::OsUrlDownloadCompletionCode dll_completion =
      openwow::net::OsUrlDownloadCompletionCode::kFailure;
  if (!openwow::net::DownloadUrlToStringWithResult(
          dll_url.c_str(),
          &dll_body,
          static_cast<std::uint32_t>(kScanDllDownloadTimeoutMs),
          &dll_completion)) {
    return;
  }

  (void)WriteStagedScanDllModule(current_module_path,
                                 staged_module_path,
                                 dll_body,
                                 dll_completion);
}

[[nodiscard]] ScanDllExecutionResult MakeScanDllErrorResult() {
  return {};
}

#if defined(_WIN32)

constexpr std::size_t kScanDllDetailBufferLength = 0x200;

[[nodiscard]] ScanDllExecutionResult MapScanDllExecutionResult(
    const int verdict_code,
    const bool continue_anyway_blocked,
    std::string detail_text) {
  ScanDllExecutionResult result;
  result.status = ScanDllStatus::kComplete;

  switch (verdict_code) {
    case 0:
      result.finished = true;
      result.continue_anyway_blocked = false;
      result.result_primary_text.clear();
      result.result_secondary_text.clear();
      return result;

    case 1:
      result.finished = false;
      result.continue_anyway_blocked = continue_anyway_blocked;
      result.result_primary_text = "HACK";
      result.result_secondary_text = std::move(detail_text);
      return result;

    case 2:
    case 3:
      result.finished = false;
      result.continue_anyway_blocked = continue_anyway_blocked;
      result.result_primary_text = "TROJAN";
      result.result_secondary_text = std::move(detail_text);
      return result;

    default:
      return MakeScanDllErrorResult();
  }
}
#endif

[[nodiscard]] ScanDllExecutionResult ExecuteScanDllModuleDefault(
    const std::filesystem::path& module_path) {
#if defined(_WIN32)
  using ScanDllExportV3 = int(__stdcall*)(int*, int*, std::uint8_t*, int);
  using ScanDllExportV1 = int(__stdcall*)(int*, std::uint8_t*, int);

  std::error_code ec;
  if (!std::filesystem::is_regular_file(module_path, ec)) {
    return MakeScanDllErrorResult();
  }

  const std::string module_path_string = module_path.string();
  HMODULE module = ::LoadLibraryA(module_path_string.c_str());
  if (module == nullptr) {
    return MakeScanDllErrorResult();
  }

  std::array<std::uint8_t, kScanDllDetailBufferLength> detail_buffer{};
  int verdict_code = 0;
  int continue_flag = 0;
  bool call_succeeded = false;

  if (const auto export_v3 = reinterpret_cast<ScanDllExportV3>(
          ::GetProcAddress(module, MAKEINTRESOURCEA(3)));
      export_v3 != nullptr) {
    call_succeeded = export_v3(&verdict_code,
                               &continue_flag,
                               detail_buffer.data(),
                               static_cast<int>(detail_buffer.size()))
        != 0;
  } else if (const auto export_v1 = reinterpret_cast<ScanDllExportV1>(
                 ::GetProcAddress(module, MAKEINTRESOURCEA(1)));
             export_v1 != nullptr) {
    call_succeeded = export_v1(&verdict_code,
                               detail_buffer.data(),
                               static_cast<int>(detail_buffer.size()))
        != 0;
  }

  ::FreeLibrary(module);
  detail_buffer.back() = 0;

  if (!call_succeeded) {
    return MakeScanDllErrorResult();
  }

  return MapScanDllExecutionResult(
      verdict_code,
      continue_flag != 0,
      std::string(reinterpret_cast<const char*>(detail_buffer.data())));
#else
  (void)module_path;
  return MakeScanDllErrorResult();
#endif
}

ScanDllRuntimeState& GetScanDllRuntime() {
  static ScanDllRuntimeState state{
      .executor = ExecuteScanDllModuleDefault,
  };
  return state;
}

template <typename T>
[[nodiscard]] bool FutureReady(const std::future<T>& future) {
  return future.valid()
      && future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

void ReapCompletedScanDllUpdateWorker() {
  auto& runtime = GetScanDllRuntime();
  std::optional<std::future<void>> ready_future;

  {
    std::lock_guard lock(runtime.mutex);
    if (!runtime.update_future || !FutureReady(*runtime.update_future)) {
      return;
    }
    ready_future = std::move(*runtime.update_future);
    runtime.update_future.reset();
  }

  if (!ready_future || !ready_future->valid()) {
    return;
  }

  try {
    ready_future->get();
  } catch (...) {
  }
}

[[nodiscard]] bool IsScanDllUpdateWorkerRunning() {
  ReapCompletedScanDllUpdateWorker();

  auto& runtime = GetScanDllRuntime();
  std::lock_guard lock(runtime.mutex);
  return runtime.update_future && !FutureReady(*runtime.update_future);
}

[[nodiscard]] bool StartScanDllExecutionWorker(const std::filesystem::path& module_path) {
  auto& runtime = GetScanDllRuntime();
  ScanDllExecutorForTests executor;
  {
    std::lock_guard lock(runtime.mutex);
    if (runtime.execution_future && !FutureReady(*runtime.execution_future)) {
      return false;
    }
    executor = runtime.executor ? runtime.executor : ExecuteScanDllModuleDefault;
  }

  try {
    auto worker_future = std::async(
        std::launch::async,
        [executor = std::move(executor), module_path]() mutable {
          return executor(module_path);
        });

    std::lock_guard lock(runtime.mutex);
    runtime.execution_future.emplace(std::move(worker_future));
    return true;
  } catch (const std::future_error&) {
    return false;
  }
}

void StartScanDllUpdateWorker(const std::filesystem::path& current_module_path,
                              const std::filesystem::path& staged_module_path,
                              std::string version_url,
                              std::string dll_url,
                              std::optional<std::string> current_version) {
  auto& runtime = GetScanDllRuntime();

  try {
    auto update_future = std::async(
        std::launch::async,
        [current_module_path,
         staged_module_path,
         version_url = std::move(version_url),
         dll_url = std::move(dll_url),
         current_version = std::move(current_version)]() mutable {
          RunScanDllUpdateWorker(current_module_path,
                                 staged_module_path,
                                 std::move(version_url),
                                 std::move(dll_url),
                                 std::move(current_version));
        });

    std::lock_guard lock(runtime.mutex);
    runtime.update_future.emplace(std::move(update_future));
  } catch (const std::future_error&) {
  }
}

[[nodiscard]] std::optional<ScanDllExecutionResult> TryTakeScanDllExecutionResult() {
  auto& runtime = GetScanDllRuntime();
  std::future<ScanDllExecutionResult> ready_future;

  {
    std::lock_guard lock(runtime.mutex);
    if (!runtime.execution_future || !FutureReady(*runtime.execution_future)) {
      return std::nullopt;
    }
    ready_future = std::move(*runtime.execution_future);
    runtime.execution_future.reset();
  }

  try {
    return ready_future.get();
  } catch (...) {
    return MakeScanDllErrorResult();
  }
}

[[nodiscard]] bool StartScanDllWorker(const std::string_view version_url,
                                      const std::string_view dll_url) {
  const auto current_module_path = ResolveScanDllModulePath();
  const auto staged_module_path = ResolveScanDllStagedModulePath();
  const auto current_version =
      PrepareScanDllModuleVersion(current_module_path, staged_module_path);

  StartScanDllUpdateWorker(current_module_path,
                           staged_module_path,
                           std::string(version_url),
                           std::string(dll_url),
                           current_version);
  return StartScanDllExecutionWorker(current_module_path);
}

[[nodiscard]] std::optional<ScanDllExecutionResult> TryTakeScanDllWorkerResult() {
  ReapCompletedScanDllUpdateWorker();
  return TryTakeScanDllExecutionResult();
}

std::string NormalizeLoginCameraModelPath(std::string_view raw_path) {
  if (raw_path.empty()) {
    return {};
  }

  std::string normalized(raw_path);
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  std::filesystem::path model_path(normalized);
  const std::string extension = model_path.extension().string();
  if (extension == ".mdx" || extension == ".mdl") {
    model_path.replace_extension(".m2");
    normalized = model_path.generic_string();
  } else if (extension.empty()) {
    normalized += ".m2";
  }

  return normalized;
}

std::uint32_t ResolveCharLoginCinematicSequenceId(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint8_t class_id,
    const std::uint8_t race_id) {
  if (class_id != 0) {
    if (const auto* class_entry = dbc.chr_classes().LookupEntry(class_id);
        class_entry != nullptr && class_entry->cinematic_sequence_id != 0) {
      return class_entry->cinematic_sequence_id;
    }
  }

  if (race_id != 0) {
    if (const auto* race_entry = dbc.chr_races().LookupEntry(race_id);
        race_entry != nullptr && race_entry->cinematic_sequence_id != 0) {
      return race_entry->cinematic_sequence_id;
    }
  }

  return 0;
}

void BuildLoginCameraTransform(const float origin_xyz[3],
                               const float facing_radians,
                               float* out_transform4x4) {
  openwow::math::row_major_mat4x4::SetIdentity(out_transform4x4);

  const float sine = std::sin(facing_radians);
  const float cosine = std::cos(facing_radians);
  out_transform4x4[0] = cosine;
  out_transform4x4[1] = sine;
  out_transform4x4[4] = -sine;
  out_transform4x4[5] = cosine;
  out_transform4x4[12] = origin_xyz[0];
  out_transform4x4[13] = origin_xyz[1];
  out_transform4x4[14] = origin_xyz[2];
}

CGlueMgrInternalState& GetInternal() {
  static CGlueMgrInternalState s;
  return s;
}

[[nodiscard]] std::string TruncateGlueScreenName(std::string_view screen) {
  constexpr std::size_t kStoredBufferLength = 64;
  constexpr std::size_t kMaxStoredChars = kStoredBufferLength - 1;
  return std::string(screen.substr(0, kMaxStoredChars));
}

void ResetStateToIdle(CGlueMgrInternalState& state) {
  state.current_state = GlueState::kIdle;
  state.status_counter = 0;
}

[[nodiscard]] const openwow::net::wotlk::RealmInfo* GetSelectedRealmInfo(
    const GlueGameState& state) {
  const int selected_index = state.selected_realm_index;
  if (selected_index < 0
      || selected_index >= static_cast<int>(state.realms.size())) {
    return nullptr;
  }

  return &state.realms[static_cast<std::size_t>(selected_index)];
}

void SetRealmNameCVar(const std::string& realm_name) {
  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.Exists("realmName")) {
    cvars.RegisterCVar("realmName", "");
  }

  (void)cvars.SetCVar("realmName", realm_name, true);
}

}

bool FindAndSelectSavedRealm(GlueGameState& state, const bool force_locked) {
  if (state.realms.empty()) {
    return false;
  }

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  const std::string saved_name = cvars.GetCVar("realmName");
  if (saved_name.empty()) {
    return false;
  }

  for (std::size_t i = 0; i < state.realms.size(); ++i) {
    if (!openwow::text::EqualsIgnoreCaseAscii(state.realms[i].name, saved_name)) {
      continue;
    }

    if (state.realms[i].locked && !force_locked) {
      return false;
    }

    state.selected_realm_index = static_cast<int>(i);
    return true;
  }

  return false;
}

bool SetRealmName(GlueGameState& state, const std::string& name) {
  SetRealmNameCVar(name);
  return FindAndSelectSavedRealm(state, true);
}

namespace {

void QueueRealmDisconnect(CGlueMgrInternalState& state, GlueGameState* game_state,
                          const bool reconnect_after_disconnect) {
  state.disconnect_pending = true;
  state.reconnect_after_disconnect = reconnect_after_disconnect;
  if (game_state != nullptr) {
    game_state->connected = false;
    if (!reconnect_after_disconnect) {
      game_state->wants_world_connect = false;
    }
  }
}

void FireOkStatusDialog(const GlueEventCallback& fire_event, std::string message) {
  if (!fire_event) {
    return;
  }

  fire_event("OPEN_STATUS_DIALOG",
             {MakeLuaString("OKAY"), MakeLuaString(std::move(message))});
}

void FireStatusDialog(const GlueEventCallback& fire_event, const char* button,
                      std::string message) {
  if (!fire_event) {
    return;
  }

  fire_event("OPEN_STATUS_DIALOG",
             {MakeLuaString(button), MakeLuaString(std::move(message))});
}

void FireDisconnectedFromServer(const GlueEventCallback& fire_event,
                                const std::uint32_t reason) {
  if (!fire_event) {
    return;
  }

  fire_event("DISCONNECTED_FROM_SERVER",
             {MakeLuaNumber(static_cast<double>(reason))});
}

std::string LocalizedGlueStringOrFallback(const GlueGameState& state,
                                          const char* key,
                                          const char* fallback = nullptr) {
  if (key != nullptr && *key != '\0' && state.resolve_glue_string) {
    const std::string resolved = state.resolve_glue_string(key);
    if (!resolved.empty()) {
      return resolved;
    }
  }

  if (key != nullptr && *key != '\0') {
    auto& localization = openwow::game::Localization::Get();
    if (localization.HasString(key)) {
      return localization.GetString(key);
    }
  }

  return fallback != nullptr ? std::string(fallback) : std::string();
}

bool IsActiveRealmConnectionObject(const void* disconnected_connection) {
  return openwow::net::ClientServices::IsActiveConnectionObject(
      disconnected_connection);
}

std::string BuildDisconnectDialogMessage(const void* disconnected_connection) {
  auto& client_services = openwow::net::ClientServices::Instance();

  const auto info = client_services.GetDisconnectDialogInfo();
  if (!info.has_pending_result || info.operation_success) {
    return {};
  }

  if (info.expected_response ==
      static_cast<std::int32_t>(openwow::net::ConnectionResponse::kAuthWaitQueue)) {
    const auto* const active_connection =
        openwow::net::wotlk::RealmConnection::GetActiveInstance();
    if (active_connection == nullptr || disconnected_connection != active_connection) {
      return {};
    }
  }

  return info.message;
}

void ResetPatchDownloadTiming(CGlueMgrInternalState& state) {
  state.patch_download_elapsed = 0.0f;
  state.patch_download_timer_armed = false;
}

}

void Login_SetScreen(const GlueEventCallback& fire_event,
                     const std::string_view screen_name) {
  if (!fire_event) {
    return;
  }

  fire_event("SET_GLUE_SCREEN", {MakeLuaString(std::string(screen_name))});
}

void CGlueMgr_HandlePatchFailure(const GlueEventCallback& fire_event,
                                 const int error_code,
                                 const unsigned int auxiliary_value) {
  openwow::net::ClientServices::Instance().Disconnect();
  ResetStateToIdle(GetInternal());
  Login_SetScreen(fire_event, "login");

  if (!fire_event) {
    return;
  }

  std::string message =
      openwow::game::Localization::Get().GetString("PATCH_FAILED_MESSAGE",
                                                   "PATCH_FAILED_MESSAGE");
  if (error_code == 2) {
    std::array<char, 1024> formatted_message{};
    const std::string format =
        openwow::game::Localization::Get().GetString("PATCH_FAILED_DISK_FULL",
                                                     "PATCH_FAILED_DISK_FULL");
    const unsigned int clamped_value = std::max(auxiliary_value, 2u);
    openwow::core::SStrPrintf(formatted_message.data(), formatted_message.size(),
                              format.c_str(), clamped_value);
    message.assign(formatted_message.data());
  }

  FireOkStatusDialog(fire_event, std::move(message));
}

void CGlueMgr_StartPatchDownload(GlueGameState& state,
                                 const GlueEventCallback& fire_event) {
  auto& internal = GetInternal();
  internal.current_state = GlueState::kPatchDownload;
  internal.status_counter = 0;
  ResetPatchDownloadTiming(internal);

  if (fire_event) {
    fire_event("CLOSE_STATUS_DIALOG", {});
  }

  Login_SetScreen(fire_event, "patchdownload");
  (void)openwow::net::BootstrapLoginPatchDownload(
      state.login_request.username,
      state.login_request.password);
}

void CGlueMgr_UpdatePatchDownload(GlueGameState& state,
                                  const GlueEventCallback& fire_event,
                                  const float dt_seconds) {
  auto& internal = GetInternal();

  if (const auto restart_request =
          openwow::net::LoginPatchDownloadBridge::Get().TakeQueuedLoginRestart();
      restart_request.has_value()) {
    internal.current_state = GlueState::kAuthenticating;
    internal.status_counter = 0;
    ResetPatchDownloadTiming(internal);

    state.StageLoginRequest(restart_request->account_name,
                            restart_request->password);
    state.wants_login = true;
    return;
  }

  const auto snapshot =
      openwow::net::LoginPatchDownloadBridge::Get().SnapshotActiveDownload();
  if (!snapshot.has_active_download) {
    ResetPatchDownloadTiming(internal);
    return;
  }

  if (!internal.patch_download_timer_armed) {
    internal.patch_download_elapsed = 0.0f;
    internal.patch_download_timer_armed = true;
  }

  switch (snapshot.state) {
    case openwow::net::LoginPatchDownloadState::kComplete: {
      internal.patch_download_progress = 1.0f;
      if (fire_event) {
        fire_event("PATCH_DOWNLOADED", {});
      }

      auto download = openwow::net::LoginPatchDownloadBridge::Get().TakeActiveDownload();
      if (download != nullptr) {
        download->FinalizeSuccess();
        if (download->download_state() == openwow::net::LoginPatchDownloadState::kFailed) {
          const auto failure_info = download->failure_info();
          CGlueMgr_HandlePatchFailure(fire_event,
                                      static_cast<int>(failure_info.result_code),
                                      static_cast<unsigned int>(
                                          failure_info.auxiliary_value));
        } else {
          ResetStateToIdle(internal);
        }
      }
      ResetPatchDownloadTiming(internal);
      return;
    }

    case openwow::net::LoginPatchDownloadState::kFailed: {
      auto download = openwow::net::LoginPatchDownloadBridge::Get().TakeActiveDownload();
      const auto result_code =
          download != nullptr ? download->failure_info().result_code
                              : snapshot.result_code;
      const auto auxiliary_value =
          download != nullptr ? download->failure_info().auxiliary_value : 0u;
      if (download != nullptr) {
        download->Close(true);
      }
      CGlueMgr_HandlePatchFailure(
          fire_event, static_cast<int>(result_code), auxiliary_value);
      ResetPatchDownloadTiming(internal);
      return;
    }

    case openwow::net::LoginPatchDownloadState::kRestartLogin:
    case openwow::net::LoginPatchDownloadState::kInProgress:
      break;
  }

  internal.patch_download_elapsed += std::max(dt_seconds, 0.0f);
  if (internal.patch_download_elapsed <= 0.25f) {
    return;
  }

  internal.patch_download_elapsed = 0.0f;

  internal.patch_download_progress =
      static_cast<float>(snapshot.progress_ratio);
  if (fire_event) {
    fire_event("PATCH_UPDATE_PROGRESS", {});
  }

  auto& client_services = openwow::net::ClientServices::Instance();
  if (client_services.GetLoginConnectionType()
          != openwow::net::LoginConnectionType::kBattleNet
      && !client_services.HasLoginConnection()) {
    if (auto download = openwow::net::LoginPatchDownloadBridge::Get().TakeActiveDownload();
        download != nullptr) {
      download->AbortTransfer();
    }
    ResetPatchDownloadTiming(internal);
    ResetStateToIdle(internal);
    client_services.Disconnect();
    Login_SetScreen(fire_event, "login");
    FireOkStatusDialog(
        fire_event,
        openwow::game::Localization::Get().GetString("DISCONNECTED", "DISCONNECTED"));
  }
}

void CGlueMgr_StartScanDll(GlueGameState& state,
                           const GlueEventCallback& fire_event,
                           const std::string_view version_url,
                           const std::string_view dll_url) {
  auto& internal = GetInternal();
  if (state.scan_dll.status != ScanDllStatus::kIdle
      || internal.current_state == GlueState::kScanDll
      || IsScanDllUpdateWorkerRunning()) {
    return;
  }

  state.scan_dll.status = ScanDllStatus::kRunning;
  state.scan_dll.finished = false;
  state.scan_dll.continue_anyway_blocked = false;
  state.scan_dll.result_primary_text.clear();
  state.scan_dll.result_secondary_text.clear();

  if (!StartScanDllWorker(version_url, dll_url)) {
    ResetStateToIdle(internal);
    state.scan_dll.status = ScanDllStatus::kError;
    if (fire_event) {
      fire_event("SCANDLL_ERROR",
                 {MakeLuaString(LocalizedGlueStringOrFallback(
                     state, "SCANDLL_MESSAGE_ERROR", "SCANDLL_MESSAGE_ERROR"))});
    }
    return;
  }

  internal.current_state = GlueState::kScanDll;
  internal.status_counter = 0;

  if (fire_event) {
    fire_event("SCANDLL_DOWNLOADING",
               {MakeLuaString(LocalizedGlueStringOrFallback(
                   state, "SCANDLL_MESSAGE_SCANNING", "SCANDLL_MESSAGE_SCANNING"))});
  }
}

void CGlueMgr_UpdateScanDll(GlueGameState& state,
                            const GlueEventCallback& fire_event) {
  auto& internal = GetInternal();
  if (internal.current_state != GlueState::kScanDll) {
    return;
  }

  if (state.scan_dll.status == ScanDllStatus::kRunning) {
    const auto worker_result = TryTakeScanDllWorkerResult();
    if (!worker_result) {
      return;
    }

    state.scan_dll.status = worker_result->status;
    state.scan_dll.finished = worker_result->finished;
    state.scan_dll.continue_anyway_blocked =
        worker_result->continue_anyway_blocked;
    state.scan_dll.result_primary_text = worker_result->result_primary_text;
    state.scan_dll.result_secondary_text = worker_result->result_secondary_text;
  }

  if (state.scan_dll.status != ScanDllStatus::kError
      && state.scan_dll.status != ScanDllStatus::kComplete) {
    return;
  }

  ResetStateToIdle(internal);
  if (!fire_event) {
    return;
  }

  if (state.scan_dll.status == ScanDllStatus::kError) {
    fire_event("SCANDLL_ERROR",
               {MakeLuaString(LocalizedGlueStringOrFallback(
                   state, "SCANDLL_MESSAGE_ERROR", "SCANDLL_MESSAGE_ERROR"))});
    return;
  }

  fire_event("SCANDLL_FINISHED",
             {MakeLuaString(state.scan_dll.result_primary_text),
              MakeLuaString(state.scan_dll.result_secondary_text),
              MakeLuaNumber(state.scan_dll.continue_anyway_blocked ? 1.0 : 0.0)});
}

float CGlueMgr_GetPatchDownloadProgress() {
  return GetInternal().patch_download_progress;
}

void CGlueMgr_ResetPatchDownloadRuntimeForTests() {
  auto& internal = GetInternal();
  internal.patch_download_progress = 0.0f;
  ResetPatchDownloadTiming(internal);
  openwow::net::LoginPatchDownloadBridge::Get().Clear();
}

void CGlueMgr_SetScanDllExecutorForTests(ScanDllExecutorForTests executor) {
  auto& runtime = GetScanDllRuntime();
  std::lock_guard lock(runtime.mutex);
  runtime.executor = executor ? std::move(executor) : ExecuteScanDllModuleDefault;
}

void CGlueMgr_ResetScanDllRuntimeForTests() {
  auto& runtime = GetScanDllRuntime();
  std::optional<std::future<ScanDllExecutionResult>> execution_future;
  std::optional<std::future<void>> update_future;
  {
    std::lock_guard lock(runtime.mutex);
    execution_future = std::move(runtime.execution_future);
    update_future = std::move(runtime.update_future);
    runtime.execution_future.reset();
    runtime.update_future.reset();
    runtime.executor = ExecuteScanDllModuleDefault;
  }

  if (execution_future && execution_future->valid()) {
    execution_future->wait();
  }
  if (update_future && update_future->valid()) {
    update_future->wait();
  }
}

void CGlueMgr_InitFFXEffects() {
  auto& s = GetInternal();
  if (s.ffx_initialized) return;
  s.ffx_initialized = true;

}

void CGlueMgr_SetGlueScreen(GlueGameState& state, const std::string& new_screen) {
  const std::string old_screen = state.current_screen;
  const std::string stored_screen = TruncateGlueScreenName(new_screen);

  state.current_screen = stored_screen;

  if (state.background_controller != nullptr) {
    state.background_controller->OnScreenTransition(old_screen, stored_screen);
  }

  if (state.on_screen_transition != nullptr &&
      (UsesCharacterScreenHandler(old_screen) || UsesCharacterScreenHandler(stored_screen))) {
    state.on_screen_transition(old_screen, stored_screen);
  }
}

void CGlueMgr_ConnectToRealm(GlueGameState& state) {
  const auto* const realm = GetSelectedRealmInfo(state);
  if (realm == nullptr) {
    return;
  }

  SetRealmName(state, realm->name);

  auto& s = GetInternal();

  if (state.connected) {

    QueueRealmDisconnect(s, &state, true);
    openwow::net::ClientServices::Instance().DisconnectAndCleanup();
    return;
  }

  s.current_state = GlueState::kConnecting;
  s.status_counter = 0;
  state.status_dialog_type = StatusDialogType::kCancel;
  FireStatusDialog(state.fire_event, "CANCEL",
                   LocalizedGlueStringOrFallback(state, "GAME_SERVER_LOGIN",
                                                 "GAME_SERVER_LOGIN"));

  state.wants_world_connect = true;
}

void CGlueMgr_EnterWorld(GlueGameState& state) {
  state.wants_enter_world = true;
}

void CGlueMgr_CleanupEnterWorldCharacterScenes(GlueGameState& state) {
  if (state.char_select_scene != nullptr) {
    state.char_select_scene->ReleaseContent();
  }
  if (state.char_customize_scene != nullptr) {
    state.char_customize_scene->ReleaseContent();
  }
}

void CGlueMgr_CleanupCharCreateForEnterWorld(GlueGameState& state) {

  state.ClearCreateCustomizationCache();
}

void CGlueMgr_SendCharCreate(GlueGameState& state) {
  auto& s = GetInternal();
  if (!state.char_create_request.pending) return;

  s.current_state = GlueState::kCharCreateInProgress;
  s.status_counter = 0;
  state.status_dialog_type = StatusDialogType::kCancel;
  FireStatusDialog(
      state.fire_event,
      "CANCEL",
      LocalizedGlueStringOrFallback(
          state,
          openwow::net::ClientServices::GetResultString(
              static_cast<int>(openwow::net::ConnectionResponse::kCharCreateInProgress)),
          "CHAR_CREATE_IN_PROGRESS"));

  state.wants_create_character = true;
}

void CGlueMgr_SendCharDelete(GlueGameState& state, std::uint64_t guid) {
  if (guid == 0) return;

  auto& s = GetInternal();
  s.current_state = GlueState::kCharDeleteInProgress;
  s.status_counter = 0;
  state.status_dialog_type = StatusDialogType::kCancel;
  FireStatusDialog(
      state.fire_event,
      "CANCEL",
      LocalizedGlueStringOrFallback(
          state,
          openwow::net::ClientServices::GetResultString(
              static_cast<int>(openwow::net::ConnectionResponse::kCharDeleteInProgress)),
          "CHAR_DELETE_IN_PROGRESS"));

  state.char_delete_request.pending = true;
  state.char_delete_request.guid = guid;
  state.wants_delete_character = true;
}

bool CGlueMgr_SendCharRename(GlueGameState& state, std::uint64_t guid,
                              const std::string& new_name) {
  if (guid == 0 || new_name.empty()) return true;

  const auto result_code = openwow::game::ValidateGlueCharacterNameResultCode(new_name);
  if (result_code != 87) {
    if (state.fire_event) {
      state.fire_event(
          "FORCE_RENAME_CHARACTER",
          {MakeLuaString(openwow::net::ClientServices::GetResultString(result_code))});
    }
    return false;
  }

  auto& s = GetInternal();
  s.current_state = GlueState::kCharRenameInProgress;
  s.status_counter = 0;
  state.status_dialog_type = StatusDialogType::kCancel;
  FireStatusDialog(state.fire_event, "CANCEL",
                   LocalizedGlueStringOrFallback(state, "CHAR_RENAME_IN_PROGRESS",
                                                 "CHAR_RENAME_IN_PROGRESS"));

  state.char_rename_request.pending = true;
  state.char_rename_request.guid = guid;
  state.char_rename_request.new_name = new_name;
  state.wants_rename_character = true;
  return true;
}

bool CGlueMgr_SendDeclinedCharacterNames(
    GlueGameState& state, std::uint64_t guid,
    const std::array<std::string, 5>& declined_forms) {
  const auto* selected_character =
      CGlueMgr_GetCharacterEntry(state, state.selected_character_index);
  if (selected_character == nullptr) {
    return true;
  }

  for (const auto& form : declined_forms) {
    const std::uint8_t result_code =
        openwow::game::declension::ValidateDeclinedCharacterForm(
            selected_character->name, form);
    if (result_code != 87) {
      if (state.fire_event) {
        state.fire_event(
            "FORCE_DECLINE_CHARACTER",
            {MakeLuaString(openwow::net::ClientServices::GetResultString(result_code))});
      }
      return false;
    }
  }

  auto& s = GetInternal();
  s.current_state = GlueState::kCharDeclineInProgress;
  s.status_counter = 0;
  state.status_dialog_type = StatusDialogType::kCancel;
  FireStatusDialog(state.fire_event, "CANCEL",
                   LocalizedGlueStringOrFallback(state, "CHAR_DECLINE_IN_PROGRESS",
                                                 "CHAR_DECLINE_IN_PROGRESS"));

  state.char_decline_request.pending = true;
  state.char_decline_request.guid = guid;
  state.char_decline_request.base_name = selected_character->name;
  state.char_decline_request.forms = declined_forms;
  state.wants_decline_character = true;
  return true;
}

bool CGlueMgr_SendCharCustomize(GlueGameState& state, std::uint64_t guid,
                                 const std::string& name,
                                 std::uint8_t gender, std::uint8_t skin,
                                 std::uint8_t hair_style, std::uint8_t hair_color,
                                 std::uint8_t facial_hair, std::uint8_t face) {
  if (guid == 0 || name.empty()) return true;

  auto& s = GetInternal();
  s.current_state = GlueState::kCharCustomizeInProgress;
  s.status_counter = 0;
  state.status_dialog_type = StatusDialogType::kCancel;
  FireStatusDialog(state.fire_event, "CANCEL",
                   LocalizedGlueStringOrFallback(state, "CHAR_CUSTOMIZE_IN_PROGRESS",
                                                 "CHAR_CUSTOMIZE_IN_PROGRESS"));

  state.char_customize_request.pending = true;
  state.char_customize_request.guid = guid;
  state.char_customize_request.name = name;
  state.char_customize_request.gender = gender;
  state.char_customize_request.skin = skin;
  state.char_customize_request.face = face;
  state.char_customize_request.hair_style = hair_style;
  state.char_customize_request.hair_color = hair_color;
  state.char_customize_request.facial_hair = facial_hair;
  state.wants_customize_character = true;
  return true;
}

bool CGlueMgr_SendFactionChange(GlueGameState& state, std::uint64_t guid,
                                 const std::string& name,
                                 std::uint8_t gender, std::uint8_t skin,
                                 std::uint8_t hair_style, std::uint8_t hair_color,
                                 std::uint8_t facial_hair, std::uint8_t face,
                                 std::uint8_t race) {
  if (guid == 0 || name.empty()) return true;

  auto& s = GetInternal();
  s.current_state = GlueState::kCharCustomizeInProgress;
  s.status_counter = 0;
  state.status_dialog_type = StatusDialogType::kCancel;
  FireStatusDialog(state.fire_event, "CANCEL",
                   LocalizedGlueStringOrFallback(state, "FACTION_CHANGE_IN_PROGRESS",
                                                 "FACTION_CHANGE_IN_PROGRESS"));

  state.char_faction_change_request.pending = true;
  state.char_faction_change_request.guid = guid;
  state.char_faction_change_request.name = name;
  state.char_faction_change_request.gender = gender;
  state.char_faction_change_request.skin = skin;
  state.char_faction_change_request.face = face;
  state.char_faction_change_request.hair_style = hair_style;
  state.char_faction_change_request.hair_color = hair_color;
  state.char_faction_change_request.facial_hair = facial_hair;
  state.char_faction_change_request.race = race;
  state.wants_faction_change = true;
  return true;
}

bool CGlueMgr_SendRaceChange(GlueGameState& state, std::uint64_t guid,
                              const std::string& name,
                              std::uint8_t gender, std::uint8_t skin,
                              std::uint8_t hair_style, std::uint8_t hair_color,
                              std::uint8_t facial_hair, std::uint8_t face,
                              std::uint8_t race) {
  if (guid == 0 || name.empty()) return true;

  auto& s = GetInternal();
  s.current_state = GlueState::kCharCustomizeInProgress;
  s.status_counter = 0;
  state.status_dialog_type = StatusDialogType::kCancel;
  FireStatusDialog(state.fire_event, "CANCEL",
                   LocalizedGlueStringOrFallback(state, "RACE_CHANGE_IN_PROGRESS",
                                                 "RACE_CHANGE_IN_PROGRESS"));

  state.char_race_change_request.pending = true;
  state.char_race_change_request.guid = guid;
  state.char_race_change_request.name = name;
  state.char_race_change_request.gender = gender;
  state.char_race_change_request.skin = skin;
  state.char_race_change_request.face = face;
  state.char_race_change_request.hair_style = hair_style;
  state.char_race_change_request.hair_color = hair_color;
  state.char_race_change_request.facial_hair = facial_hair;
  state.char_race_change_request.race = race;
  state.wants_race_change = true;
  return true;
}

void CGlueMgr_RequestCharacterList(GlueGameState& state) {
  auto& s = GetInternal();
  if (s.current_state == GlueState::kCharEnumPending) return;

  s.current_state = GlueState::kCharListRetrieving;
  s.status_counter = 0;
  state.status_dialog_type = StatusDialogType::kCancel;
  FireStatusDialog(state.fire_event, "CANCEL",
                   LocalizedGlueStringOrFallback(
                       state, "CHAR_LIST_RETRIEVING", "CHAR_LIST_RETRIEVING"));
  openwow::net::ClientServices::Instance().GetCharacters();
  state.wants_character_list_refresh = true;
}

void CGlueMgr_ResetCharacterListDisplay(GlueGameState& state) {
  state.characters.clear();
  state.selected_character_index = -1;
  state.wants_enter_world = false;
  if (state.char_select_scene != nullptr) {
    state.char_select_scene->SyncFromGameState(state);
  }
}

void CGlueMgr_RequestRealmList(GlueGameState& state, bool show_dialog) {
  auto& s = GetInternal();
  s.current_state = GlueState::kRealmListPending;
  s.status_counter = 0;

  state.request_realm_list_show_dialog = show_dialog;
  state.request_realm_list_dialog_opened = false;
  if (show_dialog) {
    state.status_dialog_type = StatusDialogType::kCancel;
    if (state.fire_event) {
      FireStatusDialog(state.fire_event, "CANCEL",
                       LocalizedGlueStringOrFallback(state, "REALM_LIST_IN_PROGRESS",
                                                     "REALM_LIST_IN_PROGRESS"));
      state.request_realm_list_dialog_opened = true;
    }
  }

  openwow::net::ClientServices::Instance().GetRealmList();
  state.wants_realm_list_refresh = true;
}

void CGlueMgr_ResetStateToIdle() {
  ResetStateToIdle(GetInternal());
}

int CGlueMgr_GetStateValue() {
  return static_cast<int>(GetInternal().current_state);
}

void CGlueMgr_SetStateValue(const int state_value) {
  auto& s = GetInternal();
  s.current_state = static_cast<GlueState>(state_value);
  s.status_counter = 0;
}

bool CGlueMgr_SetupCharLoginCamera(
    openwow::render::m2::M2System& m2_system,
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::vfs::VirtualFileSystem& vfs,
    const std::uint8_t class_id,
    const std::uint8_t race_id,
    float* const out_camera_position_xyz) {
  if (out_camera_position_xyz == nullptr) {
    return false;
  }

  const std::uint32_t cinematic_sequence_id =
      ResolveCharLoginCinematicSequenceId(dbc, class_id, race_id);
  if (cinematic_sequence_id == 0) {
    return false;
  }

  const auto* cinematic_sequence =
      dbc.cinematic_sequences().LookupEntry(cinematic_sequence_id);
  if (cinematic_sequence == nullptr || cinematic_sequence->camera_ids[0] == 0) {
    return false;
  }

  const auto* cinematic_camera =
      dbc.cinematic_camera().LookupEntry(cinematic_sequence->camera_ids[0]);
  if (cinematic_camera == nullptr || cinematic_camera->model.empty()) {
    return false;
  }

  const std::string model_path =
      NormalizeLoginCameraModelPath(cinematic_camera->model);
  if (model_path.empty()) {
    return false;
  }

  m2_system.SetFileLoader(
      [&vfs](const std::string& path) -> std::vector<std::uint8_t> {
        const auto bytes = vfs.ReadFileBytes(path);
        return bytes.value_or(std::vector<std::uint8_t>{});
      });
  const auto load_result = m2_system.LoadModelForSampling(model_path);
  if (load_result.status != openwow::render::m2::M2ResultStatus::kReady ||
      load_result.model_id == 0) {
    return false;
  }

  const auto camera_pose = m2_system.QueryCameraSample(load_result.model_id, 0, 0, 0);
  if (camera_pose.status != openwow::render::m2::M2ResultStatus::kReady) {
    return false;
  }

  const float origin_xyz[3] = {
      cinematic_camera->origin_x,
      cinematic_camera->origin_y,
      cinematic_camera->origin_z,
  };
  float world_transform[16];
  BuildLoginCameraTransform(origin_xyz, cinematic_camera->origin_facing,
                            world_transform);

  const float local_camera_position[3] = {
      camera_pose.pose.position[0],
      camera_pose.pose.position[1],
      camera_pose.pose.position[2],
  };
  openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
      out_camera_position_xyz, local_camera_position, world_transform);
  return true;
}

const openwow::net::wotlk::CharacterSummary*
CGlueMgr_GetCharacterEntry(const GlueGameState& state, int index) {
  if (index < 0 || index >= static_cast<int>(state.characters.size())) {
    return nullptr;
  }
  return &state.characters[static_cast<std::size_t>(index)];
}

void CGlueMgr_SetDisconnectReason(const std::uint32_t reason) {
  GetInternal().disconnect_reason = reason;
}

void CGlueMgr_RequestSilentDisconnect(GlueGameState* state) {
  QueueRealmDisconnect(GetInternal(), state, false);
}

namespace {

bool HandleNetDisconnect(GlueGameState& state,
                         const bool is_active_realm_connection,
                         const void* disconnected_connection) {
  auto& s = GetInternal();
  const bool display_disconnect_script =
      (s.current_state != GlueState::kConnecting);

  ResetStateToIdle(s);

  if (s.disconnect_pending) {
    s.disconnect_pending = false;
    state.connected = false;
    if (s.reconnect_after_disconnect) {
      s.reconnect_after_disconnect = false;
      s.current_state = GlueState::kConnecting;
      s.status_counter = 0;
      state.status_dialog_type = StatusDialogType::kCancel;
      FireStatusDialog(state.fire_event, "CANCEL",
                       LocalizedGlueStringOrFallback(state, "GAME_SERVER_LOGIN",
                                                     "GAME_SERVER_LOGIN"));
      state.wants_world_connect = true;
    }
    return true;
  }

  if (!is_active_realm_connection) {
    return true;
  }

  state.connected = false;
  state.wants_world_connect = false;

  if (!display_disconnect_script) {
    if (const std::string dialog_message =
            BuildDisconnectDialogMessage(disconnected_connection);
        !dialog_message.empty()) {
      state.status_dialog_type = StatusDialogType::kOkay;
      FireOkStatusDialog(state.fire_event, dialog_message);
    } else {

      SetRealmName(state, "");
      state.status_dialog_type = StatusDialogType::kNone;
      FireDisconnectedFromServer(state.fire_event, s.disconnect_reason);
    }
  } else {
    state.status_dialog_type = StatusDialogType::kNone;
    FireDisconnectedFromServer(state.fire_event, s.disconnect_reason);
  }

  openwow::net::ClientServices::Instance().Disconnect();
  return true;
}

}

bool CGlueMgr_NetDisconnectHandler(GlueGameState& state,
                                   const void* disconnected_connection) {
  return HandleNetDisconnect(
      state, IsActiveRealmConnectionObject(disconnected_connection),
      disconnected_connection);
}

bool CGlueMgr_NetDisconnectHandler(GlueGameState& state) {
  return HandleNetDisconnect(state, true, nullptr);
}

}

#include "openwow/ui/glue/legal_notice_sync.h"

#include "openwow/data/startup_filesystem_state.h"
#include "openwow/game/localization.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/glue/cgluemgr.h"
#include "openwow/ui/glue/server_alert_sync.h"
#include "openwow/vfs/sfile_core.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace openwow::ui::glue {

namespace {

constexpr int kLatestAgreementsTimeoutMs = 5000;
constexpr std::uint32_t kAgreementsArchiveFlags = 0x800u;

struct LegalNoticeDefinition {
  LegalNoticeId id;
  const char* cvar_name;
  const char* filename;
  bool preserve_notice_after_remote_sync_accept = false;
};

constexpr std::array<LegalNoticeDefinition, 5> kLegalNoticeDefinitions = {{
    {LegalNoticeId::kTos, "readTOS", "tos.html", true},
    {LegalNoticeId::kEula, "readEULA", "eula.html", true},
    {LegalNoticeId::kTerminationWithoutNotice,
     "readTerminationWithoutNotice",
     "termination.html",
     true},
    {LegalNoticeId::kScanning, "readScanning", nullptr, false},
    {LegalNoticeId::kContest, "readContest", nullptr, false},
}};

std::size_t ToIndex(const LegalNoticeId id) {
  return static_cast<std::size_t>(id);
}

const LegalNoticeDefinition& NoticeDefinition(const LegalNoticeId id) {
  return kLegalNoticeDefinitions[ToIndex(id)];
}

std::filesystem::path ResolveClientRoot() {
  const auto& startup_state = openwow::data::GetStartupFileSystemState();
  if (!startup_state.executable_base_path.empty()) {
    return std::filesystem::path(startup_state.executable_base_path);
  }
  return std::filesystem::current_path();
}

std::filesystem::path ResolveArchiveDataRoot() {
  auto root = ResolveClientRoot();
  const auto& startup_state = openwow::data::GetStartupFileSystemState();
  if (startup_state.archive_data_path.empty()) {
    return root / "Data";
  }

  std::string archive_data_path = startup_state.archive_data_path;
  for (char& ch : archive_data_path) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  return (root / std::filesystem::path(archive_data_path)).lexically_normal();
}

std::filesystem::path BuildDataFilePath(const std::string_view filename,
                                        const bool locale_prefix) {
  auto path = ResolveArchiveDataRoot();
  if (locale_prefix) {
    path /= openwow::game::Localization::Get().GetLocaleName();
  }
  path /= std::filesystem::path(std::string(filename));
  return path;
}

void UpdateLegalNoticeCVarForSyncedFile(const char* const cvar_name) {
  if (cvar_name == nullptr || *cvar_name == '\0') {
    return;
  }

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  const int current_value = cvars.GetCVarInt(cvar_name);
  (void)cvars.SetCVar(cvar_name, current_value >= 1 ? "-1" : "-2", true);
}

void ResetDownloadedBodyStorage(std::string& body) {
  std::string empty;
  body.swap(empty);
}

bool RemoveExistingRegularFile(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) {
    return true;
  }

  return std::filesystem::remove(path, ec);
}

bool WriteBinaryFileWithoutCreatingParents(const std::filesystem::path& path,
                                           const std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }

  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  return static_cast<bool>(output);
}

void ProcessDownloadedArchiveToDisk(std::string_view archive_bytes) {
  if (archive_bytes.empty()) {
    return;
  }

  const auto client_root = ResolveClientRoot();
  const bool locale_prefix = openwow::data::ProbeCommonArchiveLayout(client_root);
  const auto archive_path = BuildDataFilePath("agreements.mpq", locale_prefix);

  if (!WriteBinaryFileWithoutCreatingParents(archive_path, archive_bytes)) {
    return;
  }

  void* archive_handle = nullptr;
  if (!openwow::vfs::SFileOpenArchiveWrapped(archive_path.string().c_str(),
                                             0,
                                             kAgreementsArchiveFlags,
                                             &archive_handle)) {
    std::error_code ec;
    std::filesystem::remove(archive_path, ec);
    return;
  }

  for (const auto& notice : kLegalNoticeDefinitions) {
    if (notice.filename == nullptr) {
      continue;
    }

    void* loaded_data = nullptr;
    int loaded_size = 0;
    if (!openwow::vfs::SFileReadFileToBuffer(
            archive_handle, notice.filename, &loaded_data, &loaded_size, 0, 0)) {
      continue;
    }

    const std::string_view source_bytes(
        static_cast<const char*>(loaded_data),
        static_cast<std::size_t>(loaded_size));
    (void)CGlueMgr_SyncDataFile(
        notice.filename, source_bytes, notice.cvar_name, locale_prefix);
    openwow::vfs::SFileFreeLoadedData(loaded_data);
  }

  (void)openwow::vfs::SFileCloseArchiveWrapped(archive_handle);
  std::error_code ec;
  std::filesystem::remove(archive_path, ec);
}

}

LegalNoticeState& LegalNoticeState::Get() {
  static LegalNoticeState instance;
  return instance;
}

std::size_t LegalNoticeState::ToIndex(const LegalNoticeId id) {
  return openwow::ui::glue::ToIndex(id);
}

void LegalNoticeState::InitializeFromCVars() {
  auto& cvars = openwow::ui::game::CVarSystem::Instance();

  std::lock_guard lock(mutex_);
  for (const auto& notice : kLegalNoticeDefinitions) {
    const int value = cvars.GetCVarInt(notice.cvar_name);
    notices_[ToIndex(notice.id)] = {
        .show = value < 0,
        .accepted = value == 1,
    };
  }
  initialized_ = true;
}

void LegalNoticeState::EnsureInitializedFromCVars() {
  {
    std::lock_guard lock(mutex_);
    if (initialized_) {
      return;
    }
  }

  InitializeFromCVars();
}

bool LegalNoticeState::IsAccepted(const LegalNoticeId id) const {
  const_cast<LegalNoticeState*>(this)->EnsureInitializedFromCVars();
  std::lock_guard lock(mutex_);
  return notices_[ToIndex(id)].accepted;
}

bool LegalNoticeState::ShouldShow(const LegalNoticeId id) const {
  const_cast<LegalNoticeState*>(this)->EnsureInitializedFromCVars();
  std::lock_guard lock(mutex_);
  return notices_[ToIndex(id)].show;
}

void LegalNoticeState::Accept(const LegalNoticeId id) {
  EnsureInitializedFromCVars();

  const auto& notice = NoticeDefinition(id);
  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  const int current_value = cvars.GetCVarInt(notice.cvar_name);

  {
    std::lock_guard lock(mutex_);
    auto& flags = notices_[ToIndex(id)];
    flags.show = false;
    flags.accepted = true;
  }

  const char* persisted_value = "1";
  if (notice.preserve_notice_after_remote_sync_accept && current_value == -2) {
    persisted_value = "-1";
  }

  (void)cvars.SetCVar(notice.cvar_name, persisted_value, true);
}

void LegalNoticeState::ResetForTests() {
  std::lock_guard lock(mutex_);
  for (auto& notice : notices_) {
    notice = {};
  }
  initialized_ = false;
}

LatestAgreementsService::LatestAgreementsService()
    : dependencies_(MakeDefaultDependencies()) {}

LatestAgreementsService& LatestAgreementsService::Get() {
  static LatestAgreementsService instance;
  return instance;
}

void InitializeGlueStartupState() {
  LegalNoticeState::Get().InitializeFromCVars();

  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  cvars.RegisterCVar(
      "synchronizeSettings",
      "1",
      openwow::ui::game::CVarFlags::Archive
          | openwow::ui::game::CVarFlags::ServerSent,
      "Whether client settings should be stored on the server");

  (void)LatestAgreementsService::Get().Start();
  (void)ServerAlertService::Get().Start();
}

LatestAgreementsService::Dependencies
LatestAgreementsService::MakeDefaultDependencies() {
  return {
      .start_download =
          [](const char* const url,
             openwow::net::OsUrlDownloadCallbackFn callback,
             void* const callback_data,
             const int timeout_ms) {
            return openwow::net::OsURLDownload_Start(
                url, callback, callback_data, timeout_ms);
          },
      .resolve_url = [] {
        return openwow::game::Localization::Get().GetString(
            "LATEST_AGREEMENTS_URL");
      },
      .process_archive = ProcessDownloadedArchiveToDisk,
  };
}

bool LatestAgreementsService::Start() {
  Dependencies deps;
  {
    std::lock_guard lock(mutex_);
    ResetDownloadedBodyStorage(response_body_);
    pending_ = true;
    deps = dependencies_;
  }

  const std::string url =
      deps.resolve_url ? deps.resolve_url() : std::string();
  if (!deps.start_download
      || !deps.start_download(url.c_str(),
                              &LatestAgreementsService::DownloadCallback,
                              this,
                              kLatestAgreementsTimeoutMs)) {
    std::lock_guard lock(mutex_);
    pending_ = false;
    ResetDownloadedBodyStorage(response_body_);
    return false;
  }

  return true;
}

void LatestAgreementsService::AbortAndReset() {
  std::lock_guard lock(mutex_);
  pending_ = false;
  ResetDownloadedBodyStorage(response_body_);
}

bool LatestAgreementsService::pending() const {
  std::lock_guard lock(mutex_);
  return pending_;
}

bool LatestAgreementsService::DownloadCallback(
    void* const callback_data,
    const std::uint8_t* const bytes,
    const std::uint32_t byte_count,
    const std::uint32_t event_flag,
    const std::uint32_t completion_code) {
  auto* const service =
      static_cast<LatestAgreementsService*>(callback_data);
  if (service == nullptr) {
    return true;
  }
  return service->OnDownloadEvent(
      bytes, byte_count, event_flag, completion_code);
}

void LatestAgreementsService::SetDependenciesForTests(Dependencies deps) {
  std::lock_guard lock(mutex_);
  if (!deps.start_download) {
    deps.start_download = dependencies_.start_download;
  }
  if (!deps.resolve_url) {
    deps.resolve_url = dependencies_.resolve_url;
  }
  if (!deps.process_archive) {
    deps.process_archive = dependencies_.process_archive;
  }
  dependencies_ = std::move(deps);
  ResetDownloadedBodyStorage(response_body_);
  pending_ = false;
}

void LatestAgreementsService::ResetDependenciesForTests() {
  std::lock_guard lock(mutex_);
  dependencies_ = MakeDefaultDependencies();
  ResetDownloadedBodyStorage(response_body_);
  pending_ = false;
}

bool LatestAgreementsService::OnDownloadEvent(
    const std::uint8_t* const bytes,
    const std::uint32_t byte_count,
    const std::uint32_t event_flag,
    const std::uint32_t completion_code) {
  (void)completion_code;

  std::string completed_body;
  ProcessArchiveFn process_archive;

  {
    std::lock_guard lock(mutex_);
    if (!pending_) {
      return true;
    }

    if (event_flag == 0) {
      if (bytes != nullptr && byte_count != 0) {
        response_body_.append(reinterpret_cast<const char*>(bytes),
                              static_cast<std::size_t>(byte_count));
      }
      return true;
    }

    pending_ = false;
    if (response_body_.empty()) {
      return true;
    }

    completed_body.swap(response_body_);
    process_archive = dependencies_.process_archive;
  }

  if (process_archive) {
    process_archive(completed_body);
  }
  return true;
}

bool CGlueMgr_SyncDataFile(const std::string_view filename,
                           const std::string_view source_bytes,
                           const char* const cvar_name,
                           const bool locale_prefix) {
  if (filename.empty()) {
    return false;
  }

  const auto target_path = BuildDataFilePath(filename, locale_prefix);

  std::error_code ec;
  if (std::filesystem::is_regular_file(target_path, ec)) {
    std::ifstream existing(target_path, std::ios::binary);
    if (existing) {
      std::string current_bytes(
          (std::istreambuf_iterator<char>(existing)),
          std::istreambuf_iterator<char>());
      if (current_bytes.size() == source_bytes.size()
          && current_bytes == source_bytes) {
        return true;
      }
    }
  }

  if (!RemoveExistingRegularFile(target_path)) {
    return false;
  }

  if (!WriteBinaryFileWithoutCreatingParents(target_path, source_bytes)) {
    return false;
  }

  UpdateLegalNoticeCVarForSyncedFile(cvar_name);
  return true;
}

}

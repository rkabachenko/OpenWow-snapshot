#pragma once

#include "openwow/net/os_url_download.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace openwow::ui::glue {

enum class LegalNoticeId : std::uint8_t {
  kTos = 0,
  kEula = 1,
  kTerminationWithoutNotice = 2,
  kScanning = 3,
  kContest = 4,
};

class LegalNoticeState {
 public:
  static LegalNoticeState& Get();

  void InitializeFromCVars();

  [[nodiscard]] bool IsAccepted(LegalNoticeId id) const;
  [[nodiscard]] bool ShouldShow(LegalNoticeId id) const;

  void Accept(LegalNoticeId id);

  void ResetForTests();

 private:
  struct NoticeFlags {
    bool show = false;
    bool accepted = false;
  };

  static std::size_t ToIndex(LegalNoticeId id);
  void EnsureInitializedFromCVars();

  mutable std::mutex mutex_;
  NoticeFlags notices_[5]{};
  bool initialized_ = false;
};

class LatestAgreementsService {
 public:
  using StartDownloadFn =
      std::function<bool(const char*,
                         openwow::net::OsUrlDownloadCallbackFn,
                         void*,
                         int)>;
  using ResolveUrlFn = std::function<std::string()>;
  using ProcessArchiveFn = std::function<void(std::string_view)>;

  struct Dependencies {
    StartDownloadFn start_download;
    ResolveUrlFn resolve_url;
    ProcessArchiveFn process_archive;
  };

  static LatestAgreementsService& Get();

  bool Start();
  void AbortAndReset();

  [[nodiscard]] bool pending() const;

  static bool DownloadCallback(void* callback_data,
                               const std::uint8_t* bytes,
                               std::uint32_t byte_count,
                               std::uint32_t event_flag,
                               std::uint32_t completion_code);

  void SetDependenciesForTests(Dependencies deps);
  void ResetDependenciesForTests();

 private:
  LatestAgreementsService();

  [[nodiscard]] static Dependencies MakeDefaultDependencies();
  bool OnDownloadEvent(const std::uint8_t* bytes,
                       std::uint32_t byte_count,
                       std::uint32_t event_flag,
                       std::uint32_t completion_code);

  mutable std::mutex mutex_;
  Dependencies dependencies_;
  std::string response_body_;
  bool pending_ = false;
};

void InitializeGlueStartupState();

}

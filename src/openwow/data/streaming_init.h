#pragma once

#include "openwow/core/client_crt_random.h"
#include "openwow/core/streaming_storage.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::data {

struct StreamingLegacyFileEntry {
  std::string raw_path;
  std::string absolute_source_path;
  std::string lookup_path;
  std::int64_t size{0};
  std::string file_version;
  std::uint32_t flags{0};
};

struct StreamingManifestTransportSection {
  std::int32_t md5_size{0};
  std::int32_t split_size{0};
};

struct StreamingManifestServerScheduleEntry {
  std::int64_t start_time_ns_since_2000{0};
  std::int32_t usage{100};
};

struct StreamingManifestServer {
  std::string location;
  std::vector<std::string> timeslot_ids;
  std::vector<StreamingManifestServerScheduleEntry> schedule{
      StreamingManifestServerScheduleEntry{}};
  std::int32_t selection_count{0};
  std::int32_t can_deactivate{0};
  std::int32_t max_retry{5};
  bool enabled{true};
};

struct StreamingManifestTimeslot {
  std::int64_t start_time_ns_since_2000{0};
  std::int64_t end_time_ns_since_2000{0};
  std::int32_t usage{0};
};

struct StreamingManifestFileEntry {
  std::string raw_path;
  std::string lookup_path;
  openwow::core::FileManifestEntry entry;
  std::optional<StreamingManifestTransportSection> resolved_transport_section;
};

struct StreamingManifestDirectFileRequest {
  std::string source_url;
  std::int32_t max_retry{5};
  std::optional<StreamingManifestTransportSection> resolved_transport_section;
};

struct StreamingManifestState {
  core::ClientCrtRandom random{};
  std::int32_t version{0};
  bool trial_mode{false};
  bool source_manifest_is_indirect{false};
  bool transport_manifest_is_indirect{false};
  std::string source_manifest;
  std::string transport_manifest;
  std::set<std::string> source_root_uris;
  std::map<std::string, std::string> path_aliases;
  std::map<std::string, std::string> parameters;
  std::map<std::string, StreamingManifestServer> servers;
  std::map<std::string, StreamingManifestTransportSection> transport_sections;
  std::map<std::string, StreamingManifestTimeslot> timeslots;
  std::map<std::string, StreamingManifestFileEntry> file_entries;
  std::vector<std::string> legacy_entries;
  std::vector<StreamingLegacyFileEntry> legacy_file_entries;
};

[[nodiscard]] const char* ManifestArchiveIndexToName(std::uint32_t index);

const char* GetTrackerURLForLocale(const char* locale);

void Streaming_SetLocale(const char* locale);

void Streaming_SetBuildNumber(int build_number);

void Streaming_SetSpeedTest(int speed_test);

void Streaming_ReportStats(bool is_startup, bool has_new_account);

void Streaming_RecordDeferredDownloadBytes(std::uint64_t bytes);
void Streaming_RecordLocalReadBytes(std::uint64_t bytes);
void Streaming_RecordDownloadRetry();

bool Streaming_ConfigureBgPreloadSleep(int speed_test);

using StreamingFrameCallback = void (*)();

int Streaming_RegisterFrameCallback(StreamingFrameCallback callback);

void Streaming_RecordFrameDeltaHistogram();

void Streaming_RunRegisteredFrameCallback();

bool Streaming_LoadManifest(const char* manifest_path);

bool InitializeStreaming(const char* manifest_path,
                         const char* storage_path,
                         const char* path_variant_suffix,
                         const char* data_path_override,
                         bool force_streaming);

void ShutdownStreaming();

bool IsStreamingInitialized();

bool IsOnlineModeActive();

int GetCurrentStreamingStatusCode();
void SetCurrentStreamingStatusCode(int code);

const std::string& GetStreamingStatusMessage();
const char* GetStreamingStatusMessageCString();
void PushStreamingStatusMessage(std::string message, int context, int storm_error);

void ResetCurrentStreamingStatusChain();

bool StreamingStatusContainsContext(int context);

bool StreamingStatusContainsContextAndError(int context, int storm_error);

const StreamingManifestState& GetStreamingManifestState();

void SetManifestParameterIntValue(std::string_view name, int value);

[[nodiscard]] std::int32_t GetManifestServerCurrentScheduleUsage(
    const StreamingManifestServer& server,
    std::int64_t time_ns_since_2000);

[[nodiscard]] std::int64_t NormalizeManifestScheduleQueryTime(
    std::int64_t time_ns_since_2000);

[[nodiscard]] std::string SelectManifestFileServerLocation(
    const openwow::core::FileManifestEntry& entry);

[[nodiscard]] std::string BuildFileManifestDirectFilePath(
    std::string_view lookup_key);

[[nodiscard]] std::optional<StreamingManifestDirectFileRequest>
BuildFileManifestDirectFileRequest(std::string_view lookup_key);

[[nodiscard]] std::vector<std::string> EnumerateFileManifestDirectoryFiles(
    std::string_view path);

int GetFileManifestPrefixStatus(std::string_view lookup_key,
                                std::string_view path);

std::optional<bool> GetFileManifestEntryActiveFlag(std::string_view lookup_key);

std::optional<StreamingManifestTransportSection>
GetFileManifestResolvedTransportSection(std::string_view lookup_key);

void RegisterFileManifestEntry(openwow::core::FileManifestEntry entry,
                               std::int32_t prefix_gate_count,
                               std::int32_t prefix_status_code,
                               bool prefix_active,
                               std::optional<StreamingManifestTransportSection>
                                   resolved_transport_section = std::nullopt);

void ClearFileManifestPrefixActiveOnDeferredReadFailure(
    std::string_view lookup_key,
    std::string_view path);

const char* GetStreamingTrackerURL();

const char* GetStreamingLocaleTag();

int GetStreamingBuildNumber();

int GetStreamingSpeedTest();

void ClearStreamingStatusMessageForInit();

const std::array<double, 24>& GetStreamingFrameDeltaHistogramForTests();

void SetStreamingFrameDeltaHistogramForTests(
    const std::array<double, 24>& histogram);

void SetStreamingReportCountersForTests(std::uint64_t background_downloaded_bytes,
                                        std::uint64_t deferred_downloaded_bytes,
                                        std::uint64_t local_read_bytes,
                                        std::uint32_t retry_count);

std::string PercentEncodeTrackerComponentForTests(std::string_view value);

std::string BuildTrackerUrlForTests(std::string_view info_hash,
                                    int event_phase,
                                    std::uint64_t downloaded_bytes,
                                    const char* stats_override);

void SetStreamingFrameTickSourceForTests(
    std::function<std::uint32_t()> tick_source);

void SetStreamingRandomSeedSourceForTests(
    std::function<unsigned int()> seed_source);

void SetStreamingRandomSeedSinkForTests(
    std::function<void(unsigned int)> seed_sink);

void SetStreamingCurrentTimeSourceForTests(
    std::function<std::int64_t()> current_time_source);

void SetStreamingRandomValueSourceForTests(
    std::function<int()> random_value_source);

void SetStreamingTelemetryDispatchForTests(
    std::function<bool(const std::string&)> dispatch);

void PushStreamingStatusMessageForTests(std::string message,
                                        int context,
                                        int storm_error);

void SetStreamingStatusMessageForTests(std::string message,
                                       int context,
                                       int storm_error);

void ResetStreamingInitStateForTests();

}

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openwow::vfs {

enum class DataPreloadPathReadyState : std::int32_t {
  kError = -1,
  kUnavailable = 0,
  kReady = 1,
  kPartial = 2,
};

struct DataPreloadPathProgress {
  bool resolved{false};
  bool has_partial_progress{false};
  std::int64_t loaded_bytes{0};
  std::int64_t total_bytes{0};
  std::optional<DataPreloadPathReadyState> ready_state_override{};
};

struct DataPreloadNodeProgressState {
  std::vector<std::string> paths;
  std::int32_t current_path_index{-1};
  std::int32_t queued_path_index{0};
  std::int64_t committed_bytes{0};
  bool complete{false};
};

struct DataPreloadControlStateSnapshot {
  int worker_state{0};
  int requested_state{0};
  bool converted_trial{false};
  int background_zone_id{-1};
  int background_zone_param{-1};
  std::int32_t map_id{-1};
  int selected_race{-1};
  std::int32_t map_chunk_focus_key{-1};
  std::int32_t map_tile_focus_key{-1};
  bool dirty{true};
};

using DataPreloadArchiveLookupCallback =
    std::function<bool(const char *, std::string *, std::uint64_t *, std::uint32_t *)>;
using DataPreloadPartialProgressCallback =
    std::function<std::optional<std::uint64_t>(const char *, std::uint64_t, std::uint32_t,
                                                const char *)>;

struct DataPreloadDependencies {
  std::function<bool(const std::string &, std::string *)> read_list_file;
  std::function<bool(const std::string &, std::string *)> read_archive_list_file;
  DataPreloadArchiveLookupCallback lookup_archive_source;
};

class DataPreloadController {
public:
  struct RuntimeState;

  explicit DataPreloadController(DataPreloadDependencies dependencies);
  ~DataPreloadController();

  DataPreloadController(const DataPreloadController &) = delete;
  DataPreloadController &operator=(const DataPreloadController &) = delete;

  DataPreloadPathProgress QueryPathProgress(const char *filename);
  DataPreloadPathReadyState QueryPathReadyState(const char *filename);
  DataPreloadPathReadyState QueryReadOnlyArchivePathReadyState(
      const char *filename, const char *archive_path, std::uint64_t logical_offset,
      std::int32_t compressed_size);
  bool RequestPathAvailability(const char *filename, int queue_index, bool wait_for_completion);
  bool AccumulatePathProgress(const char *filename, std::uint64_t *loaded_bytes,
                              std::uint64_t *total_bytes);

  bool StartWorkerIfNeeded();
  void StopWorkerIfActive();
  void SetRequestedState(int requested_state);
  void SetConvertedTrialFlag(bool converted);
  void SetBackgroundZoneState(int zone_id, int param);
  void SetMapId(std::uint32_t map_id);
  void SetMapChunkFocus(int chunk_y, int chunk_x);
  void SetSelectedRace(int race_id);
  int GetSelectedRace();
  bool IsCurrentRaceReadyForLoading();
  bool IsStartRaceCommonComplete();
  double GetStartRaceProgress(int race_id);
  bool IsStartRaceComplete(int race_id);
  bool IsStartRaceGateOpen();

  void ResetForTests();
  bool IsWorkerRunningForTests();
  std::size_t GetWorkerCreateCountForTests();
  std::size_t GetWorkerWakeCountForTests();
  DataPreloadControlStateSnapshot GetControlStateForTests();
  void SetListFileLoaderForTests(
      std::function<bool(const std::string &, std::string *)> loader);
  void WaitForListLoaderIdleForTests();
  std::vector<std::string> SnapshotNodeKeysForTests();
  void SetArchiveLookupForTests(DataPreloadArchiveLookupCallback lookup);
  void SetPartialProgressProbeForTests(DataPreloadPartialProgressCallback probe);
  void ResetProgressHooksForTests();

private:
  std::unique_ptr<RuntimeState> runtime_;
};

DataPreloadController &RetailDataPreloadController();

void CommitDataPreloadNodeQueuedRange(
    DataPreloadNodeProgressState &node,
    const std::function<DataPreloadPathProgress(const std::string &)> &observe_path);
double ComputeDataPreloadNodeProgressRatio(
    const DataPreloadNodeProgressState &node,
    const std::function<DataPreloadPathProgress(const std::string &)> &observe_path);

DataPreloadPathProgress QueryDataPreloadPathProgress(const char *filename);
DataPreloadPathReadyState QueryDataPreloadPathReadyState(const char *filename);
DataPreloadPathReadyState QueryReadOnlyArchiveFileDataPreloadReadyState(
    const char *filename, const char *archive_path, std::uint64_t logical_offset,
    std::int32_t compressed_size);
bool RequestDataPreloadPathAvailability(const char *filename, int queue_index,
                                        bool wait_for_completion);
bool AccumulateDataPreloadPathProgress(const char *filename, std::uint64_t *loaded_bytes,
                                       std::uint64_t *total_bytes);
bool StartDataPreloadThreadIfNeeded();
void StopDataPreloadRuntimeWorkerIfActive();
void SetDataPreloadRequestedState(int requested_state);
void SetDataPreloadConvertedTrialFlag(bool converted);
void SetDataPreloadBackgroundZoneState(int zone_id, int param);
void SetDataPreloadMapId(std::uint32_t map_id);
void SetDataPreloadMapChunkFocus(int chunk_y, int chunk_x);
void SetDataPreloadSelectedRace(int race_id);
int GetDataPreloadSelectedRace();
bool IsCurrentDataPreloadRaceReadyForLoading();
bool IsStartRaceCommonDataPreloadComplete();
double GetStartRaceDataPreloadProgress(int race_id);
bool IsStartRaceDataPreloadComplete(int race_id);
bool IsStartRaceDataPreloadGateOpen();
void ResetDataPreloadThreadStateForTests();
bool IsDataPreloadThreadRunningForTests();
std::size_t GetDataPreloadThreadCreateCountForTests();
std::size_t GetDataPreloadThreadWakeCountForTests();
DataPreloadControlStateSnapshot GetDataPreloadControlStateForTests();
void SetDataPreloadListFileLoaderForTests(
    std::function<bool(const std::string &, std::string *)> loader);
void WaitForDataPreloadListLoaderIdleForTests();
std::vector<std::string> SnapshotDataPreloadNodeKeysForTests();
void SetDataPreloadArchiveLookupForTests(DataPreloadArchiveLookupCallback lookup);
void SetDataPreloadPartialProgressProbeForTests(DataPreloadPartialProgressCallback probe);
void ResetDataPreloadProgressHooksForTests();

}

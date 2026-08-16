#include "openwow/vfs/retail/streaming/data_preload_controller.h"
#include "openwow/vfs/retail/sfile_archive.h"

#include "openwow/core/init_subsystems.h"
#include "openwow/core/md5.h"
#include "openwow/core/storm_thread.h"
#include "openwow/core/streaming_storage.h"
#include "openwow/data/streaming_init.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace openwow::vfs {
namespace {

struct ThreadHandle {
  std::atomic<std::int32_t> lifecycle_state{0};
  std::atomic<std::size_t> signal_generation{0};
  std::condition_variable_any wake_cv;
  std::mutex wait_mutex;
  openwow::core::JthreadCompat worker;
};

enum class SourceKind { kListFile, kArchiveListFile };

struct SourceSpec {
  SourceKind kind{SourceKind::kListFile};
  std::string key;
  std::string source_name;
};

struct ControlState {
  int worker_state = 0;
  int requested_state = 0;
  bool converted_trial = false;
  int background_zone_id = -1;
  int background_zone_param = -1;
  int map_id = -1;
  int selected_race = -1;
  std::int32_t map_chunk_focus_key = -1;
  std::int32_t map_tile_focus_key = -1;
  bool dirty = true;
};

struct CachedList {
  bool materialized = false;
  DataPreloadNodeProgressState node;
};

struct PathRequestState {
  int requested_queue_index = std::numeric_limits<int>::max();
  std::uint64_t source_id = 0;
};

struct BlockingRequestWaitState {
  std::mutex mutex;
  std::condition_variable completed_cv;
  bool completed = false;
};

constexpr std::array<int, 8> kBackgroundRaceSweep = {1, 4, 5, 6, 2, 7, 3, 8};
constexpr std::array<int, 7> kBackgroundZoneSweep = {10, 44, 267, 331, 400, 406, 11};
constexpr std::array<const char *, 8> kArchiveListFiles = {
    "terrain.MPQ", "wmo.MPQ",   "model.MPQ",     "texture.MPQ",
    "misc.MPQ",    "fonts.MPQ", "interface.MPQ", "dbc.MPQ",
};
constexpr std::size_t kBackgroundDownloadQueueCount = 7;
constexpr std::size_t kWorkerBatchLimit = 20;

std::string NormalizeListKey(const char *path) {
  if (!path) {
    return {};
  }
  std::string normalized(path);
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : static_cast<char>(ch);
  });
  return normalized;
}

std::string NormalizeSourceKey(SourceKind kind, const char *source_name) {
  std::string normalized = NormalizeListKey(source_name);
  if (normalized.empty()) {
    return {};
  }
  return kind == SourceKind::kArchiveListFile ? "archive-listfile:" + normalized : normalized;
}

SourceSpec MakeListSource(const std::string &path) {
  return {SourceKind::kListFile, NormalizeSourceKey(SourceKind::kListFile, path.c_str()), path};
}

SourceSpec MakeArchiveListSource(const std::string &path) {
  return {SourceKind::kArchiveListFile,
          NormalizeSourceKey(SourceKind::kArchiveListFile, path.c_str()), path};
}

std::vector<std::string> ParseListEntries(const std::string &contents) {
  std::vector<std::string> entries;
  const char *cursor = contents.c_str();
  while (*cursor != '\0') {
    const char *delimiter = std::strstr(cursor, "\r\n");
    if (!delimiter) {
      break;
    }
    entries.emplace_back(cursor, static_cast<std::size_t>(delimiter - cursor));
    cursor = delimiter + 2;
  }
  return entries;
}

std::string BuildStartRacePath(int race_id) {
  const std::string race_name = openwow::core::RaceId_ToModelName(race_id);
  return race_name.empty() ? "TrialLists/StartRaceCommon.lst"
                           : "TrialLists/Start" + race_name + ".lst";
}

bool IsThreadActive(const std::shared_ptr<ThreadHandle> &handle) {
  return handle && handle->lifecycle_state.load(std::memory_order_acquire) >= 0;
}

void ApplyPathProgress(const DataPreloadPathProgress &path, std::int64_t &loaded,
                       std::int64_t &total) {
  if (!path.resolved) {
    return;
  }
  loaded += path.has_partial_progress ? path.loaded_bytes : path.total_bytes;
  total += path.total_bytes;
}

DataPreloadPathReadyState ClassifyPathProgress(const DataPreloadPathProgress &progress) {
  if (!progress.resolved) {
    return DataPreloadPathReadyState::kReady;
  }
  if (progress.ready_state_override) {
    return *progress.ready_state_override;
  }
  if (!progress.has_partial_progress || progress.loaded_bytes >= progress.total_bytes) {
    return DataPreloadPathReadyState::kReady;
  }
  return progress.loaded_bytes <= 0 ? DataPreloadPathReadyState::kUnavailable
                                    : DataPreloadPathReadyState::kPartial;
}

int MapRequestedState(int requested_state) {
  switch (requested_state) {
  case 1: return 1;
  case 2: return 3;
  case 3: return 4;
  case 4: return 12;
  default: return 0;
  }
}

int SelectDownloadQueue(int worker_state) {
  return worker_state >= 0 && worker_state <= 4 ? 3 : 6;
}

std::int32_t ArithmeticShiftRightFloor(std::int32_t value, std::int32_t shift) {
  const std::int64_t divisor = std::int64_t{1} << shift;
  return value >= 0 ? static_cast<std::int32_t>(value / divisor)
                    : -static_cast<std::int32_t>((-static_cast<std::int64_t>(value) + divisor - 1) /
                                                 divisor);
}

}

struct DataPreloadController::RuntimeState {
  explicit RuntimeState(DataPreloadDependencies value) : dependencies(std::move(value)) {}

  std::mutex mutex;
  std::condition_variable list_cv;
  ControlState control;
  std::shared_ptr<ThreadHandle> thread_handle;
  openwow::core::JthreadCompat list_loader;
  std::size_t create_count = 0;
  std::size_t wake_count = 0;
  std::size_t active_list_loads = 0;
  std::string active_key;
  std::map<std::string, CachedList, std::less<>> cached_lists;
  std::deque<SourceSpec> pending_list_paths;
  std::unordered_set<std::string> pending_list_keys;
  std::unordered_map<openwow::core::StreamingPathMd5Key, PathRequestState,
                     openwow::core::StreamingPathMd5KeyHash>
      pending_path_requests;
  DataPreloadDependencies dependencies;
  std::function<bool(const std::string &, std::string *)> test_list_loader;
  DataPreloadArchiveLookupCallback test_archive_lookup;
  DataPreloadPartialProgressCallback test_progress_probe;
};

namespace {

bool ReadSource(DataPreloadController::RuntimeState &runtime, const SourceSpec &source,
                std::string *contents) {
  if (runtime.test_list_loader) {
    return runtime.test_list_loader(source.source_name, contents);
  }
  if (source.kind == SourceKind::kArchiveListFile) {
    return runtime.dependencies.read_archive_list_file &&
           runtime.dependencies.read_archive_list_file(source.source_name, contents);
  }
  return runtime.dependencies.read_list_file &&
         runtime.dependencies.read_list_file(source.source_name, contents);
}

std::optional<SourceSpec> BuildSourceSpec(const ControlState &control, int worker_state,
                                          std::size_t iteration) {
  switch (worker_state) {
  case 0: return MakeListSource("TrialLists/StartClient.lst");
  case 1: return MakeListSource("TrialLists/StartCharacter.lst");
  case 2: return MakeListSource("TrialLists/StartRaceCommon.lst");
  case 3: return MakeListSource(BuildStartRacePath(control.selected_race));
  case 4:
    if (control.background_zone_id <= 0) return std::nullopt;
    return MakeListSource("TrialLists/StartBackgroundZone" +
                          std::to_string(control.background_zone_id) + ".lst");
  case 5: {
    const std::string race = openwow::core::RaceId_ToModelName(control.selected_race);
    return race.empty() ? std::nullopt
                        : std::optional<SourceSpec>(MakeListSource("TrialLists/StartBackground" +
                                                                  race + ".lst"));
  }
  case 6: {
    if (iteration >= kBackgroundRaceSweep.size()) return std::nullopt;
    const std::string race = openwow::core::RaceId_ToModelName(kBackgroundRaceSweep[iteration]);
    return race.empty() ? std::nullopt
                        : std::optional<SourceSpec>(MakeListSource("TrialLists/StartBackground" +
                                                                  race + ".lst"));
  }
  case 7:
    if (iteration >= kBackgroundZoneSweep.size()) return std::nullopt;
    return MakeListSource("TrialLists/StartBackgroundZone" +
                          std::to_string(kBackgroundZoneSweep[iteration]) + ".lst");
  case 9:
    return iteration < kArchiveListFiles.size() ? MakeArchiveListSource(kArchiveListFiles[iteration])
                                                : std::optional<SourceSpec>{};
  case 10: {
    const char *name = openwow::data::ManifestArchiveIndexToName(static_cast<std::uint32_t>(iteration));
    return name && *name ? MakeArchiveListSource(name) : std::optional<SourceSpec>{};
  }
  default: return std::nullopt;
  }
}

void AdvanceWorkerState(int &worker_state, std::size_t &iteration, bool converted_trial) {
  switch (worker_state) {
  case 0: worker_state = 1; return;
  case 1:
  case 3:
  case 5: worker_state = 2; return;
  case 2: worker_state = 6; return;
  case 4: worker_state = 5; return;
  case 6:
    if (iteration == kBackgroundRaceSweep.size()) { worker_state = 7; iteration = 0; }
    return;
  case 7:
    if (iteration == kBackgroundZoneSweep.size()) {
      worker_state = converted_trial ? 9 : 11;
      iteration = 0;
    }
    return;
  case 8:
    if (iteration == 2) { worker_state = 9; iteration = 0; }
    return;
  case 9:
    if (iteration == kArchiveListFiles.size()) { worker_state = 11; iteration = 0; }
    return;
  case 10:
    if (iteration == 5) { worker_state = 11; iteration = 0; }
    return;
  default: worker_state = 12; return;
  }
}

void NotifyWorkerLocked(DataPreloadController::RuntimeState &runtime) {
  if (!runtime.thread_handle) return;
  runtime.thread_handle->signal_generation.fetch_add(1, std::memory_order_release);
  runtime.thread_handle->wake_cv.notify_all();
  openwow::core::StreamingStorage::Instance().NotifyBackgroundDownloadStateWaiters();
}

void SignalWorkerLocked(DataPreloadController::RuntimeState &runtime) {
  ++runtime.wake_count;
  NotifyWorkerLocked(runtime);
}

void MaterializeListLocked(DataPreloadController::RuntimeState &runtime, const std::string &key,
                           std::vector<std::string> entries);
void EnsureListLoaderLocked(DataPreloadController::RuntimeState &runtime);

void RunListLoader(DataPreloadController::RuntimeState *runtime,
                   openwow::core::stop_token stop_token) {
  while (!stop_token.stop_requested()) {
    SourceSpec source;
    bool skip = false;
    {
      std::unique_lock lock(runtime->mutex);
      runtime->list_cv.wait(lock, [&] {
        return stop_token.stop_requested() || !runtime->pending_list_paths.empty();
      });
      if (stop_token.stop_requested()) break;
      source = std::move(runtime->pending_list_paths.front());
      runtime->pending_list_paths.pop_front();
      runtime->pending_list_keys.erase(source.key);
      ++runtime->active_list_loads;
      const auto it = runtime->cached_lists.find(source.key);
      skip = it != runtime->cached_lists.end() && it->second.materialized;
      if (skip) --runtime->active_list_loads;
    }
    if (!skip) {
      std::string contents;
      std::vector<std::string> entries;
      if (ReadSource(*runtime, source, &contents)) entries = ParseListEntries(contents);
      {
        std::lock_guard lock(runtime->mutex);
        MaterializeListLocked(*runtime, source.key, std::move(entries));
        if (runtime->active_key == source.key && runtime->thread_handle) {
          runtime->thread_handle->signal_generation.fetch_add(1, std::memory_order_release);
          runtime->thread_handle->wake_cv.notify_all();
        }
      }
      {
        std::lock_guard lock(runtime->mutex);
        --runtime->active_list_loads;
      }
    }
    runtime->list_cv.notify_all();
  }
}

void EnsureListLoaderLocked(DataPreloadController::RuntimeState &runtime) {
  if (runtime.list_loader.joinable()) return;
  runtime.list_loader = openwow::core::JthreadCompat(
      [&runtime](openwow::core::stop_token token) { RunListLoader(&runtime, token); });
}

void QueueListLoadLocked(DataPreloadController::RuntimeState &runtime, const SourceSpec &source) {
  if (source.key.empty() || source.source_name.empty()) return;
  const auto it = runtime.cached_lists.find(source.key);
  if ((it != runtime.cached_lists.end() && it->second.materialized) ||
      !runtime.pending_list_keys.insert(source.key).second) {
    return;
  }
  EnsureListLoaderLocked(runtime);
  runtime.pending_list_paths.push_back(source);
  runtime.list_cv.notify_all();
}

DataPreloadPathProgress ObserveArchivePath(DataPreloadController::RuntimeState &runtime,
                                           const std::string &archive_path,
                                           std::uint64_t block_offset,
                                           std::uint32_t total_bytes,
                                           const char *filename) {
  DataPreloadPathProgress progress{.resolved = true,
                                   .total_bytes = static_cast<std::int64_t>(total_bytes)};
  auto &storage = openwow::core::StreamingStorage::Instance();
  if (!storage.HasManifestEntryForPath(archive_path.c_str())) return progress;
  if (runtime.test_progress_probe) {
    const auto loaded = runtime.test_progress_probe(archive_path.c_str(), block_offset, total_bytes,
                                                     filename);
    if (loaded) {
      progress.has_partial_progress = true;
      progress.loaded_bytes = static_cast<std::int64_t>(std::min<std::uint64_t>(*loaded, total_bytes));
    }
    return progress;
  }
  const auto cached = storage.LookupPreloadPartialProgress(filename);
  if (cached && *cached >= total_bytes) {
    progress.has_partial_progress = true;
    progress.loaded_bytes = total_bytes;
    progress.ready_state_override = DataPreloadPathReadyState::kReady;
    return progress;
  }
  if (const auto availability =
          storage.QuerySourceRangeAvailability(archive_path.c_str(), block_offset, total_bytes)) {
    progress.has_partial_progress = true;
    progress.loaded_bytes = static_cast<std::int64_t>(
        std::min<std::uint64_t>(availability->resident_bytes, total_bytes));
    progress.ready_state_override = availability->saw_missing
                                        ? DataPreloadPathReadyState::kUnavailable
                                        : availability->saw_partial
                                              ? DataPreloadPathReadyState::kPartial
                                              : DataPreloadPathReadyState::kReady;
  } else if (cached) {
    progress.has_partial_progress = true;
    progress.loaded_bytes =
        static_cast<std::int64_t>(std::min<std::uint64_t>(*cached, total_bytes));
  }
  return progress;
}

DataPreloadPathProgress ObservePath(DataPreloadController::RuntimeState &runtime,
                                    const char *filename) {
  std::string archive_path;
  std::uint64_t block_offset = 0;
  std::uint32_t total_bytes = 0;
  auto &lookup = runtime.test_archive_lookup ? runtime.test_archive_lookup
                                             : runtime.dependencies.lookup_archive_source;
  if (!lookup || !lookup(filename, &archive_path, &block_offset, &total_bytes)) return {};
  return ObserveArchivePath(runtime, archive_path, block_offset, total_bytes, filename);
}

bool RefreshListProgress(DataPreloadController::RuntimeState &runtime, CachedList &list) {
  if (!list.materialized || list.node.complete) return false;
  auto observe = [&runtime](const std::string &path) { return ObservePath(runtime, path.c_str()); };
  const auto count = static_cast<std::int32_t>(list.node.paths.size());
  bool changed = false;
  while (!list.node.complete) {
    if (list.node.queued_path_index >= count) {
      CommitDataPreloadNodeQueuedRange(list.node, observe);
      return true;
    }
    const std::string &path = list.node.paths[static_cast<std::size_t>(list.node.queued_path_index)];
    const DataPreloadPathProgress progress = ObservePath(runtime, path.c_str());
    if (progress.resolved && progress.has_partial_progress &&
        progress.loaded_bytes < progress.total_bytes) {
      break;
    }
    ++list.node.queued_path_index;
    CommitDataPreloadNodeQueuedRange(list.node, observe);
    changed = true;
  }
  return changed;
}

void MaterializeListLocked(DataPreloadController::RuntimeState &runtime, const std::string &key,
                           std::vector<std::string> entries) {
  auto [it, inserted] = runtime.cached_lists.try_emplace(key);
  if (inserted || !it->second.materialized) {
    it->second.materialized = true;
    it->second.node.paths = std::move(entries);
    it->second.node.current_path_index = -1;
    it->second.node.queued_path_index = 0;
    it->second.node.committed_bytes = 0;
    it->second.node.complete = false;
    RefreshListProgress(runtime, it->second);
  }
}

CachedList *FindCachedListLocked(DataPreloadController::RuntimeState &runtime,
                                 const std::string &path) {
  const auto it = runtime.cached_lists.find(NormalizeSourceKey(SourceKind::kListFile, path.c_str()));
  return it == runtime.cached_lists.end() ? nullptr : &it->second;
}

bool QueryCachedProgressLocked(DataPreloadController::RuntimeState &runtime,
                               const std::string &path, double *progress) {
  CachedList *list = FindCachedListLocked(runtime, path);
  if (!progress || !list || !list->materialized) return false;
  const bool changed = RefreshListProgress(runtime, *list);
  if (changed && NormalizeSourceKey(SourceKind::kListFile, path.c_str()) == runtime.active_key &&
      runtime.thread_handle) {
    runtime.thread_handle->signal_generation.fetch_add(1, std::memory_order_release);
    runtime.thread_handle->wake_cv.notify_all();
  }
  *progress = ComputeDataPreloadNodeProgressRatio(
      list->node, [&runtime](const std::string &candidate) {
        return ObservePath(runtime, candidate.c_str());
      });
  return true;
}

void SelectNextNodeLocked(DataPreloadController::RuntimeState &runtime, bool advance) {
  std::size_t iteration = 0;
  while (runtime.control.worker_state != 11 && runtime.control.worker_state != 12) {
    if (advance) {
      AdvanceWorkerState(runtime.control.worker_state, iteration, runtime.control.converted_trial);
      if (runtime.control.worker_state == 11 || runtime.control.worker_state == 12) break;
    }
    const int worker_state = runtime.control.worker_state;
    const auto source = BuildSourceSpec(runtime.control, worker_state, iteration);
    if (worker_state >= 6) ++iteration;
    if (!source) { advance = true; continue; }
    auto [it, inserted] = runtime.cached_lists.try_emplace(source->key);
    if (!it->second.materialized) QueueListLoadLocked(runtime, *source);
    else { (void)inserted; RefreshListProgress(runtime, it->second); }
    if (it->second.node.complete) { advance = true; continue; }
    runtime.active_key = source->key;
    return;
  }
  runtime.active_key.clear();
}

void EnsureListMaterialized(DataPreloadController::RuntimeState &runtime, const std::string &path) {
  const SourceSpec source = MakeListSource(path);
  if (source.key.empty()) return;
  {
    std::lock_guard lock(runtime.mutex);
    const auto it = runtime.cached_lists.find(source.key);
    if (it != runtime.cached_lists.end() && it->second.materialized) return;
    runtime.pending_list_keys.erase(source.key);
    std::erase_if(runtime.pending_list_paths,
                  [&](const SourceSpec &candidate) { return candidate.key == source.key; });
  }
  std::string contents;
  std::vector<std::string> entries;
  if (ReadSource(runtime, source, &contents)) entries = ParseListEntries(contents);
  {
    std::lock_guard lock(runtime.mutex);
    MaterializeListLocked(runtime, source.key, std::move(entries));
  }
  runtime.list_cv.notify_all();
}

bool IsCachedResolvableLocked(DataPreloadController::RuntimeState &runtime,
                              const std::string &path) {
  CachedList *list = FindCachedListLocked(runtime, path);
  if (!list || !list->materialized) return false;
  const bool changed = RefreshListProgress(runtime, *list);
  if (changed && NormalizeSourceKey(SourceKind::kListFile, path.c_str()) == runtime.active_key)
    NotifyWorkerLocked(runtime);
  return list->node.complete;
}

enum class WaitMode { kWakeSignal, kBackgroundDownloads };
struct StepResult {
  bool should_exit = false;
  WaitMode wait_mode = WaitMode::kWakeSignal;
  int queue = -1;
};

int QueueActiveBatch(DataPreloadController *controller,
                     DataPreloadController::RuntimeState &runtime) {
  int queued = 0;
  while (queued < static_cast<int>(kWorkerBatchLimit)) {
    std::string path;
    int queue = 0;
    {
      std::lock_guard lock(runtime.mutex);
      if (runtime.control.dirty || runtime.active_key.empty()) break;
      const auto it = runtime.cached_lists.find(runtime.active_key);
      if (it == runtime.cached_lists.end() || !it->second.materialized) break;
      auto &node = it->second.node;
      if (node.queued_path_index >= static_cast<std::int32_t>(node.paths.size())) break;
      path = node.paths[static_cast<std::size_t>(node.queued_path_index++)];
      queue = SelectDownloadQueue(runtime.control.worker_state);
    }
    if (path.empty()) break;
    if (controller->QueryPathReadyState(path.c_str()) != DataPreloadPathReadyState::kReady) {
      (void)controller->RequestPathAvailability(path.c_str(), queue, false);
      ++queued;
    }
  }
  return queued;
}

StepResult StepWorker(DataPreloadController *controller,
                      DataPreloadController::RuntimeState &runtime) {
  while (true) {
    {
      std::lock_guard lock(runtime.mutex);
      if (runtime.control.dirty) {
        runtime.control.dirty = false;
        runtime.control.worker_state = MapRequestedState(runtime.control.requested_state);
        runtime.active_key.clear();
        if (runtime.control.worker_state == 12) return {.should_exit = true};
        SelectNextNodeLocked(runtime, false);
      } else if (runtime.active_key.empty()) {
        SelectNextNodeLocked(runtime, false);
      } else {
        const auto it = runtime.cached_lists.find(runtime.active_key);
        if (it == runtime.cached_lists.end()) { runtime.active_key.clear(); continue; }
        if (!it->second.materialized) return {};
        RefreshListProgress(runtime, it->second);
        if (it->second.node.complete) { SelectNextNodeLocked(runtime, true); continue; }
      }
    }
    {
      std::lock_guard lock(runtime.mutex);
      if (runtime.active_key.empty()) return {};
    }
    const int queued = QueueActiveBatch(controller, runtime);
    if (queued > 0) {
      std::lock_guard lock(runtime.mutex);
      if (runtime.control.dirty) continue;
      return {.wait_mode = WaitMode::kBackgroundDownloads,
              .queue = SelectDownloadQueue(runtime.control.worker_state)};
    }
    {
      std::lock_guard lock(runtime.mutex);
      if (runtime.control.dirty) continue;
      const auto it = runtime.cached_lists.find(runtime.active_key);
      if (it == runtime.cached_lists.end() || !it->second.materialized) {
        runtime.active_key.clear();
        continue;
      }
      CommitDataPreloadNodeQueuedRange(
          it->second.node, [&runtime](const std::string &path) {
            return ObservePath(runtime, path.c_str());
          });
      if (it->second.node.complete) { SelectNextNodeLocked(runtime, true); continue; }
    }
  }
}

void RunWorker(DataPreloadController *controller, DataPreloadController::RuntimeState *runtime,
               const std::shared_ptr<ThreadHandle> &handle,
               openwow::core::stop_token stop_token) {
  handle->lifecycle_state.store(1, std::memory_order_release);
  std::size_t generation = 0;
  while (!stop_token.stop_requested()) {
    const StepResult step = StepWorker(controller, *runtime);
    if (step.should_exit) break;
    generation = handle->signal_generation.load(std::memory_order_acquire);
    if (step.wait_mode == WaitMode::kBackgroundDownloads) {
      const auto result = openwow::core::StreamingStorage::Instance()
                              .WaitForBackgroundDownloadQueueBacklogUntil(
                                  static_cast<std::size_t>(step.queue), [&] {
                                    return stop_token.stop_requested() ||
                                           handle->signal_generation.load(
                                               std::memory_order_acquire) != generation;
                                  });
      if (result != openwow::core::StreamingStorage::BackgroundDownloadQueueBarrierResult::kCompleted ||
          stop_token.stop_requested()) continue;
      generation = handle->signal_generation.load(std::memory_order_acquire);
      continue;
    }
    std::unique_lock lock(handle->wait_mutex);
    handle->wake_cv.wait_for(lock, std::chrono::milliseconds(100), [&] {
      return stop_token.stop_requested() ||
             handle->signal_generation.load(std::memory_order_acquire) != generation;
    });
    generation = handle->signal_generation.load(std::memory_order_acquire);
  }
  handle->lifecycle_state.store(-1, std::memory_order_release);
}

void StopHandle(const std::shared_ptr<ThreadHandle> &handle) {
  if (!handle) return;
  handle->worker.request_stop();
  handle->wake_cv.notify_all();
  openwow::core::StreamingStorage::Instance().NotifyBackgroundDownloadStateWaiters();
  if (handle->worker.joinable()) handle->worker.join();
}

}

DataPreloadController::DataPreloadController(DataPreloadDependencies dependencies)
    : runtime_(std::make_unique<RuntimeState>(std::move(dependencies))) {}

DataPreloadController::~DataPreloadController() {
  StopWorkerIfActive();
}

DataPreloadPathProgress DataPreloadController::QueryPathProgress(const char *filename) {
  return ObservePath(*runtime_, filename);
}

DataPreloadPathReadyState DataPreloadController::QueryPathReadyState(const char *filename) {
  return ClassifyPathProgress(QueryPathProgress(filename));
}

DataPreloadPathReadyState DataPreloadController::QueryReadOnlyArchivePathReadyState(
    const char *filename, const char *archive_path, std::uint64_t logical_offset,
    std::int32_t compressed_size) {
  if (!archive_path || !*archive_path) return DataPreloadPathReadyState::kReady;
  if (compressed_size <= 0) return DataPreloadPathReadyState::kError;
  const DataPreloadPathProgress progress = ObserveArchivePath(
      *runtime_, archive_path, logical_offset, static_cast<std::uint32_t>(compressed_size),
      filename ? filename : "");
  return ClassifyPathProgress(progress);
}

bool DataPreloadController::RequestPathAvailability(const char *filename, int queue_index,
                                                    bool wait_for_completion) {
  std::string archive_path;
  std::uint64_t offset = 0;
  std::uint32_t total = 0;
  auto &lookup = runtime_->test_archive_lookup ? runtime_->test_archive_lookup
                                               : runtime_->dependencies.lookup_archive_source;
  if (!lookup || !lookup(filename, &archive_path, &offset, &total)) return false;
  const DataPreloadPathProgress progress =
      ObserveArchivePath(*runtime_, archive_path, offset, total, filename);
  if (!progress.resolved || progress.total_bytes <= 0) return false;
  const auto key = openwow::core::StreamingStorage::MakeLowercasePathMd5Key(filename);
  auto &storage = openwow::core::StreamingStorage::Instance();
  if (ClassifyPathProgress(progress) == DataPreloadPathReadyState::kReady) {
    std::lock_guard lock(runtime_->mutex);
    const auto it = runtime_->pending_path_requests.find(key);
    if (it != runtime_->pending_path_requests.end()) {
      storage.CloseBackgroundDownloadSource(it->second.source_id);
      runtime_->pending_path_requests.erase(it);
    }
    return true;
  }
  if (queue_index < 0 || static_cast<std::size_t>(queue_index) >= kBackgroundDownloadQueueCount ||
      (queue_index > 3 && !openwow::data::IsOnlineModeActive())) return false;

  const std::string path = filename ? filename : "";
  auto queue_request = [&](std::optional<openwow::core::StreamingPathMd5Key> pending_key,
                           const std::shared_ptr<BlockingRequestWaitState> &wait_state) {
    storage.InitializeBackgroundDownloadWorkers();
    const std::uint64_t source_id = storage.CreateBackgroundDownloadSource();
    storage.SetBackgroundDownloadSourceCompletionCallback(
        source_id, [this, path, total, source_id, pending_key, wait_state] {
          auto &storage_ref = openwow::core::StreamingStorage::Instance();
          storage_ref.RecordPreloadPartialProgress(path.c_str(), total);
          storage_ref.CloseBackgroundDownloadSource(source_id);
          if (pending_key) {
            std::lock_guard lock(runtime_->mutex);
            const auto it = runtime_->pending_path_requests.find(*pending_key);
            if (it != runtime_->pending_path_requests.end() && it->second.source_id == source_id)
              runtime_->pending_path_requests.erase(it);
          }
          if (wait_state) {
            {
              std::lock_guard lock(wait_state->mutex);
              wait_state->completed = true;
            }
            wait_state->completed_cv.notify_all();
          }
        });
    if (!storage.QueueBackgroundDownloadTask(static_cast<std::size_t>(queue_index), source_id)) {
      storage.CloseBackgroundDownloadSource(source_id);
      return std::optional<std::uint64_t>{};
    }
    (void)storage.MarkLogicalRangePendingCompressed(archive_path.c_str(), offset, total);
    return std::optional<std::uint64_t>{source_id};
  };

  if (wait_for_completion) {
    auto wait = std::make_shared<BlockingRequestWaitState>();
    if (!queue_request(std::nullopt, wait)) return false;
    std::unique_lock lock(wait->mutex);
    wait->completed_cv.wait(lock, [&] { return wait->completed; });
    return QueryPathReadyState(filename) == DataPreloadPathReadyState::kReady;
  }

  std::lock_guard lock(runtime_->mutex);
  const auto existing = runtime_->pending_path_requests.find(key);
  if (existing != runtime_->pending_path_requests.end() &&
      existing->second.requested_queue_index <= queue_index) return true;
  if (existing != runtime_->pending_path_requests.end()) {
    storage.CloseBackgroundDownloadSource(existing->second.source_id);
    runtime_->pending_path_requests.erase(existing);
  }
  const auto source_id = queue_request(key, nullptr);
  if (!source_id) return false;
  runtime_->pending_path_requests[key] = {queue_index, *source_id};
  return true;
}

bool DataPreloadController::AccumulatePathProgress(const char *filename,
                                                  std::uint64_t *loaded_bytes,
                                                  std::uint64_t *total_bytes) {
  if (!loaded_bytes || !total_bytes) return false;
  const DataPreloadPathProgress progress = QueryPathProgress(filename);
  if (!progress.resolved) return false;
  std::int64_t loaded = static_cast<std::int64_t>(*loaded_bytes);
  std::int64_t total = static_cast<std::int64_t>(*total_bytes);
  ApplyPathProgress(progress, loaded, total);
  *loaded_bytes = static_cast<std::uint64_t>(loaded);
  *total_bytes = static_cast<std::uint64_t>(total);
  return true;
}

bool DataPreloadController::StartWorkerIfNeeded() {
  if (!openwow::data::IsOnlineModeActive()) return false;
  std::lock_guard lock(runtime_->mutex);
  if (IsThreadActive(runtime_->thread_handle)) return true;
  auto handle = std::make_shared<ThreadHandle>();
  try {
    handle->worker = openwow::core::JthreadCompat(
        [this, handle](openwow::core::stop_token token) {
          RunWorker(this, runtime_.get(), handle, token);
        });
  } catch (const std::system_error &) {
    handle->lifecycle_state.store(-1, std::memory_order_release);
    return false;
  }
  runtime_->thread_handle = std::move(handle);
  ++runtime_->create_count;
  return true;
}

void DataPreloadController::StopWorkerIfActive() {
  std::shared_ptr<ThreadHandle> handle;
  openwow::core::JthreadCompat loader;
  {
    std::lock_guard lock(runtime_->mutex);
    if (runtime_->control.worker_state != 12) {
      runtime_->control.worker_state = 12;
      runtime_->control.requested_state = 4;
      runtime_->control.dirty = true;
    }
    if (runtime_->thread_handle) SignalWorkerLocked(*runtime_);
    handle = std::move(runtime_->thread_handle);
    loader = std::move(runtime_->list_loader);
  }
  if (loader.joinable()) {
    loader.request_stop();
    runtime_->list_cv.notify_all();
    loader.join();
  }
  if (handle && handle->worker.joinable()) handle->worker.join();
}

void DataPreloadController::SetRequestedState(int value) {
  if (!openwow::data::IsOnlineModeActive()) return;
  std::lock_guard lock(runtime_->mutex);
  if (runtime_->control.requested_state == value) return;
  runtime_->control.requested_state = value;
  runtime_->control.dirty = true;
  SignalWorkerLocked(*runtime_);
}

void DataPreloadController::SetConvertedTrialFlag(bool value) {
  if (!openwow::data::IsOnlineModeActive()) return;
  std::lock_guard lock(runtime_->mutex);
  if (runtime_->control.converted_trial == value) return;
  runtime_->control.converted_trial = value;
  SignalWorkerLocked(*runtime_);
}

void DataPreloadController::SetBackgroundZoneState(int zone_id, int param) {
  std::lock_guard lock(runtime_->mutex);
  if (runtime_->control.background_zone_id != zone_id) {
    runtime_->control.background_zone_id = zone_id;
    runtime_->control.dirty = true;
    SignalWorkerLocked(*runtime_);
  }
  runtime_->control.background_zone_param = param;
}

void DataPreloadController::SetMapId(std::uint32_t map_id) {
  std::lock_guard lock(runtime_->mutex);
  const int value = static_cast<int>(map_id);
  if (runtime_->control.map_id == value) return;
  runtime_->control.map_id = value;
  runtime_->control.dirty = true;
  SignalWorkerLocked(*runtime_);
}

void DataPreloadController::SetMapChunkFocus(int chunk_y, int chunk_x) {
  std::lock_guard lock(runtime_->mutex);
  const std::int32_t chunk_key = chunk_y + chunk_x * 1024;
  const std::int32_t tile_key = ArithmeticShiftRightFloor(chunk_y + 15, 4) +
      static_cast<std::int32_t>(static_cast<std::uint32_t>(chunk_x * 4 + 60) & 0xFFFFFFC0u);
  if (runtime_->control.map_tile_focus_key != tile_key) {
    runtime_->control.map_tile_focus_key = tile_key;
    runtime_->control.dirty = true;
    SignalWorkerLocked(*runtime_);
  }
  runtime_->control.map_chunk_focus_key = chunk_key;
}

void DataPreloadController::SetSelectedRace(int race_id) {
  std::lock_guard lock(runtime_->mutex);
  if (runtime_->control.selected_race == race_id) return;
  runtime_->control.selected_race = race_id;
  runtime_->control.dirty = true;
  SignalWorkerLocked(*runtime_);
}

int DataPreloadController::GetSelectedRace() {
  std::lock_guard lock(runtime_->mutex);
  return runtime_->control.selected_race;
}

double DataPreloadController::GetStartRaceProgress(int race_id) {
  const std::string common = "TrialLists/StartRaceCommon.lst";
  const std::string race = BuildStartRacePath(race_id);
  std::lock_guard lock(runtime_->mutex);
  double common_progress = 0.0;
  const double common_half = QueryCachedProgressLocked(*runtime_, common, &common_progress)
                                 ? common_progress * 0.5
                                 : 0.0;
  if (race == common) return common_half + common_half;
  double race_progress = 0.0;
  return QueryCachedProgressLocked(*runtime_, race, &race_progress)
             ? common_half + race_progress * 0.5
             : common_half;
}

bool DataPreloadController::IsCurrentRaceReadyForLoading() {
  if (!openwow::data::IsOnlineModeActive()) return true;
  const int race = GetSelectedRace();
  if (race <= 0) return false;
  const std::string race_path = BuildStartRacePath(race);
  EnsureListMaterialized(*runtime_, "TrialLists/StartRaceCommon.lst");
  EnsureListMaterialized(*runtime_, race_path);
  std::lock_guard lock(runtime_->mutex);
  return IsCachedResolvableLocked(*runtime_, "TrialLists/StartRaceCommon.lst") &&
         IsCachedResolvableLocked(*runtime_, race_path);
}

bool DataPreloadController::IsStartRaceCommonComplete() {
  EnsureListMaterialized(*runtime_, "TrialLists/StartRaceCommon.lst");
  std::lock_guard lock(runtime_->mutex);
  return IsCachedResolvableLocked(*runtime_, "TrialLists/StartRaceCommon.lst");
}

bool DataPreloadController::IsStartRaceComplete(int race_id) {
  if (!IsStartRaceCommonComplete()) return false;
  const std::string race = BuildStartRacePath(race_id);
  EnsureListMaterialized(*runtime_, race);
  std::lock_guard lock(runtime_->mutex);
  return IsCachedResolvableLocked(*runtime_, race);
}

bool DataPreloadController::IsStartRaceGateOpen() {
  return !openwow::data::IsOnlineModeActive() || IsCurrentRaceReadyForLoading();
}

void DataPreloadController::ResetForTests() {
  std::shared_ptr<ThreadHandle> handle;
  openwow::core::JthreadCompat loader;
  std::vector<std::uint64_t> source_ids;
  {
    std::lock_guard lock(runtime_->mutex);
    handle = std::move(runtime_->thread_handle);
    loader = std::move(runtime_->list_loader);
    runtime_->control = {};
    runtime_->create_count = 0;
    runtime_->wake_count = 0;
    runtime_->active_list_loads = 0;
    runtime_->active_key.clear();
    runtime_->cached_lists.clear();
    runtime_->pending_list_paths.clear();
    runtime_->pending_list_keys.clear();
    for (const auto &[_, request] : runtime_->pending_path_requests)
      source_ids.push_back(request.source_id);
    runtime_->pending_path_requests.clear();
    runtime_->test_list_loader = {};
  }
  auto &storage = openwow::core::StreamingStorage::Instance();
  for (std::uint64_t source_id : source_ids) storage.CloseBackgroundDownloadSource(source_id);
  StopHandle(handle);
  if (loader.joinable()) {
    loader.request_stop();
    runtime_->list_cv.notify_all();
    loader.join();
  }
}

bool DataPreloadController::IsWorkerRunningForTests() {
  std::lock_guard lock(runtime_->mutex);
  return IsThreadActive(runtime_->thread_handle);
}

std::size_t DataPreloadController::GetWorkerCreateCountForTests() {
  std::lock_guard lock(runtime_->mutex);
  return runtime_->create_count;
}

std::size_t DataPreloadController::GetWorkerWakeCountForTests() {
  std::lock_guard lock(runtime_->mutex);
  return runtime_->wake_count;
}

DataPreloadControlStateSnapshot DataPreloadController::GetControlStateForTests() {
  std::lock_guard lock(runtime_->mutex);
  return {runtime_->control.worker_state, runtime_->control.requested_state,
          runtime_->control.converted_trial, runtime_->control.background_zone_id,
          runtime_->control.background_zone_param, runtime_->control.map_id,
          runtime_->control.selected_race, runtime_->control.map_chunk_focus_key,
          runtime_->control.map_tile_focus_key, runtime_->control.dirty};
}

void DataPreloadController::SetListFileLoaderForTests(
    std::function<bool(const std::string &, std::string *)> loader) {
  std::lock_guard lock(runtime_->mutex);
  runtime_->test_list_loader = std::move(loader);
}

void DataPreloadController::WaitForListLoaderIdleForTests() {
  std::unique_lock lock(runtime_->mutex);
  runtime_->list_cv.wait(lock, [&] {
    if (!runtime_->pending_list_paths.empty() || runtime_->active_list_loads != 0) return false;
    if (!IsThreadActive(runtime_->thread_handle) || runtime_->active_key.empty()) return true;
    const auto it = runtime_->cached_lists.find(runtime_->active_key);
    return it != runtime_->cached_lists.end() && it->second.materialized &&
           !it->second.node.complete;
  });
}

std::vector<std::string> DataPreloadController::SnapshotNodeKeysForTests() {
  std::lock_guard lock(runtime_->mutex);
  std::vector<std::string> keys;
  keys.reserve(runtime_->cached_lists.size());
  for (const auto &[key, _] : runtime_->cached_lists) keys.push_back(key);
  std::sort(keys.begin(), keys.end());
  return keys;
}

void DataPreloadController::SetArchiveLookupForTests(DataPreloadArchiveLookupCallback lookup) {
  std::lock_guard lock(runtime_->mutex);
  runtime_->test_archive_lookup = std::move(lookup);
}

void DataPreloadController::SetPartialProgressProbeForTests(
    DataPreloadPartialProgressCallback probe) {
  std::lock_guard lock(runtime_->mutex);
  runtime_->test_progress_probe = std::move(probe);
}

void DataPreloadController::ResetProgressHooksForTests() {
  std::lock_guard lock(runtime_->mutex);
  runtime_->test_archive_lookup = {};
  runtime_->test_progress_probe = {};
}

void CommitDataPreloadNodeQueuedRange(
    DataPreloadNodeProgressState &node,
    const std::function<DataPreloadPathProgress(const std::string &)> &observe_path) {
  const std::int32_t previous = node.current_path_index;
  node.current_path_index = node.queued_path_index;
  if (node.queued_path_index >= 0 &&
      static_cast<std::size_t>(node.queued_path_index) == node.paths.size()) {
    node.paths.clear();
    node.complete = true;
    return;
  }
  for (std::int32_t index = previous + 1; index < node.queued_path_index; ++index) {
    const DataPreloadPathProgress progress =
        observe_path ? observe_path(node.paths[static_cast<std::size_t>(index)])
                     : DataPreloadPathProgress{};
    if (progress.resolved)
      node.committed_bytes += progress.has_partial_progress ? progress.loaded_bytes
                                                            : progress.total_bytes;
  }
}

double ComputeDataPreloadNodeProgressRatio(
    const DataPreloadNodeProgressState &node,
    const std::function<DataPreloadPathProgress(const std::string &)> &observe_path) {
  if (node.complete) return 1.0;
  std::int64_t loaded = node.committed_bytes;
  std::int64_t total = node.committed_bytes;
  std::size_t index = node.current_path_index >= 0
                          ? static_cast<std::size_t>(node.current_path_index) + 1
                          : 0;
  while (index < node.paths.size()) {
    ApplyPathProgress(observe_path ? observe_path(node.paths[index]) : DataPreloadPathProgress{},
                      loaded, total);
    ++index;
  }
  return static_cast<double>(loaded) / static_cast<double>(total);
}

DataPreloadController &RetailDataPreloadController() {
  static DataPreloadController controller({
      .read_list_file = [](const std::string &path, std::string *contents) {
        if (!contents) return false;
        std::vector<std::uint8_t> bytes;
        if (!ReadRetailVfsFileBytes(path.c_str(), &bytes)) return false;
        contents->assign(bytes.begin(), bytes.end());
        return true;
      },
      .read_archive_list_file = [](const std::string &archive, std::string *contents) {
        return ReadRetailArchiveListFile(archive.c_str(), contents);
      },
      .lookup_archive_source = [](const char *filename, std::string *archive_path,
                                  std::uint64_t *offset, std::uint32_t *bytes) {
        return QueryWrappedArchiveFileMetadata(filename, archive_path, offset, bytes, nullptr);
      },
  });
  return controller;
}

DataPreloadPathProgress QueryDataPreloadPathProgress(const char *path) {
  return RetailDataPreloadController().QueryPathProgress(path);
}
DataPreloadPathReadyState QueryDataPreloadPathReadyState(const char *path) {
  return RetailDataPreloadController().QueryPathReadyState(path);
}
DataPreloadPathReadyState QueryReadOnlyArchiveFileDataPreloadReadyState(
    const char *path, const char *archive, std::uint64_t offset, std::int32_t compressed_size) {
  return RetailDataPreloadController().QueryReadOnlyArchivePathReadyState(
      path, archive, offset, compressed_size);
}
bool RequestDataPreloadPathAvailability(const char *path, int queue, bool wait) {
  return RetailDataPreloadController().RequestPathAvailability(path, queue, wait);
}
bool AccumulateDataPreloadPathProgress(const char *path, std::uint64_t *loaded,
                                       std::uint64_t *total) {
  return RetailDataPreloadController().AccumulatePathProgress(path, loaded, total);
}
bool StartDataPreloadThreadIfNeeded() { return RetailDataPreloadController().StartWorkerIfNeeded(); }
void StopDataPreloadRuntimeWorkerIfActive() { RetailDataPreloadController().StopWorkerIfActive(); }
void SetDataPreloadRequestedState(int value) { RetailDataPreloadController().SetRequestedState(value); }
void SetDataPreloadConvertedTrialFlag(bool value) {
  RetailDataPreloadController().SetConvertedTrialFlag(value);
}
void SetDataPreloadBackgroundZoneState(int zone, int param) {
  RetailDataPreloadController().SetBackgroundZoneState(zone, param);
}
void SetDataPreloadMapId(std::uint32_t map) { RetailDataPreloadController().SetMapId(map); }
void SetDataPreloadMapChunkFocus(int y, int x) {
  RetailDataPreloadController().SetMapChunkFocus(y, x);
}
void SetDataPreloadSelectedRace(int race) { RetailDataPreloadController().SetSelectedRace(race); }
int GetDataPreloadSelectedRace() { return RetailDataPreloadController().GetSelectedRace(); }
bool IsCurrentDataPreloadRaceReadyForLoading() {
  return RetailDataPreloadController().IsCurrentRaceReadyForLoading();
}
bool IsStartRaceCommonDataPreloadComplete() {
  return RetailDataPreloadController().IsStartRaceCommonComplete();
}
double GetStartRaceDataPreloadProgress(int race) {
  return RetailDataPreloadController().GetStartRaceProgress(race);
}
bool IsStartRaceDataPreloadComplete(int race) {
  return RetailDataPreloadController().IsStartRaceComplete(race);
}
bool IsStartRaceDataPreloadGateOpen() { return RetailDataPreloadController().IsStartRaceGateOpen(); }
void ResetDataPreloadThreadStateForTests() { RetailDataPreloadController().ResetForTests(); }
bool IsDataPreloadThreadRunningForTests() {
  return RetailDataPreloadController().IsWorkerRunningForTests();
}
std::size_t GetDataPreloadThreadCreateCountForTests() {
  return RetailDataPreloadController().GetWorkerCreateCountForTests();
}
std::size_t GetDataPreloadThreadWakeCountForTests() {
  return RetailDataPreloadController().GetWorkerWakeCountForTests();
}
DataPreloadControlStateSnapshot GetDataPreloadControlStateForTests() {
  return RetailDataPreloadController().GetControlStateForTests();
}
void SetDataPreloadListFileLoaderForTests(
    std::function<bool(const std::string &, std::string *)> loader) {
  RetailDataPreloadController().SetListFileLoaderForTests(std::move(loader));
}
void WaitForDataPreloadListLoaderIdleForTests() {
  RetailDataPreloadController().WaitForListLoaderIdleForTests();
}
std::vector<std::string> SnapshotDataPreloadNodeKeysForTests() {
  return RetailDataPreloadController().SnapshotNodeKeysForTests();
}
void SetDataPreloadArchiveLookupForTests(DataPreloadArchiveLookupCallback lookup) {
  RetailDataPreloadController().SetArchiveLookupForTests(std::move(lookup));
}
void SetDataPreloadPartialProgressProbeForTests(DataPreloadPartialProgressCallback probe) {
  RetailDataPreloadController().SetPartialProgressProbeForTests(std::move(probe));
}
void ResetDataPreloadProgressHooksForTests() {
  RetailDataPreloadController().ResetProgressHooksForTests();
}

}

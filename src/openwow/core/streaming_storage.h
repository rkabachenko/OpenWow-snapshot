
#pragma once

#include "openwow/runtime/scheduling/jthread_compat.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace openwow::core {

struct StreamingEntry {
  std::string filename;
  std::string hash;
  uint8_t flags = 0;
  uint64_t fileSize = 0;
  uint64_t storageBytesUsed = 0;
  uint32_t blockSize = 0;
  uint32_t blockCount = 0;

  bool bypassPartValidation = false;

  uint64_t legacyManifestRecordOffset = 0;

  struct BlockMeta {
    uint32_t key = 0;
    uint32_t offset_lo = 0;
    uint32_t offset_hi = 0;
    uint32_t size_lo = 0;
    uint32_t size_hi = 0;
    uint32_t dup_size_lo = 0;
    uint32_t dup_size_hi = 0;
    uint32_t field14 = 0;
  };
  static_assert(sizeof(BlockMeta) == 32, "Streaming manifest block records must stay 32 bytes");
  std::vector<BlockMeta> blocks;
};

struct StreamingStorageConfig {
  std::string location;
  std::vector<std::string> timeSlotIds;
  int32_t maxRetry = 5;
  int32_t canDeactivate = 0;
};

enum class StreamingStatus : uint8_t {
  Idle = 0,
  Streaming = 1,
  Paused = 2,
  Error = 3,
  Complete = 4
};

struct StreamingState {
  StreamingStatus status = StreamingStatus::Idle;
  uint32_t totalFiles = 0;
  uint32_t loadedFiles = 0;
  uint64_t totalBytes = 0;
  uint64_t loadedBytes = 0;
};

struct StreamingChecksummedRange {
  uint64_t storageBegin = 0;
  uint64_t storageEnd = 0;
  uint32_t dataOffset = 0;
};

struct StreamingTransferRateSample {
  uint64_t totalBytes = 0;
  uint32_t tickMs = 0;
};

using StreamingPathMd5Key = std::array<std::uint8_t, 16>;

struct StreamingPathMd5KeyHash {
  std::size_t operator()(const StreamingPathMd5Key &key) const noexcept;
};

struct StreamingSourceRangeAvailability {
  std::uint64_t resident_bytes = 0;
  bool saw_partial = false;
  bool saw_missing = false;
};

class StreamingTransferRateTracker {
public:
  static constexpr std::size_t kSampleCount = 51;

  void Reset(uint32_t start_tick_ms = 0);
  void RecordSample(uint64_t total_bytes, uint32_t tick_ms);
  [[nodiscard]] uint32_t EstimateBytesPerSecond() const;

private:
  uint32_t startTickMs_ = 0;
  uint32_t currentIndex_ = 0;
  std::array<StreamingTransferRateSample, kSampleCount> samples_{};
};

enum class StreamingPartBlockState : std::uint32_t {
  Missing = 0,
  Reserved = 1,
  PendingWrite = 2,
  Available = 3,
  Busy = 4,
};

struct StreamingPartBlockRecord {
  StreamingPartBlockState state = StreamingPartBlockState::Missing;
  std::uint64_t partFileOffset = 0;
  std::uint64_t auxiliaryValue = 0;
  std::uint32_t blockSize = 0;
};

class StreamingPartEntryRuntime {
public:
  void SetFlags(std::uint32_t flags);
  void SetBlockSize(std::uint32_t block_size);
  void SetBlocks(std::vector<StreamingPartBlockRecord> blocks);
  void SetBlock(std::size_t index, StreamingPartBlockRecord block);
  void SetRetired(bool retired);

  [[nodiscard]] std::uint32_t GetFlags() const {
    return flags_;
  }
  [[nodiscard]] std::uint32_t GetBlockSize() const {
    return blockSize_;
  }
  [[nodiscard]] const std::vector<StreamingPartBlockRecord> &GetBlocks() const {
    return blocks_;
  }
  [[nodiscard]] bool IsRetired() const {
    return retired_;
  }

private:
  friend class StreamingStorage;

  std::uint32_t flags_ = 0;
  std::uint32_t blockSize_ = 0;
  bool retired_ = false;
  std::vector<StreamingPartBlockRecord> blocks_{};
};

struct StreamingPartPendingWrite {
  const StreamingPartEntryRuntime *owner = nullptr;
  std::uint64_t logicalBlockOffset = 0;
  std::vector<std::uint8_t> bytes;
};

struct StreamingPartFileState;

enum class StreamingPartWriteMetadataMode : std::uint8_t {
  PartFileBlockTable = 0,
  LegacyManifest = 1,
};

struct StreamingPartWriteTask {
  StreamingPartWriteMetadataMode metadataMode = StreamingPartWriteMetadataMode::PartFileBlockTable;
  int partFileHandle = 0;
  int metadataFileHandle = 0;
  StreamingPartFileState *partFileState = nullptr;
  StreamingEntry *manifestEntry = nullptr;
  std::uint64_t manifestEntryOffset = 0;
  std::uint32_t manifestEntryCount = 0;
  StreamingPartPendingWrite pendingWrite{};
};

struct QueuedPendingPartWrite {
  const StreamingPartEntryRuntime *owner = nullptr;
  std::uint64_t logicalBlockOffset = 0;
  std::shared_ptr<std::vector<std::uint8_t>> bytes{};
};

struct QueuedStreamingPartWriteTask {
  StreamingPartWriteMetadataMode metadataMode = StreamingPartWriteMetadataMode::PartFileBlockTable;
  int partFileHandle = 0;
  int metadataFileHandle = 0;
  StreamingPartFileState *partFileState = nullptr;
  StreamingEntry *manifestEntry = nullptr;
  std::uint64_t manifestEntryOffset = 0;
  std::uint32_t manifestEntryCount = 0;
  QueuedPendingPartWrite pendingWrite{};
};

struct FileManifestEntry;

struct StreamingPartFileState {
  std::string headerPath;
  std::uint32_t persistedFlags = 0;
  std::uint64_t logicalFileSize = 0;
  std::uint64_t storageEnd = 0;
  std::uint32_t blockSize = 0;
  bool storageNeedsRewrite = false;
  bool storageHeaderInitialized = false;
  bool storageLayoutValidated = false;
  StreamingPartEntryRuntime entry;
};

class StreamingStorage {
public:
  using SharedSFileTask = void (*)(void *);
  enum class BackgroundDownloadTaskDispatchResult : std::uint8_t {
    kComplete = 0,
    kRetryFront,
  };
  enum class BackgroundDownloadQueueBarrierResult : std::uint8_t {
    kNoQueuedTasks = 0,
    kCompleted,
    kInterrupted,
  };
  using BackgroundDownloadTaskCallback = std::function<BackgroundDownloadTaskDispatchResult()>;

  class SharedSFileTaskWorker {
  public:
    SharedSFileTaskWorker();
    ~SharedSFileTaskWorker();

    SharedSFileTaskWorker(const SharedSFileTaskWorker &) = delete;
    SharedSFileTaskWorker &operator=(const SharedSFileTaskWorker &) = delete;

    bool Dispatch(SharedSFileTask task, void *context);
    void WaitForCompletion();

  private:
    void Shutdown();
    void ThreadProc();

    std::mutex mutex_;
    std::condition_variable requestCv_;
    std::condition_variable completionCv_;
    std::thread thread_{};
    SharedSFileTask task_ = nullptr;
    void *taskContext_ = nullptr;
    bool stopRequested_ = false;
    bool taskCompleted_ = true;
  };

  class SharedSFileTaskWorkerLease {
  public:
    SharedSFileTaskWorkerLease() = default;
    SharedSFileTaskWorkerLease(const SharedSFileTaskWorkerLease &) = delete;
    SharedSFileTaskWorkerLease &operator=(const SharedSFileTaskWorkerLease &) = delete;
    SharedSFileTaskWorkerLease(SharedSFileTaskWorkerLease &&other) noexcept;
    SharedSFileTaskWorkerLease &operator=(SharedSFileTaskWorkerLease &&other) noexcept;
    ~SharedSFileTaskWorkerLease();

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    SharedSFileTaskWorker &operator[](std::size_t index);
    const SharedSFileTaskWorker &operator[](std::size_t index) const;
    void Reset();

  private:
    SharedSFileTaskWorkerLease(StreamingStorage *owner, std::uint64_t generation,
                               std::vector<std::unique_ptr<SharedSFileTaskWorker>> workers);

    StreamingStorage *owner_ = nullptr;
    std::uint64_t generation_ = 0;
    std::vector<std::unique_ptr<SharedSFileTaskWorker>> workers_{};

    friend class StreamingStorage;
  };

  static StreamingStorage &Instance();

  bool ParseAttribute(StreamingStorageConfig &config, const char *attrName, const char *attrValue);

  bool ParseManifestBinary(const void *data, size_t dataSize);

  bool WriteLegacyManifestEntry(int fileHandle, const StreamingEntry &entry,
                                std::uint64_t entryOffset, std::uint32_t entryCount,
                                std::optional<std::uint32_t> blockIndex, bool writeFullRecord,
                                std::uint32_t runtimeFlags = 0) const;

  const std::vector<StreamingEntry> &GetEntries() const;

  [[nodiscard]] bool HasManifestEntryForPath(const char *source_path) const;

  bool MarkLogicalRangePendingCompressed(const char *source_path, std::uint64_t logical_offset,
                                         std::uint32_t size);

  std::mutex &GetArchiveListMutex();

  std::mutex &GetStreamingStateMutex();

  StreamingState GetState() const;

  void SetState(const StreamingState &state);

  int GetBgPreloadSleep() const;

  void SetBgPreloadSleep(int ms);

  void SetBgPreloadSleepFromManifestParameter(int ms);

  void ApplyBgPreloadSleepOverride(bool restore_saved_value);

  SharedSFileTaskWorkerLease AcquireSharedSFileTaskWorkers(std::size_t worker_count);

  bool ResolveAbsolutePathFromBase(const char *source, char *resolved_path,
                                   int resolved_path_capacity) const;

  [[nodiscard]] bool QueuePendingPartWriteTask(StreamingPartWriteTask task);

  [[nodiscard]] bool TrySetPartEntryBlockStateRange(StreamingPartEntryRuntime &entry,
                                                    std::uint64_t logical_offset,
                                                    std::uint32_t size,
                                                    StreamingPartBlockState state);

  void SetPartEntryBlockStateRange(StreamingPartEntryRuntime &entry, std::uint64_t logical_offset,
                                   std::uint32_t size, StreamingPartBlockState state);

  bool InitSimple();

  bool Init(const std::string &manifestPath, bool strictMode);

  int BuildVariantPath(char *destination, int capacity, const char *base_path,
                       const char *extension) const;

  void SetVariantPathSuffix(const char *suffix);

  [[nodiscard]] bool IsSimpleMode() const;

  void IncrementVariantPathRetryOrdinal();

  void SetVariantPathStateForTests(const char *suffix, std::uint32_t retry_ordinal);

  int ThreadProc();

  void SignalWorker();

  void RequestStop();

  void Shutdown();

  void InitializeBackgroundDownloadWorkers();

  [[nodiscard]] int FindIdleBackgroundDownloadWorkerSlot() const;

  bool IsReady() const;

  void SetStreamingStateValue(uint32_t value);

  static bool SetManifestEntryAttribute(FileManifestEntry &entry, const char *attrName,
                                        const char *attrValue);

  void DrainPendingPartWriteTasksForHandle(int partFileHandle);

  bool MarkStreamingEntryBypassPartValidationIfAllBlocksReady(StreamingEntry &entry);

  void ResetStreamingEntryNonReadyBlocks(StreamingEntry &entry);

  static std::string BuildManifestEntryRelativeFilePath(const FileManifestEntry &entry);

  static StreamingChecksummedRange
  MapLogicalRangeToChecksummedStorageRange(uint64_t logicalBegin, uint64_t logicalEndInclusive,
                                           uint32_t blockSize, uint64_t logicalFileSize);

  static StreamingPathMd5Key MakeLowercasePathMd5Key(const char *path);

  static bool DecodeMd5HexDigest32(std::string_view text, std::uint8_t out_digest[16]);

  static bool VerifyDownloadChecksumBlocks(std::string_view checksummed_storage,
                                           std::uint32_t block_size, std::string &out_data);

  [[nodiscard]] std::optional<std::uint64_t> LookupPreloadPartialProgress(const char *path) const;

  [[nodiscard]] std::optional<StreamingSourceRangeAvailability>
  QuerySourceRangeAvailability(const char *source_path, std::uint64_t logical_offset,
                               std::uint32_t size) const;

  void RecordPreloadPartialProgress(const char *path, std::uint64_t loaded_bytes);

  void ResetPreloadPartialProgressCache();

  bool ReadPartEntryLogicalSpan(
      const StreamingPartEntryRuntime &entry, void *destination, std::uint64_t logical_offset,
      std::uint32_t size,
      const std::function<bool(std::uint64_t part_file_offset)> &part_file_reader) const;

  static bool GetExeDirectory(char *outBuf, uint32_t bufSize);

  bool SetBasePath(const char *path);

  static StreamingEntry *LookupStreamingEntry(void *callback_table, const std::string *lookup_key,
                                              const char *source_path,
                                              std::uint64_t logical_file_size, const char *hash,
                                              std::uint32_t block_size);

  static bool ReinitPartFile(int fileHandle, void *entryData);

  static bool OpenPartFile(int fileHandle, void *entryData);

  static bool IsTrialMode();

  static bool ConfigureBgPreloadSleep(int bandwidth);

  [[nodiscard]] std::uint64_t CreateBackgroundDownloadSource();

  void SetBackgroundDownloadSourceDispatchCallback(std::uint64_t source_id,
                                                   BackgroundDownloadTaskCallback on_dispatch);
  void SetBackgroundDownloadSourceCompletionCallback(std::uint64_t source_id,
                                                     std::function<void()> on_complete);
  void CloseBackgroundDownloadSource(std::uint64_t source_id);
  bool QueueBackgroundDownloadTask(std::size_t queue_index, std::uint64_t source_id);

  BackgroundDownloadQueueBarrierResult
  WaitForBackgroundDownloadQueueBacklogUntil(std::size_t queue_index,
                                             const std::function<bool()> &interrupted);
  void WaitForBackgroundDownloadTasksIdle();
  bool WaitForBackgroundDownloadTasksIdleUntil(const std::function<bool()> &interrupted);
  void NotifyBackgroundDownloadStateWaiters();

  [[nodiscard]] std::uint32_t GetBackgroundDownloadWorkerAliveMaskForTests() const;
  [[nodiscard]] std::uint32_t GetBackgroundDownloadWorkerBusyMaskForTests() const;
  void SetBackgroundDownloadWorkerBusyForTests(std::size_t slot_index, bool busy);
  void
  SetBackgroundDownloadSourceDispatchCallbackForTests(std::uint64_t source_id,
                                                      BackgroundDownloadTaskCallback on_dispatch);
  [[nodiscard]] std::uint64_t CreateBackgroundDownloadSourceForTests();
  void CloseBackgroundDownloadSourceForTests(std::uint64_t source_id);
  bool RequeueBackgroundDownloadTaskToFrontForTests(std::size_t queue_index,
                                                    std::uint64_t source_id);
  bool QueueBackgroundDownloadTaskForTests(std::size_t queue_index, std::uint64_t source_id);
  bool DequeueBackgroundDownloadTaskForTests(std::size_t queue_index, std::uint64_t &source_id);
  std::size_t SelectNextBackgroundDownloadQueueForTests(std::size_t worker_slot,
                                                        std::uint64_t &source_id);
  bool WaitForBackgroundDownloadTasksIdleForTests(std::uint32_t timeout_ms = 200);
  [[nodiscard]] std::size_t GetBackgroundDownloadDequeueCountForTests() const;
  [[nodiscard]] std::size_t GetBackgroundDownloadScheduleMissCountForTests() const;
  [[nodiscard]] std::size_t GetBackgroundDownloadCompletedTaskCountForTests() const;
  [[nodiscard]] std::size_t GetBackgroundDownloadSkippedTaskCountForTests() const;
  void SetStorageReadyForTests(bool ready);
  void SetPreloadPartialProgressForTests(const char *path, std::uint64_t loaded_bytes);
  bool EraseRuntimeEntryBySourcePathForTests(const char *source_path);
  void QueuePendingPartWriteForTests(StreamingPartPendingWrite pending_write);
  void SetPendingPartWritesForTests(std::vector<StreamingPartPendingWrite> pending_writes);
  [[nodiscard]] std::size_t GetPendingPartWriteCountForTests() const;
  void QueuePendingPartWriteTaskForTests(StreamingPartWriteTask task);
  [[nodiscard]] std::size_t DrainPendingPartWriteTasksForTests();
  [[nodiscard]] std::size_t GetPendingPartWriteTaskCountForTests();
  [[nodiscard]] std::uint32_t GetStreamingStateValueForTests() const;
  [[nodiscard]] std::string GetBasePathForTests() const;
  [[nodiscard]] std::size_t GetIdleSharedSFileTaskWorkerCountForTests() const;

  void ResetForTests();

private:
  friend class StreamingPartEntryRuntime;

  StreamingStorage() = default;

  static constexpr std::size_t kBackgroundDownloadWorkerCount = 2;
  static constexpr std::size_t kBackgroundDownloadQueueCount = 7;
  static constexpr std::size_t kBackgroundDownloadPriorityGroupCount = 2;
  static constexpr std::size_t kBackgroundDownloadPriorityOrderLength = 6;
  static constexpr std::size_t kBackgroundDownloadDirectQueueIndex = 3;
  static constexpr std::size_t kBackgroundDownloadNoQueue = 7;
  enum class BackgroundDownloadActiveTaskKind : std::uint8_t {
    kIdle = 0,
    kDispatch,
    kQueueBarrier,
  };

  struct BackgroundDownloadWorkerSlot {
    std::mutex wait_mutex;
    std::condition_variable_any wait_cv;
    openwow::core::JthreadCompat thread;
    BackgroundDownloadActiveTaskKind active_task_kind =
        BackgroundDownloadActiveTaskKind::kIdle;
  };

  struct BackgroundDownloadSourceState {
    std::mutex mutex;
    bool closed = false;
    BackgroundDownloadTaskCallback on_dispatch;
  };

  struct BackgroundDownloadQueueBarrierState {
    std::mutex mutex;
    std::condition_variable completed_cv;
    bool completed = false;
  };

  struct BackgroundDownloadTaskRecord {
    bool is_queue_barrier = false;
    std::uint64_t source_id = 0;
    std::weak_ptr<BackgroundDownloadSourceState> source;
    std::shared_ptr<BackgroundDownloadQueueBarrierState> queue_barrier;
  };

  void ShutdownBackgroundDownloadWorkers();
  void ClearParsedManifestEntries();
  void ResetBackgroundDownloadStateLocked();
  void BackgroundDownloadWorkerThread(std::size_t slot_index, openwow::core::stop_token stop_token);
  [[nodiscard]] bool HasPendingBackgroundDownloadTasksLocked() const;
  bool TryDequeueBackgroundDownloadTaskLocked(std::size_t queue_index,
                                              BackgroundDownloadTaskRecord &task);
  bool TryDequeueBackgroundDownloadTaskByPriorityGroupLocked(std::size_t priority_group,
                                                             BackgroundDownloadTaskRecord &task,
                                                             std::size_t &queue_index);
  std::size_t SelectNextBackgroundDownloadQueueLocked(std::size_t worker_slot,
                                                      BackgroundDownloadTaskRecord &task);
  [[nodiscard]] std::optional<std::size_t> FindWakeableBackgroundDownloadWorkerLocked() const;
  [[nodiscard]] bool HasActiveBackgroundDownloadDispatchesLocked() const;
  void RequeueBackgroundDownloadTaskToFrontLocked(std::size_t queue_index,
                                                  const BackgroundDownloadTaskRecord &task);
  static std::shared_ptr<BackgroundDownloadSourceState> TryAcquireBackgroundDownloadSourceLease(
      const std::weak_ptr<BackgroundDownloadSourceState> &source);
  void SignalBackgroundDownloadThrottleEvent();
  void WaitForBackgroundDownloadThrottleEvent(openwow::core::stop_token stop_token,
                                              std::chrono::milliseconds timeout);
  void ReleaseSharedSFileTaskWorkers(std::uint64_t generation,
                                     std::vector<std::unique_ptr<SharedSFileTaskWorker>> workers);
  void ShutdownSharedSFileTaskWorkers();
  [[nodiscard]] std::uint64_t
  ComputeNextLegacyManifestRecordOffsetLocked(std::string_view excluded_lookup_key = {}) const;
  [[nodiscard]] bool FlushPendingPartWriteTask(const QueuedStreamingPartWriteTask &task);
  [[nodiscard]] bool WritePartFileBlockRecord(int fileHandle, std::uint32_t blockIndex,
                                              const StreamingPartBlockRecord &block) const;
  [[nodiscard]] bool TryPopPendingPartWriteTask(QueuedStreamingPartWriteTask &task);
  bool EraseRuntimeEntryByLookupKeyLocked(std::string_view lookup_key);
  bool EraseRuntimeEntryBySourcePathLocked(std::string_view source_path);

  mutable std::mutex archiveListMutex_;
  mutable std::mutex streamingStateMutex_;
  std::vector<StreamingEntry> entries_;
  std::unordered_map<std::string, std::size_t> entryIndicesByLookupPath_{};
  std::unordered_map<std::string, std::unique_ptr<StreamingEntry>> runtimeEntriesByLookupKey_{};
  StreamingState state_;
  std::uint32_t streamingStateValue_ = 0;
  int bgPreloadSleep_ = 100;

  int bgPreloadSleepRestoreValue_ = 0;
  bool initialized_ = false;
  bool storageReady_ = false;
  bool simpleMode_ = false;
  std::atomic<bool> stopRequested_{false};
  std::atomic<bool> workerStopped_{false};
  openwow::core::JthreadCompat workerThread_{};
  std::mutex workerMutex_;
  std::condition_variable workerCV_;
  std::deque<QueuedStreamingPartWriteTask> pendingPartWriteTasks_{};
  mutable std::mutex backgroundDownloadMutex_;
  std::condition_variable backgroundDownloadTaskIdleCv_;
  mutable std::mutex backgroundDownloadThrottleMutex_;
  std::condition_variable backgroundDownloadThrottleCv_;
  std::array<BackgroundDownloadWorkerSlot, kBackgroundDownloadWorkerCount>
      backgroundDownloadWorkers_{};
  std::array<std::deque<BackgroundDownloadTaskRecord>, kBackgroundDownloadQueueCount>
      backgroundDownloadQueues_{};
  std::unordered_map<std::uint64_t, std::shared_ptr<BackgroundDownloadSourceState>>
      backgroundDownloadSources_{};
  std::array<std::size_t, kBackgroundDownloadPriorityGroupCount>
      backgroundDownloadPriorityRotation_{};
  std::uint64_t nextBackgroundDownloadSourceId_ = 1;
  std::size_t backgroundDownloadDequeueCount_ = 0;
  std::size_t backgroundDownloadScheduleMissCount_ = 0;
  std::size_t backgroundDownloadCompletedTaskCount_ = 0;
  std::size_t backgroundDownloadSkippedTaskCount_ = 0;
  std::atomic<std::uint32_t> backgroundDownloadWorkerAliveMask_{0};
  std::atomic<std::uint32_t> backgroundDownloadWorkerBusyMask_{0};
  bool backgroundDownloadThrottleSignaled_ = false;
  bool backgroundDownloadWorkersInitialized_ = false;
  mutable std::mutex preloadPartialProgressMutex_;
  mutable std::mutex streamingPartMutex_;
  std::unordered_map<StreamingPathMd5Key, std::uint64_t, StreamingPathMd5KeyHash>
      preloadPartialProgressByKey_{};
  std::deque<QueuedPendingPartWrite> pendingPartWrites_{};
  mutable std::mutex sharedSFileTaskWorkerMutex_;
  std::vector<std::unique_ptr<SharedSFileTaskWorker>> sharedSFileTaskWorkers_{};
  std::uint64_t sharedSFileTaskWorkerGeneration_ = 1;
  bool sharedSFileTaskWorkerAcceptingReturns_ = true;
  mutable std::mutex variantPathMutex_;
  std::string variantPathSuffix_;
  std::uint32_t variantPathRetryOrdinal_ = 0;
  std::string basePath_;
  std::string manifestPath_;
  int storageManifestFileHandle_ = 0;
};

struct FileManifestEntry {
  std::string name;
  int64_t size = 0;
  std::string fileVersion;
  uint32_t flags = 0;
  std::string path;
  std::string transportItem;
  std::vector<std::string> validServers;
};

}

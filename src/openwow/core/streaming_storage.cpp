
#include "streaming_storage.h"

#include "openwow/core/md5.h"
#include "openwow/core/storm_path.h"
#include "openwow/core/storm_string.h"
#include "openwow/core/storm_utils.h"
#include "openwow/data/streaming_init.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/sfile_core.h"
#include "openwow/vfs/retail/io_unit/io_unit_compat.h"
#include "openwow/vfs/retail/streaming/streaming_read_plan.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <limits>
#include <new>
#include <thread>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace openwow::core {

StreamingStorage::SharedSFileTaskWorker::SharedSFileTaskWorker()
    : thread_([this]() { ThreadProc(); }) {}

StreamingStorage::SharedSFileTaskWorker::~SharedSFileTaskWorker() {
  Shutdown();
}

bool StreamingStorage::SharedSFileTaskWorker::Dispatch(const SharedSFileTask task,
                                                       void *const context) {
  if (task == nullptr) {
    return false;
  }

  {
    std::lock_guard lock(mutex_);
    if (stopRequested_ || task_ != nullptr || !taskCompleted_) {
      return false;
    }

    task_ = task;
    taskContext_ = context;
    taskCompleted_ = false;
  }

  requestCv_.notify_one();
  return true;
}

void StreamingStorage::SharedSFileTaskWorker::WaitForCompletion() {
  std::unique_lock lock(mutex_);
  completionCv_.wait(lock, [this]() { return taskCompleted_ && task_ == nullptr; });
}

void StreamingStorage::SharedSFileTaskWorker::Shutdown() {
  {
    std::lock_guard lock(mutex_);
    if (stopRequested_) {
      return;
    }

    stopRequested_ = true;
    task_ = nullptr;
    taskContext_ = nullptr;
    taskCompleted_ = true;
  }

  requestCv_.notify_one();
  completionCv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void StreamingStorage::SharedSFileTaskWorker::ThreadProc() {
  std::unique_lock lock(mutex_);
  requestCv_.wait(lock, [this]() { return task_ != nullptr || stopRequested_; });
  while (task_ != nullptr) {
    const SharedSFileTask task = task_;
    void *const task_context = taskContext_;
    lock.unlock();
    task(task_context);
    lock.lock();

    task_ = nullptr;
    taskContext_ = nullptr;
    taskCompleted_ = true;
    completionCv_.notify_all();
    requestCv_.wait(lock, [this]() { return task_ != nullptr || stopRequested_; });
  }
}

StreamingStorage::SharedSFileTaskWorkerLease::SharedSFileTaskWorkerLease(
    StreamingStorage *const owner, const std::uint64_t generation,
    std::vector<std::unique_ptr<SharedSFileTaskWorker>> workers)
    : owner_(owner), generation_(generation), workers_(std::move(workers)) {}

StreamingStorage::SharedSFileTaskWorkerLease::SharedSFileTaskWorkerLease(
    SharedSFileTaskWorkerLease &&other) noexcept
    : owner_(other.owner_), generation_(other.generation_), workers_(std::move(other.workers_)) {
  other.owner_ = nullptr;
  other.generation_ = 0;
}

StreamingStorage::SharedSFileTaskWorkerLease &
StreamingStorage::SharedSFileTaskWorkerLease::operator=(
    SharedSFileTaskWorkerLease &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  Reset();
  owner_ = other.owner_;
  generation_ = other.generation_;
  workers_ = std::move(other.workers_);
  other.owner_ = nullptr;
  other.generation_ = 0;
  return *this;
}

StreamingStorage::SharedSFileTaskWorkerLease::~SharedSFileTaskWorkerLease() {
  Reset();
}

bool StreamingStorage::SharedSFileTaskWorkerLease::empty() const noexcept {
  return workers_.empty();
}

std::size_t StreamingStorage::SharedSFileTaskWorkerLease::size() const noexcept {
  return workers_.size();
}

StreamingStorage::SharedSFileTaskWorker &
StreamingStorage::SharedSFileTaskWorkerLease::operator[](const std::size_t index) {
  return *workers_[index];
}

const StreamingStorage::SharedSFileTaskWorker &
StreamingStorage::SharedSFileTaskWorkerLease::operator[](const std::size_t index) const {
  return *workers_[index];
}

void StreamingStorage::SharedSFileTaskWorkerLease::Reset() {
  if (owner_ == nullptr) {
    workers_.clear();
    generation_ = 0;
    return;
  }

  owner_->ReleaseSharedSFileTaskWorkers(generation_, std::move(workers_));
  owner_ = nullptr;
  generation_ = 0;
}

namespace {

constexpr std::uint64_t kStreamingChecksumTrailerBytes = 32;
constexpr std::uint32_t kLegacyStreamingManifestVersion = 2;
constexpr std::uint64_t kLegacyStreamingManifestFileHeaderSize = 8;
constexpr std::uint64_t kLegacyStreamingManifestEntryHeaderSize = 321;
constexpr std::uint64_t kLegacyStreamingManifestSerializedBlockSize = 24;
constexpr std::size_t kLegacyStreamingManifestPathFieldSize = 260;
constexpr std::size_t kLegacyStreamingManifestHashFieldSize = 37;
constexpr std::uint64_t kLegacyStreamingManifestStorageBytesOffset = 305;
constexpr std::uint32_t kLegacyStreamingManifestFlagSkipLocalWrite = 4;
constexpr std::uint32_t kStreamingPartFileHeaderVersion = 2;
constexpr std::uint64_t kStreamingPartFileHeaderSize = 52;
constexpr std::uint64_t kStreamingPartFileSerializedBlockSize = 20;

struct StreamingPartFileHeader {
  std::uint32_t version = 0;
  std::array<char, 32> path{};
  std::uint32_t persistedFlags = 0;
  std::uint64_t logicalFileSize = 0;
  std::uint32_t blockSize = 0;
};

struct StreamingPartFileSerializedBlock {
  std::uint32_t availabilityTag = 0;
  std::uint64_t partFileOffset = 0;
  std::uint64_t auxiliaryValue = 0;
};

[[nodiscard]] constexpr std::uint32_t LoadLittleEndian32(
    const std::uint8_t *const bytes) noexcept {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8u) |
         (static_cast<std::uint32_t>(bytes[2]) << 16u) |
         (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

[[nodiscard]] constexpr std::uint64_t LoadLittleEndian64(
    const std::uint8_t *const bytes) noexcept {
  return static_cast<std::uint64_t>(LoadLittleEndian32(bytes)) |
         (static_cast<std::uint64_t>(LoadLittleEndian32(bytes + 4u)) << 32u);
}

constexpr void StoreLittleEndian32(std::uint8_t *const bytes,
                                   const std::uint32_t value) noexcept {
  bytes[0] = static_cast<std::uint8_t>(value);
  bytes[1] = static_cast<std::uint8_t>(value >> 8u);
  bytes[2] = static_cast<std::uint8_t>(value >> 16u);
  bytes[3] = static_cast<std::uint8_t>(value >> 24u);
}

constexpr void StoreLittleEndian64(std::uint8_t *const bytes,
                                   const std::uint64_t value) noexcept {
  StoreLittleEndian32(bytes, static_cast<std::uint32_t>(value));
  StoreLittleEndian32(bytes + 4u, static_cast<std::uint32_t>(value >> 32u));
}

[[nodiscard]] std::array<std::uint8_t, kStreamingPartFileHeaderSize>
SerializeStreamingPartFileHeader(const StreamingPartFileHeader &header) {
  std::array<std::uint8_t, kStreamingPartFileHeaderSize> bytes{};
  StoreLittleEndian32(bytes.data(), header.version);
  std::memcpy(bytes.data() + 4u, header.path.data(), header.path.size());
  StoreLittleEndian32(bytes.data() + 36u, header.persistedFlags);
  StoreLittleEndian64(bytes.data() + 40u, header.logicalFileSize);
  StoreLittleEndian32(bytes.data() + 48u, header.blockSize);
  return bytes;
}

[[nodiscard]] StreamingPartFileHeader DeserializeStreamingPartFileHeader(
    const std::array<std::uint8_t, kStreamingPartFileHeaderSize> &bytes) {
  StreamingPartFileHeader header;
  header.version = LoadLittleEndian32(bytes.data());
  std::memcpy(header.path.data(), bytes.data() + 4u, header.path.size());
  header.persistedFlags = LoadLittleEndian32(bytes.data() + 36u);
  header.logicalFileSize = LoadLittleEndian64(bytes.data() + 40u);
  header.blockSize = LoadLittleEndian32(bytes.data() + 48u);
  return header;
}

[[nodiscard]] std::array<std::uint8_t, kStreamingPartFileSerializedBlockSize>
SerializeStreamingPartFileBlock(const StreamingPartFileSerializedBlock &block) {
  std::array<std::uint8_t, kStreamingPartFileSerializedBlockSize> bytes{};
  StoreLittleEndian32(bytes.data(), block.availabilityTag);
  StoreLittleEndian64(bytes.data() + 4u, block.partFileOffset);
  StoreLittleEndian64(bytes.data() + 12u, block.auxiliaryValue);
  return bytes;
}

[[nodiscard]] StreamingPartFileSerializedBlock DeserializeStreamingPartFileBlock(
    const std::uint8_t *const bytes) noexcept {
  return {
      .availabilityTag = LoadLittleEndian32(bytes),
      .partFileOffset = LoadLittleEndian64(bytes + 4u),
      .auxiliaryValue = LoadLittleEndian64(bytes + 12u),
  };
}

template <typename T>
[[nodiscard]] bool TryResizeVector(std::vector<T> *const values,
                                   const std::size_t size) noexcept {
  if (size > values->max_size()) {
    values->clear();
    return false;
  }

  try {
    values->resize(size);
    return true;
  } catch (const std::bad_alloc &) {
    values->clear();
    return false;
  }
}

const QueuedPendingPartWrite *
FindPendingPartWrite(const std::deque<QueuedPendingPartWrite> &pending_writes,
                     const StreamingPartEntryRuntime &entry,
                     const std::uint64_t logical_block_offset) {
  for (const auto &pending_write : pending_writes) {
    if (pending_write.owner == &entry && pending_write.logicalBlockOffset == logical_block_offset) {
      return &pending_write;
    }
  }

  return nullptr;
}

std::deque<QueuedPendingPartWrite>::iterator
FindPendingPartWriteIterator(std::deque<QueuedPendingPartWrite> &pending_writes,
                             const QueuedPendingPartWrite &pending_write) {
  return std::find_if(pending_writes.begin(), pending_writes.end(),
                      [&pending_write](const QueuedPendingPartWrite &candidate) {
                        return candidate.owner == pending_write.owner &&
                               candidate.logicalBlockOffset == pending_write.logicalBlockOffset;
                      });
}

std::shared_ptr<std::vector<std::uint8_t>>
MakeQueuedPendingPartWriteBytes(std::vector<std::uint8_t> bytes) {
  return std::make_shared<std::vector<std::uint8_t>>(std::move(bytes));
}

QueuedPendingPartWrite
MakeQueuedPendingPartWrite(StreamingPartPendingWrite pending_write) {
  QueuedPendingPartWrite queued_pending_write;
  queued_pending_write.owner = pending_write.owner;
  queued_pending_write.logicalBlockOffset = pending_write.logicalBlockOffset;
  queued_pending_write.bytes = MakeQueuedPendingPartWriteBytes(std::move(pending_write.bytes));
  return queued_pending_write;
}

QueuedStreamingPartWriteTask
MakeQueuedStreamingPartWriteTask(StreamingPartWriteTask task) {
  QueuedStreamingPartWriteTask queued_task;
  queued_task.metadataMode = task.metadataMode;
  queued_task.partFileHandle = task.partFileHandle;
  queued_task.metadataFileHandle = task.metadataFileHandle;
  queued_task.partFileState = task.partFileState;
  queued_task.manifestEntry = task.manifestEntry;
  queued_task.manifestEntryOffset = task.manifestEntryOffset;
  queued_task.manifestEntryCount = task.manifestEntryCount;
  queued_task.pendingWrite = MakeQueuedPendingPartWrite(std::move(task.pendingWrite));
  return queued_task;
}

std::uint64_t CombineLowHighDwords(const std::uint32_t low, const std::uint32_t high) {
  return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32);
}

constexpr std::uint64_t LegacyManifestRecordSize(const std::uint32_t block_count) {
  return kLegacyStreamingManifestEntryHeaderSize +
         (static_cast<std::uint64_t>(block_count) * kLegacyStreamingManifestSerializedBlockSize);
}

void SplitQwordToDwords(const std::uint64_t value, std::uint32_t &low, std::uint32_t &high) {
  low = static_cast<std::uint32_t>(value);
  high = static_cast<std::uint32_t>(value >> 32);
}

constexpr std::uint32_t BackgroundDownloadWorkerBit(const std::size_t slot_index) {
  return 1u << static_cast<std::uint32_t>(slot_index);
}

constexpr std::array<std::size_t, 6> kBackgroundDownloadPriorityQueueOffsets = {0, 1, 0, 2, 0, 1};

constexpr std::array<std::size_t, 2> kBackgroundDownloadPriorityQueueBases = {0, 4};

constexpr std::array<std::uint8_t, 256> BuildStreamingHexNibbleLookup() {
  std::array<std::uint8_t, 256> lookup{};

  for (unsigned char ch = '0'; ch <= '9'; ++ch) {
    lookup[ch] = static_cast<std::uint8_t>(ch - '0');
  }
  for (unsigned char ch = 'A'; ch <= 'F'; ++ch) {
    lookup[ch] = static_cast<std::uint8_t>(10 + (ch - 'A'));
  }
  for (unsigned char ch = 'a'; ch <= 'f'; ++ch) {
    lookup[ch] = static_cast<std::uint8_t>(10 + (ch - 'a'));
  }

  return lookup;
}

constexpr auto kStreamingHexNibbleLookup = BuildStreamingHexNibbleLookup();

struct StreamingPartBlockRange {
  std::size_t first = 0;
  std::size_t last = 0;
};

[[noreturn]] void FailStreamingPartParameterContract() {
  std::terminate();
}

StreamingPartBlockRange ResolveStreamingPartBlockRangeLocked(
    const StreamingPartEntryRuntime &entry, const std::uint64_t logical_offset,
    const std::uint32_t size, const bool allow_zero_size_single_block) {
  const auto block_size = entry.GetBlockSize();
  const auto block_count = entry.GetBlocks().size();
  if (block_size == 0 || block_count == 0) {
    FailStreamingPartParameterContract();
  }

  if (size == 0 && !allow_zero_size_single_block) {
    FailStreamingPartParameterContract();
  }

  const auto first_block = static_cast<std::size_t>(logical_offset / block_size);
  if (first_block >= block_count) {
    FailStreamingPartParameterContract();
  }

  std::size_t last_block = first_block;
  if (size != 0) {
    const auto last_byte_offset = logical_offset + static_cast<std::uint64_t>(size) - 1u;
    if (last_byte_offset < logical_offset) {
      FailStreamingPartParameterContract();
    }

    last_block = static_cast<std::size_t>(last_byte_offset / block_size);
    if (last_block >= block_count) {
      FailStreamingPartParameterContract();
    }
  }

  return {
      .first = first_block,
      .last = last_block,
  };
}

void CopyStreamingPartBytes(void *destination, const std::uint8_t *source, const std::size_t size) {
  if (size == 0) {
    return;
  }

  auto *destination_bytes = static_cast<std::uint8_t *>(destination);
  if (destination_bytes >= source + size || destination_bytes + size <= source) {
    std::memcpy(destination_bytes, source, size);
    return;
  }

  std::memmove(destination_bytes, source, size);
}

std::array<std::uint8_t, kLegacyStreamingManifestFileHeaderSize>
BuildLegacyStreamingManifestFileHeader(const std::uint32_t entry_count) {
  std::array<std::uint8_t, kLegacyStreamingManifestFileHeaderSize> header{};
  StoreLittleEndian32(header.data(), kLegacyStreamingManifestVersion);
  StoreLittleEndian32(header.data() + sizeof(std::uint32_t), entry_count);
  return header;
}

std::array<std::uint8_t, kLegacyStreamingManifestEntryHeaderSize>
BuildLegacyStreamingManifestEntryHeader(const StreamingEntry &entry) {
  std::array<std::uint8_t, kLegacyStreamingManifestEntryHeaderSize> header{};
  SStrCopy(reinterpret_cast<char *>(header.data()), entry.filename.c_str(),
           kLegacyStreamingManifestPathFieldSize);
  SStrCopy(
      reinterpret_cast<char *>(header.data() + kLegacyStreamingManifestPathFieldSize),
      entry.hash.c_str(), kLegacyStreamingManifestHashFieldSize);
  header[296] = entry.flags;
  StoreLittleEndian64(header.data() + 297, entry.fileSize);
  StoreLittleEndian64(header.data() + kLegacyStreamingManifestStorageBytesOffset,
                      entry.storageBytesUsed);
  StoreLittleEndian32(header.data() + 313, entry.blockSize);
  StoreLittleEndian32(header.data() + 317, entry.blockCount);
  return header;
}

void SerializeLegacyStreamingManifestBlock(const StreamingEntry::BlockMeta &block,
                                           std::uint8_t *destination) {
  StoreLittleEndian32(destination, block.key);
  StoreLittleEndian32(destination + 4, block.offset_lo);
  StoreLittleEndian32(destination + 8, block.offset_hi);
  StoreLittleEndian32(destination + 12, block.size_lo);
  StoreLittleEndian32(destination + 16, block.size_hi);
  StoreLittleEndian32(destination + 20, block.field14);
}

std::uint64_t QueryLooseFileHandleSize(const int handle) {
  return openwow::vfs::IOUnitContainer_GetFileSizeByHandle(handle);
}

bool AreAllStreamingEntryBlocksReady(const StreamingEntry &entry) {
  return std::all_of(entry.blocks.begin(), entry.blocks.end(),
                     [](const StreamingEntry::BlockMeta &block) { return block.key == 3u; });
}

bool ReadLooseFileExactAtOffset(const int handle, void *buffer, const std::uint64_t offset,
                                const std::uint32_t bytes_to_read) {
  std::uint32_t dispatched_bytes = bytes_to_read;
  return openwow::vfs::IOUnitContainer_ReadFileHandleAtOffset(handle, buffer, offset,
                                                              &dispatched_bytes) &&
         dispatched_bytes == bytes_to_read;
}

bool WriteLooseFileExactAtOffset(const int handle, const void *buffer, const std::uint64_t offset,
                                 const std::uint32_t bytes_to_write) {
  std::uint32_t dispatched_bytes = bytes_to_write;
  return openwow::vfs::IOUnitContainer_WriteFileHandleAtOffset(handle, buffer, offset,
                                                               &dispatched_bytes) &&
         dispatched_bytes == bytes_to_write;
}

bool StreamingPartHeaderPathMismatches(
    const std::array<char, 32> &on_disk_path,
    const std::string &expected_path) {
  const auto terminator =
      std::find(on_disk_path.begin(), on_disk_path.end(), '\0');
  if (terminator == on_disk_path.end()) {
    return true;
  }

  return expected_path !=
         std::string_view(on_disk_path.data(),
                          static_cast<std::size_t>(terminator - on_disk_path.begin()));
}

std::optional<std::uint32_t> ComputeStreamingPartBlockCount(
    const StreamingPartFileState &state) {
  if (state.blockSize == 0 || state.logicalFileSize == 0) {
    return state.blockSize == 0 ? std::nullopt
                                : std::optional<std::uint32_t>{0};
  }

  const std::uint64_t block_count =
      (state.logicalFileSize / state.blockSize) +
      static_cast<std::uint64_t>((state.logicalFileSize % state.blockSize) != 0);
  if (block_count > std::numeric_limits<std::uint32_t>::max() ||
      block_count * kStreamingPartFileSerializedBlockSize >
          std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }

  return static_cast<std::uint32_t>(block_count);
}

std::uint64_t ComputeStreamingPartStorageEnd(const std::uint32_t block_count,
                                             const std::uint32_t available_block_count,
                                             const std::uint32_t block_size) {
  return kStreamingPartFileHeaderSize +
         (static_cast<std::uint64_t>(block_count) * kStreamingPartFileSerializedBlockSize) +
         (static_cast<std::uint64_t>(available_block_count) * block_size);
}

std::string NormalizeStreamingManifestLookupPath(std::string_view raw_path) {
  if (raw_path.empty()) {
    return {};
  }

  char resolved_path[260] = {};
  const std::string source(raw_path);
  if (!openwow::vfs::FileSystem_MakeAbsolutePath(
          source.c_str(), resolved_path, static_cast<int>(sizeof(resolved_path)))) {
    return {};
  }

  std::string normalized;
  normalized.reserve(sizeof(resolved_path) - 1);
  for (std::size_t index = 0; index < (sizeof(resolved_path) - 1) && resolved_path[index] != '\0';
       ++index) {
    char value = resolved_path[index];
    if (value == '/') {
      value = '\\';
    }
    normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
  }

  return normalized;
}

bool ResetStreamingPartRuntimeState(StreamingPartFileState &state,
                                    const std::uint32_t block_count) {
  std::vector<StreamingPartBlockRecord> blocks;
  if (!TryResizeVector(&blocks, block_count)) {
    return false;
  }
  state.entry.SetRetired(false);
  state.entry.SetBlockSize(state.blockSize);
  state.entry.SetBlocks(std::move(blocks));
  return true;
}

bool StrIEqual(const char *a, const char *b) {
  if (!a || !b)
    return false;
  while (*a && *b) {
    char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
    if (ca != cb)
      return false;
    ++a;
    ++b;
  }
  return *a == *b;
}

}

std::size_t StreamingPathMd5KeyHash::operator()(const StreamingPathMd5Key &key) const noexcept {
  std::size_t seed = 1469598103934665603ull;
  for (const std::uint8_t byte : key) {
    seed ^= static_cast<std::size_t>(byte);
    seed *= 1099511628211ull;
  }
  return seed;
}

StreamingStorage &StreamingStorage::Instance() {
  static StreamingStorage inst;
  return inst;
}

int StreamingStorage::BuildVariantPath(char *destination, const int capacity, const char *base_path,
                                       const char *extension) const {
  if (!destination || capacity <= 0) {
    return 0;
  }

  const int copied_length = CopyStormPath(destination, base_path ? base_path : "", capacity);

  std::string suffix;
  std::uint32_t retry_ordinal = 0;
  {
    std::lock_guard lock(variantPathMutex_);
    suffix = variantPathSuffix_;
    retry_ordinal = variantPathRetryOrdinal_;
  }

  int length = copied_length;
  if (!suffix.empty()) {
    length = AppendStormPath(destination, ".", capacity);
    length = AppendStormPath(destination, suffix.c_str(), capacity);
  }
  if (retry_ordinal != 0) {
    char ordinal_buffer[16] = {};
    std::snprintf(ordinal_buffer, sizeof(ordinal_buffer), "%u", retry_ordinal);
    length = AppendStormPath(destination, ".", capacity);
    length = AppendStormPath(destination, ordinal_buffer, capacity);
  }
  if (extension && *extension) {
    length = AppendStormPath(destination, ".", capacity);
    length = AppendStormPath(destination, extension, capacity);
  }

  return length;
}

void StreamingStorage::SetVariantPathSuffix(const char *suffix) {
  std::lock_guard lock(variantPathMutex_);
  variantPathSuffix_ = suffix ? suffix : "";
}

bool StreamingStorage::IsSimpleMode() const {
  return simpleMode_;
}

void StreamingStorage::IncrementVariantPathRetryOrdinal() {
  std::lock_guard lock(variantPathMutex_);
  ++variantPathRetryOrdinal_;
}

void StreamingStorage::SetVariantPathStateForTests(const char *suffix,
                                                   const std::uint32_t retry_ordinal) {
  std::lock_guard lock(variantPathMutex_);
  variantPathSuffix_ = suffix ? suffix : "";
  variantPathRetryOrdinal_ = retry_ordinal;
}

void StreamingPartEntryRuntime::SetFlags(const std::uint32_t flags) {
  auto &storage = StreamingStorage::Instance();
  std::lock_guard lock(storage.streamingPartMutex_);
  flags_ = flags;
}

void StreamingPartEntryRuntime::SetBlockSize(const std::uint32_t block_size) {
  auto &storage = StreamingStorage::Instance();
  std::lock_guard lock(storage.streamingPartMutex_);
  blockSize_ = block_size;
}

void StreamingPartEntryRuntime::SetBlocks(std::vector<StreamingPartBlockRecord> blocks) {
  auto &storage = StreamingStorage::Instance();
  std::lock_guard lock(storage.streamingPartMutex_);
  blocks_ = std::move(blocks);
}

void StreamingPartEntryRuntime::SetBlock(const std::size_t index,
                                         const StreamingPartBlockRecord block) {
  auto &storage = StreamingStorage::Instance();
  std::lock_guard lock(storage.streamingPartMutex_);
  if (index >= blocks_.size()) {
    FailStreamingPartParameterContract();
  }

  blocks_[index] = block;
}

void StreamingPartEntryRuntime::SetRetired(const bool retired) {
  auto &storage = StreamingStorage::Instance();
  std::lock_guard lock(storage.streamingPartMutex_);
  retired_ = retired;
}

void StreamingTransferRateTracker::Reset(uint32_t start_tick_ms) {
  startTickMs_ = start_tick_ms;
  currentIndex_ = 0;
  samples_.fill({});
}

void StreamingTransferRateTracker::RecordSample(uint64_t total_bytes, uint32_t tick_ms) {
  ++currentIndex_;
  if (currentIndex_ >= kSampleCount) {
    currentIndex_ = 0;
  }

  auto &sample = samples_[currentIndex_];
  sample.tickMs = 0;
  sample.totalBytes = total_bytes;
  sample.tickMs = tick_ms;
}

uint32_t StreamingTransferRateTracker::EstimateBytesPerSecond() const {
  uint32_t first = 0;
  uint32_t last = 0;

  if (currentIndex_ == kSampleCount - 1) {
    first = 0;
  } else {
    first = currentIndex_ + 1;
  }

  last = currentIndex_;
  if (last == 0) {
    last = static_cast<uint32_t>(kSampleCount);
  }
  --last;

  while (samples_[first].tickMs == 0) {
    if (first == last) {
      break;
    }

    ++first;
    if (first >= kSampleCount) {
      first = 0;
    }
  }

  while (samples_[last].tickMs == 0) {
    if (first == last) {
      break;
    }

    if (last == 0) {
      last = static_cast<uint32_t>(kSampleCount);
    }
    --last;
  }

  uint64_t byte_delta = 0;
  if (samples_[last].totalBytes > samples_[first].totalBytes) {
    byte_delta = samples_[last].totalBytes - samples_[first].totalBytes;
  }

  int elapsed_ms =
      static_cast<int>(samples_[last].tickMs) - static_cast<int>(samples_[first].tickMs);
  if (first == last) {
    uint32_t next = last + 1;
    if (next >= kSampleCount) {
      next = 0;
    }

    if (samples_[next].tickMs != 0) {
      byte_delta = samples_[next].totalBytes;
      elapsed_ms = static_cast<int>(samples_[next].tickMs) - static_cast<int>(startTickMs_);
    }
  }

  if (byte_delta == 0 || elapsed_ms <= 0) {
    return 0;
  }

  return static_cast<uint32_t>(1000ULL * (byte_delta / static_cast<uint32_t>(elapsed_ms)));
}

bool StreamingStorage::ParseAttribute(StreamingStorageConfig &config, const char *attrName,
                                      const char *attrValue) {
  if (!attrName)
    return false;

  if (StrIEqual(attrName, "location")) {
    config.location.assign(attrValue);
    return true;
  }
  if (StrIEqual(attrName, "timeslotid")) {
    config.timeSlotIds.emplace_back(attrValue);
    return true;
  }
  if (StrIEqual(attrName, "maxretry")) {
    config.maxRetry = static_cast<int32_t>(std::atol(attrValue));
    return true;
  }
  if (StrIEqual(attrName, "candeactivate")) {
    config.canDeactivate = static_cast<int32_t>(std::atol(attrValue));
    return true;
  }

  return false;
}

bool StreamingStorage::ParseManifestBinary(const void *data, size_t dataSize) {

  if (!data || dataSize < 8) {
    ClearParsedManifestEntries();
    return false;
  }

  const auto *bytes = static_cast<const uint8_t *>(data);

  const std::uint32_t entryCount = LoadLittleEndian32(bytes + 4u);

  const uint8_t *ptr = bytes + 8;
  size_t remaining = dataSize - 8;
  std::uint64_t record_offset = kLegacyStreamingManifestFileHeaderSize;

  std::vector<StreamingEntry> newEntries;
  std::unordered_map<std::string, std::size_t> newEntryIndicesByLookupPath;

  const auto fail_parse = [this]() {
    ClearParsedManifestEntries();
    return false;
  };

  const std::size_t reservable_entry_count = std::min<std::size_t>(
      entryCount, remaining / kLegacyStreamingManifestEntryHeaderSize);
  try {
    newEntries.reserve(reservable_entry_count);
  } catch (const std::bad_alloc &) {
    return fail_parse();
  }

  for (uint32_t i = 0; i < entryCount && remaining > 0; ++i) {
    if (remaining < kLegacyStreamingManifestEntryHeaderSize)
      return fail_parse();

    StreamingEntry entry;
    entry.legacyManifestRecordOffset = record_offset;

    entry.filename = std::string(reinterpret_cast<const char *>(ptr),
                                 strnlen(reinterpret_cast<const char *>(ptr), 260));

    entry.hash = std::string(reinterpret_cast<const char *>(ptr + 260),
                             strnlen(reinterpret_cast<const char *>(ptr + 260), 37));

    entry.flags = ptr[296];

    entry.fileSize = LoadLittleEndian64(ptr + 297u);
    entry.storageBytesUsed = LoadLittleEndian64(ptr + 305u);
    entry.blockSize = LoadLittleEndian32(ptr + 313u);
    entry.blockCount = LoadLittleEndian32(ptr + 317u);

    if (entry.blockSize == 0)
      return fail_parse();
    const std::uint64_t expectedBlocks =
        (entry.fileSize / entry.blockSize) +
        static_cast<std::uint64_t>((entry.fileSize % entry.blockSize) != 0);
    if (expectedBlocks != entry.blockCount)
      return fail_parse();

    ptr += kLegacyStreamingManifestEntryHeaderSize;
    remaining -= kLegacyStreamingManifestEntryHeaderSize;

    if (entry.blockCount > remaining / kLegacyStreamingManifestSerializedBlockSize)
      return fail_parse();
    const std::size_t blockDataSize =
        static_cast<std::size_t>(entry.blockCount) *
        kLegacyStreamingManifestSerializedBlockSize;

    if (!TryResizeVector(&entry.blocks, entry.blockCount))
      return fail_parse();
    const uint8_t *blockPtr = ptr;
    for (uint32_t b = 0; b < entry.blockCount; ++b) {
      auto &blk = entry.blocks[b];
      blk.key = LoadLittleEndian32(blockPtr);
      blk.offset_lo = LoadLittleEndian32(blockPtr + 4u);
      blk.offset_hi = LoadLittleEndian32(blockPtr + 8u);
      blk.size_lo = LoadLittleEndian32(blockPtr + 12u);
      blk.size_hi = LoadLittleEndian32(blockPtr + 16u);
      blk.dup_size_lo = blk.size_lo;
      blk.dup_size_hi = blk.size_hi;
      blk.field14 = LoadLittleEndian32(blockPtr + 20u);
      blockPtr += kLegacyStreamingManifestSerializedBlockSize;
    }

    ptr += blockDataSize;
    remaining -= blockDataSize;
    record_offset += LegacyManifestRecordSize(entry.blockCount);

    const std::string lookup_path = NormalizeStreamingManifestLookupPath(entry.filename);
    const auto [index_it, inserted] =
        newEntryIndicesByLookupPath.emplace(lookup_path, newEntries.size());
    if (inserted) {
      newEntries.push_back(std::move(entry));
    } else {
      newEntries[index_it->second] = std::move(entry);
    }
  }

  entries_ = std::move(newEntries);
  entryIndicesByLookupPath_ = std::move(newEntryIndicesByLookupPath);
  initialized_ = true;
  return true;
}

std::uint64_t
StreamingStorage::ComputeNextLegacyManifestRecordOffsetLocked(
    const std::string_view excluded_lookup_key) const {
  std::map<std::string, const StreamingEntry *> ordered_entries;

  for (const auto &[lookup_key, entry_index] : entryIndicesByLookupPath_) {
    if (entry_index >= entries_.size()) {
      continue;
    }

    ordered_entries.insert_or_assign(lookup_key, &entries_[entry_index]);
  }

  for (const auto &[lookup_key, runtime_entry] : runtimeEntriesByLookupKey_) {
    if (runtime_entry == nullptr) {
      continue;
    }

    ordered_entries.insert_or_assign(lookup_key, runtime_entry.get());
  }

  std::uint64_t next_offset = kLegacyStreamingManifestFileHeaderSize;
  for (const auto &[lookup_key, entry] : ordered_entries) {
    if (!excluded_lookup_key.empty() && lookup_key == excluded_lookup_key) {
      continue;
    }

    if (entry == nullptr || (entry->flags & kLegacyStreamingManifestFlagSkipLocalWrite) != 0) {
      continue;
    }

    if (next_offset <= entry->legacyManifestRecordOffset) {
      next_offset = entry->legacyManifestRecordOffset + LegacyManifestRecordSize(entry->blockCount);
    }
  }

  return next_offset;
}

bool StreamingStorage::WriteLegacyManifestEntry(const int fileHandle, const StreamingEntry &entry,
                                                const std::uint64_t entryOffset,
                                                const std::uint32_t entryCount,
                                                const std::optional<std::uint32_t> blockIndex,
                                                const bool writeFullRecord,
                                                const std::uint32_t runtimeFlags) const {
  if (!storageReady_) {
    return false;
  }

  if ((runtimeFlags & kLegacyStreamingManifestFlagSkipLocalWrite) != 0) {
    return true;
  }

  const auto entryHeader = BuildLegacyStreamingManifestEntryHeader(entry);
  if (writeFullRecord) {
    const auto fileHeader = BuildLegacyStreamingManifestFileHeader(entryCount);
    if (!WriteLooseFileExactAtOffset(fileHandle, fileHeader.data(), 0,
                                     static_cast<std::uint32_t>(fileHeader.size()))) {
      return false;
    }

    if (!WriteLooseFileExactAtOffset(fileHandle, entryHeader.data(), entryOffset,
                                     static_cast<std::uint32_t>(entryHeader.size()))) {
      return false;
    }

    if (entry.blocks.empty()) {
      return true;
    }

    if (entry.blocks.size() >
        (std::numeric_limits<std::uint32_t>::max() / kLegacyStreamingManifestSerializedBlockSize)) {
      return false;
    }

    std::vector<std::uint8_t> serializedBlocks(entry.blocks.size() *
                                               kLegacyStreamingManifestSerializedBlockSize);
    for (std::size_t index = 0; index < entry.blocks.size(); ++index) {
      SerializeLegacyStreamingManifestBlock(
          entry.blocks[index],
          serializedBlocks.data() + (index * kLegacyStreamingManifestSerializedBlockSize));
    }

    return WriteLooseFileExactAtOffset(fileHandle, serializedBlocks.data(),
                                       entryOffset + kLegacyStreamingManifestEntryHeaderSize,
                                       static_cast<std::uint32_t>(serializedBlocks.size()));
  }

  if (!WriteLooseFileExactAtOffset(fileHandle,
                                   entryHeader.data() + kLegacyStreamingManifestStorageBytesOffset,
                                   entryOffset + kLegacyStreamingManifestStorageBytesOffset,
                                   static_cast<std::uint32_t>(sizeof(entry.storageBytesUsed)))) {
    return false;
  }

  if (!blockIndex.has_value() || *blockIndex >= entry.blocks.size()) {
    return true;
  }

  std::array<std::uint8_t, kLegacyStreamingManifestSerializedBlockSize> serializedBlock{};
  SerializeLegacyStreamingManifestBlock(entry.blocks[*blockIndex], serializedBlock.data());
  return WriteLooseFileExactAtOffset(
      fileHandle, serializedBlock.data(),
      entryOffset + kLegacyStreamingManifestEntryHeaderSize +
          (static_cast<std::uint64_t>(*blockIndex) * kLegacyStreamingManifestSerializedBlockSize),
      static_cast<std::uint32_t>(serializedBlock.size()));
}

const std::vector<StreamingEntry> &StreamingStorage::GetEntries() const {
  return entries_;
}

namespace {

StreamingEntry *FindStreamingEntryByPathLocked(
    std::unordered_map<std::string, std::unique_ptr<StreamingEntry>> &runtime_entries,
    std::unordered_map<std::string, std::size_t> &entry_indices,
    std::vector<StreamingEntry> &entries, const char *source_path) {
  if (source_path == nullptr) {
    return nullptr;
  }

  const std::string lookup_path = NormalizeStreamingManifestLookupPath(source_path);
  if (lookup_path.empty()) {
    return nullptr;
  }

  if (const auto runtime_it = runtime_entries.find(lookup_path); runtime_it != runtime_entries.end()) {
    return runtime_it->second.get();
  }

  const auto parsed_it = entry_indices.find(lookup_path);
  if (parsed_it == entry_indices.end() || parsed_it->second >= entries.size()) {
    return nullptr;
  }

  return &entries[parsed_it->second];
}

const StreamingEntry *FindStreamingEntryByPathLocked(
    const std::unordered_map<std::string, std::unique_ptr<StreamingEntry>> &runtime_entries,
    const std::unordered_map<std::string, std::size_t> &entry_indices,
    const std::vector<StreamingEntry> &entries, const char *source_path) {
  if (source_path == nullptr) {
    return nullptr;
  }

  const std::string lookup_path = NormalizeStreamingManifestLookupPath(source_path);
  if (lookup_path.empty()) {
    return nullptr;
  }

  if (const auto runtime_it = runtime_entries.find(lookup_path); runtime_it != runtime_entries.end()) {
    return runtime_it->second.get();
  }

  const auto parsed_it = entry_indices.find(lookup_path);
  if (parsed_it == entry_indices.end() || parsed_it->second >= entries.size()) {
    return nullptr;
  }

  return &entries[parsed_it->second];
}

}

bool StreamingStorage::HasManifestEntryForPath(const char *source_path) const {
  if (!storageReady_ || source_path == nullptr) {
    return false;
  }

  const std::string lookup_path = NormalizeStreamingManifestLookupPath(source_path);
  if (lookup_path.empty()) {
    return false;
  }

  std::lock_guard lock(streamingStateMutex_);
  if (runtimeEntriesByLookupKey_.find(lookup_path) != runtimeEntriesByLookupKey_.end()) {
    return true;
  }

  return entryIndicesByLookupPath_.find(lookup_path) != entryIndicesByLookupPath_.end();
}

std::optional<StreamingSourceRangeAvailability>
StreamingStorage::QuerySourceRangeAvailability(const char *source_path,
                                               const std::uint64_t logical_offset,
                                               const std::uint32_t size) const {
  if (!storageReady_ || source_path == nullptr) {
    return std::nullopt;
  }

  StreamingEntry entry_snapshot;
  {
    std::lock_guard lock(streamingStateMutex_);
    const StreamingEntry *const entry = FindStreamingEntryByPathLocked(
        runtimeEntriesByLookupKey_, entryIndicesByLookupPath_, entries_, source_path);
    if (entry == nullptr) {
      return std::nullopt;
    }

    entry_snapshot = *entry;
  }

  const auto descriptors = openwow::vfs::BuildSFileReadPlanDescriptors(
      entry_snapshot, static_cast<std::int64_t>(logical_offset), static_cast<std::int32_t>(size));
  if (descriptors.empty()) {
    return std::nullopt;
  }

  StreamingSourceRangeAvailability availability;
  for (const auto &descriptor : descriptors) {
    switch (descriptor.kind) {
    case openwow::vfs::SFileReadPlanDescriptorKind::kResidentRaw:
      availability.resident_bytes += descriptor.logical_size;
      break;
    case openwow::vfs::SFileReadPlanDescriptorKind::kPartialRaw:
    case openwow::vfs::SFileReadPlanDescriptorKind::kPartialCompressed:
      availability.saw_partial = true;
      break;
    case openwow::vfs::SFileReadPlanDescriptorKind::kMissingCompressed:
    case openwow::vfs::SFileReadPlanDescriptorKind::kDependency:
      availability.saw_missing = true;
      break;
    }
  }

  return availability;
}

bool StreamingStorage::MarkLogicalRangePendingCompressed(const char *source_path,
                                                         const std::uint64_t logical_offset,
                                                         const std::uint32_t size) {
  if (size == 0) {
    return false;
  }

  std::lock_guard lock(streamingStateMutex_);
  StreamingEntry *const entry = FindStreamingEntryByPathLocked(runtimeEntriesByLookupKey_,
                                                               entryIndicesByLookupPath_, entries_,
                                                               source_path);
  if (entry == nullptr || entry->blockSize == 0 || entry->blocks.empty() ||
      logical_offset >= entry->fileSize) {
    return false;
  }

  const std::uint64_t clamped_last_offset =
      std::min<std::uint64_t>(entry->fileSize - 1u, logical_offset + size - 1u);
  const std::size_t first_block = static_cast<std::size_t>(logical_offset / entry->blockSize);
  const std::size_t last_block = static_cast<std::size_t>(clamped_last_offset / entry->blockSize);
  if (first_block >= entry->blocks.size()) {
    return false;
  }

  const std::size_t clamped_last_block = std::min(last_block, entry->blocks.size() - 1u);
  for (std::size_t block_index = first_block; block_index <= clamped_last_block; ++block_index) {
    auto &block = entry->blocks[block_index];
    if (block.key == 2u || block.key == 3u) {
      continue;
    }

    block.key = 1u;
  }

  return true;
}

std::mutex &StreamingStorage::GetArchiveListMutex() {
  return archiveListMutex_;
}

std::mutex &StreamingStorage::GetStreamingStateMutex() {
  return streamingStateMutex_;
}

StreamingState StreamingStorage::GetState() const {
  std::lock_guard lock(streamingStateMutex_);
  return state_;
}

void StreamingStorage::SetState(const StreamingState &state) {
  std::lock_guard lock(streamingStateMutex_);
  state_ = state;
}

int StreamingStorage::GetBgPreloadSleep() const {

  return std::max(0, bgPreloadSleep_);
}

void StreamingStorage::SetBgPreloadSleep(int ms) {
  bgPreloadSleep_ = ms;
  openwow::data::SetManifestParameterIntValue("bgpreloadsleep", ms);
}

void StreamingStorage::SetBgPreloadSleepFromManifestParameter(int ms) {
  bgPreloadSleep_ = ms;
}

void StreamingStorage::ApplyBgPreloadSleepOverride(bool restore_saved_value) {
  if (restore_saved_value) {
    SetBgPreloadSleep(bgPreloadSleepRestoreValue_);
    return;
  }

  if (GetBgPreloadSleep() != 0) {
    bgPreloadSleepRestoreValue_ = GetBgPreloadSleep();
  }

  SetBgPreloadSleep(0);
}

StreamingStorage::SharedSFileTaskWorkerLease
StreamingStorage::AcquireSharedSFileTaskWorkers(const std::size_t worker_count) {
  if (worker_count == 0) {
    return {};
  }

  std::vector<std::unique_ptr<SharedSFileTaskWorker>> workers;
  workers.reserve(worker_count);

  std::uint64_t generation = 0;
  {
    std::lock_guard lock(sharedSFileTaskWorkerMutex_);
    sharedSFileTaskWorkerAcceptingReturns_ = true;
    generation = sharedSFileTaskWorkerGeneration_;
    while (!sharedSFileTaskWorkers_.empty() && workers.size() < worker_count) {
      workers.push_back(std::move(sharedSFileTaskWorkers_.back()));
      sharedSFileTaskWorkers_.pop_back();
    }
  }

  while (workers.size() < worker_count) {
    workers.push_back(std::make_unique<SharedSFileTaskWorker>());
  }

  return SharedSFileTaskWorkerLease(this, generation, std::move(workers));
}

void StreamingStorage::ReleaseSharedSFileTaskWorkers(
    const std::uint64_t generation, std::vector<std::unique_ptr<SharedSFileTaskWorker>> workers) {
  if (workers.empty()) {
    return;
  }

  {
    std::lock_guard lock(sharedSFileTaskWorkerMutex_);
    if (sharedSFileTaskWorkerAcceptingReturns_ && generation == sharedSFileTaskWorkerGeneration_) {
      for (auto &worker : workers) {
        sharedSFileTaskWorkers_.push_back(std::move(worker));
      }
      return;
    }
  }
}

void StreamingStorage::ShutdownSharedSFileTaskWorkers() {
  std::vector<std::unique_ptr<SharedSFileTaskWorker>> workers;
  {
    std::lock_guard lock(sharedSFileTaskWorkerMutex_);
    sharedSFileTaskWorkerAcceptingReturns_ = false;
    ++sharedSFileTaskWorkerGeneration_;
    workers.swap(sharedSFileTaskWorkers_);
  }
}

bool StreamingStorage::QueuePendingPartWriteTask(StreamingPartWriteTask task) {
  if (task.partFileState == nullptr) {
    FailStreamingPartParameterContract();
  }

  auto &entry = task.partFileState->entry;
  if (entry.blockSize_ == 0) {
    FailStreamingPartParameterContract();
  }

  const std::uint64_t block_index = task.pendingWrite.logicalBlockOffset / entry.blockSize_;
  QueuedStreamingPartWriteTask queued_task = MakeQueuedStreamingPartWriteTask(std::move(task));
  queued_task.pendingWrite.owner = &entry;

  {
    std::lock_guard lock(streamingPartMutex_);
    if (block_index >= entry.blocks_.size()) {
      FailStreamingPartParameterContract();
    }

    auto &block = entry.blocks_[block_index];
    if (block.state != StreamingPartBlockState::Missing &&
        block.state != StreamingPartBlockState::Reserved &&
        block.state != StreamingPartBlockState::Busy) {
      return false;
    }

    block.state = StreamingPartBlockState::PendingWrite;
    pendingPartWrites_.push_back(queued_task.pendingWrite);
  }

  {
    std::lock_guard lock(workerMutex_);
    pendingPartWriteTasks_.push_back(std::move(queued_task));
  }

  SignalWorker();
  return true;
}

bool StreamingStorage::TrySetPartEntryBlockStateRange(StreamingPartEntryRuntime &entry,
                                                      const std::uint64_t logical_offset,
                                                      const std::uint32_t size,
                                                      const StreamingPartBlockState state) {
  if (size == 0) {
    return false;
  }

  std::lock_guard lock(streamingPartMutex_);
  if (entry.retired_) {
    return false;
  }

  const auto block_range =
      ResolveStreamingPartBlockRangeLocked(entry, logical_offset, size, false);
  for (std::size_t block_index = block_range.first; block_index <= block_range.last; ++block_index) {
    auto &block = entry.blocks_[block_index];
    if (state == StreamingPartBlockState::Reserved &&
        (block.state == StreamingPartBlockState::PendingWrite ||
         block.state == StreamingPartBlockState::Available)) {
      continue;
    }

    block.state = state;
  }

  return true;
}

void StreamingStorage::SetPartEntryBlockStateRange(StreamingPartEntryRuntime &entry,
                                                   const std::uint64_t logical_offset,
                                                   const std::uint32_t size,
                                                   const StreamingPartBlockState state) {
  std::lock_guard lock(streamingPartMutex_);
  const auto block_range =
      ResolveStreamingPartBlockRangeLocked(entry, logical_offset, size, true);
  for (std::size_t block_index = block_range.first; block_index <= block_range.last; ++block_index) {
    entry.blocks_[block_index].state = state;
  }
}

void StreamingStorage::DrainPendingPartWriteTasksForHandle(const int partFileHandle) {
  std::vector<QueuedStreamingPartWriteTask> queued_tasks;
  {
    std::scoped_lock lock(workerMutex_, streamingPartMutex_);
    auto task_it = pendingPartWriteTasks_.begin();
    while (task_it != pendingPartWriteTasks_.end()) {
      if (task_it->partFileHandle != partFileHandle) {
        ++task_it;
        continue;
      }

      queued_tasks.push_back(std::move(*task_it));
      task_it = pendingPartWriteTasks_.erase(task_it);
    }
  }

  for (const auto &task : queued_tasks) {
    (void)FlushPendingPartWriteTask(task);
  }
}

bool StreamingStorage::MarkStreamingEntryBypassPartValidationIfAllBlocksReady(StreamingEntry &entry) {
  std::lock_guard lock(streamingStateMutex_);
  if (!AreAllStreamingEntryBlocksReady(entry)) {
    return false;
  }

  entry.bypassPartValidation = true;
  return true;
}

void StreamingStorage::ResetStreamingEntryNonReadyBlocks(StreamingEntry &entry) {
  std::lock_guard lock(streamingStateMutex_);
  for (auto &block : entry.blocks) {
    if (block.key != 3u) {
      block.key = 0u;
    }
  }
}

bool StreamingStorage::TryPopPendingPartWriteTask(QueuedStreamingPartWriteTask &task) {
  std::lock_guard lock(workerMutex_);
  if (pendingPartWriteTasks_.empty()) {
    return false;
  }

  task = std::move(pendingPartWriteTasks_.front());
  pendingPartWriteTasks_.pop_front();
  return true;
}

bool StreamingStorage::WritePartFileBlockRecord(const int fileHandle,
                                                const std::uint32_t blockIndex,
                                                const StreamingPartBlockRecord &block) const {
  StreamingPartFileSerializedBlock serialized_block{};
  serialized_block.availabilityTag = static_cast<std::uint32_t>(block.state);
  serialized_block.partFileOffset = block.partFileOffset;
  serialized_block.auxiliaryValue = block.auxiliaryValue;
  const auto serialized_bytes = SerializeStreamingPartFileBlock(serialized_block);
  return WriteLooseFileExactAtOffset(
      fileHandle, serialized_bytes.data(),
      kStreamingPartFileHeaderSize +
          (static_cast<std::uint64_t>(blockIndex) * kStreamingPartFileSerializedBlockSize),
      static_cast<std::uint32_t>(serialized_bytes.size()));
}

bool StreamingStorage::FlushPendingPartWriteTask(const QueuedStreamingPartWriteTask &task) {
  if (task.partFileState == nullptr) {
    return false;
  }

  auto &entry = task.partFileState->entry;
  std::lock_guard lock(streamingPartMutex_);
  const auto remove_pending_write_locked = [&]() {
    const auto it = FindPendingPartWriteIterator(pendingPartWrites_, task.pendingWrite);
    if (it != pendingPartWrites_.end()) {
      pendingPartWrites_.erase(it);
    }
  };

  if (entry.blockSize_ == 0) {
    FailStreamingPartParameterContract();
  }

  const std::uint64_t block_index = task.pendingWrite.logicalBlockOffset / entry.blockSize_;
  if (block_index >= entry.blocks_.size()) {
    FailStreamingPartParameterContract();
  }

  auto &block = entry.blocks_[block_index];
  if (block.state != StreamingPartBlockState::PendingWrite &&
      block.state != StreamingPartBlockState::Available) {
    remove_pending_write_locked();
    return false;
  }

  const bool is_last_block = block_index + 1 == entry.blocks_.size();
  const std::uint32_t block_size = entry.blockSize_;
  const auto &pending_bytes = *task.pendingWrite.bytes;
  if (!is_last_block && pending_bytes.size() < block_size) {
    FailStreamingPartParameterContract();
  }

  const bool needs_padded_tail = is_last_block && pending_bytes.size() != block_size;
  std::vector<std::uint8_t> padded_block;
  const void *write_buffer = pending_bytes.data();
  if (needs_padded_tail) {
    padded_block.resize(block_size);
    CopyStreamingPartBytes(padded_block.data(), pending_bytes.data(), pending_bytes.size());
    write_buffer = padded_block.data();
  }

  const bool had_existing_offset = block.partFileOffset != 0;
  const std::uint64_t write_offset =
      had_existing_offset ? block.partFileOffset : task.partFileState->storageEnd;
  const bool write_ok =
      WriteLooseFileExactAtOffset(task.partFileHandle, write_buffer, write_offset, block_size);
  if (!write_ok) {
    block.state = StreamingPartBlockState::Missing;
    remove_pending_write_locked();
    return false;
  }

  const std::uint64_t previous_storage_end = task.partFileState->storageEnd;
  const std::uint64_t previous_manifest_storage_bytes =
      task.manifestEntry != nullptr ? task.manifestEntry->storageBytesUsed : 0;
  const auto previous_manifest_storage_offset =
      task.manifestEntry != nullptr && block_index < task.manifestEntry->blocks.size()
          ? CombineLowHighDwords(task.manifestEntry->blocks[block_index].size_lo,
                                 task.manifestEntry->blocks[block_index].size_hi)
          : 0ull;

  if (!had_existing_offset) {
    block.partFileOffset = write_offset;
    task.partFileState->storageEnd += block_size;
  }
  block.state = StreamingPartBlockState::Available;

  bool metadata_ok = false;
  switch (task.metadataMode) {
  case StreamingPartWriteMetadataMode::PartFileBlockTable:
    metadata_ok = WritePartFileBlockRecord(task.partFileHandle,
                                           static_cast<std::uint32_t>(block_index), block);
    break;
  case StreamingPartWriteMetadataMode::LegacyManifest:
    if (task.manifestEntry == nullptr || block_index >= task.manifestEntry->blocks.size()) {
      metadata_ok = false;
      break;
    }

    task.manifestEntry->storageBytesUsed = task.partFileState->storageEnd;
    task.manifestEntry->blocks[block_index].key = static_cast<std::uint32_t>(block.state);
    SplitQwordToDwords(block.partFileOffset, task.manifestEntry->blocks[block_index].size_lo,
                       task.manifestEntry->blocks[block_index].size_hi);
    metadata_ok = WriteLegacyManifestEntry(task.metadataFileHandle, *task.manifestEntry,
                                           task.manifestEntryOffset, task.manifestEntryCount,
                                           static_cast<std::uint32_t>(block_index), false);
    break;
  }

  if (!metadata_ok) {
    if (task.manifestEntry != nullptr && block_index < task.manifestEntry->blocks.size()) {
      task.manifestEntry->storageBytesUsed = previous_manifest_storage_bytes;
      task.manifestEntry->blocks[block_index].key = 0;
      SplitQwordToDwords(previous_manifest_storage_offset,
                         task.manifestEntry->blocks[block_index].size_lo,
                         task.manifestEntry->blocks[block_index].size_hi);
    }
    if (!had_existing_offset) {
      block.partFileOffset = 0;
      task.partFileState->storageEnd = previous_storage_end;
    }
    block.state = StreamingPartBlockState::Missing;
  }

  remove_pending_write_locked();
  return metadata_ok;
}

bool StreamingStorage::InitSimple() {
  simpleMode_ = true;
  if (storageReady_)
    return true;

  {
    std::lock_guard archive_lock(archiveListMutex_);
    ClearParsedManifestEntries();
  }
  manifestPath_.clear();

  stopRequested_.store(false);
  workerStopped_.store(false);
  storageReady_ = true;

  workerThread_ = openwow::core::JthreadCompat([this](openwow::core::stop_token) { this->ThreadProc(); });

  return true;
}

bool StreamingStorage::Init(const std::string &manifestPath, bool strictMode) {
  simpleMode_ = false;

  if (storageReady_)
    return false;

  {
    std::lock_guard archive_lock(archiveListMutex_);
    ClearParsedManifestEntries();
  }

  stopRequested_.store(false);
  workerStopped_.store(false);
  storageReady_ = true;
  manifestPath_.clear();
  storageManifestFileHandle_ = 0;

  workerThread_ = openwow::core::JthreadCompat([this](openwow::core::stop_token) { this->ThreadProc(); });

  if (manifestPath.empty())
    return false;

  std::array<char, 260> absolute_manifest_path{};
  if (!openwow::vfs::FileSystem_MakeAbsolutePath(
          manifestPath.c_str(), absolute_manifest_path.data(),
          static_cast<int>(absolute_manifest_path.size()))) {
    return false;
  }
  manifestPath_ = absolute_manifest_path.data();

  void *loaded_manifest_buffer = nullptr;
  int loaded_manifest_size = 0;
  int opened_storage_handle = 0;

  const auto release_loaded_manifest = [&]() {
    if (loaded_manifest_buffer == nullptr) {
      return;
    }

    (void)openwow::vfs::SFileFreeLoadedData(loaded_manifest_buffer);
    loaded_manifest_buffer = nullptr;
  };

  const auto close_opened_storage_handle = [&]() {
    if (opened_storage_handle == 0) {
      return;
    }

    (void)openwow::vfs::IOUnitContainer_CloseFileHandle(opened_storage_handle);
    opened_storage_handle = 0;
  };

  for (int attempt = 0; attempt < 25 && loaded_manifest_buffer == nullptr; ++attempt) {
    std::array<char, 260> variant_path{};
    BuildVariantPath(variant_path.data(), static_cast<int>(variant_path.size()),
                     manifestPath.c_str(), "");

    if (openwow::vfs::FileSystem_GetPathType(variant_path.data()) ==
        openwow::vfs::FileSystemPathType::kMissing) {
      break;
    }

    if (!openwow::vfs::IOUnitContainer_CreateFileHandle(variant_path.data(), 0x1003u,
                                                        &opened_storage_handle)) {
      IncrementVariantPathRetryOrdinal();
      continue;
    }

    if (!openwow::vfs::IOUnitContainer_LoadFileHandleData(opened_storage_handle,
                                                          &loaded_manifest_buffer,
                                                          &loaded_manifest_size)) {
      break;
    }
  }

  if (loaded_manifest_buffer == nullptr) {
    if (strictMode) {
      storageManifestFileHandle_ = opened_storage_handle;
      return false;
    }

    if (opened_storage_handle != 0) {
      close_opened_storage_handle();
      (void)openwow::vfs::FileSystem_DeleteFile(manifestPath.c_str());
    }

    simpleMode_ = true;
    return true;
  }

  const bool parsed = ParseManifestBinary(
      loaded_manifest_buffer,
      static_cast<std::size_t>(static_cast<std::uint32_t>(loaded_manifest_size)));
  release_loaded_manifest();

  if (parsed) {
    storageManifestFileHandle_ = opened_storage_handle;
    return true;
  }

  if (strictMode) {
    storageManifestFileHandle_ = opened_storage_handle;
    return false;
  }

  close_opened_storage_handle();
  simpleMode_ = true;
  (void)openwow::vfs::FileSystem_DeleteFile(manifestPath.c_str());
  return true;
}

int StreamingStorage::ThreadProc() {
  if (stopRequested_.load()) {
    workerStopped_.store(true);
    return 1;
  }

  while (true) {
    QueuedStreamingPartWriteTask task;
    {
      std::unique_lock<std::mutex> lock(workerMutex_);
      workerCV_.wait(lock,
                     [this]() { return stopRequested_.load() || !pendingPartWriteTasks_.empty(); });
      if (stopRequested_.load()) {
        break;
      }
      task = std::move(pendingPartWriteTasks_.front());
      pendingPartWriteTasks_.pop_front();
    }

    (void)FlushPendingPartWriteTask(task);
  }

  workerStopped_.store(true);
  return 1;
}

void StreamingStorage::SignalWorker() {
  workerCV_.notify_one();
  SignalBackgroundDownloadThrottleEvent();
}

void StreamingStorage::RequestStop() {
  stopRequested_.store(true);
  workerCV_.notify_all();
  SignalBackgroundDownloadThrottleEvent();
  backgroundDownloadTaskIdleCv_.notify_all();
  for (auto &slot : backgroundDownloadWorkers_) {
    slot.wait_cv.notify_all();
  }
}

void StreamingStorage::Shutdown() {
  RequestStop();

  if (workerThread_.joinable()) {
    workerThread_.request_stop();
    workerThread_.join();
  }
  workerThread_ = {};

  if (storageManifestFileHandle_ != 0) {
    (void)openwow::vfs::IOUnitContainer_CloseFileHandle(storageManifestFileHandle_);
    storageManifestFileHandle_ = 0;
  }

  ShutdownBackgroundDownloadWorkers();
  ShutdownSharedSFileTaskWorkers();

  {
    std::lock_guard archive_lock(archiveListMutex_);
    ClearParsedManifestEntries();
  }
  {
    std::lock_guard state_lock(streamingStateMutex_);
    state_ = {};
    streamingStateValue_ = 0;
  }
  {
    std::lock_guard preload_lock(preloadPartialProgressMutex_);
    preloadPartialProgressByKey_.clear();
  }
  {
    std::lock_guard streaming_part_lock(streamingPartMutex_);
    pendingPartWrites_.clear();
  }
  {
    std::lock_guard worker_lock(workerMutex_);
    pendingPartWriteTasks_.clear();
  }

  initialized_ = false;
  storageReady_ = false;
  simpleMode_ = false;
  manifestPath_.clear();
  stopRequested_.store(false);
  workerStopped_.store(false);
  backgroundDownloadTaskIdleCv_.notify_all();
}

void StreamingStorage::ClearParsedManifestEntries() {
  entries_.clear();
  entryIndicesByLookupPath_.clear();
  runtimeEntriesByLookupKey_.clear();
  initialized_ = false;
}

void StreamingStorage::InitializeBackgroundDownloadWorkers() {
  std::lock_guard lock(backgroundDownloadMutex_);
  if (backgroundDownloadWorkersInitialized_) {
    return;
  }

  {
    std::lock_guard throttle_lock(backgroundDownloadThrottleMutex_);
    backgroundDownloadThrottleSignaled_ = false;
  }

  backgroundDownloadWorkerAliveMask_.store(0, std::memory_order_release);
  backgroundDownloadWorkerBusyMask_.store(0, std::memory_order_release);
  backgroundDownloadPriorityRotation_.fill(0);

  for (std::size_t slot_index = 0; slot_index < kBackgroundDownloadWorkerCount; ++slot_index) {
    auto &slot = backgroundDownloadWorkers_[slot_index];
    slot.thread = openwow::core::JthreadCompat([this, slot_index](openwow::core::stop_token stop_token) {
      BackgroundDownloadWorkerThread(slot_index, stop_token);
    });
    backgroundDownloadWorkerAliveMask_.fetch_or(BackgroundDownloadWorkerBit(slot_index),
                                                std::memory_order_acq_rel);
  }

  backgroundDownloadWorkersInitialized_ = true;
}

int StreamingStorage::FindIdleBackgroundDownloadWorkerSlot() const {
  const std::uint32_t alive_mask =
      backgroundDownloadWorkerAliveMask_.load(std::memory_order_acquire);
  const std::uint32_t busy_mask = backgroundDownloadWorkerBusyMask_.load(std::memory_order_acquire);

  for (int slot_index = 0; slot_index < static_cast<int>(kBackgroundDownloadWorkerCount);
       ++slot_index) {
    const std::uint32_t slot_bit =
        BackgroundDownloadWorkerBit(static_cast<std::size_t>(slot_index));
    if ((alive_mask & slot_bit) != 0 && (busy_mask & slot_bit) == 0) {
      return slot_index;
    }
  }

  return -1;
}

bool StreamingStorage::HasPendingBackgroundDownloadTasksLocked() const {
  return std::any_of(backgroundDownloadQueues_.begin(), backgroundDownloadQueues_.end(),
                     [](const auto &queue) { return !queue.empty(); });
}

bool StreamingStorage::HasActiveBackgroundDownloadDispatchesLocked() const {
  return std::any_of(backgroundDownloadWorkers_.begin(), backgroundDownloadWorkers_.end(),
                     [](const BackgroundDownloadWorkerSlot &slot) {
                       return slot.active_task_kind ==
                              BackgroundDownloadActiveTaskKind::kDispatch;
                     });
}

bool StreamingStorage::TryDequeueBackgroundDownloadTaskLocked(const std::size_t queue_index,
                                                              BackgroundDownloadTaskRecord &task) {
  if (queue_index >= kBackgroundDownloadQueueCount) {
    return false;
  }

  auto &queue = backgroundDownloadQueues_[queue_index];
  if (queue.empty()) {
    return false;
  }

  task = std::move(queue.front());
  queue.pop_front();
  ++backgroundDownloadDequeueCount_;
  return true;
}

bool StreamingStorage::TryDequeueBackgroundDownloadTaskByPriorityGroupLocked(
    const std::size_t priority_group, BackgroundDownloadTaskRecord &task,
    std::size_t &queue_index) {
  if (priority_group >= kBackgroundDownloadPriorityGroupCount) {
    return false;
  }

  if (priority_group != 0 && !openwow::data::IsOnlineModeActive()) {
    return false;
  }

  const std::size_t base_queue = kBackgroundDownloadPriorityQueueBases[priority_group];
  auto &rotation = backgroundDownloadPriorityRotation_[priority_group];

  for (std::size_t attempt = 0; attempt < kBackgroundDownloadPriorityOrderLength; ++attempt) {
    const std::size_t sequence_index = rotation % kBackgroundDownloadPriorityOrderLength;
    rotation = (sequence_index + 1) % kBackgroundDownloadPriorityOrderLength;

    const std::size_t candidate_queue =
        base_queue + kBackgroundDownloadPriorityQueueOffsets[sequence_index];
    if (!TryDequeueBackgroundDownloadTaskLocked(candidate_queue, task)) {
      continue;
    }

    queue_index = candidate_queue;
    return true;
  }

  return false;
}

std::size_t
StreamingStorage::SelectNextBackgroundDownloadQueueLocked(const std::size_t worker_slot,
                                                          BackgroundDownloadTaskRecord &task) {
  (void)worker_slot;

  std::size_t queue_index = kBackgroundDownloadNoQueue;
  if (TryDequeueBackgroundDownloadTaskByPriorityGroupLocked(0, task, queue_index)) {
    return queue_index;
  }

  if (TryDequeueBackgroundDownloadTaskLocked(kBackgroundDownloadDirectQueueIndex, task)) {
    return kBackgroundDownloadDirectQueueIndex;
  }

  if (TryDequeueBackgroundDownloadTaskByPriorityGroupLocked(1, task, queue_index)) {
    return queue_index;
  }

  ++backgroundDownloadScheduleMissCount_;
  return kBackgroundDownloadNoQueue;
}

std::optional<std::size_t> StreamingStorage::FindWakeableBackgroundDownloadWorkerLocked() const {
  if (!HasPendingBackgroundDownloadTasksLocked()) {
    return std::nullopt;
  }

  const std::uint32_t alive_mask =
      backgroundDownloadWorkerAliveMask_.load(std::memory_order_acquire);
  const std::uint32_t busy_mask = backgroundDownloadWorkerBusyMask_.load(std::memory_order_acquire);
  for (std::size_t slot_index = 0; slot_index < kBackgroundDownloadWorkerCount; ++slot_index) {
    const std::uint32_t slot_bit = BackgroundDownloadWorkerBit(slot_index);
    if ((alive_mask & slot_bit) == 0 || (busy_mask & slot_bit) != 0) {
      continue;
    }

    return slot_index;
  }

  return std::nullopt;
}

void StreamingStorage::RequeueBackgroundDownloadTaskToFrontLocked(
    const std::size_t queue_index, const BackgroundDownloadTaskRecord &task) {
  if (queue_index >= kBackgroundDownloadQueueCount) {
    return;
  }

  backgroundDownloadQueues_[queue_index].push_front(task);
}

void StreamingStorage::SignalBackgroundDownloadThrottleEvent() {
  {
    std::lock_guard lock(backgroundDownloadThrottleMutex_);
    backgroundDownloadThrottleSignaled_ = true;
  }

  backgroundDownloadThrottleCv_.notify_one();
}

void StreamingStorage::WaitForBackgroundDownloadThrottleEvent(
    openwow::core::stop_token stop_token, const std::chrono::milliseconds timeout) {
  if (timeout.count() <= 0) {
    return;
  }

  std::unique_lock lock(backgroundDownloadThrottleMutex_);
  (void)backgroundDownloadThrottleCv_.wait_for(lock, timeout, [this, &stop_token]() {
    return stop_token.stop_requested() || backgroundDownloadThrottleSignaled_;
  });
  if (backgroundDownloadThrottleSignaled_) {
    backgroundDownloadThrottleSignaled_ = false;
  }
}

std::shared_ptr<StreamingStorage::BackgroundDownloadSourceState>
StreamingStorage::TryAcquireBackgroundDownloadSourceLease(
    const std::weak_ptr<BackgroundDownloadSourceState> &source) {
  auto retained_source = source.lock();
  if (!retained_source) {
    return {};
  }

  std::lock_guard lock(retained_source->mutex);
  if (retained_source->closed) {
    return {};
  }

  return retained_source;
}

void StreamingStorage::ResetBackgroundDownloadStateLocked() {
  backgroundDownloadWorkersInitialized_ = false;
  backgroundDownloadPriorityRotation_.fill(0);
  for (auto &slot : backgroundDownloadWorkers_) {
    slot.active_task_kind = BackgroundDownloadActiveTaskKind::kIdle;
  }
  for (auto &queue : backgroundDownloadQueues_) {
    queue.clear();
  }
  backgroundDownloadSources_.clear();
  nextBackgroundDownloadSourceId_ = 1;
  backgroundDownloadDequeueCount_ = 0;
  backgroundDownloadScheduleMissCount_ = 0;
  backgroundDownloadCompletedTaskCount_ = 0;
  backgroundDownloadSkippedTaskCount_ = 0;
  {
    std::lock_guard throttle_lock(backgroundDownloadThrottleMutex_);
    backgroundDownloadThrottleSignaled_ = false;
  }
}

void StreamingStorage::ShutdownBackgroundDownloadWorkers() {
  {
    std::lock_guard lock(backgroundDownloadMutex_);
    if (!backgroundDownloadWorkersInitialized_) {
      ResetBackgroundDownloadStateLocked();
      backgroundDownloadWorkerAliveMask_.store(0, std::memory_order_release);
      backgroundDownloadWorkerBusyMask_.store(0, std::memory_order_release);
      return;
    }

    for (auto &slot : backgroundDownloadWorkers_) {
      slot.thread.request_stop();
    }

    for (auto &slot : backgroundDownloadWorkers_) {
      slot.wait_cv.notify_all();
    }
  }

  for (auto &slot : backgroundDownloadWorkers_) {
    if (slot.thread.joinable()) {
      slot.thread.join();
    }
  }

  std::lock_guard lock(backgroundDownloadMutex_);
  ResetBackgroundDownloadStateLocked();
  backgroundDownloadWorkerAliveMask_.store(0, std::memory_order_release);
  backgroundDownloadWorkerBusyMask_.store(0, std::memory_order_release);
  backgroundDownloadTaskIdleCv_.notify_all();
}

void StreamingStorage::BackgroundDownloadWorkerThread(const std::size_t slot_index,
                                                      openwow::core::stop_token stop_token) {
  auto &slot = backgroundDownloadWorkers_[slot_index];
  const std::uint32_t slot_bit = BackgroundDownloadWorkerBit(slot_index);

  while (!stop_token.stop_requested()) {
    std::unique_lock wait_lock(slot.wait_mutex);
    (void)slot.wait_cv.wait_for(wait_lock, std::chrono::milliseconds(100), [this, &stop_token]() {
      std::lock_guard queue_lock(backgroundDownloadMutex_);
      return stop_token.stop_requested() || HasPendingBackgroundDownloadTasksLocked();
    });
    wait_lock.unlock();

    if (stop_token.stop_requested()) {
      break;
    }

    BackgroundDownloadTaskRecord task;
    std::size_t selected_queue = kBackgroundDownloadNoQueue;
    bool queued_tasks_remain_after_dequeue = false;
    {
      std::lock_guard queue_lock(backgroundDownloadMutex_);
      backgroundDownloadWorkerBusyMask_.fetch_or(slot_bit, std::memory_order_acq_rel);
      selected_queue = SelectNextBackgroundDownloadQueueLocked(slot_index, task);
      if (selected_queue == kBackgroundDownloadNoQueue) {
        slot.active_task_kind = BackgroundDownloadActiveTaskKind::kIdle;
        backgroundDownloadWorkerBusyMask_.fetch_and(~slot_bit, std::memory_order_acq_rel);
        backgroundDownloadTaskIdleCv_.notify_all();
        continue;
      }

      slot.active_task_kind = task.is_queue_barrier
                                  ? BackgroundDownloadActiveTaskKind::kQueueBarrier
                                  : BackgroundDownloadActiveTaskKind::kDispatch;
      queued_tasks_remain_after_dequeue = HasPendingBackgroundDownloadTasksLocked();
    }

    bool should_retry_front = false;
    bool executed = false;
    if (task.is_queue_barrier) {
      if (!queued_tasks_remain_after_dequeue) {

        std::unique_lock queue_lock(backgroundDownloadMutex_);
        backgroundDownloadTaskIdleCv_.wait(queue_lock, [this]() {
          return !HasActiveBackgroundDownloadDispatchesLocked();
        });
      }

      if (task.queue_barrier) {
        {
          std::lock_guard state_lock(task.queue_barrier->mutex);
          task.queue_barrier->completed = true;
        }
        task.queue_barrier->completed_cv.notify_all();
      }
      executed = true;
    } else {
      const auto retained_source = TryAcquireBackgroundDownloadSourceLease(task.source);
      executed = static_cast<bool>(retained_source);
      BackgroundDownloadTaskCallback on_dispatch;
      if (retained_source) {
        std::lock_guard source_lock(retained_source->mutex);
        on_dispatch = retained_source->on_dispatch;
      }

      BackgroundDownloadTaskDispatchResult dispatch_result =
          BackgroundDownloadTaskDispatchResult::kComplete;
      if (retained_source && on_dispatch) {
        dispatch_result = on_dispatch();
      }

      const bool retry_requested =
          retained_source && dispatch_result == BackgroundDownloadTaskDispatchResult::kRetryFront;
      if (retry_requested) {
        openwow::data::Streaming_RecordDownloadRetry();
        std::lock_guard source_lock(retained_source->mutex);
        should_retry_front = !retained_source->closed;
      }
    }

    std::optional<std::size_t> next_slot_to_wake;
    {
      std::lock_guard queue_lock(backgroundDownloadMutex_);
      if (should_retry_front) {
        RequeueBackgroundDownloadTaskToFrontLocked(selected_queue, task);
      } else if (executed) {
        ++backgroundDownloadCompletedTaskCount_;
      } else {
        ++backgroundDownloadSkippedTaskCount_;
      }

      slot.active_task_kind = BackgroundDownloadActiveTaskKind::kIdle;
      backgroundDownloadWorkerBusyMask_.fetch_and(~slot_bit, std::memory_order_acq_rel);
      next_slot_to_wake = FindWakeableBackgroundDownloadWorkerLocked();

      backgroundDownloadTaskIdleCv_.notify_all();
    }

    if (next_slot_to_wake.has_value()) {
      backgroundDownloadWorkers_[*next_slot_to_wake].wait_cv.notify_all();
    }

    if (selected_queue > 2) {

      WaitForBackgroundDownloadThrottleEvent(stop_token,
                                             std::chrono::milliseconds(GetBgPreloadSleep()));
    }
  }

  {
    std::lock_guard queue_lock(backgroundDownloadMutex_);
    slot.active_task_kind = BackgroundDownloadActiveTaskKind::kIdle;
  }
  backgroundDownloadWorkerBusyMask_.fetch_and(~slot_bit, std::memory_order_acq_rel);
  backgroundDownloadWorkerAliveMask_.fetch_and(~slot_bit, std::memory_order_acq_rel);
  backgroundDownloadTaskIdleCv_.notify_all();
}

bool StreamingStorage::IsReady() const {
  return storageReady_;
}

void StreamingStorage::SetStreamingStateValue(uint32_t value) {
  std::lock_guard lock(streamingStateMutex_);
  streamingStateValue_ = value;
}

bool StreamingStorage::SetManifestEntryAttribute(FileManifestEntry &entry, const char *attrName,
                                                 const char *attrValue) {
  if (!attrName)
    return false;

  if (StrIEqual(attrName, "name")) {
    entry.name = attrValue;
    return true;
  }
  if (StrIEqual(attrName, "size")) {
    entry.size = std::atoll(attrValue);
    return true;
  }
  if (StrIEqual(attrName, "fileversion")) {
    entry.fileVersion = attrValue;
    return true;
  }
  if (StrIEqual(attrName, "flags")) {
    entry.flags = static_cast<uint32_t>(std::atol(attrValue));
    return true;
  }
  if (StrIEqual(attrName, "path")) {
    entry.path = attrValue;
    return true;
  }
  if (StrIEqual(attrName, "transportitem")) {
    entry.transportItem = attrValue;
    return true;
  }
  if (StrIEqual(attrName, "validserver")) {
    entry.validServers.emplace_back(attrValue);
    return true;
  }
  return false;
}

std::string StreamingStorage::BuildManifestEntryRelativeFilePath(const FileManifestEntry &entry) {
  std::string relative_path;
  relative_path.reserve(entry.path.size() + entry.name.size());
  relative_path.append(entry.path);
  relative_path.append(entry.name);
  return relative_path;
}

StreamingChecksummedRange StreamingStorage::MapLogicalRangeToChecksummedStorageRange(
    uint64_t logicalBegin, uint64_t logicalEndInclusive, uint32_t blockSize,
    uint64_t logicalFileSize) {
  const auto blockSize64 = static_cast<uint64_t>(blockSize);
  const uint64_t firstBlockIndex = logicalBegin / blockSize64;
  const uint64_t alignedLogicalBegin = firstBlockIndex * blockSize64;

  const uint64_t lastBlockIndex = logicalEndInclusive / blockSize64;
  const uint64_t alignedLogicalEndExclusive = (lastBlockIndex + 1) * blockSize64;
  const uint64_t cappedLogicalEndInclusive =
      std::min(alignedLogicalEndExclusive, logicalFileSize) - 1;

  StreamingChecksummedRange result;
  result.storageBegin = alignedLogicalBegin + firstBlockIndex * kStreamingChecksumTrailerBytes;
  result.storageEnd =
      cappedLogicalEndInclusive + (lastBlockIndex + 1) * kStreamingChecksumTrailerBytes;
  result.dataOffset = static_cast<uint32_t>(logicalBegin - alignedLogicalBegin);
  return result;
}

StreamingPathMd5Key StreamingStorage::MakeLowercasePathMd5Key(const char *path) {
  std::array<char, 260> buffer{};

  if (path) {
    std::size_t length = 0;
    while (length + 1 < buffer.size() && path[length] != '\0') {
      buffer[length] = path[length];
      ++length;
    }
    buffer[length] = '\0';
  }

  for (char &ch : buffer) {
    if (ch == '\0') {
      break;
    }

    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }

  return MD5_Digest(reinterpret_cast<const std::uint8_t *>(buffer.data()),
                    std::strlen(buffer.data()));
}

bool StreamingStorage::DecodeMd5HexDigest32(std::string_view text, std::uint8_t out_digest[16]) {
  if (out_digest == nullptr || text.size() != 32u) {
    return false;
  }

  for (std::size_t index = 0; index < 16; ++index) {
    const auto high = kStreamingHexNibbleLookup[static_cast<unsigned char>(text[index * 2])];
    const auto low = kStreamingHexNibbleLookup[static_cast<unsigned char>(text[index * 2 + 1])];
    out_digest[index] = static_cast<std::uint8_t>((high << 4) | low);
  }

  return true;
}

bool StreamingStorage::VerifyDownloadChecksumBlocks(const std::string_view checksummed_storage,
                                                    const std::uint32_t block_size,
                                                    std::string &out_data) {
  out_data.clear();
  if (block_size == 0u) {
    return false;
  }

  out_data.reserve(checksummed_storage.size());

  std::size_t offset = 0;
  while (offset < checksummed_storage.size()) {
    const std::size_t remaining_size = checksummed_storage.size() - offset;
    if (remaining_size < kStreamingChecksumTrailerBytes) {
      return false;
    }

    const std::size_t block_data_size =
        std::min<std::size_t>(block_size, remaining_size - kStreamingChecksumTrailerBytes);
    const std::string_view block_data = checksummed_storage.substr(offset, block_data_size);
    const std::string_view checksum_text =
        checksummed_storage.substr(offset + block_data_size, kStreamingChecksumTrailerBytes);

    out_data.append(block_data.data(), block_data.size());

    std::array<std::uint8_t, 16> expected_digest{};
    if (!DecodeMd5HexDigest32(checksum_text, expected_digest.data())) {
      return false;
    }

    const auto actual_digest = MD5_Digest(block_data.data(), block_data.size());
    if (actual_digest != expected_digest) {
      return false;
    }

    offset += block_data_size + kStreamingChecksumTrailerBytes;
  }

  return true;
}

std::optional<std::uint64_t>
StreamingStorage::LookupPreloadPartialProgress(const char *path) const {
  const StreamingPathMd5Key key = MakeLowercasePathMd5Key(path);
  std::lock_guard lock(preloadPartialProgressMutex_);
  const auto it = preloadPartialProgressByKey_.find(key);
  if (it == preloadPartialProgressByKey_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void StreamingStorage::RecordPreloadPartialProgress(const char *path,
                                                    const std::uint64_t loaded_bytes) {
  const StreamingPathMd5Key key = MakeLowercasePathMd5Key(path);
  std::lock_guard lock(preloadPartialProgressMutex_);
  preloadPartialProgressByKey_[key] = loaded_bytes;
}

void StreamingStorage::ResetPreloadPartialProgressCache() {
  std::lock_guard lock(preloadPartialProgressMutex_);
  preloadPartialProgressByKey_.clear();
}

bool StreamingStorage::ReadPartEntryLogicalSpan(
    const StreamingPartEntryRuntime &entry, void *destination, const std::uint64_t logical_offset,
    const std::uint32_t size,
    const std::function<bool(std::uint64_t part_file_offset)> &part_file_reader) const {
  std::unique_lock lock(streamingPartMutex_);
  if (entry.blockSize_ == 0) {
    FailStreamingPartParameterContract();
  }

  const std::uint64_t block_index = logical_offset / entry.blockSize_;
  if (block_index >= entry.blocks_.size()) {
    FailStreamingPartParameterContract();
  }

  if ((entry.flags_ & 4u) != 0) {
    return false;
  }

  const std::uint64_t block_base_offset = block_index * entry.blockSize_;

  while (entry.blocks_[block_index].state == StreamingPartBlockState::Busy) {
    lock.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    lock.lock();
  }

  const auto &block = entry.blocks_[block_index];
  if (block.state == StreamingPartBlockState::Available) {
    if (!part_file_reader) {
      return false;
    }

    const std::uint64_t part_file_offset =
        logical_offset + block.partFileOffset - block_base_offset;
    return part_file_reader(part_file_offset);
  }

  const auto *const pending_write =
      FindPendingPartWrite(pendingPartWrites_, entry, block_base_offset);
  if (pending_write != nullptr) {
    const std::uint64_t source_offset = logical_offset - block_base_offset;
    const auto &pending_bytes = *pending_write->bytes;
    if (source_offset > pending_bytes.size() ||
        pending_bytes.size() - static_cast<std::size_t>(source_offset) < size) {
      return false;
    }

    CopyStreamingPartBytes(destination, pending_bytes.data() + source_offset, size);
    return true;
  }

  return false;
}

bool StreamingStorage::GetExeDirectory(char *outBuf, uint32_t bufSize) {
  return openwow::core::GetExeDirectory(outBuf, bufSize);
}

bool StreamingStorage::SetBasePath(const char *path) {
  std::lock_guard lock(streamingStateMutex_);

  if (!path || !*path) {
    char exeDir[260];
    if (!GetExeDirectory(exeDir, 260)) {
      basePath_.clear();
      return true;
    }
    basePath_ = exeDir;
    return true;
  }

  size_t len = std::strlen(path);
  if (len + 1 >= 260) {
    return false;

  }

  basePath_ = path;

  const char separator = ChooseStormPathSeparator(basePath_.c_str());
  std::size_t length = basePath_.size();
  const char tail = length == 0 ? '\0' : basePath_[length - 1];
  if (length == 0 || tail != separator) {
    if (tail == '/' || tail == '\\') {
      --length;
    }
    if (length > 258) {
      length = 258;
    }
    basePath_.resize(length);
    basePath_.push_back(separator);
  }

  return true;
}

bool StreamingStorage::ResolveAbsolutePathFromBase(const char *source, char *resolved_path,
                                                   const int resolved_path_capacity) const {
  if (!source || !resolved_path || resolved_path_capacity <= 0) {
    return false;
  }

  std::array<char, 260> joined_path{};
  {
    std::lock_guard lock(streamingStateMutex_);
    openwow::core::JoinStormPathBounded(joined_path.data(),
                                        static_cast<int>(joined_path.size()),
                                        basePath_.c_str(), source);
  }

  return openwow::vfs::FileSystem_MakeAbsolutePath(joined_path.data(), resolved_path,
                                                   resolved_path_capacity);
}

StreamingEntry *StreamingStorage::LookupStreamingEntry(
    void *callback_table, const std::string *lookup_key, const char *source_path,
    const std::uint64_t logical_file_size, const char *hash, const std::uint32_t block_size) {
  if (lookup_key == nullptr) {
    return nullptr;
  }

  auto &storage = Instance();
  std::lock_guard lock(storage.streamingStateMutex_);

  const auto find_existing_entry = [&storage, lookup_key]() -> StreamingEntry * {
    const auto entry_it = storage.runtimeEntriesByLookupKey_.find(*lookup_key);
    if (entry_it != storage.runtimeEntriesByLookupKey_.end()) {
      return entry_it->second.get();
    }

    const auto parsed_entry_it = storage.entryIndicesByLookupPath_.find(*lookup_key);
    if (parsed_entry_it == storage.entryIndicesByLookupPath_.end() ||
        parsed_entry_it->second >= storage.entries_.size()) {
      return nullptr;
    }

    return &storage.entries_[parsed_entry_it->second];
  };

  const auto create_runtime_entry = [&storage, lookup_key, source_path, logical_file_size, hash,
                                     block_size]() -> StreamingEntry * {
    if (block_size == 0) {
      return nullptr;
    }

    const std::uint64_t block_count_u64 =
        logical_file_size == 0 ? 0 : (logical_file_size + block_size - 1) / block_size;
    if (block_count_u64 > std::numeric_limits<std::uint32_t>::max()) {
      return nullptr;
    }

    auto entry = std::make_unique<StreamingEntry>();
    entry->filename = source_path != nullptr ? source_path : "";
    entry->hash = hash != nullptr ? hash : "";
    entry->flags = 0;
    entry->fileSize = logical_file_size;
    entry->storageBytesUsed = 0;
    entry->blockSize = block_size;
    entry->blockCount = static_cast<std::uint32_t>(block_count_u64);
    entry->bypassPartValidation = true;
    if (!storage.simpleMode_) {
      entry->legacyManifestRecordOffset =
          storage.ComputeNextLegacyManifestRecordOffsetLocked(*lookup_key);
    }
    entry->blocks.resize(entry->blockCount);

    if (!storage.simpleMode_) {
      std::uint64_t logical_offset = 0;
      std::uint64_t remaining_size = logical_file_size;
      for (auto &block : entry->blocks) {
        const std::uint32_t logical_span =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining_size, block_size));
        SplitQwordToDwords(logical_offset, block.offset_lo, block.offset_hi);
        block.field14 = logical_span;
        logical_offset += block_size;
        remaining_size -= logical_span;
      }
    }

    auto [entry_it, inserted] =
        storage.runtimeEntriesByLookupKey_.insert_or_assign(*lookup_key, std::move(entry));
    (void)inserted;
    return entry_it->second.get();
  };

  if (source_path == nullptr) {
    return find_existing_entry();
  }

  std::array<char, 260> part_path{};
  storage.BuildVariantPath(part_path.data(), static_cast<int>(part_path.size()), source_path,
                           "part");

  if (StreamingEntry *existing_entry = find_existing_entry(); existing_entry != nullptr) {
    if (existing_entry->bypassPartValidation) {
      return existing_entry;
    }

    if ((existing_entry->flags & 1u) != 0u && AreAllStreamingEntryBlocksReady(*existing_entry)) {
      existing_entry->bypassPartValidation = true;
      return existing_entry;
    }

    openwow::vfs::FileStackPathMetadata metadata{};
    if (openwow::vfs::FileStack_QueryPathMetadata(callback_table, part_path.data(), &metadata) &&
        existing_entry->storageBytesUsed ==
            CombineLowHighDwords(metadata.file_size_low, metadata.file_size_high) &&
        existing_entry->fileSize == logical_file_size && hash != nullptr &&
        existing_entry->hash == hash) {
      return existing_entry;
    }

    (void)storage.EraseRuntimeEntryByLookupKeyLocked(*lookup_key);
  }

  if (!storage.simpleMode_) {
    (void)openwow::vfs::FileSystem_DeleteFile(part_path.data());
  }

  return create_runtime_entry();
}

bool StreamingStorage::EraseRuntimeEntryByLookupKeyLocked(const std::string_view lookup_key) {
  return runtimeEntriesByLookupKey_.erase(std::string(lookup_key)) != 0;
}

bool StreamingStorage::EraseRuntimeEntryBySourcePathLocked(const std::string_view source_path) {
  const std::string lookup_key = NormalizeStreamingManifestLookupPath(source_path);
  if (runtimeEntriesByLookupKey_.erase(lookup_key) != 0) {
    return true;
  }

  const auto entry_it = std::find_if(
      runtimeEntriesByLookupKey_.begin(), runtimeEntriesByLookupKey_.end(),
      [source_path](const auto &entry) {
        return entry.second != nullptr && entry.second->filename == source_path;
      });
  if (entry_it == runtimeEntriesByLookupKey_.end()) {
    return false;
  }

  runtimeEntriesByLookupKey_.erase(entry_it);
  return true;
}

bool StreamingStorage::ReinitPartFile(int fileHandle, void *entryData) {
  auto *state = static_cast<StreamingPartFileState *>(entryData);
  if (state == nullptr || state->blockSize == 0) {
    return false;
  }

  const auto block_count = ComputeStreamingPartBlockCount(*state);
  if (!block_count.has_value() ||
      !ResetStreamingPartRuntimeState(*state, *block_count)) {
    return false;
  }

  StreamingPartFileHeader header{};
  header.version = kStreamingPartFileHeaderVersion;
  SStrCopy(header.path.data(), state->headerPath.c_str(),
           header.path.size());
  header.persistedFlags = state->persistedFlags;
  header.logicalFileSize = state->logicalFileSize;
  header.blockSize = state->blockSize;
  const auto serialized_header = SerializeStreamingPartFileHeader(header);

  if (!WriteLooseFileExactAtOffset(fileHandle, serialized_header.data(), 0,
                                   static_cast<std::uint32_t>(serialized_header.size()))) {
    return false;
  }

  const std::uint64_t serialized_block_bytes_u64 =
      static_cast<std::uint64_t>(*block_count) * kStreamingPartFileSerializedBlockSize;
  if (serialized_block_bytes_u64 > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }

  std::vector<std::uint8_t> serialized_blocks;
  if (!TryResizeVector(&serialized_blocks,
                       static_cast<std::size_t>(serialized_block_bytes_u64))) {
    return false;
  }
  if (!serialized_blocks.empty() &&
      !WriteLooseFileExactAtOffset(fileHandle, serialized_blocks.data(), serialized_header.size(),
                                   static_cast<std::uint32_t>(serialized_block_bytes_u64))) {
    return false;
  }

  state->storageEnd = ComputeStreamingPartStorageEnd(*block_count, 0, state->blockSize);
  state->storageHeaderInitialized = true;
  state->storageNeedsRewrite = false;
  state->storageLayoutValidated = true;
  (void)openwow::vfs::IOUnitContainer_SetFileHandleSize(fileHandle, state->storageEnd, 0);
  return true;
}

bool StreamingStorage::OpenPartFile(int fileHandle, void *entryData) {
  auto *state = static_cast<StreamingPartFileState *>(entryData);
  if (state == nullptr || state->blockSize == 0) {
    return false;
  }

  if (state->storageLayoutValidated) {
    return true;
  }

  const std::uint64_t current_file_size = QueryLooseFileHandleSize(fileHandle);

  if (current_file_size < kStreamingPartFileHeaderSize) {
    return ReinitPartFile(fileHandle, entryData);
  }

  std::array<std::uint8_t, kStreamingPartFileHeaderSize> serialized_header{};
  if (!ReadLooseFileExactAtOffset(fileHandle, serialized_header.data(), 0,
                                  static_cast<std::uint32_t>(serialized_header.size()))) {
    return ReinitPartFile(fileHandle, entryData);
  }
  const StreamingPartFileHeader header =
      DeserializeStreamingPartFileHeader(serialized_header);

  if (header.blockSize == 0 || header.version != kStreamingPartFileHeaderVersion ||
      header.logicalFileSize != state->logicalFileSize ||
      StreamingPartHeaderPathMismatches(header.path, state->headerPath)) {
    return ReinitPartFile(fileHandle, entryData);
  }

  state->persistedFlags = header.persistedFlags;
  state->storageEnd = current_file_size;

  const auto block_count = ComputeStreamingPartBlockCount(*state);
  if (!block_count.has_value()) {
    return ReinitPartFile(fileHandle, entryData);
  }
  const std::uint64_t serialized_block_bytes_u64 =
      static_cast<std::uint64_t>(*block_count) * kStreamingPartFileSerializedBlockSize;
  if (serialized_block_bytes_u64 > std::numeric_limits<std::uint32_t>::max()) {
    return ReinitPartFile(fileHandle, entryData);
  }

  std::vector<std::uint8_t> serialized_blocks;
  if (!TryResizeVector(&serialized_blocks,
                       static_cast<std::size_t>(serialized_block_bytes_u64))) {
    return false;
  }
  if (!serialized_blocks.empty() &&
      !ReadLooseFileExactAtOffset(fileHandle, serialized_blocks.data(), serialized_header.size(),
                                  static_cast<std::uint32_t>(serialized_block_bytes_u64))) {
    return ReinitPartFile(fileHandle, entryData);
  }

  std::vector<StreamingPartBlockRecord> runtime_blocks;
  if (!TryResizeVector(&runtime_blocks, *block_count)) {
    return false;
  }

  std::uint32_t available_block_count = 0;
  for (std::uint32_t block_index = 0; block_index < *block_count; ++block_index) {
    const auto serialized_block = DeserializeStreamingPartFileBlock(
        serialized_blocks.data() +
        (static_cast<std::size_t>(block_index) * kStreamingPartFileSerializedBlockSize));
    auto &runtime_block = runtime_blocks[block_index];
    runtime_block.state = serialized_block.availabilityTag != 0 ? StreamingPartBlockState::Available
                                                                : StreamingPartBlockState::Missing;
    runtime_block.partFileOffset = serialized_block.partFileOffset;
    runtime_block.auxiliaryValue = serialized_block.auxiliaryValue;
    runtime_block.blockSize = state->blockSize;

    if (runtime_block.state == StreamingPartBlockState::Available) {
      if (runtime_block.partFileOffset > current_file_size) {
        return ReinitPartFile(fileHandle, entryData);
      }
      ++available_block_count;
    }
  }

  state->entry.SetBlockSize(state->blockSize);
  state->entry.SetRetired(false);
  state->entry.SetBlocks(std::move(runtime_blocks));

  const std::uint64_t expected_storage_end =
      ComputeStreamingPartStorageEnd(*block_count, available_block_count, state->blockSize);
  if (current_file_size > expected_storage_end) {
    (void)openwow::vfs::IOUnitContainer_SetFileHandleSize(fileHandle, expected_storage_end, 0);
    state->storageEnd = expected_storage_end;
  } else if (current_file_size < expected_storage_end) {
    return ReinitPartFile(fileHandle, entryData);
  }

  state->storageLayoutValidated = true;
  return true;
}

bool StreamingStorage::IsTrialMode() {
  return openwow::data::GetStreamingManifestState().trial_mode;
}

bool StreamingStorage::ConfigureBgPreloadSleep(int bandwidth) {
  if (bandwidth == 0)
    return true;

  int sleepMs = 25000000 / bandwidth;

  if (sleepMs < 10)
    sleepMs = 10;
  if (sleepMs > 1000)
    sleepMs = 1000;

  Instance().SetBgPreloadSleep(sleepMs);
  return true;
}

std::uint32_t StreamingStorage::GetBackgroundDownloadWorkerAliveMaskForTests() const {
  return backgroundDownloadWorkerAliveMask_.load(std::memory_order_acquire);
}

std::uint32_t StreamingStorage::GetBackgroundDownloadWorkerBusyMaskForTests() const {
  return backgroundDownloadWorkerBusyMask_.load(std::memory_order_acquire);
}

void StreamingStorage::SetBackgroundDownloadWorkerBusyForTests(const std::size_t slot_index,
                                                               const bool busy) {
  if (slot_index >= kBackgroundDownloadWorkerCount) {
    return;
  }

  const std::uint32_t slot_bit = BackgroundDownloadWorkerBit(slot_index);
  if (busy) {
    if ((backgroundDownloadWorkerAliveMask_.load(std::memory_order_acquire) & slot_bit) == 0) {
      return;
    }
    backgroundDownloadWorkerBusyMask_.fetch_or(slot_bit, std::memory_order_acq_rel);
    return;
  }

  backgroundDownloadWorkerBusyMask_.fetch_and(~slot_bit, std::memory_order_acq_rel);
}

std::uint64_t StreamingStorage::CreateBackgroundDownloadSource() {
  std::lock_guard lock(backgroundDownloadMutex_);
  const std::uint64_t source_id = nextBackgroundDownloadSourceId_++;
  backgroundDownloadSources_.emplace(source_id, std::make_shared<BackgroundDownloadSourceState>());
  return source_id;
}

void StreamingStorage::SetBackgroundDownloadSourceDispatchCallback(
    const std::uint64_t source_id, BackgroundDownloadTaskCallback on_dispatch) {
  std::shared_ptr<BackgroundDownloadSourceState> source;
  {
    std::lock_guard lock(backgroundDownloadMutex_);
    const auto it = backgroundDownloadSources_.find(source_id);
    if (it == backgroundDownloadSources_.end()) {
      return;
    }

    source = it->second;
  }

  std::lock_guard source_lock(source->mutex);
  if (source->closed) {
    return;
  }

  source->on_dispatch = std::move(on_dispatch);
}

void StreamingStorage::SetBackgroundDownloadSourceCompletionCallback(
    const std::uint64_t source_id, std::function<void()> on_complete) {
  SetBackgroundDownloadSourceDispatchCallback(
      source_id, [on_complete = std::move(on_complete)]() mutable {
        if (on_complete) {
          on_complete();
        }
        return BackgroundDownloadTaskDispatchResult::kComplete;
      });
}

void StreamingStorage::CloseBackgroundDownloadSource(const std::uint64_t source_id) {
  std::shared_ptr<BackgroundDownloadSourceState> source;
  {
    std::lock_guard lock(backgroundDownloadMutex_);
    const auto it = backgroundDownloadSources_.find(source_id);
    if (it == backgroundDownloadSources_.end()) {
      return;
    }

    source = it->second;
    backgroundDownloadSources_.erase(it);
  }

  std::lock_guard source_lock(source->mutex);
  source->closed = true;
}

bool StreamingStorage::QueueBackgroundDownloadTask(const std::size_t queue_index,
                                                   const std::uint64_t source_id) {
  if (queue_index >= kBackgroundDownloadQueueCount) {
    return false;
  }

  std::shared_ptr<BackgroundDownloadSourceState> source;
  {
    std::lock_guard lock(backgroundDownloadMutex_);
    const auto it = backgroundDownloadSources_.find(source_id);
    if (it == backgroundDownloadSources_.end()) {
      return false;
    }

    source = it->second;
    backgroundDownloadQueues_[queue_index].push_back(BackgroundDownloadTaskRecord{
        .source_id = source_id,
        .source = source,
    });
  }

  for (auto &worker : backgroundDownloadWorkers_) {
    worker.wait_cv.notify_all();
  }
  backgroundDownloadTaskIdleCv_.notify_all();
  return true;
}

StreamingStorage::BackgroundDownloadQueueBarrierResult
StreamingStorage::WaitForBackgroundDownloadQueueBacklogUntil(
    const std::size_t queue_index, const std::function<bool()> &interrupted) {
  if (queue_index >= kBackgroundDownloadQueueCount) {
    return BackgroundDownloadQueueBarrierResult::kNoQueuedTasks;
  }

  const auto barrier_state = std::make_shared<BackgroundDownloadQueueBarrierState>();

  {
    std::lock_guard lock(backgroundDownloadMutex_);
    if (backgroundDownloadQueues_[queue_index].empty()) {
      return BackgroundDownloadQueueBarrierResult::kNoQueuedTasks;
    }

    backgroundDownloadQueues_[queue_index].push_back(BackgroundDownloadTaskRecord{
        .is_queue_barrier = true,
        .queue_barrier = barrier_state,
    });
  }

  for (auto &worker : backgroundDownloadWorkers_) {
    worker.wait_cv.notify_all();
  }
  backgroundDownloadTaskIdleCv_.notify_all();

  std::unique_lock lock(barrier_state->mutex);
  barrier_state->completed_cv.wait(lock, [&]() {
    return barrier_state->completed || (interrupted && interrupted());
  });

  if (barrier_state->completed) {
    return BackgroundDownloadQueueBarrierResult::kCompleted;
  }

  return BackgroundDownloadQueueBarrierResult::kInterrupted;
}

void StreamingStorage::WaitForBackgroundDownloadTasksIdle() {
  std::unique_lock lock(backgroundDownloadMutex_);
  backgroundDownloadTaskIdleCv_.wait(lock, [this]() {
    if (backgroundDownloadWorkerBusyMask_.load(std::memory_order_acquire) != 0) {
      return false;
    }

    return !HasPendingBackgroundDownloadTasksLocked();
  });
}

bool StreamingStorage::WaitForBackgroundDownloadTasksIdleUntil(
    const std::function<bool()> &interrupted) {
  std::unique_lock lock(backgroundDownloadMutex_);
  backgroundDownloadTaskIdleCv_.wait(lock, [this, &interrupted]() {
    if (interrupted && interrupted()) {
      return true;
    }

    if (backgroundDownloadWorkerBusyMask_.load(std::memory_order_acquire) != 0) {
      return false;
    }

    return !HasPendingBackgroundDownloadTasksLocked();
  });

  return !interrupted || !interrupted();
}

void StreamingStorage::NotifyBackgroundDownloadStateWaiters() {
  backgroundDownloadTaskIdleCv_.notify_all();
}

std::uint64_t StreamingStorage::CreateBackgroundDownloadSourceForTests() {
  return CreateBackgroundDownloadSource();
}

void StreamingStorage::SetBackgroundDownloadSourceDispatchCallbackForTests(
    const std::uint64_t source_id, BackgroundDownloadTaskCallback on_dispatch) {
  SetBackgroundDownloadSourceDispatchCallback(source_id, std::move(on_dispatch));
}

void StreamingStorage::CloseBackgroundDownloadSourceForTests(const std::uint64_t source_id) {
  CloseBackgroundDownloadSource(source_id);
}

bool StreamingStorage::RequeueBackgroundDownloadTaskToFrontForTests(const std::size_t queue_index,
                                                                    const std::uint64_t source_id) {
  if (queue_index >= kBackgroundDownloadQueueCount) {
    return false;
  }

  std::shared_ptr<BackgroundDownloadSourceState> source;
  {
    std::lock_guard lock(backgroundDownloadMutex_);
    const auto it = backgroundDownloadSources_.find(source_id);
    if (it == backgroundDownloadSources_.end()) {
      return false;
    }

    source = it->second;
    RequeueBackgroundDownloadTaskToFrontLocked(queue_index, BackgroundDownloadTaskRecord{
                                                                .source_id = source_id,
                                                                .source = source,
                                                            });
  }

  return true;
}

bool StreamingStorage::QueueBackgroundDownloadTaskForTests(const std::size_t queue_index,
                                                           const std::uint64_t source_id) {
  return QueueBackgroundDownloadTask(queue_index, source_id);
}

bool StreamingStorage::DequeueBackgroundDownloadTaskForTests(const std::size_t queue_index,
                                                             std::uint64_t &source_id) {
  BackgroundDownloadTaskRecord task;
  {
    std::lock_guard lock(backgroundDownloadMutex_);
    if (!TryDequeueBackgroundDownloadTaskLocked(queue_index, task)) {
      return false;
    }
  }

  source_id = task.source_id;
  return true;
}

std::size_t
StreamingStorage::SelectNextBackgroundDownloadQueueForTests(const std::size_t worker_slot,
                                                            std::uint64_t &source_id) {
  BackgroundDownloadTaskRecord task;
  std::lock_guard lock(backgroundDownloadMutex_);
  const std::size_t queue_index = SelectNextBackgroundDownloadQueueLocked(worker_slot, task);
  source_id = task.source_id;
  return queue_index;
}

bool StreamingStorage::WaitForBackgroundDownloadTasksIdleForTests(const std::uint32_t timeout_ms) {
  std::unique_lock lock(backgroundDownloadMutex_);
  return backgroundDownloadTaskIdleCv_.wait_for(
      lock, std::chrono::milliseconds(timeout_ms), [this]() {
        if (backgroundDownloadWorkerBusyMask_.load(std::memory_order_acquire) != 0) {
          return false;
        }

        return !HasPendingBackgroundDownloadTasksLocked();
      });
}

std::size_t StreamingStorage::GetBackgroundDownloadDequeueCountForTests() const {
  std::lock_guard lock(backgroundDownloadMutex_);
  return backgroundDownloadDequeueCount_;
}

std::size_t StreamingStorage::GetBackgroundDownloadScheduleMissCountForTests() const {
  std::lock_guard lock(backgroundDownloadMutex_);
  return backgroundDownloadScheduleMissCount_;
}

std::size_t StreamingStorage::GetBackgroundDownloadCompletedTaskCountForTests() const {
  std::lock_guard lock(backgroundDownloadMutex_);
  return backgroundDownloadCompletedTaskCount_;
}

std::size_t StreamingStorage::GetBackgroundDownloadSkippedTaskCountForTests() const {
  std::lock_guard lock(backgroundDownloadMutex_);
  return backgroundDownloadSkippedTaskCount_;
}

void StreamingStorage::SetStorageReadyForTests(const bool ready) {
  storageReady_ = ready;
}

void StreamingStorage::SetPreloadPartialProgressForTests(const char *path,
                                                         const std::uint64_t loaded_bytes) {
  RecordPreloadPartialProgress(path, loaded_bytes);
}

bool StreamingStorage::EraseRuntimeEntryBySourcePathForTests(const char *source_path) {
  std::lock_guard lock(streamingStateMutex_);
  return EraseRuntimeEntryBySourcePathLocked(source_path != nullptr ? source_path : "");
}

void StreamingStorage::QueuePendingPartWriteForTests(StreamingPartPendingWrite pending_write) {
  std::lock_guard lock(streamingPartMutex_);
  pendingPartWrites_.push_back(MakeQueuedPendingPartWrite(std::move(pending_write)));
}

void StreamingStorage::SetPendingPartWritesForTests(
    std::vector<StreamingPartPendingWrite> pending_writes) {
  std::lock_guard lock(streamingPartMutex_);
  pendingPartWrites_.clear();
  for (auto &pending_write : pending_writes) {
    pendingPartWrites_.push_back(MakeQueuedPendingPartWrite(std::move(pending_write)));
  }
}

std::size_t StreamingStorage::GetPendingPartWriteCountForTests() const {
  std::lock_guard lock(streamingPartMutex_);
  return pendingPartWrites_.size();
}

void StreamingStorage::QueuePendingPartWriteTaskForTests(StreamingPartWriteTask task) {
  if (task.partFileState != nullptr) {
    task.pendingWrite.owner = &task.partFileState->entry;
  }

  QueuedStreamingPartWriteTask queued_task = MakeQueuedStreamingPartWriteTask(std::move(task));

  {
    std::lock_guard lock(streamingPartMutex_);
    pendingPartWrites_.push_back(queued_task.pendingWrite);
  }
  {
    std::lock_guard lock(workerMutex_);
    pendingPartWriteTasks_.push_back(std::move(queued_task));
  }

  SignalWorker();
}

std::size_t StreamingStorage::DrainPendingPartWriteTasksForTests() {
  std::size_t drained = 0;
  QueuedStreamingPartWriteTask task;
  while (TryPopPendingPartWriteTask(task)) {
    (void)FlushPendingPartWriteTask(task);
    ++drained;
  }
  return drained;
}

std::size_t StreamingStorage::GetPendingPartWriteTaskCountForTests() {
  std::lock_guard lock(workerMutex_);
  return pendingPartWriteTasks_.size();
}

std::uint32_t StreamingStorage::GetStreamingStateValueForTests() const {
  std::lock_guard lock(streamingStateMutex_);
  return streamingStateValue_;
}

std::string StreamingStorage::GetBasePathForTests() const {
  std::lock_guard lock(streamingStateMutex_);
  return basePath_;
}

std::size_t StreamingStorage::GetIdleSharedSFileTaskWorkerCountForTests() const {
  std::lock_guard lock(sharedSFileTaskWorkerMutex_);
  return sharedSFileTaskWorkers_.size();
}

void StreamingStorage::ResetForTests() {
  Shutdown();

  bgPreloadSleep_ = 100;
  bgPreloadSleepRestoreValue_ = 0;
  {
    std::lock_guard lock(variantPathMutex_);
    variantPathSuffix_.clear();
    variantPathRetryOrdinal_ = 0;
  }
  basePath_.clear();
  {
    std::lock_guard lock(backgroundDownloadThrottleMutex_);
    backgroundDownloadThrottleSignaled_ = false;
  }
  backgroundDownloadTaskIdleCv_.notify_all();
}

}

#include "openwow/vfs/retail/sound_cache/sound_cache.h"
#include "openwow/vfs/retail/sfile_archive.h"

#include "openwow/audio/codecs/ogg/ogg_decompress.h"
#include "openwow/core/storm_containers.h"
#include "openwow/core/storm_thread.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/platform/adapters/win32/win32_compat.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/vfs/adapters/mpq/mpq_archive.h"
#include "openwow/vfs/retail/archive_registry.h"
#include "openwow/vfs/retail/runtime_file.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <new>
#include <utility>

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
#include <StormLib.h>
#endif

namespace openwow::vfs {
namespace {

constexpr std::size_t kInitialArchiveHashTableEntries = 0x10000u;
using DecodeBuffer = SoundCacheDecodeBuffer;

using DecodeCallback = unsigned char (*)(void *source_data, int source_size,
                                         void *decode_buffer);

bool ResizeDecodeBuffer(DecodeBuffer *buffer,
                        std::unique_ptr<std::uint8_t[]> *storage,
                        const std::size_t required_size) {
  if (!buffer || !storage ||
      required_size > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  if (required_size <= kSoundCacheInlineBufferSize) {
    storage->reset();
    buffer->data = buffer->inline_storage;
    return true;
  }

  std::unique_ptr<std::uint8_t[]> replacement(
      new (std::nothrow) std::uint8_t[required_size]);
  if (!replacement) {
    return false;
  }
  buffer->data = replacement.get();
  *storage = std::move(replacement);
  return true;
}

bool Decode(void *raw_callback, const std::uint8_t *source_data,
            const std::uint32_t source_size, DecodeBuffer *decode_buffer,
            std::unique_ptr<std::uint8_t[]> *decode_storage) {
  if (!raw_callback || !decode_buffer || !decode_storage) {
    return false;
  }
  if (raw_callback == reinterpret_cast<void *>(openwow::audio::OggVorbis_DecodeToWAV)) {
    std::vector<std::uint8_t> decoded_wav;
    if (!openwow::audio::OggVorbis_DecodeToWAV(source_data, source_size, decoded_wav) ||
        !ResizeDecodeBuffer(decode_buffer, decode_storage, decoded_wav.size())) {
      return false;
    }
    if (!decoded_wav.empty()) {
      std::memcpy(decode_buffer->data, decoded_wav.data(), decoded_wav.size());
    }
    decode_buffer->size = static_cast<std::uint32_t>(decoded_wav.size());
    return true;
  }

  const auto callback = reinterpret_cast<DecodeCallback>(raw_callback);
  return callback(const_cast<std::uint8_t *>(source_data), static_cast<int>(source_size),
                  decode_buffer) != 0;
}

class FilenameQueue {
public:
  static constexpr std::size_t kMaxSlotCount = 0x09249249u;

  void Enqueue(const char *filename) {
    EnsureCapacity(1);
    auto &slot = slots_[PhysicalIndex(count_)];
    if (!slot) {
      slot = std::make_unique<std::string>();
    }
    slot->assign(filename ? filename : "");
    ++count_;
  }

  void Clear() {
    slots_.clear();
    head_ = 0;
    count_ = 0;
  }

  std::vector<std::string> Snapshot() const {
    std::vector<std::string> pending;
    pending.reserve(count_);
    for (std::size_t index = 0; index < count_; ++index) {
      const auto &slot = slots_[PhysicalIndex(index)];
      pending.push_back(slot ? *slot : std::string());
    }
    return pending;
  }

  std::size_t capacity() const { return slots_.size(); }
  std::size_t size() const { return count_; }

  std::string Front() const {
    if (count_ == 0 || slots_.empty()) {
      return {};
    }
    const auto &slot = slots_[head_];
    return slot ? *slot : std::string();
  }

  void PopFront() {
    if (count_ == 0 || slots_.empty()) {
      return;
    }
    if (auto &slot = slots_[head_]) {
      slot->clear();
    }
    if (++head_ >= slots_.size()) {
      head_ = 0;
    }
    if (--count_ == 0) {
      head_ = 0;
    }
  }

private:
  std::size_t PhysicalIndex(const std::size_t logical_index) const {
    if (slots_.empty()) {
      return 0;
    }
    const std::size_t index = head_ + logical_index;
    return index >= slots_.size() ? index - slots_.size() : index;
  }

  void EnsureCapacity(const std::size_t requested_slots) {
    if (slots_.size() > count_ + requested_slots) {
      return;
    }
    const std::size_t current_count = slots_.size();
    if (kMaxSlotCount - current_count < requested_slots) {
      openwow::core::ThrowDequeTooLongLengthError();
    }
    std::size_t growth = requested_slots;
    std::size_t suggested_growth = current_count / 2;
    if (suggested_growth < 8) {
      suggested_growth = 8;
    }
    if (requested_slots < suggested_growth &&
        current_count <= kMaxSlotCount - suggested_growth) {
      growth = suggested_growth;
    }
    std::vector<std::unique_ptr<std::string>> grown(current_count + growth);
    for (std::size_t index = 0; index < count_; ++index) {
      grown[index] = std::move(slots_[PhysicalIndex(index)]);
    }
    slots_.swap(grown);
    head_ = 0;
  }

  std::vector<std::unique_ptr<std::string>> slots_;
  std::size_t head_{0};
  std::size_t count_{0};
};

std::filesystem::path ResolveArchivePath() {
  const auto &startup = openwow::data::GetStartupFileSystemState();
  std::filesystem::path path;
  if (!startup.executable_base_path.empty()) {
    path = ToNativePath(startup.executable_base_path.c_str());
  }
  path /= "Data";
  path /= "SoundCache.MPQ";
  path.make_preferred();
  return path;
}

bool OpenOrCreateArchive(const std::filesystem::path &path, void **out_archive) {
  if (!out_archive) {
    return false;
  }
  *out_archive = nullptr;
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  void *archive = nullptr;
  const std::string native_path = path.string();
  if (mpq::OpenStormArchive(native_path.c_str(), 100, 0, &archive)) {
    *out_archive = archive;
    return true;
  }
  std::error_code error;
  if (const auto parent = path.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(parent, error);
  }
  HANDLE created_archive = nullptr;
  if (SFileCreateArchive(native_path.c_str(), MPQ_CREATE_ARCHIVE_V1,
                         kInitialArchiveHashTableEntries, &created_archive)) {
    *out_archive = created_archive;
    return true;
  }
#else
  (void)path;
#endif
  return false;
}

void ReleaseArchive(void *archive) {
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (archive) {
    (void)SFileFlushArchive(static_cast<HANDLE>(archive));
    (void)SFileCloseArchive(static_cast<HANDLE>(archive));
  }
#else
  (void)archive;
#endif
}

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
class ArchiveFile {
public:
  ~ArchiveFile() {
    if (handle_) {
      SFileCloseFile(handle_);
    }
  }
  HANDLE *out() { return &handle_; }
  HANDLE get() const { return handle_; }
  bool Finish() {
    if (!handle_ || SFileFinishFile(handle_) == 0) {
      return false;
    }
    handle_ = nullptr;
    return true;
  }

private:
  HANDLE handle_ = nullptr;
};
#endif

bool WriteArchiveFile(void *archive, const char *filename, const DecodeBuffer &buffer) {
  if (!archive || !filename) {
    return false;
  }
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  ArchiveFile file;
  if (!SFileCreateFile(static_cast<HANDLE>(archive), filename, 0, buffer.size, 0, 0,
                       file.out())) {
    return false;
  }
  if (buffer.size != 0 &&
      (!buffer.data || SFileWriteFile(file.get(), buffer.data, buffer.size, 0) == 0)) {
    return false;
  }
  return file.Finish();
#else
  (void)buffer;
  return false;
#endif
}

}

class SoundCache::Impl {
public:
  explicit Impl(ArchiveRegistry &archive_registry) : archive_registry_(archive_registry) {}

  ~Impl() { Shutdown(); }

  bool Initialize(void *raw_decode_callback, FileReader reader) {
    std::lock_guard lock(lifecycle_mutex_);
    if (initialized_) {
      return true;
    }

    archive_path_ = ResolveArchivePath();
    void *archive = nullptr;
    if (!OpenOrCreateArchive(archive_path_, &archive)) {
      return false;
    }
    std::unique_ptr<DecodeBuffer> buffer(new (std::nothrow) DecodeBuffer);
    std::unique_ptr<openwow::platform::StormEvent> queue_event(
        new (std::nothrow) openwow::platform::StormEvent);
    std::unique_ptr<openwow::platform::StormCriticalSection> queue_cs(
        new (std::nothrow) openwow::platform::StormCriticalSection);
    std::unique_ptr<openwow::platform::StormCriticalSection> archive_cs(
        new (std::nothrow) openwow::platform::StormCriticalSection);
    if (!buffer || !queue_event || !queue_cs || !archive_cs) {
      ReleaseArchive(archive);
      return false;
    }
    buffer->data = buffer->inline_storage;
    queue_cs->Initialize();
    archive_cs->Initialize();

    archive_ = archive;
    decode_buffer_ = std::move(buffer);
    decode_callback_ = raw_decode_callback;
    file_reader_ = std::move(reader);
    shutdown_requested_.store(false, std::memory_order_release);
    worker_done_.store(false, std::memory_order_release);
    queue_event_ = std::move(queue_event);
    queue_cs_ = std::move(queue_cs);
    archive_cs_ = std::move(archive_cs);
    initialized_ = true;

    worker_ = openwow::core::StormThread::Instance().Create(
        [this](void *) {
          WorkerMain();
          return 0;
        },
        nullptr, "Sound Cache Worker");
    if (!worker_) {
      worker_done_.store(true, std::memory_order_release);
    }
    return true;
  }

  bool Shutdown() {
    std::unique_lock lock(lifecycle_mutex_);
    if (!initialized_) {
      return false;
    }
    shutdown_requested_.store(true, std::memory_order_release);
    if (queue_event_) {
      queue_event_->Set();
    }
    if (worker_) {
      (void)worker_->WaitForCompletion();
      worker_.reset();
    }

    queue_event_.reset();
    if (queue_cs_) {
      queue_cs_->Delete();
      queue_cs_.reset();
    }
    if (archive_cs_) {
      archive_cs_->Delete();
      archive_cs_.reset();
    }
    ReleaseArchive(archive_);
    archive_ = nullptr;
    decode_storage_.reset();
    decode_buffer_.reset();
    decode_callback_ = nullptr;
    file_reader_ = {};
    initialized_ = false;
    return true;
  }

  int Enqueue(const char *filename) {
    std::lock_guard lock(lifecycle_mutex_);
    return EnqueueLocked(filename);
  }

  bool OpenCachedOrEnqueue(const char *filename,
                           std::shared_ptr<RuntimeFile> *out_runtime_file) {
    std::lock_guard lock(lifecycle_mutex_);
    if (!initialized_) {
      return false;
    }
    auto runtime_file = std::make_shared<RuntimeFile>();
    runtime_file->handle.type = 3;
    runtime_file->logical_path = filename;
    bool opened = false;
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
    {
      ArchiveLock archive_lock(*this);
      opened = archive_ && runtime_file->OpenArchive(archive_registry_, archive_, filename);
    }
#endif
    if (!opened) {
      (void)EnqueueLocked(filename);
    } else if (out_runtime_file) {
      *out_runtime_file = std::move(runtime_file);
    }
    return opened;
  }

  void ResetQueueForTests() {
    QueueLock lock(*this);
    queue_.Clear();
    queue_signal_count_.store(0, std::memory_order_release);
  }

  std::vector<std::string> SnapshotQueueForTests() const {
    QueueLock lock(*this);
    return queue_.Snapshot();
  }

  std::size_t QueueCapacityForTests() const {
    QueueLock lock(*this);
    return queue_.capacity();
  }

  std::size_t QueueSignalCountForTests() const {
    return queue_signal_count_.load(std::memory_order_acquire);
  }

  void ResetForTests() {
    (void)Shutdown();
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    decode_callback_ = nullptr;
    file_reader_ = {};
    archive_ = nullptr;
    decode_storage_.reset();
    decode_buffer_.reset();
    queue_event_.reset();
    queue_cs_.reset();
    archive_cs_.reset();
    shutdown_requested_.store(false, std::memory_order_release);
    worker_done_.store(false, std::memory_order_release);
    initialized_ = false;
    QueueLock queue_lock(*this);
    queue_.Clear();
    queue_signal_count_.store(0, std::memory_order_release);
    archive_path_.clear();
  }

private:
  class QueueLock {
  public:
    explicit QueueLock(const Impl &owner) : owner_(owner) {
      if (owner_.queue_cs_) {
        owner_.queue_cs_->Enter();
      } else {
        owner_.fallback_queue_mutex_.lock();
      }
    }
    ~QueueLock() {
      if (owner_.queue_cs_) {
        owner_.queue_cs_->Leave();
      } else {
        owner_.fallback_queue_mutex_.unlock();
      }
    }

  private:
    const Impl &owner_;
  };

  class ArchiveLock {
  public:
    explicit ArchiveLock(Impl &owner) : critical_section_(owner.archive_cs_.get()) {
      if (critical_section_) {
        critical_section_->Enter();
      }
    }
    ~ArchiveLock() {
      if (critical_section_) {
        critical_section_->Leave();
      }
    }

  private:
    openwow::platform::StormCriticalSection *critical_section_;
  };

  int EnqueueLocked(const char *filename) {
    if (!filename) {
      return 0;
    }
    {
      QueueLock lock(*this);
      queue_.Enqueue(filename);
    }
    queue_signal_count_.fetch_add(1, std::memory_order_acq_rel);
    if (queue_event_) {
      queue_event_->Set();
    }
    return 1;
  }

  int Process(const std::string &filename) {
    if (!decode_callback_ || !decode_buffer_ || !archive_ || !file_reader_) {
      return 0;
    }
    std::vector<std::uint8_t> source_bytes;
    if (!file_reader_(filename.c_str(), &source_bytes) ||
        source_bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      return 0;
    }
    if (!Decode(decode_callback_, source_bytes.empty() ? nullptr : source_bytes.data(),
                 static_cast<std::uint32_t>(source_bytes.size()), decode_buffer_.get(),
                 &decode_storage_)) {
      return 0;
    }
    ArchiveLock lock(*this);
    return WriteArchiveFile(archive_, filename.c_str(), *decode_buffer_) ? 1 : 0;
  }

  void WorkerMain() {
    auto *event = queue_event_.get();
    if (!event) {
      worker_done_.store(true, std::memory_order_release);
      return;
    }
    while (!shutdown_requested_.load(std::memory_order_acquire)) {
      event->Wait();
      std::size_t pending_count = 0;
      {
        QueueLock lock(*this);
        pending_count = queue_.size();
      }
      while (pending_count > 0) {
        std::string filename;
        {
          QueueLock lock(*this);
          filename = queue_.Front();
        }
        (void)Process(filename);
        {
          QueueLock lock(*this);
          queue_.PopFront();
          pending_count = queue_.size();
        }
        openwow::platform::StormSleep(500);
      }
    }
    worker_done_.store(true, std::memory_order_release);
  }

  ArchiveRegistry &archive_registry_;
  mutable std::mutex lifecycle_mutex_;
  mutable std::mutex fallback_queue_mutex_;
  FilenameQueue queue_;
  std::atomic<std::size_t> queue_signal_count_{0};
  std::filesystem::path archive_path_;
  std::unique_ptr<openwow::platform::StormEvent> queue_event_;
  std::unique_ptr<openwow::platform::StormCriticalSection> queue_cs_;
  std::unique_ptr<openwow::platform::StormCriticalSection> archive_cs_;
  std::shared_ptr<openwow::core::StormThreadBlock> worker_;
  FileReader file_reader_;
  void *decode_callback_ = nullptr;
  std::unique_ptr<DecodeBuffer> decode_buffer_;
  std::unique_ptr<std::uint8_t[]> decode_storage_;
  void *archive_ = nullptr;
  std::atomic<bool> shutdown_requested_{false};
  std::atomic<bool> worker_done_{false};
  bool initialized_ = false;
};

SoundCache::SoundCache(ArchiveRegistry &archive_registry)
    : impl_(std::make_unique<Impl>(archive_registry)) {}
SoundCache::~SoundCache() = default;
bool SoundCache::Initialize(void *decode_callback, FileReader file_reader) {
  return impl_->Initialize(decode_callback, std::move(file_reader));
}
bool SoundCache::Shutdown() { return impl_->Shutdown(); }
int SoundCache::Enqueue(const char *filename) { return impl_->Enqueue(filename); }
bool SoundCache::OpenCachedOrEnqueue(const char *filename,
                                     std::shared_ptr<RuntimeFile> *out_runtime_file) {
  return impl_->OpenCachedOrEnqueue(filename, out_runtime_file);
}
void SoundCache::ResetQueueForTests() { impl_->ResetQueueForTests(); }
std::vector<std::string> SoundCache::SnapshotQueueForTests() const {
  return impl_->SnapshotQueueForTests();
}
std::size_t SoundCache::QueueCapacityForTests() const { return impl_->QueueCapacityForTests(); }
std::size_t SoundCache::QueueSignalCountForTests() const {
  return impl_->QueueSignalCountForTests();
}
void SoundCache::ResetForTests() { impl_->ResetForTests(); }

SoundCache &RetailSoundCache() {
  static SoundCache cache(RetailArchiveRegistry());
  return cache;
}

bool SFile2_InitSoundCache(void *decode_callback) {
  return RetailSoundCache().Initialize(decode_callback, ReadRetailVfsFileBytes);
}
int SFileOpenFile_EnqueueSoundCache(const char *filename) {
  return RetailSoundCache().Enqueue(filename);
}
bool SoundCache_Shutdown() { return RetailSoundCache().Shutdown(); }
void ResetSoundCacheQueueForTests() { RetailSoundCache().ResetQueueForTests(); }
std::vector<std::string> SnapshotSoundCacheQueueForTests() {
  return RetailSoundCache().SnapshotQueueForTests();
}
std::size_t GetSoundCacheQueueCapacityForTests() {
  return RetailSoundCache().QueueCapacityForTests();
}
std::size_t GetSoundCacheQueueSignalCountForTests() {
  return RetailSoundCache().QueueSignalCountForTests();
}
void ResetSoundCacheRuntimeForTests() { RetailSoundCache().ResetForTests(); }

}

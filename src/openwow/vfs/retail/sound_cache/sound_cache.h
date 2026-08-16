#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace openwow::vfs {

class ArchiveRegistry;
class RuntimeFile;

inline constexpr std::size_t kSoundCacheInlineBufferSize = 0x400000u;

struct SoundCacheDecodeBuffer {
  std::uint32_t size{0};
  std::uint8_t *data{nullptr};
  std::uint8_t inline_storage[kSoundCacheInlineBufferSize]{};
};

static_assert(sizeof(void *) != 4 || sizeof(SoundCacheDecodeBuffer) == 0x400008u,
              "Sound cache decode buffer must preserve the i386 callback ABI");
static_assert(sizeof(void *) != 4 || offsetof(SoundCacheDecodeBuffer, size) == 0u);
static_assert(sizeof(void *) != 4 || offsetof(SoundCacheDecodeBuffer, data) == 4u);
static_assert(sizeof(void *) != 4 ||
              offsetof(SoundCacheDecodeBuffer, inline_storage) == 8u);

class SoundCache {
public:
  using FileReader =
      std::function<bool(const char *path, std::vector<std::uint8_t> *out_bytes)>;

  explicit SoundCache(ArchiveRegistry &archive_registry);
  ~SoundCache();

  SoundCache(const SoundCache &) = delete;
  SoundCache &operator=(const SoundCache &) = delete;

  bool Initialize(void *decode_callback, FileReader file_reader);
  bool Shutdown();
  int Enqueue(const char *filename);
  bool OpenCachedOrEnqueue(const char *filename,
                           std::shared_ptr<RuntimeFile> *out_runtime_file);

  void ResetQueueForTests();
  std::vector<std::string> SnapshotQueueForTests() const;
  std::size_t QueueCapacityForTests() const;
  std::size_t QueueSignalCountForTests() const;
  void ResetForTests();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

SoundCache &RetailSoundCache();

bool SFile2_InitSoundCache(void *decode_callback);
int SFileOpenFile_EnqueueSoundCache(const char *filename);
bool SoundCache_Shutdown();

void ResetSoundCacheQueueForTests();
std::vector<std::string> SnapshotSoundCacheQueueForTests();
std::size_t GetSoundCacheQueueCapacityForTests();
std::size_t GetSoundCacheQueueSignalCountForTests();
void ResetSoundCacheRuntimeForTests();

}

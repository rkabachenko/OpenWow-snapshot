#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace openwow::core {
struct StreamingEntry;
}

namespace openwow::vfs {

struct StormArchiveEventDispatcherCompat {
  void *vtable = nullptr;
  void *callback_table = nullptr;
};

class StreamingPartFileHandle {
public:
  struct ContextLink {
    enum class ReleaseDisposition : std::uint8_t {
      NoAttachment = 0,
      Retained = 1,
      FinalRelease = 2,
    };

    void AttachStreamingEntry(openwow::core::StreamingEntry *entry,
                              std::uint32_t retain_count = 1u) noexcept;
    void DetachStreamingEntry() noexcept;
    [[nodiscard]] openwow::core::StreamingEntry *attached_streaming_entry() const noexcept;
    [[nodiscard]] std::uint32_t attached_streaming_entry_refcount() const noexcept;
    ReleaseDisposition ReleaseStreamingEntryReference(
        openwow::core::StreamingEntry *&released_entry) noexcept;

    void *active_stream = nullptr;
    std::string normalized_lookup_key;
    std::array<std::byte, 0x38> provider_state{};

  private:
    mutable std::mutex attachment_mutex_{};
    openwow::core::StreamingEntry *attached_streaming_entry_ = nullptr;
    std::uint32_t attached_streaming_entry_refcount_ = 0;
  };

  static constexpr std::size_t kStreamContextLinkOffset = 72;
  static constexpr std::size_t kReservedPrefixBytes =
      kStreamContextLinkOffset - sizeof(void *) - sizeof(std::uint32_t);

  struct RawHandle {
    void *provider_handle = nullptr;
    std::uint32_t open_flags = 0;
    std::array<std::byte, kReservedPrefixBytes> reserved{};
    ContextLink *stream_context_link = nullptr;
    const char *source_path = nullptr;
  };

  explicit StreamingPartFileHandle(const char *source_path);
  StreamingPartFileHandle(const StreamingPartFileHandle &) = delete;
  StreamingPartFileHandle &operator=(const StreamingPartFileHandle &) = delete;

  void *raw_handle() noexcept { return &raw_handle_; }
  const void *raw_handle() const noexcept { return &raw_handle_; }
  void *provider_handle() const noexcept { return raw_handle_.provider_handle; }
  void set_provider_handle(void *value) noexcept { raw_handle_.provider_handle = value; }
  std::uint32_t open_flags() const noexcept { return raw_handle_.open_flags; }
  void set_open_flags(std::uint32_t value) noexcept { raw_handle_.open_flags = value; }
  const char *source_path() const noexcept { return raw_handle_.source_path; }
  ContextLink &context_link() noexcept { return inline_link_; }
  const ContextLink &context_link() const noexcept { return inline_link_; }

private:
  RawHandle raw_handle_{};
  ContextLink inline_link_{};
  std::string source_path_storage_{};
};

static_assert(offsetof(StreamingPartFileHandle::RawHandle, stream_context_link) ==
              StreamingPartFileHandle::kStreamContextLinkOffset);
static_assert(sizeof(StreamingPartFileHandle::ContextLink::provider_state) == 0x38);

struct ArchiveEventDispatchTimingHooks {
  std::function<std::int64_t()> now_ns;
  std::function<void(std::uint32_t)> sleep_ms;
  std::int32_t pre_sleep_milliseconds{-1};
};

bool Storm_DispatchArchiveEvent(void *self, std::uint32_t *event_words);
bool Storm_DispatchArchiveEventWithPacing(void *self, std::uint32_t *event_words);
void RegisterStreamingPartActiveStream(void *active_stream);
void UnregisterStreamingPartActiveStream(void *active_stream);
void StreamingPartFile_RequestCloseAndDrainPendingWrites(
    StreamingPartFileHandle::RawHandle *file_handle);
bool StreamingPartFile_FlushEntryCache(StreamingPartFileHandle::RawHandle *file_handle,
                                       openwow::core::StreamingEntry *entry);
bool StreamingPartFile_RequestCloseDrainAndDispatchArchiveEvent(
    void *self, std::uint32_t *event_words);
bool StreamingPartFile_ReleaseEntryAndDispatchArchiveEvent(
    void *self, std::uint32_t *event_words);
bool StreamingPartFile_CloseHandleIfNoActiveStream(void *self,
                                                   std::uint32_t *event_words);

void SetArchiveEventDispatchTimingHooksForTests(ArchiveEventDispatchTimingHooks hooks);
void ResetArchiveEventDispatchTimingStateForTests();
bool IsStreamingPartActiveStreamCloseRequestedForTests(void *active_stream);
void SetStreamingPartReleaseNotificationEnabledForTests(bool enabled);
void SetStreamingPartFileHandleResolverForTests(
    std::function<void *(std::uint32_t)> resolver);

}

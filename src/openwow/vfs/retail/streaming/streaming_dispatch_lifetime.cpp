#include "openwow/vfs/retail/streaming/streaming_dispatch_lifetime.h"

#include "openwow/core/streaming_storage.h"
#include "openwow/platform/adapters/win32/win32_compat.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/vfs/retail/file_stack/file_stack_provider.h"

#include <cmath>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>

namespace openwow::vfs {
namespace {

using ArchiveEventDispatchFn = bool (*)(void *, std::uint32_t *);

constexpr std::int64_t kPacingUnitsPerSecond = 56888;
constexpr std::int64_t kMinimumDebtNs = -1000000000ll;
constexpr std::int64_t kSleepThresholdNs = 10000000ll;
constexpr std::int64_t kNanosecondsPerMillisecond = 1000000ll;
constexpr double kNanosecondsPerSecond = 1000000000.0;

struct TimingState {
  std::mutex mutex;
  std::int64_t sleep_debt_ns = 0;
  ArchiveEventDispatchTimingHooks hooks{};
};

struct ActiveStreamState {
  std::mutex mutex;
  std::condition_variable quiesced_cv;
  bool close_requested = false;
  std::uint32_t active_callbacks = 0;
};

struct ActiveStreamRegistry {
  std::mutex mutex;
  std::unordered_map<void *, std::shared_ptr<ActiveStreamState>> states;
};

TimingState &Timing() {
  static TimingState state;
  return state;
}

ActiveStreamRegistry &Registry() {
  static ActiveStreamRegistry registry;
  return registry;
}

bool &ReleaseNotificationEnabled() {
  static bool enabled = false;
  return enabled;
}

std::function<void *(std::uint32_t)> &HandleResolver() {
  static std::function<void *(std::uint32_t)> resolver;
  return resolver;
}

std::shared_ptr<ActiveStreamState> FindState(void *stream) {
  if (!stream) {
    return {};
  }
  auto &registry = Registry();
  std::lock_guard lock(registry.mutex);
  const auto it = registry.states.find(stream);
  return it == registry.states.end() ? std::shared_ptr<ActiveStreamState>{} : it->second;
}

class ScopedActiveCallback {
public:
  explicit ScopedActiveCallback(void *stream) : state_(FindState(stream)) {
    if (state_) {
      std::lock_guard lock(state_->mutex);
      ++state_->active_callbacks;
    }
  }
  ~ScopedActiveCallback() {
    if (!state_) {
      return;
    }
    std::lock_guard lock(state_->mutex);
    if (state_->active_callbacks > 0) {
      --state_->active_callbacks;
    }
    if (state_->active_callbacks == 0) {
      state_->quiesced_cv.notify_all();
    }
  }
private:
  std::shared_ptr<ActiveStreamState> state_;
};

ArchiveEventDispatchTimingHooks CopyHooks() {
  auto &state = Timing();
  std::lock_guard lock(state.mutex);
  return state.hooks;
}

std::int64_t NowNs() {
  const auto hooks = CopyHooks();
  return hooks.now_ns ? hooks.now_ns()
                      : openwow::core::GameClock::GetCurrentTimeNsSince2000();
}

void Sleep(std::uint32_t milliseconds) {
  const auto hooks = CopyHooks();
  if (hooks.sleep_ms) {
    hooks.sleep_ms(milliseconds);
  } else {
    openwow::platform::StormSleep(milliseconds);
  }
}

void Pace(std::int32_t pacing_units, std::int64_t elapsed_ns) {
  const auto hooks = CopyHooks();
  if (hooks.pre_sleep_milliseconds > 0) {
    Sleep(static_cast<std::uint32_t>(hooks.pre_sleep_milliseconds));
  }
  const auto target_ns = static_cast<std::int64_t>(std::llrint(
      (static_cast<double>(pacing_units) / static_cast<double>(kPacingUnitsPerSecond)) *
      kNanosecondsPerSecond));
  std::uint32_t sleep_ms = 0;
  {
    auto &state = Timing();
    std::lock_guard lock(state.mutex);
    state.sleep_debt_ns += target_ns - elapsed_ns;
    if (state.sleep_debt_ns < kMinimumDebtNs) {
      state.sleep_debt_ns = kMinimumDebtNs;
      return;
    }
    if (state.sleep_debt_ns > kSleepThresholdNs) {
      const auto whole_ms = state.sleep_debt_ns / kNanosecondsPerMillisecond;
      state.sleep_debt_ns -= whole_ms * kNanosecondsPerMillisecond;
      sleep_ms = static_cast<std::uint32_t>(whole_ms);
    }
  }
  if (sleep_ms > 0) {
    Sleep(sleep_ms);
  }
}

bool Dispatch(void *callback_table, std::uint32_t *event_words) {
  if (!callback_table) {
    return false;
  }
  ArchiveEventDispatchFn callback = nullptr;
  std::memcpy(&callback, static_cast<std::uint8_t *>(callback_table) + event_words[0],
              sizeof(callback));
  return callback(callback_table, event_words);
}

void *ResolveHandle(std::uint32_t word) {
  auto &resolver = HandleResolver();
  return resolver ? resolver(word)
                  : reinterpret_cast<void *>(static_cast<std::uintptr_t>(word));
}

bool ConvertProviderHandle(const void *provider_handle, int &out_handle) {
  if (!provider_handle) {
    return false;
  }
  const auto value = static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(provider_handle));
  if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
    return false;
  }
  out_handle = static_cast<int>(value);
  return true;
}

void DispatchReleaseNotification(std::uint32_t handle_word) {
  if (!ReleaseNotificationEnabled()) {
    return;
  }
  auto *table = GetActiveFileStackCallbackTable();
  if (!table) {
    return;
  }
  std::array<std::uint32_t, 36> event{};
  event[0] = 0x20u;
  event[3] = handle_word;
  (void)Dispatch(table, event.data());
}

}

StreamingPartFileHandle::StreamingPartFileHandle(const char *source_path)
    : source_path_storage_(source_path ? source_path : "") {
  raw_handle_.stream_context_link = &inline_link_;
  raw_handle_.source_path = source_path_storage_.c_str();
}

void StreamingPartFileHandle::ContextLink::AttachStreamingEntry(
    openwow::core::StreamingEntry *entry, std::uint32_t retain_count) noexcept {
  std::lock_guard lock(attachment_mutex_);
  attached_streaming_entry_ = entry;
  attached_streaming_entry_refcount_ = entry ? retain_count : 0;
}

void StreamingPartFileHandle::ContextLink::DetachStreamingEntry() noexcept {
  std::lock_guard lock(attachment_mutex_);
  attached_streaming_entry_ = nullptr;
  attached_streaming_entry_refcount_ = 0;
}

openwow::core::StreamingEntry *
StreamingPartFileHandle::ContextLink::attached_streaming_entry() const noexcept {
  std::lock_guard lock(attachment_mutex_);
  return attached_streaming_entry_;
}

std::uint32_t
StreamingPartFileHandle::ContextLink::attached_streaming_entry_refcount() const noexcept {
  std::lock_guard lock(attachment_mutex_);
  return attached_streaming_entry_refcount_;
}

StreamingPartFileHandle::ContextLink::ReleaseDisposition
StreamingPartFileHandle::ContextLink::ReleaseStreamingEntryReference(
    openwow::core::StreamingEntry *&released_entry) noexcept {
  std::lock_guard lock(attachment_mutex_);
  released_entry = nullptr;
  if (!attached_streaming_entry_ || attached_streaming_entry_refcount_ == 0) {
    return ReleaseDisposition::NoAttachment;
  }
  released_entry = attached_streaming_entry_;
  if (--attached_streaming_entry_refcount_ > 0) {
    released_entry = nullptr;
    return ReleaseDisposition::Retained;
  }
  attached_streaming_entry_ = nullptr;
  return ReleaseDisposition::FinalRelease;
}

bool Storm_DispatchArchiveEvent(void *self, std::uint32_t *event_words) {
  ScopedActiveCallback callback(self);
  return Dispatch(static_cast<StormArchiveEventDispatcherCompat *>(self)->callback_table,
                  event_words);
}

bool Storm_DispatchArchiveEventWithPacing(void *self, std::uint32_t *event_words) {
  ScopedActiveCallback callback(self);
  const auto start = NowNs();
  const bool result = Dispatch(
      static_cast<StormArchiveEventDispatcherCompat *>(self)->callback_table, event_words);
  const auto elapsed = NowNs() - start;
  std::int32_t units = 0;
  std::memcpy(&units, event_words + 24, sizeof(units));
  Pace(units, elapsed);
  return result;
}

void RegisterStreamingPartActiveStream(void *stream) {
  if (!stream) {
    return;
  }
  auto &registry = Registry();
  std::lock_guard lock(registry.mutex);
  auto &state = registry.states[stream];
  if (!state) {
    state = std::make_shared<ActiveStreamState>();
  }
}

void UnregisterStreamingPartActiveStream(void *stream) {
  if (!stream) {
    return;
  }
  auto &registry = Registry();
  std::lock_guard lock(registry.mutex);
  registry.states.erase(stream);
}

void StreamingPartFile_RequestCloseAndDrainPendingWrites(
    StreamingPartFileHandle::RawHandle *file_handle) {
  if (!file_handle) {
    return;
  }
  int provider_handle = 0;
  if (ConvertProviderHandle(file_handle->provider_handle, provider_handle)) {
    openwow::core::StreamingStorage::Instance().DrainPendingPartWriteTasksForHandle(
        provider_handle);
  }
  auto *link = file_handle->stream_context_link;
  if (!link || !link->active_stream) {
    return;
  }
  const auto state = FindState(link->active_stream);
  if (!state) {
    return;
  }
  std::unique_lock lock(state->mutex);
  state->close_requested = true;
  state->quiesced_cv.wait(lock, [&] { return state->active_callbacks == 0; });
}

bool StreamingPartFile_FlushEntryCache(StreamingPartFileHandle::RawHandle *file_handle,
                                       openwow::core::StreamingEntry *entry) {
  if (!entry) {
    return false;
  }
  if ((entry->flags & 4u) != 0) {
    return true;
  }
  auto &storage = openwow::core::StreamingStorage::Instance();
  if ((entry->flags & 1u) != 0 &&
      storage.MarkStreamingEntryBypassPartValidationIfAllBlocksReady(*entry)) {
    return true;
  }
  StreamingPartFile_RequestCloseAndDrainPendingWrites(file_handle);
  storage.ResetStreamingEntryNonReadyBlocks(*entry);
  return true;
}

bool StreamingPartFile_RequestCloseDrainAndDispatchArchiveEvent(
    void *self, std::uint32_t *event_words) {
  auto *file = static_cast<StreamingPartFileHandle::RawHandle *>(ResolveHandle(event_words[3]));
  if (file && file->stream_context_link && file->stream_context_link->active_stream) {
    StreamingPartFile_RequestCloseAndDrainPendingWrites(file);
  }
  return Dispatch(static_cast<StormArchiveEventDispatcherCompat *>(self)->callback_table,
                  event_words);
}

bool StreamingPartFile_ReleaseEntryAndDispatchArchiveEvent(
    void *self, std::uint32_t *event_words) {
  if (!event_words) {
    return false;
  }
  DispatchReleaseNotification(event_words[3]);
  auto *file = static_cast<StreamingPartFileHandle::RawHandle *>(ResolveHandle(event_words[3]));
  auto *link = file ? file->stream_context_link : nullptr;
  bool flush_ok = true;
  if (link) {
    openwow::core::StreamingEntry *released = nullptr;
    const auto disposition = link->ReleaseStreamingEntryReference(released);
    if (disposition == StreamingPartFileHandle::ContextLink::ReleaseDisposition::Retained) {
      return true;
    }
    if (disposition == StreamingPartFileHandle::ContextLink::ReleaseDisposition::FinalRelease) {
      flush_ok = file && StreamingPartFile_FlushEntryCache(file, released);
      std::string().swap(link->normalized_lookup_key);
    }
  }
  auto *dispatcher = static_cast<StormArchiveEventDispatcherCompat *>(self);
  const bool dispatch_ok = dispatcher && dispatcher->callback_table
                               ? Dispatch(dispatcher->callback_table, event_words)
                               : false;
  return flush_ok && dispatch_ok;
}

bool StreamingPartFile_CloseHandleIfNoActiveStream(void *self,
                                                   std::uint32_t *event_words) {
  auto *file = static_cast<StreamingPartFileHandle::RawHandle *>(ResolveHandle(event_words[3]));
  if (file && file->stream_context_link && file->stream_context_link->active_stream) {
    return false;
  }
  return Dispatch(static_cast<StormArchiveEventDispatcherCompat *>(self)->callback_table,
                  event_words);
}

void SetArchiveEventDispatchTimingHooksForTests(ArchiveEventDispatchTimingHooks hooks) {
  auto &state = Timing();
  std::lock_guard lock(state.mutex);
  state.hooks = std::move(hooks);
}

void ResetArchiveEventDispatchTimingStateForTests() {
  auto &state = Timing();
  std::lock_guard lock(state.mutex);
  state.sleep_debt_ns = 0;
  state.hooks = {};
}

bool IsStreamingPartActiveStreamCloseRequestedForTests(void *stream) {
  const auto state = FindState(stream);
  if (!state) {
    return false;
  }
  std::lock_guard lock(state->mutex);
  return state->close_requested;
}

void SetStreamingPartReleaseNotificationEnabledForTests(bool enabled) {
  ReleaseNotificationEnabled() = enabled;
}

void SetStreamingPartFileHandleResolverForTests(
    std::function<void *(std::uint32_t)> resolver) {
  HandleResolver() = std::move(resolver);
}

}

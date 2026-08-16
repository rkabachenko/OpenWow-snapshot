
#include "openwow/runtime/scheduling/evt_sched.h"
#include "openwow/runtime/scheduling/frame_scheduler.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_alloc.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"
#include "openwow/core/storm_sync.h"
#include "openwow/input/input_control.h"
#include "openwow/platform/window/system_mouse_speed.h"
#include "openwow/platform/adapters/win32/win32_compat.h"
#include "openwow/platform/window/window_manager.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <thread>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace openwow::core {

static EvtSchedGlobals g_evt;
static std::mutex g_evt_mutex;

static uint32_t g_input_button_mask = 0;
static uint32_t g_input_active_mask = 0;
static uint32_t g_input_mode = 0;
static uint32_t g_input_state_bits = 0;
static std::mutex g_evt_window_termination_callback_mutex;
static EvtWindowTerminationCallback g_evt_window_termination_callback = nullptr;
static int g_evt_window_termination_callback_param = 0;
using EvtWindowTimerCallback = void (*)();
static std::mutex g_evt_window_timer_callback_mutex;
static EvtWindowTimerCallback g_evt_window_timer_callback = nullptr;

int EvtThread_CreateOrReuseLocked();

#if defined(_WIN32)
static std::mutex g_evt_window_timer_hook_mutex;
static HWND g_evt_window_timer_hook_hwnd = nullptr;
static WNDPROC g_evt_window_timer_hook_prev_wndproc = nullptr;
static SHORT g_evt_startup_num_lock_state = 0;
static bool g_evt_startup_num_lock_state_captured = false;
#endif

namespace {

using RawTimerCallback = int (*)(EvtTimerDispatchArgs *, int);
using RawDestroyCallback = void (*)(EvtTimerDispatchArgs *, int, int, int);
using RawEventCallback = int (*)(void *, int);

constexpr int kEvtHandlerSlotCount = 36;
constexpr int kContextFinalizeHandlerType = 3;
constexpr int kEnterHandlerType = 8;
constexpr int kExitHandlerType = 4;
constexpr int kFrameDispatchTypePreTimers = 5;
constexpr int kFrameDispatchTypePostQueue = 6;
constexpr int kFrameDispatchTypePostTimers = 7;
constexpr int kImmediateCharEventType = 1;
constexpr int kImmediateInputResetEventType = 2;
constexpr int kImmediateButtonDownEventType = 9;
constexpr int kImmediateButtonUpEventType = 10;
constexpr int kImmediateButtonRepeatEventType = 11;
constexpr int kImmediateMouseDownEventType = 12;
constexpr int kImmediateMouseUpEventType = 13;
constexpr int kImmediateMouseMoveEventType = 14;
constexpr int kImmediateMouseButtonReleaseEventType = 15;
constexpr int kImmediateInputModeChangedEventType = 16;
constexpr int kImmediateMouseWheelEventType = 17;
constexpr int kImmediateMouseDeltaMoveEventType = 18;
constexpr int kImmediateAxisEventType = 19;
constexpr int kImmediateFocusGainedEventType = 20;
constexpr int kImmediateFocusLostEventType = 21;
constexpr int kImmediateFocusDataEventType = 22;
constexpr int kImmediateImeCompositionEventType = 34;
constexpr int kImmediateImeCandidateEventType = 35;
constexpr int kEvtContextStateActive = 0;
constexpr int kEvtContextStateShutdownRequested = 1;
constexpr int kEvtContextStateFinalized = 2;
constexpr uint32_t kInvalidEvtThreadSlot = UINT32_MAX;
constexpr uint32_t kInvalidQueuedContextIndex = UINT32_MAX;
constexpr uint32_t kInvalidTimerHeapIndex = UINT32_MAX;
constexpr uint32_t kFlushBufferedDispatchFlag = 1;
constexpr uint32_t kCapturedInputDispatchFlag = 2;
constexpr uint32_t kFallbackInputViewportWidth = 1024;
constexpr uint32_t kFallbackInputViewportHeight = 768;
constexpr std::size_t kLegacyEvtContextSize = 632;
constexpr std::size_t kLegacyEvtContextStateOffset = 0x3C;
constexpr std::size_t kLegacyEvtContextNextWakeTickOffset = 0x4C;
constexpr std::size_t kLegacyEvtContextTickOffset = 0x50;
constexpr std::size_t kLegacyEvtContextFlagsOffset = 0x54;
constexpr std::size_t kLegacyEvtContextIdleTimeOffset = 0x58;
constexpr std::size_t kLegacyEvtContextIdleTimeMirrorOffset = 0x5C;
constexpr std::size_t kLegacyEvtContextWeightOffset = 0x60;
constexpr std::size_t kLegacyEvtContextWeightMirrorOffset = 0x64;
constexpr std::size_t kLegacyEvtContextLoadRebalancePendingOffset = 0x68;
constexpr std::size_t kLegacyEvtContextBucketTableOffset = 0x6C;
constexpr std::size_t kLegacyEvtContextBucketStride = 0x0C;
constexpr std::size_t kLegacyEvtContextBucketSentinelOffset = 0x04;
constexpr std::size_t kLegacyEvtContextQueuedMessageListRootOffset = 0x21C;
constexpr std::size_t kLegacyEvtContextQueuedMessageListHeadOffset = 0x220;
constexpr std::size_t kLegacyEvtContextQueuedMessageListTailOffset = 0x224;
constexpr std::size_t kLegacyEvtContextHeldButtonMaskOffset = 0x228;
constexpr std::size_t kLegacyEvtContextRepeatKeyDownListRootOffset = 0x22C;
constexpr std::size_t kLegacyEvtContextRepeatKeyDownListHeadOffset = 0x230;
constexpr std::size_t kLegacyEvtContextRepeatKeyDownListTailOffset = 0x234;
constexpr std::size_t kLegacyEvtContextTimerWordsOffset = 0x23C;
constexpr std::size_t kLegacyEvtContextQueuedMessageListCapacityOffset = 0x268;
constexpr std::size_t kLegacyEvtContextHandleOffset = 0x0C;
constexpr std::size_t kLegacyEvtContextProfileObjectOffset = 0x26C;
constexpr std::size_t kLegacyEvtContextDebugContextOffset = 0x270;
constexpr std::size_t kLegacyEvtContextFlagsArgOffset = 0x274;
constexpr uint32_t kLegacyTaggedListBit = 1u;

#if defined(_WIN32)
constexpr UINT kWindowTimerCallbackMessage0 = 0x313u;
constexpr UINT kWindowTimerCallbackMessage1 = 0xA4u;
constexpr UINT kWindowTimerCallbackMessage2 = 0xA5u;
constexpr UINT kWindowTimerPeriodMs = 0x21u;
#endif

struct EvtFrameDispatchArgs {
  float delta_seconds = 0.0f;
  uint32_t current_tick_ms = 0;
};

struct WindowClientRect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

struct EvtHandlerNode {
  int callback = 0;
  int param = 0;
  float priority = 0.0f;
  bool linked = false;
  std::shared_ptr<EvtHandlerNode> next;
  std::weak_ptr<EvtHandlerNode> prev;
};

struct EvtHandlerBucket {
  std::shared_ptr<EvtHandlerNode> head;
};

struct QueuedEvtContextEvent {
  static constexpr std::size_t kMinimumPayloadBytes = sizeof(uint32_t);

  uint32_t event_type = 0;
  std::unique_ptr<std::byte[]> payload_storage;
  std::size_t payload_size = 0;
  std::size_t payload_storage_size = 0;

  void AllocatePayloadStorage(std::size_t requested_size) {
    payload_storage_size = std::max(requested_size, kMinimumPayloadBytes);
    payload_storage.reset(new std::byte[payload_storage_size]);
    payload_size = requested_size;
  }

  [[nodiscard]] void *payload_data() const {
    return payload_storage.get();
  }
};

struct EvtImmediatePointerPayload {
  uint32_t input_mode = 0;
  uint32_t button_mask = 0;
  uint32_t held_button_mask = 0;
  uint32_t button_state_bits = 0;
  uint32_t dispatch_flags = 0;
  float normalized_x = 0.0f;
  float normalized_y = 0.0f;
  float axis_value = 0.0f;
  float timestamp = 0.0f;
};

struct EvtImmediateMouseDeltaPayload {
  uint32_t reserved = 0;
  uint32_t source_id = 0;
  uint32_t padding = 0;
  float magnitude = 0.0f;
  float angle = 0.0f;
  float normalized_dx = 0.0f;
  float normalized_dy = 0.0f;
  uint32_t timestamp_word = 0;
};
static_assert(sizeof(EvtImmediateMouseDeltaPayload) == 32);

struct EvtImmediateAxisPayload {
  uint32_t source = 0;
  uint32_t axis_id = 0;
  uint32_t padding_0 = 0;
  float normalized_value = 0.0f;
  uint32_t padding_1[3] = {};
  uint32_t timestamp = 0;
};
static_assert(sizeof(EvtImmediateAxisPayload) == 32);

struct EvtImmediateImeCompositionPayload {
  uint32_t word_0 = 0;
  uint32_t word_1 = 0;
  uint32_t word_2 = 0;
  uint32_t code_page = 0;
};

struct EvtImmediateImeCandidatePayload {
  uint32_t word_0 = 0;
  uint32_t word_1 = 0;
};

struct EvtImmediateFocusPayload {
  uint32_t reserved = 0;
  uint32_t value = 0;
  uint32_t flag = 0;
  uint32_t timestamp = 0;
};

struct EvtImmediateCharPayload {
  uint32_t character = 0;
  uint32_t button_state_bits = 0;
  uint32_t event_word_2 = 0;
};

struct EvtImmediateUtf16CharSequenceEvent {
  const uint16_t *characters = nullptr;
  int character_count = 0;
};

struct EvtImmediateButtonPayload {
  int button_index = 0;
  uint32_t button_state_bits = 0;
  int event_word_1 = 0;
  int event_word_3 = 0;
};

struct EvtTimerSlot {
  uint32_t id = 0;
  uint32_t deadline_tick_ms = 0;
  float interval_seconds = 0.0f;
  int callback = 0;
  int param = 0;
  int destroy_callback = 0;
  int destroy_arg0 = 0;
  int destroy_arg1 = 0;
  int destroy_arg2 = 0;
  uint32_t timer_heap_index = kInvalidTimerHeapIndex;
  bool queued = false;
};

struct EvtTimerSlotPointerArray {
  [[nodiscard]] uint32_t capacity() const {
    return static_cast<uint32_t>(entries_.size());
  }

  [[nodiscard]] uint32_t count() const {
    return count_;
  }

  [[nodiscard]] bool contains(uint32_t index) const {
    return index < std::min(count_, capacity());
  }

  void clear() {
    entries_.clear();
    count_ = 0;
    growth_quantum_ = 0;
  }

  std::unique_ptr<EvtTimerSlot> &operator[](uint32_t index) {
    return entries_[index];
  }

  const std::unique_ptr<EvtTimerSlot> &operator[](uint32_t index) const {
    return entries_[index];
  }

  void set_capacity(uint32_t new_capacity) {
    if (new_capacity == capacity()) {
      return;
    }

    const uint32_t preserved_count = std::min(count_, new_capacity);
    std::vector<std::unique_ptr<EvtTimerSlot>> rebuilt(static_cast<std::size_t>(new_capacity));
    for (uint32_t index = 0; index < preserved_count; ++index) {
      rebuilt[index] = std::move(entries_[index]);
    }

    entries_.swap(rebuilt);
  }

  void set_count(uint32_t new_count) {
    count_ = new_count;
  }

  uint32_t resolve_auto_grow_quantum(uint32_t requested_count) {
    if (requested_count >= 64) {
      growth_quantum_ = 64;
      return growth_quantum_;
    }

    uint32_t quantum = 1;
    for (uint32_t value = requested_count & (requested_count - 1); value != 0;
         value &= (value - 1)) {
      quantum = value;
    }
    return quantum;
  }

  std::vector<std::unique_ptr<EvtTimerSlot>> entries_;
  uint32_t count_ = 0;
  uint32_t growth_quantum_ = 0;
};

struct EvtThreadSlotRuntime {
  std::mutex queue_mutex;
  uint32_t slot_index = 0;
  uint32_t refcount = 0;
  uint32_t total_weight = 0;
  uint32_t average_weight = 0;
  uint32_t context_count = 0;
  uint32_t fairness_credit = 0;
  std::unique_ptr<SEvent> wake_event;
  std::vector<uint32_t> queued_context_handles;
};

struct EvtContextRuntime {
  struct TlsBinding {
    uint32_t handle = 0;
  };

  uint32_t handle = 0;
  EvtContextIdaState ida = {};
  TlsBinding tls_binding = {};
  CallbackHandle frame_pump_handle = CallbackHandle::Invalid;
  void *legacy_storage = nullptr;
  void *profile_object = nullptr;
  std::mutex handler_mutex;
  std::array<EvtHandlerBucket, kEvtHandlerSlotCount> handlers;
  std::mutex timer_mutex;
  std::mutex state_mutex;
  std::deque<QueuedEvtContextEvent> queued_events;
  EvtTimerSlotPointerArray timer_slots;
  std::vector<uint32_t> free_timer_ids;
  std::vector<uint32_t> timer_heap_ids;
  std::vector<int> repeat_tracked_button_downs;
  uint32_t next_timer_id = 1;
  uint32_t current_tick_ms = GameClock::GetTickCount32();
  uint32_t weight = 0;
  uint32_t worker_slot_index = kInvalidEvtThreadSlot;
  uint32_t queued_context_index = kInvalidQueuedContextIndex;
  uint32_t held_mouse_button_mask = 0;
  double tick_fraction_ms = 0.0;
  int state = kEvtContextStateActive;
  bool enter_dispatched = false;
  bool finalize_started = false;
  bool load_rebalance_pending = false;
  bool owns_legacy_storage = false;
  bool slot_load_accounted = false;
};

struct EvtWorkerThreadHandleSlot {
  void *handle = nullptr;
};

using EvtWorkerThreadHandleSlotStorage = std::unique_ptr<EvtWorkerThreadHandleSlot>;

std::unordered_map<uint32_t, std::shared_ptr<EvtContextRuntime>> g_evt_contexts;
std::unordered_map<EvtContextRuntime *, std::shared_ptr<EvtContextRuntime>> g_pending_evt_contexts;
std::unordered_map<EvtContextRuntime *, uint32_t> g_evt_context_handles_by_ptr;
std::unordered_map<void *, std::shared_ptr<EvtContextRuntime>> g_evt_contexts_by_legacy_ptr;
std::vector<std::shared_ptr<EvtThreadSlotRuntime>> g_evt_thread_slots;
std::vector<std::unique_ptr<SCritSect>> g_evt_slot_critical_sections;
std::vector<EvtWorkerThreadHandleSlotStorage> g_evt_worker_thread_handles;
std::unique_ptr<SEvent> g_evt_ready_event;
std::unique_ptr<SEvent> g_evt_shutdown_event;
uint32_t g_next_evt_context_handle = 0;
uint32_t g_primary_evt_context_handle = 0;
std::mutex g_evt_callback_mutex;
std::unordered_map<int, intptr_t> g_evt_legacy_callbacks;
int g_next_evt_callback_token = 1;
WindowClientRect g_cached_active_window_client_rect;

uint32_t ToLegacyPointerWord(const void *ptr) {
  return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(ptr) & UINT32_MAX);
}

bool RefreshCachedActiveWindowClientRectFromWindowManager() {
  auto &window_manager = platform::WindowManager::Get();
  const auto width = window_manager.GetWidth();
  const auto height = window_manager.GetHeight();
  if (width == 0 || height == 0) {
    return false;
  }

  g_cached_active_window_client_rect.left = 0;
  g_cached_active_window_client_rect.top = 0;
  g_cached_active_window_client_rect.right = static_cast<int>(width);
  g_cached_active_window_client_rect.bottom = static_cast<int>(height);
  return true;
}

bool GetCachedActiveWindowClientRect(WindowClientRect *out_rect) {
  if (out_rect == nullptr) {
    SErrSetLastError(87);
    return false;
  }

  if (g_cached_active_window_client_rect.right == 0 ||
      g_cached_active_window_client_rect.bottom == 0) {
    if (!RefreshCachedActiveWindowClientRectFromWindowManager()) {
      return false;
    }
  } else {
    auto &window_manager = platform::WindowManager::Get();
    const auto width = window_manager.GetWidth();
    const auto height = window_manager.GetHeight();
    if (width != 0 && height != 0 &&
        (g_cached_active_window_client_rect.right != static_cast<int>(width) ||
         g_cached_active_window_client_rect.bottom != static_cast<int>(height))) {
      (void)RefreshCachedActiveWindowClientRectFromWindowManager();
    }
  }

  *out_rect = g_cached_active_window_client_rect;
  return true;
}

void SyncEvtSchedulerInitBackingPointersLocked() {
  g_evt.dword_B417D8 = g_evt_thread_slots.empty() ? nullptr : g_evt_thread_slots.data();
  g_evt.dword_B417DC =
      g_evt_slot_critical_sections.empty() ? nullptr : g_evt_slot_critical_sections.front().get();
  g_evt.dword_B41818 =
      g_evt_worker_thread_handles.empty() ? nullptr : g_evt_worker_thread_handles.data();
  g_evt.dword_B41820 = g_evt_ready_event ? 1u : 0u;
  g_evt.dword_B41824 = g_evt_shutdown_event ? 1u : 0u;
}

void ResetEvtSchedulerEvent(std::unique_ptr<SEvent> &event) {
  if (!event) {
    event = std::make_unique<SEvent>(true, false);
    return;
  }

  event->Reset();
}

void ResetEvtSchedulerInitSliceLocked() {
  g_evt_thread_slots.clear();
  g_evt_slot_critical_sections.clear();
  g_evt_worker_thread_handles.clear();
  g_evt_ready_event.reset();
  g_evt_shutdown_event.reset();
  g_evt.dword_B417C4 = 0;
  g_evt.dword_B417C8 = 0;
  g_evt.dword_B417D4 = 0;
  g_evt.dword_B417CC = 0;
  g_evt.dword_B417E0 = 0;
  g_evt.dword_B41810 = 0;
  g_evt.dword_B41814 = 0;
  g_evt.dword_B4181C = 0;
  SyncEvtSchedulerInitBackingPointersLocked();
}

uint32_t ResolveEvtWorkerHandleGrowthQuantumLocked(uint32_t requested_count) {
  if (g_evt.dword_B4181C != 0) {
    return g_evt.dword_B4181C;
  }

  if (requested_count >= 64) {
    g_evt.dword_B4181C = 64;
    return g_evt.dword_B4181C;
  }

  uint32_t quantum = requested_count;
  for (uint32_t value = requested_count & (requested_count - 1); value != 0; value &= (value - 1)) {
    quantum = value;
  }
  if (quantum == 0) {
    quantum = 1;
  }
  return quantum;
}

uint32_t RoundUpToQuantum(uint32_t requested_count, uint32_t quantum) {
  if (quantum == 0) {
    return requested_count;
  }

  const uint32_t remainder = requested_count % quantum;
  if (remainder == 0) {
    return requested_count;
  }
  return requested_count + quantum - remainder;
}

void EvtSched_ResizeWorkerHandleArrayLocked(uint32_t new_capacity) {
  if (new_capacity == g_evt.dword_B41810 &&
      g_evt_worker_thread_handles.size() == static_cast<std::size_t>(new_capacity)) {
    return;
  }

  const uint32_t preserved_count = std::min(g_evt.dword_B41814, new_capacity);
  std::vector<EvtWorkerThreadHandleSlotStorage> rebuilt(
      static_cast<std::size_t>(new_capacity));
  for (uint32_t index = 0; index < preserved_count; ++index) {
    rebuilt[index] = std::move(g_evt_worker_thread_handles[index]);
  }
  g_evt_worker_thread_handles.swap(rebuilt);
  g_evt.dword_B41810 = static_cast<uint32_t>(g_evt_worker_thread_handles.size());
  SyncEvtSchedulerInitBackingPointersLocked();
}

void EnsureEvtWorkerThreadHandleCapacityLocked(uint32_t requested_count) {
  if (requested_count <= g_evt.dword_B41810) {
    return;
  }

  const uint32_t quantum = ResolveEvtWorkerHandleGrowthQuantumLocked(requested_count);
  EvtSched_ResizeWorkerHandleArrayLocked(RoundUpToQuantum(requested_count, quantum));
}

EvtWorkerThreadHandleSlotStorage *AppendEvtWorkerThreadHandleSlotLocked() {
  const uint32_t requested_count = g_evt.dword_B41814 + 1;
  if (requested_count > g_evt.dword_B41810) {
    EnsureEvtWorkerThreadHandleCapacityLocked(requested_count);
  }

  auto *slot = &g_evt_worker_thread_handles[g_evt.dword_B41814];
  ++g_evt.dword_B41814;
  SyncEvtSchedulerInitBackingPointersLocked();
  return slot;
}

void StoreLegacyU32(void *storage, std::size_t offset, uint32_t value) {
  std::memcpy(static_cast<std::byte *>(storage) + offset, &value, sizeof(value));
}

uint32_t LoadLegacyU32(const void *storage, std::size_t offset) {
  uint32_t value = 0;
  std::memcpy(&value, static_cast<const std::byte *>(storage) + offset, sizeof(value));
  return value;
}

void InitializeLegacyIntrusiveListHead(void *storage, std::size_t head_offset) {
  const auto *const sentinel =
      static_cast<std::byte *>(storage) + head_offset + kLegacyEvtContextBucketSentinelOffset;
  StoreLegacyU32(storage, head_offset, 0);
  StoreLegacyU32(storage, head_offset + 0x4, ToLegacyPointerWord(sentinel));
  StoreLegacyU32(storage, head_offset + 0x8, ToLegacyPointerWord(sentinel) | kLegacyTaggedListBit);
}

void InitializeLegacySentinelPair(void *storage, std::size_t head_offset, std::size_t tail_offset) {
  const auto *const sentinel = static_cast<std::byte *>(storage) + head_offset;
  StoreLegacyU32(storage, head_offset, ToLegacyPointerWord(sentinel));
  StoreLegacyU32(storage, tail_offset, ToLegacyPointerWord(sentinel) | kLegacyTaggedListBit);
}

void MirrorLegacyEvtContextState(const EvtContextRuntime &context) {
  if (context.legacy_storage == nullptr) {
    return;
  }

  StoreLegacyU32(context.legacy_storage, kLegacyEvtContextStateOffset,
                 static_cast<uint32_t>(context.state));
  StoreLegacyU32(context.legacy_storage, kLegacyEvtContextTickOffset, context.ida.tick_ms);

  uint32_t flags_word = context.ida.flags_word;
  if (context.enter_dispatched) {
    flags_word |= 1u;
  }
  StoreLegacyU32(context.legacy_storage, kLegacyEvtContextFlagsOffset, flags_word);
}

void MirrorLegacyEvtContextSchedulingState(const EvtContextRuntime &context) {
  if (context.legacy_storage == nullptr) {
    return;
  }

  StoreLegacyU32(context.legacy_storage, kLegacyEvtContextNextWakeTickOffset,
                 context.ida.next_wake_tick_ms);
  StoreLegacyU32(context.legacy_storage, kLegacyEvtContextWeightOffset, context.weight);
  StoreLegacyU32(context.legacy_storage, kLegacyEvtContextWeightMirrorOffset,
                 context.ida.weight_mirror);
  StoreLegacyU32(context.legacy_storage, kLegacyEvtContextLoadRebalancePendingOffset,
                 context.load_rebalance_pending ? 1u : 0u);
}

void ResetLegacyEvtContextHandlerBuckets(void *storage) {
  if (storage == nullptr) {
    return;
  }

  for (int slot = 0; slot < kEvtHandlerSlotCount; ++slot) {
    InitializeLegacyIntrusiveListHead(
        storage, kLegacyEvtContextBucketTableOffset +
                     static_cast<std::size_t>(slot) * kLegacyEvtContextBucketStride);
  }
}

void ResetLegacyEvtContextQueuedMessageState(void *storage) {
  if (storage == nullptr) {
    return;
  }

  StoreLegacyU32(storage, kLegacyEvtContextQueuedMessageListRootOffset, 4u);
  InitializeLegacySentinelPair(storage, kLegacyEvtContextQueuedMessageListHeadOffset,
                               kLegacyEvtContextQueuedMessageListTailOffset);
}

void ResetLegacyEvtContextRepeatKeyState(void *storage) {
  if (storage == nullptr) {
    return;
  }

  StoreLegacyU32(storage, kLegacyEvtContextHeldButtonMaskOffset, 0u);
  StoreLegacyU32(storage, kLegacyEvtContextRepeatKeyDownListRootOffset, 0u);
  InitializeLegacySentinelPair(storage, kLegacyEvtContextRepeatKeyDownListHeadOffset,
                               kLegacyEvtContextRepeatKeyDownListTailOffset);
}

void ResetLegacyEvtContextTimerWords(void *storage) {
  if (storage == nullptr) {
    return;
  }

  static constexpr std::pair<std::size_t, uint32_t> kResetWords[] = {
      {kLegacyEvtContextTimerWordsOffset + 0x00, 0u},
      {kLegacyEvtContextTimerWordsOffset + 0x04, 0u},
      {kLegacyEvtContextTimerWordsOffset + 0x08, 0u},
      {kLegacyEvtContextTimerWordsOffset + 0x0C, 0u},
      {kLegacyEvtContextTimerWordsOffset + 0x10, 0u},
      {kLegacyEvtContextTimerWordsOffset + 0x14, 0u},
      {kLegacyEvtContextTimerWordsOffset + 0x18, 0u},
      {kLegacyEvtContextTimerWordsOffset + 0x1C, 0u},
      {kLegacyEvtContextTimerWordsOffset + 0x20, 0u},
      {kLegacyEvtContextTimerWordsOffset + 0x24, 0u},
      {kLegacyEvtContextTimerWordsOffset + 0x28, 4u},
  };

  for (const auto &[offset, value] : kResetWords) {
    StoreLegacyU32(storage, offset, value);
  }
}

std::shared_ptr<EvtContextRuntime> FindEvtContextByLegacyStorage(const void *legacy_storage) {
  if (legacy_storage == nullptr) {
    return {};
  }

  std::lock_guard lock(g_evt_mutex);
  const auto it = g_evt_contexts_by_legacy_ptr.find(const_cast<void *>(legacy_storage));
  if (it == g_evt_contexts_by_legacy_ptr.end()) {
    return {};
  }
  return it->second;
}

void ClearHandlerBucketUnlocked(EvtHandlerBucket &bucket);
EvtContextRuntime::TlsBinding *GetActiveEvtContextTlsBinding();
void RemoveContextFromQueuedSlot(const std::shared_ptr<EvtContextRuntime> &context);
void ReleaseContextWorkerSlot(const std::shared_ptr<EvtContextRuntime> &context);
void UnregisterEvtContextLocked(const std::shared_ptr<EvtContextRuntime> &context);
void DispatchImmediateContextEvent(const std::shared_ptr<EvtContextRuntime> &context,
                                   int event_type, void *payload);
void DispatchImmediateContextEvent(int ctx, int event_type, void *payload);
template <typename Payload>
void DispatchImmediateContextEvent(int ctx, int event_type, Payload &payload);

void DestroyEvtContextRuntime(const std::shared_ptr<EvtContextRuntime> &context,
                              bool delete_legacy_storage) {
  if (!context) {
    return;
  }

  const auto frame_pump_handle = context->frame_pump_handle;
  context->frame_pump_handle = CallbackHandle::Invalid;

  {
    std::lock_guard lock(context->handler_mutex);
    for (auto &bucket : context->handlers) {
      ClearHandlerBucketUnlocked(bucket);
    }
  }

  {
    std::lock_guard lock(context->state_mutex);
    context->queued_events.clear();
    context->repeat_tracked_button_downs.clear();
    context->held_mouse_button_mask = 0;
  }

  {
    std::lock_guard lock(context->timer_mutex);
    context->timer_heap_ids.clear();
    context->free_timer_ids.clear();
    context->timer_slots.clear();
    context->next_timer_id = 1;
  }

  RemoveContextFromQueuedSlot(context);
  ReleaseContextWorkerSlot(context);

  {
    std::lock_guard lock(g_evt_mutex);
    UnregisterEvtContextLocked(context);
  }

  if (GetActiveEvtContextTlsBinding() == &context->tls_binding) {
    SetEvtContextTlsValue(nullptr);
  }

  if (frame_pump_handle != CallbackHandle::Invalid) {
    FrameScheduler::Instance().Unregister(frame_pump_handle);
  }

  if (context->legacy_storage != nullptr) {
    ResetLegacyEvtContextHandlerBuckets(context->legacy_storage);
    ResetLegacyEvtContextQueuedMessageState(context->legacy_storage);
    ResetLegacyEvtContextRepeatKeyState(context->legacy_storage);
    ResetLegacyEvtContextTimerWords(context->legacy_storage);
  }

  if (context->profile_object != nullptr) {
    (void)SMemFree(context->profile_object, ".\\Prop.cpp", 0x30, 0);
    context->profile_object = nullptr;
  }

  if (delete_legacy_storage && context->owns_legacy_storage &&
      context->legacy_storage != nullptr) {
    (void)SMemFree(context->legacy_storage, ".\\EvtSched.cpp", 0x3AD, 0);
  }

  context->handle = 0;
  context->tls_binding.handle = 0;
  context->legacy_storage = nullptr;
  context->owns_legacy_storage = false;
}

EvtContextRuntime::TlsBinding *GetActiveEvtContextTlsBinding() {
  return static_cast<EvtContextRuntime::TlsBinding *>(GetEvtContextTlsValue());
}

struct ScopedEvtContext {
  explicit ScopedEvtContext(EvtContextRuntime::TlsBinding *binding)
      : previous(GetEvtContextTlsValue()) {
    SetEvtContextTlsValue(binding);
  }

  ~ScopedEvtContext() {
    SetEvtContextTlsValue(previous);
  }

  void *previous = nullptr;
};

intptr_t ResolveLegacyCallbackAddress(int callback);
void EnsureContextEnterHandlersDispatched(const std::shared_ptr<EvtContextRuntime> &context);
void FinalizeEvtContext(const std::shared_ptr<EvtContextRuntime> &context);
std::shared_ptr<EvtContextRuntime> FindEvtContextByLegacyStorage(const void *legacy_storage);
void DispatchQueuedContextEvents(const std::shared_ptr<EvtContextRuntime> &context);
void DispatchPostTimerHandlersIfActive(const std::shared_ptr<EvtContextRuntime> &context);
void DispatchPostQueueHandlers(const std::shared_ptr<EvtContextRuntime> &context,
                               EvtFrameDispatchArgs frame_args);
void DispatchPostQueueCompletionIfActive(const std::shared_ptr<EvtContextRuntime> &context);
bool SetEvtContextShutdownRequested(const std::shared_ptr<EvtContextRuntime> &context);
bool IsEvtContextShutdownRequested(const std::shared_ptr<EvtContextRuntime> &context);
void SignalEventSchedulerWorkerShutdown();
std::shared_ptr<EvtThreadSlotRuntime> FindEvtThreadSlot(uint32_t slot_index);
bool FindEvtThreadSlotByOpaquePointer(const void *opaque, uint32_t *slot_index,
                                      std::shared_ptr<EvtThreadSlotRuntime> *slot);
void ClearDestroyedEvtThreadQueue(const std::shared_ptr<EvtThreadSlotRuntime> &slot);
void DestroyEvtThreadSlotRuntime(uint32_t slot_index,
                                 const std::shared_ptr<EvtThreadSlotRuntime> &slot);
uint32_t DequeueQueuedContextFromSlot(uint32_t slot_index);
void RemoveContextFromQueuedSlot(const std::shared_ptr<EvtContextRuntime> &context);
void ReleaseContextWorkerSlot(const std::shared_ptr<EvtContextRuntime> &context);
bool RescheduleContextOnWorkerSlot(const std::shared_ptr<EvtContextRuntime> &context,
                                   uint32_t wake_tick_ms, uint32_t queued_weight);
bool SubmitContextToWorkerSlot(const std::shared_ptr<EvtContextRuntime> &context,
                               uint32_t wake_tick_ms);
std::shared_ptr<EvtContextRuntime> FindEvtContext(uint32_t handle);
uint32_t DecodeContextHandle(void *ctx);
std::shared_ptr<EvtContextRuntime> ResolveEvtContextForSubmissionLocked(void *ctx);
uint32_t RegisterEvtContextLocked(const std::shared_ptr<EvtContextRuntime> &context,
                                  bool *newly_registered = nullptr);
void UnregisterEvtContextLocked(const std::shared_ptr<EvtContextRuntime> &context);
bool DispatchHandlers(const std::shared_ptr<EvtContextRuntime> &context, int event_type,
                      void *payload);

float ResolveMouseDispatchTimestampWord(const int *event_words) {
  float timestamp = 0.0f;
  std::memcpy(&timestamp, event_words + 3, sizeof(timestamp));
  return timestamp;
}

bool HasExclusiveInterior(const WindowClientRect &rect) {
  return rect.right - rect.left != 0 && rect.bottom - rect.top != 0;
}

bool RectContainsPointExclusive(const WindowClientRect &rect, const int x, const int y) {
  return rect.left < x && rect.right > x && rect.top < y && rect.bottom > y;
}

int ClampToExclusiveInterior(const int coordinate, const int lower_edge, const int upper_edge) {
  const int lower_inset = lower_edge + 1;
  const int lower_clamped = (coordinate <= lower_inset) ? lower_inset : coordinate;
  const int upper_inset = upper_edge - 1;
  if (upper_inset <= lower_clamped) {
    return upper_inset;
  }

  if (coordinate > lower_inset) {
    return coordinate;
  }

  return lower_inset;
}

std::pair<float, float> ScreenToNormalizedWindowPoint(int screen_x, int screen_y) {
  if (HasExclusiveInterior(g_cached_active_window_client_rect) &&
      !RectContainsPointExclusive(g_cached_active_window_client_rect, screen_x, screen_y)) {
    screen_x = ClampToExclusiveInterior(screen_x, g_cached_active_window_client_rect.left,
                                        g_cached_active_window_client_rect.right);
    screen_y = ClampToExclusiveInterior(screen_y, g_cached_active_window_client_rect.top,
                                        g_cached_active_window_client_rect.bottom);
    platform::WindowManager::Get().SetCursorPosition(screen_x, screen_y);
  }

  WindowClientRect client_rect{};
  client_rect.right = static_cast<int>(kFallbackInputViewportWidth);
  client_rect.bottom = static_cast<int>(kFallbackInputViewportHeight);
  (void)GetCachedActiveWindowClientRect(&client_rect);

  const auto width = std::max(client_rect.right - client_rect.left, 1);
  const auto height = std::max(client_rect.bottom - client_rect.top, 1);

  return {static_cast<float>(screen_x) / static_cast<float>(width),
          1.0f - static_cast<float>(screen_y) / static_cast<float>(height)};
}

EvtImmediateCharPayload BuildImmediateCharPayload(uint32_t character, uint32_t event_word_2) {
  EvtImmediateCharPayload payload{};
  payload.character = character;
  payload.button_state_bits = g_input_state_bits;
  payload.event_word_2 = event_word_2;
  return payload;
}

EvtImmediatePointerPayload BuildImmediatePointerPayloadBase(uint32_t button_mask) {
  EvtImmediatePointerPayload payload{};
  payload.input_mode = g_input_mode;
  payload.button_mask = button_mask;
  payload.held_button_mask = g_input_button_mask;
  payload.button_state_bits = g_input_state_bits;
  payload.dispatch_flags = (g_input_mode == 1) ? kCapturedInputDispatchFlag : 0;
  return payload;
}

EvtImmediatePointerPayload BuildImmediateScreenPointPayload(uint32_t button_mask, int screen_x,
                                                           int screen_y, float axis_value,
                                                           float timestamp) {
  auto payload = BuildImmediatePointerPayloadBase(button_mask);
  const auto [normalized_x, normalized_y] = ScreenToNormalizedWindowPoint(screen_x, screen_y);
  payload.normalized_x = normalized_x;
  payload.normalized_y = normalized_y;
  payload.axis_value = axis_value;
  payload.timestamp = timestamp;
  return payload;
}

void MirrorLegacyHeldButtonMask(EvtContextRuntime &context) {
  if (context.legacy_storage == nullptr) {
    return;
  }

  StoreLegacyU32(context.legacy_storage, kLegacyEvtContextHeldButtonMaskOffset,
                 context.held_mouse_button_mask);
}

void SetEvtContextInvalidParameterError() {
  SErrSetLastError(87);
}

void ApplyWorldMouseButtonDownState(bool is_down) {
  if (is_down) {
    openwow::input::OnMouseButtonSet();
  } else {
    openwow::input::OnMouseButtonClear();
  }
}

void SetRelativeCursorMode(bool enabled) {
  auto &window_manager = platform::WindowManager::Get();
  if (enabled) {
    (void)window_manager.BeginRelativeCursorMode();
  } else {
    window_manager.EndRelativeCursorMode();
  }
}

void CheckActiveMaskAfterButtonRelease() {
  if (g_input_active_mask != 0 &&
      g_input_active_mask != (g_input_active_mask & g_input_button_mask)) {
    ApplyWorldMouseButtonDownState(false);
  }
}

void DispatchSingleMouseButtonRelease(int ctx, uint32_t button_mask, int screen_x,
                                      int screen_y, float timestamp, bool is_flush) {
  g_input_button_mask &= ~button_mask;
  auto payload = BuildImmediateScreenPointPayload(button_mask, screen_x, screen_y,
                                                   0.0f, timestamp);
  if (is_flush) {
    payload.dispatch_flags |= kFlushBufferedDispatchFlag;
  }
  DispatchImmediateContextEvent(ctx, kImmediateMouseButtonReleaseEventType, payload);
  CheckActiveMaskAfterButtonRelease();
}

void PreprocessImmediateEventState(EvtContextRuntime &context, int &event_type, void *payload) {
  switch (event_type) {
  case kImmediateInputResetEventType:
    context.held_mouse_button_mask = 0;
    context.repeat_tracked_button_downs.clear();
    MirrorLegacyHeldButtonMask(context);
    break;
  case kImmediateButtonDownEventType:
  case kImmediateButtonUpEventType: {
    const int button_index = static_cast<EvtImmediateButtonPayload *>(payload)->button_index;
    auto &tracked_button_downs = context.repeat_tracked_button_downs;
    const auto first_match =
        std::remove(tracked_button_downs.begin(), tracked_button_downs.end(), button_index);
    const bool removed_existing = first_match != tracked_button_downs.end();
    tracked_button_downs.erase(first_match, tracked_button_downs.end());

    if (removed_existing && event_type == kImmediateButtonDownEventType) {
      event_type = kImmediateButtonRepeatEventType;
    } else if (event_type == kImmediateButtonDownEventType) {
      tracked_button_downs.insert(tracked_button_downs.begin(), button_index);
    }
    break;
  }
  case kImmediateMouseDownEventType: {
    const auto &mouse_payload = *static_cast<const EvtImmediatePointerPayload *>(payload);
    context.held_mouse_button_mask |= mouse_payload.button_mask;
    MirrorLegacyHeldButtonMask(context);
    break;
  }
  case kImmediateMouseButtonReleaseEventType: {
    const auto &mouse_payload = *static_cast<const EvtImmediatePointerPayload *>(payload);
    context.held_mouse_button_mask &= ~mouse_payload.button_mask;
    MirrorLegacyHeldButtonMask(context);
    break;
  }
  default:
    break;
  }
}

void DispatchImmediateContextEvent(const std::shared_ptr<EvtContextRuntime> &context,
                                   int event_type, void *payload) {
  if (!context) {
    return;
  }

  int dispatched_event_type = event_type;
  {
    std::lock_guard lock(context->state_mutex);
    PreprocessImmediateEventState(*context, dispatched_event_type, payload);
  }

  if (IsSErrDisplayErrorActive()) {
    return;
  }

  (void)DispatchHandlers(context, dispatched_event_type, payload);
}

void DispatchImmediateContextEvent(int ctx, int event_type, void *payload) {
  if (ctx == 0) {
    SetEvtContextInvalidParameterError();
    return;
  }

  const auto context = FindEvtContext(static_cast<uint32_t>(ctx));
  DispatchImmediateContextEvent(context, event_type, payload);
}

template <typename Payload>
void DispatchImmediateContextEvent(int ctx, int event_type, Payload &payload) {
  DispatchImmediateContextEvent(ctx, event_type, static_cast<void *>(&payload));
}

void DispatchImmediateCharEvent(int ctx, uint32_t character, uint32_t event_word_2) {
  auto payload = BuildImmediateCharPayload(character, event_word_2);
  DispatchImmediateContextEvent(ctx, kImmediateCharEventType, payload);
}

void DispatchImmediateUtf16CharSequence(int ctx, const uint16_t *characters, int character_count) {
  for (int index = 0; index < character_count; ++index) {
    DispatchImmediateCharEvent(ctx, characters[index], 1);
  }
}

uint32_t ResolveInputStateBit(int button_index) {

  const auto shift = static_cast<uint32_t>(static_cast<uint8_t>(button_index)) & 31u;
  return 1u << shift;
}

bool UpdateInputStateBitsForButtonTransition(int button_index, bool is_down) {
  if (button_index > 5) {
    return true;
  }

  const uint32_t button_bit = ResolveInputStateBit(button_index);
  if (is_down) {
    if ((g_input_state_bits & button_bit) != 0) {
      return false;
    }
    g_input_state_bits |= button_bit;
    return true;
  }

  if ((g_input_state_bits & button_bit) == 0) {
    return false;
  }

  g_input_state_bits &= ~button_bit;
  return true;
}

void DispatchImmediateButtonTransition(int ctx, int handler_type, int button_index, int event_word_1,
                                       int event_word_3, bool is_down) {
  if (!UpdateInputStateBitsForButtonTransition(button_index, is_down)) {
    return;
  }

  EvtImmediateButtonPayload payload{};
  payload.button_index = button_index;
  payload.button_state_bits = g_input_state_bits;
  payload.event_word_1 = event_word_1;
  payload.event_word_3 = event_word_3;
  DispatchImmediateContextEvent(ctx, handler_type, payload);
}

constexpr float kPositiveNormalizationScale = 1.0f / 32767.0f;
constexpr float kNegativeNormalizationScale = 1.0f / 32768.0f;
constexpr float kPi = 3.14159265358979323846f;

float NormalizeSignedInputDelta(int raw_delta) {
  if (raw_delta >= 0) {
    return static_cast<float>(raw_delta) * kPositiveNormalizationScale;
  }
  return static_cast<float>(raw_delta) * kNegativeNormalizationScale;
}

void DispatchImmediateMouseDeltaMove(int ctx, uint32_t source_id, int delta_x, int delta_y,
                                     uint32_t timestamp_word) {
  const float dx = NormalizeSignedInputDelta(delta_x);
  const float dy = NormalizeSignedInputDelta(delta_y);
  const float magnitude = std::sqrt(dx * dx + dy * dy);
  const float angle = kPi - std::atan2(dx, dy);

  EvtImmediateMouseDeltaPayload payload{};
  payload.reserved = 0;
  payload.source_id = source_id;
  payload.magnitude = magnitude;
  payload.angle = angle;
  payload.normalized_dx = dx;
  payload.normalized_dy = dy;
  payload.timestamp_word = timestamp_word;

  DispatchImmediateContextEvent(ctx, kImmediateMouseDeltaMoveEventType, payload);
}

void DispatchImmediateAxisEvent(int ctx, uint32_t source, uint32_t axis_id,
                                int axis_value, uint32_t timestamp) {
  EvtImmediateAxisPayload payload{};
  payload.source = source;
  payload.axis_id = axis_id;
  payload.normalized_value = NormalizeSignedInputDelta(axis_value);
  payload.timestamp = timestamp;
  DispatchImmediateContextEvent(ctx, kImmediateAxisEventType, payload);
}

uint32_t GetActiveCodePage() {
#if defined(_WIN32)
  return ::GetACP();
#else
  return 65001;
#endif
}

uint32_t SelectPrimaryEvtContextHandleLocked() {
  uint32_t selected = 0;
  for (const auto &[handle, _] : g_evt_contexts) {
    if (selected == 0 || handle < selected) {
      selected = handle;
    }
  }
  return selected;
}

std::shared_ptr<EvtContextRuntime> FindEvtContext(uint32_t handle) {
  if (handle == 0) {
    return {};
  }

  std::lock_guard lock(g_evt_mutex);
  const auto it = g_evt_contexts.find(handle);
  if (it == g_evt_contexts.end()) {
    return {};
  }
  return it->second;
}

uint32_t AllocateEvtContextHandleLocked() {
  uint32_t candidate = g_next_evt_context_handle;
  bool wrapped = false;
  while (true) {
    ++candidate;
    if (candidate == 0) {
      wrapped = true;
      ++candidate;
    }

    if (!wrapped || g_evt_contexts.find(candidate) == g_evt_contexts.end()) {
      g_next_evt_context_handle = candidate;
      return candidate;
    }
  }
}

std::shared_ptr<EvtContextRuntime> ResolveEvtContextForSubmissionLocked(void *ctx) {
  if (ctx == nullptr) {
    return {};
  }

  const auto legacy = g_evt_contexts_by_legacy_ptr.find(ctx);
  if (legacy != g_evt_contexts_by_legacy_ptr.end()) {
    return legacy->second;
  }

  const auto handle = DecodeContextHandle(ctx);
  if (handle != 0) {
    const auto by_handle = g_evt_contexts.find(handle);
    if (by_handle != g_evt_contexts.end()) {
      return by_handle->second;
    }
  }

  auto *const runtime = static_cast<EvtContextRuntime *>(ctx);
  const auto pending = g_pending_evt_contexts.find(runtime);
  if (pending != g_pending_evt_contexts.end()) {
    return pending->second;
  }

  const auto registered = g_evt_context_handles_by_ptr.find(runtime);
  if (registered == g_evt_context_handles_by_ptr.end()) {
    return {};
  }

  const auto by_handle = g_evt_contexts.find(registered->second);
  if (by_handle == g_evt_contexts.end()) {
    return {};
  }
  return by_handle->second;
}

uint32_t RegisterEvtContextLocked(const std::shared_ptr<EvtContextRuntime> &context,
                                  bool *newly_registered) {
  if (newly_registered != nullptr) {
    *newly_registered = false;
  }
  if (!context) {
    return 0;
  }

  const auto existing_by_pointer = g_evt_context_handles_by_ptr.find(context.get());
  if (existing_by_pointer != g_evt_context_handles_by_ptr.end()) {
    context->handle = existing_by_pointer->second;
    context->tls_binding.handle = context->handle;
    return context->handle;
  }

  const uint32_t handle = AllocateEvtContextHandleLocked();
  context->handle = handle;
  context->tls_binding.handle = handle;
  g_evt_contexts[handle] = context;
  g_evt_context_handles_by_ptr[context.get()] = handle;
  if (context->legacy_storage != nullptr) {
    StoreLegacyU32(context->legacy_storage, kLegacyEvtContextHandleOffset, handle);
  }
  g_pending_evt_contexts.erase(context.get());
  if (g_primary_evt_context_handle == 0) {
    g_primary_evt_context_handle = handle;
  }
  if (newly_registered != nullptr) {
    *newly_registered = true;
  }
  return handle;
}

void UnregisterEvtContextLocked(const std::shared_ptr<EvtContextRuntime> &context) {
  if (!context) {
    return;
  }

  g_pending_evt_contexts.erase(context.get());
  g_evt_context_handles_by_ptr.erase(context.get());
  if (context->legacy_storage != nullptr) {
    g_evt_contexts_by_legacy_ptr.erase(context->legacy_storage);
  }
  if (context->handle != 0) {
    g_evt_contexts.erase(context->handle);
    if (g_primary_evt_context_handle == context->handle) {
      g_primary_evt_context_handle = SelectPrimaryEvtContextHandleLocked();
    }
  }
}

uint32_t DecodeContextHandle(void *ctx) {
  const auto raw = reinterpret_cast<std::uintptr_t>(ctx);
  if (raw == 0 || raw > UINT32_MAX) {
    return 0;
  }
  return static_cast<uint32_t>(raw);
}

std::shared_ptr<EvtThreadSlotRuntime> FindEvtThreadSlot(uint32_t slot_index) {
  std::lock_guard lock(g_evt_mutex);
  if (slot_index >= g_evt_thread_slots.size()) {
    return {};
  }
  return g_evt_thread_slots[slot_index];
}

bool FindEvtThreadSlotByOpaquePointer(const void *opaque, uint32_t *slot_index,
                                      std::shared_ptr<EvtThreadSlotRuntime> *slot) {
  if (opaque == nullptr) {
    return false;
  }

  std::lock_guard lock(g_evt_mutex);
  for (uint32_t index = 0; index < static_cast<uint32_t>(g_evt_thread_slots.size()); ++index) {
    const auto &candidate = g_evt_thread_slots[index];
    if (!candidate || candidate.get() != opaque) {
      continue;
    }

    if (slot_index != nullptr) {
      *slot_index = index;
    }
    if (slot != nullptr) {
      *slot = candidate;
    }
    return true;
  }

  return false;
}

void ClearDestroyedEvtThreadQueue(const std::shared_ptr<EvtThreadSlotRuntime> &slot) {
  if (!slot) {
    return;
  }

  std::vector<uint32_t> queued_handles;
  {
    std::lock_guard lock(slot->queue_mutex);
    queued_handles = slot->queued_context_handles;
    slot->queued_context_handles.clear();
    std::vector<uint32_t>().swap(slot->queued_context_handles);
  }

  for (const uint32_t context_handle : queued_handles) {
    const auto context = FindEvtContext(context_handle);
    if (!context) {
      continue;
    }

    if (context->worker_slot_index == slot->slot_index) {
      context->worker_slot_index = kInvalidEvtThreadSlot;
      context->slot_load_accounted = false;
    }
    context->queued_context_index = kInvalidQueuedContextIndex;
  }
}

void DestroyEvtThreadSlotRuntime(uint32_t slot_index,
                                 const std::shared_ptr<EvtThreadSlotRuntime> &slot) {
  if (!slot) {
    return;
  }

  EvtThreadQueue_Destroy(slot.get());

  {
    std::lock_guard lock(slot->queue_mutex);
    slot->refcount = 0;
    slot->total_weight = 0;
    slot->average_weight = 0;
    slot->context_count = 0;
    slot->fairness_credit = 0;
    slot->wake_event.reset();
  }

  std::lock_guard lock(g_evt_mutex);
  if (slot_index >= g_evt_thread_slots.size() || g_evt_thread_slots[slot_index].get() != slot.get()) {
    return;
  }

  if (slot_index == g_evt.dword_B417C8) {
    g_evt.dword_B417C4 = 0;
  }
  g_evt_thread_slots[slot_index].reset();
  SyncEvtSchedulerInitBackingPointersLocked();
}

void UpdateQueuedContextPosition(uint32_t context_handle, uint32_t slot_index,
                                 uint32_t queue_index) {
  const auto context = FindEvtContext(context_handle);
  if (!context) {
    return;
  }

  context->worker_slot_index = slot_index;
  context->queued_context_index = queue_index;
}

void RecomputeWorkerSlotAverageLocked(EvtThreadSlotRuntime &slot) {
  slot.average_weight = slot.context_count != 0 ? slot.total_weight / slot.context_count : 0;
}

bool ShouldContextSortBefore(uint32_t lhs_handle, uint32_t rhs_handle) {
  const auto lhs = FindEvtContext(lhs_handle);
  const auto rhs = FindEvtContext(rhs_handle);
  if (!lhs || !rhs) {
    return lhs_handle < rhs_handle;
  }
  if (lhs->ida.next_wake_tick_ms != rhs->ida.next_wake_tick_ms) {
    return lhs->ida.next_wake_tick_ms <= rhs->ida.next_wake_tick_ms;
  }
  return lhs->handle <= rhs->handle;
}

void SwapQueuedContextsLocked(const std::shared_ptr<EvtThreadSlotRuntime> &slot, uint32_t lhs_index,
                              uint32_t rhs_index) {
  if (!slot || lhs_index >= slot->queued_context_handles.size() ||
      rhs_index >= slot->queued_context_handles.size() || lhs_index == rhs_index) {
    return;
  }

  std::swap(slot->queued_context_handles[lhs_index], slot->queued_context_handles[rhs_index]);
  UpdateQueuedContextPosition(slot->queued_context_handles[lhs_index], slot->slot_index, lhs_index);
  UpdateQueuedContextPosition(slot->queued_context_handles[rhs_index], slot->slot_index, rhs_index);
}

void SiftQueuedContextUpLocked(const std::shared_ptr<EvtThreadSlotRuntime> &slot,
                               uint32_t queue_index) {
  while (slot && queue_index != 0) {
    const uint32_t parent_index = (queue_index - 1) / 2;
    if (ShouldContextSortBefore(slot->queued_context_handles[parent_index],
                                slot->queued_context_handles[queue_index])) {
      break;
    }

    SwapQueuedContextsLocked(slot, queue_index, parent_index);
    queue_index = parent_index;
  }
}

void SiftQueuedContextDownLocked(const std::shared_ptr<EvtThreadSlotRuntime> &slot,
                                 uint32_t queue_index) {
  if (!slot) {
    return;
  }

  const auto child_count = static_cast<uint32_t>(slot->queued_context_handles.size());
  while (true) {
    uint32_t child_index = queue_index * 2 + 1;
    if (child_index >= child_count) {
      break;
    }

    if (child_index + 1 < child_count &&
        ShouldContextSortBefore(slot->queued_context_handles[child_index + 1],
                                slot->queued_context_handles[child_index])) {
      ++child_index;
    }

    if (ShouldContextSortBefore(slot->queued_context_handles[queue_index],
                                slot->queued_context_handles[child_index])) {
      break;
    }

    SwapQueuedContextsLocked(slot, queue_index, child_index);
    queue_index = child_index;
  }
}

void QueueContextLocked(const std::shared_ptr<EvtThreadSlotRuntime> &slot,
                        const std::shared_ptr<EvtContextRuntime> &context) {
  if (!slot || !context) {
    return;
  }

  const uint32_t queue_index = static_cast<uint32_t>(slot->queued_context_handles.size());
  slot->queued_context_handles.push_back(context->handle);
  context->worker_slot_index = slot->slot_index;
  context->queued_context_index = queue_index;
  SiftQueuedContextUpLocked(slot, queue_index);
}

uint32_t DequeueQueuedContextAtIndexLocked(const std::shared_ptr<EvtThreadSlotRuntime> &slot,
                                           uint32_t queue_index) {
  if (!slot || queue_index >= slot->queued_context_handles.size()) {
    return 0;
  }

  const uint32_t removed_handle = slot->queued_context_handles[queue_index];
  const uint32_t last_index = static_cast<uint32_t>(slot->queued_context_handles.size() - 1);
  if (queue_index != last_index) {
    slot->queued_context_handles[queue_index] = slot->queued_context_handles[last_index];
    UpdateQueuedContextPosition(slot->queued_context_handles[queue_index], slot->slot_index,
                                queue_index);
  }
  slot->queued_context_handles.pop_back();

  if (queue_index < slot->queued_context_handles.size()) {
    SiftQueuedContextDownLocked(slot, queue_index);
  }

  const auto removed_context = FindEvtContext(removed_handle);
  if (removed_context) {
    removed_context->queued_context_index = kInvalidQueuedContextIndex;
  }
  return removed_handle;
}

uint32_t DequeueQueuedContextFromSlot(uint32_t slot_index) {
  const auto slot = FindEvtThreadSlot(slot_index);
  if (!slot) {
    if (slot_index == g_evt.dword_B417C8) {
      g_evt.dword_B417C4 = 0;
    }
    return 0;
  }

  uint32_t context_handle = 0;
  {
    std::lock_guard lock(slot->queue_mutex);
    if (!slot->queued_context_handles.empty()) {
      context_handle = DequeueQueuedContextAtIndexLocked(slot, 0);
    }
  }

  if (slot_index == g_evt.dword_B417C8) {
    g_evt.dword_B417C4 = context_handle;
  }
  return context_handle;
}

std::vector<std::shared_ptr<EvtContextRuntime>>
DrainQueuedContextsForSlotShutdown(uint32_t slot_index) {
  const auto slot = FindEvtThreadSlot(slot_index);
  if (!slot) {
    if (slot_index == g_evt.dword_B417C8) {
      g_evt.dword_B417C4 = 0;
    }
    return {};
  }

  std::vector<uint32_t> drained_handles;
  {
    std::lock_guard lock(slot->queue_mutex);
    drained_handles.reserve(slot->queued_context_handles.size());
    while (!slot->queued_context_handles.empty()) {
      const uint32_t context_handle = DequeueQueuedContextAtIndexLocked(slot, 0);
      if (context_handle != 0) {
        drained_handles.push_back(context_handle);
      }
    }
  }

  if (slot_index == g_evt.dword_B417C8) {
    g_evt.dword_B417C4 = 0;
  }

  std::vector<std::shared_ptr<EvtContextRuntime>> drained_contexts;
  drained_contexts.reserve(drained_handles.size());
  for (auto it = drained_handles.rbegin(); it != drained_handles.rend(); ++it) {
    const auto context = FindEvtContext(*it);
    if (context) {
      drained_contexts.push_back(context);
    }
  }

  return drained_contexts;
}

void RemoveContextFromQueuedSlot(const std::shared_ptr<EvtContextRuntime> &context) {
  if (!context || context->worker_slot_index == kInvalidEvtThreadSlot ||
      context->queued_context_index == kInvalidQueuedContextIndex) {
    return;
  }

  const auto slot = FindEvtThreadSlot(context->worker_slot_index);
  if (!slot) {
    context->queued_context_index = kInvalidQueuedContextIndex;
    return;
  }

  std::lock_guard lock(slot->queue_mutex);
  if (context->queued_context_index < slot->queued_context_handles.size() &&
      slot->queued_context_handles[context->queued_context_index] == context->handle) {
    (void)DequeueQueuedContextAtIndexLocked(slot, context->queued_context_index);
  } else {
    context->queued_context_index = kInvalidQueuedContextIndex;
  }
}

void ReleaseContextWorkerSlot(const std::shared_ptr<EvtContextRuntime> &context) {
  if (!context || !context->slot_load_accounted ||
      context->worker_slot_index == kInvalidEvtThreadSlot) {
    return;
  }

  const auto slot = FindEvtThreadSlot(context->worker_slot_index);
  if (slot) {
    std::vector<std::shared_ptr<EvtThreadSlotRuntime>> slots;
    {
      std::lock_guard topology_lock(g_evt_mutex);
      slots = g_evt_thread_slots;
    }

    uint32_t released_slot_total_weight = 0;
    {
      std::lock_guard lock(slot->queue_mutex);
      slot->total_weight =
          slot->total_weight > context->weight ? slot->total_weight - context->weight : 0;
      if (slot->context_count != 0) {
        --slot->context_count;
      }
      RecomputeWorkerSlotAverageLocked(*slot);
      released_slot_total_weight = slot->total_weight;
    }

    for (const auto &candidate : slots) {
      if (!candidate || candidate == slot) {
        continue;
      }

      std::lock_guard candidate_lock(candidate->queue_mutex);
      if (candidate->average_weight == 0 ||
          candidate->total_weight < candidate->average_weight + released_slot_total_weight) {
        continue;
      }

      const uint32_t added_credit =
          (candidate->total_weight - released_slot_total_weight) / candidate->average_weight;
      candidate->fairness_credit =
          std::min(candidate->context_count, candidate->fairness_credit + added_credit);
    }
  }

  if (context->worker_slot_index == g_evt.dword_B417C8 && g_evt.dword_B417C4 == context->handle) {
    g_evt.dword_B417C4 = 0;
  }

  context->slot_load_accounted = false;
  context->queued_context_index = kInvalidQueuedContextIndex;
  context->worker_slot_index = kInvalidEvtThreadSlot;
}

bool RescheduleContextOnWorkerSlot(const std::shared_ptr<EvtContextRuntime> &context,
                                   uint32_t wake_tick_ms, uint32_t queued_weight) {
  if (!context || context->worker_slot_index == kInvalidEvtThreadSlot) {
    return false;
  }

  const bool wake_changed = context->ida.next_wake_tick_ms != wake_tick_ms;
  if (wake_changed) {
    context->ida.next_wake_tick_ms = wake_tick_ms;
    if (context->queued_context_index != kInvalidQueuedContextIndex) {
      RemoveContextFromQueuedSlot(context);
    }
  }

  if (context->ida.weight_mirror != queued_weight) {
    const uint32_t active_weight = context->weight;
    context->ida.weight_mirror = queued_weight;
    const uint32_t weight_delta = queued_weight > active_weight ? queued_weight - active_weight
                                                                : active_weight - queued_weight;
    context->load_rebalance_pending = weight_delta >= (active_weight >> 3);
  }

  MirrorLegacyEvtContextSchedulingState(*context);

  const auto current_slot = FindEvtThreadSlot(context->worker_slot_index);
  if (!current_slot) {
    return false;
  }

  bool should_balance = false;
  {
    std::lock_guard lock(current_slot->queue_mutex);
    should_balance = current_slot->fairness_credit != 0 || context->load_rebalance_pending;
  }

  bool queued_on_target_slot = false;
  if (should_balance) {
    if (context->load_rebalance_pending) {
      std::lock_guard lock(current_slot->queue_mutex);
      current_slot->total_weight = current_slot->total_weight > context->weight
                                       ? current_slot->total_weight - context->weight
                                       : 0;
      context->weight = context->ida.weight_mirror;
      context->ida.weight = context->weight;
      current_slot->total_weight += context->weight;
      RecomputeWorkerSlotAverageLocked(*current_slot);
    }

    std::vector<std::shared_ptr<EvtThreadSlotRuntime>> slots;
    {
      std::lock_guard topology_lock(g_evt_mutex);
      slots = g_evt_thread_slots;
    }

    auto target_slot = current_slot;
    uint32_t target_projected_total_weight = 0;
    {
      std::lock_guard lock(current_slot->queue_mutex);
      target_projected_total_weight = current_slot->total_weight;
    }

    for (const auto &candidate : slots) {
      if (!candidate || candidate == current_slot) {
        continue;
      }

      std::lock_guard candidate_lock(candidate->queue_mutex);
      const uint32_t projected_total_weight = candidate->total_weight + context->weight;
      if (projected_total_weight < target_projected_total_weight) {
        target_slot = candidate;
        target_projected_total_weight = projected_total_weight;
      }
    }

    if (target_slot == current_slot) {
      std::lock_guard lock(current_slot->queue_mutex);
      context->load_rebalance_pending = false;
      if (context->weight <= current_slot->average_weight) {
        current_slot->fairness_credit = 0;
      }
    } else {
      if (context->queued_context_index != kInvalidQueuedContextIndex) {
        RemoveContextFromQueuedSlot(context);
      }

      {
        std::lock_guard lock(current_slot->queue_mutex);
        current_slot->total_weight = current_slot->total_weight > context->weight
                                         ? current_slot->total_weight - context->weight
                                         : 0;
        if (current_slot->context_count != 0) {
          --current_slot->context_count;
        }
        RecomputeWorkerSlotAverageLocked(*current_slot);
        if (current_slot->fairness_credit != 0) {
          --current_slot->fairness_credit;
        }
      }

      {
        std::lock_guard lock(target_slot->queue_mutex);
        ++target_slot->context_count;
        target_slot->total_weight += context->weight;
        RecomputeWorkerSlotAverageLocked(*target_slot);
        QueueContextLocked(target_slot, context);
      }

      context->load_rebalance_pending = false;
      queued_on_target_slot = true;
    }
  }

  if (!queued_on_target_slot &&
      (context->queued_context_index == kInvalidQueuedContextIndex || wake_changed)) {
    std::lock_guard lock(current_slot->queue_mutex);
    QueueContextLocked(current_slot, context);
  }

  MirrorLegacyEvtContextSchedulingState(*context);
  return true;
}

bool SubmitContextToWorkerSlot(const std::shared_ptr<EvtContextRuntime> &context,
                               uint32_t wake_tick_ms) {
  if (!context) {
    return false;
  }

  if (context->slot_load_accounted && context->worker_slot_index != kInvalidEvtThreadSlot) {
    return RescheduleContextOnWorkerSlot(context, wake_tick_ms, context->ida.weight_mirror);
  }

  context->ida.next_wake_tick_ms = wake_tick_ms;
  MirrorLegacyEvtContextSchedulingState(*context);

  std::shared_ptr<EvtThreadSlotRuntime> slot;
  {
    std::lock_guard lock(g_evt_mutex);
    if (g_evt_thread_slots.empty()) {
      return false;
    }

    for (const auto &candidate : g_evt_thread_slots) {
      if (!candidate) {
        continue;
      }
      if (!slot || candidate->total_weight < slot->total_weight ||
          (candidate->total_weight == slot->total_weight &&
           candidate->slot_index < slot->slot_index)) {
        slot = candidate;
      }
    }
  }

  if (!slot) {
    return false;
  }

  std::lock_guard lock(slot->queue_mutex);
  if (!context->slot_load_accounted) {
    slot->total_weight += context->weight;
    ++slot->context_count;
    RecomputeWorkerSlotAverageLocked(*slot);
    context->slot_load_accounted = true;
  }
  QueueContextLocked(slot, context);
  MirrorLegacyEvtContextSchedulingState(*context);
  return true;
}

EvtTimerSlot *FindTimerSlot(EvtContextRuntime &context, uint32_t timer_id) {
  if (!context.timer_slots.contains(timer_id) || !context.timer_slots[timer_id]) {
    return nullptr;
  }

  return context.timer_slots[timer_id].get();
}

const EvtTimerSlot *FindTimerSlot(const EvtContextRuntime &context, uint32_t timer_id) {
  if (!context.timer_slots.contains(timer_id) || !context.timer_slots[timer_id]) {
    return nullptr;
  }

  return context.timer_slots[timer_id].get();
}

bool TimerDeadlinePrecedesOrMatches(const EvtContextRuntime &context, uint32_t lhs_timer_id,
                                    uint32_t rhs_timer_id) {
  const auto *lhs = FindTimerSlot(context, lhs_timer_id);
  const auto *rhs = FindTimerSlot(context, rhs_timer_id);
  if (!lhs || !rhs) {
    return false;
  }

  return static_cast<int32_t>(lhs->deadline_tick_ms - rhs->deadline_tick_ms) <= 0;
}

void SetTimerHeapEntryLocked(EvtContextRuntime &context, uint32_t heap_index, uint32_t timer_id) {
  context.timer_heap_ids[heap_index] = timer_id;
  if (auto *slot = FindTimerSlot(context, timer_id)) {
    slot->queued = true;
    slot->timer_heap_index = heap_index;
  }
}

void SiftTimerHeapUpLocked(EvtContextRuntime &context, uint32_t start_index, uint32_t timer_id) {
  uint32_t heap_index = start_index;
  while (heap_index > 0) {
    const uint32_t parent_index = (heap_index - 1) >> 1;
    const uint32_t parent_timer_id = context.timer_heap_ids[parent_index];
    if (TimerDeadlinePrecedesOrMatches(context, parent_timer_id, timer_id)) {
      break;
    }

    SetTimerHeapEntryLocked(context, heap_index, parent_timer_id);
    heap_index = parent_index;
  }

  SetTimerHeapEntryLocked(context, heap_index, timer_id);
}

void SiftTimerHeapDownLocked(EvtContextRuntime &context, uint32_t start_index, uint32_t timer_id) {
  const uint32_t count = static_cast<uint32_t>(context.timer_heap_ids.size());
  uint32_t heap_index = start_index;
  while (count > 1 && heap_index <= ((count - 2) >> 1)) {
    uint32_t child_index = heap_index * 2 + 1;
    if (child_index + 1 < count) {
      const uint32_t left_child_timer_id = context.timer_heap_ids[child_index];
      const uint32_t right_child_timer_id = context.timer_heap_ids[child_index + 1];
      if (TimerDeadlinePrecedesOrMatches(context, right_child_timer_id, left_child_timer_id)) {
        ++child_index;
      }
    }

    const uint32_t child_timer_id = context.timer_heap_ids[child_index];
    if (TimerDeadlinePrecedesOrMatches(context, timer_id, child_timer_id)) {
      break;
    }

    SetTimerHeapEntryLocked(context, heap_index, child_timer_id);
    heap_index = child_index;
  }

  SetTimerHeapEntryLocked(context, heap_index, timer_id);
}

void RemoveTimerHeapIndexLocked(EvtContextRuntime &context, uint32_t removed_index) {
  const uint32_t moved_timer_id = context.timer_heap_ids.back();
  context.timer_heap_ids.pop_back();
  if (removed_index >= context.timer_heap_ids.size()) {
    return;
  }

  if (removed_index > 0) {
    const uint32_t parent_index = (removed_index - 1) >> 1;
    const uint32_t parent_timer_id = context.timer_heap_ids[parent_index];
    if (!TimerDeadlinePrecedesOrMatches(context, parent_timer_id, moved_timer_id)) {
      SiftTimerHeapUpLocked(context, removed_index, moved_timer_id);
      return;
    }
  }

  SiftTimerHeapDownLocked(context, removed_index, moved_timer_id);
}

void RemoveQueuedTimerLocked(EvtContextRuntime &context, uint32_t timer_id) {
  auto *slot = FindTimerSlot(context, timer_id);
  if (!slot) {
    return;
  }

  if (!slot->queued || slot->timer_heap_index == kInvalidTimerHeapIndex ||
      slot->timer_heap_index >= context.timer_heap_ids.size() ||
      context.timer_heap_ids[slot->timer_heap_index] != timer_id) {
    slot->queued = false;
    slot->timer_heap_index = kInvalidTimerHeapIndex;
    return;
  }

  const uint32_t removed_index = slot->timer_heap_index;
  slot->queued = false;
  slot->timer_heap_index = kInvalidTimerHeapIndex;
  RemoveTimerHeapIndexLocked(context, removed_index);
}

void RecycleTimerLocked(EvtContextRuntime &context, uint32_t timer_id) {
  if (!context.timer_slots.contains(timer_id) || !context.timer_slots[timer_id]) {
    return;
  }

  auto &slot = *context.timer_slots[timer_id];
  RemoveQueuedTimerLocked(context, timer_id);
  slot.deadline_tick_ms = 0;
  slot.interval_seconds = 0.0f;
  slot.callback = 0;
  slot.param = 0;
  slot.destroy_callback = 0;
  slot.destroy_arg0 = 0;
  slot.destroy_arg1 = 0;
  slot.destroy_arg2 = 0;
  slot.timer_heap_index = kInvalidTimerHeapIndex;
  context.free_timer_ids.push_back(timer_id);
}

void TrimCancelledTimerHeadLocked(EvtContextRuntime &context) {
  while (!context.timer_heap_ids.empty()) {
    const uint32_t timer_id = context.timer_heap_ids.front();
    if (!context.timer_slots.contains(timer_id) || !context.timer_slots[timer_id]) {
      RemoveTimerHeapIndexLocked(context, 0);
      continue;
    }

    const auto &slot = *context.timer_slots[timer_id];
    if (slot.callback != 0 || slot.destroy_callback != 0) {
      break;
    }

    RecycleTimerLocked(context, timer_id);
  }
}

void InsertQueuedTimerLocked(EvtContextRuntime &context, uint32_t timer_id) {
  auto *slot = FindTimerSlot(context, timer_id);
  if (!slot) {
    return;
  }

  RemoveQueuedTimerLocked(context, timer_id);
  context.timer_heap_ids.push_back(timer_id);
  SiftTimerHeapUpLocked(context, static_cast<uint32_t>(context.timer_heap_ids.size() - 1), timer_id);
  slot->queued = true;
}

uint32_t AdvanceContextTickLocked(EvtContextRuntime &context, double delta_sec) {
  const double total_ms = context.tick_fraction_ms + delta_sec * 1000.0;
  if (total_ms <= 0.0) {
    return context.current_tick_ms;
  }

  const auto elapsed_ms = static_cast<uint32_t>(total_ms);
  context.tick_fraction_ms = total_ms - static_cast<double>(elapsed_ms);
  context.current_tick_ms += elapsed_ms;
  return context.current_tick_ms;
}

std::shared_ptr<EvtHandlerNode> FindNextLinkedHandlerNodeUnlocked(
    std::shared_ptr<EvtHandlerNode> node) {
  while (node && !node->linked) {
    node = node->next;
  }
  return node;
}

void UnlinkHandlerNodeUnlocked(EvtHandlerBucket &bucket,
                               const std::shared_ptr<EvtHandlerNode> &node) {
  if (!node || !node->linked) {
    return;
  }

  const auto prev = node->prev.lock();
  const auto next = node->next;
  if (prev) {
    prev->next = next;
  } else {
    bucket.head = next;
  }
  if (next) {
    next->prev = prev;
  }

  node->linked = false;
  node->prev.reset();
}

void ClearHandlerBucketUnlocked(EvtHandlerBucket &bucket) {
  auto node = bucket.head;
  bucket.head.reset();
  while (node) {
    auto next = node->next;
    node->linked = false;
    node->prev.reset();
    node->next.reset();
    node = std::move(next);
  }
}

void InsertHandlerNodeInIdaOrderUnlocked(EvtHandlerBucket &bucket,
                                         std::shared_ptr<EvtHandlerNode> node) {
  auto current = bucket.head;
  std::shared_ptr<EvtHandlerNode> previous;
  while (current && current->priority > node->priority) {
    previous = current;
    current = current->next;
  }

  node->linked = true;
  node->prev = previous;
  node->next = current;

  if (current) {
    current->prev = node;
  }
  if (previous) {
    previous->next = node;
  } else {
    bucket.head = node;
  }
}

void RegisterHandlerRecord(const std::shared_ptr<EvtContextRuntime> &context, int type,
                           int callback, int param, float priority) {
  if (!context || type < 0 || type >= kEvtHandlerSlotCount) {
    return;
  }

  auto node = std::make_shared<EvtHandlerNode>();
  node->callback = callback;
  node->param = param;
  node->priority = priority;

  std::lock_guard lock(context->handler_mutex);
  auto &bucket = context->handlers[static_cast<std::size_t>(type)];
  InsertHandlerNodeInIdaOrderUnlocked(bucket, std::move(node));
}

bool DispatchHandlers(const std::shared_ptr<EvtContextRuntime> &context, int event_type,
                      void *payload) {
  if (!context || event_type < 0 || event_type >= kEvtHandlerSlotCount) {
    return true;
  }

  std::shared_ptr<EvtHandlerNode> current;
  {
    std::lock_guard lock(context->handler_mutex);
    current = FindNextLinkedHandlerNodeUnlocked(
        context->handlers[static_cast<std::size_t>(event_type)].head);
  }
  if (!current) {
    return true;
  }

  ScopedEvtContext scope(&context->tls_binding);
  while (current) {
    int callback = 0;
    int param = 0;
    std::shared_ptr<EvtHandlerNode> next;
    {
      std::lock_guard lock(context->handler_mutex);

      next = current->next;
      if (current->linked) {
        callback = current->callback;
        param = current->param;
      }
    }

    if (callback != 0) {
      const auto address = ResolveLegacyCallbackAddress(callback);
      if (address != 0) {
        auto handler = reinterpret_cast<RawEventCallback>(address);
        if (handler(payload, param) == 0) {
          return false;
        }
      }
    }

    {
      std::lock_guard lock(context->handler_mutex);
      current = FindNextLinkedHandlerNodeUnlocked(std::move(next));
    }
  }

  return true;
}

void RunContextFrameTick(const std::shared_ptr<EvtContextRuntime> &context, double delta_sec) {
  if (!context) {
    return;
  }

  uint32_t current_tick_ms = 0;
  {
    std::lock_guard lock(context->timer_mutex);
    current_tick_ms = AdvanceContextTickLocked(*context, delta_sec);
    context->ida.tick_ms = current_tick_ms;
  }
  MirrorLegacyEvtContextState(*context);

  EnsureContextEnterHandlersDispatched(context);

  EvtFrameDispatchArgs frame_args{};
  frame_args.delta_seconds = static_cast<float>(delta_sec);
  frame_args.current_tick_ms = current_tick_ms;

  (void)DispatchHandlers(context, kFrameDispatchTypePreTimers, &frame_args);
  EvtTimer_ProcessDueTimers(static_cast<int>(context->handle), current_tick_ms);
  DispatchPostTimerHandlersIfActive(context);
  DispatchQueuedContextEvents(context);
  DispatchPostQueueHandlers(context, frame_args);
  DispatchPostQueueCompletionIfActive(context);
}

void RegisterFramePump(const std::shared_ptr<EvtContextRuntime> &context) {
  if (!context || context->frame_pump_handle != CallbackHandle::Invalid) {
    return;
  }

  const uint32_t handle = context->handle;
  context->frame_pump_handle = FrameScheduler::Instance().Register(
      Phase::LateUpdate, 1000,
      [handle](double delta_sec) {
        const auto runtime = FindEvtContext(handle);
        if (!runtime) {
          return;
        }
        RunContextFrameTick(runtime, delta_sec);
        if (IsEvtContextShutdownRequested(runtime)) {
          FinalizeEvtContext(runtime);
        }
      },
      "EvtContext_FrameTick");
}

void EnsureContextEnterHandlersDispatched(const std::shared_ptr<EvtContextRuntime> &context) {
  if (!context || context->enter_dispatched) {
    return;
  }

  {
    std::lock_guard lock(context->timer_mutex);
    context->current_tick_ms = GameClock::GetTickCount32();
    context->ida.tick_ms = context->current_tick_ms;
  }

  context->enter_dispatched = true;
  MirrorLegacyEvtContextState(*context);
  (void)DispatchHandlers(context, kEnterHandlerType, nullptr);
}

bool SetEvtContextShutdownRequested(const std::shared_ptr<EvtContextRuntime> &context) {
  if (!context) {
    return false;
  }

  std::lock_guard lock(context->state_mutex);
  if (context->state == kEvtContextStateActive && !context->finalize_started) {
    context->state = kEvtContextStateShutdownRequested;
    MirrorLegacyEvtContextState(*context);
  }
  return true;
}

bool IsEvtContextShutdownRequested(const std::shared_ptr<EvtContextRuntime> &context) {
  if (!context) {
    return false;
  }

  std::lock_guard lock(context->state_mutex);
  return context->state == kEvtContextStateShutdownRequested;
}

void DispatchPostTimerHandlersIfActive(const std::shared_ptr<EvtContextRuntime> &context) {
  if (!context) {
    return;
  }

  {
    std::lock_guard lock(context->state_mutex);
    if (context->state == kEvtContextStateShutdownRequested) {
      return;
    }
  }

  (void)DispatchHandlers(context, kFrameDispatchTypePostTimers, nullptr);
}

void DispatchPostQueueHandlers(const std::shared_ptr<EvtContextRuntime> &context,
                               EvtFrameDispatchArgs frame_args) {
  if (!context) {
    return;
  }

  {
    std::lock_guard lock(context->state_mutex);
    if (context->state == kEvtContextStateShutdownRequested) {
      return;
    }

    if ((context->ida.flags_word & 2u) != 0) {
      context->ida.flags_word |= 4u;
      MirrorLegacyEvtContextState(*context);
    }
  }

  (void)DispatchHandlers(context, kFrameDispatchTypePostQueue, &frame_args);
}

void DispatchPostQueueCompletionIfActive(const std::shared_ptr<EvtContextRuntime> &context) {
  if (!context) {
    return;
  }

  bool should_dispatch = false;
  {
    std::lock_guard lock(context->state_mutex);
    if (context->state == kEvtContextStateShutdownRequested) {
      return;
    }

    if ((context->ida.flags_word & 4u) != 0) {
      context->ida.flags_word &= ~4u;
      MirrorLegacyEvtContextState(*context);
      should_dispatch = true;
    }
  }

  if (should_dispatch) {
    (void)DispatchHandlers(context, 23, nullptr);
  }
}

void SignalEventSchedulerWorkerShutdown() {
  SEvent *shutdown_event = nullptr;
  bool signal_worker_slots = false;
  std::vector<std::shared_ptr<EvtThreadSlotRuntime>> worker_slots;
  {
    std::lock_guard lock(g_evt_mutex);
    shutdown_event = g_evt_shutdown_event.get();
    signal_worker_slots = (g_evt.dword_B417E0 == 0);
    if (signal_worker_slots) {
      worker_slots = g_evt_thread_slots;
    }
  }

  if (shutdown_event) {
    shutdown_event->Set();
  }
  if (!signal_worker_slots) {
    return;
  }

  for (const auto &slot : worker_slots) {
    if (slot && slot->wake_event) {
      slot->wake_event->Set();
    }
  }
}

void FinalizeContextForShutdown(const std::shared_ptr<EvtContextRuntime> &context) {
  if (!context) {
    return;
  }

  (void)SetEvtContextShutdownRequested(context);
  FinalizeEvtContext(context);
}

void FinalizeEvtContext(const std::shared_ptr<EvtContextRuntime> &context) {
  if (!context) {
    return;
  }

  {
    std::lock_guard lock(context->state_mutex);
    if (context->finalize_started) {
      return;
    }
    context->finalize_started = true;
  }

  bool wake_workers = false;
  {
    std::lock_guard lock(g_evt_mutex);
    if ((context->ida.flags_word & 2u) != 0 && g_evt.dword_B417D0 != 0) {
      --g_evt.dword_B417D0;
      wake_workers = (g_evt.dword_B417D0 == 0);
    }
  }

  EnsureContextEnterHandlersDispatched(context);

  if (wake_workers) {
    SignalEventSchedulerWorkerShutdown();
  }

  (void)DispatchHandlers(context, kContextFinalizeHandlerType, nullptr);
  {
    std::lock_guard lock(context->state_mutex);
    context->state = kEvtContextStateFinalized;
  }
  MirrorLegacyEvtContextState(*context);
  DispatchQueuedContextEvents(context);

  (void)DispatchHandlers(context, kExitHandlerType, nullptr);
  (void)EvtContext_DeletingDtor(context->legacy_storage,
                                static_cast<char>(context->owns_legacy_storage ? 1 : 0));
}

void DispatchQueuedContextEvents(const std::shared_ptr<EvtContextRuntime> &context) {
  if (!context) {
    return;
  }

  for (;;) {
    QueuedEvtContextEvent queued_event;
    {
      std::lock_guard lock(context->state_mutex);
      if (context->queued_events.empty()) {
        break;
      }

      queued_event = std::move(context->queued_events.front());
      context->queued_events.pop_front();
    }

    DispatchImmediateContextEvent(context, static_cast<int>(queued_event.event_type),
                                  queued_event.payload_data());
  }
}

intptr_t ResolveLegacyCallbackAddress(int callback) {
#if INTPTR_MAX <= INT_MAX
  return static_cast<intptr_t>(callback);
#else
  std::lock_guard lock(g_evt_callback_mutex);
  const auto it = g_evt_legacy_callbacks.find(callback);
  if (it == g_evt_legacy_callbacks.end()) {
    return 0;
  }
  return it->second;
#endif
}

EvtWindowTimerCallback GetEvtWindowTimerCallback() {
  std::lock_guard lock(g_evt_window_timer_callback_mutex);
  return g_evt_window_timer_callback;
}

void SetEvtWindowTimerCallback(EvtWindowTimerCallback callback) {
  std::lock_guard lock(g_evt_window_timer_callback_mutex);
  g_evt_window_timer_callback = callback;
}

void CaptureStartupInputState() {
  openwow::platform::SystemMouseSpeedController::Instance().BootstrapFromSystemDefault();
#if defined(_WIN32)
  g_evt_startup_num_lock_state = ::GetAsyncKeyState(VK_NUMLOCK);
  g_evt_startup_num_lock_state_captured = true;
#endif
}

void RestoreStartupKeyboardToggleStateIfNeeded() {
#if defined(_WIN32)
  if (!g_evt_startup_num_lock_state_captured) {
    return;
  }

  if (::GetAsyncKeyState(VK_NUMLOCK) == g_evt_startup_num_lock_state) {
    return;
  }

  INPUT input{};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = VK_NUMLOCK;
  (void)::SendInput(1, &input, sizeof(input));
#endif
}

void EvtSched_PumpActiveContextFromWindowTimer() {
  const auto context = FindEvtContext(g_evt.dword_B417C4);
  if (!context) {
    return;
  }

  uint32_t previous_tick_ms = 0;
  {
    std::lock_guard lock(context->timer_mutex);
    previous_tick_ms = context->current_tick_ms;
  }

  const uint32_t current_tick_ms = GameClock::GetTickCount32();
  const double delta_sec = static_cast<double>(current_tick_ms - previous_tick_ms) * 0.001;
  RunContextFrameTick(context, delta_sec);
  if (IsEvtContextShutdownRequested(context)) {
    FinalizeEvtContext(context);
  }
}

#if defined(_WIN32)
void CALLBACK EvtSched_WindowTimerThunk(HWND , UINT , UINT_PTR ,
                                        DWORD ) {
  if (const auto callback = GetEvtWindowTimerCallback(); callback != nullptr) {
    callback();
  }
}

LRESULT CALLBACK EvtSched_WindowTimerHookWndProc(HWND hwnd, UINT msg, WPARAM w_param,
                                                 LPARAM l_param) {
  WNDPROC previous_wndproc = nullptr;
  {
    std::lock_guard lock(g_evt_window_timer_hook_mutex);
    previous_wndproc = g_evt_window_timer_hook_prev_wndproc;
  }

  if (previous_wndproc == nullptr) {
    return ::DefWindowProcA(hwnd, msg, w_param, l_param);
  }

  if (msg == kWindowTimerCallbackMessage0 || msg == kWindowTimerCallbackMessage1 ||
      msg == kWindowTimerCallbackMessage2) {
    UINT_PTR timer_id = 0;
    if (GetEvtWindowTimerCallback() != nullptr) {
      timer_id = ::SetTimer(hwnd, 0, kWindowTimerPeriodMs, &EvtSched_WindowTimerThunk);
    }

    const LRESULT result = ::CallWindowProcA(previous_wndproc, hwnd, msg, w_param, l_param);
    if (timer_id != 0) {
      ::KillTimer(hwnd, 0);
    }
    return result;
  }

  return ::CallWindowProcA(previous_wndproc, hwnd, msg, w_param, l_param);
}
#endif

}

uint32_t EvtContextTls_GetCurrentHandle() {
  return EvtContextTls_GetIndexedDword(0);
}

void *EvtContext_Dtor(void *ctx) {
  if (ctx == nullptr) {
    return nullptr;
  }

  const auto context = FindEvtContextByLegacyStorage(ctx);
  if (!context) {
    ResetLegacyEvtContextHandlerBuckets(ctx);
    ResetLegacyEvtContextQueuedMessageState(ctx);
    ResetLegacyEvtContextRepeatKeyState(ctx);
    ResetLegacyEvtContextTimerWords(ctx);
    return ctx;
  }

  DestroyEvtContextRuntime(context, false);
  return ctx;
}

void *EvtContext_DeletingDtor(void *ctx, char delete_flag) {
  if (ctx == nullptr) {
    return nullptr;
  }

  const auto context = FindEvtContextByLegacyStorage(ctx);
  if (!context) {
    EvtContext_Dtor(ctx);
    return ctx;
  }

  DestroyEvtContextRuntime(context, (delete_flag & 1) != 0);
  return ctx;
}

void EvtSched_SignalWorkerShutdown() {
  SignalEventSchedulerWorkerShutdown();
}

void InitEvtSchedulerConfig_CaptureStartupInputState() {
  CaptureStartupInputState();
}

void InitEvtSchedulerConfig(uint32_t thread_count, int32_t a2) {

  InitEvtSchedulerConfig_CaptureStartupInputState();

  uint32_t count = thread_count;
  if (count == 0)
    count = 1;

  EvtSched_Init(count, a2);
  SetEvtWindowTimerCallback(&EvtSched_PumpActiveContextFromWindowTimer);
}

void EvtSched_ClearWindowTimerCallback() {
  SetEvtWindowTimerCallback(nullptr);
}

void EvtSched_AttachWindowTimerHook(void *native_window_handle) {
#if defined(_WIN32)
  auto *hwnd = static_cast<HWND>(native_window_handle);
  if (hwnd == nullptr) {
    return;
  }

  std::lock_guard lock(g_evt_window_timer_hook_mutex);
  if (g_evt_window_timer_hook_hwnd == hwnd && g_evt_window_timer_hook_prev_wndproc != nullptr) {
    return;
  }

  if (g_evt_window_timer_hook_hwnd != nullptr && g_evt_window_timer_hook_prev_wndproc != nullptr) {
    (void)::SetWindowLongPtrA(g_evt_window_timer_hook_hwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(g_evt_window_timer_hook_prev_wndproc));
  }

  const LONG_PTR previous =
      ::SetWindowLongPtrA(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(&EvtSched_WindowTimerHookWndProc));
  if (previous == 0) {
    return;
  }

  g_evt_window_timer_hook_hwnd = hwnd;
  g_evt_window_timer_hook_prev_wndproc = reinterpret_cast<WNDPROC>(previous);
#else
  (void)native_window_handle;
#endif
}

void EvtSched_RestoreStartupInputStateAfterAudioShutdown() {
  RestoreStartupKeyboardToggleStateIfNeeded();
  openwow::platform::SystemMouseSpeedController::Instance().RestoreOriginalSpeedIfWindowActive();
  g_input_button_mask = 0;
  g_input_state_bits = 0;
}

int InitEventScheduler_Thunk() {
  return InitEventScheduler();
}

int InitEventScheduler() {
  {
    std::lock_guard lock(g_evt_mutex);
    if (g_evt_ready_event) {
      g_evt_ready_event->Set();
    }
    SyncEvtSchedulerInitBackingPointersLocked();
  }
  int result = EvtSched_WorkerThreadProc(1, 0);
  {
    std::lock_guard lock(g_evt_mutex);
    g_evt.dword_B417C8 = 0;
  }
  return result;
}

int EvtSched_WorkerThreadProc(uint32_t flags, int32_t ) {
  SEvent *ready_event = nullptr;
  int slot_index = 0;
  if (flags != 0) {
    std::lock_guard lock(g_evt_mutex);
    slot_index = static_cast<int>(g_evt.dword_B417C8);
    ready_event = g_evt_ready_event.get();
  } else {
    slot_index = EvtThread_CreateOrReuse();
    std::lock_guard lock(g_evt_mutex);
    ready_event = g_evt_ready_event.get();
  }
  SetEvtContextTlsValue(nullptr);
  if (ready_event) {
    (void)ready_event->Wait(0xFFFFFFFF);
  }

  (void)slot_index;
  return 0;
}

int CreateEventContext_Thunk(int a1, int a2, int a3, int a4, uint32_t a5) {
  return CreateEventContext(a1, a2, a3, a4, a5);
}

int EvtContext_RequestShutdown(uint32_t context_handle) {
  uint32_t resolved_handle = context_handle;
  if (resolved_handle == 0) {
    resolved_handle = EvtContextTls_GetCurrentHandle();
  }

  const auto context = FindEvtContext(resolved_handle);
  if (!context) {
    return 0;
  }

  return SetEvtContextShutdownRequested(context) ? static_cast<int>(resolved_handle) : 0;
}

int EvtWindow_InvokeTerminationCallback() {
  EvtWindowTerminationCallback callback = nullptr;
  int callback_param = 0;
  {
    std::lock_guard lock(g_evt_window_termination_callback_mutex);
    callback = g_evt_window_termination_callback;
    callback_param = g_evt_window_termination_callback_param;
  }

  if (callback == nullptr) {
    return 1;
  }

  return callback(callback_param);
}

EvtWindowTerminationCallback EvtWindow_SetTerminationCallback(EvtWindowTerminationCallback callback,
                                                              int param) {
  {
    std::lock_guard lock(g_evt_window_termination_callback_mutex);
    g_evt_window_termination_callback = callback;
    g_evt_window_termination_callback_param = param;
  }

  return callback;
}

EvtWindowTerminationCallback
EvtWindow_SetTerminationCallback_Thunk(EvtWindowTerminationCallback callback, int param) {
  return EvtWindow_SetTerminationCallback(callback, param);
}

bool EvtContext_PostEventPayload(uint32_t context_handle, uint32_t event_type, const void *payload,
                                 std::size_t payload_size) {
  uint32_t resolved_handle = context_handle;
  if (resolved_handle == 0) {
    resolved_handle = EvtContextTls_GetCurrentHandle();
  }

  const auto context = FindEvtContext(resolved_handle);
  if (!context) {
    return false;
  }

  {
    std::lock_guard lock(context->state_mutex);
    if (context->state == kEvtContextStateFinalized) {
      return false;
    }
  }

  QueuedEvtContextEvent queued_event{};
  queued_event.event_type = event_type;
  queued_event.AllocatePayloadStorage(payload_size);
  if (payload_size != 0 && payload != nullptr) {
    std::memcpy(queued_event.payload_data(), payload, payload_size);
  }

  std::lock_guard lock(context->state_mutex);
  context->queued_events.push_back(std::move(queued_event));
  return true;
}

void EvtContext_RegisterCurrentHandler(uint32_t event_type, int callback, int param,
                                       float priority) {
  if (event_type >= static_cast<uint32_t>(kEvtHandlerSlotCount) || callback == 0) {
    return;
  }

  const auto context = FindEvtContext(EvtContextTls_GetCurrentHandle());
  if (!context) {
    return;
  }

  EvtContext_RegisterHandler(reinterpret_cast<void *>(static_cast<std::uintptr_t>(context->handle)),
                             static_cast<int>(event_type), callback, param, priority);
}

bool EvtContext_UnregisterCurrentHandlers(int event_type, int callback, int param,
                                          uint8_t match_flags) {
  const auto context = FindEvtContext(EvtContextTls_GetCurrentHandle());
  if (!context) {
    return false;
  }

  bool removed_any = false;
  std::lock_guard lock(context->handler_mutex);
  for (int index = 0; index < kEvtHandlerSlotCount; ++index) {
    if ((match_flags & 0x1u) != 0 && index != event_type) {
      continue;
    }

    auto &bucket = context->handlers[static_cast<std::size_t>(index)];
    for (auto node = bucket.head; node; node = node->next) {
      if (!node->linked) {
        continue;
      }
      if ((match_flags & 0x2u) != 0 && node->callback != callback) {
        continue;
      }
      if ((match_flags & 0x4u) != 0 && node->param != param) {
        continue;
      }

      UnlinkHandlerNodeUnlocked(bucket, node);
      removed_any = true;
    }
  }

  return removed_any;
}

bool EvtContext_UnregisterCurrentHandler(uint32_t event_id, int callback) {
  return EvtContext_UnregisterCurrentHandlers(static_cast<int>(event_id), callback, 0, 0xFFu);
}

int EvtSched_RegisterLegacyCallback(intptr_t callback_address) {
  if (callback_address == 0) {
    return 0;
  }

#if INTPTR_MAX <= INT_MAX
  return static_cast<int>(callback_address);
#else
  std::lock_guard lock(g_evt_callback_mutex);
  for (const auto &[token, existing] : g_evt_legacy_callbacks) {
    if (existing == callback_address) {
      return token;
    }
  }

  const int token = g_next_evt_callback_token++;
  g_evt_legacy_callbacks[token] = callback_address;
  return token;
#endif
}

int CreateEventContext(int interactive, int enter_cb, int exit_cb, int idle_time, uint32_t flags) {
  if (idle_time == 0)
    idle_time = 1;

  void *context_storage = SMemAlloc(kLegacyEvtContextSize, ".\\EvtSched.cpp", 0x3AD, 0);
  if (context_storage != nullptr) {
    context_storage = EvtContext_Init(context_storage, interactive != 0 ? 2 : 0, idle_time,
                                      interactive != 0 ? 1000 : 1, nullptr,
                                      static_cast<int>((flags >> 1) & 1u));

    std::lock_guard lock(g_evt_mutex);
    const auto context = ResolveEvtContextForSubmissionLocked(context_storage);
    if (context) {
      context->owns_legacy_storage = true;
    }
  }

  if (interactive) {
    std::lock_guard lock(g_evt_mutex);
    ++g_evt.dword_B417D0;
  }

  if (enter_cb != 0) {
    EvtContext_RegisterHandler(context_storage, kEnterHandlerType, enter_cb, 0, 1000.0f);
  }
  if (exit_cb != 0) {
    EvtContext_RegisterHandler(context_storage, kExitHandlerType, exit_cb, 0, 1000.0f);
  }

  const int handle = EvtSched_SubmitContext(context_storage);
  if (handle != 0) {
    const auto runtime = FindEvtContext(static_cast<uint32_t>(handle));
    if (runtime) {
      SetEvtContextTlsValue(&runtime->tls_binding);
    }
  }
  return handle;
}

void *EvtContext_Init(void *ctx, int interactive, int idle_time, int weight, void *debug_context,
                      int flags) {
  if (!ctx)
    return nullptr;

  auto context = std::make_shared<EvtContextRuntime>();
  context->legacy_storage = ctx;
  context->profile_object = Prop_Alloc();
  context->ida.tick_ms = GameClock::GetTickCount32();
  context->ida.flags_word = static_cast<uint32_t>(interactive);
  context->ida.idle_time = static_cast<uint32_t>(idle_time);
  context->ida.idle_time_mirror = static_cast<uint32_t>(idle_time);
  context->ida.weight = static_cast<uint32_t>(weight);
  context->ida.weight_mirror = static_cast<uint32_t>(weight);
  context->ida.flags = static_cast<uint32_t>(flags);
  context->current_tick_ms = context->ida.tick_ms;
  context->weight = static_cast<uint32_t>(weight);
  context->state = kEvtContextStateActive;

  std::memset(ctx, 0, kLegacyEvtContextSize);
  for (int slot = 0; slot < kEvtHandlerSlotCount; ++slot) {
    InitializeLegacyIntrusiveListHead(ctx, kLegacyEvtContextBucketTableOffset +
                                               static_cast<std::size_t>(slot) *
                                                   kLegacyEvtContextBucketStride);
  }
  InitializeLegacySentinelPair(ctx, kLegacyEvtContextQueuedMessageListHeadOffset,
                               kLegacyEvtContextQueuedMessageListTailOffset);
  InitializeLegacySentinelPair(ctx, kLegacyEvtContextRepeatKeyDownListHeadOffset,
                               kLegacyEvtContextRepeatKeyDownListTailOffset);
  StoreLegacyU32(ctx, kLegacyEvtContextTickOffset, context->ida.tick_ms);
  StoreLegacyU32(ctx, kLegacyEvtContextFlagsOffset, context->ida.flags_word);
  StoreLegacyU32(ctx, kLegacyEvtContextIdleTimeOffset, context->ida.idle_time);
  StoreLegacyU32(ctx, kLegacyEvtContextIdleTimeMirrorOffset, context->ida.idle_time_mirror);
  StoreLegacyU32(ctx, kLegacyEvtContextWeightOffset, context->ida.weight);
  StoreLegacyU32(ctx, kLegacyEvtContextWeightMirrorOffset, context->ida.weight_mirror);
  StoreLegacyU32(ctx, kLegacyEvtContextNextWakeTickOffset, 0);
  StoreLegacyU32(ctx, kLegacyEvtContextLoadRebalancePendingOffset, 0);
  StoreLegacyU32(ctx, kLegacyEvtContextQueuedMessageListRootOffset, 4);
  StoreLegacyU32(ctx, kLegacyEvtContextQueuedMessageListCapacityOffset, 4);
  StoreLegacyU32(ctx, kLegacyEvtContextProfileObjectOffset,
                 ToLegacyPointerWord(context->profile_object));
  StoreLegacyU32(ctx, kLegacyEvtContextDebugContextOffset, ToLegacyPointerWord(debug_context));
  StoreLegacyU32(ctx, kLegacyEvtContextFlagsArgOffset, context->ida.flags);

  {
    std::lock_guard lock(g_evt_mutex);
    g_evt_contexts_by_legacy_ptr[ctx] = context;
  }

  return ctx;
}

void EvtContext_RegisterHandler(void *ctx, int type, int callback, int param, float priority) {
  if (ctx == nullptr) {
    SErrSetLastError(87);
    return;
  }

  std::shared_ptr<EvtContextRuntime> context;
  {
    std::lock_guard lock(g_evt_mutex);
    context = ResolveEvtContextForSubmissionLocked(ctx);
  }
  if (!context || type < 0 || type >= kEvtHandlerSlotCount) {
    return;
  }
  RegisterHandlerRecord(context, type, callback, param, priority);
}

void EvtSched_Init(uint32_t thread_count, int32_t a2) {
  std::lock_guard lock(g_evt_mutex);

  if (g_evt.dword_B417D4 != 0) {
  }

  ResetEvtSchedulerInitSliceLocked();
  g_evt.dword_B417E0 = static_cast<uint32_t>(a2);
  g_evt.dword_B417CC = static_cast<uint32_t>(SThread_GetCurrentPriority());

  uint32_t rounded = 1;
  while (rounded < thread_count && rounded != 0) {
    rounded *= 2;
  }

  g_evt.dword_B417D4 = rounded;
  g_evt.dword_B417C4 = 0;
  g_evt_thread_slots.assign(rounded, {});
  g_evt_slot_critical_sections.clear();
  g_evt_slot_critical_sections.reserve(rounded);
  for (uint32_t slot_index = 0; slot_index < rounded; ++slot_index) {
    g_evt_slot_critical_sections.push_back(std::make_unique<SCritSect>());
  }
  ResetEvtSchedulerEvent(g_evt_ready_event);
  ResetEvtSchedulerEvent(g_evt_shutdown_event);
  SyncEvtSchedulerInitBackingPointersLocked();

  g_evt.dword_B417C8 = static_cast<uint32_t>(EvtThread_CreateOrReuseLocked());

  while (g_evt.dword_B41814 + 1 < thread_count) {
    auto *slot = AppendEvtWorkerThreadHandleSlotLocked();
    *slot = std::make_unique<EvtWorkerThreadHandleSlot>();
    (void)EvtThread_CreateOrReuseLocked();
  }
}

void EvtSched_Shutdown() {
  uint32_t slot_count = 0;
  {
    std::lock_guard lock(g_evt_mutex);
    slot_count = static_cast<uint32_t>(g_evt_thread_slots.size());
  }

  for (uint32_t slot_index = 0; slot_index < slot_count; ++slot_index) {
    auto drained_contexts = DrainQueuedContextsForSlotShutdown(slot_index);
    if (const auto slot = FindEvtThreadSlot(slot_index)) {
      DestroyEvtThreadSlotRuntime(slot_index, slot);
    }
    for (const auto &context : drained_contexts) {
      FinalizeContextForShutdown(context);
    }
  }

  std::vector<std::shared_ptr<EvtContextRuntime>> contexts;
  std::vector<std::shared_ptr<EvtContextRuntime>> orphaned_legacy_contexts;
  {
    std::lock_guard lock(g_evt_mutex);
    contexts.reserve(g_evt_contexts.size());
    for (const auto &[_, context] : g_evt_contexts) {
      contexts.push_back(context);
    }
    orphaned_legacy_contexts.reserve(g_evt_contexts_by_legacy_ptr.size());
    for (const auto &[_, context] : g_evt_contexts_by_legacy_ptr) {
      if (context && context->handle == 0) {
        orphaned_legacy_contexts.push_back(context);
      }
    }
  }

  for (const auto &context : contexts) {
    FinalizeContextForShutdown(context);
  }
  for (const auto &context : orphaned_legacy_contexts) {
    (void)EvtContext_DeletingDtor(context->legacy_storage,
                                  static_cast<char>(context->owns_legacy_storage ? 1 : 0));
  }

  std::lock_guard lock(g_evt_mutex);

  g_primary_evt_context_handle = 0;
  g_next_evt_context_handle = 0;
  g_evt_contexts.clear();
  g_pending_evt_contexts.clear();
  g_evt_context_handles_by_ptr.clear();
  g_evt_contexts_by_legacy_ptr.clear();
  g_evt_thread_slots.clear();
  g_evt_slot_critical_sections.clear();
  g_evt_worker_thread_handles.clear();
  g_evt_ready_event.reset();
  g_evt_shutdown_event.reset();
  SetEvtContextTlsValue(nullptr);

  g_evt.dword_B417C4 = 0;
  g_evt.dword_B417D4 = 0;
  g_evt.dword_B417D8 = nullptr;
  g_evt.dword_B417DC = nullptr;
  g_evt.dword_B417D0 = 0;
  g_evt.dword_B417CC = 0;
  g_evt.dword_B417E0 = 0;
  g_evt.dword_B41810 = 0;
  g_evt.dword_B41814 = 0;
  g_evt.dword_B41818 = nullptr;
  g_evt.dword_B4181C = 0;
  g_evt.dword_B41820 = 0;
  g_evt.dword_B41824 = 0;
  g_cached_active_window_client_rect = {};
}

int EvtSched_SubmitContext(void *ctx) {
  std::shared_ptr<EvtContextRuntime> context;
  uint32_t handle = 0;
  bool newly_registered = false;
  {
    std::lock_guard lock(g_evt_mutex);
    context = ResolveEvtContextForSubmissionLocked(ctx);
    if (!context) {
      return 0;
    }
    handle = RegisterEvtContextLocked(context, &newly_registered);
  }

  if (!context) {
    return 0;
  }

  if (!SubmitContextToWorkerSlot(context, GameClock::GetTickCount32())) {
    if (newly_registered) {
      DestroyEvtContextRuntime(context, true);
    }
    return 0;
  }

  RegisterFramePump(context);
  return static_cast<int>(handle);
}

int EvtSched_SubmitContext_callee_47E3D0(void * , void *ctx) {
  std::lock_guard lock(g_evt_mutex);
  const auto context = ResolveEvtContextForSubmissionLocked(ctx);
  if (!context) {
    return 0;
  }
  return static_cast<int>(RegisterEvtContextLocked(context));
}

void *EvtSched_Shutdown_callee_47DE50(void * ) {
  std::lock_guard lock(g_evt_mutex);
  return AppendEvtWorkerThreadHandleSlotLocked();
}

int EvtSched_ShutdownWaitProc(void *event_handle) {
  auto *shutdown_event = static_cast<SEvent *>(event_handle);
  while (shutdown_event->Wait(0) != 0) {
    (void)openwow::platform::StormSleep(100);
  }
  return 0;
}

void ScheduleEvent(uint32_t event_id, int callback) {
  EvtContext_RegisterCurrentHandler(event_id, callback, 0, 0.0f);
}

int EvtThread_CreateOrReuseLocked() {
  if (g_evt_thread_slots.empty()) {
    return 0;
  }

  uint32_t selected_slot = 0;
  for (uint32_t slot_index = 0; slot_index < static_cast<uint32_t>(g_evt_thread_slots.size());
       ++slot_index) {
    if (!g_evt_thread_slots[slot_index]) {
      selected_slot = slot_index;
      break;
    }

    const auto selected = g_evt_thread_slots[selected_slot];
    const auto candidate = g_evt_thread_slots[slot_index];
    if (selected && candidate && candidate->refcount >= selected->refcount) {
      continue;
    }

    selected_slot = slot_index;
  }

  auto &slot = g_evt_thread_slots[selected_slot];
  if (!slot) {
    slot = std::make_shared<EvtThreadSlotRuntime>();
    slot->slot_index = selected_slot;
    slot->wake_event = std::make_unique<SEvent>(false, false);
  }

  ++slot->refcount;
  SyncEvtSchedulerInitBackingPointersLocked();
  return static_cast<int>(selected_slot);
}

int EvtThread_CreateOrReuse() {
  std::lock_guard lock(g_evt_mutex);
  return EvtThread_CreateOrReuseLocked();
}

int EvtThread_Destroy(void * , void *block) {
  uint32_t slot_index = 0;
  std::shared_ptr<EvtThreadSlotRuntime> slot;
  if (!FindEvtThreadSlotByOpaquePointer(block, &slot_index, &slot)) {
    return 0;
  }

  DestroyEvtThreadSlotRuntime(slot_index, slot);
  return 0;
}

void EvtThreadQueue_Destroy(void *queue) {
  std::shared_ptr<EvtThreadSlotRuntime> slot;
  if (!FindEvtThreadSlotByOpaquePointer(queue, nullptr, &slot)) {
    return;
  }

  ClearDestroyedEvtThreadQueue(slot);
}

bool FindEvtContextHandlerBucketByLegacyListRoot(
    void *list_root, std::shared_ptr<EvtContextRuntime> *context_out, std::size_t *bucket_index_out) {
  if (list_root == nullptr) {
    return false;
  }

  auto *const list_root_bytes = static_cast<std::byte *>(list_root);

  std::lock_guard lock(g_evt_mutex);
  for (const auto &[storage, context] : g_evt_contexts_by_legacy_ptr) {
    auto *const storage_bytes = static_cast<std::byte *>(storage);
    if (list_root_bytes < storage_bytes) {
      continue;
    }
    const auto offset = static_cast<std::size_t>(list_root_bytes - storage_bytes);
    if (offset < kLegacyEvtContextBucketTableOffset ||
        offset >= kLegacyEvtContextBucketTableOffset +
                      static_cast<std::size_t>(kEvtHandlerSlotCount) * kLegacyEvtContextBucketStride ||
        ((offset - kLegacyEvtContextBucketTableOffset) % kLegacyEvtContextBucketStride) != 0) {
      continue;
    }

    if (context_out != nullptr) {
      *context_out = context;
    }
    if (bucket_index_out != nullptr) {
      *bucket_index_out =
          (offset - kLegacyEvtContextBucketTableOffset) / kLegacyEvtContextBucketStride;
    }
    return true;
  }
  return false;
}

void EvtHandler_DestroyAll(void *list) {
  std::shared_ptr<EvtContextRuntime> context;
  std::size_t bucket_index = 0;
  if (!FindEvtContextHandlerBucketByLegacyListRoot(list, &context, &bucket_index) || !context) {
    return;
  }

  {
    std::lock_guard lock(context->handler_mutex);
    ClearHandlerBucketUnlocked(context->handlers[bucket_index]);
  }

  if (context->legacy_storage != nullptr) {
    InitializeLegacyIntrusiveListHead(
        context->legacy_storage, kLegacyEvtContextBucketTableOffset +
                                     bucket_index * kLegacyEvtContextBucketStride);
  }
}

int EvtHandler_Destroy(void *list, void *block) {
  std::shared_ptr<EvtContextRuntime> context;
  std::size_t bucket_index = 0;
  if (!FindEvtContextHandlerBucketByLegacyListRoot(list, &context, &bucket_index) || !context ||
      block == nullptr) {
    return 0;
  }

  std::shared_ptr<EvtHandlerNode> next_node;
  bool bucket_empty = false;
  {
    std::lock_guard lock(context->handler_mutex);
    auto &bucket = context->handlers[bucket_index];
    auto current = bucket.head;
    while (current && current.get() != block) {
      current = current->next;
    }
    if (!current) {
      return 0;
    }

    next_node = current->next;
    UnlinkHandlerNodeUnlocked(bucket, current);
    current->next.reset();
    bucket_empty = bucket.head == nullptr;
  }

  if (context->legacy_storage != nullptr && bucket_empty) {
    InitializeLegacyIntrusiveListHead(
        context->legacy_storage, kLegacyEvtContextBucketTableOffset +
                                     bucket_index * kLegacyEvtContextBucketStride);
  }

#if INTPTR_MAX <= INT_MAX
  return static_cast<int>(reinterpret_cast<std::intptr_t>(next_node.get()));
#else
  return 0;
#endif
}

std::shared_ptr<EvtContextRuntime> FindEvtContextByLegacyListRoot(void *list_root,
                                                                  std::size_t list_offset) {
  if (list_root == nullptr) {
    return {};
  }

  auto *const legacy_storage = static_cast<std::byte *>(list_root) - list_offset;
  std::lock_guard lock(g_evt_mutex);
  const auto it = g_evt_contexts_by_legacy_ptr.find(legacy_storage);
  if (it == g_evt_contexts_by_legacy_ptr.end()) {
    return {};
  }
  return it->second;
}

void EvtMessage_Destroy(void *list) {
  const auto context =
      FindEvtContextByLegacyListRoot(list, kLegacyEvtContextQueuedMessageListRootOffset);
  if (!context) {
    return;
  }

  {
    std::lock_guard lock(context->state_mutex);
    context->queued_events.clear();
  }

  if (context->legacy_storage != nullptr) {
    InitializeLegacySentinelPair(context->legacy_storage, kLegacyEvtContextQueuedMessageListHeadOffset,
                                 kLegacyEvtContextQueuedMessageListTailOffset);
    StoreLegacyU32(context->legacy_storage, kLegacyEvtContextQueuedMessageListRootOffset, 4);
  }
}

void EvtKeyDown_Destroy(void *list) {
  const auto context =
      FindEvtContextByLegacyListRoot(list, kLegacyEvtContextRepeatKeyDownListRootOffset);
  if (!context) {
    return;
  }

  {
    std::lock_guard lock(context->state_mutex);
    context->repeat_tracked_button_downs.clear();
  }

  if (context->legacy_storage != nullptr) {
    StoreLegacyU32(context->legacy_storage, kLegacyEvtContextRepeatKeyDownListRootOffset, 0);
    InitializeLegacySentinelPair(context->legacy_storage,
                                 kLegacyEvtContextRepeatKeyDownListHeadOffset,
                                 kLegacyEvtContextRepeatKeyDownListTailOffset);
  }
}

static void EvtContext_DispatchInputModeChanged(int ctx, uint32_t input_mode) {
  g_input_mode = input_mode;

  auto payload = BuildImmediatePointerPayloadBase(0);
  DispatchImmediateContextEvent(ctx, kImmediateInputModeChangedEventType, payload);
}

static void EvtContext_DispatchInputReset(int ctx, int event_word_0) {
  g_input_button_mask = 0;
  g_input_state_bits = 0;

  int payload_word = event_word_0;
  DispatchImmediateContextEvent(ctx, kImmediateInputResetEventType, &payload_word);

  CheckActiveMaskAfterButtonRelease();
}

void EvtContext_UpdateInputModeForActiveMask(int ctx, int input_mode, int active_mask) {
  if (!ctx) {
    SetEvtContextInvalidParameterError();
    return;
  }

  if (active_mask == (active_mask & static_cast<int>(g_input_button_mask))) {
    g_input_active_mask = static_cast<uint32_t>(active_mask);
    if (static_cast<uint32_t>(input_mode) != g_input_mode) {
      SetRelativeCursorMode(input_mode != 0);
      EvtContext_DispatchInputModeChanged(ctx, static_cast<uint32_t>(input_mode));
    }
  }
}

void push_context(void *event_data, int event_type, void *result, int ctx) {
  if (!ctx) {
    SetEvtContextInvalidParameterError();
    return;
  }

  switch (event_type) {
  case 0: {

    auto *event_words = static_cast<int *>(event_data);
    const int screen_x = event_words[1];
    const int screen_y = event_words[2];
    while (g_input_button_mask != 0) {
      const uint32_t lowest_bit = g_input_button_mask & (0u - g_input_button_mask);
      const auto timestamp = static_cast<float>(GameClock::GetTickCount32());
      DispatchSingleMouseButtonRelease(ctx, lowest_bit, screen_x, screen_y, timestamp, true);
    }
    return;
  }
  case 1: {
    auto *event_words = static_cast<const uint32_t *>(event_data);
    DispatchImmediateCharEvent(ctx, event_words[0], event_words[1]);
    return;
  }
  case 2: {
    const auto &char_sequence = *static_cast<const EvtImmediateUtf16CharSequenceEvent *>(event_data);
    DispatchImmediateUtf16CharSequence(ctx, char_sequence.characters,
                                       char_sequence.character_count);
    return;
  }
  case 5:
    if (EvtWindow_InvokeTerminationCallback() != 0) {
      EvtSched_SignalWorkerShutdown();
      if (auto *result_word = static_cast<int *>(result); result_word != nullptr) {
        *result_word = 1;
      }
    }
    return;
  case 6: {
    auto *event_words = static_cast<int *>(event_data);
    EvtContext_DispatchInputReset(ctx, event_words[0]);
    return;
  }
  case 19:
    if (EvtWindow_InvokeTerminationCallback() != 0) {
      if (auto *result_word = static_cast<int *>(result); result_word != nullptr) {
        *result_word = 1;
      }
    }
    return;
  case 7: {
    auto *event_words = static_cast<int *>(event_data);
    DispatchImmediateButtonTransition(ctx, kImmediateButtonDownEventType, event_words[0],
                                      event_words[1], event_words[3], true);
    return;
  }
  case 8: {
    auto *event_words = static_cast<int *>(event_data);
    DispatchImmediateButtonTransition(ctx, kImmediateButtonUpEventType, event_words[0],
                                      event_words[1], event_words[3], false);
    return;
  }
  case 9: {
    auto *event_words = static_cast<int *>(event_data);
    const auto button_mask = static_cast<uint32_t>(event_words[0]);
    g_input_button_mask |= button_mask;
    auto payload = BuildImmediateScreenPointPayload(button_mask, event_words[1], event_words[2],
                                                    0.0f,
                                                    ResolveMouseDispatchTimestampWord(event_words));

    DispatchImmediateContextEvent(ctx, kImmediateMouseDownEventType, payload);
    return;
  }
  case 10: {

    auto *event_words = static_cast<int *>(event_data);
    auto payload = BuildImmediateScreenPointPayload(
        0, event_words[1], event_words[2], 0.0f,
        ResolveMouseDispatchTimestampWord(event_words));
    DispatchImmediateContextEvent(ctx, kImmediateMouseUpEventType, payload);
    return;
  }
  case 13: {
    auto *event_words = static_cast<int *>(event_data);
    const auto button_mask = static_cast<uint32_t>(event_words[0]);
    DispatchSingleMouseButtonRelease(ctx, button_mask, event_words[1], event_words[2],
                                     ResolveMouseDispatchTimestampWord(event_words), false);
    return;
  }
  case 11: {
    auto *event_words = static_cast<int *>(event_data);
    float wheel_delta = 0.0f;
    std::memcpy(&wheel_delta, event_words, sizeof(wheel_delta));

    auto payload =
        BuildImmediateScreenPointPayload(0, event_words[1], event_words[2], wheel_delta,
                                         ResolveMouseDispatchTimestampWord(event_words));

    DispatchImmediateContextEvent(ctx, kImmediateMouseWheelEventType, payload);
    return;
  }
  case 12: {
    auto *event_words = static_cast<int *>(event_data);
    auto payload = BuildImmediatePointerPayloadBase(0);
    payload.normalized_x = static_cast<float>(event_words[1]);
    payload.normalized_y = static_cast<float>(event_words[2]);
    payload.timestamp = ResolveMouseDispatchTimestampWord(event_words);

    DispatchImmediateContextEvent(ctx, kImmediateMouseMoveEventType, payload);
    return;
  }
  case 14: {

    auto *event_words = static_cast<int *>(event_data);
    DispatchImmediateMouseDeltaMove(ctx, static_cast<uint32_t>(event_words[0]),
                                    event_words[1], event_words[2],
                                    static_cast<uint32_t>(event_words[3]));
    return;
  }
  case 3: {

    auto *event_words = static_cast<uint32_t *>(event_data);
    EvtImmediateImeCompositionPayload payload{};
    payload.word_0 = event_words[0];
    payload.word_1 = event_words[1];
    payload.word_2 = event_words[2];
    payload.code_page = GetActiveCodePage();
    DispatchImmediateContextEvent(ctx, kImmediateImeCompositionEventType, payload);
    return;
  }
  case 4: {

    auto *event_words = static_cast<uint32_t *>(event_data);
    EvtImmediateImeCandidatePayload payload{};
    payload.word_0 = event_words[0];
    payload.word_1 = event_words[1];
    DispatchImmediateContextEvent(ctx, kImmediateImeCandidateEventType, payload);
    return;
  }
  case 15: {

    auto *event_words = static_cast<int *>(event_data);
    DispatchImmediateAxisEvent(ctx, 0, static_cast<uint32_t>(event_words[0]),
                               event_words[1], static_cast<uint32_t>(event_words[3]));
    return;
  }
  case 16: {
    auto *event_words = static_cast<uint32_t *>(event_data);
    EvtImmediateFocusPayload payload{};
    payload.value = event_words[0];
    payload.flag = 1;
    payload.timestamp = event_words[3];
    DispatchImmediateContextEvent(ctx, kImmediateFocusGainedEventType, payload);
    return;
  }
  case 17: {
    auto *event_words = static_cast<uint32_t *>(event_data);
    EvtImmediateFocusPayload payload{};
    payload.value = event_words[0];
    payload.flag = 0;
    payload.timestamp = event_words[3];
    DispatchImmediateContextEvent(ctx, kImmediateFocusLostEventType, payload);
    return;
  }
  case 18: {
    auto *event_words = static_cast<uint32_t *>(event_data);
    EvtImmediateFocusPayload payload{};
    payload.value = event_words[0];
    payload.flag = event_words[1];
    payload.timestamp = event_words[3];
    DispatchImmediateContextEvent(ctx, kImmediateFocusDataEventType, payload);
    return;
  }
  default:
    return;
  }
}

void EvtTimerSlotPointerArray_SetCapacity(EvtTimerSlotPointerArray &array, uint32_t new_capacity) {
  array.set_capacity(new_capacity);
}

void EvtTimer_GrowArray(void *arr, uint32_t index, int zero_init) {
  auto *array = static_cast<EvtTimerSlotPointerArray *>(arr);
  if (!array) {
    return;
  }

  const uint32_t new_count = index + 1;
  if (index >= array->count()) {
    if (new_count > array->capacity()) {
      const uint32_t growth_quantum = array->resolve_auto_grow_quantum(new_count);
      uint32_t rounded_capacity = new_count;
      const uint32_t remainder = new_count % growth_quantum;
      if (remainder != 0) {
        rounded_capacity = new_count + growth_quantum - remainder;
      }

      EvtTimerSlotPointerArray_SetCapacity(*array, rounded_capacity);
    }

    if (zero_init != 0) {
      for (uint32_t entry_index = array->count(); entry_index < new_count; ++entry_index) {
        (*array)[entry_index].reset();
      }
    }

    array->set_count(new_count);
  }
}

uint32_t EvtTimer_RegisterCurrentContext(uint32_t interval_ms, int callback, int param) {
  const uint32_t context_handle = EvtContextTls_GetCurrentHandle();
  if (context_handle == 0) {
    return 0;
  }

  return EvtTimer_Register(static_cast<int>(context_handle), interval_ms, callback, param, 0, 0, 0,
                           0);
}

int EvtTimer_CancelCurrentContext(uint32_t timer_id, int callback, const char * ) {
  (void)callback;
  const auto context = FindEvtContext(EvtContextTls_GetCurrentHandle());
  if (!context || timer_id == 0) {
    return 0;
  }

  std::lock_guard lock(context->timer_mutex);
  if (!context->timer_slots.contains(timer_id) || !context->timer_slots[timer_id]) {
    return 0;
  }

  auto &slot = *context->timer_slots[timer_id];
  slot.callback = 0;
  slot.destroy_callback = 0;
  TrimCancelledTimerHeadLocked(*context);

  return 1;
}

int32_t EvtContext_GetMsUntilNextTimer(int ctx, uint32_t current_tick_ms) {
  const auto context = FindEvtContext(static_cast<uint32_t>(ctx));
  if (!context) {
    return 0;
  }

  std::lock_guard lock(context->timer_mutex);
  if (context->timer_heap_ids.empty()) {
    return -1;
  }

  const uint32_t root_timer_id = context->timer_heap_ids.front();
  const auto *slot = FindTimerSlot(*context, root_timer_id);
  if (!slot) {
    return -1;
  }

  const int32_t delta = static_cast<int32_t>(slot->deadline_tick_ms - current_tick_ms);
  return delta < 0 ? 0 : delta;
}

bool EvtTimer_ProcessDueTimers(int ctx, uint32_t current_tick_ms) {
  const auto context = FindEvtContext(static_cast<uint32_t>(ctx));
  if (!context) {
    return false;
  }

  bool fired_any = false;
  for (;;) {
    EvtTimerSlot slot_snapshot;
    bool have_callback = false;

    {
      std::lock_guard lock(context->timer_mutex);
      TrimCancelledTimerHeadLocked(*context);
      if (context->timer_heap_ids.empty()) {
        break;
      }

      const uint32_t timer_id = context->timer_heap_ids.front();
      if (!context->timer_slots.contains(timer_id) || !context->timer_slots[timer_id]) {
        RemoveTimerHeapIndexLocked(*context, 0);
        continue;
      }

      const auto &slot = *context->timer_slots[timer_id];
      if (static_cast<int32_t>(slot.deadline_tick_ms - current_tick_ms) > 0) {
        break;
      }

      slot_snapshot = slot;
      have_callback = slot.callback != 0 || slot.destroy_callback != 0;
      RecycleTimerLocked(*context, timer_id);
    }

    if (!have_callback) {
      continue;
    }

    fired_any = true;
    ScopedEvtContext scope(&context->tls_binding);
    EvtTimerDispatchArgs args;
    args.interval_seconds = slot_snapshot.interval_seconds;
    args.current_tick_ms = current_tick_ms;

    if (slot_snapshot.callback != 0) {
      const auto address = ResolveLegacyCallbackAddress(slot_snapshot.callback);
      if (address != 0) {
        auto callback = reinterpret_cast<RawTimerCallback>(address);
        callback(&args, slot_snapshot.param);
      }
      continue;
    }

    const auto address = ResolveLegacyCallbackAddress(slot_snapshot.destroy_callback);
    if (address != 0) {
      auto destroy_callback = reinterpret_cast<RawDestroyCallback>(address);
      destroy_callback(&args, slot_snapshot.destroy_arg0, slot_snapshot.destroy_arg1,
                       slot_snapshot.destroy_arg2);
    }
  }

  return fired_any;
}

uint32_t EvtTimer_Register(int ctx, uint32_t interval_ms, int callback, int param, int destroy_cb,
                           int a6, int a7, int a8) {
  const auto context = FindEvtContext(static_cast<uint32_t>(ctx));
  if (!context) {
    return 0;
  }

  if (!callback && !destroy_cb) {
    return 0;
  }

  std::lock_guard lock(context->timer_mutex);

  uint32_t timer_id = 0;
  if (!context->free_timer_ids.empty()) {
    timer_id = context->free_timer_ids.back();
    context->free_timer_ids.pop_back();
  } else {
    timer_id = context->next_timer_id++;
    EvtTimer_GrowArray(&context->timer_slots, timer_id, 1);
  }

  auto &slot_ptr = context->timer_slots[timer_id];
  if (!slot_ptr) {
    slot_ptr = std::make_unique<EvtTimerSlot>();
  }

  auto &slot = *slot_ptr;
  slot.id = timer_id;
  slot.deadline_tick_ms = context->current_tick_ms + interval_ms;
  slot.interval_seconds = static_cast<float>(interval_ms) * 0.001f;
  slot.callback = callback;
  slot.param = param;
  slot.destroy_callback = destroy_cb;
  slot.destroy_arg0 = a6;
  slot.destroy_arg1 = a7;
  slot.destroy_arg2 = a8;
  InsertQueuedTimerLocked(*context, timer_id);
  return timer_id;
}

namespace detail {

void EvtSched_SetContextCurrentTickForTests(uint32_t context_handle, uint32_t current_tick_ms) {
  const auto context = FindEvtContext(context_handle);
  if (!context) {
    return;
  }

  std::lock_guard lock(context->timer_mutex);
  context->current_tick_ms = current_tick_ms;
  context->tick_fraction_ms = 0.0;
  context->ida.tick_ms = current_tick_ms;
  if (context->legacy_storage != nullptr) {
    StoreLegacyU32(context->legacy_storage, kLegacyEvtContextTickOffset, current_tick_ms);
  }
}

void EvtSched_SetContextWakeTickForTests(uint32_t context_handle, uint32_t wake_tick_ms) {
  const auto context = FindEvtContext(context_handle);
  if (!context) {
    return;
  }

  (void)RescheduleContextOnWorkerSlot(context, wake_tick_ms, context->ida.weight_mirror);
}

void EvtSched_SetContextQueuedWeightForTests(uint32_t context_handle, uint32_t queued_weight) {
  const auto context = FindEvtContext(context_handle);
  if (!context) {
    return;
  }

  (void)RescheduleContextOnWorkerSlot(context, context->ida.next_wake_tick_ms, queued_weight);
}

uint32_t EvtSched_DequeueContextForSlotForTests(uint32_t slot_index) {
  return DequeueQueuedContextFromSlot(slot_index);
}

uint32_t EvtSched_GetCurrentPrimarySlotContextForTests() {
  return g_evt.dword_B417C4;
}

std::size_t EvtSched_GetQueuedContextCountForTests(uint32_t slot_index) {
  const auto slot = FindEvtThreadSlot(slot_index);
  if (!slot) {
    return 0;
  }

  std::lock_guard lock(slot->queue_mutex);
  return slot->queued_context_handles.size();
}

uint32_t EvtSched_GetContextWorkerSlotForTests(uint32_t context_handle) {
  const auto context = FindEvtContext(context_handle);
  return context ? context->worker_slot_index : kInvalidEvtThreadSlot;
}

uint32_t EvtSched_GetContextActiveWeightForTests(uint32_t context_handle) {
  const auto context = FindEvtContext(context_handle);
  return context ? context->weight : 0;
}

uint32_t EvtSched_GetContextQueuedWeightForTests(uint32_t context_handle) {
  const auto context = FindEvtContext(context_handle);
  return context ? context->ida.weight_mirror : 0;
}

bool EvtSched_IsContextLoadRebalancePendingForTests(uint32_t context_handle) {
  const auto context = FindEvtContext(context_handle);
  return context ? context->load_rebalance_pending : false;
}

uint32_t EvtSched_GetWorkerSlotTotalWeightForTests(uint32_t slot_index) {
  const auto slot = FindEvtThreadSlot(slot_index);
  if (!slot) {
    return 0;
  }

  std::lock_guard lock(slot->queue_mutex);
  return slot->total_weight;
}

uint32_t EvtSched_GetWorkerSlotAverageWeightForTests(uint32_t slot_index) {
  const auto slot = FindEvtThreadSlot(slot_index);
  if (!slot) {
    return 0;
  }

  std::lock_guard lock(slot->queue_mutex);
  return slot->average_weight;
}

uint32_t EvtSched_GetWorkerSlotContextCountForTests(uint32_t slot_index) {
  const auto slot = FindEvtThreadSlot(slot_index);
  if (!slot) {
    return 0;
  }

  std::lock_guard lock(slot->queue_mutex);
  return slot->context_count;
}

uint32_t EvtSched_GetWorkerSlotFairnessCreditForTests(uint32_t slot_index) {
  const auto slot = FindEvtThreadSlot(slot_index);
  if (!slot) {
    return 0;
  }

  std::lock_guard lock(slot->queue_mutex);
  return slot->fairness_credit;
}

bool EvtSched_IsContextRegisteredForTests(uint32_t context_handle) {
  return static_cast<bool>(FindEvtContext(context_handle));
}

bool EvtSched_IsContextInteractiveForTests(uint32_t context_handle) {
  const auto context = FindEvtContext(context_handle);
  if (!context) {
    return false;
  }

  return (context->ida.flags_word >> 1) != 0;
}

uint32_t EvtSched_GetContextFlagsWordForTests(uint32_t context_handle) {
  const auto context = FindEvtContext(context_handle);
  if (!context || context->legacy_storage == nullptr) {
    return 0;
  }

  return LoadLegacyU32(context->legacy_storage, kLegacyEvtContextFlagsOffset);
}

uint32_t EvtSched_GetRoundedSlotCountForTests() {
  return g_evt.dword_B417D4;
}

bool EvtSched_HasWorkerSlotForTests(uint32_t slot_index) {
  return static_cast<bool>(FindEvtThreadSlot(slot_index));
}

uint32_t EvtSched_GetMainWorkerSlotForTests() {
  return g_evt.dword_B417C8;
}

uint32_t EvtSched_GetWorkerThreadHandleCountForTests() {
  return g_evt.dword_B41814;
}

uint32_t EvtSched_GetWorkerThreadHandleCapacityForTests() {
  return g_evt.dword_B41810;
}

uint32_t EvtSched_GetWorkerThreadHandleGrowthQuantumForTests() {
  return g_evt.dword_B4181C;
}

void EvtSched_SetWorkerThreadHandleCapacityForTests(uint32_t new_capacity) {
  std::lock_guard lock(g_evt_mutex);
  EvtSched_ResizeWorkerHandleArrayLocked(new_capacity);
}

void EvtSched_SetWorkerThreadHandleValueForTests(uint32_t slot_index, std::uintptr_t value) {
  std::lock_guard lock(g_evt_mutex);
  if (slot_index >= g_evt.dword_B41814 ||
      slot_index >= static_cast<uint32_t>(g_evt_worker_thread_handles.size())) {
    return;
  }

  auto &slot = g_evt_worker_thread_handles[slot_index];
  if (!slot) {
    slot = std::make_unique<EvtWorkerThreadHandleSlot>();
  }
  slot->handle = reinterpret_cast<void *>(value);
}

std::uintptr_t EvtSched_GetWorkerThreadHandleValueForTests(uint32_t slot_index) {
  std::lock_guard lock(g_evt_mutex);
  if (slot_index >= g_evt.dword_B41814 ||
      slot_index >= static_cast<uint32_t>(g_evt_worker_thread_handles.size())) {
    return 0;
  }

  const auto &slot = g_evt_worker_thread_handles[slot_index];
  if (!slot) {
    return 0;
  }

  return reinterpret_cast<std::uintptr_t>(slot->handle);
}

uint32_t EvtSched_GetSavedThreadPriorityForTests() {
  return g_evt.dword_B417CC;
}

uint32_t EvtSched_GetInitModeForTests() {
  return g_evt.dword_B417E0;
}

bool EvtSched_HasWindowTimerCallbackForTests() {
  return GetEvtWindowTimerCallback() != nullptr;
}

bool EvtSched_IsShutdownEventSignaledForTests() {
  std::lock_guard lock(g_evt_mutex);
  return g_evt_shutdown_event && g_evt_shutdown_event->IsSignaled();
}

bool EvtSched_IsWorkerWakeEventSignaledForTests(uint32_t slot_index) {
  const auto slot = FindEvtThreadSlot(slot_index);
  return slot && slot->wake_event && slot->wake_event->IsSignaled();
}

bool EvtSched_DestroyWorkerSlotForTests(uint32_t slot_index) {
  const auto slot = FindEvtThreadSlot(slot_index);
  if (!slot) {
    return false;
  }

  DestroyEvtThreadSlotRuntime(slot_index, slot);
  return true;
}

}

}

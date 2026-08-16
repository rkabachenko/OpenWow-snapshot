
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace openwow::core {

struct EvtSchedGlobals {
  uint32_t dword_B417C4 = 0;
  uint32_t dword_B417D4 = 0;
  void *dword_B417D8 = nullptr;
  void *dword_B417DC = nullptr;
  uint32_t dword_B417CC = 0;
  uint32_t dword_B417C8 = 0;
  uint32_t dword_B417D0 = 0;
  uint32_t dword_B417E0 = 0;
  uint32_t dword_B41810 = 0;
  uint32_t dword_B41814 = 0;
  void *dword_B41818 = nullptr;
  uint32_t dword_B4181C = 0;
  uint32_t dword_B41820 = 0;
  uint32_t dword_B41824 = 0;
  std::atomic<int32_t> dword_AC0DC0{0};
};

struct EvtContextIdaState {
  uint32_t tick_ms = 0;
  uint32_t next_wake_tick_ms = 0;
  uint32_t flags_word = 0;
  uint32_t idle_time = 0;
  uint32_t idle_time_mirror = 0;
  uint32_t weight = 0;
  uint32_t weight_mirror = 0;
  uint32_t flags = 0;
};

using EvtWindowTerminationCallback = int (*)(int param);

struct EvtTimerDispatchArgs {
  float interval_seconds = 0.0f;
  uint32_t current_tick_ms = 0;
};

void InitEvtSchedulerConfig(uint32_t thread_count, int32_t a2);

void InitEvtSchedulerConfig_CaptureStartupInputState();

void EvtSched_ClearWindowTimerCallback();

void EvtSched_AttachWindowTimerHook(void *native_window_handle);

void EvtSched_RestoreStartupInputStateAfterAudioShutdown();

int InitEventScheduler_Thunk();

int InitEventScheduler();

int EvtSched_WorkerThreadProc(uint32_t flags, int32_t a2);

int CreateEventContext_Thunk(int a1, int a2, int a3, int a4, uint32_t a5);

uint32_t EvtContextTls_GetCurrentHandle();

int EvtContext_RequestShutdown(uint32_t context_handle);

bool EvtContext_PostEventPayload(uint32_t context_handle, uint32_t event_type, const void *payload,
                                 std::size_t payload_size);

void EvtContext_RegisterCurrentHandler(uint32_t event_type, int callback, int param,
                                       float priority);

bool EvtContext_UnregisterCurrentHandlers(int event_type, int callback, int param,
                                          uint8_t match_flags);

int EvtWindow_InvokeTerminationCallback();

EvtWindowTerminationCallback EvtWindow_SetTerminationCallback(EvtWindowTerminationCallback callback,
                                                              int param);

EvtWindowTerminationCallback
EvtWindow_SetTerminationCallback_Thunk(EvtWindowTerminationCallback callback, int param);

int EvtSched_RegisterLegacyCallback(std::intptr_t callback_address);

int CreateEventContext(int interactive, int enter_cb, int exit_cb, int idle_time, uint32_t flags);

void *EvtContext_Init(void *ctx, int interactive, int idle_time, int weight, void *debug_context,
                      int flags);

void *EvtContext_Dtor(void *ctx);

void *EvtContext_DeletingDtor(void *ctx, char delete_flag);

void EvtContext_RegisterHandler(void *ctx, int type, int callback, int param, float priority);

void EvtSched_Init(uint32_t thread_count, int32_t a2);

void EvtSched_Shutdown();

int EvtSched_SubmitContext(void *ctx);

int EvtSched_SubmitContext_callee_47E3D0(void *cs, void *ctx);

void *EvtSched_Shutdown_callee_47DE50(void *arr);

int EvtSched_ShutdownWaitProc(void *event_handle);

void EvtSched_SignalWorkerShutdown();

void ScheduleEvent(uint32_t event_id, int callback);

bool EvtContext_UnregisterCurrentHandler(uint32_t event_id, int callback);

int EvtThread_CreateOrReuse();

int EvtThread_Destroy(void *list, void *block);

void EvtThreadQueue_Destroy(void *queue);

void EvtHandler_DestroyAll(void *list);

int EvtHandler_Destroy(void *list, void *block);

void EvtMessage_Destroy(void *list);

void EvtKeyDown_Destroy(void *list);

void EvtContext_UpdateInputModeForActiveMask(int ctx, int input_mode, int active_mask);

void push_context(void *event_data, int event_type, void *result, int ctx);

void EvtTimer_GrowArray(void *arr, uint32_t index, int zero_init);

uint32_t EvtTimer_RegisterCurrentContext(uint32_t interval_ms, int callback, int param);

int EvtTimer_CancelCurrentContext(uint32_t timer_id, int callback, const char *debug_name);

int32_t EvtContext_GetMsUntilNextTimer(int ctx, uint32_t current_tick_ms);

bool EvtTimer_ProcessDueTimers(int ctx, uint32_t current_tick_ms);

uint32_t EvtTimer_Register(int ctx, uint32_t interval_ms, int callback, int param, int destroy_cb,
                           int a6, int a7, int a8);

namespace detail {

void EvtSched_SetContextCurrentTickForTests(uint32_t context_handle, uint32_t current_tick_ms);
void EvtSched_SetContextWakeTickForTests(uint32_t context_handle, uint32_t wake_tick_ms);
void EvtSched_SetContextQueuedWeightForTests(uint32_t context_handle, uint32_t queued_weight);
uint32_t EvtSched_DequeueContextForSlotForTests(uint32_t slot_index);
uint32_t EvtSched_GetCurrentPrimarySlotContextForTests();
std::size_t EvtSched_GetQueuedContextCountForTests(uint32_t slot_index);
uint32_t EvtSched_GetContextWorkerSlotForTests(uint32_t context_handle);
uint32_t EvtSched_GetContextActiveWeightForTests(uint32_t context_handle);
uint32_t EvtSched_GetContextQueuedWeightForTests(uint32_t context_handle);
bool EvtSched_IsContextLoadRebalancePendingForTests(uint32_t context_handle);
uint32_t EvtSched_GetWorkerSlotTotalWeightForTests(uint32_t slot_index);
uint32_t EvtSched_GetWorkerSlotAverageWeightForTests(uint32_t slot_index);
uint32_t EvtSched_GetWorkerSlotContextCountForTests(uint32_t slot_index);
uint32_t EvtSched_GetWorkerSlotFairnessCreditForTests(uint32_t slot_index);
bool EvtSched_IsContextRegisteredForTests(uint32_t context_handle);
bool EvtSched_IsContextInteractiveForTests(uint32_t context_handle);
uint32_t EvtSched_GetContextFlagsWordForTests(uint32_t context_handle);
uint32_t EvtSched_GetRoundedSlotCountForTests();
bool EvtSched_HasWorkerSlotForTests(uint32_t slot_index);
uint32_t EvtSched_GetMainWorkerSlotForTests();
uint32_t EvtSched_GetWorkerThreadHandleCountForTests();
uint32_t EvtSched_GetWorkerThreadHandleCapacityForTests();
uint32_t EvtSched_GetWorkerThreadHandleGrowthQuantumForTests();
void EvtSched_SetWorkerThreadHandleCapacityForTests(uint32_t new_capacity);
void EvtSched_SetWorkerThreadHandleValueForTests(uint32_t slot_index, std::uintptr_t value);
std::uintptr_t EvtSched_GetWorkerThreadHandleValueForTests(uint32_t slot_index);
uint32_t EvtSched_GetSavedThreadPriorityForTests();
uint32_t EvtSched_GetInitModeForTests();
bool EvtSched_HasWindowTimerCallbackForTests();
bool EvtSched_IsShutdownEventSignaledForTests();
bool EvtSched_IsWorkerWakeEventSignaledForTests(uint32_t slot_index);
bool EvtSched_DestroyWorkerSlotForTests(uint32_t slot_index);

}

}


#pragma once

#include <cstdint>
#include <atomic>

namespace openwow::data {

struct AsyncFileReadObject {
    uint32_t fileHandle;
    void*    readBuffer;
    uint32_t readSize;
    uint32_t completionContext;
    uint32_t doneCallback;
    uint32_t callbackDone;
    uint32_t ownerQueue;
    uint32_t lastTouchedFrame;
    uint8_t  priority;
    uint8_t  completionDispatched;
    uint8_t  completionPending;
    uint8_t  isActive;
    uint8_t  field_24;
    uint8_t  isSecondaryQueued;
    uint32_t listNext;
    uint32_t listPrev;
};

#if INTPTR_MAX == INT32_MAX
static_assert(sizeof(AsyncFileReadObject) == 48,
              "AsyncFileReadObject must be 48 bytes on 32-bit builds");
#endif

struct CAsyncQueue {
    uint32_t queueListPrevLink;
    uint32_t queueListNextNode;
    uint32_t priorityLinkOffset;
    uint32_t priorityTailLink;
    uint32_t priorityFirstNode;
    uint32_t secondaryLinkOffset;
    uint32_t secondaryTailLink;
    uint32_t secondaryFirstNode;
    uint32_t staleFrameCheck;
};

static_assert(sizeof(CAsyncQueue) == 36, "CAsyncQueue must match the IDA layout");

struct AsyncThreadNode {
    uint32_t       linkPrev = 0;
    uint32_t       linkNext = 0;
    void*          threadHandle = nullptr;
    std::uintptr_t queue = 0;
    uint32_t       activeObjectToken = 0;
};

#if INTPTR_MAX == INT32_MAX
static_assert(sizeof(AsyncThreadNode) == 20,
              "portable AsyncThreadNode must remain compact on 32-bit builds");
#endif

struct StormIntrusiveListRoot {
    uint32_t linkOffset = 0;
    uint32_t tailLink = 0;
    uint32_t firstNode = 0;
};

inline uint8_t g_asyncCritSect[40] = {};

inline void* g_asyncTlsSlot = nullptr;

inline std::atomic<uint32_t> g_asyncShutdownEvent{0};

inline uint32_t g_asyncRateLimit = 0;

inline uint32_t g_asyncCompletionPumpBudgetMs = 0;

inline uintptr_t g_asyncQueues[3] = {};

inline StormIntrusiveListRoot g_asyncQueueListRoot = {};

inline StormIntrusiveListRoot g_asyncThreadListRoot = {};

inline StormIntrusiveListRoot g_asyncCompletionList = {};

inline StormIntrusiveListRoot g_asyncObjectPoolRoot = {};

inline std::atomic<uint32_t> g_asyncCritSectHeld{0};

inline uint32_t g_asyncWaitBeginCallback = 0;

inline uint32_t g_asyncWaitTickCallback = 0;

inline std::int32_t g_asyncWaitAllRemaining = 0;

inline uint32_t g_asyncWaitAllProgressContext = 0;

inline uint32_t g_asyncWaitAllProgressCallback = 0;

inline uint32_t g_asyncWaitObject = 0;

inline uint32_t g_asyncWaitDepth = 0;

inline uint32_t g_asyncPreloadPromotionLastTick = 0;

using AsyncFileReadEventDestroyHook = void (*)(void* event);
using AsyncFileReadMemoryFreeHook = void (*)(void* ptr, const char* file, int line, int flags);
using AsyncFileReadPointerResolveHook = void* (*)(std::uint32_t rawPointer);
using AsyncFileReadPointerEncodeHook = std::uint32_t (*)(const void* nativePointer);
using AsyncFileReadCompletionCallback = void (*)(std::uint32_t context);
using AsyncFileReadIdleCallback = void (*)();
using AsyncFileReadWaitTickCallback = void (*)(float progress, int context);
using AsyncFileWaitAllCountCallback = std::uint32_t (*)();
using AsyncFileWaitAllProgressCallback =
    void (*)(float progress, std::uint32_t context);
using AsyncFileReadThreadCreateHook =
    bool (*)(int (*proc)(void*), void* param, void** outHandle, const char* threadName);
using AsyncFileReadThreadWaitHook = void (*)(void* handle, std::uint32_t timeout);
using AsyncFileReadHandleDestroyHook = void (*)(std::uint32_t handle);
using AsyncFileReadSleepHook = void (*)(std::uint32_t milliseconds);
using AsyncFileReadTickCountHook = std::uint32_t (*)();
using AsyncFileReadReadyStateHook = int (*)(std::uint32_t fileHandle);

void AsyncFileRead_DestroyObject(AsyncFileReadObject* obj);

[[nodiscard]] std::uint32_t StormIntrusiveList_GetNextObjectToken(
    const StormIntrusiveListRoot& list,
    std::uint32_t currentObjectToken);

void AsyncFileRead_DestroyObjectCallback(std::uint32_t context);

[[nodiscard]] std::uint32_t AsyncFileRead_EncodePointerToken(const void* nativePointer);

[[nodiscard]] std::uint32_t AsyncFileRead_EncodeCompletionCallbackToken(
    AsyncFileReadCompletionCallback callback);

int AsyncFileRead_DestroyOrDeflect(AsyncFileReadObject* obj,
                                   AsyncFileReadCompletionCallback callback);

AsyncFileReadObject* CAsyncObject_Allocate();

void CAsyncObject_FreeAll(uint32_t* listHead);

void CAsyncThread_DestroyAll(uint32_t* listHead);

AsyncThreadNode* CAsyncThread_CreateAndStart(CAsyncQueue* queue, const char* threadName);

void SetAsyncFileReadCleanupHooksForTests(AsyncFileReadEventDestroyHook destroyEventHook,
                                          AsyncFileReadMemoryFreeHook freeHook);
void ResetAsyncFileReadCleanupHooksForTests();
void SetAsyncFileReadPointerResolverForTests(AsyncFileReadPointerResolveHook resolver);
void SetAsyncFileReadPointerEncoderForTests(AsyncFileReadPointerEncodeHook encoder);
void SetAsyncFileReadThreadHooksForTests(AsyncFileReadThreadCreateHook createHook,
                                         AsyncFileReadThreadWaitHook waitHook);
void SetAsyncFileReadHandleDestroyHookForTests(AsyncFileReadHandleDestroyHook hook);
void SetAsyncFileReadSleepHookForTests(AsyncFileReadSleepHook hook);
void SetAsyncFileReadTickCountHookForTests(AsyncFileReadTickCountHook hook);
void SetAsyncFileReadReadyStateHookForTests(AsyncFileReadReadyStateHook hook);

void AsyncIO_EnterCriticalSection();

void AsyncIO_LeaveCriticalSection();

[[nodiscard]] void* AsyncFileRead_ResolvePointerToken(std::uint32_t rawPointer);

void AsyncFileRead_MoveToPrimaryQueue(AsyncFileReadObject* object,
                                      bool insertBeforeEqualPriority);

void AsyncFileRead_MoveToSecondaryQueue(AsyncFileReadObject* object,
                                        bool insertBeforeEqualPriority);

[[nodiscard]] bool AsyncFileRead_CanTouchObject(
    const AsyncFileReadObject* object);

void AsyncFileRead_TouchObject(AsyncFileReadObject* object,
                               std::uint32_t currentFrame);

int AsyncFileRead_ThreadProc(uintptr_t threadInfo);

int AsyncFileRead_QueueObject(AsyncFileReadObject* object,
                              bool insertBeforeEqualPriority);

void AsyncFileRead_Wait(AsyncFileReadObject* object);

std::uint32_t AsyncFile_SetWaitCallbacks(AsyncFileReadIdleCallback beginCallback,
                                         AsyncFileReadWaitTickCallback tickCallback);

std::uint32_t AsyncFile_SetWaitAllProgressCallback(
    AsyncFileWaitAllProgressCallback callback,
    std::uint32_t context);

bool AsyncFile_RegisterCompletionPumpCallback(AsyncFileReadIdleCallback callback);

bool AsyncFile_RegisterWaitAllCountCallback(AsyncFileWaitAllCountCallback callback);

int AsyncFileRead_ProcessCompletedCallbacks();

int AsyncFileRead_PromoteReadyStreamingRequests(void* eventData, int param);

void AsyncFileRead_FlushCompletedCallbacks();

[[nodiscard]] bool AsyncFileRead_HasPendingWork();

int AsyncFileRead_WaitAll();

void CAsyncQueue_Destroy(CAsyncQueue* queue, std::uint32_t queueToken);

CAsyncQueue* CAsyncQueue_Create();

void AsyncIO_Initialize(uint32_t rateLimit, uint32_t completionPumpBudgetMs);

void AsyncFile_Shutdown();

}

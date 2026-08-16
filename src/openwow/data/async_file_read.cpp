
#include "openwow/data/async_file_read.h"
#include "openwow/runtime/scheduling/evt_sched.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_alloc.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_intrusive_list.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/streaming_init.h"
#include "openwow/vfs/retail/runtime_file_registry.h"
#include "openwow/vfs/retail/sfile_runtime.h"
#include "openwow/vfs/retail/streaming/data_preload_controller.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace openwow::data {

namespace {

constexpr int kStormDestructorLine = -2;
constexpr int kStormFreeFlags = 0;
constexpr std::uint32_t kStormTaggedPointerMask = 1u;
constexpr std::uint32_t kAsyncReadyPromotionIntervalMs = 250u;

struct AsyncWorkerThreadHandle {
    std::mutex              mutex;
    std::condition_variable finishedCv;
    std::thread             thread;
    bool                    finished = false;
};

struct AsyncFileReadCleanupHooks {
    AsyncFileReadEventDestroyHook destroyEvent = nullptr;
    AsyncFileReadMemoryFreeHook freeMemory = nullptr;
    AsyncFileReadPointerResolveHook resolvePointer = nullptr;
    AsyncFileReadPointerEncodeHook encodePointer = nullptr;
    AsyncFileReadThreadCreateHook createThread = nullptr;
    AsyncFileReadThreadWaitHook waitThread = nullptr;
    AsyncFileReadHandleDestroyHook destroyHandle = nullptr;
    AsyncFileReadSleepHook sleep = nullptr;
    AsyncFileReadTickCountHook tickCount = nullptr;
    AsyncFileReadReadyStateHook readyState = nullptr;
};

struct AsyncPointerRegistry {
    std::mutex                                 mutex;
    std::uint32_t                              nextToken = 0x60000000u;
    std::unordered_map<std::uint32_t, void*>   tokenToPointer;
    std::unordered_map<const void*, std::uint32_t> pointerToToken;
};

struct AsyncCompletionCallbackRegistry {
    std::mutex                                         mutex;
    std::uint32_t                                      nextToken = 0x70000000u;
    std::unordered_map<std::uint32_t, std::uintptr_t>  tokenToCallback;
    std::unordered_map<std::uintptr_t, std::uint32_t>  callbackToToken;
};

struct AsyncProgressCallbackRegistry {
    std::mutex                                         mutex;
    std::uint32_t                                      nextToken = 0x71000000u;
    std::unordered_map<std::uint32_t, std::uintptr_t>  tokenToCallback;
    std::unordered_map<std::uintptr_t, std::uint32_t>  callbackToToken;
};

struct AsyncWaitBeginCallbackRegistry {
    std::mutex                                         mutex;
    std::uint32_t                                      nextToken = 0x72000000u;
    std::unordered_map<std::uint32_t, std::uintptr_t>  tokenToCallback;
    std::unordered_map<std::uintptr_t, std::uint32_t>  callbackToToken;
};

struct AsyncWaitTickCallbackRegistry {
    std::mutex                                         mutex;
    std::uint32_t                                      nextToken = 0x73000000u;
    std::unordered_map<std::uint32_t, std::uintptr_t>  tokenToCallback;
    std::unordered_map<std::uintptr_t, std::uint32_t>  callbackToToken;
};

AsyncFileReadCleanupHooks& MutableAsyncFileReadCleanupHooks() {
    static AsyncFileReadCleanupHooks hooks;
    return hooks;
}

AsyncPointerRegistry& MutableAsyncPointerRegistry() {
    static AsyncPointerRegistry registry;
    return registry;
}

AsyncCompletionCallbackRegistry& MutableAsyncCompletionCallbackRegistry() {
    static AsyncCompletionCallbackRegistry registry;
    return registry;
}

AsyncProgressCallbackRegistry& MutableAsyncProgressCallbackRegistry() {
    static AsyncProgressCallbackRegistry registry;
    return registry;
}

AsyncWaitBeginCallbackRegistry& MutableAsyncWaitBeginCallbackRegistry() {
    static AsyncWaitBeginCallbackRegistry registry;
    return registry;
}

AsyncWaitTickCallbackRegistry& MutableAsyncWaitTickCallbackRegistry() {
    static AsyncWaitTickCallbackRegistry registry;
    return registry;
}

std::vector<AsyncFileReadIdleCallback>& MutableAsyncCompletionPumpCallbacks() {
    static std::vector<AsyncFileReadIdleCallback> callbacks;
    return callbacks;
}

std::vector<AsyncFileWaitAllCountCallback>& MutableAsyncWaitAllCountCallbacks() {
    static std::vector<AsyncFileWaitAllCountCallback> callbacks;
    return callbacks;
}

int LegacyAsyncCompletionPumpCallback() {
    static const int callback = openwow::core::EvtSched_RegisterLegacyCallback(
        reinterpret_cast<std::intptr_t>(&AsyncFileRead_ProcessCompletedCallbacks));
    return callback;
}

int LegacyAsyncReadyPromotionCallback() {
    static const int callback = openwow::core::EvtSched_RegisterLegacyCallback(
        reinterpret_cast<std::intptr_t>(&AsyncFileRead_PromoteReadyStreamingRequests));
    return callback;
}

[[nodiscard]] std::uint32_t EncodeAsyncPointer(const void* nativePointer);
[[nodiscard]] std::uint32_t EncodeAsyncCallback(AsyncFileReadCompletionCallback callback);
[[nodiscard]] AsyncFileReadCompletionCallback ResolveAsyncCallback(std::uint32_t callbackToken);
[[nodiscard]] std::uint32_t EncodeAsyncProgressCallback(
    AsyncFileWaitAllProgressCallback callback);
[[nodiscard]] AsyncFileWaitAllProgressCallback ResolveAsyncProgressCallback(
    std::uint32_t callbackToken);
[[nodiscard]] std::uint32_t EncodeAsyncWaitBeginCallback(
    AsyncFileReadIdleCallback callback);
[[nodiscard]] AsyncFileReadIdleCallback ResolveAsyncWaitBeginCallback(
    std::uint32_t callbackToken);
[[nodiscard]] std::uint32_t EncodeAsyncWaitTickCallback(
    AsyncFileReadWaitTickCallback callback);
[[nodiscard]] AsyncFileReadWaitTickCallback ResolveAsyncWaitTickCallback(
    std::uint32_t callbackToken);

template <typename Callback, typename Registry>
[[nodiscard]] std::uint32_t EncodeAsyncFunctionToken(
    const Callback callback,
    Registry& registry) {
    if (callback == nullptr) {
        return 0;
    }

#if INTPTR_MAX == INT32_MAX
    return static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(callback));
#else
    std::lock_guard lock(registry.mutex);
    const auto key = reinterpret_cast<std::uintptr_t>(callback);
    if (const auto it = registry.callbackToToken.find(key);
        it != registry.callbackToToken.end()) {
        return it->second;
    }

    const auto token = registry.nextToken;
    registry.nextToken += 0x100u;
    registry.tokenToCallback.emplace(token, key);
    registry.callbackToToken.emplace(key, token);
    return token;
#endif
}

template <typename Callback, typename Registry>
[[nodiscard]] Callback ResolveAsyncFunctionToken(
    const std::uint32_t callbackToken,
    Registry& registry) {
    if (callbackToken == 0) {
        return nullptr;
    }

#if INTPTR_MAX == INT32_MAX
    return reinterpret_cast<Callback>(
        static_cast<std::uintptr_t>(callbackToken));
#else
    std::lock_guard lock(registry.mutex);
    const auto it = registry.tokenToCallback.find(callbackToken);
    if (it == registry.tokenToCallback.end()) {
        return nullptr;
    }

    return reinterpret_cast<Callback>(it->second);
#endif
}

[[nodiscard]] std::uint32_t GetAsyncQueueListTailToken() {
    return EncodeAsyncPointer(&g_asyncQueueListRoot.tailLink);
}

[[nodiscard]] std::uint32_t GetAsyncQueueListHeadToken() {
    return EncodeAsyncPointer(&g_asyncQueueListRoot.firstNode);
}

[[nodiscard]] std::uint32_t GetAsyncThreadListTailToken() {
    return EncodeAsyncPointer(&g_asyncThreadListRoot.tailLink);
}

[[nodiscard]] std::uint32_t GetAsyncThreadListHeadToken() {
    return EncodeAsyncPointer(&g_asyncThreadListRoot.firstNode);
}

[[nodiscard]] std::uint32_t GetAsyncObjectPoolTailToken() {
    return EncodeAsyncPointer(&g_asyncObjectPoolRoot.tailLink);
}

[[nodiscard]] std::uint32_t GetAsyncObjectPoolHeadToken() {
    return EncodeAsyncPointer(&g_asyncObjectPoolRoot.firstNode);
}

[[nodiscard]] std::uint32_t GetAsyncCompletionListTailToken() {
    return EncodeAsyncPointer(&g_asyncCompletionList.tailLink);
}

[[nodiscard]] std::uint32_t GetAsyncCompletionListHeadToken() {
    return EncodeAsyncPointer(&g_asyncCompletionList.firstNode);
}

void EnsureAsyncQueueListInitialized() {
    g_asyncQueueListRoot.linkOffset =
        static_cast<std::uint32_t>(offsetof(CAsyncQueue, queueListPrevLink));
    if (g_asyncQueueListRoot.tailLink != 0) {
        return;
    }

    g_asyncQueueListRoot.tailLink = GetAsyncQueueListTailToken();
    g_asyncQueueListRoot.firstNode =
        GetAsyncQueueListHeadToken() | kStormTaggedPointerMask;
}

[[nodiscard]] StormIntrusiveListRoot* GetAsyncQueueListRoot() {
    EnsureAsyncQueueListInitialized();
    return &g_asyncQueueListRoot;
}

void EnsureAsyncThreadListInitialized() {
    g_asyncThreadListRoot.linkOffset =
        static_cast<std::uint32_t>(offsetof(AsyncThreadNode, linkPrev));
    if (g_asyncThreadListRoot.tailLink != 0) {
        return;
    }

    g_asyncThreadListRoot.tailLink = GetAsyncThreadListTailToken();
    g_asyncThreadListRoot.firstNode =
        GetAsyncThreadListHeadToken() | kStormTaggedPointerMask;
}

[[nodiscard]] StormIntrusiveListRoot* GetAsyncThreadListRoot() {
    EnsureAsyncThreadListInitialized();
    return &g_asyncThreadListRoot;
}

void EnsureAsyncObjectPoolInitialized() {
    g_asyncObjectPoolRoot.linkOffset =
        static_cast<std::uint32_t>(offsetof(AsyncFileReadObject, listNext));
    if (g_asyncObjectPoolRoot.tailLink != 0) {
        return;
    }

    g_asyncObjectPoolRoot.tailLink = GetAsyncObjectPoolTailToken();
    g_asyncObjectPoolRoot.firstNode =
        GetAsyncObjectPoolHeadToken() | kStormTaggedPointerMask;
}

[[nodiscard]] StormIntrusiveListRoot* GetAsyncObjectPoolRoot() {
    EnsureAsyncObjectPoolInitialized();
    return &g_asyncObjectPoolRoot;
}

void EnsureAsyncCompletionListInitialized() {
    g_asyncCompletionList.linkOffset =
        static_cast<std::uint32_t>(offsetof(AsyncFileReadObject, listNext));
    if (g_asyncCompletionList.tailLink != 0) {
        return;
    }

    g_asyncCompletionList.tailLink = GetAsyncCompletionListTailToken();
    g_asyncCompletionList.firstNode =
        GetAsyncCompletionListHeadToken() | kStormTaggedPointerMask;
}

[[nodiscard]] StormIntrusiveListRoot* GetAsyncCompletionListRoot() {
    EnsureAsyncCompletionListInitialized();
    return &g_asyncCompletionList;
}

[[nodiscard]] std::uint32_t GetIntrusiveListFront(const StormIntrusiveListRoot& list) {
    if ((list.firstNode & kStormTaggedPointerMask) != 0 || list.firstNode == 0) {
        return 0;
    }
    return list.firstNode;
}

[[nodiscard]] std::uint32_t NormalizeAsyncNode(const std::uint32_t rawNode) {
    if ((rawNode & kStormTaggedPointerMask) != 0 || rawNode == 0) {
        return 0;
    }
    return rawNode;
}

[[nodiscard]] void* ResolveAsyncPointer(const std::uint32_t rawPointer) {
    if (rawPointer == 0) {
        return nullptr;
    }

    if (rawPointer == GetAsyncQueueListTailToken()) {
        return &g_asyncQueueListRoot.tailLink;
    }

    if (rawPointer == GetAsyncQueueListHeadToken()) {
        return &g_asyncQueueListRoot.firstNode;
    }

    if (rawPointer == GetAsyncThreadListTailToken()) {
        return &g_asyncThreadListRoot.tailLink;
    }

    if (rawPointer == GetAsyncThreadListHeadToken()) {
        return &g_asyncThreadListRoot.firstNode;
    }

    if (rawPointer == GetAsyncObjectPoolTailToken()) {
        return &g_asyncObjectPoolRoot.tailLink;
    }

    if (rawPointer == GetAsyncObjectPoolHeadToken()) {
        return &g_asyncObjectPoolRoot.firstNode;
    }

    if (rawPointer == GetAsyncCompletionListTailToken()) {
        return &g_asyncCompletionList.tailLink;
    }

    if (rawPointer == GetAsyncCompletionListHeadToken()) {
        return &g_asyncCompletionList.firstNode;
    }

    auto& hooks = MutableAsyncFileReadCleanupHooks();
    if (hooks.resolvePointer) {
        return hooks.resolvePointer(rawPointer);
    }

#if INTPTR_MAX != INT32_MAX
    auto& registry = MutableAsyncPointerRegistry();
    std::lock_guard lock(registry.mutex);
    if (const auto it = registry.tokenToPointer.find(rawPointer);
        it != registry.tokenToPointer.end()) {
        return it->second;
    }

    const auto baseToken = rawPointer & 0xFFFFFF00u;
    if (baseToken != rawPointer) {
        if (const auto it = registry.tokenToPointer.find(baseToken);
            it != registry.tokenToPointer.end()) {
            auto* const bytePointer = static_cast<std::uint8_t*>(it->second);
            return bytePointer + (rawPointer - baseToken);
        }
    }
#endif

    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(rawPointer));
}

[[nodiscard]] std::uint32_t* ResolveAsyncWordPointer(const std::uint32_t rawPointer) {
    return static_cast<std::uint32_t*>(ResolveAsyncPointer(rawPointer));
}

[[nodiscard]] std::uint32_t EncodeAsyncPointer(const void* nativePointer) {
    if (nativePointer == nullptr) {
        return 0;
    }

    auto& hooks = MutableAsyncFileReadCleanupHooks();
    if (hooks.encodePointer) {
        return hooks.encodePointer(nativePointer);
    }

#if INTPTR_MAX != INT32_MAX
    auto& registry = MutableAsyncPointerRegistry();
    std::lock_guard lock(registry.mutex);
    if (const auto it = registry.pointerToToken.find(nativePointer);
        it != registry.pointerToToken.end()) {
        return it->second;
    }

    const auto token = registry.nextToken;
    registry.nextToken += 0x100u;
    registry.pointerToToken.emplace(nativePointer, token);
    registry.tokenToPointer.emplace(token, const_cast<void*>(nativePointer));
    return token;
#endif

    return static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(nativePointer));
}

[[nodiscard]] std::uint32_t EncodeAsyncCallback(
    const AsyncFileReadCompletionCallback callback) {
    auto& registry = MutableAsyncCompletionCallbackRegistry();
    return EncodeAsyncFunctionToken(callback, registry);
}

[[nodiscard]] AsyncFileReadCompletionCallback ResolveAsyncCallback(
    const std::uint32_t callbackToken) {
    auto& registry = MutableAsyncCompletionCallbackRegistry();
    return ResolveAsyncFunctionToken<AsyncFileReadCompletionCallback>(
        callbackToken, registry);
}

[[nodiscard]] std::uint32_t EncodeAsyncProgressCallback(
    const AsyncFileWaitAllProgressCallback callback) {
    auto& registry = MutableAsyncProgressCallbackRegistry();
    return EncodeAsyncFunctionToken(callback, registry);
}

[[nodiscard]] AsyncFileWaitAllProgressCallback ResolveAsyncProgressCallback(
    const std::uint32_t callbackToken) {
    auto& registry = MutableAsyncProgressCallbackRegistry();
    return ResolveAsyncFunctionToken<AsyncFileWaitAllProgressCallback>(
        callbackToken, registry);
}

[[nodiscard]] std::uint32_t EncodeAsyncWaitBeginCallback(
    const AsyncFileReadIdleCallback callback) {
    auto& registry = MutableAsyncWaitBeginCallbackRegistry();
    return EncodeAsyncFunctionToken(callback, registry);
}

[[nodiscard]] AsyncFileReadIdleCallback ResolveAsyncWaitBeginCallback(
    const std::uint32_t callbackToken) {
    auto& registry = MutableAsyncWaitBeginCallbackRegistry();
    return ResolveAsyncFunctionToken<AsyncFileReadIdleCallback>(
        callbackToken, registry);
}

[[nodiscard]] std::uint32_t EncodeAsyncWaitTickCallback(
    const AsyncFileReadWaitTickCallback callback) {
    auto& registry = MutableAsyncWaitTickCallbackRegistry();
    return EncodeAsyncFunctionToken(callback, registry);
}

[[nodiscard]] AsyncFileReadWaitTickCallback ResolveAsyncWaitTickCallback(
    const std::uint32_t callbackToken) {
    auto& registry = MutableAsyncWaitTickCallbackRegistry();
    return ResolveAsyncFunctionToken<AsyncFileReadWaitTickCallback>(
        callbackToken, registry);
}

void UnlinkStormIntrusiveNode(const std::uint32_t rawLinkPointer) {
    (void)openwow::core::UnlinkStormIntrusiveLink<std::uint32_t>(
        rawLinkPointer,
        [](const std::uint32_t rawPointer) {
            return ResolveAsyncWordPointer(rawPointer);
        });
}

void DestroyAsyncThreadHandle(void* event) {
    auto& hooks = MutableAsyncFileReadCleanupHooks();
    if (hooks.destroyEvent) {
        hooks.destroyEvent(event);
        return;
    }

    if (event == nullptr) {
        return;
    }

    auto* handle = static_cast<AsyncWorkerThreadHandle*>(event);
    {
        std::unique_lock lock(handle->mutex);
        handle->finishedCv.wait(lock, [handle] { return handle->finished; });
    }
    if (handle->thread.joinable()) {
        handle->thread.join();
    }
    delete handle;
}

void DestroyAsyncFileHandle(const std::uint32_t handle) {
    if (handle == 0) {
        return;
    }

    auto& hooks = MutableAsyncFileReadCleanupHooks();
    if (hooks.destroyHandle) {
        hooks.destroyHandle(handle);
        return;
    }

    (void)openwow::vfs::RetailRuntimeFileRegistry().Remove(static_cast<int>(handle));
}

void FreeStormMemory(void* ptr, const char* tag) {
    auto& hooks = MutableAsyncFileReadCleanupHooks();
    if (hooks.freeMemory) {
        hooks.freeMemory(ptr, tag, kStormDestructorLine, kStormFreeFlags);
        return;
    }

    openwow::core::SMemFree(ptr, tag, kStormDestructorLine, kStormFreeFlags);
}

template <typename Node, typename BeforeFreeFn>
void DestroyStormIntrusiveList(StormIntrusiveListRoot* list,
                               const std::size_t linkOffset,
                               const char* allocationTag,
                               BeforeFreeFn beforeFree) {
    while (const auto front = GetIntrusiveListFront(*list)) {
        auto* node = static_cast<Node*>(ResolveAsyncPointer(front));
        beforeFree(node);
        const auto rawLinkPointer = front + static_cast<std::uint32_t>(linkOffset);
        UnlinkStormIntrusiveNode(rawLinkPointer);
        FreeStormMemory(node, allocationTag);
    }
}

void UnlinkStormIntrusiveList(StormIntrusiveListRoot& list) {
    while (const auto front = GetIntrusiveListFront(list)) {
        UnlinkStormIntrusiveNode(front + list.linkOffset);
    }
}

void LinkStormIntrusiveNode(StormIntrusiveListRoot& list,
                            const std::uint32_t nodeToken,
                            const std::uint32_t linkToken) {
    auto* linkWords = ResolveAsyncWordPointer(linkToken);
    if (linkWords == nullptr) {
        return;
    }

    if (linkWords[0] != 0) {
        UnlinkStormIntrusiveNode(linkToken);
    }

    auto* tailLinkWords = ResolveAsyncWordPointer(list.tailLink);
    linkWords[0] = list.tailLink;
    linkWords[1] = tailLinkWords[1];
    tailLinkWords[1] = nodeToken;
    list.tailLink = linkToken;
}

std::recursive_mutex& AsyncCritSectMutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

void SleepAsyncSpin(const std::uint32_t milliseconds) {
    if (auto& hooks = MutableAsyncFileReadCleanupHooks(); hooks.sleep) {
        hooks.sleep(milliseconds);
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

[[nodiscard]] std::uint32_t GetAsyncTickCount() {
    if (auto& hooks = MutableAsyncFileReadCleanupHooks(); hooks.tickCount) {
        return hooks.tickCount();
    }

    return openwow::core::GameClock::GetTickCount32();
}

[[nodiscard]] int QueryAsyncReadyState(const std::uint32_t fileHandle) {
    if (auto& hooks = MutableAsyncFileReadCleanupHooks(); hooks.readyState) {
        return hooks.readyState(fileHandle);
    }

    char logicalPath[260]{};
    if (openwow::vfs::SFileHandle_CopyLogicalPathBounded(static_cast<int>(fileHandle),
                                                         logicalPath,
                                                         static_cast<int>(sizeof(logicalPath))) == 0) {
        return 0;
    }

    return static_cast<int>(
        openwow::vfs::QueryDataPreloadPathReadyState(logicalPath));
}

void ResetAsyncShutdownEvent() {
    g_asyncShutdownEvent = 0;
}

void SignalAsyncShutdownEvent() {
    g_asyncShutdownEvent = 1;
}

bool CreateDefaultAsyncWorkerThread(int (*proc)(void*),
                                    void* param,
                                    void** outHandle,
                                    const char* ) {
    if (proc == nullptr || outHandle == nullptr) {
        return false;
    }

    auto* handle = new AsyncWorkerThreadHandle();
    handle->thread = std::thread([handle, proc, param]() {
        (void)proc(param);
        {
            std::lock_guard lock(handle->mutex);
            handle->finished = true;
        }
        handle->finishedCv.notify_all();
    });
    *outHandle = handle;
    return true;
}

void WaitDefaultAsyncWorkerThread(void* handle, const std::uint32_t timeout) {
    if (handle == nullptr) {
        return;
    }

    auto* worker = static_cast<AsyncWorkerThreadHandle*>(handle);
    std::unique_lock lock(worker->mutex);
    if (timeout == 0xFFFFFFFFu) {
        worker->finishedCv.wait(lock, [worker] { return worker->finished; });
    } else {
        (void)worker->finishedCv.wait_for(lock,
                                          std::chrono::milliseconds(timeout),
                                          [worker] { return worker->finished; });
    }
}

void WaitAsyncThreadHandle(void* handle, const std::uint32_t timeout) {
    auto& hooks = MutableAsyncFileReadCleanupHooks();
    if (hooks.waitThread != nullptr) {
        hooks.waitThread(handle, timeout);
        return;
    }

    WaitDefaultAsyncWorkerThread(handle, timeout);
}

[[nodiscard]] bool IsAsyncStreamingModeEnabled() {
    return IsStreamingInitialized();
}

int AsyncFileRead_ThreadProcBridge(void* param) {
    return AsyncFileRead_ThreadProc(
        reinterpret_cast<std::uintptr_t>(param));
}

[[nodiscard]] std::uint32_t GetAsyncObjectToken(
    const AsyncFileReadObject* object) {
    return EncodeAsyncPointer(object);
}

[[nodiscard]] std::uint32_t GetAsyncObjectLinkToken(
    const AsyncFileReadObject* object) {
    return GetAsyncObjectToken(object) +
           static_cast<std::uint32_t>(offsetof(AsyncFileReadObject, listNext));
}

void DestroyAsyncFileReadObjectLocked(AsyncFileReadObject* object) {
    if (object->listNext != 0) {
        UnlinkStormIntrusiveNode(GetAsyncObjectLinkToken(object));
    }

    DestroyAsyncFileHandle(object->fileHandle);
    LinkStormIntrusiveNode(*GetAsyncObjectPoolRoot(),
                           GetAsyncObjectToken(object),
                           GetAsyncObjectLinkToken(object));
}

[[nodiscard]] StormIntrusiveListRoot* ResolveAsyncQueueRoot(
    const std::uint32_t queueToken,
    const std::size_t rootOffset) {
    return static_cast<StormIntrusiveListRoot*>(
        ResolveAsyncPointer(queueToken +
                            static_cast<std::uint32_t>(rootOffset)));
}

void InsertAsyncObjectBefore(AsyncFileReadObject* object,
                             StormIntrusiveListRoot& list,
                             const std::uint32_t targetObjectToken) {
    const auto objectToken = GetAsyncObjectToken(object);
    const auto objectLinkToken = GetAsyncObjectLinkToken(object);

    auto* objectLinkWords = ResolveAsyncWordPointer(objectLinkToken);
    auto* targetLinkWords =
        ResolveAsyncWordPointer(targetObjectToken + list.linkOffset);
    const auto previousLink = targetLinkWords[0];
    auto* previousLinkWords = ResolveAsyncWordPointer(previousLink);

    objectLinkWords[0] = previousLink;
    objectLinkWords[1] = previousLinkWords[1];
    previousLinkWords[1] = objectToken;
    targetLinkWords[0] = objectLinkToken;
}

void AppendAsyncObjectToTail(AsyncFileReadObject* object,
                             StormIntrusiveListRoot& list) {
    const auto objectToken = GetAsyncObjectToken(object);
    const auto objectLinkToken = GetAsyncObjectLinkToken(object);

    auto* objectLinkWords = ResolveAsyncWordPointer(objectLinkToken);
    auto* tailLinkWords = ResolveAsyncWordPointer(list.tailLink);

    objectLinkWords[0] = list.tailLink;
    objectLinkWords[1] = tailLinkWords[1];
    tailLinkWords[1] = objectToken;
    list.tailLink = objectLinkToken;
}

void MoveAsyncObjectBetweenQueues(AsyncFileReadObject* object,
                                  const std::size_t rootOffset,
                                  const bool insertBeforeEqualPriority,
                                  const std::uint8_t secondaryQueuedValue) {
    if (object == nullptr || object->ownerQueue == 0) {
        return;
    }

    const auto objectLinkToken = GetAsyncObjectLinkToken(object);
    auto* objectLinkWords = ResolveAsyncWordPointer(objectLinkToken);
    if (objectLinkWords != nullptr && objectLinkWords[0] != 0) {
        UnlinkStormIntrusiveNode(objectLinkToken);
    }

    auto* list = ResolveAsyncQueueRoot(object->ownerQueue, rootOffset);
    if (list == nullptr) {
        return;
    }

    for (auto candidateToken = StormIntrusiveList_GetNextObjectToken(*list, 0);
         candidateToken != 0;
         candidateToken =
             StormIntrusiveList_GetNextObjectToken(*list, candidateToken)) {
        auto* candidate = static_cast<AsyncFileReadObject*>(
            ResolveAsyncPointer(candidateToken));
        if (candidate == nullptr) {
            return;
        }

        if (object->priority <= candidate->priority &&
            (insertBeforeEqualPriority ||
             object->priority != candidate->priority)) {
            InsertAsyncObjectBefore(object, *list, candidateToken);
            object->isSecondaryQueued = secondaryQueuedValue;
            return;
        }
    }

    AppendAsyncObjectToTail(object, *list);
    object->isSecondaryQueued = secondaryQueuedValue;
}

void MoveAsyncObjectToQueueHead(AsyncFileReadObject* object,
                                StormIntrusiveListRoot& list,
                                const std::uint8_t secondaryQueuedValue) {
    if (object == nullptr) {
        return;
    }

    const auto objectToken = GetAsyncObjectToken(object);
    const auto objectLinkToken = GetAsyncObjectLinkToken(object);
    auto* const objectLinkWords = ResolveAsyncWordPointer(objectLinkToken);
    if (objectLinkWords == nullptr) {
        return;
    }

    if (objectLinkWords[0] != 0) {
        UnlinkStormIntrusiveNode(objectLinkToken);
    }

    objectLinkWords[0] = list.tailLink;
    objectLinkWords[1] = list.firstNode;

    const auto nextNode = list.firstNode;
    if ((nextNode & kStormTaggedPointerMask) == 0 && nextNode != 0) {
        auto* const nextLinkWords =
            ResolveAsyncWordPointer(nextNode + list.linkOffset);
        if (nextLinkWords != nullptr) {
            nextLinkWords[0] = objectLinkToken;
        }
    } else {
        auto* const rootLinkWords =
            ResolveAsyncWordPointer(nextNode & ~kStormTaggedPointerMask);
        if (rootLinkWords != nullptr) {
            rootLinkWords[0] = objectLinkToken;
        }
    }

    list.firstNode = objectToken;
    object->isSecondaryQueued = secondaryQueuedValue;
}

void InvokeAsyncWaitBeginCallback() {
    auto* const callback = ResolveAsyncWaitBeginCallback(g_asyncWaitBeginCallback);
    if (callback != nullptr) {
        callback();
    }
}

void InvokeAsyncWaitTickCallback() {
    auto* const callback = ResolveAsyncWaitTickCallback(g_asyncWaitTickCallback);
    if (callback != nullptr) {
        callback(0.0f, 0);
    }
}

}

std::uint32_t StormIntrusiveList_GetNextObjectToken(
    const StormIntrusiveListRoot& list,
    const std::uint32_t currentObjectToken) {
    if (currentObjectToken == 0) {
        return NormalizeAsyncNode(list.firstNode);
    }

    auto* linkWords =
        ResolveAsyncWordPointer(currentObjectToken + list.linkOffset);
    return linkWords == nullptr ? 0u : NormalizeAsyncNode(linkWords[1]);
}

void AsyncIO_EnterCriticalSection() {
    g_asyncCritSectHeld = 1;
    AsyncCritSectMutex().lock();
}

void AsyncIO_LeaveCriticalSection() {
    AsyncCritSectMutex().unlock();
    g_asyncCritSectHeld = 0;
}

void* AsyncFileRead_ResolvePointerToken(const std::uint32_t rawPointer) {
    return ResolveAsyncPointer(rawPointer);
}

std::uint32_t AsyncFileRead_EncodePointerToken(const void* nativePointer) {
    return EncodeAsyncPointer(nativePointer);
}

std::uint32_t AsyncFileRead_EncodeCompletionCallbackToken(
    const AsyncFileReadCompletionCallback callback) {
    return EncodeAsyncCallback(callback);
}

void AsyncFileRead_DestroyObject(AsyncFileReadObject* obj) {
    if (!obj) {
        return;
    }

    AsyncIO_EnterCriticalSection();

    if (obj->isActive) {
        AsyncIO_LeaveCriticalSection();
        while (obj->isActive) {
            SleepAsyncSpin(1);
        }
        AsyncIO_EnterCriticalSection();
    }

    DestroyAsyncFileReadObjectLocked(obj);
    AsyncIO_LeaveCriticalSection();
}

void AsyncFileRead_DestroyObjectCallback(const std::uint32_t context) {
    auto* const object = static_cast<AsyncFileReadObject*>(
        ResolveAsyncPointer(context));
    AsyncFileRead_DestroyObject(object);
}

int AsyncFileRead_DestroyOrDeflect(
    AsyncFileReadObject* obj,
    AsyncFileReadCompletionCallback callback) {
    AsyncIO_EnterCriticalSection();
    if (obj->isActive) {
        obj->completionContext = EncodeAsyncPointer(obj);
        if (callback == nullptr) {
            callback = &AsyncFileRead_DestroyObjectCallback;
        }

        const auto encodedCallback = EncodeAsyncCallback(callback);
        obj->callbackDone = encodedCallback;
        obj->doneCallback = encodedCallback;
        AsyncIO_LeaveCriticalSection();
        return 0;
    }

    DestroyAsyncFileReadObjectLocked(obj);
    AsyncIO_LeaveCriticalSection();
    return 1;
}

AsyncFileReadObject* CAsyncObject_Allocate() {
    AsyncIO_EnterCriticalSection();

    AsyncFileReadObject* obj = nullptr;

    auto* const objectPool = GetAsyncObjectPoolRoot();
    if (const auto objectToken = GetIntrusiveListFront(*objectPool);
        objectToken != 0) {
        obj = static_cast<AsyncFileReadObject*>(ResolveAsyncPointer(objectToken));
        if (obj != nullptr) {
            UnlinkStormIntrusiveNode(GetAsyncObjectLinkToken(obj));
        }
    } else {
        auto* const mem = openwow::core::SMemAlloc(
            sizeof(AsyncFileReadObject), "AVC:AsyncObject", -2, 8);
        if (mem != nullptr) {
            obj = reinterpret_cast<AsyncFileReadObject*>(mem);
            obj->listNext = 0;
            obj->listPrev = 0;
        }
    }

    AsyncIO_LeaveCriticalSection();

    if (!obj) {
        return nullptr;
    }

    obj->fileHandle = 0;
    obj->readBuffer = nullptr;
    obj->readSize = 0;
    obj->completionContext = 0;
    obj->doneCallback = 0;
    obj->callbackDone = 0;
    obj->ownerQueue = 0;
    obj->lastTouchedFrame = 0;
    obj->completionDispatched = 0;
    obj->completionPending = 0;
    obj->isActive = 0;
    obj->field_24 = 0;
    obj->isSecondaryQueued = 0;
    obj->priority = 126;

    return obj;
}

void CAsyncObject_FreeAll(uint32_t* listHead) {
    auto* list = reinterpret_cast<StormIntrusiveListRoot*>(listHead);
    DestroyStormIntrusiveList<AsyncFileReadObject>(
        list,
        offsetof(AsyncFileReadObject, listNext),
        "AVC:AsyncObject",
        [](AsyncFileReadObject*) {});
}

void CAsyncThread_DestroyAll(uint32_t* listHead) {
    auto* list = reinterpret_cast<StormIntrusiveListRoot*>(listHead);
    DestroyStormIntrusiveList<AsyncThreadNode>(
        list,
        0,
        "AVC:AsyncThread",
        [](AsyncThreadNode* node) { DestroyAsyncThreadHandle(node->threadHandle); });
}

AsyncThreadNode* CAsyncThread_CreateAndStart(CAsyncQueue* queue,
                                             const char* threadName) {
    auto* node = static_cast<AsyncThreadNode*>(
        openwow::core::SMemAlloc(sizeof(AsyncThreadNode),
                                 "AVC:AsyncThread",
                                 kStormDestructorLine,
                                 8));
    if (node == nullptr) {
        return nullptr;
    }

    node->linkPrev = 0;
    node->linkNext = 0;
    node->threadHandle = nullptr;

    auto* threadList = GetAsyncThreadListRoot();
    LinkStormIntrusiveNode(*threadList,
                           EncodeAsyncPointer(node),
                           EncodeAsyncPointer(&node->linkPrev));

    node->queue = reinterpret_cast<std::uintptr_t>(queue);
    node->activeObjectToken = 0;

    auto& hooks = MutableAsyncFileReadCleanupHooks();
    const bool started =
        hooks.createThread != nullptr
            ? hooks.createThread(&AsyncFileRead_ThreadProcBridge,
                                 node,
                                 &node->threadHandle,
                                 threadName)
            : CreateDefaultAsyncWorkerThread(&AsyncFileRead_ThreadProcBridge,
                                             node,
                                             &node->threadHandle,
                                             threadName);
    if (!started) {
        return node;
    }

    return node;
}

int AsyncFileRead_ThreadProc(uintptr_t threadInfo) {
    auto* const thread_node = reinterpret_cast<AsyncThreadNode*>(threadInfo);
    if (thread_node == nullptr || thread_node->queue == 0) {
        return 0;
    }

    auto* const queue = reinterpret_cast<CAsyncQueue*>(thread_node->queue);
    g_asyncTlsSlot = thread_node;

    while (g_asyncShutdownEvent == 0) {
        std::uint32_t completions_since_sleep = 0;

        while (true) {
            AsyncFileReadObject* object = nullptr;

            AsyncIO_EnterCriticalSection();
            auto* const primary_queue =
                reinterpret_cast<StormIntrusiveListRoot*>(&queue->priorityLinkOffset);
            auto object_token = GetIntrusiveListFront(*primary_queue);
            if (object_token != 0) {
                object = static_cast<AsyncFileReadObject*>(
                    ResolveAsyncPointer(object_token));
                if (object != nullptr && queue->staleFrameCheck != 0 &&
                    GetAsyncTickCount() - object->lastTouchedFrame > 3u) {
                    AsyncFileRead_MoveToSecondaryQueue(object, true);
                    AsyncIO_LeaveCriticalSection();
                    continue;
                }
            } else {
                auto* const secondary_queue =
                    reinterpret_cast<StormIntrusiveListRoot*>(&queue->secondaryLinkOffset);
                object_token = GetIntrusiveListFront(*secondary_queue);
                if (object_token != 0) {
                    object = static_cast<AsyncFileReadObject*>(
                        ResolveAsyncPointer(object_token));
                }
            }

            if (object == nullptr) {
                AsyncIO_LeaveCriticalSection();
                break;
            }

            if (object->listNext != 0) {
                UnlinkStormIntrusiveNode(GetAsyncObjectLinkToken(object));
            }

            object->ownerQueue = 0;
            object->isActive = 1;
            thread_node->activeObjectToken = object_token;
            AsyncIO_LeaveCriticalSection();

            int retries_remaining = 10;
            while (true) {
                if (IsAsyncStreamingModeEnabled() && object->fileHandle != 0) {
                    (void)openwow::vfs::AsyncFileRead_RequestDataPreloadPathAvailability(
                        static_cast<int>(object->fileHandle),
                        object->priority > 0x7Fu ? 2 : 1,
                        1);
                }

                if (openwow::vfs::SFile_ReadFile(static_cast<int>(object->fileHandle),
                                                 object->readBuffer,
                                                 static_cast<int>(object->readSize),
                                                 nullptr,
                                                 0,
                                                 0) != 0) {
                    break;
                }

                --retries_remaining;
                if (retries_remaining == 0) {
                    break;
                }
            }

            AsyncIO_EnterCriticalSection();
            if (object->listNext != 0) {
                UnlinkStormIntrusiveNode(GetAsyncObjectLinkToken(object));
            }

            AppendAsyncObjectToTail(object, *GetAsyncCompletionListRoot());
            thread_node->activeObjectToken = 0;
            object->isActive = 0;
            object->completionPending = 1;
            AsyncIO_LeaveCriticalSection();

            if (g_asyncRateLimit != 0 &&
                ++completions_since_sleep == g_asyncRateLimit) {
                SleepAsyncSpin(1);
                completions_since_sleep = 0;
            }
        }

        SleepAsyncSpin(1);
    }

    return 0;
}

int AsyncFileRead_QueueObject(AsyncFileReadObject* object,
                              const bool insertBeforeEqualPriority) {
    if (object == nullptr) {
        return 0;
    }

    auto* selected_queue = reinterpret_cast<CAsyncQueue*>(g_asyncQueues[0]);
    if (IsAsyncStreamingModeEnabled() && object->fileHandle != 0) {
        char logical_path[260]{};
        if (openwow::vfs::SFileHandle_CopyLogicalPathBounded(
                static_cast<int>(object->fileHandle),
                logical_path,
                static_cast<int>(sizeof(logical_path))) != 0) {
            const auto ready_state =
                openwow::vfs::QueryDataPreloadPathReadyState(logical_path);
            if (ready_state == openwow::vfs::DataPreloadPathReadyState::kUnavailable ||
                ready_state == openwow::vfs::DataPreloadPathReadyState::kPartial) {
                object->field_24 = 1;
                selected_queue = reinterpret_cast<CAsyncQueue*>(g_asyncQueues[1]);
                if (object->priority > 0x7Fu) {
                    selected_queue =
                        reinterpret_cast<CAsyncQueue*>(g_asyncQueues[2]);
                }
            }
        }
    }

    if (selected_queue == nullptr) {
        return 0;
    }

    AsyncIO_EnterCriticalSection();
    object->ownerQueue = AsyncFileRead_EncodePointerToken(selected_queue);
    if (GetAsyncObjectToken(object) == g_asyncWaitObject) {
        object->priority = object->priority > 0x7Fu ? 0x80u : 0u;
        object->lastTouchedFrame = GetAsyncTickCount();
        auto* const primary_queue =
            reinterpret_cast<StormIntrusiveListRoot*>(&selected_queue->priorityLinkOffset);
        MoveAsyncObjectToQueueHead(object, *primary_queue, 0);
    } else if (selected_queue->staleFrameCheck != 0) {
        AsyncFileRead_MoveToSecondaryQueue(object, insertBeforeEqualPriority);
    } else {
        AsyncFileRead_MoveToPrimaryQueue(object, insertBeforeEqualPriority);
    }
    AsyncIO_LeaveCriticalSection();

    if (IsAsyncStreamingModeEnabled() && object->fileHandle != 0) {
        (void)openwow::vfs::AsyncFileRead_RequestDataPreloadPathAvailability(
            static_cast<int>(object->fileHandle), 0, 0);
    }

    return 0;
}

void AsyncFileRead_Wait(AsyncFileReadObject* object) {
    if (object == nullptr) {
        return;
    }

    if (g_asyncWaitDepth != 0) {
        openwow::core::SErrFatalCondition(
            "AsyncFileReadWait(): s_waiting != FALSE");
    }

    ++g_asyncWaitDepth;
    AsyncIO_EnterCriticalSection();

    if (object->completionDispatched != 0) {
        AsyncIO_LeaveCriticalSection();
        return;
    }

    g_asyncWaitObject = GetAsyncObjectToken(object);
    if (object->isActive == 0 && object->completionPending == 0 &&
        object->ownerQueue != 0) {
        auto* const primary_queue =
            ResolveAsyncQueueRoot(object->ownerQueue,
                                  offsetof(CAsyncQueue, priorityLinkOffset));
        if (primary_queue != nullptr) {
            MoveAsyncObjectToQueueHead(object, *primary_queue, 0);
        }
    }
    AsyncIO_LeaveCriticalSection();

    InvokeAsyncWaitBeginCallback();
    while (true) {
        InvokeAsyncWaitTickCallback();
        AsyncFileRead_ProcessCompletedCallbacks();
        if (g_asyncWaitObject == 0) {
            break;
        }
        SleepAsyncSpin(1);
    }

    --g_asyncWaitDepth;
}

std::uint32_t AsyncFile_SetWaitCallbacks(
    const AsyncFileReadIdleCallback beginCallback,
    const AsyncFileReadWaitTickCallback tickCallback) {
    const auto encodedBeginCallback = EncodeAsyncWaitBeginCallback(beginCallback);
    g_asyncWaitBeginCallback = encodedBeginCallback;
    g_asyncWaitTickCallback = EncodeAsyncWaitTickCallback(tickCallback);
    return encodedBeginCallback;
}

std::uint32_t AsyncFile_SetWaitAllProgressCallback(
    const AsyncFileWaitAllProgressCallback callback,
    const std::uint32_t context) {
    const auto encodedCallback = EncodeAsyncProgressCallback(callback);
    g_asyncWaitAllProgressCallback = encodedCallback;
    g_asyncWaitAllProgressContext = context;
    return encodedCallback;
}

bool AsyncFile_RegisterCompletionPumpCallback(
    const AsyncFileReadIdleCallback callback) {
    if (callback == nullptr) {
        return false;
    }

    auto& callbacks = MutableAsyncCompletionPumpCallbacks();
    if (std::find(callbacks.begin(), callbacks.end(), callback) ==
        callbacks.end()) {
        callbacks.push_back(callback);
    }

    return true;
}

bool AsyncFile_RegisterWaitAllCountCallback(
    const AsyncFileWaitAllCountCallback callback) {
    if (callback == nullptr) {
        return false;
    }

    auto& callbacks = MutableAsyncWaitAllCountCallbacks();
    if (std::find(callbacks.begin(), callbacks.end(), callback) ==
        callbacks.end()) {
        callbacks.push_back(callback);
    }

    return true;
}

int AsyncFileRead_ProcessCompletedCallbacks() {
    const auto startTick = GetAsyncTickCount();

    while (true) {
        AsyncIO_EnterCriticalSection();
        auto* const completionList = GetAsyncCompletionListRoot();
        const auto completionToken = GetIntrusiveListFront(*completionList);
        if (completionToken == 0) {
            AsyncIO_LeaveCriticalSection();
            break;
        }

        auto* const object = static_cast<AsyncFileReadObject*>(
            ResolveAsyncPointer(completionToken));
        const auto callback = ResolveAsyncCallback(object->doneCallback);
        const auto context = object->completionContext;
        UnlinkStormIntrusiveNode(GetAsyncObjectLinkToken(object));
        if (completionToken == g_asyncWaitObject) {
            g_asyncWaitObject = 0;
        }
        object->completionDispatched = 1;
        AsyncIO_LeaveCriticalSection();

        if (callback != nullptr) {
            callback(context);
        }
        --g_asyncWaitAllRemaining;

        if (GetAsyncTickCount() - startTick > g_asyncCompletionPumpBudgetMs) {
            break;
        }
    }

    for (const auto callback : MutableAsyncCompletionPumpCallbacks()) {
        callback();
    }

    return 1;
}

int AsyncFileRead_PromoteReadyStreamingRequests(void* , int ) {
    const auto currentTick = GetAsyncTickCount();
    if (currentTick - g_asyncPreloadPromotionLastTick < kAsyncReadyPromotionIntervalMs) {
        return 1;
    }

    g_asyncPreloadPromotionLastTick = currentTick;

    auto* const targetQueue = reinterpret_cast<CAsyncQueue*>(g_asyncQueues[0]);
    if (targetQueue == nullptr) {
        return 1;
    }

    auto* const targetPrimaryQueue =
        reinterpret_cast<StormIntrusiveListRoot*>(&targetQueue->priorityLinkOffset);
    const auto targetQueueToken = AsyncFileRead_EncodePointerToken(targetQueue);

    AsyncIO_EnterCriticalSection();
    for (std::size_t queueIndex = 1; queueIndex < 3; ++queueIndex) {
        auto* const sourceQueue =
            reinterpret_cast<CAsyncQueue*>(g_asyncQueues[queueIndex]);
        if (sourceQueue == nullptr) {
            continue;
        }

        const StormIntrusiveListRoot sourcePrimaryQueue{
            .linkOffset = sourceQueue->priorityLinkOffset,
            .tailLink = sourceQueue->priorityTailLink,
            .firstNode = sourceQueue->priorityFirstNode,
        };

        for (auto objectToken =
                 StormIntrusiveList_GetNextObjectToken(sourcePrimaryQueue, 0);
             objectToken != 0;) {
            auto* const object = static_cast<AsyncFileReadObject*>(
                ResolveAsyncPointer(objectToken));
            const auto nextObjectToken =
                StormIntrusiveList_GetNextObjectToken(sourcePrimaryQueue, objectToken);

            if (object != nullptr &&
                QueryAsyncReadyState(object->fileHandle) == static_cast<int>(
                    openwow::vfs::DataPreloadPathReadyState::kReady)) {
                if (object->listNext != 0) {
                    UnlinkStormIntrusiveNode(GetAsyncObjectLinkToken(object));
                }

                AppendAsyncObjectToTail(object, *targetPrimaryQueue);
                object->ownerQueue = targetQueueToken;
            }

            objectToken = nextObjectToken;
        }
    }
    AsyncIO_LeaveCriticalSection();

    return 1;
}

void AsyncFileRead_FlushCompletedCallbacks() {
    while (true) {
        AsyncIO_EnterCriticalSection();
        auto* const completionList = GetAsyncCompletionListRoot();
        const auto completionToken = GetIntrusiveListFront(*completionList);
        if (completionToken == 0) {
            AsyncIO_LeaveCriticalSection();
            break;
        }

        auto* const object = static_cast<AsyncFileReadObject*>(
            ResolveAsyncPointer(completionToken));
        const auto callback = ResolveAsyncCallback(object->callbackDone);
        const auto context = object->completionContext;
        UnlinkStormIntrusiveNode(GetAsyncObjectLinkToken(object));
        AsyncIO_LeaveCriticalSection();

        if (callback != nullptr) {
            callback(context);
        }
    }
}

bool AsyncFileRead_HasPendingWork() {
    AsyncIO_EnterCriticalSection();

    bool hasPendingWork = false;
    for (auto threadToken = GetIntrusiveListFront(*GetAsyncThreadListRoot());
         threadToken != 0;
         threadToken = StormIntrusiveList_GetNextObjectToken(
             *GetAsyncThreadListRoot(),
             threadToken)) {
        auto* const threadNode = static_cast<AsyncThreadNode*>(
            ResolveAsyncPointer(threadToken));
        if (threadNode != nullptr && threadNode->activeObjectToken != 0) {
            hasPendingWork = true;
            break;
        }
    }

    if (!hasPendingWork) {
        for (auto queueToken = GetIntrusiveListFront(*GetAsyncQueueListRoot());
             queueToken != 0;
             queueToken = StormIntrusiveList_GetNextObjectToken(
                 *GetAsyncQueueListRoot(),
                 queueToken)) {
            auto* const queue = static_cast<CAsyncQueue*>(
                ResolveAsyncPointer(queueToken));
            if (queue != nullptr &&
                GetIntrusiveListFront(*reinterpret_cast<StormIntrusiveListRoot*>(
                    &queue->priorityLinkOffset)) != 0) {
                hasPendingWork = true;
                break;
            }
        }
    }

    if (!hasPendingWork &&
        GetIntrusiveListFront(*GetAsyncCompletionListRoot()) != 0) {
        hasPendingWork = true;
    }

    AsyncIO_LeaveCriticalSection();
    return hasPendingWork;
}

int AsyncFileRead_WaitAll() {
    std::int32_t totalCallbacks = 0;
    for (const auto callback : MutableAsyncWaitAllCountCallbacks()) {
        totalCallbacks += static_cast<std::int32_t>(callback());
    }
    g_asyncWaitAllRemaining = totalCallbacks;

    if (AsyncFileRead_HasPendingWork()) {
        while (true) {
            AsyncFileRead_ProcessCompletedCallbacks();

            if (const auto progressCallback = ResolveAsyncProgressCallback(
                    g_asyncWaitAllProgressCallback);
                progressCallback != nullptr) {
                float progress = 1.0f;
                if (totalCallbacks != 0) {
                    double normalized =
                        static_cast<double>(totalCallbacks - g_asyncWaitAllRemaining) /
                        static_cast<double>(totalCallbacks);
                    if (normalized < 0.0) {
                        normalized = 0.0;
                    } else if (normalized > 1.0) {
                        normalized = 1.0;
                    }
                    progress = static_cast<float>(normalized);
                }

                progressCallback(progress, g_asyncWaitAllProgressContext);
            }

            SleepAsyncSpin(1);
            if (!AsyncFileRead_HasPendingWork()) {
                break;
            }
        }
    }

    g_asyncWaitAllProgressCallback = 0;
    return 0;
}

void CAsyncQueue_Destroy(CAsyncQueue* queue, const std::uint32_t queueToken) {

    UnlinkStormIntrusiveList(
        *reinterpret_cast<StormIntrusiveListRoot*>(&queue->secondaryLinkOffset));

    UnlinkStormIntrusiveNode(
        queueToken +
        static_cast<std::uint32_t>(offsetof(CAsyncQueue, secondaryTailLink)));

    UnlinkStormIntrusiveList(
        *reinterpret_cast<StormIntrusiveListRoot*>(&queue->priorityLinkOffset));

    UnlinkStormIntrusiveNode(
        queueToken +
        static_cast<std::uint32_t>(offsetof(CAsyncQueue, priorityTailLink)));

    UnlinkStormIntrusiveNode(
        queueToken +
        static_cast<std::uint32_t>(offsetof(CAsyncQueue, queueListPrevLink)));
}

CAsyncQueue* CAsyncQueue_Create() {
    auto* mem = openwow::core::SMemAlloc(36, "AVC:AsyncQueue", -2, 8);
    if (!mem) {
        return nullptr;
    }

    auto* queue = reinterpret_cast<CAsyncQueue*>(mem);
    const auto queueToken = EncodeAsyncPointer(queue);
    auto* queueListRoot = GetAsyncQueueListRoot();

    queue->queueListPrevLink = 0;
    queue->queueListNextNode = 0;

    queue->priorityLinkOffset =
        static_cast<std::uint32_t>(offsetof(AsyncFileReadObject, listNext));
    queue->priorityTailLink =
        queueToken +
        static_cast<std::uint32_t>(offsetof(CAsyncQueue, priorityTailLink));
    queue->priorityFirstNode =
        (queueToken +
         static_cast<std::uint32_t>(offsetof(CAsyncQueue, priorityFirstNode))) |
        kStormTaggedPointerMask;

    queue->secondaryLinkOffset =
        static_cast<std::uint32_t>(offsetof(AsyncFileReadObject, listNext));
    queue->secondaryTailLink =
        queueToken +
        static_cast<std::uint32_t>(offsetof(CAsyncQueue, secondaryTailLink));
    queue->secondaryFirstNode =
        (queueToken +
         static_cast<std::uint32_t>(offsetof(CAsyncQueue, secondaryFirstNode))) |
        kStormTaggedPointerMask;

    queue->staleFrameCheck = 0;

    queue->queueListPrevLink = 0;
    queue->queueListNextNode = 0;
    LinkStormIntrusiveNode(*queueListRoot,
                           queueToken,
                           queueToken +
                               static_cast<std::uint32_t>(
                                   offsetof(CAsyncQueue, queueListPrevLink)));
    return queue;
}

void AsyncIO_Initialize(uint32_t rateLimit, uint32_t completionPumpBudgetMs) {
    g_asyncRateLimit = rateLimit;
    g_asyncCompletionPumpBudgetMs = completionPumpBudgetMs;

    if (rateLimit > 100)
        g_asyncRateLimit = 100;

    if (completionPumpBudgetMs < 20)
        g_asyncCompletionPumpBudgetMs = 20;

    openwow::core::EvtContext_RegisterCurrentHandler(
        7u, LegacyAsyncCompletionPumpCallback(), 0, 0.0f);

    const bool streamingModeEnabled = IsAsyncStreamingModeEnabled();
    if (streamingModeEnabled) {
        openwow::core::EvtContext_RegisterCurrentHandler(
            6u, LegacyAsyncReadyPromotionCallback(), 0, -1.0f);
    }

    g_asyncWaitObject = 0;
    g_asyncWaitAllProgressCallback = 0;
    g_asyncWaitAllProgressContext = 0;
    g_asyncWaitTickCallback = 0;
    g_asyncWaitBeginCallback = 0;

    g_asyncTlsSlot = openwow::core::GetEvtContextTlsValue();

    ResetAsyncShutdownEvent();

    static constexpr const char* kAsyncQueueNames[3] = {
        "Disk Queue",
        "Net Geometry Queue",
        "Net Texture Queue",
    };
    const int numQueues = streamingModeEnabled ? 3 : 1;
    for (int i = 0; i < numQueues; ++i) {
        auto* queue = CAsyncQueue_Create();
        g_asyncQueues[i] = reinterpret_cast<uintptr_t>(queue);
        (void)CAsyncThread_CreateAndStart(queue, kAsyncQueueNames[i]);
    }

    if (streamingModeEnabled && g_asyncQueues[2] != 0) {
        reinterpret_cast<CAsyncQueue*>(g_asyncQueues[2])->staleFrameCheck = 1;
    }

    g_asyncCritSectHeld = 0;
}

void AsyncFile_Shutdown() {

    SignalAsyncShutdownEvent();
    auto* threadList = GetAsyncThreadListRoot();
    for (auto threadToken = GetIntrusiveListFront(*threadList);
         threadToken != 0;
         threadToken = NormalizeAsyncNode(static_cast<AsyncThreadNode*>(
                                              ResolveAsyncPointer(threadToken))
                                              ->linkNext)) {
        auto* node = static_cast<AsyncThreadNode*>(ResolveAsyncPointer(threadToken));
        WaitAsyncThreadHandle(node->threadHandle, 0xFFFFFFFFu);
    }

    AsyncFileRead_FlushCompletedCallbacks();

    CAsyncObject_FreeAll(&g_asyncObjectPoolRoot.linkOffset);

    auto* queueList = GetAsyncQueueListRoot();
    while (const auto queueToken = GetIntrusiveListFront(*queueList)) {
        auto* queue = static_cast<CAsyncQueue*>(ResolveAsyncPointer(queueToken));
        CAsyncQueue_Destroy(queue, queueToken);
        FreeStormMemory(queue, "AVC:AsyncQueue");
    }

    g_asyncQueues[0] = 0;
    g_asyncQueues[1] = 0;
    g_asyncQueues[2] = 0;

    CAsyncThread_DestroyAll(&g_asyncThreadListRoot.linkOffset);

    MutableAsyncCompletionPumpCallbacks().clear();

    MutableAsyncWaitAllCountCallbacks().clear();

    g_asyncWaitAllProgressCallback = 0;

    g_asyncWaitAllProgressContext = 0;

    g_asyncWaitTickCallback = 0;

    g_asyncWaitBeginCallback = 0;

    if (IsAsyncStreamingModeEnabled()) {
        (void)openwow::core::EvtContext_UnregisterCurrentHandlers(
            6, LegacyAsyncReadyPromotionCallback(), 0, 0x2u);
    }
    (void)openwow::core::EvtContext_UnregisterCurrentHandlers(
        7, LegacyAsyncCompletionPumpCallback(), 0, 0xFFu);
}

void SetAsyncFileReadCleanupHooksForTests(AsyncFileReadEventDestroyHook destroyEventHook,
                                          AsyncFileReadMemoryFreeHook freeHook) {
    auto& hooks = MutableAsyncFileReadCleanupHooks();
    hooks.destroyEvent = destroyEventHook;
    hooks.freeMemory = freeHook;
}

void ResetAsyncFileReadCleanupHooksForTests() {
    SetAsyncFileReadCleanupHooksForTests(nullptr, nullptr);
    SetAsyncFileReadPointerResolverForTests(nullptr);
    SetAsyncFileReadPointerEncoderForTests(nullptr);
    SetAsyncFileReadThreadHooksForTests(nullptr, nullptr);
    SetAsyncFileReadHandleDestroyHookForTests(nullptr);
    SetAsyncFileReadSleepHookForTests(nullptr);
    SetAsyncFileReadTickCountHookForTests(nullptr);
    SetAsyncFileReadReadyStateHookForTests(nullptr);
    MutableAsyncCompletionPumpCallbacks().clear();
    MutableAsyncWaitAllCountCallbacks().clear();
    g_asyncWaitDepth = 0;

#if INTPTR_MAX != INT32_MAX
    {
        auto& pointerRegistry = MutableAsyncPointerRegistry();
        std::lock_guard lock(pointerRegistry.mutex);
        pointerRegistry.nextToken = 0x60000000u;
        pointerRegistry.tokenToPointer.clear();
        pointerRegistry.pointerToToken.clear();
    }
    {
        auto& callbackRegistry = MutableAsyncCompletionCallbackRegistry();
        std::lock_guard lock(callbackRegistry.mutex);
        callbackRegistry.nextToken = 0x70000000u;
        callbackRegistry.tokenToCallback.clear();
        callbackRegistry.callbackToToken.clear();
    }
    {
        auto& callbackRegistry = MutableAsyncProgressCallbackRegistry();
        std::lock_guard lock(callbackRegistry.mutex);
        callbackRegistry.nextToken = 0x71000000u;
        callbackRegistry.tokenToCallback.clear();
        callbackRegistry.callbackToToken.clear();
    }
    {
        auto& callbackRegistry = MutableAsyncWaitBeginCallbackRegistry();
        std::lock_guard lock(callbackRegistry.mutex);
        callbackRegistry.nextToken = 0x72000000u;
        callbackRegistry.tokenToCallback.clear();
        callbackRegistry.callbackToToken.clear();
    }
    {
        auto& callbackRegistry = MutableAsyncWaitTickCallbackRegistry();
        std::lock_guard lock(callbackRegistry.mutex);
        callbackRegistry.nextToken = 0x73000000u;
        callbackRegistry.tokenToCallback.clear();
        callbackRegistry.callbackToToken.clear();
    }
#endif
}

void SetAsyncFileReadPointerResolverForTests(AsyncFileReadPointerResolveHook resolver) {
    MutableAsyncFileReadCleanupHooks().resolvePointer = resolver;
}

void SetAsyncFileReadPointerEncoderForTests(AsyncFileReadPointerEncodeHook encoder) {
    MutableAsyncFileReadCleanupHooks().encodePointer = encoder;
}

void SetAsyncFileReadThreadHooksForTests(AsyncFileReadThreadCreateHook createHook,
                                         AsyncFileReadThreadWaitHook waitHook) {
    auto& hooks = MutableAsyncFileReadCleanupHooks();
    hooks.createThread = createHook;
    hooks.waitThread = waitHook;
}

void SetAsyncFileReadHandleDestroyHookForTests(AsyncFileReadHandleDestroyHook hook) {
    MutableAsyncFileReadCleanupHooks().destroyHandle = hook;
}

void SetAsyncFileReadSleepHookForTests(AsyncFileReadSleepHook hook) {
    MutableAsyncFileReadCleanupHooks().sleep = hook;
}

void SetAsyncFileReadTickCountHookForTests(AsyncFileReadTickCountHook hook) {
    MutableAsyncFileReadCleanupHooks().tickCount = hook;
}

void SetAsyncFileReadReadyStateHookForTests(AsyncFileReadReadyStateHook hook) {
    MutableAsyncFileReadCleanupHooks().readyState = hook;
}

void AsyncFileRead_MoveToPrimaryQueue(AsyncFileReadObject* object,
                                      const bool insertBeforeEqualPriority) {
    MoveAsyncObjectBetweenQueues(object,
                                 offsetof(CAsyncQueue, priorityLinkOffset),
                                 insertBeforeEqualPriority,
                                 0);
}

void AsyncFileRead_MoveToSecondaryQueue(AsyncFileReadObject* object,
                                        const bool insertBeforeEqualPriority) {
    MoveAsyncObjectBetweenQueues(object,
                                 offsetof(CAsyncQueue, secondaryLinkOffset),
                                 insertBeforeEqualPriority,
                                 1);
}

bool AsyncFileRead_CanTouchObject(const AsyncFileReadObject* object) {
    return object->completionDispatched == 0 &&
           object->completionPending == 0 &&
           object->isActive == 0;
}

void AsyncFileRead_TouchObject(AsyncFileReadObject* object,
                               const std::uint32_t currentFrame) {
    if (object == nullptr) {
        return;
    }

    object->lastTouchedFrame = currentFrame;
    if (object->isSecondaryQueued != 0) {
        AsyncFileRead_MoveToPrimaryQueue(object, true);
    }
}

}

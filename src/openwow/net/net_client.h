#pragma once

#include "openwow/core/storm_sync.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <semaphore>
#include <unordered_map>
#include <vector>

namespace openwow::net {

static constexpr int kMaxWorkerThreads = 32;
static constexpr int kMaxWaitObjects = 64;
using NetClientThreadInitCallback = void (*)();
using WowConnectionSocketCreateFn = int (*)(int af, int type, int protocol);

class NetSemaphore {
public:
    NetSemaphore(int initial_count, int maximum_count)
        : semaphore_(initial_count),
          current_count_(initial_count),
          maximum_count_(std::max(0, maximum_count)) {}

    NetSemaphore(const NetSemaphore&) = delete;
    NetSemaphore& operator=(const NetSemaphore&) = delete;

    bool WaitFor(std::chrono::milliseconds timeout) {
        if (!semaphore_.try_acquire_for(timeout)) {
            return false;
        }
        current_count_.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }

    void Release(int count = 1) {
        if (count <= 0) {
            return;
        }

        int current = current_count_.load(std::memory_order_acquire);
        while (true) {
            const int next = std::min(maximum_count_, current + count);
            const int delta = next - current;
            if (current_count_.compare_exchange_weak(
                    current, next, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                if (delta > 0) {
                    semaphore_.release(delta);
                }
                return;
            }
        }
    }

    [[nodiscard]] int maximum_count() const { return maximum_count_; }
    [[nodiscard]] int current_count() const {
        return current_count_.load(std::memory_order_acquire);
    }

private:
    std::counting_semaphore<INT_MAX> semaphore_;
    std::atomic<int> current_count_{0};
    int maximum_count_ = 0;
};

struct NetIntrusiveListRoot {
    uint32_t link_offset = 0;
    uintptr_t tail_link = 0;
    uintptr_t first_node = 0;

    void Reset(uint32_t offset) {
        link_offset = offset;
        tail_link = reinterpret_cast<uintptr_t>(&tail_link);
        first_node = reinterpret_cast<uintptr_t>(&first_node) | uintptr_t{1};
    }

    void Clear() {
        link_offset = 0;
        tail_link = 0;
        first_node = 0;
    }
};

struct NetWorkerSlot {
    void* thread_handle = nullptr;
    uint32_t thread_index = 0;
    bool stop_flag = false;
    void* owner = nullptr;
    void* current_conn = nullptr;
    void* event_handle = nullptr;
    std::recursive_mutex cs;
    uint32_t processor_count = 0;
    std::unique_ptr<core::SEvent> wake_event;
};

struct NetClient {
    ~NetClient();

    void* main_thread_handle = nullptr;
    void* main_stop_event = nullptr;
    bool stop_flag = false;
    uint32_t worker_count = 0;
    NetWorkerSlot workers[kMaxWorkerThreads];

    std::recursive_mutex global_cs;
    NetIntrusiveListRoot connection_list_root;
    void* worker_semaphore = nullptr;
    NetIntrusiveListRoot pending_list_root;
    NetIntrusiveListRoot cleanup_list_root;
    void* cleanup_semaphore = nullptr;
    NetClientThreadInitCallback thread_init_callback = nullptr;
    void* wsa_event = nullptr;
    bool init_complete = false;

    std::mutex wake_mutex;
    std::condition_variable wake_cv;
    bool wake_event_pending = false;

    std::vector<void*> registered_connections;

    std::mutex socket_event_mutex;
    std::unordered_map<void*, void*> connection_socket_events;

#ifndef _WIN32
    int wake_read_fd = -1;
    int wake_write_fd = -1;
#endif

    std::unique_ptr<core::SEvent> main_stop_signal;
    std::unique_ptr<NetSemaphore> available_worker_slots;
    std::unique_ptr<NetSemaphore> cleanup_slots;
};

using WowConnectionReleasePhaseFn = void(*)(void* connection, void* context);

struct WowConnectionReleaseHooks {
    WowConnectionReleasePhaseFn destroy = nullptr;
    WowConnectionReleasePhaseFn free = nullptr;
    void* context = nullptr;
};

NetClient*& GetGlobalNetClient();

void NetClient_ResetTrackedSocketRegistry();
void NetClient_TrackSocketFd(int socket_fd);
void NetClient_UntrackSocketFd(int socket_fd);
bool NetClient_IsTrackedSocketFd(int socket_fd);

int WowConnection_InitializeNetworkSystem(
    void* filter_callback,
    NetClientThreadInitCallback thread_init_callback,
    uint32_t max_workers,
    int thread_init_fn);

NetClient* NetClient_Init(
    NetClient* self,
    int max_workers,
    NetClientThreadInitCallback thread_init_callback);
void NetClient_WSAInit(NetClient* self, int unused);
void NetClient_CreateNetThreads(NetClient* self);
int NetClient_MainThreadProc(void* param);
void NetClient_MainThreadLoop(NetClient* self);
int NetClient_WorkerThreadProc(void* param);
void NetClient_WorkerBody(NetClient* self, int slot_index);
void NetClient_DispatchToWorker(NetClient* self, void* conn, int flags);
void NetClient_ShutdownWorkers(NetClient* self);
void NetClient_Destroy(NetClient* self);

void NetClient_RegisterConnection(NetClient* self, void* conn);

void NetClient_UnregisterConnection(NetClient* self, void* conn);

void NetClient_FinalizeConnectionRelease(NetClient* self, void* conn);

void NetClient_SignalWakeEvent(void* net_client);
void NetClient_SignalWakeEventNoArg(void* net_client, int unused);

bool NetClient_EnsureConnectionEventAndWake(NetClient* self, void* conn);

bool NetClient_UpdateConnectionEventMaskAndWake(
    NetClient* self, void* conn, int unused_state);

void WowConnection_CloseWSAEvent(void* conn);

int WowConnection_Release(void* conn);

void WowConnection_RegisterReleaseHooks(
    void* conn, WowConnectionReleaseHooks hooks);
void WowConnection_UnregisterReleaseHooks(void* conn);

void WowConnection_SetSocketCreateFnForTests(WowConnectionSocketCreateFn fn);
void WowConnection_ResetSocketCreateFnForTests();

void WowConnection_ConnectInternal(void* conn);
bool WowConnection_Connect(void* conn, uint32_t addr, uint16_t port, int unused);
bool WowConnection_ConnectByAddressString(
    void* conn, const char* host, uint16_t port, int unused);
bool WowConnection_ConnectByHostPortString(
    void* conn, const char* endpoint, int timeout_ms);
void WowConnection_DataReady(void* conn);
int WowConnection_AcceptLoop(void* listener_conn);

}

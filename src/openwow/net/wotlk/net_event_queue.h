#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace openwow::net {

static constexpr int kNetEventTypeMessage     = 24;
static constexpr int kNetEventTypeConnected   = 25;
static constexpr int kNetEventTypeDataReady   = 26;
static constexpr int kNetEventTypeDisconnect  = 27;
static constexpr int kNetEventTypeShutdown    = 28;
static constexpr int kNetEventTypeAuthReady   = 29;

struct NetEventQueueNode {
    NetEventQueueNode* next;
    NetEventQueueNode* prev;
    uint32_t event_id;
    uint32_t timestamp;
    void*    payload;
    uint32_t payload_size;
};

struct NetEventQueue {
    void*     owner;
    alignas(std::mutex) std::byte mutex_storage[sizeof(std::mutex)];
    int32_t   pending_count;
    NetEventQueueNode list_sentinel;
};

NetEventQueue* NetEventQueue_Construct(NetEventQueue* self, void* owner);

void NetEventQueue_DrainNodes(NetEventQueueNode* sentinel);

void NetEventQueue_Flush(NetEventQueue* self);

void NetEventQueue_PostEvent(NetEventQueue* self, int event_id,
                             int connection, void* net_client,
                             void* payload, uint32_t payload_size);

void NetEventQueue_Cleanup(NetEventQueue* self);

struct NetEventProcessDispatch {

    void (*on_message)(void* owner, uint32_t timestamp, void* data,
                       uint32_t size);

    void (*on_connected)(void* owner);

    void (*on_disconnected)(void* owner);

    void (*on_cant_connect)(void* owner);

    void (*on_auth_ready)(void* owner, void* data);
    void (*check_ping)(void* owner);
    void (*destroy)(void* owner);
};

void NetEventQueue_ProcessEvents(NetEventQueue* self,
                                  const NetEventProcessDispatch& dispatch);

}

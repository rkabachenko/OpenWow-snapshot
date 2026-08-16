
#include "openwow/net/wotlk/net_event_queue.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/net/wotlk/wow_client_connection.h"

#include <cstring>
#include <mutex>
#include <new>
#include "openwow/foundation/compiler/atomic_ops.h"

namespace openwow::net {

static std::mutex& GetQueueMutex(NetEventQueue* self) {
    return *std::launder(reinterpret_cast<std::mutex*>(self->mutex_storage));
}

NetEventQueue* NetEventQueue_Construct(NetEventQueue* self, void* owner) {
    new (self->mutex_storage) std::mutex();
    self->pending_count = 0;
    self->list_sentinel.next = nullptr;
    self->list_sentinel.prev = nullptr;
    self->list_sentinel.event_id = 0;
    self->list_sentinel.timestamp = 0;
    self->list_sentinel.payload = nullptr;
    self->list_sentinel.payload_size = 0;
    self->owner = owner;
    return self;
}

void NetEventQueue_DrainNodes(NetEventQueueNode* sentinel) {
    NetEventQueueNode* node = sentinel->next;
    while (node && node != sentinel) {
        NetEventQueueNode* next_node = node->next;

        if (node->payload) {

            delete[] static_cast<uint8_t*>(node->payload);
            node->payload = nullptr;
        }

        if (node->next) {
            node->next->prev = node->prev;
        }
        if (node->prev) {
            node->prev->next = node->next;
        }
        node->next = nullptr;
        node->prev = nullptr;

        delete node;

        node = next_node;
    }

    sentinel->next = nullptr;
    sentinel->prev = nullptr;
}

void NetEventQueue_Flush(NetEventQueue* self) {
    std::lock_guard<std::mutex> lock(GetQueueMutex(self));
    NetEventQueue_DrainNodes(&self->list_sentinel);
}

void NetEventQueue_PostEvent(NetEventQueue* self, int event_id,
                             int , void* net_client,
                             void* payload, uint32_t payload_size) {
    if (!self) {
        return;
    }
    std::lock_guard<std::mutex> lock(GetQueueMutex(self));

    auto* node = new (std::nothrow) NetEventQueueNode();
    if (!node) {
        return;
    }
    node->next = nullptr;
    node->prev = nullptr;
    node->event_id = static_cast<uint32_t>(event_id);
    node->timestamp = openwow::core::GameClock::GetTickCount32();

    if (payload_size > 0 && payload) {
        auto* buf = new (std::nothrow) uint8_t[payload_size];
        if (!buf) {
            delete node;
            return;
        }
        std::memcpy(buf, payload, payload_size);
        node->payload = buf;
        node->payload_size = payload_size;
    } else {
        node->payload = nullptr;
        node->payload_size = 0;
    }

    node->next = nullptr;
    node->prev = self->list_sentinel.prev
        ? self->list_sentinel.prev
        : &self->list_sentinel;
    if (self->list_sentinel.prev) {
        self->list_sentinel.prev->next = node;
    } else {
        self->list_sentinel.next = node;
    }
    self->list_sentinel.prev = node;

    if (net_client != nullptr) {
        auto* connection = static_cast<WowClientConnection*>(net_client);
        openwow::compiler::AtomicFetchAddSeqCst(
            const_cast<int32_t*>(&connection->pending_event_count), 1);
    }
}

void NetEventQueue_Cleanup(NetEventQueue* self) {
    {
        std::lock_guard<std::mutex> lock(GetQueueMutex(self));
        NetEventQueue_DrainNodes(&self->list_sentinel);
    }
    GetQueueMutex(self).~mutex();
}

void NetEventQueue_ProcessEvents(NetEventQueue* self,
                                  const NetEventProcessDispatch& dispatch) {
    std::lock_guard<std::mutex> lock(GetQueueMutex(self));

    auto* connection = static_cast<WowClientConnection*>(self->owner);
    if (!connection) return;

    openwow::compiler::AtomicFetchAddSeqCst(const_cast<int32_t*>(&connection->pending_event_count), 1);

    bool shutdown_triggered = false;

    NetEventQueueNode* node = self->list_sentinel.next;
    while (node && node != &self->list_sentinel) {
        if (!connection->shutdown_pending) {
            switch (node->event_id) {
                case kNetEventTypeMessage:
                    if (dispatch.on_message) {
                        dispatch.on_message(connection, node->timestamp,
                                            node->payload, node->payload_size);
                    }
                    break;
                case kNetEventTypeConnected:
                    if (dispatch.on_connected) {
                        dispatch.on_connected(connection);
                    }
                    break;
                case kNetEventTypeDataReady:
                    if (dispatch.on_disconnected) {
                        dispatch.on_disconnected(connection);
                    }
                    break;
                case kNetEventTypeDisconnect:
                    if (dispatch.on_cant_connect) {
                        dispatch.on_cant_connect(connection);
                    }
                    break;
                case kNetEventTypeShutdown:
                    connection->shutdown_pending = 1;
                    shutdown_triggered = true;
                    break;
                case kNetEventTypeAuthReady:
                    if (dispatch.on_auth_ready) {
                        dispatch.on_auth_ready(connection, node->payload);
                    }
                    break;
                default:
                    break;
            }
        }

        int32_t new_count =
            openwow::compiler::AtomicFetchSubSeqCst(const_cast<int32_t*>(&connection->pending_event_count), 1) - 1;
        if (new_count == 0 && connection->shutdown_pending) {
            if (dispatch.destroy) {
                dispatch.destroy(connection);
            }
        }

        node = node->next;
    }

    if (!shutdown_triggered) {
        if (dispatch.check_ping) {
            dispatch.check_ping(connection);
        }
    }

    int32_t final_count =
        openwow::compiler::AtomicFetchSubSeqCst(const_cast<int32_t*>(&connection->pending_event_count), 1)
 - 1;
    if (final_count == 0 && connection->shutdown_pending) {
        if (dispatch.destroy) {
            dispatch.destroy(connection);
        }
    }

    NetEventQueue_DrainNodes(&self->list_sentinel);
}

}

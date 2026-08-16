#pragma once

#include "openwow/net/transport/wow_connection.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <vector>

namespace openwow::net {

struct NetEventQueue;

static constexpr int kMaxOpcodes         = 1311;
static constexpr int kMaxRttSamples      = 16;

static constexpr std::size_t kAuthBlockSize = 0x530;
static constexpr std::size_t kAccountNameCapacity = 0x500;
static constexpr std::size_t kLoginServerIdOffsetInAuthBlock = 0x500;
static constexpr std::size_t kSessionKeyOffsetInAuthBlock = 0x504;
static constexpr std::size_t kSessionKeySize = 40;
static constexpr std::size_t kLoginServerTypeOffsetInAuthBlock = 0x52C;

enum WowClientState : uint32_t {
    kStateUninitialized = 0,
    kStateInitialized   = 2,
    kStateConnecting    = 4,
    kStateConnected     = 5,
    kStateTransfer      = 6,
};

using OpcodeHandlerFn = int(*)(uintptr_t context, int opcode,
                               int connection_id, void* data_store);

struct HeldMessage {
    HeldMessage* next;
    HeldMessage* prev;
    std::vector<std::uint8_t> packet_bytes;
};

struct WowClientConnection {

    void* vtable_ptr;

    char account_name[kAccountNameCapacity];
    std::uint32_t login_server_id;
    std::array<std::uint8_t, kSessionKeySize> session_key;
    std::uint32_t login_server_type;

    WowClientState state;

    uint8_t pending_auth;
    uint8_t pad_1337[3];

    OpcodeHandlerFn opcode_handlers[kMaxOpcodes];

    uintptr_t opcode_contexts[kMaxOpcodes];

    NetEventQueue* event_queue;

    void* primary_connection;

    void* secondary_connection;

    uint32_t transfer_cookie;

    volatile int32_t pending_event_count;

    uint8_t shutdown_pending;
    uint8_t pad_11849[3];

    uint32_t ping_timestamp;

    uint32_t ping_sequence;

    uint32_t rtt_samples[kMaxRttSamples];

    uint32_t rtt_read_idx;

    uint32_t rtt_write_idx;

    uint32_t session_bytes_sent;

    volatile int32_t session_bytes_recv;

    uint32_t connection_timestamp;

    std::mutex stats_cs;

    HeldMessage* held_msg_head;

    WowConnectionResponse connection_response;

    std::function<void(std::uint16_t)> opcode_observer;
    std::function<void(std::uint32_t)> disconnect_callback;
};

WowClientConnection* WowClientConnection_Construct(WowClientConnection* self);
std::size_t WowClientConnection_RegistryCount();
bool WowClientConnection_IsRegistered(const WowClientConnection* self);

int WowClientConnection_Initialize(WowClientConnection* self);

bool WowClientConnection_SetAuthBlock(
    WowClientConnection* self,
    std::span<const std::uint8_t, kAuthBlockSize> auth_block);
[[nodiscard]] std::span<const std::uint8_t, kSessionKeySize>
WowClientConnection_GetSessionKey(const WowClientConnection* self);

void WowClientConnection_DestructorImpl(WowClientConnection* self);

void WowClientConnection_Destructor(WowClientConnection* self, bool free_memory);

void WowClientConnection_ScalarDeletingDestructor(WowClientConnection* self, bool free);

int WowClientConnection_ConnectToAddress(WowClientConnection* self,
                                         const char* address);

int WowClientConnection_InitiateConnect(WowClientConnection* self,
                                        const char* host,
                                        uint16_t port);

void WowClientConnection_Disconnect(WowClientConnection* self);

int WowClientConnection_Shutdown(WowClientConnection* self);

void WowClientConnection_PrintStatsAndCleanup(WowClientConnection* self);

int WowClientConnection_RegisterOpcodeHandler(WowClientConnection* self,
                                               int opcode,
                                               OpcodeHandlerFn handler,
                                               uintptr_t context);

int WowClientConnection_UnregisterOpcodeHandler(WowClientConnection* self,
                                                 int opcode);

int WowClientConnection_DispatchOpcode(WowClientConnection* self,
                                        int connection_id,
                                        void* data_store,
                                        int flags);

void WowClientConnection_OnConnected(WowClientConnection* self,
                                      void* connection, int a3,
                                      uint32_t timestamp, int a5);

bool WowClientConnection_ProofOfWork(const void* sha1_state,
                                      uint32_t difficulty,
                                      uint32_t* nonce_out);

int WowClientConnection_OnMessageReceived(WowClientConnection* self,
                                           int connection_id,
                                           void* data, int size);

int WowClientConnection_HandleEnterWorld(WowClientConnection* self);

int WowClientConnection_HandleDisconnect(WowClientConnection* self);

int WowClientConnection_HandleLogoutComplete(WowClientConnection* self);

void WowClientConnection_HandlePong(WowClientConnection* self,
                                     void* connection,
                                     void* data_store);

void WowClientConnection_InitEncryption(WowClientConnection* self,
                                         void* connection,
                                         const char* server_seed,
                                         uint8_t seed_len);

void WowClientConnection_OnDataReady(WowClientConnection* self,
                                      int connection, int a3, int a4);

void WowClientConnection_HandleAuthChallenge(WowClientConnection* self,
                                              void* connection,
                                              void* data_store);

HeldMessage* WowClientConnection_AllocHeldMessage(void* list_ptr,
                                                    int opcode,
                                                    int size, uint8_t flags);

void WowClientConnection_SendPacket(WowClientConnection* self,
                                     void* data_store);

void WowClientConnection_OnTransferDisconnected(WowClientConnection* self,
                                                void* connection, int a3, int a4);

void WowClientConnection_HandleReconnect(WowClientConnection* self,
                                          void* connection,
                                          void* data_store);

void WowClientConnection_HandleTransferRedirect(WowClientConnection* self,
                                                  void* connection,
                                                  void* data_store);

void* WowClientConnection_FreeHeldMessage(void* list_ptr, HeldMessage* msg);

int WowClientConnection_HandleTransferComplete(WowClientConnection* self,
                                                void* connection, int a3);

void WowClientConnection_MessageHandler(WowClientConnection* self,
                                         void* connection,
                                         int msg_type,
                                         void* data_store);

void WowClientConnection_GetNetworkStats(WowClientConnection* self,
                                          float* bandwidth_in_kbps,
                                          float* bandwidth_out_kbps,
                                          uint32_t* latency_ms);

struct NetEventProcessDispatch;
void WowClientConnection_ProcessEventQueue(
    WowClientConnection* self, const NetEventProcessDispatch& dispatch);

void WowClientConnection_SendPingIfNeeded(WowClientConnection* self);

extern uint32_t g_total_bytes_sent;
extern uint32_t g_total_packets_sent;
extern uint32_t g_total_bytes_received;
extern uint32_t g_total_opcodes_dispatched;
extern int32_t  g_active_netclient_count;
extern uint32_t g_netclient_init_timestamp;

}


#include "openwow/net/wotlk/wow_client_connection.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_alloc.h"
#include "openwow/core/storm_string.h"
#include "openwow/net/serialization/cdatastore_ops.h"
#include "openwow/net/serialization/cdatastore_vtable.h"
#include "openwow/net/serialization/temp_world_packet_storage.h"
#include "openwow/net/wotlk/net_event_queue.h"
#include "openwow/net/wotlk/proof_of_work.h"
#include "openwow/net/wotlk/protocol/world_header_crypto.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/net/net_client.h"
#include "openwow/net/transport/wow_connection.h"
#include "openwow/foundation/hashing/retail_sha1.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <new>
#include <span>
#include <string_view>
#include <vector>

#include <SDL2/SDL_timer.h>
#include "openwow/foundation/compiler/atomic_ops.h"

namespace openwow::net {
extern uint32_t g_total_bytes_sent;
extern uint32_t g_total_packets_sent;

namespace {

class WowClientConnectionRegistry {
public:
    static WowClientConnectionRegistry& Instance() {
        static auto* registry = new WowClientConnectionRegistry();
        return *registry;
    }

    void Register(WowClientConnection* instance) {
        std::scoped_lock lock(mutex_);
        active_instances_.push_front(instance);
    }

    void Unregister(const WowClientConnection* instance) {
        std::scoped_lock lock(mutex_);
        const auto it =
            std::find(active_instances_.begin(), active_instances_.end(), instance);
        if (it != active_instances_.end()) {
            active_instances_.erase(it);
        }
    }

    [[nodiscard]] std::size_t Count() const {
        std::scoped_lock lock(mutex_);
        return active_instances_.size();
    }

    [[nodiscard]] bool Contains(const WowClientConnection* instance) const {
        std::scoped_lock lock(mutex_);
        return std::find(active_instances_.begin(), active_instances_.end(), instance) !=
               active_instances_.end();
    }

private:
    mutable std::mutex mutex_;
    std::list<WowClientConnection*> active_instances_;
};

constexpr std::uint32_t kTransferResumeOpcode = 1294u;
constexpr std::uint32_t kTransferSuspendOpcode = 1296u;
constexpr std::uint32_t kReadPosSentinel = 0xFFFFFFFFu;
constexpr std::uint32_t kWindowSizeSentinel = 0xFFFFFFFFu;
constexpr std::size_t kTransferDigestSize = 20u;

std::span<const std::uint8_t, kSessionKeySize> GetSessionKeyBytes(
    const WowClientConnection& self) {
    return self.session_key;
}

std::string_view GetBoundedAccountName(const WowClientConnection& self) {
    const auto* const end =
        std::find(self.account_name, self.account_name + kAccountNameCapacity,
                  '\0');
    return std::string_view(
        self.account_name,
        static_cast<std::size_t>(end - self.account_name));
}

bool ReadU32Le(CDataStore& store, std::uint32_t& value) {
    std::array<std::uint8_t, 4> bytes{};
    CDataStore_GetBytes(store, bytes.data(), bytes.size());
    if (store.read_pos > store.write_pos) {
        return false;
    }
    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8U) |
            (static_cast<std::uint32_t>(bytes[2]) << 16U) |
            (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

void PutU32Le(CDataStore& store, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U),
    };
    CDataStore_PutBytes(store, bytes.data(), bytes.size());
}

void PutU64Le(CDataStore& store, const std::uint64_t value) {
    PutU32Le(store, static_cast<std::uint32_t>(value));
    PutU32Le(store, static_cast<std::uint32_t>(value >> 32U));
}

void AppendHeldMessage(HeldMessage*& head, HeldMessage* msg) {
    if (!msg) {
        return;
    }

    if (!head) {
        head = msg;
        return;
    }

    auto* tail = head;
    while (tail->next) {
        tail = tail->next;
    }
    tail->next = msg;
    msg->prev = tail;
}

void InitTempPacket(CDataStoreTempPacket& packet) {
    packet.store.data = nullptr;
    packet.store.window_base = 0;
    packet.store.window_size = 0;
    packet.store.write_pos = 0;
    packet.store.read_pos = kReadPosSentinel;
    packet.store.vtable = CDataStore_TempWorldPacketVTable();
    CDataStore__InitTempWorldPacketStorage(
        &packet, &packet.store.data, &packet.store.window_base,
        reinterpret_cast<int*>(&packet.store.window_size));
}

void DestroyTempPacket(CDataStoreTempPacket& packet) {
    packet.store.vtable = CDataStore_TempWorldPacketVTable();
    if (packet.store.window_size != kWindowSizeSentinel) {
        CDataStore__CleanupTempWorldPacketStorage(
            &packet, &packet.store.data, &packet.store.window_base,
            reinterpret_cast<int*>(&packet.store.window_size));
    }
    CDataStore_Dtor(&packet.store);
}

void BuildTransferCookiePacket(CDataStoreTempPacket& packet,
                               const std::uint32_t opcode,
                               const std::uint32_t transfer_cookie) {
    InitTempPacket(packet);
    CDataStore_PutUInt32(packet.store, opcode);
    CDataStore_PutUInt32(packet.store, transfer_cookie);
    packet.store.read_pos = 0;
}

std::array<std::uint8_t, kTransferDigestSize> ComputeReconnectDigest(
    const WowClientConnection& self,
    const std::uint32_t address_v4,
    const std::uint16_t port) {
    return wotlk::ComputeSha1PadHmac(
        GetSessionKeyBytes(self),
        {reinterpret_cast<const std::uint8_t*>(&address_v4), sizeof(address_v4)},
        {reinterpret_cast<const std::uint8_t*>(&port), sizeof(port)});
}

void SendPacketBytes(WowClientConnection* self,
                     std::span<const std::uint8_t> packet_bytes) {
    if (!self || self->state != kStateConnected || !self->primary_connection ||
        packet_bytes.empty()) {
        return;
    }

    auto* connection = static_cast<WowConnection*>(self->primary_connection);
    connection->Send(packet_bytes.data(), packet_bytes.size());

    g_total_bytes_sent += static_cast<std::uint32_t>(packet_bytes.size());
    ++g_total_packets_sent;
    self->session_bytes_sent += static_cast<std::uint32_t>(packet_bytes.size());

    if (!connection->IsEncryptionEnabled()) {
        WowClientConnection_InitEncryption(self, self->primary_connection, nullptr, 0);
    }
}

void SendPacketBytesToConnection(void* connection,
                                 const std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return;
    }
    if (auto* managed = WowConnection_TryGetManaged(connection)) {
        managed->Send(bytes.data(), bytes.size());
    }
}

void CloseAndReleaseConnection(void*& connection) {
    if (!connection) {
        return;
    }

    if (auto* managed = WowConnection_TryGetManaged(connection)) {
        managed->SetHandlerSynchronously(nullptr);
        managed->Close();
    }

    WowConnection_Release(connection);
    connection = nullptr;
}

void CloseConnectionWithoutRelease(void* connection) {
    if (auto* managed = WowConnection_TryGetManaged(connection)) {
        managed->Close();
    }
}

void ConfigureConnectionResponse(WowClientConnection* self) {
    if (!self || self->connection_response.on_framed_packet) {
        return;
    }

    self->connection_response.on_framed_packet =
        [self](WowConnection* connection, const std::uint64_t timestamp,
               CDataStore& packet) {
            WowClientConnection_MessageHandler(
                self, connection, static_cast<int>(timestamp), &packet);
        };
    self->connection_response.on_connect =
        [self](WowConnection* connection, const std::uint64_t timestamp) {
            WowClientConnection_OnConnected(
                self, connection, 0, static_cast<std::uint32_t>(timestamp), 0);
        };
    self->connection_response.on_connect_failed =
        [self](WowConnection* connection, const std::uint64_t) {
            WowClientConnection_OnTransferDisconnected(
                self, connection, 0, 0);
        };
    self->connection_response.on_disconnect =
        [self](WowConnection*) {
            WowClientConnection_OnDataReady(self, 0, 0, 0);
        };
}

WowConnection* AllocateConnection(WowClientConnection* self) {
    ConfigureConnectionResponse(self);
    auto* connection = new (std::nothrow) WowConnection();
    if (connection) {
        connection->SetHandler(&self->connection_response);
    }
    return connection;
}

}

uint32_t g_total_bytes_sent          = 0;

uint32_t g_total_packets_sent        = 0;

uint32_t g_total_bytes_received      = 0;

uint32_t g_total_opcodes_dispatched  = 0;

int32_t  g_active_netclient_count    = 0;

uint32_t g_netclient_init_timestamp  = 0;

std::size_t WowClientConnection_RegistryCount() {
    return WowClientConnectionRegistry::Instance().Count();
}

bool WowClientConnection_IsRegistered(const WowClientConnection* self) {
    return WowClientConnectionRegistry::Instance().Contains(self);
}

WowClientConnection* WowClientConnection_Construct(WowClientConnection* self) {
    self->vtable_ptr = nullptr;

    self->state = kStateUninitialized;
    self->pending_auth = 0;
    self->event_queue = nullptr;
    self->primary_connection = nullptr;
    self->secondary_connection = nullptr;
    self->shutdown_pending = 0;
    self->held_msg_head = nullptr;
    self->pending_event_count = 0;
    self->transfer_cookie = 0;

    self->ping_timestamp = 0;
    self->ping_sequence = 0;
    self->rtt_read_idx = 0;
    self->rtt_write_idx = 0;
    self->session_bytes_sent = 0;
    self->session_bytes_recv = 0;
    self->connection_timestamp = 0;

    std::memset(self->account_name, 0, kAccountNameCapacity);
    self->login_server_id = 0;
    self->session_key.fill(0);
    self->login_server_type = 0;
    std::memset(self->opcode_handlers, 0, sizeof(self->opcode_handlers));
    std::memset(self->opcode_contexts, 0, sizeof(self->opcode_contexts));
    std::memset(self->rtt_samples, 0, sizeof(self->rtt_samples));

    self->connection_response = {};
    self->opcode_observer = {};
    self->disconnect_callback = {};
    ConfigureConnectionResponse(self);

    WowClientConnectionRegistry::Instance().Register(self);

    return self;
}

namespace {
void* g_evt_context_tls_value = nullptr;

void NetClient_EvtContextPropagateCallback() {
    void* current = core::GetEvtContextTlsValue();
    if (current != g_evt_context_tls_value) {
        core::SetEvtContextTlsValue(g_evt_context_tls_value);
    }
}
}

int WowClientConnection_Initialize(WowClientConnection* self) {
    if (g_active_netclient_count > 0
        || (g_evt_context_tls_value = core::GetEvtContextTlsValue(),
            WowConnection_InitializeNetworkSystem(
                nullptr, NetClient_EvtContextPropagateCallback, 1, 0) != 0)) {
        ++g_active_netclient_count;

        auto* queue = new (std::nothrow) NetEventQueue();
        if (queue) {
            NetEventQueue_Construct(queue, self);
        }
        self->event_queue = queue;

        std::memset(self->opcode_handlers, 0, sizeof(self->opcode_handlers));
        std::memset(self->opcode_contexts, 0, sizeof(self->opcode_contexts));

        self->primary_connection = AllocateConnection(self);

        self->state = kStateInitialized;

        g_netclient_init_timestamp = SDL_GetTicks();

        return 1;
    }
    return 0;
}

bool WowClientConnection_SetAuthBlock(
    WowClientConnection* self,
    const std::span<const std::uint8_t, kAuthBlockSize> auth_block) {
    if (!self) {
        return false;
    }

    std::memcpy(self->account_name, auth_block.data(), kAccountNameCapacity);
    std::memcpy(&self->login_server_id,
                auth_block.data() + kLoginServerIdOffsetInAuthBlock,
                sizeof(self->login_server_id));
    std::copy_n(auth_block.begin() + kSessionKeyOffsetInAuthBlock,
                kSessionKeySize, self->session_key.begin());
    std::memcpy(&self->login_server_type,
                auth_block.data() + kLoginServerTypeOffsetInAuthBlock,
                sizeof(self->login_server_type));
    return true;
}

std::span<const std::uint8_t, kSessionKeySize>
WowClientConnection_GetSessionKey(const WowClientConnection* self) {
    if (!self) {
        static constexpr std::array<std::uint8_t, kSessionKeySize> kEmpty{};
        return kEmpty;
    }
    return self->session_key;
}

void WowClientConnection_DestructorImpl(WowClientConnection* self) {
    self->vtable_ptr = nullptr;

    WowClientConnection_PrintStatsAndCleanup(self);

    WowClientConnectionRegistry::Instance().Unregister(self);
}

void WowClientConnection_Destructor(WowClientConnection* self, bool free_memory) {
    WowClientConnection_DestructorImpl(self);
    if (free_memory) {
        delete self;
    }
}

void WowClientConnection_ScalarDeletingDestructor(WowClientConnection* self, bool free) {
    self->vtable_ptr = nullptr;
    if (free) {
        delete self;
    }
}

int WowClientConnection_ConnectToAddress(WowClientConnection* self,
                                         const char* address) {
    if (self->state != kStateInitialized) {
        openwow::core::SErrFatalCondition(
            "Expected (m_netState == NS_INITIALIZED), got %d",
            static_cast<int>(self->state));
    }

    uint16_t port = 9090;
    char buf[1024];
    openwow::core::SStrCopy(buf, address, sizeof(buf));

    char* colon = std::strchr(buf, ':');
    if (colon) {
        *colon = '\0';
        port = static_cast<uint16_t>(std::atol(colon + 1));
    }

    if (auto* conn = WowConnection_TryGetManaged(self->primary_connection)) {
        conn->EnableEncryption(false);
    }

    self->state = kStateInitialized;
    return WowClientConnection_InitiateConnect(self, buf, port);
}

int WowClientConnection_InitiateConnect(WowClientConnection* self,
                                        const char* host,
                                        uint16_t port) {
    if (self->state != kStateInitialized) {
        openwow::core::SErrFatalCondition(
            "Expected (m_netState == NS_INITIALIZED), got %d",
            static_cast<int>(self->state));
    }

    self->state = kStateConnecting;
    WowConnection_ConnectByAddressString(
        self->primary_connection, host, port, -1);

    if (self->secondary_connection) {
        if (auto* sec = WowConnection_TryGetManaged(self->secondary_connection)) {
            sec->SetHandlerSynchronously(nullptr);
            sec->Close();
        }
        WowConnection_Release(self->secondary_connection);
        self->secondary_connection = nullptr;
    }

    return 1;
}

void WowClientConnection_Disconnect(WowClientConnection* self) {
    if (!self) {
        return;
    }

    if (self->secondary_connection) {
        CloseAndReleaseConnection(self->secondary_connection);
    }

    if (self->state == kStateConnected) {
        self->state = kStateTransfer;

        CloseConnectionWithoutRelease(self->primary_connection);
    } else {
        if (self->event_queue) {
            NetEventQueue_Flush(self->event_queue);
        }
        if (self->primary_connection) {
            CloseAndReleaseConnection(self->primary_connection);
        }

        self->primary_connection = AllocateConnection(self);
        self->state = kStateInitialized;
    }
}

int WowClientConnection_Shutdown(WowClientConnection* self) {
    if (self->state == kStateUninitialized) return 0;

    WowClientConnection_Disconnect(self);
    if (self->event_queue) {
        NetEventQueue_PostEvent(self->event_queue, kNetEventTypeShutdown, 0,
                                self, nullptr, 0);
    }
    return 1;
}

void WowClientConnection_PrintStatsAndCleanup(WowClientConnection* self) {
    if (self->state == kStateUninitialized) return;

    WowClientConnection_Disconnect(self);

    if (self->primary_connection) {
        CloseAndReleaseConnection(self->primary_connection);
    }

    std::memset(self->opcode_handlers, 0, sizeof(self->opcode_handlers));
    std::memset(self->opcode_contexts, 0, sizeof(self->opcode_contexts));

    if (self->event_queue) {
        NetEventQueue_Cleanup(self->event_queue);
        delete self->event_queue;
        self->event_queue = nullptr;
    }

    --g_active_netclient_count;
    if (g_active_netclient_count == 0) {
        SDL_Delay(1);
        NetClient_Cleanup();

        char message[192]{};
        std::snprintf(
            message, sizeof(message),
            "Client net stats: %u bytes sent, %u bytes received, %u msec elapsed",
            g_total_bytes_sent, g_total_bytes_received,
            SDL_GetTicks() - g_netclient_init_timestamp);
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, message);
    }

    self->state = kStateUninitialized;
}

int WowClientConnection_RegisterOpcodeHandler(WowClientConnection* self,
                                               int opcode,
                                               OpcodeHandlerFn handler,
                                               uintptr_t context) {
    if (opcode >= 0 && opcode < kMaxOpcodes) {
        self->opcode_handlers[opcode] = handler;
        self->opcode_contexts[opcode] = context;
    }
    return opcode;
}

int WowClientConnection_UnregisterOpcodeHandler(WowClientConnection* self,
                                                 int opcode) {
    if (opcode >= 0 && opcode < kMaxOpcodes) {
        self->opcode_handlers[opcode] = nullptr;
        self->opcode_contexts[opcode] = 0;
    }
    return opcode;
}

int WowClientConnection_DispatchOpcode(WowClientConnection* self,
                                        int connection_id,
                                        void* data_store,
                                        int ) {
    ++g_total_opcodes_dispatched;

    uint16_t opcode = 0;
    if (data_store) {
        auto& store = *static_cast<CDataStore*>(data_store);
        CDataStore_GetUInt16(store, &opcode);
    }

    if (self->opcode_observer) {
        self->opcode_observer(opcode);
    }

    if (opcode < kMaxOpcodes && self->opcode_handlers[opcode]) {
        return self->opcode_handlers[opcode](
            self->opcode_contexts[opcode], opcode, connection_id, data_store);
    }

    if (data_store) {
        auto& store = *static_cast<CDataStore*>(data_store);
        if (store.read_pos <= store.write_pos) {
            store.read_pos = store.write_pos;
        }
    }
    return 0;
}

void WowClientConnection_OnConnected(WowClientConnection* self,
                                      void* connection, int ,
                                      uint32_t timestamp, int ) {
    if (connection != self->primary_connection) return;

    {
        std::lock_guard<std::mutex> lock(self->stats_cs);
        self->connection_timestamp = timestamp;
        self->session_bytes_recv = 0;
        self->session_bytes_sent = 0;
        self->rtt_read_idx = 0;
        self->rtt_write_idx = 0;

        self->ping_timestamp = openwow::core::GameClock::GetTickCount32();
    }

    if (self->event_queue) {
        NetEventQueue_PostEvent(self->event_queue, kNetEventTypeConnected,
                                static_cast<int>(reinterpret_cast<std::intptr_t>(connection)),
                                self, nullptr, 0);
    }
}

bool WowClientConnection_ProofOfWork(const void* sha1_state,
                                      uint32_t difficulty,
                                      uint32_t* nonce_out) {
    if (!sha1_state || !nonce_out || difficulty > 160) {
        return false;
    }

    if (difficulty == 0) {
        nonce_out[0] = 0;
        nonce_out[1] = 0;
        return true;
    }

    const auto& src_ctx = *static_cast<const foundation::hashing::RetailSha1State*>(sha1_state);

    for (uint64_t nonce = 0; ; ++nonce) {
        foundation::hashing::RetailSha1State ctx = src_ctx;

        const uint32_t lo = static_cast<uint32_t>(nonce & 0xFFFFFFFFu);
        const uint32_t hi = static_cast<uint32_t>(nonce >> 32);
        foundation::hashing::UpdateRetailSha1(ctx, reinterpret_cast<const uint8_t*>(&lo), 4);
        foundation::hashing::UpdateRetailSha1(ctx, reinterpret_cast<const uint8_t*>(&hi), 4);

        std::array<uint8_t, 20> digest{};
        foundation::hashing::FinalizeRetailSha1(ctx, digest.data());

        uint32_t leading_zeros = 0;
        for (std::size_t i = 0; i < digest.size(); ++i) {
            if (digest[i] == 0) {
                leading_zeros += 8;
            } else {
                uint8_t b = digest[i];
                for (int bit = 0; bit < 8; ++bit) {
                    if ((b >> bit) & 1) break;
                    ++leading_zeros;
                }
                break;
            }
        }

        if (leading_zeros >= difficulty) {
            nonce_out[0] = lo;
            nonce_out[1] = hi;
            return true;
        }

        if (nonce == UINT64_MAX) break;
    }

    return false;
}

int WowClientConnection_OnMessageReceived(WowClientConnection* self,
                                           int connection_id,
                                           void* data, int size) {
    g_total_bytes_received += static_cast<uint32_t>(size) + 2;

    if (self->state == kStateConnected) {
        CDataStore packet_store{};
        packet_store.vtable      = CDataStore_BaseVTable();
        packet_store.data        = static_cast<std::uint8_t*>(data);
        packet_store.window_base = 0;
        packet_store.window_size = 0xFFFFFFFFu;
        packet_store.write_pos   = static_cast<std::uint32_t>(size);
        packet_store.read_pos    = 0;

        WowClientConnection_DispatchOpcode(self, connection_id,
                                            &packet_store, 0);

        packet_store.vtable = CDataStore_BaseVTable();
        if (packet_store.window_size != 0xFFFFFFFFu) {
            CDataStore_Free(packet_store.data, packet_store.window_base,
                            packet_store.window_size);
        }
    }

    return 1;
}

int WowClientConnection_HandleEnterWorld(WowClientConnection* self) {
    self->state = kStateConnected;
    return 1;
}

int WowClientConnection_HandleDisconnect(WowClientConnection* self) {
    self->state = kStateInitialized;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "NetClient::HandleDisconnect()");
    if (self->disconnect_callback) {
        self->disconnect_callback(0);
    }
    return 1;
}

int WowClientConnection_HandleLogoutComplete(WowClientConnection* self) {
    self->state = kStateInitialized;
    return 1;
}

void WowClientConnection_HandlePong(WowClientConnection* self,
                                     void* connection,
                                     void* data_store) {
    if (!self || !data_store) {
        return;
    }
    if (connection != self->primary_connection || self->pending_auth) {
        CloseConnectionWithoutRelease(connection);
        return;
    }

    auto& store = *static_cast<CDataStore*>(data_store);
    std::lock_guard<std::mutex> lock(self->stats_cs);

    uint32_t recv_seq = 0;
    CDataStore_GetUInt32(store, &recv_seq);

    if (recv_seq != self->ping_sequence) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                           "Received pong with old sequence");
        return;
    }

    const uint32_t now = openwow::core::GameClock::GetTickCount32();
    const uint32_t rtt = now - self->ping_timestamp;
    self->rtt_samples[self->rtt_write_idx] = rtt;
    ++self->rtt_write_idx;
    if (self->rtt_write_idx >= kMaxRttSamples)
        self->rtt_write_idx = 0;

    if (self->rtt_write_idx == self->rtt_read_idx) {
        ++self->rtt_read_idx;
        if (self->rtt_read_idx >= kMaxRttSamples)
            self->rtt_read_idx = 0;
    }
}

void WowClientConnection_InitEncryption(WowClientConnection* self,
                                         void* connection,
                                         const char* server_seed,
                                         const uint8_t seed_len) {
    auto* wow_connection = WowConnection_TryGetManaged(connection);
    if (!self || !wow_connection) {
        return;
    }

    const auto session_key = GetSessionKeyBytes(*self);
    wow_connection->InitCryptoKeys(
        session_key.data(), session_key.size(),
        reinterpret_cast<const std::uint8_t*>(server_seed), seed_len);
    wow_connection->SetHeaderCryptBytes(4, 2);
    wow_connection->EnableEncryption(true);
}

void WowClientConnection_OnDataReady(WowClientConnection* self,
                                      int connection, int , int ) {
    float bandwidth_in = 0.0F;
    float bandwidth_out = 0.0F;
    std::uint32_t latency = 0;
    WowClientConnection_GetNetworkStats(
        self, &bandwidth_in, &bandwidth_out, &latency);

    if (self->event_queue) {
        NetEventQueue_PostEvent(self->event_queue, kNetEventTypeDataReady,
                                connection, self, nullptr, 0);
    }
}

void WowClientConnection_HandleAuthChallenge(WowClientConnection* self,
                                              void* connection,
                                              void* data_store) {
    if (!self || !data_store) {
        return;
    }

    auto& store = *static_cast<CDataStore*>(data_store);

    uint32_t difficulty = 0;
    uint32_t server_seed = 0;
    std::array<std::uint8_t, 32> challenge_bytes{};
    if (!ReadU32Le(store, difficulty) || !ReadU32Le(store, server_seed)) {
        CloseConnectionWithoutRelease(connection);
        return;
    }
    CDataStore_GetBytes(store, challenge_bytes.data(), challenge_bytes.size());
    if (store.read_pos > store.write_pos) {
        CloseConnectionWithoutRelease(connection);
        return;
    }

    const std::string_view account_name = GetBoundedAccountName(*self);
    foundation::hashing::RetailSha1State sha1_ctx;
    foundation::hashing::InitializeRetailSha1(sha1_ctx);
    foundation::hashing::UpdateRetailSha1(sha1_ctx,
        reinterpret_cast<const uint8_t*>(account_name.data()),
        static_cast<uint32_t>(account_name.size()));
    foundation::hashing::UpdateRetailSha1(sha1_ctx, challenge_bytes.data(), challenge_bytes.size());

    uint32_t nonce[2] = {};
    if (!WowClientConnection_ProofOfWork(&sha1_ctx, difficulty, nonce)) {
        CloseConnectionWithoutRelease(connection);
        return;
    }

    if (connection == self->primary_connection) {

        std::array<std::uint8_t, 12> proof_record{};
        for (std::size_t index = 0; index < 4; ++index) {
            proof_record[index] =
                static_cast<std::uint8_t>(server_seed >> (index * 8U));
            proof_record[index + 4] =
                static_cast<std::uint8_t>(nonce[0] >> (index * 8U));
            proof_record[index + 8] =
                static_cast<std::uint8_t>(nonce[1] >> (index * 8U));
        }
        if (self->event_queue) {
            NetEventQueue_PostEvent(self->event_queue, kNetEventTypeAuthReady,
                                    static_cast<int>(reinterpret_cast<std::intptr_t>(connection)),
                                    self, proof_record.data(),
                                    proof_record.size());
        }
    } else if (connection == self->secondary_connection) {

        CDataStoreTempPacket response{};
        InitTempPacket(response);
        PutU32Le(response.store, 0x512U);
        CDataStore_PutString(response.store,
                             std::string(account_name).c_str());
        const std::uint64_t nonce64 =
            static_cast<std::uint64_t>(nonce[0]) |
            (static_cast<std::uint64_t>(nonce[1]) << 32U);
        PutU64Le(response.store, nonce64);

        const auto session_key = GetSessionKeyBytes(*self);
        const auto transfer_digest = wotlk::ComputeRealmRedirectionAuthDigest(
            account_name, session_key, server_seed);
        CDataStore_PutBytes(response.store, transfer_digest.data(),
                            transfer_digest.size());
        response.store.read_pos = 0;

        const std::uint8_t* packet_bytes = nullptr;
        if (CDataStore_GetReadSpanPointer(
                response.store, packet_bytes, response.store.write_pos)) {
            SendPacketBytesToConnection(
                connection,
                std::span<const std::uint8_t>(
                    packet_bytes, response.store.write_pos));
            WowClientConnection_InitEncryption(
                self, connection,
                reinterpret_cast<const char*>(challenge_bytes.data()),
                static_cast<std::uint8_t>(challenge_bytes.size()));
        } else {
            CloseConnectionWithoutRelease(connection);
        }
        DestroyTempPacket(response);
    } else {
        CloseConnectionWithoutRelease(connection);
    }
}

HeldMessage* WowClientConnection_AllocHeldMessage(void* list_ptr,
                                                  int insert_mode,
                                                  int size, uint8_t ) {
    auto* msg = new HeldMessage();
    msg->next = nullptr;
    msg->prev = nullptr;
    (void)size;

    if (list_ptr && insert_mode != 0) {
        auto*& head = *static_cast<HeldMessage**>(list_ptr);
        AppendHeldMessage(head, msg);
    }

    return msg;
}

void WowClientConnection_SendPacket(WowClientConnection* self,
                                     void* data_store) {
    if (!self) return;
    if (self->state != kStateConnected) return;
    if (!data_store) return;

    auto& store = *static_cast<CDataStore*>(data_store);
    if (store.read_pos > store.write_pos) return;

    const auto unread_bytes = store.write_pos - store.read_pos;
    if (unread_bytes == 0) return;

    if (self->pending_auth) {
        auto* held =
            WowClientConnection_AllocHeldMessage(&self->held_msg_head, 2, 0, 0);
        if (held) {
            const std::uint8_t* packet_bytes = nullptr;
            if (!CDataStore_GetReadSpanPointer(store, packet_bytes, unread_bytes)) {
                WowClientConnection_FreeHeldMessage(&self->held_msg_head, held);
                return;
            }

            held->packet_bytes.assign(packet_bytes, packet_bytes + unread_bytes);
        }
    } else {
        const std::uint8_t* packet_bytes = nullptr;
        if (!CDataStore_GetReadSpanPointer(store, packet_bytes, unread_bytes)) {
            return;
        }

        SendPacketBytes(
            self, std::span<const std::uint8_t>(packet_bytes, unread_bytes));
    }
}

void WowClientConnection_OnTransferDisconnected(WowClientConnection* self,
                                                void* connection, int ,
                                                int ) {
    if (connection == self->secondary_connection) {
        CDataStoreTempPacket packet{};
        BuildTransferCookiePacket(packet, kTransferResumeOpcode, self->transfer_cookie);
        WowClientConnection_SendPacket(self, &packet.store);
        DestroyTempPacket(packet);
        WowConnection_Release(self->secondary_connection);
        self->secondary_connection = nullptr;
    } else if (connection == self->primary_connection) {
        if (self->event_queue) {
            NetEventQueue_PostEvent(self->event_queue, kNetEventTypeDisconnect,
                                    static_cast<int>(
                                        reinterpret_cast<std::uintptr_t>(connection)),
                                    self, nullptr, 0);
        }
    }
}

void WowClientConnection_HandleReconnect(WowClientConnection* self,
                                          void* connection,
                                          void* data_store) {
    if (!data_store) {
        return;
    }

    auto& store = *static_cast<CDataStore*>(data_store);
    self->transfer_cookie = 0;

    std::uint32_t new_addr = 0;
    std::uint16_t new_port = 0;
    CDataStore_GetUInt32(store, &new_addr);
    CDataStore_GetUInt16(store, &new_port);
    CDataStore_GetUInt32(store, &self->transfer_cookie);

    if (self->secondary_connection || self->pending_auth) {
        CDataStoreTempPacket packet{};
        BuildTransferCookiePacket(packet, kTransferResumeOpcode, self->transfer_cookie);
        WowClientConnection_SendPacket(self, &packet.store);
        DestroyTempPacket(packet);
        return;
    }

    self->secondary_connection = AllocateConnection(self);

    const auto expected_digest = ComputeReconnectDigest(*self, new_addr, new_port);
    std::array<std::uint8_t, kTransferDigestSize> packet_digest{};
    CDataStore_GetBytes(store, packet_digest.data(), packet_digest.size());

    if (packet_digest != expected_digest ||
        !CDataStore_AtEnd(store) ||
        store.read_pos > store.write_pos) {
        if (connection) {
            static_cast<WowConnection*>(connection)->Close();
        }
        return;
    }

    if (self->secondary_connection) {
        WowConnection_Connect(self->secondary_connection, new_addr, new_port, -1);
    }
}

void WowClientConnection_HandleTransferRedirect(WowClientConnection* self,
                                                void* connection,
                                                void* data_store) {
    if (!self || !data_store) {
        return;
    }

    if (connection != self->primary_connection ||
        self->pending_auth ||
        !self->secondary_connection) {
        CloseConnectionWithoutRelease(connection);
        return;
    }

    auto& store = *static_cast<CDataStore*>(data_store);
    std::uint32_t redirect_cookie = 0;
    CDataStore_GetUInt32(store, &redirect_cookie);
    if (!CDataStore_AtEnd(store) || store.read_pos > store.write_pos) {
        CloseConnectionWithoutRelease(connection);
        return;
    }

    CDataStoreTempPacket packet{};
    BuildTransferCookiePacket(packet, kTransferSuspendOpcode, redirect_cookie);
    WowClientConnection_SendPacket(self, &packet.store);
    DestroyTempPacket(packet);
    self->pending_auth = 1;
}

void* WowClientConnection_FreeHeldMessage(void* list_ptr, HeldMessage* msg) {
    if (!msg) return nullptr;

    auto* next = msg->next;
    if (msg->prev) {
        msg->prev->next = msg->next;
    }
    if (msg->next) {
        msg->next->prev = msg->prev;
    }
    if (list_ptr) {
        auto*& head = *static_cast<HeldMessage**>(list_ptr);
        if (head == msg) {
            head = msg->next;
        }
    }

    delete msg;
    return next;
}

int WowClientConnection_HandleTransferComplete(WowClientConnection* self,
                                                void* connection, int ) {
    if (connection == self->secondary_connection) {
        void* old_primary = self->primary_connection;
        self->primary_connection = self->secondary_connection;
        self->secondary_connection = nullptr;
        if (old_primary) {
            CloseAndReleaseConnection(old_primary);
        }
    } else {
        if (self->secondary_connection) {
            CloseAndReleaseConnection(self->secondary_connection);
        }
        self->secondary_connection = nullptr;
    }

    self->pending_auth = 0;

    HeldMessage* msg = self->held_msg_head;
    while (msg) {
        SendPacketBytes(self, msg->packet_bytes);
        HeldMessage* next = static_cast<HeldMessage*>(
            WowClientConnection_FreeHeldMessage(&self->held_msg_head, msg));
        msg = next;
    }
    self->held_msg_head = nullptr;

    return 0;
}

void WowClientConnection_MessageHandler(WowClientConnection* self,
                                         void* connection,
                                         int ,
                                         void* data_store) {
    if (!self || !data_store) {
        CloseConnectionWithoutRelease(connection);
        return;
    }

    auto& store = *static_cast<CDataStore*>(data_store);
    const std::uint32_t packet_size = store.write_pos;
    const std::uint8_t* packet_bytes = nullptr;
    store.read_pos = 0;
    if (!CDataStore_GetReadSpanPointer(store, packet_bytes, packet_size)) {
        CloseConnectionWithoutRelease(connection);
        return;
    }
    openwow::compiler::AtomicFetchAddSeqCst(const_cast<std::int32_t*>(&self->session_bytes_recv), static_cast<std::int32_t>(packet_size));

    store.read_pos = 0;
    uint16_t opcode = 0;
    CDataStore_GetUInt16(store, &opcode);

    switch (opcode) {
        case 0x1EC:
            WowClientConnection_HandleAuthChallenge(self, connection, data_store);
            return;
        case 0x1DD:
            WowClientConnection_HandlePong(self, connection, data_store);
            return;
        case 0x50D:
            WowClientConnection_HandleReconnect(self, connection, data_store);
            return;
        case 0x50F:
            WowClientConnection_HandleTransferRedirect(self, connection, data_store);
            return;
        case 0x511:
            WowClientConnection_HandleTransferComplete(self, connection, 0);
            return;
        default:
            break;
    }

    if (connection == self->primary_connection && !self->pending_auth) {
        store.read_pos = store.write_pos;
        if (self->event_queue) {
            NetEventQueue_PostEvent(self->event_queue, kNetEventTypeMessage,
                                    static_cast<int>(reinterpret_cast<std::intptr_t>(connection)),
                                    self, const_cast<std::uint8_t*>(packet_bytes),
                                    packet_size);
        }
    } else {
        CloseConnectionWithoutRelease(connection);
    }
}

void WowClientConnection_GetNetworkStats(WowClientConnection* self,
                                          float* bandwidth_in_kbps,
                                          float* bandwidth_out_kbps,
                                          uint32_t* latency_ms) {
    std::lock_guard<std::mutex> lock(self->stats_cs);

    const double elapsed_sec =
        static_cast<double>(SDL_GetTicks() - self->connection_timestamp) * 0.001;

    constexpr double kBytesToKB = 1.0 / 1024.0;

    *bandwidth_in_kbps =
        static_cast<float>(static_cast<double>(
            static_cast<uint32_t>(self->session_bytes_recv)) * kBytesToKB / elapsed_sec);

    *bandwidth_out_kbps =
        static_cast<float>(static_cast<double>(
            self->session_bytes_sent) * kBytesToKB / elapsed_sec);

    uint32_t sum = 0;
    uint32_t count = 0;
    uint32_t idx = self->rtt_read_idx;
    const uint32_t end_idx = self->rtt_write_idx;

    if (idx == end_idx) {
        *latency_ms = 0;
        return;
    }

    do {
        if (idx >= kMaxRttSamples) {
            idx = 0;
            if (end_idx == 0)
                break;
        }
        sum += self->rtt_samples[idx++];
        ++count;
    } while (idx != end_idx);

    *latency_ms = count ? sum / count : 0;
}

void WowClientConnection_ProcessEventQueue(
    WowClientConnection* self, const NetEventProcessDispatch& dispatch) {
    if (self->event_queue) {
        NetEventQueue_ProcessEvents(self->event_queue, dispatch);
    }
}

static uint32_t g_snapshot_bytes_sent = 0;
static uint32_t g_snapshot_bytes_received = 0;
static uint32_t g_snapshot_packets_sent = 0;
static uint32_t g_snapshot_opcodes_dispatched = 0;

void WowClientConnection_SendPingIfNeeded(WowClientConnection* self) {
    if (!self) {
        return;
    }
    g_snapshot_bytes_sent = g_total_bytes_sent;
    g_snapshot_bytes_received = g_total_bytes_received;
    g_snapshot_packets_sent = g_total_packets_sent;
    g_snapshot_opcodes_dispatched = g_total_opcodes_dispatched;

    const uint32_t now = openwow::core::GameClock::GetTickCount32();
    const int32_t elapsed =
        static_cast<int32_t>(now - self->ping_timestamp - 30000u);
    auto* const primary = WowConnection_TryGetManaged(self->primary_connection);
    if (elapsed >= 0 && self->state == kStateConnected && primary &&
        primary->IsEncryptionEnabled()) {
        std::uint32_t sequence = 0;
        std::uint32_t reported_latency = 0;
        {
            std::lock_guard<std::mutex> lock(self->stats_cs);
            self->ping_timestamp = now;
            sequence = ++self->ping_sequence;

            reported_latency = self->rtt_write_idx == 0
                ? 0U
                : self->rtt_samples[self->rtt_write_idx - 1];
        }

        CDataStoreTempPacket packet{};
        InitTempPacket(packet);
        PutU32Le(packet.store, OpcodeValue(wotlk::Opcode::CMSG_PING));
        PutU32Le(packet.store, sequence);
        PutU32Le(packet.store, reported_latency);
        packet.store.read_pos = 0;
        WowClientConnection_SendPacket(self, &packet.store);
        DestroyTempPacket(packet);
    }
}

}

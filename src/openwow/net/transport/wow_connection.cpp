#include "openwow/net/transport/wow_connection.h"

#include "openwow/net/serialization/cdatastore_ops.h"
#include "openwow/net/net_client.h"
#include "openwow/net/wotlk/protocol/world_header_crypto.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>
#include <unordered_set>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace openwow::net {

#ifdef _WIN32
using SocketTransferSize = int;
using SocketBufferLength = int;
#else
using SocketTransferSize = ssize_t;
using SocketBufferLength = std::size_t;
#endif

namespace {

struct ManagedConnectionRegistry {
  std::mutex mutex;
  std::unordered_set<const WowConnection*> instances;
};

ManagedConnectionRegistry& GetManagedConnectionRegistry() {
  static ManagedConnectionRegistry registry;
  return registry;
}

struct ArtificialNetworkDelayRange {
  std::atomic<std::uint32_t> minimum_delay_ms{0};
  std::atomic<std::uint32_t> maximum_delay_ms{0};
};

ArtificialNetworkDelayRange& GetArtificialNetworkDelayRange() {
  static ArtificialNetworkDelayRange delay_range;
  return delay_range;
}

void RegisterManagedConnection(const WowConnection* conn) {
  auto& registry = GetManagedConnectionRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  registry.instances.insert(conn);
}

void UnregisterManagedConnection(const WowConnection* conn) {
  auto& registry = GetManagedConnectionRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  registry.instances.erase(conn);
}

std::uint32_t GetCurrentThreadIdValue() {
  return static_cast<std::uint32_t>(
      std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

std::uint16_t ReadNetworkPort(const std::array<std::uint8_t, 16>& storage) {
  std::uint16_t port_network = 0;
  std::memcpy(&port_network, storage.data() + 2, sizeof(port_network));
  return ntohs(port_network);
}

std::uint32_t ReadIPv4Address(const std::array<std::uint8_t, 16>& storage) {
  std::uint32_t addr_network = 0;
  std::memcpy(&addr_network, storage.data() + 4, sizeof(addr_network));
  return addr_network;
}

std::uint64_t GetNetworkTickCount() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

#ifdef _WIN32
constexpr int kSocketSendFlags = 0;
#else
constexpr int kSocketSendFlags = MSG_NOSIGNAL;
#endif

constexpr std::size_t kWorldHeaderKeyBytes = 20;
constexpr std::size_t kRc4WarmupDropBytes = 0x400;

int GetLastSocketErrorCode() {
#ifdef _WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}

bool SocketErrorIsWouldBlock(int error_code) {
#ifdef _WIN32
  return error_code == WSAEWOULDBLOCK;
#else
  return error_code == EAGAIN || error_code == EWOULDBLOCK;
#endif
}

bool SocketErrorIsInterrupted(const int error_code) {
#ifdef _WIN32
  return error_code == WSAEINTR;
#else
  return error_code == EINTR;
#endif
}

bool ArtificialNetworkDelayEnabled() {
  return GetArtificialNetworkDelayRange().minimum_delay_ms.load(
             std::memory_order_relaxed) != 0;
}

std::uint64_t ComputeArtificialNetworkDelayMs(
    core::ClientCrtRandom& random) {
  const auto& delay_range = GetArtificialNetworkDelayRange();
  const std::uint32_t minimum_delay_ms =
      delay_range.minimum_delay_ms.load(std::memory_order_relaxed);
  if (minimum_delay_ms == 0) {
    return 0;
  }

  const std::uint32_t maximum_delay_ms =
      delay_range.maximum_delay_ms.load(std::memory_order_relaxed);
  if (maximum_delay_ms <= minimum_delay_ms) {
    return minimum_delay_ms;
  }

  const std::uint64_t random_value = random.Next();
  const std::uint64_t range_size =
      static_cast<std::uint64_t>(maximum_delay_ms - minimum_delay_ms) + 1;
  return minimum_delay_ms + (range_size * random_value) / 32768U;
}

void WakeConnectionNetClient(const WowConnection* conn, int previous_state) {
  if (!conn) {
    return;
  }

  if (auto* net_client = GetGlobalNetClient()) {
    NetClient_UpdateConnectionEventMaskAndWake(
        net_client, const_cast<WowConnection*>(conn), previous_state);
  }
}

void PopulateSocketAddresses(
    int socket_fd, std::array<std::uint8_t, 16>& peer_storage,
    std::array<std::uint8_t, 16>& local_storage) {
  socklen_t peer_len = static_cast<socklen_t>(peer_storage.size());
  ::getpeername(socket_fd, reinterpret_cast<struct sockaddr*>(peer_storage.data()),
                &peer_len);

  socklen_t local_len = static_cast<socklen_t>(local_storage.size());
  ::getsockname(socket_fd,
                reinterpret_cast<struct sockaddr*>(local_storage.data()),
                &local_len);
}

bool GetSocketConnectError(int socket_fd, int& socket_error) {
  socket_error = 0;
  socklen_t option_length = sizeof(socket_error);
#ifdef _WIN32
  return ::getsockopt(socket_fd, SOL_SOCKET, SO_ERROR,
                      reinterpret_cast<char*>(&socket_error),
                      &option_length) == 0;
#else
  return ::getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error,
                      &option_length) == 0;
#endif
}

std::size_t HeaderByteCount(std::size_t wire_size, std::size_t payload_size) {
  if (wire_size < payload_size) {
    return 0;
  }

  return wire_size - payload_size;
}

CDataStore MakeReadOnlyPacketStore(const std::uint8_t* data,
                                   const std::size_t size) {
  CDataStore store{};
  store.data = const_cast<std::uint8_t*>(data);
  store.window_base = 0;
  store.window_size = std::numeric_limits<std::uint32_t>::max();
  store.write_pos = static_cast<std::uint32_t>(size);
  store.read_pos = 0;
  return store;
}

class HandlerInvocationGuard {
 public:
  explicit HandlerInvocationGuard(WowConnection* connection)
      : connection_(connection),
        handler_(connection ? connection->BeginHandlerInvocation() : nullptr) {}

  ~HandlerInvocationGuard() {
    if (connection_) {
      connection_->EndHandlerInvocation();
    }
  }

  [[nodiscard]] WowConnectionResponse* get() const { return handler_; }

 private:
  WowConnection* connection_;
  WowConnectionResponse* handler_;
};

}

static std::atomic<int32_t> g_conn_count{0};
static std::atomic<uint64_t> g_send_count{0};
static std::atomic<uint64_t> g_send_bytes_total{0};

std::atomic<int32_t>& WowConnection_GetConnCount() { return g_conn_count; }
std::atomic<uint64_t>& WowConnection_GetSendCount() { return g_send_count; }
std::atomic<uint64_t>& WowConnection_GetSendBytesTotal() {
  return g_send_bytes_total;
}
void WowConnection_SetArtificialSendDelayRange(
    const std::uint32_t minimum_delay_ms,
    const std::uint32_t maximum_delay_ms) {
  auto& delay_range = GetArtificialNetworkDelayRange();
  delay_range.minimum_delay_ms.store(minimum_delay_ms,
                                     std::memory_order_relaxed);
  delay_range.maximum_delay_ms.store(std::max(minimum_delay_ms, maximum_delay_ms),
                                     std::memory_order_relaxed);
}

bool WowConnection_HasArtificialNetworkDelay() {
  return ArtificialNetworkDelayEnabled();
}

WowConnectionPacket WowConnectionPacket::Build(const uint8_t* data,
                                                std::size_t size) {
  WowConnectionPacket pkt;
  if (size > 0x7FFF) {

    pkt.buffer.resize(3 + size);
    pkt.buffer[0] = static_cast<uint8_t>((size >> 16) & 0xFF) | 0x80;
    pkt.buffer[1] = static_cast<uint8_t>((size >> 8) & 0xFF);
    pkt.buffer[2] = static_cast<uint8_t>(size & 0xFF);
    if (size != 0) {
      std::memcpy(pkt.buffer.data() + 3, data, size);
    }
  } else {

    pkt.buffer.resize(2 + size);
    pkt.buffer[0] = static_cast<uint8_t>((size >> 8) & 0xFF);
    pkt.buffer[1] = static_cast<uint8_t>(size & 0xFF);
    if (size != 0) {
      std::memcpy(pkt.buffer.data() + 2, data, size);
    }
  }
  pkt.payload_size = size;
  pkt.read_offset = 0;
  pkt.storage_size = WowConnectionPacket::QueueStorageSize(size);
  pkt.CaptureWirePrefix();
  return pkt;
}

WowConnectionPacket WowConnectionPacket::BuildRaw(const uint8_t* data,
                                                  const std::size_t size) {
  WowConnectionPacket pkt;
  pkt.buffer.resize(size);
  if (size != 0) {
    std::memcpy(pkt.buffer.data(), data, size);
  }
  pkt.payload_size = size;
  pkt.read_offset = 0;
  pkt.storage_size = WowConnectionPacket::QueueStorageSize(size);
  pkt.CaptureWirePrefix();
  return pkt;
}

void WowConnectionPacket::CaptureWirePrefix() {
  wire_prefix.fill(0);
  if (buffer.empty()) {
    return;
  }

  std::memcpy(wire_prefix.data(), buffer.data(),
              std::min<std::size_t>(wire_prefix.size(), buffer.size()));
}

WowConnection::WowConnection() {
  RegisterManagedConnection(this);
  g_conn_count.fetch_add(1, std::memory_order_relaxed);
  InitializeSharedState(nullptr, 0);
  socket_fd_ = -1;
}

WowConnection::~WowConnection() {
  g_conn_count.fetch_add(-1, std::memory_order_relaxed);

  if (auto* net_client = GetGlobalNetClient()) {
    NetClient_UnregisterConnection(net_client, this);
    WowConnection_CloseWSAEvent(this);
  }

  if (socket_fd_ >= 0) {
    WowConnection_CloseSocket(socket_fd_);
    socket_fd_ = -1;
  }

  if (heap_buffer_) {
    WDataStore_FreeBuffer(heap_buffer_, heap_buffer_size_);
    heap_buffer_ = nullptr;
    heap_buffer_size_ = 0;
    heap_buffer_received_ = 0;
  }

  FreePendingPackets();

  send_cipher_.Reset();
  recv_cipher_.Reset();
  send_header_key_.fill(0);
  recv_header_key_.fill(0);
  encryption_enabled_ = false;

  UnregisterManagedConnection(this);
}

void WowConnection::Close() {
  int previous_state = 0;
  {
    std::lock_guard<std::mutex> lock(conn_cs_);
    if (state_ != WowConnectionState::kConnected) {
      return;
    }

    previous_state = static_cast<int>(state_);
    state_ = WowConnectionState::kClosing;
  }

  WakeConnectionNetClient(this, previous_state);
}

bool WowConnection::AcceptSocket(int socket_fd, WowConnectionResponse* handler) {
  InitializeSharedState(handler, 0);
  socket_fd_ = socket_fd;
  PopulateSocketAddresses(socket_fd_, peer_sockaddr_, local_sockaddr_);
  state_ = WowConnectionState::kConnected;
  return true;
}

void WowConnection::DataReady() {
  if (!heap_buffer_) {
    heap_buffer_ = WDataStore_AllocBuffer(1024);
    heap_buffer_size_ = 1024;
    heap_buffer_received_ = 0;
  }

  const std::uint64_t dispatch_tick = GetNetworkTickCount();
  DrainDeferredReceivePackets(dispatch_tick);

  while (true) {
    const std::size_t received = heap_buffer_received_;
    int header_size = 2;
    int packet_size = -1;

    if (received >= 2) {
      if (heap_buffer_[0] & 0x80) {
        header_size = 3;
        if (received >= 3) {
          packet_size =
              ((heap_buffer_[0] & 0x7F) << 16) |
              (heap_buffer_[1] << 8) |
              heap_buffer_[2];
          packet_size += 3;
        }
      } else {
        packet_size =
            ((heap_buffer_[0] & 0x7F) << 8) | heap_buffer_[1];
        packet_size += 2;
      }
    }

    if (received >= heap_buffer_size_) {
      const std::size_t new_capacity = heap_buffer_size_ * 2;
      if (new_capacity > 0x1E84800) {
        Close();
        return;
      }

      heap_buffer_ = WDataStore_GrowBuffer(
          heap_buffer_, heap_buffer_size_, new_capacity, received);
      heap_buffer_size_ = new_capacity;
    }

    int want = 0;
    if (packet_size >= 0) {
      want = packet_size - static_cast<int>(received);
      const int spare = static_cast<int>(heap_buffer_size_ - received);
      if (spare < want) {
        want = spare;
      }
    } else {
      want = header_size - static_cast<int>(received);
    }

    int bytes_read = 0;
    if (want > 0) {
      do {
        bytes_read = ::recv(
            socket_fd_, reinterpret_cast<char*>(heap_buffer_ + received),
            static_cast<SocketBufferLength>(want), 0);
      } while (bytes_read < 0 &&
               SocketErrorIsInterrupted(GetLastSocketErrorCode()));

      if (bytes_read <= 0) {
        if (bytes_read < 0 && SocketErrorIsWouldBlock(GetLastSocketErrorCode())) {
          break;
        }

        DispatchCloseEvent();
        return;
      }

      if (encryption_enabled_) {
        DecryptHeader(heap_buffer_ + received,
                      RecvHeaderCryptByteCount(
                          received, static_cast<std::size_t>(bytes_read),
                          static_cast<std::size_t>(header_size)));
      }

      heap_buffer_received_ += static_cast<std::size_t>(bytes_read);
    } else if (packet_size < 0) {
      break;
    }

    if (packet_size >= 0 &&
        heap_buffer_received_ >= static_cast<std::size_t>(packet_size)) {
      const auto* payload = heap_buffer_ + header_size;
      const auto payload_size =
          static_cast<std::size_t>(packet_size - header_size);
      if (ArtificialNetworkDelayEnabled()) {
        QueueDeferredReceivePacket(payload, payload_size);
        WakeConnectionNetClient(this, static_cast<int>(state_));
      } else {
        DispatchFramedPacket(payload, payload_size, dispatch_tick);
      }
      heap_buffer_received_ = 0;
    }

    if (bytes_read <= 0) {
      return;
    }
  }
}

void WowConnection::DispatchReadAcceptEvent() {
  enum class DispatchAction {
    kNone,
    kAccept,
    kDataReady,
    kRecvLoop,
  };

  AddReference();

  DispatchAction action = DispatchAction::kNone;
  {
    std::lock_guard<std::mutex> lock(conn_cs_);
    if (state_ == WowConnectionState::kListening) {
      action = DispatchAction::kAccept;
    } else if (state_ == WowConnectionState::kConnected) {
      switch (receive_dispatch_mode_) {
        case WowConnectionReceiveDispatchMode::kFramedDataReady:
          action = DispatchAction::kDataReady;
          break;
        case WowConnectionReceiveDispatchMode::kRawRecvLoop:
          action = DispatchAction::kRecvLoop;
          break;
        default:
          action = DispatchAction::kNone;
          break;
      }
    }
  }

  switch (action) {
    case DispatchAction::kAccept:
      WowConnection_AcceptLoop(this);
      break;
    case DispatchAction::kDataReady:
      DataReady();
      break;
    case DispatchAction::kRecvLoop:
      RecvLoop();
      break;
    case DispatchAction::kNone:
      break;
  }

  WowConnection_Release(this);
}

void WowConnection::CompleteAsyncConnect(std::unique_lock<std::mutex>& conn_lock) {
  enum class DeferredCallback {
    kNone,
    kConnectSucceeded,
    kConnectFailed,
  };

  if (!conn_lock.owns_lock()) {
    return;
  }

  if (state_ != WowConnectionState::kConnecting || socket_fd_ < 0) {
    return;
  }

  int connect_error = 0;
  if (!GetSocketConnectError(socket_fd_, connect_error)) {
    return;
  }

  WowConnectionResponse* handler = nullptr;
  DeferredCallback deferred_callback = DeferredCallback::kNone;
  NetClient* const net_client = GetGlobalNetClient();
  const int previous_state = static_cast<int>(state_);

  if (connect_error != 0) {
    if (net_client) {
      NetClient_UnregisterConnection(net_client, this);
    }
    WowConnection_CloseSocket(socket_fd_);
    socket_fd_ = -1;
    state_ = WowConnectionState::kDisconnected;
    handler = BeginHandlerInvocation();
    deferred_callback = DeferredCallback::kConnectFailed;
  } else {
    state_ = WowConnectionState::kConnected;
    PopulateSocketAddresses(socket_fd_, peer_sockaddr_, local_sockaddr_);
    handler = BeginHandlerInvocation();
    deferred_callback = DeferredCallback::kConnectSucceeded;
  }

  conn_lock.unlock();

  if (net_client) {
    NetClient_UpdateConnectionEventMaskAndWake(net_client, this, previous_state);
  }

  if (handler) {
    switch (deferred_callback) {
      case DeferredCallback::kConnectSucceeded:
        if (handler->on_connect) {
          const auto timestamp = GetNetworkTickCount();
          handler->on_connect(this, timestamp);
        }
        break;
      case DeferredCallback::kConnectFailed:
        if (handler->on_connect_failed) {
          const auto timestamp = GetNetworkTickCount();
          handler->on_connect_failed(this, timestamp);
        }
        break;
      case DeferredCallback::kNone:
        break;
    }
  }

  conn_lock.lock();
  EndHandlerInvocation();
}

void WowConnection::DispatchConnectEvent() {
  AddReference();
  {
    std::unique_lock<std::mutex> lock(conn_cs_);
    if (state_ == WowConnectionState::kConnecting) {
      CompleteAsyncConnect(lock);
    }
  }
  WowConnection_Release(this);
}

void WowConnection::DispatchCloseEvent() {
  AddReference();

  WowConnectionResponse* handler = nullptr;
  int disconnected_socket = -1;
  int previous_state = 0;
  NetClient* net_client = nullptr;

  {
    std::lock_guard<std::mutex> lock(conn_cs_);
    net_client = GetGlobalNetClient();

    if (socket_fd_ >= 0) {
      if (net_client) {
        NetClient_UnregisterConnection(net_client, this);
      }
      disconnected_socket = socket_fd_;
      WowConnection_CloseSocket(socket_fd_);
    }

    previous_state = static_cast<int>(state_);
    state_ = WowConnectionState::kDisconnected;
    handler = BeginHandlerInvocation();
  }

  if (net_client) {
    NetClient_UpdateConnectionEventMaskAndWake(net_client, this, previous_state);
  }

  if (disconnected_socket >= 0) {
    if (handler && handler->on_disconnect) {
      handler->on_disconnect(this);
    }
  }

  {
    std::lock_guard<std::mutex> lock(conn_cs_);
    socket_fd_ = -1;
    EndHandlerInvocation();
  }

  WowConnection_Release(this);
}

int WowConnection::Send(const uint8_t* data, std::size_t size) {
  if (size == 0) {
    return 2;
  }

  g_send_count.fetch_add(1, std::memory_order_relaxed);
  g_send_bytes_total.fetch_add(size, std::memory_order_relaxed);

  std::lock_guard<std::mutex> lock(conn_cs_);
  if (state_ != WowConnectionState::kConnected) {
    return 2;
  }

  if (ArtificialNetworkDelayEnabled() || !send_queue_.empty()) {
    auto* queued_packet = CreateQueuedPacket(data, size, false);
    if (encryption_enabled_) {
      EncryptHeader(queued_packet->buffer.data(),
                    SendHeaderCryptByteCount(*queued_packet));
    }

    if (send_depth_ >= max_send_depth_) {
      HandleSendQueueOverflow();
      return 2;
    }

    WakeConnectionNetClient(this, static_cast<int>(state_));
    return 1;
  }

  auto pkt = WowConnectionPacket::Build(data, size);
  if (encryption_enabled_) {
    EncryptHeader(pkt.buffer.data(), SendHeaderCryptByteCount(pkt));
  }

  SocketTransferSize sent =
      ::send(socket_fd_, reinterpret_cast<const char*>(pkt.buffer.data()),
             static_cast<SocketBufferLength>(pkt.buffer.size()), kSocketSendFlags);
  if (sent == static_cast<SocketTransferSize>(pkt.buffer.size())) {
    return 0;
  }

  if (sent <= 0 && !SocketErrorIsWouldBlock(GetLastSocketErrorCode())) {
    const int previous_state = static_cast<int>(state_);
    state_ = WowConnectionState::kClosing;
    WakeConnectionNetClient(this, previous_state);
    return 2;
  }

  if (sent > 0) {

    pkt.read_offset = static_cast<std::size_t>(sent);
  }

  PromoteTemporaryPacket(std::move(pkt));
  if (send_depth_ >= max_send_depth_) {
    HandleSendQueueOverflow();
    return 2;
  }

  WakeConnectionNetClient(this, static_cast<int>(state_));
  return 1;
}

int WowConnection::SendRawBuffer(uint8_t* data, const std::size_t size,
                                 const bool skip_encryption) {
  g_send_bytes_total.fetch_add(size, std::memory_order_relaxed);

  std::lock_guard<std::mutex> lock(conn_cs_);
  if (!skip_encryption && encryption_enabled_) {
    EncryptHeader(data, SendRawBufferCryptByteCount(size));
  }

  if (size == 0 || state_ != WowConnectionState::kConnected) {
    return 2;
  }

  auto queue_and_wake = [&](const uint8_t* queued_data,
                            const std::size_t queued_size) -> int {
    CreateQueuedPacket(queued_data, queued_size, true);
    if (send_depth_ >= max_send_depth_) {
      HandleSendQueueOverflow();
      return 2;
    }

    WakeConnectionNetClient(this, static_cast<int>(state_));
    return 1;
  };

  if (ArtificialNetworkDelayEnabled() || !send_queue_.empty()) {
    return queue_and_wake(data, size);
  }

  const SocketTransferSize sent =
      ::send(socket_fd_, reinterpret_cast<const char*>(data),
             static_cast<SocketBufferLength>(size), kSocketSendFlags);
  if (sent == static_cast<SocketTransferSize>(size)) {
    return 0;
  }

  if (sent < 0) {
    if (SocketErrorIsWouldBlock(GetLastSocketErrorCode())) {
      return queue_and_wake(data, size);
    }

    if (send_depth_ < max_send_depth_) {
      const int previous_state = static_cast<int>(state_);
      state_ = WowConnectionState::kClosing;
      WakeConnectionNetClient(this, previous_state);
      return 2;
    }
  }

  const auto queued_offset =
      sent > 0 ? static_cast<std::size_t>(sent) : static_cast<std::size_t>(0);
  return queue_and_wake(data + queued_offset, size - queued_offset);
}

void WowConnection::FlushQueuedSends() {
  enum class DeferredCallback {
    kNone,
    kDisconnected,
    kSendQueueDrained,
  };

  AddReference();

  {
    std::unique_lock<std::mutex> lock(conn_cs_);
    if (state_ == WowConnectionState::kConnecting) {
      CompleteAsyncConnect(lock);
      WowConnection_Release(this);
      return;
    }
  }

  DeferredCallback deferred_callback = DeferredCallback::kNone;
  std::unique_ptr<HandlerInvocationGuard> callback_guard;
  int previous_state = 0;
  int disconnected_socket = -1;
  bool update_event_mask = false;
  bool reset_socket_after_callback = false;

  {
    std::lock_guard<std::mutex> lock(conn_cs_);
    previous_state = static_cast<int>(state_);
    if (send_queue_.empty()) {
      update_event_mask = true;
    } else {
      while (!send_queue_.empty()) {
        auto& packet = send_queue_.front();
        if (packet.dispatch_deadline != 0 &&
            packet.dispatch_deadline > GetNetworkTickCount()) {
          break;
        }

        const std::size_t remaining = packet.Remaining();
        std::size_t chunk = std::min<std::size_t>(remaining, 1024);
        if (chunk == 0) {
          if (send_bytes_ >= packet.WireSize()) {
            send_bytes_ -= packet.WireSize();
          } else {
            send_bytes_ = 0;
          }
          send_queue_.pop_front();
          if (send_depth_ > 0) {
            --send_depth_;
          }
        } else {
          const SocketTransferSize sent = ::send(
              socket_fd_, reinterpret_cast<const char*>(packet.Current()),
              static_cast<SocketBufferLength>(chunk), kSocketSendFlags);
          if (sent == static_cast<SocketTransferSize>(chunk)) {
            packet.read_offset += chunk;
            if (packet.Remaining() == 0) {
              if (send_bytes_ >= packet.WireSize()) {
                send_bytes_ -= packet.WireSize();
              } else {
                send_bytes_ = 0;
              }
              send_queue_.pop_front();
              if (send_depth_ > 0) {
                --send_depth_;
              }
            }
          } else if (sent > 0) {
            packet.read_offset += static_cast<std::size_t>(sent);
            break;
          } else {
            if (!SocketErrorIsWouldBlock(GetLastSocketErrorCode())) {
              callback_guard = std::make_unique<HandlerInvocationGuard>(this);
              if (auto* net_client = GetGlobalNetClient()) {
                NetClient_UnregisterConnection(net_client, this);
              }
              if (socket_fd_ >= 0) {
                disconnected_socket = socket_fd_;
                WowConnection_CloseSocket(socket_fd_);
              }
              state_ = WowConnectionState::kDisconnected;
              update_event_mask = true;
              reset_socket_after_callback = true;
              deferred_callback = DeferredCallback::kDisconnected;
            }
            break;
          }
        }

        if (send_queue_.empty() ||
            deferred_callback == DeferredCallback::kDisconnected) {
          break;
        }
      }
    }

    if (deferred_callback == DeferredCallback::kNone && pending_send_wake_ &&
        send_queue_.empty()) {
      pending_send_wake_ = false;
      callback_guard = std::make_unique<HandlerInvocationGuard>(this);
      deferred_callback = DeferredCallback::kSendQueueDrained;
    }
  }

  if (update_event_mask) {
    WakeConnectionNetClient(this, previous_state);
  }

  if (callback_guard) {
    if (WowConnectionResponse* handler = callback_guard->get()) {
      switch (deferred_callback) {
        case DeferredCallback::kDisconnected:
          if (disconnected_socket >= 0 && handler->on_disconnect) {
            handler->on_disconnect(this);
          }
          break;
        case DeferredCallback::kSendQueueDrained:
          if (handler->on_send_queue_drained) {
            handler->on_send_queue_drained(this);
          }
          break;
        case DeferredCallback::kNone:
          break;
      }
    }
    callback_guard.reset();
  }

  if (reset_socket_after_callback) {
    std::lock_guard<std::mutex> lock(conn_cs_);
    socket_fd_ = -1;
  }

  WowConnection_Release(this);
}

void WowConnection::RecvLoop() {
  constexpr std::size_t kRecvBufSize = 4096;
  uint8_t recv_buf[kRecvBufSize];
  const std::uint64_t started_at = GetNetworkTickCount();
  SocketTransferSize bytes_read = 0;
  int socket_error = 0;

  while (true) {
    do {
      bytes_read =
          ::recv(socket_fd_, reinterpret_cast<char*>(recv_buf),
                 static_cast<SocketBufferLength>(kRecvBufSize), 0);
      if (bytes_read >= 0) {
        socket_error = 0;
        break;
      }
      socket_error = GetLastSocketErrorCode();
    } while (SocketErrorIsInterrupted(socket_error));

    if (bytes_read <= 0) {
      break;
    }

    HandlerInvocationGuard callback_guard(this);
    if (WowConnectionResponse* handler = callback_guard.get()) {
      if (handler->on_data_ready) {
        handler->on_data_ready(this, GetNetworkTickCount());
      }
    }

    if (state_ == WowConnectionState::kClosing ||
        static_cast<std::int64_t>(GetNetworkTickCount() - started_at) >= 5) {
      return;
    }
  }

  if (bytes_read < 0 && SocketErrorIsWouldBlock(socket_error)) {
    return;
  }

  DispatchCloseEvent();
}

void WowConnection::InitCryptoKeys(const uint8_t* session_key,
                                   const std::size_t key_len,
                                   const uint8_t* directional_seeds,
                                   const std::size_t directional_seed_len) {

  const auto keys = directional_seeds != nullptr && directional_seed_len == 32
      ? wotlk::DeriveWorldHeaderKeys(
            session_key, key_len,
            std::span<const std::uint8_t, 32>(directional_seeds, 32))
      : wotlk::DeriveWorldHeaderKeys(session_key, key_len);

  std::copy(keys.send.begin(), keys.send.end(), send_header_key_.begin());
  std::copy(keys.receive.begin(), keys.receive.end(), recv_header_key_.begin());
  ReinitializeHeaderCiphers();
}

void WowConnection::EnableEncryption(const bool enabled) {
  std::lock_guard<std::mutex> lock(conn_cs_);
  encryption_enabled_ = enabled;
  ReinitializeHeaderCiphers();
}

void WowConnection::ReinitializeHeaderCiphers() {
  send_cipher_.Init(send_header_key_.data(), kWorldHeaderKeyBytes);
  recv_cipher_.Init(recv_header_key_.data(), kWorldHeaderKeyBytes);
  send_cipher_.Drop(kRc4WarmupDropBytes);
  recv_cipher_.Drop(kRc4WarmupDropBytes);
}

void WowConnection::SetHeaderCryptBytes(const std::uint8_t send_opcode_bytes,
                                        const std::uint8_t recv_opcode_bytes) {
  send_header_crypt_bytes_ = send_opcode_bytes;
  recv_header_crypt_bytes_ = recv_opcode_bytes;
}

void WowConnection::SetHandler(WowConnectionResponse* handler) {
  UpdateHandler(handler, HandlerUpdateMode::kDeferIfBusy);
}

void WowConnection::SetHandlerSynchronously(WowConnectionResponse* handler) {
  UpdateHandler(handler, HandlerUpdateMode::kWaitForIdle);
}

void WowConnection::UpdateHandler(WowConnectionResponse* handler,
                                  const HandlerUpdateMode mode) {
  while (true) {
    std::unique_lock<std::mutex> lock(handler_cs_);
    const bool handler_busy = handler_ref_count_ != 0;
    const bool on_handler_thread =
        handler_busy && handler_thread_id_ == GetCurrentThreadIdValue();
    if (!handler_busy || on_handler_thread) {
      handler_ = handler;
      pending_handler_ = nullptr;
      return;
    }

    if (mode == HandlerUpdateMode::kDeferIfBusy) {
      pending_handler_ = handler;
      return;
    }

    lock.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

void WowConnection::RequestSendDrainNotification() {
  int previous_state = 0;
  {
    std::lock_guard<std::mutex> lock(conn_cs_);
    previous_state = static_cast<int>(state_);
    pending_send_wake_ = true;
  }
  WakeConnectionNetClient(this, previous_state);
}

void WowConnection::SetReceiveDispatchMode(
    const WowConnectionReceiveDispatchMode mode) {
  std::lock_guard<std::mutex> lock(conn_cs_);
  receive_dispatch_mode_ = mode;
}

std::uint32_t WowConnection::GetPeerAddressV4() const {
  return ReadIPv4Address(peer_sockaddr_);
}

std::uint16_t WowConnection::GetPeerPort() const {
  return ReadNetworkPort(peer_sockaddr_);
}

std::uint32_t WowConnection::GetLocalAddressV4() const {
  return ReadIPv4Address(local_sockaddr_);
}

std::uint16_t WowConnection::GetLocalPort() const {
  return ReadNetworkPort(local_sockaddr_);
}

std::int32_t WowConnection::GetReferenceCount() const {
  return ref_count_.load(std::memory_order_acquire);
}

bool WowConnection::HasQueuedSendData() const {
  return send_depth_ != 0;
}

bool WowConnection::HasPendingSendWake() const {
  return pending_send_wake_;
}

bool WowConnection::HasDeferredReceivePackets() const {
  return !receive_queue_.empty();
}

std::uint32_t WowConnection::GetPendingNetEvents() const {
  return pending_net_events_.load(std::memory_order_acquire);
}

void WowConnection::SetPendingNetEvents(const std::uint32_t flags) {
  pending_net_events_.store(flags, std::memory_order_release);
}

std::uint32_t WowConnection::TakePendingNetEvents() {
  return pending_net_events_.exchange(0, std::memory_order_acq_rel);
}

std::int32_t WowConnection::GetActiveWorkerDispatches() const {
  return active_worker_dispatches_.load(std::memory_order_acquire);
}

std::int32_t WowConnection::IncrementActiveWorkerDispatches() {
  return active_worker_dispatches_.fetch_add(1, std::memory_order_acq_rel) + 1;
}

std::int32_t WowConnection::DecrementActiveWorkerDispatches() {
  return active_worker_dispatches_.fetch_sub(1, std::memory_order_acq_rel) - 1;
}

std::int32_t WowConnection::AddReference() {
  return ref_count_.fetch_add(1, std::memory_order_acq_rel) + 1;
}

std::int32_t WowConnection::ReleaseReference() {
  return ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
}

WowConnectionResponse* WowConnection::BeginHandlerInvocation() {
  std::lock_guard<std::mutex> lock(handler_cs_);
  ++handler_ref_count_;
  handler_thread_id_ = GetCurrentThreadIdValue();
  return handler_;
}

void WowConnection::EndHandlerInvocation() {
  std::lock_guard<std::mutex> lock(handler_cs_);
  if (handler_ref_count_ > 0) {
    --handler_ref_count_;
  }
  if (pending_handler_) {
    handler_ = pending_handler_;
    pending_handler_ = nullptr;
  }
  if (handler_ref_count_ == 0) {
    handler_thread_id_ = 0;
  }
}

void WowConnection::InitializeSharedState(WowConnectionResponse* handler,
                                          int ) {
  ref_count_.store(1, std::memory_order_release);
  handler_ = handler;
  pending_handler_ = nullptr;
  handler_ref_count_ = 0;
  handler_thread_id_ = 0;

  state_ = WowConnectionState::kInitialized;
  if (heap_buffer_) {
    WDataStore_FreeBuffer(heap_buffer_, heap_buffer_size_);
    heap_buffer_ = nullptr;
    heap_buffer_size_ = 0;
  } else {
    heap_buffer_size_ = 0;
  }
  heap_buffer_received_ = 0;

  peer_sockaddr_.fill(0);
  local_sockaddr_.fill(0);
  connect_addr_v4_ = 0;
  connect_port_ = 0;

  send_queue_.clear();
  receive_queue_.clear();
  send_depth_ = 0;
  send_bytes_ = 0;
  max_send_depth_ = 100000;
  pending_net_events_.store(0, std::memory_order_release);
  pending_send_wake_ = false;
  active_worker_dispatches_.store(0, std::memory_order_release);
  receive_dispatch_mode_ = WowConnectionReceiveDispatchMode::kFramedDataReady;

  send_cipher_.Reset();
  recv_cipher_.Reset();
  send_header_key_.fill(0);
  recv_header_key_.fill(0);
  encryption_enabled_ = false;
  send_header_crypt_bytes_ = 4;
  recv_header_crypt_bytes_ = 2;
}

WowConnectionPacket* WowConnection::CreateQueuedPacket(const uint8_t* data,
                                                       const std::size_t size,
                                                       const bool raw_buffer) {
  auto packet = raw_buffer ? WowConnectionPacket::BuildRaw(data, size)
                           : WowConnectionPacket::Build(data, size);
  const std::uint64_t artificial_delay_ms =
      ComputeArtificialNetworkDelayMs(artificial_delay_random_);
  if (artificial_delay_ms != 0) {
    packet.dispatch_deadline = GetNetworkTickCount() + artificial_delay_ms;
  }
  return QueueSendPacket(std::move(packet));
}

WowConnectionPacket* WowConnection::PromoteTemporaryPacket(
    WowConnectionPacket packet) {
  return QueueSendPacket(std::move(packet));
}

void WowConnection::QueueDeferredReceivePacket(const uint8_t* data,
                                               const std::size_t size) {
  auto packet = WowConnectionPacket::BuildRaw(data, size);
  const std::uint64_t artificial_delay_ms =
      ComputeArtificialNetworkDelayMs(artificial_delay_random_);
  if (artificial_delay_ms != 0) {
    packet.dispatch_deadline = GetNetworkTickCount() + artificial_delay_ms;
  }
  receive_queue_.push_back(std::move(packet));
}

void WowConnection::DispatchFramedPacket(const uint8_t* data,
                                         const std::size_t size,
                                         const std::uint64_t timestamp) {
  HandlerInvocationGuard callback_guard(this);
  WowConnectionResponse* handler = callback_guard.get();
  if (!handler || !handler->on_framed_packet) {
    return;
  }

  CDataStore packet = MakeReadOnlyPacketStore(data, size);
  handler->on_framed_packet(this, timestamp, packet);
}

void WowConnection::DrainDeferredReceivePackets(
    const std::uint64_t timestamp) {
  while (!receive_queue_.empty()) {
    const WowConnectionPacket& packet = receive_queue_.front();
    if (packet.dispatch_deadline != 0 && timestamp < packet.dispatch_deadline) {
      break;
    }

    DispatchFramedPacket(packet.buffer.data(), packet.buffer.size(), timestamp);
    receive_queue_.pop_front();
  }
}

WowConnectionPacket* WowConnection::QueueSendPacket(WowConnectionPacket packet) {
  send_bytes_ += packet.WireSize();
  send_queue_.push_back(std::move(packet));
  auto* queued_packet = &send_queue_.back();
  ++send_depth_;
  return queued_packet;
}

void WowConnection::HandleSendQueueOverflow() {
  const int previous_state = static_cast<int>(state_);
  state_ = WowConnectionState::kClosing;
  if (handler_ && handler_->on_disconnect) {
    handler_->on_disconnect(this);
  }
  WakeConnectionNetClient(this, previous_state);
}

void WowConnection::FreePendingPackets() {
  send_queue_.clear();
  receive_queue_.clear();
  send_depth_ = 0;
  send_bytes_ = 0;
}

void WowConnection::SendFlushBuffer(WowConnectionPacket* pkt) {
  if (pkt) {
    *pkt = WowConnectionPacket{};
  }
}

std::size_t WowConnection::SendHeaderCryptByteCount(
    const WowConnectionPacket& packet) const {
  const auto header_size = HeaderByteCount(packet.WireSize(), packet.payload_size);
  return std::min(packet.WireSize(),
                  header_size +
                      static_cast<std::size_t>(send_header_crypt_bytes_));
}

std::size_t WowConnection::SendRawBufferCryptByteCount(
    const std::size_t buffer_size) const {
  const std::size_t header_size = buffer_size > 0x8001 ? 3u : 2u;
  return std::min(buffer_size,
                  header_size +
                      static_cast<std::size_t>(send_header_crypt_bytes_));
}

std::size_t WowConnection::RecvHeaderCryptByteCount(
    const std::size_t buffered_before_read, const std::size_t bytes_read,
    const std::size_t header_size) const {
  const auto encrypted_header_size =
      header_size + static_cast<std::size_t>(recv_header_crypt_bytes_);
  if (buffered_before_read >= encrypted_header_size) {
    return 0;
  }

  return std::min(bytes_read, encrypted_header_size - buffered_before_read);
}

void WowConnection::EncryptHeader(uint8_t* buf, std::size_t count) {
  if (!encryption_enabled_) {
    return;
  }
  send_cipher_.Process(buf, count);
}

void WowConnection::DecryptHeader(uint8_t* buf, std::size_t count) {
  if (!encryption_enabled_) {
    return;
  }
  recv_cipher_.Process(buf, count);
}

void WowConnection_CloseSocket(int socket_fd) {
  if (socket_fd < 0) return;

#ifdef _WIN32
  closesocket(socket_fd);
#else
  ::close(socket_fd);
#endif

  NetClient_UntrackSocketFd(socket_fd);
}

namespace {
  std::atomic<bool> g_network_shutdown{false};
}

void NetClient_Cleanup() {
  g_network_shutdown.store(true, std::memory_order_release);

  NetClient* const net_client = GetGlobalNetClient();
  if (!net_client) {
    return;
  }

  NetClient_ShutdownWorkers(net_client);

#ifdef _WIN32
  WSACleanup();
#endif

  NetClient_Destroy(net_client);
  WDataStore_ShutdownPools();
}

void WowConnection_Initialize(WowConnection* conn, int , int ) {
  if (!conn) return;
  conn->InitializeSharedState(nullptr, 0);
}

void WowConnection_SetReceiveDispatchMode(void* conn, const std::uint32_t mode) {
  if (!conn) {
    return;
  }

  if (auto* managed = WowConnection_TryGetManaged(conn)) {
    managed->SetReceiveDispatchMode(
        static_cast<WowConnectionReceiveDispatchMode>(mode));
    return;
  }

  std::memcpy(static_cast<std::uint8_t*>(conn) + 316, &mode, sizeof(mode));
}

WowConnection* WowConnection_TryGetManaged(void* conn) {
  if (!conn) {
    return nullptr;
  }

  auto* typed = static_cast<WowConnection*>(conn);
  auto& registry = GetManagedConnectionRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  return registry.instances.find(typed) != registry.instances.end() ? typed
                                                                    : nullptr;
}

const WowConnection* WowConnection_TryGetManaged(const void* conn) {
  return WowConnection_TryGetManaged(const_cast<void*>(conn));
}

}

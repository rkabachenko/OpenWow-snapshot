#pragma once

#include "openwow/core/client_crt_random.h"
#include "openwow/net/protocol/rc4_cipher.h"
#include "openwow/net/serialization/wdatastore.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <vector>

namespace openwow::net {

class WowConnection;
struct CDataStore;

enum class WowConnectionState : uint32_t {
  kClosed         = 0,
  kInitialized    = 1,
  kConnecting     = 2,
  kListening      = 3,
  kConnected      = 5,
  kDisconnected   = 6,
  kClosing        = 7,
  kConnectFailed  = 8,
};

enum class WowConnectionReceiveDispatchMode : std::uint32_t {
  kFramedDataReady = 0,
  kRawRecvLoop = 1,
};

struct WowConnectionResponse {
  std::function<void(WowConnection* listener, WowConnection* accepted,
                     uint32_t processor_count)>
      on_accept;
  std::function<void(WowConnection*, uint64_t timestamp, CDataStore& packet)>
      on_framed_packet;
  std::function<void(WowConnection*, uint64_t timestamp)> on_connect;
  std::function<void(WowConnection*, uint64_t timestamp)> on_connect_failed;
  std::function<void(WowConnection*)> on_disconnect;
  std::function<void(WowConnection*)> on_send_queue_drained;
  std::function<void(WowConnection*, uint64_t timestamp)> on_data_ready;
};

struct WowConnectionPacket {
  static constexpr std::size_t kQueueStorageOverhead = 43;

  std::vector<uint8_t> buffer;
  std::size_t payload_size{0};
  std::size_t read_offset{0};
  std::array<uint8_t, 8> wire_prefix{};
  std::uint64_t dispatch_deadline{0};
  std::size_t storage_size{0};

  static WowConnectionPacket Build(const uint8_t* data, std::size_t size);

  static WowConnectionPacket BuildRaw(const uint8_t* data, std::size_t size);

  void CaptureWirePrefix();

  [[nodiscard]] std::size_t WireSize() const {
    return buffer.size();
  }

  [[nodiscard]] static constexpr std::size_t QueueStorageSize(
      std::size_t queued_byte_count) {
    return queued_byte_count + kQueueStorageOverhead;
  }

  [[nodiscard]] std::size_t Remaining() const {
    return WireSize() - read_offset;
  }

  [[nodiscard]] const uint8_t* Current() const {
    return buffer.data() + read_offset;
  }
};

class WowConnection {
 public:

  WowConnection();

  ~WowConnection();

  WowConnection(const WowConnection&) = delete;
  WowConnection& operator=(const WowConnection&) = delete;
  WowConnection(WowConnection&&) = delete;
  WowConnection& operator=(WowConnection&&) = delete;

  bool AcceptSocket(int socket_fd, WowConnectionResponse* handler = nullptr);

  void Close();

  int Send(const uint8_t* data, std::size_t size);

  int SendRawBuffer(uint8_t* data, std::size_t size,
                    bool skip_encryption = false);

  void RecvLoop();

  void FlushQueuedSends();

  void DispatchReadAcceptEvent();

  void DispatchConnectEvent();

  void DispatchCloseEvent();

  void DataReady();

  void InitCryptoKeys(const uint8_t* session_key, std::size_t key_len,
                      const uint8_t* directional_seeds = nullptr,
                      std::size_t directional_seed_len = 0);

  void EnableEncryption(bool enabled = true);

  void SetHeaderCryptBytes(std::uint8_t send_opcode_bytes,
                           std::uint8_t recv_opcode_bytes);

  void SetHandler(WowConnectionResponse* handler);

  void SetHandlerSynchronously(WowConnectionResponse* handler);

  void RequestSendDrainNotification();

  void SetReceiveDispatchMode(WowConnectionReceiveDispatchMode mode);

  [[nodiscard]] WowConnectionState GetState() const { return state_; }
  [[nodiscard]] int GetSocket() const { return socket_fd_; }
  [[nodiscard]] WowConnectionResponse* GetHandler() const { return handler_; }
  [[nodiscard]] bool IsConnected() const {
    return state_ == WowConnectionState::kConnected;
  }
  [[nodiscard]] bool IsEncryptionEnabled() const { return encryption_enabled_; }
  [[nodiscard]] std::size_t GetSendDepth() const { return send_depth_; }
  [[nodiscard]] std::size_t GetSendBytes() const { return send_bytes_; }
  [[nodiscard]] std::uint32_t GetPeerAddressV4() const;
  [[nodiscard]] std::uint16_t GetPeerPort() const;
  [[nodiscard]] std::uint32_t GetLocalAddressV4() const;
  [[nodiscard]] std::uint16_t GetLocalPort() const;
  [[nodiscard]] std::int32_t GetReferenceCount() const;
  [[nodiscard]] bool HasQueuedSendData() const;
  [[nodiscard]] bool HasPendingSendWake() const;
  [[nodiscard]] bool HasDeferredReceivePackets() const;
  [[nodiscard]] std::uint32_t GetPendingNetEvents() const;
  void SetPendingNetEvents(std::uint32_t flags);
  std::uint32_t TakePendingNetEvents();
  [[nodiscard]] std::int32_t GetActiveWorkerDispatches() const;
  std::int32_t IncrementActiveWorkerDispatches();
  std::int32_t DecrementActiveWorkerDispatches();

  std::int32_t AddReference();
  std::int32_t ReleaseReference();
  WowConnectionResponse* BeginHandlerInvocation();
  void EndHandlerInvocation();
  void InitializeSharedState(WowConnectionResponse* handler, int flags);

 private:
  enum class HandlerUpdateMode : std::uint8_t {
    kDeferIfBusy,
    kWaitForIdle,
  };

  friend struct WowConnectionTestAccess;
  friend void WowConnection_ConnectInternal(void* conn);
  friend bool WowConnection_Connect(
      void* conn, std::uint32_t addr, std::uint16_t port, int unused);

  WowConnectionPacket* CreateQueuedPacket(const uint8_t* data,
                                          std::size_t size,
                                          bool raw_buffer = false);

  void UpdateHandler(WowConnectionResponse* handler, HandlerUpdateMode mode);

  WowConnectionPacket* PromoteTemporaryPacket(WowConnectionPacket packet);

  void QueueDeferredReceivePacket(const uint8_t* data, std::size_t size);

  void DispatchFramedPacket(const uint8_t* data, std::size_t size,
                            std::uint64_t timestamp);

  void DrainDeferredReceivePackets(std::uint64_t timestamp);

  WowConnectionPacket* QueueSendPacket(WowConnectionPacket packet);

  void HandleSendQueueOverflow();

  void FreePendingPackets();

  static void SendFlushBuffer(WowConnectionPacket* pkt);

  void EncryptHeader(uint8_t* buf, std::size_t count);

  void DecryptHeader(uint8_t* buf, std::size_t count);

  void ReinitializeHeaderCiphers();

  [[nodiscard]] std::size_t SendHeaderCryptByteCount(
      const WowConnectionPacket& packet) const;
  [[nodiscard]] std::size_t SendRawBufferCryptByteCount(
      std::size_t buffer_size) const;
  [[nodiscard]] std::size_t RecvHeaderCryptByteCount(
      std::size_t buffered_before_read, std::size_t bytes_read,
      std::size_t header_size) const;

  void CompleteAsyncConnect(std::unique_lock<std::mutex>& conn_lock);

  std::atomic<std::int32_t> ref_count_{1};
  int socket_fd_{-1};
  WowConnectionState state_{WowConnectionState::kClosed};
  WowConnectionResponse* handler_{nullptr};

  uint8_t* heap_buffer_{nullptr};
  std::size_t heap_buffer_size_{0};
  std::size_t heap_buffer_received_{0};

  std::mutex send_cs_;

  std::array<std::uint8_t, 16> peer_sockaddr_{};
  std::array<std::uint8_t, 16> local_sockaddr_{};
  std::uint32_t connect_addr_v4_{0};
  std::uint16_t connect_port_{0};

  std::mutex handler_cs_;
  uint32_t handler_ref_count_{0};
  uint32_t handler_thread_id_{0};
  WowConnectionResponse* pending_handler_{nullptr};

  std::list<WowConnectionPacket> send_queue_;
  std::list<WowConnectionPacket> receive_queue_;
  std::size_t send_depth_{0};
  std::size_t send_bytes_{0};
  std::size_t max_send_depth_{100000};
  std::atomic<std::uint32_t> pending_net_events_{0};
  bool pending_send_wake_{false};
  core::ClientCrtRandom artificial_delay_random_;

  std::mutex conn_cs_;
  std::atomic<std::int32_t> active_worker_dispatches_{0};
  WowConnectionReceiveDispatchMode receive_dispatch_mode_{
      WowConnectionReceiveDispatchMode::kFramedDataReady};

  RC4State send_cipher_;

  RC4State recv_cipher_;

  std::array<std::uint8_t, 20> send_header_key_{};
  std::array<std::uint8_t, 20> recv_header_key_{};

  bool encryption_enabled_{false};
  uint8_t send_header_crypt_bytes_{4};
  uint8_t recv_header_crypt_bytes_{2};
};

std::atomic<int32_t>& WowConnection_GetConnCount();

std::atomic<uint64_t>& WowConnection_GetSendCount();
std::atomic<uint64_t>& WowConnection_GetSendBytesTotal();

void WowConnection_SetArtificialSendDelayRange(std::uint32_t minimum_delay_ms,
                                               std::uint32_t maximum_delay_ms);
bool WowConnection_HasArtificialNetworkDelay();

void WowConnection_CloseSocket(int socket_fd);

void NetClient_Cleanup();

void WowConnection_Initialize(WowConnection* conn, int handler_type, int flags);
void WowConnection_SetReceiveDispatchMode(void* conn, std::uint32_t mode);

WowConnection* WowConnection_TryGetManaged(void* conn);
const WowConnection* WowConnection_TryGetManaged(const void* conn);

}

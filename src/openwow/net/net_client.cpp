
#include "openwow/net/net_client.h"
#include "openwow/core/console.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"
#include "openwow/net/serialization/wdatastore.h"
#include "openwow/net/transport/wow_connection.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <utility>
#include "openwow/foundation/compiler/atomic_ops.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace openwow::net {

static NetClient *g_net_client = nullptr;
static void *g_connection_filter = nullptr;
static uint32_t g_net_client_flags = 0;

namespace {

struct NetThreadHandle {
  std::thread thread;
  openwow::core::SEvent completion{true, false};
};

struct TrackedSocketRegistry {
  std::mutex mutex;
  std::vector<std::uint32_t> words;
};

struct ConnectionReleaseRegistry {
  std::mutex mutex;
  std::unordered_map<void *, WowConnectionReleaseHooks> hooks;
};

struct RetailReceiveBufferState {
  std::unique_ptr<std::uint8_t[]> buffer;
  std::uint32_t received = 0;
  std::uint32_t capacity = 0;
};

struct RetailReceiveBufferRegistry {
  std::mutex mutex;
  std::unordered_map<void *, RetailReceiveBufferState> states;
};

ConnectionReleaseRegistry &GetConnectionReleaseRegistry() {
  static ConnectionReleaseRegistry registry;
  return registry;
}

TrackedSocketRegistry &GetTrackedSocketRegistry() {
  static TrackedSocketRegistry registry;
  return registry;
}

RetailReceiveBufferRegistry &GetRetailReceiveBufferRegistry() {
  static RetailReceiveBufferRegistry registry;
  return registry;
}

RetailReceiveBufferState &GetRetailReceiveBufferState(void *conn) {
  auto &registry = GetRetailReceiveBufferRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  return registry.states[conn];
}

void EraseRetailReceiveBufferState(void *conn) {
  auto &registry = GetRetailReceiveBufferRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  registry.states.erase(conn);
}

std::size_t GetTrackedSocketWordIndex(const int socket_fd) {
  return static_cast<std::size_t>(static_cast<unsigned int>(socket_fd) >> 5U);
}

std::uint32_t GetTrackedSocketBitMask(const int socket_fd) {
  return 1U << (static_cast<unsigned int>(socket_fd) & 31U);
}

using ConnectionFilterFn = bool (*)(const sockaddr *);

int CreateSocketFdDefault(int af, int type, int protocol) {
#ifdef _WIN32
  return static_cast<int>(::socket(af, type, protocol));
#else
  return ::socket(af, type, protocol);
#endif
}

WowConnectionSocketCreateFn &GetSocketCreateFn() {
  static WowConnectionSocketCreateFn fn = &CreateSocketFdDefault;
  return fn;
}

uint32_t GetProcessorCount() {
  return std::max(1u, std::thread::hardware_concurrency());
}

#ifdef _WIN32
void SetSocketNonBlocking(int fd) {
  u_long mode = 1;
  ioctlsocket(fd, FIONBIO, &mode);
}

int GetSocketErrorCode() {
  return WSAGetLastError();
}

bool SocketErrorIsWouldBlock(int error_code) {
  return error_code == WSAEWOULDBLOCK;
}

bool SocketErrorIsConnectPending(int error_code) {
  return error_code == WSAEWOULDBLOCK;
}

void CloseSocketFd(int fd) {
  closesocket(fd);
}

void EnableTcpNoDelay(int fd) {
  if (fd < 0) {
    return;
  }

  const int enabled = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&enabled),
             sizeof(enabled));
}

void *CreateSocketEventHandle() {
  return WSACreateEvent();
}

void CloseSocketEventHandle(void *handle) {
  if (handle) {
    WSACloseEvent(static_cast<WSAEVENT>(handle));
  }
}
#else
void SetSocketNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

int GetSocketErrorCode() {
  return errno;
}

bool SocketErrorIsWouldBlock(int error_code) {
  return error_code == EAGAIN || error_code == EWOULDBLOCK;
}

bool SocketErrorIsConnectPending(int error_code) {
  return error_code == EINPROGRESS;
}

void CloseSocketFd(int fd) {
  ::close(fd);
}

void EnableTcpNoDelay(int fd) {
  if (fd < 0) {
    return;
  }

  const int enabled = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
}

void *CreateSocketEventHandle() {

  return reinterpret_cast<void *>(static_cast<uintptr_t>(1));
}

void CloseSocketEventHandle(void * ) {}

bool SetFdNonBlocking(const int fd) {
  if (fd < 0) {
    return false;
  }

  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }

  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

openwow::core::SEvent *GetNetClientMainStopEvent(NetClient *self) {
  if (!self) {
    return nullptr;
  }
  return static_cast<openwow::core::SEvent *>(self->main_stop_event);
}

#ifndef _WIN32
bool InitializeNetClientWakeSource(NetClient *self) {
  if (!self || self->wake_read_fd >= 0 || self->wake_write_fd >= 0) {
    return self != nullptr && self->wake_read_fd >= 0 && self->wake_write_fd >= 0;
  }

  int fds[2] = {-1, -1};
  if (::pipe(fds) != 0) {
    return false;
  }

  if (!SetFdNonBlocking(fds[0]) || !SetFdNonBlocking(fds[1])) {
    ::close(fds[0]);
    ::close(fds[1]);
    return false;
  }

  self->wake_read_fd = fds[0];
  self->wake_write_fd = fds[1];
  return true;
}

void DrainNetClientWakeSource(NetClient *self) {
  if (!self || self->wake_read_fd < 0) {
    return;
  }

  std::array<char, 64> buffer{};
  while (::read(self->wake_read_fd, buffer.data(), buffer.size()) > 0) {
  }
}

void DestroyNetClientWakeSource(NetClient *self) {
  if (!self) {
    return;
  }

  if (self->wake_read_fd >= 0) {
    ::close(self->wake_read_fd);
    self->wake_read_fd = -1;
  }
  if (self->wake_write_fd >= 0) {
    ::close(self->wake_write_fd);
    self->wake_write_fd = -1;
  }
}
#endif

void ReportSocketCreateFailure(int socket_result) {
  char message[160];
  std::snprintf(message, sizeof(message), "socket() returned %d, reason: %s", socket_result,
                "can't create socket (No network connection available?)");
  openwow::core::SErrDisplayError(0x85100000, ".\\WowConnection.cpp", 0x68, message, 0, 1u,
                                  0x11111111);
}

bool SocketSignalsRemoteClose(const int socket_fd) {
  if (socket_fd < 0) {
    return false;
  }

  char byte = 0;
  const int peeked = ::recv(socket_fd, &byte, 1, MSG_PEEK);
  if (peeked == 0) {
    return true;
  }
  if (peeked > 0) {
    return false;
  }

  return !SocketErrorIsWouldBlock(GetSocketErrorCode());
}

int GetConnectionSocketFd(const void *conn) {
  if (const auto *managed = WowConnection_TryGetManaged(conn)) {
    return managed->GetSocket();
  }
  return static_cast<int>(reinterpret_cast<const uint32_t *>(conn)[1]);
}

std::uint32_t GetRetailConnectionConnectAddressV4(const void *conn) {
  std::uint32_t address = 0;
  std::memcpy(&address, static_cast<const std::uint8_t *>(conn) + 108, sizeof(address));
  return address;
}

void SetRetailConnectionConnectAddressV4(void *conn, const std::uint32_t address) {
  std::memcpy(static_cast<std::uint8_t *>(conn) + 108, &address, sizeof(address));
}

std::uint16_t GetRetailConnectionConnectPort(const void *conn) {
  std::uint16_t port = 0;
  std::memcpy(&port, static_cast<const std::uint8_t *>(conn) + 112, sizeof(port));
  return port;
}

void SetRetailConnectionConnectPort(void *conn, const std::uint16_t port) {
  std::memcpy(static_cast<std::uint8_t *>(conn) + 112, &port, sizeof(port));
}

std::uint32_t ResolveRetailConnectionAddressV4(const char *host) {
  const auto parsed = ::inet_addr(host);
  if (parsed != INADDR_NONE && parsed != 0) {
    return static_cast<std::uint32_t>(parsed);
  }

  if (hostent *resolved = ::gethostbyname(host)) {
    std::uint32_t address = 0;
    std::memcpy(&address, resolved->h_addr_list[0], sizeof(address));
    return address;
  }

  return 0;
}

void SplitRetailHostPortString(const char *endpoint, std::array<char, 256> &host,
                               std::uint16_t &port) {
  port = 0;
  if (!endpoint) {
    openwow::core::SErrSetLastError(87);
    host[0] = '\0';
    return;
  }

  const char *delimiter = std::strchr(endpoint, ':');
  if (!delimiter) {
    openwow::core::SStrCopy(host.data(), endpoint, host.size());
    return;
  }

  port = static_cast<std::uint16_t>(openwow::core::ParseSignedDecimalLikeSub76F0D0(
      delimiter + 1));

  const std::size_t copy_limit =
      std::min<std::size_t>(static_cast<std::size_t>(delimiter - endpoint) + 1u, host.size());
  openwow::core::SStrCopy(host.data(), endpoint, copy_limit);
}

uint32_t GetConnectionState(const void *conn) {
  if (const auto *managed = WowConnection_TryGetManaged(conn)) {
    return static_cast<uint32_t>(managed->GetState());
  }
  return reinterpret_cast<const uint32_t *>(conn)[4];
}

uint32_t GetConnectionQueueState(const void *conn) {
  if (const auto *managed = WowConnection_TryGetManaged(conn)) {
    return managed->HasQueuedSendData() ? 1u : 0u;
  }
  return reinterpret_cast<const uint32_t *>(conn)[58];
}

bool GetConnectionHasPendingSendWake(const void *conn) {
  if (const auto *managed = WowConnection_TryGetManaged(conn)) {
    return managed->HasPendingSendWake();
  }
  return reinterpret_cast<const uint8_t *>(conn)[320] != 0;
}

bool GetConnectionHasDeferredReceivePackets(const void *conn) {
  if (const auto *managed = WowConnection_TryGetManaged(conn)) {
    return managed->HasDeferredReceivePackets();
  }

  const auto root = static_cast<std::uintptr_t>(
      reinterpret_cast<const std::uint32_t *>(conn)[65]);
  return root != 0 && (root & std::uintptr_t{1}) == 0;
}

std::uint32_t TakeConnectionPendingNetEvents(void *conn) {
  if (auto *managed = WowConnection_TryGetManaged(conn)) {
    return managed->TakePendingNetEvents();
  }

  auto *flags = reinterpret_cast<std::uint32_t *>(reinterpret_cast<std::uint8_t *>(conn) + 248);
  return openwow::compiler::AtomicExchange(flags, 0U);
}

void SetConnectionPendingNetEvents(void *conn, const std::uint32_t flags) {
  if (auto *managed = WowConnection_TryGetManaged(conn)) {
    managed->SetPendingNetEvents(flags);
    return;
  }

  auto *pending = reinterpret_cast<std::uint32_t *>(reinterpret_cast<std::uint8_t *>(conn) + 248);
  openwow::compiler::AtomicStore(pending, flags);
}

std::int32_t GetConnectionActiveWorkerDispatches(const void *conn) {
  if (const auto *managed = WowConnection_TryGetManaged(conn)) {
    return managed->GetActiveWorkerDispatches();
  }

  auto *dispatch_count = reinterpret_cast<std::int32_t *>(
      reinterpret_cast<std::uint8_t *>(const_cast<void *>(conn)) + 304);
  return openwow::compiler::AtomicLoad(dispatch_count);
}

std::int32_t IncrementConnectionActiveWorkerDispatches(void *conn) {
  if (auto *managed = WowConnection_TryGetManaged(conn)) {
    return managed->IncrementActiveWorkerDispatches();
  }

  auto *dispatch_count =
      reinterpret_cast<std::int32_t *>(reinterpret_cast<std::uint8_t *>(conn) + 304);
  return openwow::compiler::AtomicFetchAdd(dispatch_count, 1) + 1;
}

std::int32_t DecrementConnectionActiveWorkerDispatches(void *conn) {
  if (auto *managed = WowConnection_TryGetManaged(conn)) {
    return managed->DecrementActiveWorkerDispatches();
  }

  auto *dispatch_count =
      reinterpret_cast<std::int32_t *>(reinterpret_cast<std::uint8_t *>(conn) + 304);
  return openwow::compiler::AtomicFetchSub(dispatch_count, 1) - 1;
}

struct NetDispatchCandidate {
  void *conn = nullptr;
  int socket_fd = -1;
  std::uint32_t state = 0;
  void *socket_event = nullptr;
  bool watch_read = false;
  bool watch_write = false;
  bool watch_error = false;
  bool dispatch_close = false;
};

void DispatchConnectionWorkerFlags(void *conn, const std::uint32_t flags) {
  auto *managed = WowConnection_TryGetManaged(conn);
  if ((flags & 0x1u) != 0) {
    if (managed) {
      managed->FlushQueuedSends();
    }
  }
  if ((flags & 0x2u) != 0) {
    if (managed) {
      managed->DispatchReadAcceptEvent();
    } else if (GetConnectionState(conn) == 3) {
      WowConnection_AcceptLoop(conn);
    } else {
      WowConnection_DataReady(conn);
    }
  }
  if ((flags & 0x4u) != 0) {
    if (managed) {
      managed->DispatchConnectEvent();
    }
  }
  if ((flags & 0x8u) != 0) {
    if (managed) {
      managed->DispatchCloseEvent();
    }
  }
}

auto FindRegisteredConnection(NetClient *self, void *conn) {
  return std::find(self->registered_connections.begin(), self->registered_connections.end(), conn);
}

std::int32_t GetConnectionRefCount(const void *conn) {
  if (const auto *managed = WowConnection_TryGetManaged(conn)) {
    return managed->GetReferenceCount();
  }
  auto *ref_count = const_cast<std::int32_t *>(static_cast<const std::int32_t *>(conn));
  return openwow::compiler::AtomicLoad(ref_count);
}

std::int32_t IncrementConnectionRefCount(void *conn) {
  if (auto *managed = WowConnection_TryGetManaged(conn)) {
    return managed->AddReference();
  }
  auto *ref_count = static_cast<std::int32_t *>(conn);
  return openwow::compiler::AtomicFetchAdd(ref_count, 1) + 1;
}

std::int32_t DecrementConnectionRefCount(void *conn) {
  if (auto *managed = WowConnection_TryGetManaged(conn)) {
    return managed->ReleaseReference();
  }
  auto *ref_count = static_cast<std::int32_t *>(conn);
  return openwow::compiler::AtomicFetchSub(ref_count, 1) - 1;
}

WowConnectionResponse *GetConnectionHandler(void *conn) {
  if (auto *managed = WowConnection_TryGetManaged(conn)) {
    return managed->GetHandler();
  }
  return reinterpret_cast<WowConnectionResponse **>(reinterpret_cast<uint8_t *>(conn) + 20)[0];
}

WowConnectionResponse *BeginConnectionHandlerInvocation(void *conn) {
  if (auto *managed = WowConnection_TryGetManaged(conn)) {
    return managed->BeginHandlerInvocation();
  }
  return GetConnectionHandler(conn);
}

void EndConnectionHandlerInvocation(void *conn) {
  if (auto *managed = WowConnection_TryGetManaged(conn)) {
    managed->EndHandlerInvocation();
  }
}

bool PassesConnectionFilter(int socket_fd) {
  if (!g_connection_filter) {
    return true;
  }

  ConnectionFilterFn filter = reinterpret_cast<ConnectionFilterFn>(g_connection_filter);
  sockaddr_in peer{};
  socklen_t peer_len = sizeof(peer);
  ::getpeername(socket_fd, reinterpret_cast<sockaddr *>(&peer), &peer_len);
  return filter(reinterpret_cast<const sockaddr *>(&peer));
}

#ifdef _WIN32

void *GetTrackedSocketEvent(NetClient *self, void *conn) {
  std::lock_guard<std::mutex> lock(self->socket_event_mutex);
  const auto it = self->connection_socket_events.find(conn);
  if (it == self->connection_socket_events.end()) {
    return nullptr;
  }
  return it->second;
}
#endif

WowConnectionReleaseHooks TakeReleaseHooks(void *conn) {
  auto &registry = GetConnectionReleaseRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  const auto it = registry.hooks.find(conn);
  if (it == registry.hooks.end()) {
    return {};
  }

  const WowConnectionReleaseHooks hooks = it->second;
  registry.hooks.erase(it);
  return hooks;
}

void InvokeConnectionReleaseHooks(void *conn) {
  const WowConnectionReleaseHooks hooks = TakeReleaseHooks(conn);
  if (hooks.destroy || hooks.free) {
    if (hooks.destroy) {
      hooks.destroy(conn, hooks.context);
    }
    if (hooks.free) {
      hooks.free(conn, hooks.context);
    }
    return;
  }

  if (WowConnection_TryGetManaged(conn)) {
    delete static_cast<WowConnection *>(conn);
  }
}

void InvokeThreadInitCallback(NetClientThreadInitCallback callback) {
  if (callback) {
    callback();
  }
}

template <typename Fn>
void *CreateThreadHandle(Fn &&entry_point) {
  auto *handle = new NetThreadHandle();
  handle->thread = std::thread(
      [handle, fn = std::forward<Fn>(entry_point)]() mutable {
        struct CompletionSignal final {
          openwow::core::SEvent &event;

          ~CompletionSignal() { event.Set(); }
        } signal{handle->completion};

        fn();
      });
  return handle;
}

bool WaitForThreadHandle(void *handle, const std::uint32_t timeout_ms) {
  auto *thread_handle = static_cast<NetThreadHandle *>(handle);
  if (!thread_handle) {
    return true;
  }

  return thread_handle->completion.Wait(timeout_ms) == 0;
}

void JoinThreadHandle(void *handle) {
  auto *thread_handle = static_cast<NetThreadHandle *>(handle);
  if (!thread_handle) {
    return;
  }

  if (thread_handle->thread.joinable()) {
    thread_handle->thread.join();
  }
}

void DestroyThreadHandle(void *&handle) {
  auto *thread_handle = static_cast<NetThreadHandle *>(handle);
  if (!thread_handle) {
    return;
  }

  if (thread_handle->thread.joinable()) {
    thread_handle->thread.join();
  }

  delete thread_handle;
  handle = nullptr;
}

}

NetClient *&GetGlobalNetClient() {
  return g_net_client;
}

NetClient::~NetClient() {
#ifndef _WIN32
  DestroyNetClientWakeSource(this);
#endif
}

void NetClient_ResetTrackedSocketRegistry() {
  auto &registry = GetTrackedSocketRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  registry.words.clear();
}

void NetClient_TrackSocketFd(const int socket_fd) {
  if (socket_fd < 0) {
    return;
  }

  auto &registry = GetTrackedSocketRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  const std::size_t word_index = GetTrackedSocketWordIndex(socket_fd);
  if (word_index >= registry.words.size()) {
    registry.words.resize(word_index + 1, 0);
  }
  registry.words[word_index] |= GetTrackedSocketBitMask(socket_fd);
}

void NetClient_UntrackSocketFd(const int socket_fd) {
  if (socket_fd < 0) {
    return;
  }

  auto &registry = GetTrackedSocketRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  const std::size_t word_index = GetTrackedSocketWordIndex(socket_fd);
  if (word_index >= registry.words.size()) {
    return;
  }
  registry.words[word_index] &= ~GetTrackedSocketBitMask(socket_fd);
}

bool NetClient_IsTrackedSocketFd(const int socket_fd) {
  if (socket_fd < 0) {
    return false;
  }

  auto &registry = GetTrackedSocketRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  const std::size_t word_index = GetTrackedSocketWordIndex(socket_fd);
  if (word_index >= registry.words.size()) {
    return false;
  }
  return (registry.words[word_index] & GetTrackedSocketBitMask(socket_fd)) != 0;
}

int WowConnection_InitializeNetworkSystem(void *filter_callback,
                                          NetClientThreadInitCallback thread_init_callback,
                                          uint32_t max_workers, int thread_init_fn) {
  if (g_net_client) {
    return 1;
  }

  g_connection_filter = filter_callback;
  g_net_client_flags = 0;

  if (max_workers > kMaxWorkerThreads) {
    max_workers = kMaxWorkerThreads;
  }

  WDataStore_InitPools();
  auto *nc = new NetClient();
  NetClient_Init(nc, static_cast<int>(max_workers), thread_init_callback);
  g_net_client = nc;

  NetClient_WSAInit(nc, thread_init_fn);
  NetClient_CreateNetThreads(nc);
  return 1;
}

NetClient *NetClient_Init(NetClient *self, int max_workers,
                          NetClientThreadInitCallback thread_init_callback) {
  max_workers = std::clamp(max_workers, 0, kMaxWorkerThreads);

  self->main_thread_handle = nullptr;
  self->main_stop_signal = std::make_unique<core::SEvent>(true, false);
  self->main_stop_event = self->main_stop_signal.get();
  self->stop_flag = false;
  self->worker_count = static_cast<uint32_t>(max_workers);
  self->init_complete = false;
  self->thread_init_callback = thread_init_callback;
  self->connection_list_root.Reset(216);
  self->available_worker_slots = std::make_unique<NetSemaphore>(0, max_workers);
  self->worker_semaphore = self->available_worker_slots.get();
  self->pending_list_root.Reset(0);
  self->cleanup_list_root.Reset(0);
  self->cleanup_slots = std::make_unique<NetSemaphore>(0, INT_MAX);
  self->cleanup_semaphore = self->cleanup_slots.get();
  self->wsa_event = nullptr;
  self->wake_event_pending = false;
  self->registered_connections.clear();
  self->connection_socket_events.clear();
  NetClient_ResetTrackedSocketRegistry();
#ifndef _WIN32
  self->wake_read_fd = -1;
  self->wake_write_fd = -1;
  (void)InitializeNetClientWakeSource(self);
#endif

  for (int i = 0; i < kMaxWorkerThreads; ++i) {
    self->workers[i].thread_handle = nullptr;
    self->workers[i].thread_index = static_cast<uint32_t>(i);
    self->workers[i].stop_flag = false;
    self->workers[i].owner = nullptr;
    self->workers[i].current_conn = nullptr;
    self->workers[i].wake_event = std::make_unique<core::SEvent>(false, false);
    self->workers[i].event_handle = self->workers[i].wake_event.get();
    self->workers[i].processor_count = 0;
  }

  return self;
}

void NetClient_WSAInit(NetClient *self, int unused) {
  (void)unused;
#ifdef _WIN32
  WSADATA wsa_data{};
  const int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  if (result == 0 && wsa_data.wVersion == 0x0202) {
    self->wsa_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  }
#else
  self->wsa_event = nullptr;
#endif
}

void NetClient_CreateNetThreads(NetClient *self) {
  uint32_t i = 0;
  for (; i < self->worker_count; ++i) {
    self->workers[i].thread_index = i;
    self->workers[i].current_conn = nullptr;
    self->workers[i].stop_flag = false;
    self->workers[i].owner = self;
    self->workers[i].processor_count = GetProcessorCount();
    self->workers[i].thread_handle =
        CreateThreadHandle([slot = &self->workers[i]]() { NetClient_WorkerThreadProc(slot); });
  }

  for (; i < kMaxWorkerThreads; ++i) {
    self->workers[i].thread_index = 0;
    self->workers[i].current_conn = nullptr;
    self->workers[i].stop_flag = true;
    self->workers[i].owner = self;
    self->workers[i].processor_count = 0;
  }

  self->init_complete = false;
  self->main_thread_handle =
      CreateThreadHandle([self]() { NetClient_MainThreadProc(self); });
  while (!self->init_complete) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

int NetClient_MainThreadProc(void *param) {
  auto *self = static_cast<NetClient *>(param);
  NetClient_MainThreadLoop(self);
  if (auto *stop_event = GetNetClientMainStopEvent(self)) {
    stop_event->Set();
  }
  return 0;
}

void NetClient_MainThreadLoop(NetClient *self) {
  self->init_complete = true;

  while (!self->stop_flag) {
    const bool artificial_network_delay_active =
        WowConnection_HasArtificialNetworkDelay();
    std::vector<NetDispatchCandidate> candidates;
    candidates.reserve(kMaxWaitObjects - 1);
    int timeout_ms = artificial_network_delay_active ? 10 : 500;

    {
      std::lock_guard<std::recursive_mutex> lock(self->global_cs);
      for (void *conn : self->registered_connections) {
        if (GetConnectionActiveWorkerDispatches(conn) != 0) {
          continue;
        }

        NetDispatchCandidate candidate;
        candidate.conn = conn;
        candidate.socket_fd = GetConnectionSocketFd(conn);
        candidate.state = GetConnectionState(conn);
        const bool socket_tracked =
            candidate.socket_fd >= 0 && NetClient_IsTrackedSocketFd(candidate.socket_fd);

        switch (candidate.state) {
        case 2:
          candidate.watch_write = socket_tracked;
          candidate.watch_error = socket_tracked;
#ifdef _WIN32
          candidate.socket_event = GetTrackedSocketEvent(self, conn);
#endif
          break;
        case 3:
          candidate.watch_read = socket_tracked;
#ifdef _WIN32
          candidate.socket_event = GetTrackedSocketEvent(self, conn);
#endif
          break;
        case 5:
          candidate.watch_read = socket_tracked;
          candidate.watch_error = socket_tracked;
          candidate.watch_write =
              socket_tracked &&
              (GetConnectionQueueState(conn) != 0 || GetConnectionHasPendingSendWake(conn));
#ifdef _WIN32
          candidate.socket_event = GetTrackedSocketEvent(self, conn);
#endif
          break;
        case 7:
          candidate.dispatch_close = true;
#ifdef _WIN32
          candidate.socket_event = GetTrackedSocketEvent(self, conn);
#endif
          timeout_ms = 0;
          break;
        default:
          break;
        }

        if (!candidate.watch_read && !candidate.watch_write && !candidate.watch_error &&
            !candidate.dispatch_close) {
          continue;
        }

#ifdef _WIN32
        if (!candidate.dispatch_close && !candidate.socket_event) {
          continue;
        }
#endif

        if (candidates.size() >= static_cast<std::size_t>(kMaxWaitObjects - 1)) {
          continue;
        }

        IncrementConnectionRefCount(conn);
        if (GetConnectionHasDeferredReceivePackets(conn)) {
          timeout_ms = 0;
        }
        candidates.push_back(candidate);
      }
    }

#ifdef _WIN32
    if (self->wsa_event) {
      std::array<HANDLE, kMaxWaitObjects> wait_handles{};
      wait_handles[0] = static_cast<HANDLE>(self->wsa_event);
      for (std::size_t i = 0; i < candidates.size(); ++i) {
        wait_handles[i + 1] = static_cast<HANDLE>(candidates[i].socket_event);
      }

      const DWORD wait_count = static_cast<DWORD>(candidates.size() + 1);
      const DWORD ready_index =
          ::WaitForMultipleObjects(wait_count, wait_handles.data(), FALSE, timeout_ms);
      const bool wait_signaled = ready_index < wait_count;
      if (wait_signaled && ready_index == WAIT_OBJECT_0) {
        ::ResetEvent(static_cast<HANDLE>(self->wsa_event));
        std::lock_guard<std::mutex> wake_lock(self->wake_mutex);
        self->wake_event_pending = false;
      }

      if (wait_signaled) {
        for (std::size_t i = 0; i < candidates.size(); ++i) {
          const NetDispatchCandidate &candidate = candidates[i];
          std::uint32_t flags = 0;
          if (candidate.dispatch_close) {
            flags |= 0x8u;
          }

          if (candidate.socket_fd >= 0 && candidate.socket_event) {
            WSANETWORKEVENTS network_events{};
            WSAEnumNetworkEvents(candidate.socket_fd, static_cast<WSAEVENT>(candidate.socket_event),
                                 &network_events);

            if ((network_events.lNetworkEvents & (FD_READ | FD_ACCEPT | FD_CLOSE)) != 0) {
              flags |= 0x2u;
            }
            if ((network_events.lNetworkEvents & (FD_WRITE | FD_CONNECT | FD_CLOSE)) != 0) {
              flags |= 0x1u;
            }
          }

          if (artificial_network_delay_active) {
            flags |= 0x3u;
          }

          if (flags != 0) {
            NetClient_DispatchToWorker(self, candidate.conn, static_cast<int>(flags));
          }
        }
      }
    } else {
      std::unique_lock<std::mutex> wake_lock(self->wake_mutex);
      self->wake_cv.wait_for(wake_lock, std::chrono::milliseconds(timeout_ms),
                             [self]() { return self->wake_event_pending || self->stop_flag; });
      self->wake_event_pending = false;
    }
#else
    if (self->wake_read_fd >= 0) {
      std::vector<pollfd> pollfds;
      pollfds.reserve(candidates.size() + 1);
      pollfds.push_back(pollfd{self->wake_read_fd, POLLIN, 0});

      for (const NetDispatchCandidate &candidate : candidates) {
        short events = 0;
        if (candidate.watch_read) {
          events |= POLLIN;
        }
        if (candidate.watch_write) {
          events |= POLLOUT;
        }
        pollfds.push_back(pollfd{candidate.socket_fd, events, 0});
      }

      const int ready = ::poll(pollfds.data(), pollfds.size(), timeout_ms);
      if (ready > 0 && (pollfds[0].revents & POLLIN) != 0) {
        DrainNetClientWakeSource(self);
        std::lock_guard<std::mutex> wake_lock(self->wake_mutex);
        self->wake_event_pending = false;
      }

      if (ready > 0) {
        for (std::size_t i = 0; i < candidates.size(); ++i) {
          const NetDispatchCandidate &candidate = candidates[i];
          const short revents = pollfds[i + 1].revents;
          std::uint32_t flags = 0;

          if (candidate.dispatch_close) {
            flags |= 0x8u;
          }

          if (candidate.socket_fd >= 0) {
            const bool read_ready = (revents & POLLIN) != 0;
            const bool write_ready = (revents & POLLOUT) != 0;
            const bool error_ready = (revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            const bool close_ready =
                error_ready || (read_ready && SocketSignalsRemoteClose(candidate.socket_fd));

            switch (candidate.state) {
            case 2:
              if (write_ready || error_ready) {
                flags |= 0x1u;
              }
              break;
            case 3:
              if (read_ready) {
                flags |= 0x2u;
              }
              break;
            case 5:
              if (read_ready || close_ready) {
                flags |= 0x2u;
              }
              if (write_ready || close_ready) {
                flags |= 0x1u;
              }
              break;
            default:
              break;
            }
          }

          if (artificial_network_delay_active) {
            flags |= 0x3u;
          }

          if (flags != 0) {
            NetClient_DispatchToWorker(self, candidate.conn, static_cast<int>(flags));
          }
        }
      }
    } else {
      std::unique_lock<std::mutex> wake_lock(self->wake_mutex);
      self->wake_cv.wait_for(wake_lock, std::chrono::milliseconds(timeout_ms),
                             [self]() { return self->wake_event_pending || self->stop_flag; });
      self->wake_event_pending = false;
    }
#endif

    for (const NetDispatchCandidate &candidate : candidates) {
      WowConnection_Release(candidate.conn);
    }
  }
}

int NetClient_WorkerThreadProc(void *param) {
  auto *slot = static_cast<NetWorkerSlot *>(param);
  auto *self = static_cast<NetClient *>(slot->owner);
  const int slot_index = static_cast<int>(slot->thread_index);
  NetClient_WorkerBody(self, slot_index);
  return 0;
}

void NetClient_WorkerBody(NetClient *self, int slot_index) {
  auto &slot = self->workers[slot_index];
  InvokeThreadInitCallback(self->thread_init_callback);
  if (self->available_worker_slots) {
    self->available_worker_slots->Release();
  }

  while (true) {
    slot.processor_count = GetProcessorCount();
    if (!slot.wake_event || slot.wake_event->Wait(1000) != 0) {
      continue;
    }

    void *conn = nullptr;
    {
      std::lock_guard<std::recursive_mutex> lock(slot.cs);
      if (slot.stop_flag) {
        break;
      }
      conn = slot.current_conn;
    }

    if (conn) {
      while (true) {
        const std::uint32_t flags = TakeConnectionPendingNetEvents(conn);
        if (flags == 0) {
          break;
        }

        DispatchConnectionWorkerFlags(conn, flags);
      }

      {
        std::lock_guard<std::recursive_mutex> lock(slot.cs);
        DecrementConnectionActiveWorkerDispatches(conn);
        WowConnection_Release(conn);
        slot.current_conn = nullptr;
      }

      if (self->available_worker_slots) {
        self->available_worker_slots->Release();
      }
      NetClient_SignalWakeEvent(self);
    }
  }
}

void NetClient_DispatchToWorker(NetClient *self, void *conn, int flags) {
  if (!self || !conn) {
    return;
  }

  if (self->available_worker_slots &&
      !self->available_worker_slots->WaitFor(std::chrono::milliseconds(500))) {
    return;
  }

  IncrementConnectionRefCount(conn);
  SetConnectionPendingNetEvents(conn, static_cast<std::uint32_t>(flags));
  IncrementConnectionActiveWorkerDispatches(conn);

  for (uint32_t i = 0; i < self->worker_count; ++i) {
    std::lock_guard<std::recursive_mutex> lock(self->workers[i].cs);
    if (!self->workers[i].current_conn) {
      self->workers[i].current_conn = conn;
      if (self->workers[i].wake_event) {
        self->workers[i].wake_event->Set();
      }
      return;
    }
  }

  DecrementConnectionActiveWorkerDispatches(conn);
  SetConnectionPendingNetEvents(conn, 0);
  WowConnection_Release(conn);

  if (self->available_worker_slots) {
    self->available_worker_slots->Release();
  }
}

void NetClient_ShutdownWorkers(NetClient *self) {
  if (!self) {
    return;
  }

  self->stop_flag = true;
  NetClient_SignalWakeEvent(self);
  auto *main_stop_event = GetNetClientMainStopEvent(self);
  if (self->main_thread_handle && main_stop_event && main_stop_event->Wait(10000) != 0) {
    openwow::core::ida::ConsoleLog("Main network thread did not send stop event normally");
  }
  if (self->main_thread_handle) {
    if (!WaitForThreadHandle(self->main_thread_handle, 5000)) {
      openwow::core::ida::ConsoleLog("Main network thread did not exit normally");
    } else {
      JoinThreadHandle(self->main_thread_handle);
    }
  }

  for (uint32_t i = 0; i < self->worker_count; ++i) {
    {
      std::lock_guard<std::recursive_mutex> lock(self->workers[i].cs);
      self->workers[i].stop_flag = true;
    }
    if (self->workers[i].wake_event) {
      self->workers[i].wake_event->Set();
    }
    if (self->cleanup_slots) {
      self->cleanup_slots->Release();
    }

    if (!WaitForThreadHandle(self->workers[i].thread_handle, 10000)) {
      openwow::core::ida::ConsoleLog("Network thread %d did not exit normally", i);
      continue;
    }

    JoinThreadHandle(self->workers[i].thread_handle);

    void *assigned_conn = nullptr;
    {
      std::lock_guard<std::recursive_mutex> lock(self->workers[i].cs);
      assigned_conn = self->workers[i].current_conn;
    }

    if (assigned_conn) {
      WowConnection_Release(assigned_conn);
      std::lock_guard<std::recursive_mutex> lock(self->workers[i].cs);
      if (self->workers[i].current_conn == assigned_conn) {
        self->workers[i].current_conn = nullptr;
      }
    }
  }
}

void NetClient_Destroy(NetClient *self) {
  if (!self) {
    return;
  }

  std::vector<void *> tracked_handles;
  {
    std::lock_guard<std::mutex> lock(self->socket_event_mutex);
    tracked_handles.reserve(self->connection_socket_events.size());
    for (const auto &[conn, handle] : self->connection_socket_events) {
      (void)conn;
      tracked_handles.push_back(handle);
    }
    self->connection_socket_events.clear();
  }

  for (void *handle : tracked_handles) {
    CloseSocketEventHandle(handle);
  }

  self->stop_flag = true;
  NetClient_SignalWakeEvent(self);

  for (int i = kMaxWorkerThreads - 1; i >= 0; --i) {
    {
      std::lock_guard<std::recursive_mutex> lock(self->workers[i].cs);
      self->workers[i].current_conn = nullptr;
      self->workers[i].stop_flag = true;
    }
    if (self->workers[i].wake_event) {
      self->workers[i].wake_event->Set();
    }
    DestroyThreadHandle(self->workers[i].thread_handle);
    self->workers[i].event_handle = nullptr;
    self->workers[i].wake_event.reset();
  }

  self->registered_connections.clear();
  NetClient_ResetTrackedSocketRegistry();
  self->connection_list_root.Clear();
  self->pending_list_root.Clear();
  self->cleanup_list_root.Clear();
  self->worker_semaphore = nullptr;
  self->cleanup_semaphore = nullptr;
  self->thread_init_callback = nullptr;
  self->main_stop_event = nullptr;
#ifdef _WIN32
  if (self->wsa_event) {
    CloseHandle(static_cast<HANDLE>(self->wsa_event));
    self->wsa_event = nullptr;
  }
#else
  self->wsa_event = nullptr;
  DestroyNetClientWakeSource(self);
#endif
  DestroyThreadHandle(self->main_thread_handle);
  self->available_worker_slots.reset();
  self->cleanup_slots.reset();
  self->main_stop_signal.reset();
  if (g_net_client == self) {
    g_net_client = nullptr;
  }
  delete self;
}

void NetClient_RegisterConnection(NetClient *self, void *conn) {
  if (!self || !conn) {
    return;
  }

  NetClient_TrackSocketFd(GetConnectionSocketFd(conn));

  bool inserted = false;
  {
    std::lock_guard<std::recursive_mutex> lock(self->global_cs);
    if (FindRegisteredConnection(self, conn) == self->registered_connections.end()) {
      self->registered_connections.insert(self->registered_connections.begin(), conn);
      inserted = true;
    }
  }

  if (inserted) {
    NetClient_EnsureConnectionEventAndWake(self, conn);
  }
}

void NetClient_UnregisterConnection(NetClient *self, void *conn) {
  if (!self || !conn) {
    return;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(self->global_cs);
    const auto it = FindRegisteredConnection(self, conn);
    if (it != self->registered_connections.end()) {
      self->registered_connections.erase(it);
    }
  }

  NetClient_SignalWakeEventNoArg(self, 0);
}

void NetClient_FinalizeConnectionRelease(NetClient *self, void *conn) {
  if (!self || !conn) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(self->global_cs);
  if (GetConnectionRefCount(conn) != 0) {
    return;
  }

  InvokeConnectionReleaseHooks(conn);
}

void NetClient_SignalWakeEvent(void *net_client) {
  if (!net_client) {
    return;
  }

  auto *self = static_cast<NetClient *>(net_client);
  {
    std::lock_guard<std::mutex> lock(self->wake_mutex);
    if (!self->wake_event_pending) {
      self->wake_event_pending = true;
#ifndef _WIN32
      if (self->wake_write_fd >= 0) {
        constexpr std::uint8_t kWakeToken = 1;
        (void)::write(self->wake_write_fd, &kWakeToken, sizeof(kWakeToken));
      }
#endif
    }
  }
  self->wake_cv.notify_all();

#ifdef _WIN32
  if (self->wsa_event) {
    SetEvent(static_cast<HANDLE>(self->wsa_event));
  }
#endif
}

void NetClient_SignalWakeEventNoArg(void *net_client, int ) {
  NetClient_SignalWakeEvent(net_client);
}

bool NetClient_EnsureConnectionEventAndWake(NetClient *self, void *conn) {
  if (!self || !conn) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(self->socket_event_mutex);
    auto [it, inserted] = self->connection_socket_events.emplace(conn, nullptr);
    if (inserted || !it->second) {
      it->second = CreateSocketEventHandle();
    }
  }

  EnableTcpNoDelay(GetConnectionSocketFd(conn));
  NetClient_SignalWakeEvent(self);
  return true;
}

bool NetClient_UpdateConnectionEventMaskAndWake(NetClient *self, void *conn, int unused_state) {
  (void)unused_state;

  if (!self || !conn) {
    return false;
  }

#ifdef _WIN32
  void *socket_event = GetTrackedSocketEvent(self, conn);
  const int socket_fd = GetConnectionSocketFd(conn);
  if (socket_event && socket_fd >= 0) {
    long event_mask = 0;
    switch (GetConnectionState(conn)) {
    case 2:
      event_mask = FD_CONNECT | FD_CLOSE;
      break;
    case 3:
      event_mask = FD_ACCEPT;
      break;
    case 5: {
      const bool want_write =
          GetConnectionQueueState(conn) != 0 || GetConnectionHasPendingSendWake(conn);
      event_mask = FD_READ | FD_CLOSE;
      if (want_write) {
        event_mask |= FD_WRITE;
      }
      break;
    }
    default:
      break;
    }

    WSAEventSelect(socket_fd, static_cast<WSAEVENT>(socket_event), event_mask);
  }
#endif

  NetClient_SignalWakeEvent(self);
  return true;
}

void WowConnection_CloseWSAEvent(void *conn) {
  if (!conn || !g_net_client) {
    return;
  }

  void *handle = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_net_client->socket_event_mutex);
    const auto it = g_net_client->connection_socket_events.find(conn);
    if (it == g_net_client->connection_socket_events.end()) {
      return;
    }

    handle = it->second;
    g_net_client->connection_socket_events.erase(it);
  }

  CloseSocketEventHandle(handle);
}

int WowConnection_Release(void *conn) {
  if (!conn) {
    return 0;
  }

  const std::int32_t ref_count = DecrementConnectionRefCount(conn);
  NetClient *const net_client = GetGlobalNetClient();
  if (ref_count <= 0) {
    EraseRetailReceiveBufferState(conn);
    if (net_client) {
      NetClient_FinalizeConnectionRelease(net_client, conn);
    } else {
      InvokeConnectionReleaseHooks(conn);
    }
  }

  return static_cast<int>(ref_count);
}

void WowConnection_RegisterReleaseHooks(void *conn, WowConnectionReleaseHooks hooks) {
  if (!conn) {
    return;
  }

  auto &registry = GetConnectionReleaseRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  registry.hooks[conn] = hooks;
}

void WowConnection_UnregisterReleaseHooks(void *conn) {
  if (!conn) {
    return;
  }

  auto &registry = GetConnectionReleaseRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  registry.hooks.erase(conn);
}

void WowConnection_SetSocketCreateFnForTests(WowConnectionSocketCreateFn fn) {
  GetSocketCreateFn() = fn ? fn : &CreateSocketFdDefault;
}

void WowConnection_ResetSocketCreateFnForTests() {
  GetSocketCreateFn() = &CreateSocketFdDefault;
}

void WowConnection_ConnectInternal(void *conn) {
  auto *net_client = GetGlobalNetClient();

  if (auto *managed = WowConnection_TryGetManaged(conn)) {
    std::lock_guard<std::mutex> lock(managed->conn_cs_);

    const int old_fd = managed->socket_fd_;
    if (old_fd >= 0) {
      if (net_client) {
        NetClient_UnregisterConnection(net_client, conn);
      }
      WowConnection_CloseSocket(old_fd);
      managed->socket_fd_ = -1;
    }

    const int fd = GetSocketCreateFn()(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      const auto failed_previous_state = managed->state_;
      ReportSocketCreateFailure(fd);
      managed->state_ = WowConnectionState::kConnectFailed;
      if (net_client) {
        NetClient_UpdateConnectionEventMaskAndWake(
            net_client, conn, static_cast<int>(failed_previous_state));
      }
      return;
    }

    SetSocketNonBlocking(fd);
    managed->socket_fd_ = fd;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(managed->connect_port_);
    addr.sin_addr.s_addr = managed->connect_addr_v4_;

    if (net_client) {
      NetClient_RegisterConnection(net_client, conn);
    }

    const auto previous_state = managed->state_;
    managed->heap_buffer_received_ = 0;
    managed->state_ = WowConnectionState::kConnecting;

    if (net_client) {
      NetClient_UpdateConnectionEventMaskAndWake(net_client, conn,
                                                 static_cast<int>(previous_state));
    }

    const int result = connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
    if (result < 0 && !SocketErrorIsConnectPending(GetSocketErrorCode())) {
      if (net_client) {
        NetClient_UnregisterConnection(net_client, conn);
      }
      WowConnection_CloseSocket(fd);
      managed->socket_fd_ = -1;
      const auto failed_previous_state = managed->state_;
      managed->state_ = WowConnectionState::kConnectFailed;
      if (net_client) {
        NetClient_UpdateConnectionEventMaskAndWake(net_client, conn,
                                                   static_cast<int>(failed_previous_state));
      }
    }
    return;
  }

  auto *fields = reinterpret_cast<uint32_t *>(conn);

  const int old_fd = static_cast<int>(fields[1]);
  if (old_fd >= 0) {
    if (net_client) {
      NetClient_UnregisterConnection(net_client, conn);
    }
    WowConnection_CloseSocket(old_fd);
    fields[1] = static_cast<uint32_t>(-1);
  }

  const int fd = GetSocketCreateFn()(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    const uint32_t failed_previous_state = fields[4];
    ReportSocketCreateFailure(fd);
    fields[4] = 8;
    if (net_client) {
      NetClient_UpdateConnectionEventMaskAndWake(
          net_client, conn, static_cast<int>(failed_previous_state));
    }
    return;
  }

  fields[1] = static_cast<uint32_t>(fd);
  SetSocketNonBlocking(fd);

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(GetRetailConnectionConnectPort(conn));
  addr.sin_addr.s_addr = GetRetailConnectionConnectAddressV4(conn);

  if (net_client) {
    NetClient_RegisterConnection(net_client, conn);
  }

  const uint32_t previous_state = fields[4];
  fields[8] = 0;
  fields[4] = 2;

  if (net_client) {
    NetClient_UpdateConnectionEventMaskAndWake(net_client, conn, static_cast<int>(previous_state));
  }

  const int result = connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
  if (result < 0 && !SocketErrorIsConnectPending(GetSocketErrorCode())) {
    if (net_client) {
      NetClient_UnregisterConnection(net_client, conn);
    }
    WowConnection_CloseSocket(fd);
    fields[1] = static_cast<uint32_t>(-1);
    const uint32_t failed_previous_state = fields[4];
    fields[4] = 8;
    if (net_client) {
      NetClient_UpdateConnectionEventMaskAndWake(net_client, conn,
                                                 static_cast<int>(failed_previous_state));
    }
  }
}

bool WowConnection_Connect(void *conn, uint32_t addr, uint16_t port, int ) {
  if (auto *managed = WowConnection_TryGetManaged(conn)) {
    std::lock_guard<std::mutex> lock(managed->conn_cs_);
    managed->connect_addr_v4_ = addr;
    managed->connect_port_ = port;
  } else {
    SetRetailConnectionConnectAddressV4(conn, addr);
    SetRetailConnectionConnectPort(conn, port);
  }
  WowConnection_ConnectInternal(conn);
  return true;
}

bool WowConnection_ConnectByAddressString(
    void *conn, const char *host, uint16_t port, int unused) {
  return WowConnection_Connect(
      conn, ResolveRetailConnectionAddressV4(host), port, unused);
}

bool WowConnection_ConnectByHostPortString(
    void *conn, const char *endpoint, int timeout_ms) {
  std::array<char, 256> host{};
  std::uint16_t port = 0;
  SplitRetailHostPortString(endpoint, host, port);
  return WowConnection_ConnectByAddressString(conn, host.data(), port, timeout_ms);
}

void WowConnection_DataReady(void *conn) {
  if (auto *managed = WowConnection_TryGetManaged(conn)) {
    managed->DataReady();
    return;
  }

  auto *fields = reinterpret_cast<uint32_t *>(conn);
  auto &receive_state = GetRetailReceiveBufferState(conn);

  if (!receive_state.buffer) {
    receive_state.buffer = std::make_unique<std::uint8_t[]>(1024);
    receive_state.capacity = 1024;
    receive_state.received = 0;
    fields[9] = receive_state.capacity;
    fields[8] = receive_state.received;
  }

  while (true) {
    const uint32_t received = receive_state.received;
    int header_size = 2;
    int packet_size = -1;

    if (received >= 2) {
      auto *buf = receive_state.buffer.get();
      if (buf[0] & 0x80) {
        header_size = 3;
        if (received >= 3) {
          packet_size = ((buf[0] & 0x7F) << 16) | (buf[1] << 8) | buf[2];
          packet_size += 3;
        }
      } else {
        packet_size = ((buf[0] & 0x7F) << 8) | buf[1];
        packet_size += 2;
      }
    }

    const uint32_t capacity = receive_state.capacity;
    if (received >= capacity) {
      uint32_t new_cap = capacity * 2;
      if (new_cap > 0x1E84800) {
        return;
      }

      auto new_buf = std::make_unique<std::uint8_t[]>(new_cap);
      std::memcpy(new_buf.get(), receive_state.buffer.get(), received);
      receive_state.buffer = std::move(new_buf);
      receive_state.capacity = new_cap;
      fields[9] = new_cap;
    }

    int want = 0;
    if (packet_size >= 0) {
      want = packet_size - static_cast<int>(received);
      if (static_cast<int>(capacity - received) < want) {
        want = static_cast<int>(capacity - received);
      }
    } else {
      want = header_size - static_cast<int>(received);
    }

    if (want <= 0 && packet_size < 0) {
      break;
    }

    if (want > 0) {
      const int fd = static_cast<int>(fields[1]);
      auto *buf = reinterpret_cast<char *>(receive_state.buffer.get()) + received;
      const int bytes_read = recv(fd, buf, want, 0);
      if (bytes_read <= 0) {
#ifdef _WIN32
        if (bytes_read < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
#else
        if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
#endif
          break;
        }

        fields[4] = 6;
        return;
      }

      receive_state.received += static_cast<uint32_t>(bytes_read);
      fields[8] = receive_state.received;
    }

    if (packet_size >= 0
        && receive_state.received >= static_cast<uint32_t>(packet_size)) {
      receive_state.received = 0;
      fields[8] = 0;
    }

    if (want <= 0) {
      break;
    }
  }
}

int WowConnection_AcceptLoop(void *listener_conn) {
  const int listen_fd = GetConnectionSocketFd(listener_conn);
  int count = 0;

  for (int i = 0; i < 1000; ++i) {
    struct sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);

    const int new_fd = accept(listen_fd, reinterpret_cast<struct sockaddr *>(&addr), &addr_len);
    if (new_fd < 0) {
      break;
    }

    if (!PassesConnectionFilter(new_fd)) {
      CloseSocketFd(new_fd);
      continue;
    }

    SetSocketNonBlocking(new_fd);

    auto accepted = std::make_unique<WowConnection>();
    accepted->AcceptSocket(new_fd, GetConnectionHandler(listener_conn));
    accepted->AddReference();

    IncrementConnectionRefCount(listener_conn);
    WowConnectionResponse *current_handler = BeginConnectionHandlerInvocation(listener_conn);
    if (current_handler && current_handler->on_accept) {
      current_handler->on_accept(WowConnection_TryGetManaged(listener_conn), accepted.get(),
                                 GetProcessorCount());
    }
    EndConnectionHandlerInvocation(listener_conn);

    if (auto *net_client = GetGlobalNetClient()) {
      WowConnection *accepted_raw = accepted.get();
      NetClient_RegisterConnection(net_client, accepted_raw);
      NetClient_UpdateConnectionEventMaskAndWake(net_client, accepted_raw,
                                                 static_cast<int>(accepted_raw->GetState()));
      accepted.release();
      WowConnection_Release(accepted_raw);
    } else {
      accepted->ReleaseReference();
    }

    WowConnection_Release(listener_conn);

    ++count;
  }

  return count;
}

}

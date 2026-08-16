#pragma once

#include "openwow/net/transport/packet_queue.h"
#include "openwow/net/wotlk/protocol/world_protocol.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace openwow::net {

using DisconnectCallback = std::function<void()>;

class NetworkRecvThread {
 public:
  NetworkRecvThread() = default;
  ~NetworkRecvThread();

  NetworkRecvThread(const NetworkRecvThread&) = delete;
  NetworkRecvThread& operator=(const NetworkRecvThread&) = delete;
  NetworkRecvThread(NetworkRecvThread&&) = delete;
  NetworkRecvThread& operator=(NetworkRecvThread&&) = delete;

  void Start(wotlk::RealmSession* session, PacketQueue* queue,
             DisconnectCallback on_disconnect = {});

  void Stop();

  [[nodiscard]] bool IsRunning() const { return running_.load(std::memory_order_relaxed); }

  [[nodiscard]] bool IsDisconnected() const { return disconnected_.load(std::memory_order_relaxed); }

  [[nodiscard]] std::uint64_t GetPacketsReceived() const { return packets_received_.load(std::memory_order_relaxed); }

  [[nodiscard]] std::uint32_t GetErrorCount() const { return error_count_.load(std::memory_order_relaxed); }

  [[nodiscard]] std::uint32_t GetTimeoutCount() const { return timeout_count_.load(std::memory_order_relaxed); }

  [[nodiscard]] bool IsStopRequested() const { return stop_requested_.load(std::memory_order_relaxed); }

  [[nodiscard]] std::string GetStatsSummary() const;

  void ResetStats();

 private:
  void ThreadMain();

  wotlk::RealmSession* session_{nullptr};
  PacketQueue* queue_{nullptr};
  DisconnectCallback on_disconnect_;

  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> disconnected_{false};
  std::atomic<std::uint64_t> packets_received_{0};
  std::atomic<std::uint32_t> error_count_{0};
  std::atomic<std::uint32_t> timeout_count_{0};
  std::thread thread_;
};

}

#include "openwow/net/transport/network_recv_thread.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <chrono>
#include <cinttypes>

namespace openwow::net {

NetworkRecvThread::~NetworkRecvThread() {
  Stop();
}

void NetworkRecvThread::Start(wotlk::RealmSession* session, PacketQueue* queue,
                              DisconnectCallback on_disconnect) {
  if (running_.load(std::memory_order_relaxed)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "NetworkRecvThread: already running, ignoring Start()");
    return;
  }

  if (!session) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                       "NetworkRecvThread: cannot Start() with null session");
    return;
  }

  if (!queue) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                       "NetworkRecvThread: cannot Start() with null queue");
    return;
  }

  session_ = session;
  queue_ = queue;
  on_disconnect_ = std::move(on_disconnect);
  stop_requested_.store(false, std::memory_order_relaxed);
  disconnected_.store(false, std::memory_order_relaxed);
  packets_received_.store(0, std::memory_order_relaxed);
  error_count_.store(0, std::memory_order_relaxed);
  timeout_count_.store(0, std::memory_order_relaxed);
  running_.store(true, std::memory_order_relaxed);

  thread_ = std::thread(&NetworkRecvThread::ThreadMain, this);
}

void NetworkRecvThread::Stop() {

  stop_requested_.store(true, std::memory_order_release);
  if (thread_.joinable()) {
    thread_.join();
  }
  running_.store(false, std::memory_order_relaxed);
}

void NetworkRecvThread::ThreadMain() {
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "NetworkRecvThread: started");

  constexpr int kRecvTimeoutMs = 500;
  constexpr int kMaxConsecutiveTimeouts = 120;
  int consecutive_timeouts = 0;
  const auto should_stop = [this] {
    return stop_requested_.load(std::memory_order_acquire);
  };

  while (!should_stop()) {

    if (!session_ || !session_->connected()) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                         "NetworkRecvThread: session disconnected, exiting");
      disconnected_.store(true, std::memory_order_relaxed);
      break;
    }

    wotlk::WorldPacket pkt;
    if (!session_->RecvPacket(pkt, kRecvTimeoutMs, should_stop)) {
      if (should_stop()) {
        break;
      }

      if (!session_->connected()) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                           "NetworkRecvThread: connection lost during recv");
        disconnected_.store(true, std::memory_order_relaxed);
        error_count_.fetch_add(1, std::memory_order_relaxed);
        break;
      }

      timeout_count_.fetch_add(1, std::memory_order_relaxed);
      ++consecutive_timeouts;

      if (consecutive_timeouts >= kMaxConsecutiveTimeouts) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "NetworkRecvThread: no data for 60+ seconds");
        consecutive_timeouts = 0;
      }
      continue;
    }

    consecutive_timeouts = 0;
    packets_received_.fetch_add(1, std::memory_order_relaxed);

    (void)session_->ObserveConnectionPacket(
        pkt, openwow::core::GameClock::GetTickCount32());
    queue_->Push(std::move(pkt));
  }

  running_.store(false, std::memory_order_relaxed);

  {
    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "NetworkRecvThread: exited (received=%" PRIu64 " errors=%u timeouts=%u)",
                  packets_received_.load(std::memory_order_relaxed),
                  error_count_.load(std::memory_order_relaxed),
                  timeout_count_.load(std::memory_order_relaxed));
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, msg);
  }

  if (disconnected_.load(std::memory_order_relaxed) && on_disconnect_) {
    on_disconnect_();
  }
}

std::string NetworkRecvThread::GetStatsSummary() const {
  std::string out;
  out.reserve(256);
  out += "NetworkRecvThread Stats:\n";
  out += "  running       : ";
  out += (running_.load(std::memory_order_relaxed) ? "yes" : "no");
  out += "\n  disconnected  : ";
  out += (disconnected_.load(std::memory_order_relaxed) ? "yes" : "no");
  out += "\n  stop_requested: ";
  out += (stop_requested_.load(std::memory_order_relaxed) ? "yes" : "no");
  out += "\n  packets_recv  : ";
  out += std::to_string(packets_received_.load(std::memory_order_relaxed));
  out += "\n  errors        : ";
  out += std::to_string(error_count_.load(std::memory_order_relaxed));
  out += "\n  timeouts      : ";
  out += std::to_string(timeout_count_.load(std::memory_order_relaxed));
  out += "\n";
  return out;
}

void NetworkRecvThread::ResetStats() {
  packets_received_.store(0, std::memory_order_relaxed);
  error_count_.store(0, std::memory_order_relaxed);
  timeout_count_.store(0, std::memory_order_relaxed);
}

}

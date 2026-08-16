#include "openwow/net/transport/tcp_client.h"

#include "openwow/foundation/diagnostics/logging.h"

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace openwow::net {
namespace {

using Tcp = boost::asio::ip::tcp;

constexpr auto kCancellationPollInterval = std::chrono::milliseconds(2);

const char* IoFailureKind(const boost::system::error_code& error,
                          const bool timed_out,
                          const bool cancelled_by_caller) {
  if (cancelled_by_caller) {
    return "caller_cancelled";
  }
  if (timed_out) {
    return "timeout";
  }
  if (error == boost::asio::error::eof) {
    return "peer_eof";
  }
  if (error == boost::asio::error::connection_reset) {
    return "connection_reset";
  }
  if (error == boost::asio::error::connection_aborted) {
    return "connection_aborted";
  }
  if (error == boost::asio::error::broken_pipe) {
    return "broken_pipe";
  }
  if (error == boost::asio::error::operation_aborted) {
    return "operation_aborted";
  }
  return "io_error";
}

struct OperationState {
  OperationState(boost::asio::io_context& io,
                 const std::uint32_t timeout_value,
                 const bool has_deadline_value = true)
      : timer(io),
        timeout_ms(timeout_value),
        has_deadline(has_deadline_value) {}

  std::atomic_bool cancellation_posted{false};
  boost::asio::cancellation_signal cancellation;
  boost::asio::steady_timer timer;
  std::uint64_t socket_epoch{0};
  std::uint32_t timeout_ms{0};
  bool has_deadline{true};
  bool completed{false};
  bool timed_out{false};
  bool cancelled_by_caller{false};
};

void LogIoFailure(const char* const operation,
                  const boost::system::error_code& error,
                  const OperationState& state,
                  const std::size_t transferred,
                  const std::size_t requested) {
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kWarn,
      std::string("TcpClient: ") + operation + " failed kind=" +
          IoFailureKind(error, state.timed_out, state.cancelled_by_caller) +
          " error_code=" + std::to_string(error.value()) +
          " error=\"" + error.message() + "\" transferred=" +
          std::to_string(transferred) + " requested=" +
          std::to_string(requested));
}

struct ConnectState final : OperationState {
  ConnectState(boost::asio::io_context& io,
               std::string host_value,
               const std::uint16_t port_value,
               const std::uint32_t timeout_value,
               const std::uint64_t lifecycle_value)
      : OperationState(io, timeout_value),
        resolver(io),
        host(std::move(host_value)),
        service(std::to_string(port_value)),
        lifecycle(lifecycle_value) {}

  Tcp::resolver resolver;
  Tcp::resolver::results_type endpoints;
  std::promise<bool> result;
  std::string host;
  std::string service;
  std::uint64_t lifecycle{0};
};

struct WriteState final : OperationState {
  WriteState(boost::asio::io_context& io,
             std::vector<std::uint8_t> bytes_value,
             const std::uint32_t timeout_value)
      : OperationState(io, timeout_value), bytes(std::move(bytes_value)) {}

  std::promise<bool> result;
  std::vector<std::uint8_t> bytes;
};

struct ReadState final : OperationState {
  ReadState(boost::asio::io_context& io,
            const std::size_t byte_count,
            const bool exact_value,
            const std::uint32_t timeout_value,
            const bool has_deadline_value)
      : OperationState(io, timeout_value, has_deadline_value),
        buffer(byte_count),
        exact(exact_value) {}

  std::promise<std::vector<std::uint8_t>> result;
  std::vector<std::uint8_t> buffer;
  bool exact{false};
};

}

struct TcpClient::Impl {
  Impl()
      : work_guard_(boost::asio::make_work_guard(io_context_)),
        socket_(io_context_),
        worker_([this] { io_context_.run(); }) {}

  ~Impl() {
    Disconnect();
    work_guard_.reset();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  bool Connect(const std::string& host,
               const std::uint16_t port,
               const std::uint32_t timeout_ms,
               const std::function<bool()>& should_cancel) {
    std::lock_guard connect_lock(connect_call_mutex_);

    const auto lifecycle = NextLifecycle();
    connected_.store(false, std::memory_order_release);
    DispatchAndWait([this, lifecycle] {
      if (IsCurrentLifecycle(lifecycle)) {
        CancelActiveConnect();
        CloseNativeSocket();
      }
    });

    if (!IsCurrentLifecycle(lifecycle) || host.empty() || port == 0) {
      return false;
    }

    auto state = std::make_shared<ConnectState>(
        io_context_, host, port, timeout_ms, lifecycle);
    auto result = state->result.get_future();
    boost::asio::post(io_context_, [this, state] { StartConnect(state); });
    return Await(result, state, should_cancel,
                 [this, state] { CancelConnect(state); });
  }

  bool Write(const std::vector<std::uint8_t>& bytes,
             const std::uint32_t timeout_ms,
             const std::function<bool()>& should_cancel) {
    if (bytes.empty() || !IsConnected()) {
      return false;
    }

    std::lock_guard write_lock(write_call_mutex_);
    if (!IsConnected()) {
      return false;
    }

    auto state = std::make_shared<WriteState>(io_context_, bytes, timeout_ms);
    auto result = state->result.get_future();
    boost::asio::post(io_context_, [this, state] { StartWrite(state); });
    return Await(result, state, should_cancel,
                 [state] { CancelStreamOperation(state); });
  }

  std::vector<std::uint8_t> Read(const std::size_t bytes,
                                 const bool exact,
                                 const std::uint32_t timeout_ms,
                                 const std::function<bool()>& should_cancel,
                                 const bool has_deadline = true) {
    if (bytes == 0 || !IsConnected()) {
      return {};
    }

    std::lock_guard read_lock(read_call_mutex_);
    if (!IsConnected()) {
      return {};
    }

    auto state =
        std::make_shared<ReadState>(io_context_, bytes, exact, timeout_ms,
                                    has_deadline);
    auto result = state->result.get_future();
    boost::asio::post(io_context_, [this, state] { StartRead(state); });
    return Await(result, state, should_cancel,
                 [state] { CancelStreamOperation(state); });
  }

  void Disconnect() {
    const auto lifecycle = NextLifecycle();
    connected_.store(false, std::memory_order_release);
    DispatchAndWait([this, lifecycle] {
      if (!IsCurrentLifecycle(lifecycle)) {
        return;
      }
      CancelActiveConnect();
      CloseNativeSocket();
    });
  }

  [[nodiscard]] bool IsConnected() const {
    return connected_.load(std::memory_order_acquire);
  }

 private:
  template <typename Result, typename State, typename Cancel>
  Result Await(std::future<Result>& result,
               const std::shared_ptr<State>& state,
               const std::function<bool()>& should_cancel,
               Cancel&& cancel) {
    if (!should_cancel) {
      return result.get();
    }

    while (result.wait_for(kCancellationPollInterval) !=
           std::future_status::ready) {
      if (should_cancel() &&
          !state->cancellation_posted.exchange(true,
                                               std::memory_order_acq_rel)) {
        boost::asio::post(io_context_, std::forward<Cancel>(cancel));
      }
    }
    return result.get();
  }

  template <typename Action>
  void DispatchAndWait(Action&& action) {
    auto completion = std::make_shared<std::promise<void>>();
    auto done = completion->get_future();
    boost::asio::dispatch(
        io_context_,
        [action = std::forward<Action>(action), completion]() mutable {
          action();
          completion->set_value();
        });
    done.wait();
  }

  [[nodiscard]] std::uint64_t NextLifecycle() {
    return lifecycle_.fetch_add(1, std::memory_order_acq_rel) + 1;
  }

  [[nodiscard]] bool IsCurrentLifecycle(
      const std::uint64_t lifecycle) const {
    return lifecycle_.load(std::memory_order_acquire) == lifecycle;
  }

  template <typename State, typename OnTimeout>
  static void ArmTimeout(const std::shared_ptr<State>& state,
                         OnTimeout&& on_timeout) {
    if (!state->has_deadline) {
      return;
    }
    state->timer.expires_after(std::chrono::milliseconds(state->timeout_ms));
    state->timer.async_wait(
        [state, on_timeout = std::forward<OnTimeout>(on_timeout)](
            const boost::system::error_code& error) mutable {
          if (!error && !state->completed) {
            state->timed_out = true;
            on_timeout();
          }
        });
  }

  template <typename State>
  static bool ClaimCompletion(const std::shared_ptr<State>& state) {
    if (state->completed) {
      return false;
    }
    state->completed = true;
    state->timer.cancel();
    return true;
  }

  template <typename State>
  static void CancelStreamOperation(const std::shared_ptr<State>& state) {
    if (state->completed) {
      return;
    }
    state->cancelled_by_caller = true;
    state->cancellation.emit(boost::asio::cancellation_type::all);
  }

  void StartConnect(const std::shared_ptr<ConnectState>& state) {
    if (!IsCurrentLifecycle(state->lifecycle)) {
      CompleteConnect(state, false);
      return;
    }

    state->socket_epoch = socket_epoch_;
    active_connect_ = state;
    ArmTimeout(state, [this, state] { CancelConnect(state); });

    state->resolver.async_resolve(
        state->host, state->service,
        boost::asio::bind_cancellation_slot(
            state->cancellation.slot(),
            [this, state](const boost::system::error_code& error,
                          Tcp::resolver::results_type endpoints) {
              if (error || state->completed ||
                  !IsCurrentLifecycle(state->lifecycle) ||
                  state->socket_epoch != socket_epoch_) {
                CompleteConnect(state, false);
                return;
              }

              state->endpoints = std::move(endpoints);
              boost::asio::async_connect(
                  socket_, state->endpoints,
                  boost::asio::bind_cancellation_slot(
                      state->cancellation.slot(),
                      [this, state](const boost::system::error_code& connect_error,
                                    const Tcp::endpoint&) {
                        const bool success =
                            !connect_error && !state->timed_out &&
                            !state->cancelled_by_caller &&
                            IsCurrentLifecycle(state->lifecycle) &&
                            state->socket_epoch == socket_epoch_ &&
                            socket_.is_open();
                        CompleteConnect(state, success);
                      }));
            }));
  }

  void CancelConnect(const std::shared_ptr<ConnectState>& state) {
    if (state->completed) {
      return;
    }
    if (!state->timed_out) {
      state->cancelled_by_caller = true;
    }
    state->resolver.cancel();
    state->cancellation.emit(boost::asio::cancellation_type::all);
    if (IsCurrentLifecycle(state->lifecycle) &&
        state->socket_epoch == socket_epoch_) {
      CloseNativeSocket();
    }
  }

  void CompleteConnect(const std::shared_ptr<ConnectState>& state,
                       const bool success) {
    if (!ClaimCompletion(state)) {
      return;
    }
    if (auto active = active_connect_.lock(); active == state) {
      active_connect_.reset();
    }

    if (success) {
      connected_.store(true, std::memory_order_release);
    } else if (state->socket_epoch == socket_epoch_ &&
               IsCurrentLifecycle(state->lifecycle)) {
      CloseNativeSocket();
    }
    state->result.set_value(success);
  }

  void StartWrite(const std::shared_ptr<WriteState>& state) {
    if (!connected_.load(std::memory_order_acquire) || !socket_.is_open()) {
      state->completed = true;
      state->result.set_value(false);
      return;
    }

    state->socket_epoch = socket_epoch_;
    ArmTimeout(state, [state] {
      state->cancellation.emit(boost::asio::cancellation_type::all);
    });

    boost::asio::async_write(
        socket_, boost::asio::buffer(state->bytes),
        boost::asio::bind_cancellation_slot(
            state->cancellation.slot(),
            [this, state](const boost::system::error_code& error,
                          const std::size_t written) {
              if (!ClaimCompletion(state)) {
                return;
              }
              const bool success = !error && written == state->bytes.size();
              if (!success) {
                if (!state->cancelled_by_caller) {
                  LogIoFailure("write", error, *state, written,
                               state->bytes.size());
                }
                CloseForOperation(state->socket_epoch);
              }
              state->result.set_value(success);
            }));
  }

  void StartRead(const std::shared_ptr<ReadState>& state) {
    if (!connected_.load(std::memory_order_acquire) || !socket_.is_open()) {
      state->completed = true;
      state->result.set_value({});
      return;
    }

    state->socket_epoch = socket_epoch_;
    ArmTimeout(state, [state] {

      state->cancellation.emit(boost::asio::cancellation_type::all);
    });

    const auto completion = boost::asio::bind_cancellation_slot(
        state->cancellation.slot(),
        [this, state](const boost::system::error_code& error,
                      const std::size_t received) {
          if (!ClaimCompletion(state)) {
            return;
          }

          if (state->cancelled_by_caller) {
            if (state->exact) {
              CloseForOperation(state->socket_epoch);
              state->result.set_value({});
              return;
            }
            state->buffer.resize(received);
            state->result.set_value(std::move(state->buffer));
            return;
          }

          const bool timed_out_read_some_with_data =
              state->timed_out && !state->exact && received != 0;
          if (!error || timed_out_read_some_with_data) {
            state->buffer.resize(received);
            state->result.set_value(std::move(state->buffer));
            return;
          }

          const bool fatal = !state->timed_out ||
                             (state->exact && received != 0);
          if (fatal) {
            LogIoFailure(state->exact ? "read_exact" : "read_some", error,
                         *state, received, state->buffer.size());
            CloseForOperation(state->socket_epoch);
          }
          state->result.set_value({});
        });

    if (state->exact) {
      boost::asio::async_read(socket_, boost::asio::buffer(state->buffer),
                              completion);
    } else {
      socket_.async_read_some(boost::asio::buffer(state->buffer), completion);
    }
  }

  void CancelActiveConnect() {
    if (auto active = active_connect_.lock(); active && !active->completed) {
      active->resolver.cancel();
      active->cancellation.emit(boost::asio::cancellation_type::all);
    }
  }

  void CloseForOperation(const std::uint64_t operation_epoch) {
    if (operation_epoch == socket_epoch_) {
      CloseNativeSocket();
    }
  }

  void CloseNativeSocket() {
    connected_.store(false, std::memory_order_release);
    ++socket_epoch_;
    boost::system::error_code ignored;
    socket_.close(ignored);
  }

  boost::asio::io_context io_context_;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      work_guard_;
  Tcp::socket socket_;
  std::thread worker_;

  std::atomic_bool connected_{false};
  std::atomic<std::uint64_t> lifecycle_{0};
  std::uint64_t socket_epoch_{0};
  std::weak_ptr<ConnectState> active_connect_;

  std::mutex connect_call_mutex_;
  std::mutex read_call_mutex_;
  std::mutex write_call_mutex_;
};

TcpClient::TcpClient() : impl_(std::make_unique<Impl>()) {}

TcpClient::~TcpClient() = default;

bool TcpClient::Connect(const std::string& host,
                        const std::uint16_t port,
                        const std::uint32_t timeout_ms,
                        const std::function<bool()>& should_cancel) {
  return impl_->Connect(host, port, timeout_ms, should_cancel);
}

bool TcpClient::Write(const std::vector<std::uint8_t>& bytes,
                      const std::uint32_t timeout_ms,
                      const std::function<bool()>& should_cancel) {
  return impl_->Write(bytes, timeout_ms, should_cancel);
}

std::vector<std::uint8_t> TcpClient::ReadSome(
    const std::size_t max_bytes,
    const std::uint32_t timeout_ms,
    const std::function<bool()>& should_cancel) {
  return impl_->Read(max_bytes, false, timeout_ms, should_cancel);
}

std::vector<std::uint8_t> TcpClient::ReadSomeUntilCancelled(
    const std::size_t max_bytes,
    const std::function<bool()>& should_cancel) {
  return impl_->Read(max_bytes, false, 0, should_cancel,
                     false);
}

std::vector<std::uint8_t> TcpClient::ReadExact(const std::size_t bytes,
                                               const std::uint32_t timeout_ms,
                                               const std::function<bool()> &should_cancel) {
  return impl_->Read(bytes, true, timeout_ms, should_cancel);
}

std::vector<std::uint8_t>
TcpClient::ReadExactUntilCancelled(const std::size_t bytes,
                                   const std::function<bool()> &should_cancel) {
  return impl_->Read(bytes, true, 0, should_cancel, false);
}

void TcpClient::Disconnect() {
  impl_->Disconnect();
}

bool TcpClient::IsConnected() const {
  return impl_->IsConnected();
}

}

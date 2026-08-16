#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::game {

template <typename Key>
class BasicAsyncQueryChannel {
 public:
  static constexpr std::uint32_t kDispatchWindowMs = 30000u;
  using KeyType = Key;

  struct CallbackKey {
    std::uintptr_t function_id;
    std::uint32_t cookie;

    CallbackKey() : function_id(0), cookie(0) {}
    CallbackKey(std::uintptr_t function_id_in, std::uint32_t cookie_in)
        : function_id(function_id_in), cookie(cookie_in) {}

    [[nodiscard]] bool IsValid() const {
      return function_id != 0 || cookie != 0;
    }

    [[nodiscard]] bool operator==(const CallbackKey& other) const {
      return function_id == other.function_id && cookie == other.cookie;
    }
  };

  using Callback = std::function<void(bool success)>;
  using Dispatcher = std::function<void(Key key, std::uint64_t context)>;
  struct RequestOptions {
    std::uint64_t context = 0;
    CallbackKey callback_key{};
    bool dedupe_callbacks = true;
    Callback callback{};
  };

  void SetDispatcher(Dispatcher dispatcher) {
    dispatcher_ = std::move(dispatcher);
    if (!dispatcher_) {
      return;
    }
    if (dispatch_budget_per_window_ == 0) {
      DispatchUndispatchedEntries(0);
    }
  }

  [[nodiscard]] bool HasDispatcher() const {
    return static_cast<bool>(dispatcher_);
  }

  void SetMaxInFlight(std::uint32_t max_in_flight) {
    dispatch_budget_per_window_ = max_in_flight;
    if (dispatch_budget_per_window_ == 0 && dispatcher_) {
      DispatchUndispatchedEntries(0);
    }
  }

  [[nodiscard]] bool IsPending(Key key) const {
    return pending_.contains(key);
  }

  void MarkPending(Key key, std::uint32_t current_tick_ms,
                   std::uint64_t context = 0) {
    if (key == Key{}) {
      return;
    }

    auto& entry = pending_[key];
    if (entry.context == 0) {
      entry.context = context;
    }
    if (entry.dispatched) {
      return;
    }

    entry.dispatched = true;
    CountExternalDispatch(current_tick_ms);
  }

  void Request(Key key, std::uint32_t current_tick_ms,
               RequestOptions options) {
    if (key == Key{}) {
      return;
    }

    auto [it, inserted] = pending_.try_emplace(key);
    auto& entry = it->second;
    if (entry.context == 0) {
      entry.context = options.context;
    }

    if (options.callback) {
      const bool has_duplicate =
          options.callback_key.IsValid() && options.dedupe_callbacks &&
          std::any_of(entry.callbacks.begin(), entry.callbacks.end(),
                      [&options](const PendingCallback& pending) {
                        return pending.key == options.callback_key;
                      });
      if (!has_duplicate) {
        entry.callbacks.push_back(
            PendingCallback{.key = options.callback_key,
                            .callback = std::move(options.callback)});
      }
    }

    if ((inserted || !entry.dispatched) && !IsQueued(key)) {
      if (!TryDispatch(key, entry, current_tick_ms)) {
        queued_keys_.push_back(key);
      }
    }
  }

  void Request(Key key, std::uint32_t current_tick_ms,
               std::uint64_t context = 0,
               CallbackKey callback_key = CallbackKey(),
               bool dedupe_callbacks = true,
               Callback callback = Callback()) {
    Request(key, current_tick_ms,
            RequestOptions{.context = context,
                           .callback_key = callback_key,
                           .dedupe_callbacks = dedupe_callbacks,
                           .callback = std::move(callback)});
  }

  void Pump(std::uint32_t current_tick_ms) {
    if (!dispatcher_ || queued_keys_.empty()) {
      return;
    }

    if (dispatch_budget_per_window_ == 0) {
      while (!queued_keys_.empty()) {
        const Key key = queued_keys_.front();
        queued_keys_.pop_front();

        auto it = pending_.find(key);
        if (it == pending_.end() || it->second.dispatched) {
          continue;
        }
        TryDispatch(key, it->second, current_tick_ms);
      }
      return;
    }

    RefreshDispatchWindow(current_tick_ms);
    while (!queued_keys_.empty()) {
      const Key key = queued_keys_.front();
      auto it = pending_.find(key);
      if (it == pending_.end() || it->second.dispatched) {
        queued_keys_.pop_front();
        continue;
      }
      if (dispatched_this_window_ >= dispatch_budget_per_window_) {
        break;
      }

      queued_keys_.pop_front();
      TryDispatch(key, it->second, current_tick_ms);
    }
  }

  [[nodiscard]] std::vector<Callback> Resolve(Key key) {
    auto it = pending_.find(key);
    if (it == pending_.end()) {
      return {};
    }

    auto pending_callbacks = std::move(it->second.callbacks);
    pending_.erase(it);

    std::vector<Callback> callbacks;
    callbacks.reserve(pending_callbacks.size());
    for (auto& pending : pending_callbacks) {
      if (pending.callback) {
        callbacks.push_back(std::move(pending.callback));
      }
    }
    return callbacks;
  }

  void Requeue(Key key) {
    auto it = pending_.find(key);
    if (it == pending_.end()) {
      return;
    }

    it->second.dispatched = false;
    if (!IsQueued(key)) {
      queued_keys_.push_back(key);
    }
  }

  void CancelCallbacks(CallbackKey key) {
    if (!key.IsValid()) {
      return;
    }

    for (auto& [pending_key, entry] : pending_) {
      (void)pending_key;
      entry.callbacks.erase(
          std::remove_if(entry.callbacks.begin(), entry.callbacks.end(),
                         [&key](const PendingCallback& pending) {
                           return pending.key == key;
                         }),
          entry.callbacks.end());
    }
  }

  void CancelCallback(Key entry_key, CallbackKey key) {
    if (entry_key == Key{} || !key.IsValid()) {
      return;
    }

    const auto pending_it = pending_.find(entry_key);
    if (pending_it == pending_.end()) {
      return;
    }

    auto& callbacks = pending_it->second.callbacks;
    callbacks.erase(
        std::remove_if(callbacks.begin(), callbacks.end(),
                       [&key](const PendingCallback& pending) {
                         return pending.key == key;
                       }),
        callbacks.end());
  }

  void Clear() {
    pending_.clear();
    queued_keys_.clear();
    dispatched_this_window_ = 0;
    next_window_reset_tick_ = 0;
  }

 private:
  struct PendingCallback {
    CallbackKey key;
    Callback callback;
  };

  struct PendingEntry {
    std::uint64_t context = 0;
    bool dispatched = false;
    std::vector<PendingCallback> callbacks;
  };

  void RefreshDispatchWindow(std::uint32_t current_tick_ms) {
    if (dispatch_budget_per_window_ == 0) {
      return;
    }
    if (next_window_reset_tick_ == 0 ||
        static_cast<std::int32_t>(current_tick_ms - next_window_reset_tick_) >= 0) {
      dispatched_this_window_ = 0;
      next_window_reset_tick_ = current_tick_ms + kDispatchWindowMs;
    }
  }

  void CountExternalDispatch(std::uint32_t current_tick_ms) {
    if (dispatch_budget_per_window_ == 0) {
      return;
    }
    RefreshDispatchWindow(current_tick_ms);
    if (dispatched_this_window_ < dispatch_budget_per_window_) {
      ++dispatched_this_window_;
    }
  }

  [[nodiscard]] bool IsQueued(Key key) const {
    return std::find(queued_keys_.begin(), queued_keys_.end(), key) !=
           queued_keys_.end();
  }

  bool TryDispatch(Key key, PendingEntry& entry,
                   std::uint32_t current_tick_ms) {
    if (entry.dispatched || !dispatcher_) {
      return false;
    }

    if (dispatch_budget_per_window_ != 0) {
      RefreshDispatchWindow(current_tick_ms);
      if (dispatched_this_window_ >= dispatch_budget_per_window_) {
        return false;
      }
      ++dispatched_this_window_;
    }

    entry.dispatched = true;
    dispatcher_(key, entry.context);
    return true;
  }

  void DispatchUndispatchedEntries(std::uint32_t current_tick_ms) {
    for (auto& [key, entry] : pending_) {
      if (entry.dispatched || IsQueued(key)) {
        continue;
      }
      if (!TryDispatch(key, entry, current_tick_ms)) {
        queued_keys_.push_back(key);
      }
    }
    Pump(current_tick_ms);
  }

  Dispatcher dispatcher_;
  std::unordered_map<Key, PendingEntry> pending_;
  std::deque<Key> queued_keys_;
  std::uint32_t dispatch_budget_per_window_ = 0;
  std::uint32_t dispatched_this_window_ = 0;
  std::uint32_t next_window_reset_tick_ = 0;
};

using AsyncQueryChannel = BasicAsyncQueryChannel<std::uint32_t>;
using AsyncGuidQueryChannel = BasicAsyncQueryChannel<std::uint64_t>;

}

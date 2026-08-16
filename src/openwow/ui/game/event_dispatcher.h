#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "openwow/ui/game/game_events.h"

struct lua_State;

namespace openwow::ui::game {

using EventArg = std::variant<std::monostate, std::string, int, double, bool>;

struct EventDispatchPhaseTelemetry {
  std::uint64_t signal_count{0};
  std::uint64_t listener_visits{0};
  std::uint64_t handlers_invoked{0};
  std::uint64_t listeners_skipped_for_removal{0};
  std::uint64_t total_duration_ns{0};
  std::uint64_t handler_duration_ns{0};
  std::uint64_t mutation_duration_ns{0};
  std::uint64_t max_duration_ns{0};
};

struct EventDispatcherPerformanceCounters {
  std::uint64_t signals{0};
  std::uint64_t listener_visits{0};
  std::uint64_t handlers_invoked{0};
  std::uint64_t listeners_skipped_for_removal{0};
  std::uint64_t deferred_additions{0};
  std::uint64_t deferred_removals{0};
  std::uint64_t nested_additions_consumed{0};
  std::uint64_t nested_removals_consumed{0};
  std::uint64_t frame_unregister_all_calls{0};
  std::uint64_t frame_unregister_event_visits{0};
  std::uint64_t membership_index_lookups{0};
  std::uint64_t profiled_listener_clock_samples{0};

  std::uint64_t full_listener_snapshots{0};
  std::uint64_t linear_membership_probe_steps{0};

  std::uint64_t total_dispatch_duration_ns{0};
  std::uint64_t total_handler_duration_ns{0};
  std::uint64_t total_mutation_duration_ns{0};
  std::uint64_t max_dispatch_duration_ns{0};
};

class EventDispatcher {
 public:
  EventDispatcher();
  ~EventDispatcher();

  EventDispatcher(const EventDispatcher&) = delete;
  EventDispatcher& operator=(const EventDispatcher&) = delete;

  void Initialize(lua_State* L);
  void Shutdown();

  void RegisterEvent(const std::string& event, int lua_frame_ref);
  void UnregisterEvent(const std::string& event, int lua_frame_ref);
  void UnregisterAllForFrame(int lua_frame_ref);

  [[nodiscard]] bool IsEventRegistered(const std::string& event,
                                       int lua_frame_ref) const;
  [[nodiscard]] std::vector<int> GetFramesRegisteredForEvent(
      const std::string& event) const;

  void FireEvent(const std::string& event);
  void FireEvent(const std::string& event, const std::string& arg1);
  void FireEvent(const std::string& event, const std::string& arg1,
                 const std::string& arg2);
  void FireEvent(const std::string& event, const std::string& arg1,
                 const std::string& arg2, const std::string& arg3);
  void FireEvent(const std::string& event, int arg1);
  void FireEvent(const std::string& event, int arg1, int arg2);
  void FireEventV(const std::string& event,
                  const std::vector<EventArg>& args);
  void FireEventArgs(const std::string& event,
                     std::initializer_list<EventArg> args);

  [[nodiscard]] std::size_t registered_event_count() const noexcept;
  [[nodiscard]] std::size_t total_listener_count() const noexcept;
  [[nodiscard]] lua_State* GetLuaState() const noexcept { return lua_; }

  [[nodiscard]] const EventDispatcherPerformanceCounters&
  performance_counters() const noexcept {
    return performance_counters_;
  }
  [[nodiscard]] std::optional<EventDispatchPhaseTelemetry>
  GetEventPhaseTelemetry(std::string_view event) const;
  void ResetPerformanceCounters() noexcept;

 private:
  class OrderedListenerSet {
   public:
    using SlotIndex = std::uint32_t;
    static constexpr SlotIndex kNoSlot = static_cast<SlotIndex>(-1);

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] SlotIndex first_slot() const noexcept { return head_; }
    [[nodiscard]] SlotIndex next_slot(SlotIndex slot) const noexcept;
    [[nodiscard]] int frame_ref_at(SlotIndex slot) const noexcept;

    [[nodiscard]] bool Contains(
        int lua_frame_ref,
        EventDispatcherPerformanceCounters& counters) const;
    bool AppendUnique(int lua_frame_ref,
                      EventDispatcherPerformanceCounters& counters);
    bool Erase(int lua_frame_ref,
               EventDispatcherPerformanceCounters& counters);
    [[nodiscard]] std::optional<int> PopFront();

   private:
    struct Slot {
      int lua_frame_ref{0};
      SlotIndex previous{kNoSlot};
      SlotIndex next{kNoSlot};
    };

    [[nodiscard]] SlotIndex AllocateSlot(int lua_frame_ref);
    void ReleaseSlot(SlotIndex slot) noexcept;

    std::vector<Slot> slots_;
    std::unordered_map<int, SlotIndex> slots_by_frame_;
    SlotIndex head_{kNoSlot};
    SlotIndex tail_{kNoSlot};
    SlotIndex free_head_{kNoSlot};
    std::size_t size_{0};
  };

  struct EventRegistrationState {
    OrderedListenerSet active_listeners;
    OrderedListenerSet pending_removals;
    OrderedListenerSet pending_additions;
    std::uint32_t dispatch_depth{0};
    EventDispatchPhaseTelemetry telemetry;
  };

  struct DispatchSample {
    std::uint64_t listener_visits{0};
    std::uint64_t handlers_invoked{0};
    std::uint64_t listeners_skipped_for_removal{0};
    std::uint64_t handler_duration_ns{0};
    std::uint64_t mutation_duration_ns{0};
    std::uint64_t total_duration_ns{0};
  };

  [[nodiscard]] EventRegistrationState& EnsureEventState(
      const std::string& event);
  bool AddActiveListener(const std::string& event,
                         EventRegistrationState& state, int lua_frame_ref);
  bool RemoveActiveListener(const std::string& event,
                            EventRegistrationState& state,
                            int lua_frame_ref);
  void ReconcileFrameEventAssociation(const std::string& event,
                                      const EventRegistrationState& state,
                                      int lua_frame_ref);
  void EraseFrameEventAssociation(const std::string& event,
                                  int lua_frame_ref);
  void DrainDeferredMutations(const std::string& event,
                              EventRegistrationState& state);

  void FireEventSpan(const std::string& event,
                     std::span<const EventArg> args);
  [[nodiscard]] bool DispatchToFrame(int lua_frame_ref,
                                     const std::string& event,
                                     std::span<const EventArg> args,
                                     bool profile_event_cpu);
  void RecordDispatchSample(EventRegistrationState& state,
                            const DispatchSample& sample);
  static void LogWorldEntryPhase(const std::string& event,
                                 const DispatchSample& sample);

  lua_State* lua_{nullptr};

  std::unordered_map<std::string, std::unique_ptr<EventRegistrationState>>
      event_states_;

  std::unordered_map<int, std::unordered_set<std::string>>
      event_associations_by_frame_;

  std::size_t active_event_count_{0};
  std::size_t active_listener_count_{0};
  mutable EventDispatcherPerformanceCounters performance_counters_;
};

}

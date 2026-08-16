#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/game/event_dispatcher.h"

#include "openwow/ui/lua_taint_api.h"

#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/lua_cpu_profiler.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>

namespace openwow::ui::game {
namespace {

using DispatchClock = std::chrono::steady_clock;

std::uint64_t ElapsedNanoseconds(const DispatchClock::time_point start,
                                 const DispatchClock::time_point end) {
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  return elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0;
}

void PushEventArgument(lua_State* L, const EventArg& arg) {
  if (const auto* string_value = std::get_if<std::string>(&arg); string_value != nullptr) {
    lua_pushlstring(L, string_value->data(), string_value->size());
  } else if (const auto* int_value = std::get_if<int>(&arg); int_value != nullptr) {
    lua_pushinteger(L, *int_value);
  } else if (const auto* double_value = std::get_if<double>(&arg); double_value != nullptr) {
    lua_pushnumber(L, *double_value);
  } else if (const auto* bool_value = std::get_if<bool>(&arg); bool_value != nullptr) {
    lua_pushboolean(L, *bool_value);
  } else {
    lua_pushnil(L);
  }
}

bool IsWorldEntryPhaseEvent(const std::string_view event) {
  return event == events::VARIABLES_LOADED || event == events::PLAYER_LOGIN ||
         event == events::PLAYER_ENTERING_WORLD;
}

}

EventDispatcher::OrderedListenerSet::SlotIndex
EventDispatcher::OrderedListenerSet::next_slot(const SlotIndex slot) const
    noexcept {
  return slots_[slot].next;
}

int EventDispatcher::OrderedListenerSet::frame_ref_at(
    const SlotIndex slot) const noexcept {
  return slots_[slot].lua_frame_ref;
}

bool EventDispatcher::OrderedListenerSet::Contains(
    const int lua_frame_ref,
    EventDispatcherPerformanceCounters& counters) const {
  ++counters.membership_index_lookups;
  return slots_by_frame_.contains(lua_frame_ref);
}

EventDispatcher::OrderedListenerSet::SlotIndex
EventDispatcher::OrderedListenerSet::AllocateSlot(const int lua_frame_ref) {
  SlotIndex slot = kNoSlot;
  if (free_head_ != kNoSlot) {
    slot = free_head_;
    free_head_ = slots_[slot].next;
    slots_[slot] = Slot{.lua_frame_ref = lua_frame_ref};
  } else {
    if (slots_.size() >= static_cast<std::size_t>(kNoSlot)) {
      throw std::length_error("EventDispatcher listener slot limit exceeded");
    }
    slot = static_cast<SlotIndex>(slots_.size());
    slots_.push_back(Slot{.lua_frame_ref = lua_frame_ref});
  }
  return slot;
}

bool EventDispatcher::OrderedListenerSet::AppendUnique(
    const int lua_frame_ref, EventDispatcherPerformanceCounters& counters) {
  ++counters.membership_index_lookups;
  auto [indexed_slot, inserted] =
      slots_by_frame_.try_emplace(lua_frame_ref, kNoSlot);
  if (!inserted) {
    return false;
  }

  SlotIndex slot = kNoSlot;
  try {
    slot = AllocateSlot(lua_frame_ref);
  } catch (...) {
    slots_by_frame_.erase(indexed_slot);
    throw;
  }
  indexed_slot->second = slot;
  Slot& node = slots_[slot];
  node.previous = tail_;
  node.next = kNoSlot;
  if (tail_ != kNoSlot) {
    slots_[tail_].next = slot;
  } else {
    head_ = slot;
  }
  tail_ = slot;
  ++size_;
  return true;
}

void EventDispatcher::OrderedListenerSet::ReleaseSlot(
    const SlotIndex slot) noexcept {
  Slot& node = slots_[slot];
  if (node.previous != kNoSlot) {
    slots_[node.previous].next = node.next;
  } else {
    head_ = node.next;
  }
  if (node.next != kNoSlot) {
    slots_[node.next].previous = node.previous;
  } else {
    tail_ = node.previous;
  }

  node.previous = kNoSlot;
  node.next = free_head_;
  free_head_ = slot;
  --size_;
}

bool EventDispatcher::OrderedListenerSet::Erase(
    const int lua_frame_ref, EventDispatcherPerformanceCounters& counters) {
  ++counters.membership_index_lookups;
  const auto found = slots_by_frame_.find(lua_frame_ref);
  if (found == slots_by_frame_.end()) {
    return false;
  }
  const SlotIndex slot = found->second;
  slots_by_frame_.erase(found);
  ReleaseSlot(slot);
  return true;
}

std::optional<int> EventDispatcher::OrderedListenerSet::PopFront() {
  if (head_ == kNoSlot) {
    return std::nullopt;
  }
  const SlotIndex slot = head_;
  const int lua_frame_ref = slots_[slot].lua_frame_ref;
  slots_by_frame_.erase(lua_frame_ref);
  ReleaseSlot(slot);
  return lua_frame_ref;
}

EventDispatcher::EventDispatcher() = default;

EventDispatcher::~EventDispatcher() { Shutdown(); }

void EventDispatcher::Initialize(lua_State* L) {
  lua_ = L;
  event_states_.clear();
  event_associations_by_frame_.clear();
  active_event_count_ = 0;
  active_listener_count_ = 0;
  performance_counters_ = {};
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "EventDispatcher: initialized");
}

void EventDispatcher::Shutdown() {
  event_states_.clear();
  event_associations_by_frame_.clear();
  active_event_count_ = 0;
  active_listener_count_ = 0;
  performance_counters_ = {};
  lua_ = nullptr;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "EventDispatcher: shutdown");
}

EventDispatcher::EventRegistrationState& EventDispatcher::EnsureEventState(
    const std::string& event) {
  auto [found, inserted] = event_states_.try_emplace(event);
  if (inserted || found->second == nullptr) {
    found->second = std::make_unique<EventRegistrationState>();
  }
  return *found->second;
}

bool EventDispatcher::AddActiveListener(const std::string& event,
                                        EventRegistrationState& state,
                                        const int lua_frame_ref) {
  const bool was_empty = state.active_listeners.empty();
  if (!state.active_listeners.AppendUnique(lua_frame_ref,
                                            performance_counters_)) {
    return false;
  }
  if (was_empty) {
    ++active_event_count_;
  }
  ++active_listener_count_;
  event_associations_by_frame_[lua_frame_ref].insert(event);
  return true;
}

bool EventDispatcher::RemoveActiveListener(const std::string& event,
                                           EventRegistrationState& state,
                                           const int lua_frame_ref) {
  if (!state.active_listeners.Erase(lua_frame_ref, performance_counters_)) {
    if (state.pending_removals.empty() && state.pending_additions.empty()) {
      EraseFrameEventAssociation(event, lua_frame_ref);
    } else {
      ReconcileFrameEventAssociation(event, state, lua_frame_ref);
    }
    return false;
  }

  --active_listener_count_;
  if (state.active_listeners.empty()) {
    --active_event_count_;
  }
  if (state.pending_removals.empty() && state.pending_additions.empty()) {
    EraseFrameEventAssociation(event, lua_frame_ref);
  } else {
    ReconcileFrameEventAssociation(event, state, lua_frame_ref);
  }
  return true;
}

void EventDispatcher::ReconcileFrameEventAssociation(
    const std::string& event, const EventRegistrationState& state,
    const int lua_frame_ref) {
  const bool associated =
      state.active_listeners.Contains(lua_frame_ref, performance_counters_) ||
      state.pending_removals.Contains(lua_frame_ref, performance_counters_) ||
      state.pending_additions.Contains(lua_frame_ref, performance_counters_);
  if (associated) {
    event_associations_by_frame_[lua_frame_ref].insert(event);
    return;
  }

  EraseFrameEventAssociation(event, lua_frame_ref);
}

void EventDispatcher::EraseFrameEventAssociation(const std::string& event,
                                                 const int lua_frame_ref) {
  const auto frame = event_associations_by_frame_.find(lua_frame_ref);
  if (frame != event_associations_by_frame_.end()) {
    frame->second.erase(event);
    if (frame->second.empty()) {
      event_associations_by_frame_.erase(frame);
    }
  }
}

void EventDispatcher::RegisterEvent(const std::string& event,
                                    const int lua_frame_ref) {
  if (lua_ == nullptr || event.empty() || lua_frame_ref == LUA_NOREF) {
    return;
  }

  EventRegistrationState& state = EnsureEventState(event);
  if (state.dispatch_depth == 0) {
    AddActiveListener(event, state, lua_frame_ref);
    return;
  }

  state.pending_removals.Erase(lua_frame_ref, performance_counters_);
  if (state.active_listeners.Contains(lua_frame_ref, performance_counters_)) {
    event_associations_by_frame_[lua_frame_ref].insert(event);
    return;
  }
  if (state.pending_additions.AppendUnique(lua_frame_ref,
                                            performance_counters_)) {
    ++performance_counters_.deferred_additions;
  }
  event_associations_by_frame_[lua_frame_ref].insert(event);
}

void EventDispatcher::UnregisterEvent(const std::string& event,
                                      const int lua_frame_ref) {
  if (lua_ == nullptr || event.empty() || lua_frame_ref == LUA_NOREF) {
    return;
  }

  const auto found = event_states_.find(event);
  if (found == event_states_.end()) {
    return;
  }
  EventRegistrationState& state = *found->second;
  if (state.dispatch_depth == 0) {
    RemoveActiveListener(event, state, lua_frame_ref);
    return;
  }

  state.pending_additions.Erase(lua_frame_ref, performance_counters_);
  if (state.pending_removals.AppendUnique(lua_frame_ref,
                                           performance_counters_)) {
    ++performance_counters_.deferred_removals;
  }
  event_associations_by_frame_[lua_frame_ref].insert(event);
}

void EventDispatcher::UnregisterAllForFrame(const int lua_frame_ref) {
  if (lua_ == nullptr || lua_frame_ref == LUA_NOREF) {
    return;
  }

  ++performance_counters_.frame_unregister_all_calls;
  const auto frame = event_associations_by_frame_.find(lua_frame_ref);
  if (frame == event_associations_by_frame_.end()) {
    return;
  }

  std::vector<std::string> associated_events;
  associated_events.reserve(frame->second.size());
  for (const std::string& event : frame->second) {
    associated_events.push_back(event);
  }
  performance_counters_.frame_unregister_event_visits +=
      associated_events.size();
  for (const std::string& event : associated_events) {
    UnregisterEvent(event, lua_frame_ref);
  }
}

bool EventDispatcher::IsEventRegistered(const std::string& event,
                                        const int lua_frame_ref) const {
  if (event.empty() || lua_frame_ref == LUA_NOREF) {
    return false;
  }
  const auto found = event_states_.find(event);
  return found != event_states_.end() &&
         found->second->active_listeners.Contains(lua_frame_ref,
                                                   performance_counters_);
}

std::vector<int> EventDispatcher::GetFramesRegisteredForEvent(
    const std::string& event) const {
  const EventRegistrationState* state = nullptr;
  if (const auto found = event_states_.find(event); found != event_states_.end()) {
    state = found->second.get();
  } else {

    for (const auto& [registered_event, registered_state] : event_states_) {
      if (openwow::text::EqualsIgnoreCaseAscii(registered_event, event)) {
        state = registered_state.get();
        break;
      }
    }
  }
  if (state == nullptr) {
    return {};
  }

  const OrderedListenerSet& listeners = state->active_listeners;
  std::vector<int> result;
  result.reserve(listeners.size());
  for (auto slot = listeners.first_slot();
       slot != OrderedListenerSet::kNoSlot; slot = listeners.next_slot(slot)) {
    result.push_back(listeners.frame_ref_at(slot));
  }
  return result;
}

void EventDispatcher::FireEvent(const std::string& event) {
  FireEventSpan(event, {});
}

void EventDispatcher::FireEvent(const std::string& event,
                                const std::string& arg1) {
  const std::array<EventArg, 1> args{EventArg{arg1}};
  FireEventSpan(event, args);
}

void EventDispatcher::FireEvent(const std::string& event,
                                const std::string& arg1,
                                const std::string& arg2) {
  const std::array<EventArg, 2> args{EventArg{arg1}, EventArg{arg2}};
  FireEventSpan(event, args);
}

void EventDispatcher::FireEvent(const std::string& event,
                                const std::string& arg1,
                                const std::string& arg2,
                                const std::string& arg3) {
  const std::array<EventArg, 3> args{EventArg{arg1}, EventArg{arg2},
                                     EventArg{arg3}};
  FireEventSpan(event, args);
}

void EventDispatcher::FireEvent(const std::string& event, const int arg1) {
  const std::array<EventArg, 1> args{EventArg{arg1}};
  FireEventSpan(event, args);
}

void EventDispatcher::FireEvent(const std::string& event, const int arg1,
                                const int arg2) {
  const std::array<EventArg, 2> args{EventArg{arg1}, EventArg{arg2}};
  FireEventSpan(event, args);
}

void EventDispatcher::FireEventV(const std::string& event,
                                 const std::vector<EventArg>& args) {
  FireEventSpan(event, args);
}

void EventDispatcher::FireEventArgs(
    const std::string& event, const std::initializer_list<EventArg> args) {
  FireEventSpan(event, std::span<const EventArg>(args.begin(), args.size()));
}

void EventDispatcher::DrainDeferredMutations(
    const std::string& event, EventRegistrationState& state) {

  if (state.dispatch_depth != 0) {
    while (const auto ref = state.pending_removals.PopFront()) {
      ++performance_counters_.nested_removals_consumed;
      ReconcileFrameEventAssociation(event, state, *ref);
    }
    while (const auto ref = state.pending_additions.PopFront()) {
      ++performance_counters_.nested_additions_consumed;
      ReconcileFrameEventAssociation(event, state, *ref);
    }
    return;
  }

  while (const auto ref = state.pending_removals.PopFront()) {
    RemoveActiveListener(event, state, *ref);
  }
  while (const auto ref = state.pending_additions.PopFront()) {
    AddActiveListener(event, state, *ref);
  }
}

void EventDispatcher::FireEventSpan(const std::string& event,
                                    const std::span<const EventArg> args) {
  if (lua_ == nullptr) {
    return;
  }
  const auto found = event_states_.find(event);
  if (found == event_states_.end() ||
      found->second->active_listeners.empty()) {
    return;
  }

  EventRegistrationState& state = *found->second;
  const bool profile_event_cpu = IsLuaCpuProfilerEnabled(lua_);
  DispatchSample sample;
  const auto dispatch_start = DispatchClock::now();
  ++state.dispatch_depth;

  const OrderedListenerSet& listeners = state.active_listeners;
  for (auto slot = listeners.first_slot();
       slot != OrderedListenerSet::kNoSlot;) {
    const auto next = listeners.next_slot(slot);
    const int lua_frame_ref = listeners.frame_ref_at(slot);
    ++sample.listener_visits;

    const bool removed =
        !state.pending_removals.empty() &&
        state.pending_removals.Contains(lua_frame_ref, performance_counters_);
    if (removed) {
      ++sample.listeners_skipped_for_removal;
    } else {
      const bool invoked =
          DispatchToFrame(lua_frame_ref, event, args, profile_event_cpu);
      if (invoked) {
        ++sample.handlers_invoked;
      }
    }
    slot = next;
  }
  const auto handler_loop_end = DispatchClock::now();
  sample.handler_duration_ns =
      ElapsedNanoseconds(dispatch_start, handler_loop_end);

  --state.dispatch_depth;
  DrainDeferredMutations(event, state);
  const auto dispatch_end = DispatchClock::now();
  sample.mutation_duration_ns =
      ElapsedNanoseconds(handler_loop_end, dispatch_end);
  sample.total_duration_ns =
      ElapsedNanoseconds(dispatch_start, dispatch_end);
  RecordDispatchSample(state, sample);
  LogWorldEntryPhase(event, sample);
}

bool EventDispatcher::DispatchToFrame(
    const int lua_frame_ref, const std::string& event,
    const std::span<const EventArg> args, const bool profile_event_cpu) {
  if (lua_ == nullptr) {
    return false;
  }

  const auto event_start_time = profile_event_cpu
                                    ? DispatchClock::now()
                                    : DispatchClock::time_point{};
  if (profile_event_cpu) {
    ++performance_counters_.profiled_listener_clock_samples;
  }
  const int top = lua_gettop(lua_);
  constexpr std::size_t kMaxEventPayloadArguments =
      static_cast<std::size_t>(std::numeric_limits<int>::max()) - 2u;
  if (args.size() > kMaxEventPayloadArguments ||
      lua_checkstack(lua_, static_cast<int>(args.size() + 2u)) == 0) {
    return false;
  }

  {
    const openwow::ui::ScopedNeutralLuaExecutionTaint neutral_payload(lua_);
    lua_rawgeti(lua_, LUA_REGISTRYINDEX, lua_frame_ref);
    if (lua_istable(lua_, -1) == 0) {
      lua_settop(lua_, top);
      return false;
    }

    lua_pushlstring(lua_, event.data(), event.size());
    for (const EventArg& arg : args) {
      PushEventArgument(lua_, arg);
    }
  }
  const int frame_index = top + 1;
  const auto invocation = InvokeFrameScriptHandler(
      lua_, frame_index, "OnEvent", 1 + static_cast<int>(args.size()),
      FrameScriptInvocationKind::kEvent);
  if (!invocation.invoked) {
    lua_settop(lua_, top);
    return false;
  }
  if (invocation.status != LUA_OK) {
    const char* error = lua_tostring(lua_, -1);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "EventDispatcher: error in OnEvent[" + event + "]: " +
                           (error != nullptr ? error : "(null)"));
    lua_pop(lua_, 1);
  }

  if (profile_event_cpu) {
    const double elapsed_seconds =
        std::chrono::duration<double>(DispatchClock::now() - event_start_time)
            .count();
    RecordLuaEventCpuUsage(lua_, event, elapsed_seconds);
  }

  lua_settop(lua_, top);
  return true;
}

void EventDispatcher::RecordDispatchSample(EventRegistrationState& state,
                                           const DispatchSample& sample) {
  ++state.telemetry.signal_count;
  state.telemetry.listener_visits += sample.listener_visits;
  state.telemetry.handlers_invoked += sample.handlers_invoked;
  state.telemetry.listeners_skipped_for_removal +=
      sample.listeners_skipped_for_removal;
  state.telemetry.total_duration_ns += sample.total_duration_ns;
  state.telemetry.handler_duration_ns += sample.handler_duration_ns;
  state.telemetry.mutation_duration_ns += sample.mutation_duration_ns;
  state.telemetry.max_duration_ns =
      std::max(state.telemetry.max_duration_ns, sample.total_duration_ns);

  ++performance_counters_.signals;
  performance_counters_.listener_visits += sample.listener_visits;
  performance_counters_.handlers_invoked += sample.handlers_invoked;
  performance_counters_.listeners_skipped_for_removal +=
      sample.listeners_skipped_for_removal;
  performance_counters_.total_dispatch_duration_ns +=
      sample.total_duration_ns;
  performance_counters_.total_handler_duration_ns +=
      sample.handler_duration_ns;
  performance_counters_.total_mutation_duration_ns +=
      sample.mutation_duration_ns;
  performance_counters_.max_dispatch_duration_ns =
      std::max(performance_counters_.max_dispatch_duration_ns,
               sample.total_duration_ns);
}

void EventDispatcher::LogWorldEntryPhase(const std::string& event,
                                         const DispatchSample& sample) {
  if (!IsWorldEntryPhaseEvent(event)) {
    return;
  }
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "EventDispatcher phase: event=" + event +
          " total_us=" + std::to_string(sample.total_duration_ns / 1000u) +
          " handler_us=" +
          std::to_string(sample.handler_duration_ns / 1000u) +
          " mutation_us=" +
          std::to_string(sample.mutation_duration_ns / 1000u) +
          " listeners=" + std::to_string(sample.listener_visits) +
          " invoked=" + std::to_string(sample.handlers_invoked) +
          " skipped=" +
          std::to_string(sample.listeners_skipped_for_removal));
}

std::size_t EventDispatcher::registered_event_count() const noexcept {
  return active_event_count_;
}

std::size_t EventDispatcher::total_listener_count() const noexcept {
  return active_listener_count_;
}

std::optional<EventDispatchPhaseTelemetry>
EventDispatcher::GetEventPhaseTelemetry(const std::string_view event) const {
  const auto found = event_states_.find(std::string(event));
  if (found == event_states_.end()) {
    return std::nullopt;
  }
  return found->second->telemetry;
}

void EventDispatcher::ResetPerformanceCounters() noexcept {
  performance_counters_ = {};
  for (auto& [event, state] : event_states_) {
    (void)event;
    state->telemetry = {};
  }
}

}

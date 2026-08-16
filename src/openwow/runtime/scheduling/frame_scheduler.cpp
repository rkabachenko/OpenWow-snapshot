
#include "openwow/runtime/scheduling/frame_scheduler.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <string>

namespace openwow::core {

FrameScheduler& FrameScheduler::Instance() {
    static FrameScheduler instance;
    return instance;
}

FrameScheduler::FrameScheduler()  = default;
FrameScheduler::~FrameScheduler() = default;

CallbackHandle FrameScheduler::Register(Phase phase, int priority,
                                        FrameCallback callback,
                                        std::string_view debug_name) {
    if (!callback) {
        return CallbackHandle::Invalid;
    }

    const auto raw = next_handle_.fetch_add(1, std::memory_order_relaxed);
    const auto handle = static_cast<CallbackHandle>(raw);

    Entry entry{handle, priority, std::move(callback), std::string(debug_name)};

    std::lock_guard lock(mutex_);

    if (running_frame_) {

        pending_adds_.emplace_back(phase, std::move(entry));
    } else {
        auto idx = static_cast<std::size_t>(phase);
        auto& pd = phases_[idx];
        pd.entries.push_back(std::move(entry));
        pd.dirty = true;
    }

    return handle;
}

bool FrameScheduler::Unregister(CallbackHandle handle) {
    if (handle == CallbackHandle::Invalid) return false;

    std::lock_guard lock(mutex_);

    if (running_frame_) {

        pending_removes_.push_back(handle);
        return true;
    }

    for (auto& pd : phases_) {
        auto& entries = pd.entries;
        auto it = std::find_if(entries.begin(), entries.end(),
                               [handle](const Entry& e) { return e.handle == handle; });
        if (it != entries.end()) {
            entries.erase(it);
            return true;
        }
    }

    return false;
}

bool FrameScheduler::IsRegistered(CallbackHandle handle) const {
    if (handle == CallbackHandle::Invalid) return false;

    std::lock_guard lock(mutex_);

    for (const auto& pd : phases_) {
        for (const auto& e : pd.entries) {
            if (e.handle == handle) return true;
        }
    }

    for (const auto& [_, entry] : pending_adds_) {
        if (entry.handle == handle) return true;
    }
    return false;
}

void FrameScheduler::RunFrame(double delta_sec) {

    ApplyPending();

    running_frame_ = true;

    for (std::size_t i = 0; i < kPhaseCount; ++i) {
        auto& pd = phases_[i];

        if (pd.dirty) {
            SortPhase(pd);
        }

        for (const auto& entry : pd.entries) {
            entry.callback(delta_sec);
        }
    }

    running_frame_ = false;

    frame_count_.fetch_add(1, std::memory_order_relaxed);

    ApplyPending();
}

void FrameScheduler::RunPhase(Phase phase, double delta_sec) {
    ApplyPending();

    running_frame_ = true;

    auto idx = static_cast<std::size_t>(phase);
    auto& pd = phases_[idx];
    if (pd.dirty) {
        SortPhase(pd);
    }
    for (const auto& entry : pd.entries) {
        entry.callback(delta_sec);
    }

    running_frame_ = false;
    ApplyPending();
}

std::size_t FrameScheduler::GetCallbackCount(Phase phase) const {
    std::lock_guard lock(mutex_);
    return phases_[static_cast<std::size_t>(phase)].entries.size();
}

std::size_t FrameScheduler::GetTotalCallbackCount() const {
    std::lock_guard lock(mutex_);
    std::size_t total = 0;
    for (const auto& pd : phases_) {
        total += pd.entries.size();
    }
    return total;
}

std::uint64_t FrameScheduler::GetFrameCount() const noexcept {
    return frame_count_.load(std::memory_order_relaxed);
}

void FrameScheduler::Clear() {
    std::lock_guard lock(mutex_);
    for (auto& pd : phases_) {
        pd.entries.clear();
        pd.dirty = false;
    }
    pending_adds_.clear();
    pending_removes_.clear();
    frame_count_.store(0, std::memory_order_relaxed);
}

void FrameScheduler::ApplyPending() {
    std::lock_guard lock(mutex_);

    if (!pending_removes_.empty()) {
        for (auto handle : pending_removes_) {
            for (auto& pd : phases_) {
                auto& entries = pd.entries;
                auto it = std::find_if(
                    entries.begin(), entries.end(),
                    [handle](const Entry& e) { return e.handle == handle; });
                if (it != entries.end()) {
                    entries.erase(it);
                    break;
                }
            }
        }
        pending_removes_.clear();
    }

    if (!pending_adds_.empty()) {
        for (auto& [phase, entry] : pending_adds_) {
            auto idx = static_cast<std::size_t>(phase);
            auto& pd = phases_[idx];
            pd.entries.push_back(std::move(entry));
            pd.dirty = true;
        }
        pending_adds_.clear();
    }
}

void FrameScheduler::SortPhase(PhaseData& pd) {

    std::stable_sort(pd.entries.begin(), pd.entries.end(),
                     [](const Entry& a, const Entry& b) {
                         if (a.priority != b.priority) return a.priority < b.priority;
                         return static_cast<std::uint64_t>(a.handle) <
                                static_cast<std::uint64_t>(b.handle);
                     });
    pd.dirty = false;
}

}

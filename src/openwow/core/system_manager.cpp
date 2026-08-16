#include "openwow/core/system_manager.h"

#include <algorithm>
#include <chrono>

namespace openwow::core {

bool SystemManager::RegisterSystem(const std::string& name, SystemUpdateFn fn,
                                   SystemPriority priority) {
    if (FindEntry(name) != nullptr) return false;

    SystemEntry entry;
    entry.info.name = name;
    entry.info.priority = priority;
    entry.info.enabled = true;
    entry.info.lastUpdateTimeNs = 0;
    entry.info.avgUpdateMs = 0.0f;
    entry.fn = std::move(fn);
    systems_.push_back(std::move(entry));
    sorted_ = false;
    return true;
}

bool SystemManager::UnregisterSystem(const std::string& name) {
    auto it = std::find_if(systems_.begin(), systems_.end(),
                           [&](const SystemEntry& e) { return e.info.name == name; });
    if (it == systems_.end()) return false;
    systems_.erase(it);
    return true;
}

void SystemManager::EnableSystem(const std::string& name, bool enabled) {
    if (auto* e = FindEntry(name)) e->info.enabled = enabled;
}

bool SystemManager::IsEnabled(const std::string& name) const {
    if (const auto* e = FindEntry(name)) return e->info.enabled;
    return false;
}

void SystemManager::UpdateAll(float dt) {
    EnsureSorted();

    float totalMs = 0.0f;
    for (auto& entry : systems_) {
        if (!entry.info.enabled) continue;

        auto start = std::chrono::steady_clock::now();
        entry.fn(dt);
        auto end = std::chrono::steady_clock::now();

        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        entry.info.lastUpdateTimeNs = static_cast<std::uint64_t>(ns);
        float ms = static_cast<float>(ns) / 1'000'000.0f;

        constexpr float kAlpha = 0.1f;
        entry.info.avgUpdateMs = entry.info.avgUpdateMs + kAlpha * (ms - entry.info.avgUpdateMs);
        totalMs += ms;
    }
    totalUpdateTimeMs_ = totalMs;
}

std::optional<SystemInfo> SystemManager::GetSystemInfo(const std::string& name) const {
    if (const auto* e = FindEntry(name)) return e->info;
    return std::nullopt;
}

std::vector<SystemInfo> SystemManager::GetAllSystems() const {
    EnsureSorted();
    std::vector<SystemInfo> result;
    result.reserve(systems_.size());
    for (const auto& e : systems_) result.push_back(e.info);
    return result;
}

std::size_t SystemManager::GetSystemCount() const { return systems_.size(); }

float SystemManager::GetTotalUpdateTimeMs() const { return totalUpdateTimeMs_; }

void SystemManager::ClearAll() {
    systems_.clear();
    totalUpdateTimeMs_ = 0.0f;
    sorted_ = true;
}

void SystemManager::EnsureSorted() const {
    if (sorted_) return;
    std::stable_sort(systems_.begin(), systems_.end(),
                     [](const SystemEntry& a, const SystemEntry& b) {
                         return a.info.priority < b.info.priority;
                     });
    sorted_ = true;
}

SystemManager::SystemEntry* SystemManager::FindEntry(const std::string& name) {
    auto it = std::find_if(systems_.begin(), systems_.end(),
                           [&](const SystemEntry& e) { return e.info.name == name; });
    return (it != systems_.end()) ? &*it : nullptr;
}

const SystemManager::SystemEntry* SystemManager::FindEntry(const std::string& name) const {
    auto it = std::find_if(systems_.begin(), systems_.end(),
                           [&](const SystemEntry& e) { return e.info.name == name; });
    return (it != systems_.end()) ? &*it : nullptr;
}

}

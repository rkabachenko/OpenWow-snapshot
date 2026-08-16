
#include "openwow/ui/addon_profiler.h"

#include <algorithm>

namespace openwow::ui {

ScopedCall::ScopedCall(AddonProfiler& profiler, const std::string& addon_name)
    : profiler_(profiler), addon_name_(addon_name) {
  profiler_.BeginCall(addon_name_);
}

ScopedCall::~ScopedCall() { profiler_.EndCall(addon_name_); }

void AddonProfiler::SetEnabled(bool enabled) { enabled_ = enabled; }

bool AddonProfiler::IsEnabled() const { return enabled_; }

void AddonProfiler::BeginCall(const std::string& addon_name) {
  if (!enabled_) return;

  auto& ip = profiles_[addon_name];
  if (ip.profile.addon_name.empty()) {
    ip.profile.addon_name = addon_name;
  }
  ip.call_start = std::chrono::steady_clock::now();
  ip.timing_active = true;
}

void AddonProfiler::EndCall(const std::string& addon_name) {
  if (!enabled_) return;

  auto it = profiles_.find(addon_name);
  if (it == profiles_.end() || !it->second.timing_active) return;

  auto& ip = it->second;
  auto elapsed = std::chrono::steady_clock::now() - ip.call_start;
  float elapsed_ms =
      std::chrono::duration<float, std::milli>(elapsed).count();

  ip.profile.cpu_time_ms += elapsed_ms;
  ip.profile.call_count++;
  ip.timing_active = false;
}

std::optional<AddonProfile> AddonProfiler::GetProfile(
    const std::string& addon_name) const {
  auto it = profiles_.find(addon_name);
  if (it == profiles_.end()) return std::nullopt;
  return it->second.profile;
}

std::vector<AddonProfile> AddonProfiler::GetAllProfiles() const {
  std::vector<AddonProfile> result;
  result.reserve(profiles_.size());
  for (const auto& [name, ip] : profiles_) {
    result.push_back(ip.profile);
  }
  return result;
}

std::vector<AddonProfile> AddonProfiler::GetTopByTime(uint32_t n) const {
  auto all = GetAllProfiles();
  std::sort(all.begin(), all.end(),
            [](const AddonProfile& a, const AddonProfile& b) {
              return a.cpu_time_ms > b.cpu_time_ms;
            });
  if (all.size() > n) all.resize(n);
  return all;
}

std::vector<AddonProfile> AddonProfiler::GetTopByMemory(uint32_t n) const {
  auto all = GetAllProfiles();
  std::sort(all.begin(), all.end(),
            [](const AddonProfile& a, const AddonProfile& b) {
              return a.current_memory_kb > b.current_memory_kb;
            });
  if (all.size() > n) all.resize(n);
  return all;
}

float AddonProfiler::GetTotalCPUTime() const {
  float total = 0.0F;
  for (const auto& [name, ip] : profiles_) {
    total += ip.profile.cpu_time_ms;
  }
  return total;
}

uint64_t AddonProfiler::GetTotalCallCount() const {
  uint64_t total = 0;
  for (const auto& [name, ip] : profiles_) {
    total += ip.profile.call_count;
  }
  return total;
}

uint32_t AddonProfiler::GetAddonCount() const {
  return static_cast<uint32_t>(profiles_.size());
}

void AddonProfiler::ForEach(
    const std::function<void(const AddonProfile&)>& callback) const {
  for (const auto& [name, ip] : profiles_) {
    callback(ip.profile);
  }
}

void AddonProfiler::SetMemory(const std::string& addon_name, float current_kb) {
  auto& ip = profiles_[addon_name];
  if (ip.profile.addon_name.empty()) {
    ip.profile.addon_name = addon_name;
  }
  ip.profile.current_memory_kb = current_kb;
  if (current_kb > ip.profile.peak_memory_kb) {
    ip.profile.peak_memory_kb = current_kb;
  }
}

void AddonProfiler::ResetCounters() {
  for (auto& [name, ip] : profiles_) {
    ip.profile.cpu_time_ms = 0.0F;
    ip.profile.call_count = 0;
    ip.timing_active = false;
  }
}

void AddonProfiler::Reset() {
  profiles_.clear();
  enabled_ = false;
}

}

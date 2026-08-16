#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui {

struct AddonProfile {
  std::string addon_name;
  float cpu_time_ms{0.0F};
  uint64_t call_count{0};
  float peak_memory_kb{0.0F};
  float current_memory_kb{0.0F};

  [[nodiscard]] float AvgCallTimeMs() const {
    if (call_count == 0) return 0.0F;
    return cpu_time_ms / static_cast<float>(call_count);
  }
};

class AddonProfiler;

class ScopedCall {
 public:
  ScopedCall(AddonProfiler& profiler, const std::string& addon_name);
  ~ScopedCall();

  ScopedCall(const ScopedCall&) = delete;
  ScopedCall& operator=(const ScopedCall&) = delete;

 private:
  AddonProfiler& profiler_;
  std::string addon_name_;
};

class AddonProfiler {
 public:

  void SetEnabled(bool enabled);
  [[nodiscard]] bool IsEnabled() const;

  void BeginCall(const std::string& addon_name);

  void EndCall(const std::string& addon_name);

  [[nodiscard]] std::optional<AddonProfile> GetProfile(
      const std::string& addon_name) const;

  [[nodiscard]] std::vector<AddonProfile> GetAllProfiles() const;

  [[nodiscard]] std::vector<AddonProfile> GetTopByTime(uint32_t n) const;

  [[nodiscard]] std::vector<AddonProfile> GetTopByMemory(uint32_t n) const;

  [[nodiscard]] float GetTotalCPUTime() const;

  [[nodiscard]] uint64_t GetTotalCallCount() const;

  [[nodiscard]] uint32_t GetAddonCount() const;

  void ForEach(const std::function<void(const AddonProfile&)>& callback) const;

  void SetMemory(const std::string& addon_name, float current_kb);

  void ResetCounters();

  void Reset();

 private:
  struct InternalProfile {
    AddonProfile profile;

    std::chrono::steady_clock::time_point call_start{};
    bool timing_active{false};
  };

  bool enabled_{false};
  std::unordered_map<std::string, InternalProfile> profiles_;
};

}

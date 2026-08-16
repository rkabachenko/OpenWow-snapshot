#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

enum class LatencyBand : std::uint8_t {
  Good,
  Fair,
  Poor,
  Critical,
};

struct LatencySnapshot {
  std::uint32_t worldMs = 0;
  std::uint32_t homeMs = 0;
  float timestamp = 0.0f;
};

class LatencyMonitor {
 public:
  LatencyMonitor() = default;

  void SetWorldLatency(std::uint32_t ms);
  [[nodiscard]] std::uint32_t GetWorldLatency() const;

  void SetHomeLatency(std::uint32_t ms);
  [[nodiscard]] std::uint32_t GetHomeLatency() const;

  [[nodiscard]] static LatencyBand GetBand(std::uint32_t ms);
  [[nodiscard]] LatencyBand GetWorldBand() const;
  [[nodiscard]] LatencyBand GetHomeBand() const;

  [[nodiscard]] static std::uint32_t GetBandColor(LatencyBand band);
  [[nodiscard]] static std::string GetBandName(LatencyBand band);

  [[nodiscard]] std::uint32_t GetAverageLatency(std::uint32_t samples = 10) const;
  [[nodiscard]] std::uint32_t GetPeakLatency() const;
  [[nodiscard]] std::uint32_t GetMinLatency() const;

  void AddSample(std::uint32_t worldMs, std::uint32_t homeMs);
  [[nodiscard]] std::vector<LatencySnapshot> GetHistory() const;
  [[nodiscard]] std::uint32_t GetHistorySize() const;
  void SetHistorySize(std::uint32_t size);

  [[nodiscard]] float GetDownloadSpeed() const;
  void SetDownloadSpeed(float kbps);

  [[nodiscard]] float GetUploadSpeed() const;
  void SetUploadSpeed(float kbps);

  [[nodiscard]] std::string GetBandwidth() const;

  void Reset();

 private:
  std::uint32_t world_ms_ = 0;
  std::uint32_t home_ms_ = 0;
  std::uint32_t peak_ = 0;
  std::uint32_t min_ = 0;
  bool has_sample_ = false;

  std::vector<LatencySnapshot> history_;
  std::uint32_t max_history_ = 60;
  float sample_time_ = 0.0f;

  float download_speed_ = 0.0f;
  float upload_speed_ = 0.0f;
};

}

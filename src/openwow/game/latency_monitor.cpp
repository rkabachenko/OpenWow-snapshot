#include "openwow/game/latency_monitor.h"

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace openwow::game {

void LatencyMonitor::SetWorldLatency(std::uint32_t ms) { world_ms_ = ms; }
std::uint32_t LatencyMonitor::GetWorldLatency() const { return world_ms_; }

void LatencyMonitor::SetHomeLatency(std::uint32_t ms) { home_ms_ = ms; }
std::uint32_t LatencyMonitor::GetHomeLatency() const { return home_ms_; }

LatencyBand LatencyMonitor::GetBand(std::uint32_t ms) {
  if (ms < 100) return LatencyBand::Good;
  if (ms < 300) return LatencyBand::Fair;
  if (ms < 600) return LatencyBand::Poor;
  return LatencyBand::Critical;
}

LatencyBand LatencyMonitor::GetWorldBand() const { return GetBand(world_ms_); }
LatencyBand LatencyMonitor::GetHomeBand() const { return GetBand(home_ms_); }

std::uint32_t LatencyMonitor::GetBandColor(LatencyBand band) {
  switch (band) {
    case LatencyBand::Good:     return 0xFF00FF00;
    case LatencyBand::Fair:     return 0xFFFFFF00;
    case LatencyBand::Poor:     return 0xFFFFA500;
    case LatencyBand::Critical: return 0xFFFF0000;
  }
  return 0xFFFFFFFF;
}

std::string LatencyMonitor::GetBandName(LatencyBand band) {
  switch (band) {
    case LatencyBand::Good:     return "Good";
    case LatencyBand::Fair:     return "Fair";
    case LatencyBand::Poor:     return "Poor";
    case LatencyBand::Critical: return "Critical";
  }
  return "Unknown";
}

std::uint32_t LatencyMonitor::GetAverageLatency(std::uint32_t samples) const {
  if (history_.empty()) return 0;
  std::uint32_t count =
      std::min(samples, static_cast<std::uint32_t>(history_.size()));

  std::uint64_t sum = 0;
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto& s = history_[history_.size() - 1 - i];
    sum += s.worldMs;
  }
  return static_cast<std::uint32_t>(sum / count);
}

std::uint32_t LatencyMonitor::GetPeakLatency() const { return peak_; }
std::uint32_t LatencyMonitor::GetMinLatency() const { return min_; }

void LatencyMonitor::AddSample(std::uint32_t worldMs, std::uint32_t homeMs) {
  world_ms_ = worldMs;
  home_ms_ = homeMs;

  if (!has_sample_) {
    peak_ = worldMs;
    min_ = worldMs;
    has_sample_ = true;
  } else {
    if (worldMs > peak_) peak_ = worldMs;
    if (worldMs < min_) min_ = worldMs;
  }

  LatencySnapshot snap;
  snap.worldMs = worldMs;
  snap.homeMs = homeMs;
  snap.timestamp = sample_time_;
  history_.push_back(snap);
  if (history_.size() > max_history_) {
    history_.erase(history_.begin());
  }
  sample_time_ += 1.0f;
}

std::vector<LatencySnapshot> LatencyMonitor::GetHistory() const {
  return history_;
}

std::uint32_t LatencyMonitor::GetHistorySize() const {
  return max_history_;
}

void LatencyMonitor::SetHistorySize(std::uint32_t size) {
  max_history_ = size;
  while (history_.size() > max_history_) {
    history_.erase(history_.begin());
  }
}

float LatencyMonitor::GetDownloadSpeed() const { return download_speed_; }
void LatencyMonitor::SetDownloadSpeed(float kbps) { download_speed_ = kbps; }

float LatencyMonitor::GetUploadSpeed() const { return upload_speed_; }
void LatencyMonitor::SetUploadSpeed(float kbps) { upload_speed_ = kbps; }

std::string LatencyMonitor::GetBandwidth() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(1)
      << "DL: " << download_speed_ << " kB/s "
      << "UL: " << upload_speed_ << " kB/s";
  return oss.str();
}

void LatencyMonitor::Reset() {
  world_ms_ = 0;
  home_ms_ = 0;
  peak_ = 0;
  min_ = 0;
  has_sample_ = false;
  history_.clear();
  max_history_ = 60;
  sample_time_ = 0.0f;
  download_speed_ = 0.0f;
  upload_speed_ = 0.0f;
}

}

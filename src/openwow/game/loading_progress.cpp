
#include "openwow/game/loading_progress.h"

#include <algorithm>
#include <numeric>

namespace openwow::game {

void LoadingProgressManager::SetPhases(
    const std::vector<LoadingProgressEntry>& phases) {
  phases_ = phases;
  currentPhaseIdx_ = 0;
  RebuildIndex();
}

void LoadingProgressManager::InitializeDefaultPhases() {
  phases_ = {
      {LoadingPhase::Initializing,    0.0f, "Initializing...",        0.05f},
      {LoadingPhase::TerrainData,     0.0f, "Loading terrain...",     0.25f},
      {LoadingPhase::ObjectData,      0.0f, "Loading objects...",     0.15f},
      {LoadingPhase::TextureStreaming, 0.0f, "Streaming textures...", 0.20f},
      {LoadingPhase::LightData,       0.0f, "Loading lighting...",   0.05f},
      {LoadingPhase::WMOData,         0.0f, "Loading WMO data...",   0.15f},
      {LoadingPhase::M2Data,          0.0f, "Loading models...",     0.10f},
      {LoadingPhase::Finalize,        0.0f, "Finalizing...",         0.05f},
  };
  currentPhaseIdx_ = 0;
  RebuildIndex();
}

void LoadingProgressManager::SetPhaseProgress(LoadingPhase phase,
                                              float progress) {
  auto key = static_cast<std::uint8_t>(phase);
  auto it = phaseIndex_.find(key);
  if (it != phaseIndex_.end()) {
    phases_[it->second].progress = std::clamp(progress, 0.0f, 1.0f);
  }
}

float LoadingProgressManager::GetPhaseProgress(LoadingPhase phase) const {
  auto key = static_cast<std::uint8_t>(phase);
  auto it = phaseIndex_.find(key);
  if (it != phaseIndex_.end()) {
    return phases_[it->second].progress;
  }
  return 0.0f;
}

float LoadingProgressManager::GetOverallProgress() const {
  if (phases_.empty()) return 0.0f;
  float totalWeight = 0.0f;
  float weighted = 0.0f;
  for (const auto& p : phases_) {
    totalWeight += p.weight;
    weighted += p.progress * p.weight;
  }
  if (totalWeight <= 0.0f) return 0.0f;
  return std::clamp(weighted / totalWeight, 0.0f, 1.0f);
}

bool LoadingProgressManager::IsComplete() const {
  return GetOverallProgress() >= 1.0f;
}

LoadingPhase LoadingProgressManager::GetCurrentPhase() const {
  if (phases_.empty()) return LoadingPhase::Initializing;
  if (currentPhaseIdx_ >= phases_.size()) {
    return phases_.back().phase;
  }
  return phases_[currentPhaseIdx_].phase;
}

std::string LoadingProgressManager::GetPhaseDescription(
    LoadingPhase phase) const {
  auto key = static_cast<std::uint8_t>(phase);
  auto it = phaseIndex_.find(key);
  if (it != phaseIndex_.end()) {
    return phases_[it->second].description;
  }
  return {};
}

std::string LoadingProgressManager::GetPhaseName(LoadingPhase phase) {
  switch (phase) {
    case LoadingPhase::Initializing:    return "Initializing";
    case LoadingPhase::TerrainData:     return "TerrainData";
    case LoadingPhase::ObjectData:      return "ObjectData";
    case LoadingPhase::TextureStreaming: return "TextureStreaming";
    case LoadingPhase::LightData:       return "LightData";
    case LoadingPhase::WMOData:         return "WMOData";
    case LoadingPhase::M2Data:          return "M2Data";
    case LoadingPhase::Finalize:        return "Finalize";
  }
  return "Unknown";
}

std::uint32_t LoadingProgressManager::GetPhaseCount() const {
  return static_cast<std::uint32_t>(phases_.size());
}

std::uint32_t LoadingProgressManager::GetCompletedPhaseCount() const {
  std::uint32_t count = 0;
  for (const auto& p : phases_) {
    if (p.progress >= 1.0f) ++count;
  }
  return count;
}

void LoadingProgressManager::SetTipText(const std::string& text) {
  tipText_ = text;
}

const std::string& LoadingProgressManager::GetTipText() const {
  return tipText_;
}

void LoadingProgressManager::AdvancePhase() {
  if (currentPhaseIdx_ < phases_.size()) {
    phases_[currentPhaseIdx_].progress = 1.0f;
    ++currentPhaseIdx_;
  }
}

void LoadingProgressManager::Reset() {
  phases_.clear();
  phaseIndex_.clear();
  currentPhaseIdx_ = 0;
  tipText_.clear();
}

void LoadingProgressManager::RebuildIndex() {
  phaseIndex_.clear();
  for (std::size_t i = 0; i < phases_.size(); ++i) {
    phaseIndex_[static_cast<std::uint8_t>(phases_[i].phase)] = i;
  }
}

}

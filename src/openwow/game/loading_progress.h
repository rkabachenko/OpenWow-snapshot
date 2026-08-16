
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class LoadingPhase : std::uint8_t {
  Initializing     = 0,
  TerrainData      = 1,
  ObjectData       = 2,
  TextureStreaming  = 3,
  LightData        = 4,
  WMOData          = 5,
  M2Data           = 6,
  Finalize         = 7,
};

struct LoadingProgressEntry {
  LoadingPhase phase = LoadingPhase::Initializing;
  float progress = 0.0f;
  std::string description;
  float weight = 1.0f;
};

class LoadingProgressManager {
 public:

  void SetPhases(const std::vector<LoadingProgressEntry>& phases);
  void InitializeDefaultPhases();

  void SetPhaseProgress(LoadingPhase phase, float progress);
  [[nodiscard]] float GetPhaseProgress(LoadingPhase phase) const;

  [[nodiscard]] float GetOverallProgress() const;
  [[nodiscard]] bool IsComplete() const;

  [[nodiscard]] LoadingPhase GetCurrentPhase() const;
  [[nodiscard]] std::string GetPhaseDescription(LoadingPhase phase) const;

  [[nodiscard]] static std::string GetPhaseName(LoadingPhase phase);

  [[nodiscard]] std::uint32_t GetPhaseCount() const;
  [[nodiscard]] std::uint32_t GetCompletedPhaseCount() const;

  void SetTipText(const std::string& text);
  [[nodiscard]] const std::string& GetTipText() const;

  void AdvancePhase();

  void Reset();

 private:

  std::vector<LoadingProgressEntry> phases_;

  std::unordered_map<std::uint8_t, std::size_t> phaseIndex_;
  std::size_t currentPhaseIdx_ = 0;
  std::string tipText_;

  void RebuildIndex();
};

}

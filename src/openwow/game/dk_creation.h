
#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

namespace openwow::game {

struct DKSkinOptions {
    uint32_t skinMin  = 0;
    uint32_t skinMax  = 0;
    uint32_t faceMin  = 0;
    uint32_t faceMax  = 0;
};

class DKCreationManager {
 public:
  static DKCreationManager& Get();

  DKCreationManager(const DKCreationManager&) = delete;
  DKCreationManager& operator=(const DKCreationManager&) = delete;

  void SetDKAvailable(bool available);
  [[nodiscard]] bool IsDKAvailable() const;

  void SetRequiredLevel(uint32_t level);
  [[nodiscard]] uint32_t GetRequiredLevel() const;

  void SetHasLevelRequirement(bool meets);
  [[nodiscard]] bool MeetsRequirement() const;

  [[nodiscard]] std::vector<uint32_t> GetAllowedRaces() const;
  void AddAllowedRace(uint32_t raceId);
  [[nodiscard]] bool IsRaceAllowed(uint32_t raceId) const;

  [[nodiscard]] uint32_t GetStartZone() const;
  [[nodiscard]] uint32_t GetStartLevel() const;

  [[nodiscard]] uint32_t GetMaxDKPerRealm() const;
  void SetExistingDKCount(uint32_t count);
  [[nodiscard]] uint32_t GetExistingDKCount() const;

  [[nodiscard]] bool CanCreateDK() const;

  [[nodiscard]] uint32_t GetDKClassId() const;

  void SetDefaultSkinOptions(uint32_t raceId, const DKSkinOptions& opts);
  [[nodiscard]] DKSkinOptions GetDefaultSkinOptions(uint32_t raceId) const;

  void Reset();

 private:
  DKCreationManager() = default;

  bool available_          = false;
  uint32_t required_level_ = 55;
  bool meets_requirement_  = false;
  std::vector<uint32_t> allowed_races_;
  uint32_t existing_dk_count_ = 0;

  std::vector<std::pair<uint32_t, DKSkinOptions>> skin_options_;

  mutable std::mutex mutex_;
};

}

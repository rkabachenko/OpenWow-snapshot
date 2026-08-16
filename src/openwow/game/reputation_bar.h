#pragma once

#include <cstdint>
#include <string>

namespace openwow::game {

enum class RepStanding : std::uint8_t {
  Hated = 0,
  Hostile,
  Unfriendly,
  Neutral,
  Friendly,
  Honored,
  Revered,
  Exalted,
};

struct RepBarState {
  std::uint32_t factionId{0};
  std::string factionName;
  RepStanding standing{RepStanding::Neutral};
  std::int32_t current{0};
  std::int32_t max{1};
  bool isAtWar{false};
  bool isWatched{false};
};

class ReputationBarUI {
 public:

  void SetWatchedFaction(std::uint32_t factionId, std::string name,
                         RepStanding standing, std::int32_t current,
                         std::int32_t max);

  [[nodiscard]] std::uint32_t GetWatchedFactionId() const;
  [[nodiscard]] std::string GetWatchedFactionName() const;
  [[nodiscard]] RepStanding GetStanding() const;

  [[nodiscard]] std::string GetStandingName() const;

  [[nodiscard]] std::uint32_t GetStandingColor() const;

  [[nodiscard]] std::int32_t GetCurrentRep() const;
  [[nodiscard]] std::int32_t GetMaxRep() const;

  [[nodiscard]] float GetProgress() const;

  [[nodiscard]] std::string GetProgressText() const;

  [[nodiscard]] bool HasWatchedFaction() const;
  void ClearWatchedFaction();

  void SetAtWar(bool atWar);
  [[nodiscard]] bool IsAtWar() const;

  [[nodiscard]] std::int32_t GetRecentGain() const;
  void SetRecentGain(std::int32_t amount);

  [[nodiscard]] float GetRecentGainFade() const;

  void Update(float dt);

  void Reset();

 private:
  RepBarState state_;
  std::int32_t recentGain_{0};
  float recentGainFade_{0.0f};

  static constexpr float kFadeDuration = 3.0f;
};

}

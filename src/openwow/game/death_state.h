
#pragma once

#include <cstdint>

namespace openwow::game {

enum class DeathPhase : std::uint8_t {
  Alive = 0,
  Dead,
  Ghost,
  ResurrectPending,
};

struct DeathPosition {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  uint32_t mapId = 0;
};

class PlayerDeathState {
 public:
  PlayerDeathState() = default;

  void SetPhase(DeathPhase phase);
  [[nodiscard]] DeathPhase GetPhase() const;

  [[nodiscard]] bool IsAlive() const;
  [[nodiscard]] bool IsDead() const;
  [[nodiscard]] bool IsGhost() const;
  [[nodiscard]] bool IsResurrectPending() const;

  void Die(float deathX, float deathY, float deathZ, uint32_t mapId);

  void ReleaseSpirit();

  void Resurrect();

  [[nodiscard]] DeathPosition GetDeathPosition() const;

  [[nodiscard]] float GetTimeSinceDeath() const;
  void SetTimeSinceDeath(float seconds);

  [[nodiscard]] float GetCorpseRecoveryDelay() const;

  [[nodiscard]] bool CanReleaseSpirit() const;

  void SetGhostSpeed(float multiplier);
  [[nodiscard]] float GetGhostSpeed() const;

  void SetResurrectPending(bool pending);

  [[nodiscard]] uint32_t GetDeathCount() const;
  void IncrementDeathCount();
  void ResetDeathCount();

  void Update(float dt);

  void Reset();

 private:
  DeathPhase phase_ = DeathPhase::Alive;
  DeathPosition deathPosition_;
  float timeSinceDeath_ = 0.0f;
  float ghostSpeed_ = 1.0f;
  uint32_t deathCount_ = 0;
};

}


#pragma once

#include <cstdint>

namespace openwow::game {

struct CorpseRunInfo {
  float corpseX = 0.0f;
  float corpseY = 0.0f;
  float corpseZ = 0.0f;
  uint32_t corpseMapId = 0;
  float distance = 0.0f;
  float direction = 0.0f;
};

struct CorpseRunPosition {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  uint32_t mapId = 0;
};

class CorpseRunDisplay {
 public:
  CorpseRunDisplay() = default;

  void SetCorpsePosition(float x, float y, float z, uint32_t mapId);
  [[nodiscard]] CorpseRunPosition GetCorpsePosition() const;
  [[nodiscard]] bool HasCorpse() const;

  void UpdatePlayerPosition(float x, float y, float z);

  [[nodiscard]] float GetDistanceToCorpse() const;
  [[nodiscard]] float GetDirectionToCorpse() const;

  [[nodiscard]] bool IsInRangeToRevive() const;
  [[nodiscard]] float GetReviveRange() const;
  void SetReviveRange(float range);

  [[nodiscard]] bool ShowCorpseArrow() const;

  [[nodiscard]] uint32_t GetCorpseMapId() const;
  [[nodiscard]] bool IsCorpseOnSameMap(uint32_t playerMapId) const;

  [[nodiscard]] float GetRunTimeElapsed() const;
  void SetRunTimeElapsed(float seconds);

  void Update(float dt);
  void Reset();

 private:
  CorpseRunPosition corpsePos_;
  bool hasCorpse_ = false;

  float playerX_ = 0.0f;
  float playerY_ = 0.0f;
  float playerZ_ = 0.0f;

  float reviveRange_ = 40.0f;
  float runTimeElapsed_ = 0.0f;
};

}

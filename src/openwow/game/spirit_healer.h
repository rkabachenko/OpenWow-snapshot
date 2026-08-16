
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class ResurrectOption : std::uint8_t {
  SpiritHealer = 0,
  PlayerResurrect,
  SelfResurrect,
  Soulstone,
};

struct ResurrectOffer {
  ResurrectOption option = ResurrectOption::SpiritHealer;
  ObjectGuid casterGuid;
  std::string casterName;
  float healthPercent = 0.0f;
  float manaPercent = 0.0f;
  uint32_t spellId = 0;
  float timeRemaining = 0.0f;
};

class SpiritHealerSystem {
 public:
  SpiritHealerSystem() = default;

  void SetNearSpiritHealer(bool near);
  [[nodiscard]] bool IsNearSpiritHealer() const;

  void AcceptSpiritHeal();

  void OfferResurrect(ResurrectOffer offer);
  [[nodiscard]] std::optional<ResurrectOffer> GetPendingResurrect() const;
  [[nodiscard]] bool HasPendingResurrect() const;
  void AcceptResurrect();
  void DeclineResurrect();

  [[nodiscard]] float GetResSicknessDuration(float timeDead) const;

  [[nodiscard]] bool WillGetResSickness() const;

  void SetTimeDead(float seconds);
  [[nodiscard]] float GetTimeDead() const;

  [[nodiscard]] ObjectGuid GetSpiritHealerGuid() const;
  void SetSpiritHealerGuid(ObjectGuid guid);

  [[nodiscard]] bool HasSelfResurrect() const;
  void SetSelfResurrect(uint32_t spellId, std::string name);
  void ClearSelfResurrect();
  [[nodiscard]] uint32_t GetSelfResurrectSpellId() const;

  void Update(float dt);
  void Reset();

 private:
  bool nearSpiritHealer_ = false;
  bool usedSpiritHealer_ = false;
  std::optional<ResurrectOffer> pendingOffer_;
  float timeDead_ = 0.0f;
  ObjectGuid spiritHealerGuid_;

  bool hasSelfResurrect_ = false;
  uint32_t selfResurrectSpellId_ = 0;
  std::string selfResurrectName_;
};

}

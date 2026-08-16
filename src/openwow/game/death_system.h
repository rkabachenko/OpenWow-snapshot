
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "openwow/game/object_guid.h"

namespace openwow::game {

class DeathSystem {
 public:
  static DeathSystem& Get();

  DeathSystem(const DeathSystem&) = delete;
  DeathSystem& operator=(const DeathSystem&) = delete;

  void SetDead(bool dead);
  [[nodiscard]] bool IsDead() const;
  void SetGhost(bool ghost);
  [[nodiscard]] bool IsGhost() const;

  struct CorpseLocation {
    float x = 0, y = 0, z = 0;
    std::uint32_t map_id = 0;
    bool valid = false;
  };

  void SetCorpseLocation(float x, float y, float z, std::uint32_t map_id);
  [[nodiscard]] CorpseLocation GetCorpseLocation() const;

  [[nodiscard]] float GetDistanceToCorpse(float player_x, float player_y,
                                           float player_z) const;

  void SetSpiritHealerAvailable(bool available);
  [[nodiscard]] bool IsSpiritHealerAvailable() const;

  void SetResSickness(std::uint32_t duration);
  [[nodiscard]] std::uint32_t GetResSicknessDuration() const;
  [[nodiscard]] bool HasResSickness() const;

  void OfferResurrection(const ObjectGuid& caster,
                          const std::string& caster_name);
  void AcceptResurrection();
  void DeclineResurrection();
  [[nodiscard]] bool HasPendingRes() const;
  [[nodiscard]] std::string GetResCasterName() const;

  void ReleaseSpirit();

  void SetRepopTimer(float seconds);
  [[nodiscard]] float GetRepopTimer() const;
  [[nodiscard]] bool IsRepopTimerActive() const;

  void Reset();

 private:
  DeathSystem() = default;

  bool dead_ = false;
  bool ghost_ = false;
  CorpseLocation corpse_;
  bool spirit_healer_ = false;
  std::uint32_t res_sickness_duration_ = 0;

  struct ResOffer {
    ObjectGuid caster;
    std::string name;
    bool pending = false;
  };
  ResOffer res_offer_;

  float repop_timer_ = 0;
  bool repop_timer_active_ = false;

  mutable std::mutex mutex_;
};

}

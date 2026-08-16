#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

struct TrackedCombatEvent {
  enum Type : std::uint8_t {
    DamageDealt,
    DamageTaken,
    HealingDone,
    HealingTaken,
    SpellMiss,
    KillingBlow,
    Died,
    EnterCombat,
    LeaveCombat,
  };

  Type type = DamageDealt;
  ObjectGuid source;
  ObjectGuid target;
  std::uint32_t spell_id = 0;
  std::uint32_t amount = 0;
  std::uint32_t overkill = 0;
  std::uint32_t absorbed = 0;
  std::uint32_t resisted = 0;
  std::uint32_t blocked = 0;
  bool is_critical = false;
  std::uint32_t school_mask = 0;
  std::uint32_t timestamp = 0;
};

struct SwingTimer {
  float speed = 0.0f;
  float remaining = 0.0f;
  bool is_active = false;
  std::uint32_t last_swing_time = 0;
};

struct CombatStats {
  std::uint64_t damage_done = 0;
  std::uint64_t damage_taken = 0;
  std::uint64_t healing_done = 0;
  std::uint64_t healing_taken = 0;
  std::uint32_t killing_blows = 0;
  std::uint32_t deaths = 0;
  float dps = 0.0f;
  float hps = 0.0f;
  std::uint32_t combat_start_time = 0;
  std::uint32_t combat_duration = 0;
};

class CombatTracker {
 public:
  static CombatTracker& Get();

  [[nodiscard]] bool IsInCombat() const;
  void SetInCombat(bool combat);
  [[nodiscard]] std::uint32_t GetCombatDuration() const;

  void AddEvent(const TrackedCombatEvent& event);
  [[nodiscard]] std::vector<TrackedCombatEvent> GetRecentEvents() const;

  void SetMainHandSwing(float speed);
  void SetOffHandSwing(float speed);
  void SetRangedSwing(float speed);
  void UpdateSwingTimers(float delta_time);
  [[nodiscard]] SwingTimer GetMainHandSwing() const;
  [[nodiscard]] SwingTimer GetOffHandSwing() const;
  [[nodiscard]] SwingTimer GetRangedSwing() const;
  void ResetSwingTimer(bool mainhand, bool offhand, bool ranged);

  [[nodiscard]] CombatStats GetStats() const;
  [[nodiscard]] float GetDPS() const;
  [[nodiscard]] float GetHPS() const;

  void SetAbsorbAmount(const ObjectGuid& unit, std::uint32_t amount);
  [[nodiscard]] std::uint32_t GetAbsorbAmount(const ObjectGuid& unit) const;

  void ResetStats();
  void Reset();

  static constexpr std::size_t kMaxEvents = 500;

 private:
  CombatTracker() = default;

  void RecalcStats();

  bool in_combat_ = false;
  std::uint32_t combat_start_ = 0;
  std::vector<TrackedCombatEvent> events_;
  SwingTimer main_hand_;
  SwingTimer off_hand_;
  SwingTimer ranged_;
  CombatStats stats_;
  std::unordered_map<std::uint64_t, std::uint32_t> absorbs_;
  mutable std::mutex mutex_;
};

}

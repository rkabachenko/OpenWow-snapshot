
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"

namespace openwow::game {

struct AreaSpiritHealerTime {
  ObjectGuid healer;
  std::chrono::milliseconds time_left{};
};

struct DestructibleBuildingDamage {
  std::uint64_t target_guid = 0;
  std::uint64_t caster_guid = 0;
  std::uint64_t owner_guid = 0;
  std::int32_t damage = 0;
  std::uint32_t spell_id = 0;
};

struct ProcResist {
  std::uint64_t caster = 0;
  std::uint64_t target = 0;
  std::uint32_t spell_id = 0;
  std::uint8_t is_debug = 0;
};

struct BrokenAuraEntry {
  std::uint32_t spell_id = 0;
  bool is_debuff = false;
};

struct SpellBreakLog {
  std::uint64_t victim = 0;
  std::uint64_t caster = 0;
  std::uint32_t breaking_spell_id = 0;
  std::vector<BrokenAuraEntry> broken_auras;
};

struct AuraCastLog {
  std::uint64_t caster = 0;
  std::uint64_t target = 0;
  std::uint32_t spell_id = 0;
  std::vector<std::uint32_t> triggered_spells;
};

struct ProjectilePosition {
  std::uint64_t caster = 0;
  std::uint8_t cast_id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

class CombatHandler {
 public:

  bool HandleBreakTarget(const std::uint8_t* data, std::size_t len);
  bool HandleClearTarget(const std::uint8_t* data, std::size_t len);
  bool HandleForceDisplayUpdate(const std::uint8_t* data, std::size_t len);
  bool HandleResurrectFailed(const std::uint8_t* data, std::size_t len);
  bool ParseSpiritHealerConfirm(const std::uint8_t* data, std::size_t len,
                                ObjectGuid& healer) const;

  void StoreSpiritHealerConfirm(ObjectGuid healer) {
    last_spirit_healer_guid_ = healer;
  }
  bool HandleAreaSpiritHealerTime(const std::uint8_t* data, std::size_t len);
  bool HandleDestructibleBuildingDamage(const std::uint8_t* data,
                                        std::size_t len);

  bool HandleCombatEventFailed();
  bool HandleProcResist(const std::uint8_t* data, std::size_t len);
  bool HandleProcResist(PacketReader& reader);
  bool HandleSpellBreakLog(const std::uint8_t* data, std::size_t len);
  bool HandleSpellBreakLog(PacketReader& reader);
  bool HandleAuraCastLog(const std::uint8_t* data, std::size_t len);
  bool HandleResetRangedCombatTimer(const std::uint8_t* data, std::size_t len);
  bool HandleSetProjectilePosition(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] std::uint64_t last_break_target_guid() const {
    return last_break_target_guid_;
  }
  [[nodiscard]] std::uint64_t last_clear_target_guid() const {
    return last_clear_target_guid_;
  }
  [[nodiscard]] std::uint64_t last_force_display_update_guid() const {
    return last_force_display_update_guid_;
  }
  [[nodiscard]] std::uint32_t last_resurrect_failed() const {
    return last_resurrect_failed_;
  }
  [[nodiscard]] ObjectGuid last_spirit_healer_guid() const {
    return last_spirit_healer_guid_;
  }
  [[nodiscard]] const std::optional<AreaSpiritHealerTime>&
  last_area_spirit_healer_time() const {
    return last_area_spirit_healer_time_;
  }
  [[nodiscard]] const std::optional<DestructibleBuildingDamage>&
  last_building_damage() const {
    return last_building_damage_;
  }

  [[nodiscard]] bool combat_event_failed() const { return combat_event_failed_; }
  [[nodiscard]] const std::optional<ProcResist>& last_proc_resist() const {
    return last_proc_resist_;
  }
  [[nodiscard]] const std::optional<SpellBreakLog>& last_spell_break_log() const {
    return last_spell_break_log_;
  }
  [[nodiscard]] const std::optional<AuraCastLog>& last_aura_cast_log() const {
    return last_aura_cast_log_;
  }
  [[nodiscard]] std::uint32_t ranged_timer_ms() const { return ranged_timer_ms_; }
  [[nodiscard]] const std::optional<ProjectilePosition>& last_projectile_position() const {
    return last_projectile_position_;
  }

  void Clear();

 private:
  std::uint64_t last_break_target_guid_ = 0;
  std::uint64_t last_clear_target_guid_ = 0;
  std::uint64_t last_force_display_update_guid_ = 0;
  std::uint32_t last_resurrect_failed_ = 0;
  ObjectGuid last_spirit_healer_guid_;
  std::optional<AreaSpiritHealerTime> last_area_spirit_healer_time_;
  std::optional<DestructibleBuildingDamage> last_building_damage_;

  bool combat_event_failed_ = false;
  std::optional<ProcResist> last_proc_resist_;
  std::optional<SpellBreakLog> last_spell_break_log_;
  std::optional<AuraCastLog> last_aura_cast_log_;
  std::uint32_t ranged_timer_ms_ = 0;
  std::optional<ProjectilePosition> last_projectile_position_;
};

}

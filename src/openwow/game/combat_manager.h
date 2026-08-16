#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/unit_defines.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace openwow::game {

namespace HitInfoFlag {
inline constexpr std::uint32_t kNormalSwing     = 0x00000000;
inline constexpr std::uint32_t kUnk1            = 0x00000001;
inline constexpr std::uint32_t kAffectsVictim   = 0x00000002;
inline constexpr std::uint32_t kOffhand          = 0x00000004;
inline constexpr std::uint32_t kMiss             = 0x00000010;
inline constexpr std::uint32_t kFullAbsorb       = 0x00000020;
inline constexpr std::uint32_t kPartialAbsorb    = 0x00000040;
inline constexpr std::uint32_t kFullResist       = 0x00000080;
inline constexpr std::uint32_t kPartialResist    = 0x00000100;
inline constexpr std::uint32_t kCriticalHit      = 0x00000200;
inline constexpr std::uint32_t kBlock            = 0x00002000;
inline constexpr std::uint32_t kGlancing         = 0x00010000;
inline constexpr std::uint32_t kCrushing         = 0x00020000;
inline constexpr std::uint32_t kNoAnimation      = 0x00040000;
inline constexpr std::uint32_t kRageGain         = 0x00800000;
inline constexpr std::uint32_t kFakeDamage       = 0x01000000;
}

enum class VictimState : std::uint8_t {
  kIntact    = 0,
  kHit       = 1,
  kDodge     = 2,
  kParry     = 3,
  kInterrupt = 4,
  kBlock     = 5,
  kEvade     = 6,
  kImmune    = 7,
  kDeflect   = 8,
};

struct SubDamageInfo {
  std::uint32_t school_mask{0};
  float damage_float{0.0f};
  std::uint32_t damage{0};
  std::uint32_t absorbed{0};
  std::uint32_t resisted{0};
};

struct AttackerStateUpdate {
  std::uint32_t hit_info{0};
  ObjectGuid attacker;
  ObjectGuid victim;
  std::uint32_t total_damage{0};
  std::uint32_t overkill{0};
  std::vector<SubDamageInfo> sub_damages;
  VictimState victim_state{VictimState::kIntact};
  std::uint32_t attacker_state{0};
  std::uint32_t melee_spell_id{0};
  std::uint32_t blocked_amount{0};
};

struct AttackStartInfo {
  ObjectGuid attacker;
  ObjectGuid victim;
};

struct HealthUpdate {
  ObjectGuid guid;
  std::uint32_t health{0};
};

struct PowerUpdate {
  ObjectGuid guid;
  PowerType power_type{PowerType::kMana};
  std::uint32_t value{0};
};

enum class AiReactionType : std::uint32_t {
  kAlert   = 0,
  kHostile = 2,
};

struct AiReaction {
  ObjectGuid unit;
  std::uint32_t reaction{0};
};

struct XpGainLog {
  ObjectGuid victim;
  std::uint32_t xp_total{0};
  std::uint8_t xp_type{0};
  float group_rate{0.0f};
};

struct LevelUpInfo {
  std::uint32_t level{0};
  std::int32_t health_delta{0};
  std::int32_t mana_delta{0};

  std::int32_t power_delta[6]{};

  std::int32_t stat_delta[5]{};
};

struct EnvironmentalDamage {
  ObjectGuid guid;
  std::uint8_t type{0};
  std::uint32_t damage{0};
  std::uint32_t absorbed{0};
  std::uint32_t resisted{0};
};

struct ThreatUpdateEntry {
  ObjectGuid unit;
  std::uint32_t threat{0};
};

struct ThreatUpdate {
  ObjectGuid target;
  std::vector<ThreatUpdateEntry> entries;
};

struct HighestThreatUpdate {
  ObjectGuid target;
  ObjectGuid highest;
  std::vector<ThreatUpdateEntry> entries;
};

enum class AttackSwingError {
  kNone = 0,
  kNotInRange,
  kBadFacing,
  kDeadTarget,
  kCantAttack,
};

class CombatManager {
 public:
  CombatManager() = default;

  bool HandleAttackStart(const std::uint8_t* data, std::size_t len);

  bool HandleAttackStop(const std::uint8_t* data, std::size_t len);

  bool HandleAttackerStateUpdate(const std::uint8_t* data, std::size_t len);

  bool HandleAttackerStateUpdate(PacketReader& reader);

  bool HandleCancelCombat(const std::uint8_t* data, std::size_t len);

  bool HandleAiReaction(const std::uint8_t* data, std::size_t len);

  bool HandleAttackSwingError(AttackSwingError error);

  void ClearSwingError(std::uint32_t current_time_ms);

  void SetSwingError(AttackSwingError error, std::uint32_t current_time_ms);

  [[nodiscard]] bool TryConsumeSwingErrorForDisplay(std::uint32_t current_time_ms,
                                                     AttackSwingError& out_error);

  bool HandleHealthUpdate(const std::uint8_t* data, std::size_t len);

  bool HandlePowerUpdate(const std::uint8_t* data, std::size_t len);

  bool HandleThreatUpdate(const std::uint8_t* data, std::size_t len);

  bool HandleHighestThreatUpdate(const std::uint8_t* data, std::size_t len);

  bool HandleThreatClear(const std::uint8_t* data, std::size_t len);

  bool HandleThreatRemove(const std::uint8_t* data, std::size_t len);

  bool HandleLogXpGain(const std::uint8_t* data, std::size_t len);

  bool HandleLevelUpInfo(const std::uint8_t* data, std::size_t len);

  bool HandleEnvironmentalDamageLog(const std::uint8_t* data,
                                     std::size_t len);

  [[nodiscard]] static net::wotlk::WorldPacket BuildAttackSwing(
      std::uint64_t target_guid);

  [[nodiscard]] static net::wotlk::WorldPacket BuildAttackStop();

  [[nodiscard]] bool in_combat() const { return in_combat_; }
  [[nodiscard]] const std::optional<AttackerStateUpdate>& last_state_update()
      const { return last_state_update_; }
  [[nodiscard]] const std::optional<AttackStartInfo>& attack_start() const {
    return attack_start_;
  }
  [[nodiscard]] const std::optional<AttackStartInfo>& attack_stop() const {
    return attack_stop_;
  }
  [[nodiscard]] const std::optional<HealthUpdate>& last_health_update() const {
    return last_health_update_;
  }
  [[nodiscard]] const std::optional<PowerUpdate>& last_power_update() const {
    return last_power_update_;
  }
  [[nodiscard]] const std::optional<ThreatUpdate>& threat_update() const {
    return threat_update_;
  }
  [[nodiscard]] const std::optional<HighestThreatUpdate>&
  highest_threat_update() const { return highest_threat_update_; }
  [[nodiscard]] const std::optional<AiReaction>& ai_reaction() const {
    return ai_reaction_;
  }
  [[nodiscard]] const std::optional<XpGainLog>& xp_gain() const {
    return xp_gain_;
  }
  [[nodiscard]] const std::optional<LevelUpInfo>& level_up() const {
    return level_up_;
  }
  [[nodiscard]] const std::optional<EnvironmentalDamage>& env_damage() const {
    return env_damage_;
  }
  [[nodiscard]] AttackSwingError swing_error() const { return swing_error_; }
  [[nodiscard]] std::uint32_t swing_error_next_display_ms() const {
    return swing_error_next_display_ms_;
  }

  void Clear();

 private:
  bool in_combat_{false};
  std::optional<AttackerStateUpdate> last_state_update_;
  std::optional<AttackStartInfo> attack_start_;
  std::optional<AttackStartInfo> attack_stop_;
  std::optional<HealthUpdate> last_health_update_;
  std::optional<PowerUpdate> last_power_update_;
  std::optional<ThreatUpdate> threat_update_;
  std::optional<HighestThreatUpdate> highest_threat_update_;
  std::optional<AiReaction> ai_reaction_;
  std::optional<XpGainLog> xp_gain_;
  std::optional<LevelUpInfo> level_up_;
  std::optional<EnvironmentalDamage> env_damage_;
  AttackSwingError swing_error_{AttackSwingError::kNone};

  std::uint32_t swing_error_next_display_ms_{0};
};

inline constexpr std::uint32_t kSwingErrorDisplayThrottleMs = 4000u;

}

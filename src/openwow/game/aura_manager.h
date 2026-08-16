#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::data::dbc {
struct SpellEntry;
}

namespace openwow::game {

enum class AuraFlag : std::uint8_t {
  kNone             = 0x00,
  kEffIndex0        = 0x01,
  kEffIndex1        = 0x02,
  kEffIndex2        = 0x04,

  kCasterIsTarget   = 0x08,
  kPositive         = 0x10,
  kDuration         = 0x20,
  kAnyEffectAmount  = 0x40,
  kNegative         = 0x80,
};

inline AuraFlag operator|(AuraFlag a, AuraFlag b) {
  return static_cast<AuraFlag>(
      static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
inline bool HasFlag(AuraFlag field, AuraFlag flag) {
  return (static_cast<std::uint8_t>(field) & static_cast<std::uint8_t>(flag)) != 0;
}

struct AuraSlotInfo {
  std::uint8_t slot{0};
  std::uint32_t spell_id{0};
  AuraFlag flags{AuraFlag::kNone};
  std::uint8_t caster_level{0};
  std::uint8_t stack_or_charges{0};

  ObjectGuid caster_guid;
  std::uint32_t max_duration{0};
  std::uint32_t remaining_duration{0};
};

struct AuraSlotChange {
  std::uint8_t slot{0};
  AuraSlotInfo old_value{};
  AuraSlotInfo new_value{};
};

struct AuraUpdateDiff {
  ObjectGuid target;
  std::vector<AuraSlotChange> changes;
};

enum class SpellModOp : std::uint8_t {
  kDamage       = 0,
  kDuration     = 1,
  kThreat       = 2,
  kEffect1      = 3,
  kCharges      = 4,
  kRange        = 5,
  kRadius       = 6,
  kCritChance   = 7,
  kAllEffects   = 8,
  kNotLoseCast  = 9,
  kCastingTime  = 10,
  kCooldown     = 11,
  kEffect2      = 12,
  kCost         = 14,
  kCritMultiplier = 15,

  kResistMissChance = 16,
  kJumpTargets  = 17,
  kChanceOfSuccess = 18,
  kActivationTime = 19,
  kDamageMultiplier = 20,
  kGlobalCooldown = 21,
  kDot          = 22,
  kEffect3      = 23,
  kBonusMultiplier = 24,
  kMultipleValue = 27,
  kResistDispelChance = 28,
  kCritDamageBonus = 29,
  kSpellCostRefundOnFail = 30,
};

struct SpellModifier {
  std::uint8_t group{0};
  SpellModOp op{};
  std::int32_t value{0};
  bool is_flat{true};
};

struct CastFailedInfo {
  std::uint8_t cast_count{0};
  std::uint32_t spell_id{0};
  std::uint8_t result{0};

  std::uint32_t extra1{0};
  std::uint32_t extra2{0};
  bool has_extra1{false};
  bool has_extra2{false};
};

struct SpellFailureInfo {
  ObjectGuid caster;
  std::uint8_t cast_count{0};
  std::uint32_t spell_id{0};
  std::uint8_t result{0};
};

struct SpellDelayedInfo {
  ObjectGuid caster;
  std::uint32_t delay_ms{0};
};

struct SpellChannelInfo {
  ObjectGuid caster;
  std::uint32_t spell_id{0};
  std::int32_t duration{0};
  std::int32_t time_remaining{0};
};

struct CooldownEventInfo {
  std::uint32_t spell_id{0};
  ObjectGuid guid;
};

class AuraManager {
 public:
  AuraManager() = default;

  bool HandleAuraUpdate(const std::uint8_t* data, std::size_t len);

  bool HandleAuraUpdateAll(const std::uint8_t* data, std::size_t len);

  bool HandleSetFlatSpellModifier(const std::uint8_t* data, std::size_t len);

  bool HandleSetPctSpellModifier(const std::uint8_t* data, std::size_t len);

  bool HandleCooldownEvent(const std::uint8_t* data, std::size_t len);

  bool HandleClearCooldown(const std::uint8_t* data, std::size_t len);

  bool HandleCastFailed(const std::uint8_t* data, std::size_t len);

  bool HandleSpellFailure(const std::uint8_t* data, std::size_t len);

  bool HandleSpellDelayed(const std::uint8_t* data, std::size_t len);

  bool HandleChannelStart(const std::uint8_t* data, std::size_t len);

  bool HandleChannelUpdate(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] static net::wotlk::WorldPacket BuildCancelAura(
      std::uint32_t spell_id);

  [[nodiscard]] static net::wotlk::WorldPacket BuildCancelCast(
      std::uint32_t spell_id);

  [[nodiscard]] static net::wotlk::WorldPacket BuildCancelChannelling(
      std::uint32_t reason);

  [[nodiscard]] const std::vector<AuraSlotInfo>& GetAuras(
      std::uint64_t guid) const;

  [[nodiscard]] const AuraSlotInfo* FindAuraBySpellId(
      std::uint64_t guid, std::uint32_t spell_id) const;

  [[nodiscard]] const AuraUpdateDiff& last_update_diff() const noexcept {
    return last_update_diff_;
  }

  [[nodiscard]] const std::vector<SpellModifier>& modifiers() const {
    return modifiers_;
  }

  [[nodiscard]] bool AccumulateSpellModifierDeltas(
      std::uint32_t active_spell_family,
      const data::dbc::SpellEntry& spell,
      SpellModOp op,
      std::int32_t* out_flat_delta,
      std::int32_t* out_pct_total) const;

  [[nodiscard]] bool ApplySpellModifierDeltas(
      std::uint32_t active_spell_family,
      const data::dbc::SpellEntry& spell,
      SpellModOp op,
      std::int32_t* value) const;

  [[nodiscard]] bool ApplySpellModifierDeltas(
      std::uint32_t active_spell_family,
      const data::dbc::SpellEntry& spell,
      SpellModOp op,
      float* value) const;

  [[nodiscard]] const std::optional<CastFailedInfo>& cast_failed() const {
    return cast_failed_;
  }

  [[nodiscard]] const std::optional<SpellFailureInfo>& spell_failure() const {
    return spell_failure_;
  }

  [[nodiscard]] const std::optional<SpellDelayedInfo>& spell_delayed() const {
    return spell_delayed_;
  }

  [[nodiscard]] const std::optional<SpellChannelInfo>& channel_info() const {
    return channel_info_;
  }

  [[nodiscard]] const std::optional<CooldownEventInfo>& cooldown_event()
      const { return cooldown_event_; }

  [[nodiscard]] std::uint32_t clear_cooldown_spell() const {
    return clear_cooldown_spell_;
  }

  void Clear();

 private:

  static bool ReadAuraBlock(PacketReader& reader, AuraSlotInfo& out,
                            const ObjectGuid& target);

  bool HandleSpellModifier(const std::uint8_t* data, std::size_t len,
                           bool is_flat);

  std::unordered_map<std::uint64_t, std::vector<AuraSlotInfo>> auras_;
  static const std::vector<AuraSlotInfo> kEmptyAuras;
  AuraUpdateDiff last_update_diff_;

  std::vector<SpellModifier> modifiers_;
  std::optional<CastFailedInfo> cast_failed_;
  std::optional<SpellFailureInfo> spell_failure_;
  std::optional<SpellDelayedInfo> spell_delayed_;
  std::optional<SpellChannelInfo> channel_info_;
  std::optional<CooldownEventInfo> cooldown_event_;
  std::uint32_t clear_cooldown_spell_{0};
};

}

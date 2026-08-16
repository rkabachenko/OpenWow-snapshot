
#include "openwow/game/aura_manager.h"

#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace openwow::game {

using openwow::diagnostics::Log;
using openwow::diagnostics::LogLevel;

namespace {

[[nodiscard]] std::int32_t BitCastSigned(const std::uint32_t bits) {
  return std::bit_cast<std::int32_t>(bits);
}

[[nodiscard]] std::uint32_t BitCastUnsigned(const std::int32_t value) {
  return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] std::int32_t AddWrappedI32(const std::int32_t lhs,
                                         const std::int32_t rhs) {
  return BitCastSigned(BitCastUnsigned(lhs) + BitCastUnsigned(rhs));
}

[[nodiscard]] std::int32_t MultiplyWrappedI32(const std::int32_t lhs,
                                              const std::int32_t rhs) {
  return BitCastSigned(BitCastUnsigned(lhs) * BitCastUnsigned(rhs));
}

[[nodiscard]] AuraSlotInfo MakeEmptyAuraSlot(const std::uint8_t slot) {
  AuraSlotInfo info;
  info.slot = slot;
  return info;
}

void EnsureAuraSlotStorage(std::vector<AuraSlotInfo>& auras,
                           const std::size_t required_slots) {
  const auto previous_size = auras.size();
  if (previous_size >= required_slots) {
    return;
  }

  auras.resize(required_slots);
  for (std::size_t slot = previous_size; slot < required_slots; ++slot) {
    auras[slot] = MakeEmptyAuraSlot(static_cast<std::uint8_t>(slot));
  }
}

void ResetAuraSlots(std::vector<AuraSlotInfo>& auras) {
  for (std::size_t slot = 0; slot < auras.size(); ++slot) {
    auras[slot] = MakeEmptyAuraSlot(static_cast<std::uint8_t>(slot));
  }
}

[[nodiscard]] bool AuraSlotsEqual(const AuraSlotInfo& lhs,
                                  const AuraSlotInfo& rhs) {
  return lhs.slot == rhs.slot && lhs.spell_id == rhs.spell_id &&
         lhs.flags == rhs.flags && lhs.caster_level == rhs.caster_level &&
         lhs.stack_or_charges == rhs.stack_or_charges &&
         lhs.caster_guid == rhs.caster_guid &&
         lhs.max_duration == rhs.max_duration &&
         lhs.remaining_duration == rhs.remaining_duration &&
         lhs.remaining_duration == rhs.remaining_duration;
}

AuraSlotInfo SlotOrEmpty(const std::vector<AuraSlotInfo>& slots,
                         const std::size_t slot) {
  return slot < slots.size()
             ? slots[slot]
             : MakeEmptyAuraSlot(static_cast<std::uint8_t>(slot));
}

AuraUpdateDiff BuildAuraDiff(const ObjectGuid target,
                             const std::vector<AuraSlotInfo>& old_slots,
                             const std::vector<AuraSlotInfo>& new_slots) {
  AuraUpdateDiff diff{.target = target};
  const auto count = std::max(old_slots.size(), new_slots.size());
  diff.changes.reserve(count);
  for (std::size_t slot = 0; slot < count; ++slot) {
    auto old_value = SlotOrEmpty(old_slots, slot);
    auto new_value = SlotOrEmpty(new_slots, slot);
    if (!AuraSlotsEqual(old_value, new_value)) {
      diff.changes.push_back({.slot = static_cast<std::uint8_t>(slot),
                              .old_value = std::move(old_value),
                              .new_value = std::move(new_value)});
    }
  }
  return diff;
}

}

const std::vector<AuraSlotInfo> AuraManager::kEmptyAuras;

bool AuraManager::ReadAuraBlock(PacketReader& r, AuraSlotInfo& out,
                               const ObjectGuid& target) {
  if (!r.ReadU8(out.slot)) return false;
  if (!r.ReadU32(out.spell_id)) return false;

  if (out.spell_id == 0) {

    return true;
  }

  std::uint8_t flags_raw;
  if (!r.ReadU8(flags_raw)) return false;
  out.flags = static_cast<AuraFlag>(flags_raw);
  if (!r.ReadU8(out.caster_level)) return false;
  if (!r.ReadU8(out.stack_or_charges)) return false;

  if (HasFlag(out.flags, AuraFlag::kCasterIsTarget)) {
    out.caster_guid = target;
  } else if (!r.ReadPackedGuid(out.caster_guid)) {
    return false;
  }

  if (HasFlag(out.flags, AuraFlag::kDuration)) {
    std::uint32_t max_dur, rem_dur;
    if (!r.ReadU32(max_dur)) return false;
    if (!r.ReadU32(rem_dur)) return false;
    out.max_duration = max_dur;
    out.remaining_duration = rem_dur;
  }

  return true;
}

bool AuraManager::HandleAuraUpdate(const std::uint8_t* data,
                                    std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid target;
  if (!r.ReadPackedGuid(target)) return false;
  if (target.IsEmpty()) return false;

  auto& auras = auras_[target.GetRawValue()];
  const auto old_auras = auras;

  while (r.Remaining() > 0) {
    AuraSlotInfo info;
    if (!ReadAuraBlock(r, info, target)) return false;
    EnsureAuraSlotStorage(auras, static_cast<std::size_t>(info.slot) + 1u);
    auras[info.slot] = info;
  }
  last_update_diff_ = BuildAuraDiff(target, old_auras, auras);
  return true;
}

bool AuraManager::HandleAuraUpdateAll(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid target;
  if (!r.ReadPackedGuid(target)) return false;
  if (target.IsEmpty()) return false;

  const auto existing = auras_.find(target.GetRawValue());
  auto replacement = existing != auras_.end()
                         ? existing->second
                         : std::vector<AuraSlotInfo>{};
  ResetAuraSlots(replacement);

  while (r.Remaining() > 0) {
    AuraSlotInfo info;
    if (!ReadAuraBlock(r, info, target)) return false;
    EnsureAuraSlotStorage(
        replacement, static_cast<std::size_t>(info.slot) + 1u);
    replacement[info.slot] = info;
  }
  auto& auras = auras_[target.GetRawValue()];
  auto old_auras = auras;
  auras = std::move(replacement);
  last_update_diff_ = BuildAuraDiff(target, old_auras, auras);
  return true;
}

bool AuraManager::HandleSpellModifier(const std::uint8_t* data,
                                       std::size_t len, bool is_flat) {
  PacketReader r(data, len);

  SpellModifier mod;
  if (!r.ReadU8(mod.group)) return false;
  std::uint8_t op_raw;
  if (!r.ReadU8(op_raw)) return false;
  mod.op = static_cast<SpellModOp>(op_raw);
  if (!r.ReadI32(mod.value)) return false;
  mod.is_flat = is_flat;

  for (auto& m : modifiers_) {
    if (m.group == mod.group && m.op == mod.op && m.is_flat == is_flat) {
      m.value = mod.value;
      return true;
    }
  }
  modifiers_.push_back(mod);
  return true;
}

bool AuraManager::HandleSetFlatSpellModifier(const std::uint8_t* data,
                                              std::size_t len) {
  return HandleSpellModifier(data, len, true);
}

bool AuraManager::HandleSetPctSpellModifier(const std::uint8_t* data,
                                             std::size_t len) {
  return HandleSpellModifier(data, len, false);
}

bool AuraManager::AccumulateSpellModifierDeltas(
    const std::uint32_t active_spell_family,
    const data::dbc::SpellEntry& spell,
    const SpellModOp op,
    std::int32_t* const out_flat_delta,
    std::int32_t* const out_pct_total) const {
  constexpr std::uint32_t kAttrEx3SkipSpellModifiers = 0x20000000u;
  constexpr std::uint8_t kSpellModifierGroupCount = 96;

  if (out_flat_delta != nullptr) {
    *out_flat_delta = 0;
  }
  if (out_pct_total != nullptr) {
    *out_pct_total = 100;
  }

  if (active_spell_family == 0 || spell.spell_family_name == 0 ||
      spell.spell_family_name != active_spell_family ||
      (spell.attributes_ex3 & kAttrEx3SkipSpellModifiers) != 0) {
    return false;
  }

  if (out_pct_total != nullptr) {
    *out_pct_total = 0;
  }

  std::int32_t flat_delta = 0;
  std::int32_t pct_delta = 0;
  for (const auto& modifier : modifiers_) {
    if (modifier.op != op || modifier.group >= kSpellModifierGroupCount) {
      continue;
    }

    const auto word = static_cast<std::size_t>(modifier.group / 32);
    const auto bit = static_cast<std::uint32_t>(modifier.group % 32);
    if ((spell.spell_family_flags[word] & (1u << bit)) == 0) {
      continue;
    }

    if (modifier.is_flat) {
      flat_delta = AddWrappedI32(flat_delta, modifier.value);
    } else {
      pct_delta = AddWrappedI32(pct_delta, modifier.value);
    }
  }

  if (flat_delta == 0 && pct_delta == 0) {
    return false;
  }

  if (out_flat_delta != nullptr) {
    *out_flat_delta = flat_delta;
  }
  if (out_pct_total != nullptr) {
    *out_pct_total = std::max(AddWrappedI32(pct_delta, 100), 0);
  }
  return true;
}

bool AuraManager::ApplySpellModifierDeltas(
    const std::uint32_t active_spell_family,
    const data::dbc::SpellEntry& spell,
    const SpellModOp op,
    std::int32_t* const value) const {
  if (value == nullptr) {
    return false;
  }

  std::int32_t flat_delta = 0;
  std::int32_t pct_total = 100;
  if (!AccumulateSpellModifierDeltas(
          active_spell_family, spell, op, &flat_delta, &pct_total)) {
    return false;
  }

  const auto adjusted_value = AddWrappedI32(*value, flat_delta);
  const auto scaled_value = MultiplyWrappedI32(adjusted_value, pct_total);
  *value = scaled_value / 100;
  return true;
}

bool AuraManager::ApplySpellModifierDeltas(
    const std::uint32_t active_spell_family,
    const data::dbc::SpellEntry& spell,
    const SpellModOp op,
    float* const value) const {
  if (value == nullptr) {
    return false;
  }

  std::int32_t flat_delta = 0;
  std::int32_t pct_total = 100;
  if (!AccumulateSpellModifierDeltas(
          active_spell_family, spell, op, &flat_delta, &pct_total)) {
    return false;
  }

  *value = (*value + static_cast<float>(flat_delta)) *
           (static_cast<float>(pct_total) * 0.01f);
  return true;
}

bool AuraManager::HandleCooldownEvent(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  CooldownEventInfo info;
  if (!r.ReadU32(info.spell_id)) return false;
  std::uint64_t raw;
  if (!r.ReadU64(raw)) return false;
  info.guid = ObjectGuid(raw);
  cooldown_event_ = info;
  return true;
}

bool AuraManager::HandleClearCooldown(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t spell_id;
  if (!r.ReadU32(spell_id)) return false;
  clear_cooldown_spell_ = spell_id;
  std::uint64_t guid_raw;
  if (!r.ReadU64(guid_raw)) return false;
  return true;
}

bool AuraManager::HandleCastFailed(const std::uint8_t* data,
                                    std::size_t len) {
  PacketReader r(data, len);

  CastFailedInfo info;
  if (!r.ReadU8(info.cast_count)) return false;
  if (!r.ReadU32(info.spell_id)) return false;
  if (!r.ReadU8(info.result)) return false;

  std::uint32_t extra;
  if (r.ReadU32(extra)) {
    info.extra1 = extra;
    info.has_extra1 = true;
    if (r.ReadU32(extra)) {
      info.extra2 = extra;
      info.has_extra2 = true;
    }
  }

  cast_failed_ = info;
  return true;
}

bool AuraManager::HandleSpellFailure(const std::uint8_t* data,
                                      std::size_t len) {
  PacketReader r(data, len);

  SpellFailureInfo info;
  std::uint64_t caster_raw;
  if (!r.ReadU64(caster_raw)) return false;
  info.caster = ObjectGuid(caster_raw);
  if (!r.ReadU8(info.cast_count)) return false;
  if (!r.ReadU32(info.spell_id)) return false;
  if (!r.ReadU8(info.result)) return false;

  spell_failure_ = info;
  return true;
}

bool AuraManager::HandleSpellDelayed(const std::uint8_t* data,
                                      std::size_t len) {
  PacketReader r(data, len);
  SpellDelayedInfo info;
  if (!r.ReadPackedGuid(info.caster)) return false;
  if (!r.ReadU32(info.delay_ms)) return false;
  spell_delayed_ = info;
  return true;
}

bool AuraManager::HandleChannelStart(const std::uint8_t* data,
                                      std::size_t len) {
  PacketReader r(data, len);
  SpellChannelInfo info;
  if (!r.ReadPackedGuid(info.caster)) return false;
  if (!r.ReadU32(info.spell_id)) return false;
  std::uint32_t dur;
  if (!r.ReadU32(dur)) return false;
  info.duration = static_cast<std::int32_t>(dur);
  info.time_remaining = info.duration;
  channel_info_ = info;
  return true;
}

bool AuraManager::HandleChannelUpdate(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  ObjectGuid caster;
  if (!r.ReadPackedGuid(caster)) return false;
  std::uint32_t time_raw;
  if (!r.ReadU32(time_raw)) return false;
  auto time_remaining = static_cast<std::int32_t>(time_raw);

  if (channel_info_.has_value() &&
      channel_info_->caster == caster) {
    channel_info_->time_remaining = time_remaining;
  } else {
  SpellChannelInfo info;
  info.caster = caster;
  info.time_remaining = time_remaining;
    channel_info_ = info;
  }
  return true;
}

net::wotlk::WorldPacket AuraManager::BuildCancelAura(
    std::uint32_t spell_id) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_CANCEL_AURA);
  pkt.AppendU32(spell_id);
  return pkt;
}

net::wotlk::WorldPacket AuraManager::BuildCancelCast(
    std::uint32_t spell_id) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_CANCEL_CAST);
  pkt.AppendU32(0);
  pkt.AppendU32(spell_id);
  return pkt;
}

net::wotlk::WorldPacket AuraManager::BuildCancelChannelling(
    std::uint32_t reason) {
  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::CMSG_CANCEL_CHANNELLING);
  pkt.AppendU32(reason);
  return pkt;
}

const std::vector<AuraSlotInfo>& AuraManager::GetAuras(
    std::uint64_t guid) const {
  auto it = auras_.find(guid);
  if (it != auras_.end()) return it->second;
  return kEmptyAuras;
}

const AuraSlotInfo* AuraManager::FindAuraBySpellId(
    const std::uint64_t guid, const std::uint32_t spell_id) const {
  if (spell_id == 0) {
    return nullptr;
  }

  const auto it = auras_.find(guid);
  if (it == auras_.end()) {
    return nullptr;
  }

  const AuraSlotInfo* match = nullptr;
  for (const auto& aura : it->second) {
    if (aura.spell_id != spell_id) {
      continue;
    }
    if (match == nullptr || aura.slot < match->slot) {
      match = &aura;
    }
  }

  return match;
}

void AuraManager::Clear() {
  auras_.clear();
  last_update_diff_ = {};
  modifiers_.clear();
  cast_failed_.reset();
  spell_failure_.reset();
  spell_delayed_.reset();
  channel_info_.reset();
  cooldown_event_.reset();
  clear_cooldown_spell_ = 0;
}

}

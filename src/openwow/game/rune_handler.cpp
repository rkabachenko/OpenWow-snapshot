
#include "openwow/game/rune_handler.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::game {

namespace {

constexpr float kRuneByteScale = 1.0f / 255.0f;
constexpr float kMinRuneRegenRate = 0.01f;
constexpr std::uint32_t kSpellRuneCooldownWindowMs = 10000u;
constexpr int kLuaRuneSlotLimit = 8;
static_assert(kClientTrackedRuneSlots == kLuaRuneSlotLimit);

constexpr std::array<RuneType, kClientTrackedRuneSlots> kDefaultTrackedRuneLayout = {
    RuneType::kBlood,  RuneType::kBlood,
    RuneType::kUnholy, RuneType::kUnholy,
    RuneType::kFrost,  RuneType::kFrost,
    RuneType::kBlood,  RuneType::kBlood,
};

std::uint32_t TrackedRuneCount(const std::uint32_t active_rune_count) {
  return std::min<std::uint32_t>(active_rune_count, kClientTrackedRuneSlots);
}

std::uint32_t TruncateRetailNonNegativeMilliseconds(const double value) {

  if (value <= 0.0) {
    return 0u;
  }

  constexpr auto kSignedU32Boundary = static_cast<double>(std::uint64_t{1} << 31);
  constexpr auto kUnsignedU32Boundary = static_cast<double>(std::uint64_t{1} << 32);
  constexpr auto kSignBit = static_cast<std::uint32_t>(
      std::numeric_limits<std::int32_t>::min());

  if (std::isnan(value)) {
    return kSignBit;
  }
  if (value >= kUnsignedU32Boundary) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  if (value >= kSignedU32Boundary) {
    return static_cast<std::uint32_t>(
               static_cast<std::int32_t>(std::trunc(value - kSignedU32Boundary))) |
           kSignBit;
  }
  return static_cast<std::uint32_t>(
      static_cast<std::int32_t>(std::trunc(value)));
}

bool HasReadyTickElapsed(const std::uint32_t current_time,
                         const std::uint32_t ready_at_time) {
  return static_cast<std::int32_t>(current_time - ready_at_time) >= 0;
}

std::uint8_t ScaleRequiredRuneCount(const std::uint8_t base_count,
                                    const std::int32_t cost_pct) {
  if (base_count == 0 || cost_pct <= 100) {
    return base_count;
  }

  return static_cast<std::uint8_t>(
      (static_cast<std::int32_t>(base_count) * cost_pct) / 100);
}

struct RuneDeficits {
  std::uint8_t blood = 0;
  std::uint8_t unholy = 0;
  std::uint8_t frost = 0;
  std::uint8_t death = 0;
};

std::uint32_t SelectRequiredRuneReadyTick(const std::uint8_t deficit_count,
                                          const std::uint32_t first_ready_tick,
                                          const std::uint32_t second_ready_tick) {
  if (deficit_count == 1) {
    if (first_ready_tick == 0) {
      return second_ready_tick;
    }
    if (second_ready_tick != 0 && first_ready_tick >= second_ready_tick) {
      return second_ready_tick;
    }
    return first_ready_tick;
  }

  if (deficit_count == 2) {
    return first_ready_tick <= second_ready_tick ? second_ready_tick
                                                 : first_ready_tick;
  }

  return 0;
}

}

RuneHandler::RuneHandler() {
  ResetToDefault();
}

bool RuneHandler::HandleConvertRune(const std::uint8_t* data,
                                    std::size_t len) {
  PacketReader r(data, len);
  std::uint8_t index = 0, rune_type = 0;
  if (!r.ReadU8(index) || !r.ReadU8(rune_type)) return false;
  if (index >= kMaxRunes) return false;

  RuneType new_type = static_cast<RuneType>(rune_type);
  RuneType old_type = runes_[index].type;

  runes_[index].type = new_type;
  ++convert_count_;

  if (old_type != new_type) {
    diagnostics::Log(diagnostics::LogLevel::kDebug,
              "RuneHandler: converted rune " + std::to_string(index) +
              " from " + RuneTypeName(old_type) + " to " +
              RuneTypeName(new_type));
  }
  return true;
}

bool RuneHandler::HandleResyncRunes(const std::uint8_t* data,
                                    std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t count = 0;
  if (!r.ReadU32(count)) return false;

  active_rune_count_ = TrackedRuneCount(count);

  for (std::uint32_t i = 0; i < count && i < kClientTrackedRuneSlots; ++i) {
    std::uint8_t type_val = 0, cd_byte = 0;
    if (!r.ReadU8(type_val) || !r.ReadU8(cd_byte)) return false;
    const auto type = static_cast<RuneType>(type_val);
    runes_[i].type = type;
    runes_[i].base_type = type;
    StampCooldownFromProgress(static_cast<int>(i),
                              ClampProgress(static_cast<float>(cd_byte) *
                                            kRuneByteScale));
  }
  ++resync_count_;

  diagnostics::Log(diagnostics::LogLevel::kDebug,
            "RuneHandler: resync " + std::to_string(count) + " runes");
  return true;
}

bool RuneHandler::HandleAddRunePower(const std::uint8_t* data,
                                     std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t mask = 0;
  if (!r.ReadU32(mask)) return false;

  for (std::uint32_t i = 0; i < TrackedRuneCount(active_rune_count_); ++i) {
    const std::uint32_t bit = 1u << i;
    if ((mask & bit) != 0 && (ready_mask_ & bit) == 0) {
      StampCooldownFromProgress(static_cast<int>(i), 1.0f);
    }
  }
  return true;
}

void RuneHandler::HandleSpellGoRuneData(
    const std::uint8_t before_mask,
    const std::uint8_t after_mask,
    const std::vector<std::uint8_t>& cooldown_bytes) {
  ready_mask_ = after_mask;

  std::array<float, kClientTrackedRuneSlots> slot_progress{};
  std::size_t cooldown_index = 0;
  for (std::uint32_t i = 0; i < kClientTrackedRuneSlots; ++i) {
    const std::uint32_t bit = 1u << i;
    if ((before_mask & bit) != 0 && (after_mask & bit) == 0 &&
        cooldown_index < cooldown_bytes.size()) {
      slot_progress[i] = ClampProgress(static_cast<float>(
          cooldown_bytes[cooldown_index]) * kRuneByteScale);
      ++cooldown_index;
    }
  }

  for (std::uint32_t i = 0; i < kClientTrackedRuneSlots; ++i) {
    const std::uint32_t bit = 1u << i;
    const bool had_rune_before = (before_mask & bit) != 0;
    const bool has_rune_after = (after_mask & bit) != 0;

    if (!had_rune_before || has_rune_after) {
      if ((!had_rune_before && has_rune_after) ||
          (has_rune_after && runes_[i].cooldown_start_ms != 0)) {
        ClearCooldown(static_cast<int>(i));
      }
      continue;
    }

    if (runes_[i].cooldown_start_ms == 0) {
      StampCooldownFromProgress(static_cast<int>(i), slot_progress[i]);
    }
  }
}

std::uint32_t RuneHandler::HandleRuneRegenRateChanged(const std::uint8_t rune_type,
                                                      const float old_regen_rate,
                                                      const float new_regen_rate) {
  if (old_regen_rate < kMinRuneRegenRate ||
      new_regen_rate < kMinRuneRegenRate) {
    return 0;
  }

  const auto now_ms = CurrentTimeMs();
  std::uint32_t adjusted_mask = 0;
  for (std::uint32_t i = 0; i < kClientTrackedRuneSlots; ++i) {
    const std::uint32_t bit = 1u << i;
    if (static_cast<std::uint8_t>(runes_[i].type) != rune_type ||
        (ready_mask_ & bit) != 0 ||
        runes_[i].cooldown_start_ms == 0) {
      continue;
    }

    float progress = static_cast<float>(now_ms - runes_[i].cooldown_start_ms) *
                     old_regen_rate * 0.001f;
    progress = ClampProgress(progress);

    const auto adjusted_start = static_cast<std::uint32_t>(
        now_ms - TruncateRetailNonNegativeMilliseconds(
                     progress * 1000.0 / new_regen_rate));
    runes_[i].cooldown_start_ms = adjusted_start;
    SetCooldownProgress(static_cast<int>(i), progress);
    UpdateReadyTick(static_cast<int>(i), progress, new_regen_rate);
    adjusted_mask |= bit;
  }

  return adjusted_mask;
}

void RuneHandler::SetClientTimeFn(ClientTimeFn fn) {
  client_time_fn_ = std::move(fn);
}

void RuneHandler::SetRuneRegenRateFn(RuneRegenRateFn fn) {
  rune_regen_rate_fn_ = std::move(fn);
}

static const RuneState kDummyRune{};

const RuneState& RuneHandler::GetRune(int index) const {
  if (index < 0 || index >= kClientTrackedRuneSlots) return kDummyRune;
  return runes_[static_cast<std::size_t>(index)];
}

RuneType RuneHandler::GetRuneType(int index) const {
  return GetRune(index).type;
}

RuneType RuneHandler::GetBaseRuneType(int index) const {
  return GetRune(index).base_type;
}

float RuneHandler::GetRuneCooldown(int index) const {
  return GetRune(index).cooldown_pct;
}

RuneHandler::CooldownQuery RuneHandler::GetLuaCooldown(const int index,
                                                       const float regen_rate) const {
  CooldownQuery result{};
  if (index < 0 || index >= kClientTrackedRuneSlots) {
    return result;
  }

  const auto& rune = runes_[static_cast<std::size_t>(index)];
  result.start_ms = rune.cooldown_start_ms;
  result.duration_seconds =
      regen_rate <= kMinRuneRegenRate ? 0.0f : (1.0f / regen_rate);
  result.ready = rune.cooldown_start_ms == 0;
  return result;
}

bool RuneHandler::IsLuaSlotValid(const int index) const {
  if (index < 0 || index >= kLuaRuneSlotLimit) {
    return false;
  }
  return active_rune_count_ == 0
             ? index < kLuaRuneSlotLimit
             : index < static_cast<int>(active_rune_count_);
}

std::uint32_t RuneHandler::GetReadyTickIfSpent(const int index) const {
  if (index < 0 || index >= kClientTrackedRuneSlots) {
    return 0;
  }

  const std::uint32_t bit = 1u << index;
  if ((ready_mask_ & bit) != 0) {
    return 0;
  }

  return runes_[static_cast<std::size_t>(index)].cooldown_ready_at_ms;
}

bool RuneHandler::IsRuneReady(int index) const {
  if (index < 0 || index >= kClientTrackedRuneSlots) return false;
  return (ready_mask_ & (1u << index)) != 0;
}

int RuneHandler::CountReadyRunes(RuneType type) const {
  int count = 0;
  for (std::uint32_t i = 0; i < TrackedRuneCount(active_rune_count_); ++i) {
    if (runes_[static_cast<std::size_t>(i)].type == type &&
        IsRuneReady(static_cast<int>(i))) {
      ++count;
    }
  }
  return count;
}

int RuneHandler::CountAllReady() const {
  int count = 0;
  for (std::uint32_t i = 0; i < TrackedRuneCount(active_rune_count_); ++i) {
    if (IsRuneReady(static_cast<int>(i))) ++count;
  }
  return count;
}

bool RuneHandler::IsDeathRune(int index) const {
  if (index < 0 || index >= kClientTrackedRuneSlots) return false;
  return runes_[static_cast<std::size_t>(index)].type == RuneType::kDeath;
}

bool RuneHandler::CanAfford(const RuneCost& cost,
                            const std::int32_t cost_pct) const {
  const RuneCost effective_cost = {
      ScaleRequiredRuneCount(cost.blood, cost_pct),
      ScaleRequiredRuneCount(cost.unholy, cost_pct),
      ScaleRequiredRuneCount(cost.frost, cost_pct),
  };

  int blood_ready  = 0;
  int unholy_ready = 0;
  int frost_ready  = 0;
  int death_ready  = 0;

  for (std::uint32_t i = 0; i < TrackedRuneCount(active_rune_count_); ++i) {
    if (!IsRuneReady(static_cast<int>(i))) continue;
    switch (runes_[static_cast<std::size_t>(i)].type) {
      case RuneType::kBlood:  ++blood_ready;  break;
      case RuneType::kUnholy: ++unholy_ready; break;
      case RuneType::kFrost:  ++frost_ready;  break;
      case RuneType::kDeath:  ++death_ready;  break;
    }
  }

  int death_available = death_ready;

  int blood_deficit = static_cast<int>(effective_cost.blood) - blood_ready;
  if (blood_deficit > 0) {
    death_available -= blood_deficit;
    if (death_available < 0) return false;
  }

  int unholy_deficit = static_cast<int>(effective_cost.unholy) - unholy_ready;
  if (unholy_deficit > 0) {
    death_available -= unholy_deficit;
    if (death_available < 0) return false;
  }

  int frost_deficit = static_cast<int>(effective_cost.frost) - frost_ready;
  if (frost_deficit > 0) {
    death_available -= frost_deficit;
    if (death_available < 0) return false;
  }

  return true;
}

std::optional<RuneHandler::SpellCooldownWindow> RuneHandler::PredictSpellCooldown(
    const RuneCost& cost, const std::int32_t cost_pct) const {
  const RuneCost effective_cost = {
      ScaleRequiredRuneCount(cost.blood, cost_pct),
      ScaleRequiredRuneCount(cost.unholy, cost_pct),
      ScaleRequiredRuneCount(cost.frost, cost_pct),
  };

  if (effective_cost.blood == 0 && effective_cost.unholy == 0 &&
      effective_cost.frost == 0) {
    return std::nullopt;
  }

  const int blood_ready = CountReadyRunes(RuneType::kBlood);
  const int unholy_ready = CountReadyRunes(RuneType::kUnholy);
  const int frost_ready = CountReadyRunes(RuneType::kFrost);
  const int death_ready = CountReadyRunes(RuneType::kDeath);

  RuneDeficits deficits{};
  deficits.blood = static_cast<std::uint8_t>(
      std::max(0, static_cast<int>(effective_cost.blood) - blood_ready));
  deficits.unholy = static_cast<std::uint8_t>(
      std::max(0, static_cast<int>(effective_cost.unholy) - unholy_ready));
  deficits.frost = static_cast<std::uint8_t>(
      std::max(0, static_cast<int>(effective_cost.frost) - frost_ready));

  const int total_deficit =
      static_cast<int>(deficits.blood) + static_cast<int>(deficits.unholy) +
      static_cast<int>(deficits.frost);
  if (total_deficit == 0 || death_ready >= total_deficit) {
    return std::nullopt;
  }
  deficits.death =
      static_cast<std::uint8_t>(std::max(0, total_deficit - death_ready));

  std::array<std::uint32_t, kClientTrackedRuneSlots> slot_ready_ticks{};
  std::uint32_t slowest_selected_tick = 0;
  int slowest_selected_slot = 0;

  for (int slot = 0; slot < kClientTrackedRuneSlots; ++slot) {
    const auto type = runes_[static_cast<std::size_t>(slot)].type;
    if (type == RuneType::kDeath) {
      continue;
    }

    const auto deficit_count = type == RuneType::kBlood   ? deficits.blood
                               : type == RuneType::kUnholy ? deficits.unholy
                                                           : deficits.frost;
    if (deficit_count == 0) {
      continue;
    }

    const auto ready_tick = GetReadyTickIfSpent(slot);
    slot_ready_ticks[static_cast<std::size_t>(slot)] = ready_tick;
    if (slowest_selected_tick == 0 ||
        static_cast<std::int32_t>(ready_tick - slowest_selected_tick) >= 0) {
      slowest_selected_tick = ready_tick;
      slowest_selected_slot = slot;
    }
  }

  for (int slot = 0; slot < kClientTrackedRuneSlots; ++slot) {
    if (runes_[static_cast<std::size_t>(slot)].type != RuneType::kDeath) {
      continue;
    }

    const auto ready_tick = GetReadyTickIfSpent(slot);
    if (ready_tick == 0) {
      continue;
    }

    if (slowest_selected_tick == 0) {
      slowest_selected_tick = ready_tick;
      slowest_selected_slot = slot;
      slot_ready_ticks[static_cast<std::size_t>(slot)] = ready_tick;
      continue;
    }

    if (static_cast<std::int32_t>(ready_tick - slowest_selected_tick) >= 0) {
      continue;
    }

    slot_ready_ticks[static_cast<std::size_t>(slowest_selected_slot)] =
        ready_tick;
    slowest_selected_tick = 0;
    slowest_selected_slot = 0;

    for (int scan_slot = 0; scan_slot < kClientTrackedRuneSlots; ++scan_slot) {
      if (runes_[static_cast<std::size_t>(scan_slot)].type == RuneType::kDeath) {
        continue;
      }

      const auto candidate_tick =
          slot_ready_ticks[static_cast<std::size_t>(scan_slot)];
      if (slowest_selected_tick == 0 ||
          static_cast<std::int32_t>(candidate_tick - slowest_selected_tick) >=
              0) {
        slowest_selected_tick = candidate_tick;
        slowest_selected_slot = scan_slot;
      }
    }
  }

  const auto blood_ready_tick = SelectRequiredRuneReadyTick(
      deficits.blood, slot_ready_ticks[0], slot_ready_ticks[1]);
  const auto unholy_ready_tick = SelectRequiredRuneReadyTick(
      deficits.unholy, slot_ready_ticks[2], slot_ready_ticks[3]);
  const auto frost_ready_tick = SelectRequiredRuneReadyTick(
      deficits.frost, slot_ready_ticks[4], slot_ready_ticks[5]);
  const auto ready_tick =
      std::max({blood_ready_tick, unholy_ready_tick, frost_ready_tick});
  if (ready_tick == 0) {
    return std::nullopt;
  }

  return SpellCooldownWindow{
      .start_tick_ms = ready_tick - kSpellRuneCooldownWindowMs,
      .duration_ms = kSpellRuneCooldownWindowMs,
      .ready_tick_ms = ready_tick,
  };
}

void RuneHandler::SetCooldownDuration(float seconds) {
  if (seconds > 0.0f) cooldown_duration_ = seconds;
}

float RuneHandler::GetCooldownDuration() const {
  return cooldown_duration_;
}

std::uint32_t RuneHandler::Update(float dt) {
  std::uint32_t ready_mask_delta = 0;

  if (dt > 0.0f) {
    const float regen_rate =
        (cooldown_duration_ > 0.0f) ? dt / cooldown_duration_ : 1.0f;

    for (std::uint32_t i = 0; i < TrackedRuneCount(active_rune_count_); ++i) {
      auto& rune = runes_[static_cast<std::size_t>(i)];
      if ((ready_mask_ & (1u << i)) == 0 && rune.cooldown_pct < 1.0f) {
        rune.cooldown_pct = std::min(1.0f, rune.cooldown_pct + regen_rate);
      }
    }
  }

  const auto now_ms = CurrentTimeMs();
  for (std::uint32_t i = 0; i < TrackedRuneCount(active_rune_count_); ++i) {
    const std::uint32_t bit = 1u << i;
    if ((ready_mask_ & bit) != 0) {
      continue;
    }

    const auto ready_at_ms = GetReadyTickIfSpent(static_cast<int>(i));
    if (ready_at_ms == 0 || !HasReadyTickElapsed(now_ms, ready_at_ms)) {
      continue;
    }

    ready_mask_ |= bit;
    ready_mask_delta |= bit;
    ClearCooldown(static_cast<int>(i));
  }

  return ready_mask_delta;
}

void RuneHandler::SpendRune(int index) {
  if (index < 0 || index >= kMaxRunes) return;
  ready_mask_ &= ~(1u << index);
  StampCooldownFromProgress(index, 0.0f);
}

const char* RuneHandler::RuneTypeName(RuneType type) {
  switch (type) {
    case RuneType::kBlood:  return "Blood";
    case RuneType::kUnholy: return "Unholy";
    case RuneType::kFrost:  return "Frost";
    case RuneType::kDeath:  return "Death";
  }
  return "Unknown";
}

void RuneHandler::ResetToDefault() {
  for (int i = 0; i < kClientTrackedRuneSlots; ++i) {
    runes_[static_cast<std::size_t>(i)].type =
        kDefaultTrackedRuneLayout[static_cast<std::size_t>(i)];
    runes_[static_cast<std::size_t>(i)].base_type =
        kDefaultTrackedRuneLayout[static_cast<std::size_t>(i)];
    runes_[static_cast<std::size_t>(i)].cooldown_pct = 1.0f;
    runes_[static_cast<std::size_t>(i)].cooldown_start_ms = 0;
    runes_[static_cast<std::size_t>(i)].cooldown_ready_at_ms = 0;
  }
  ready_mask_ = 0xFFFFFFFFu;
  convert_count_ = 0;
  resync_count_ = 0;
  active_rune_count_ = kMaxRunes;
}

void RuneHandler::Clear() {
  ResetToDefault();
  cooldown_duration_ = 10.0f;
}

void RuneHandler::SetCooldownProgress(const int index, const float progress) {
  runes_[static_cast<std::size_t>(index)].cooldown_pct = ClampProgress(progress);
}

void RuneHandler::ClearCooldown(const int index) {
  auto& rune = runes_[static_cast<std::size_t>(index)];
  rune.cooldown_start_ms = 0;
  rune.cooldown_ready_at_ms = 0;
  rune.cooldown_pct = 1.0f;
}

void RuneHandler::StampCooldownFromProgress(const int index, const float progress) {
  auto& rune = runes_[static_cast<std::size_t>(index)];
  const float clamped_progress = ClampProgress(progress);
  rune.cooldown_start_ms =
      CurrentTimeMs() - TruncateRetailNonNegativeMilliseconds(
                            cooldown_duration_ * 1000.0 * clamped_progress);
  SetCooldownProgress(index, clamped_progress);
  UpdateReadyTickFromCurrentRuneType(index, clamped_progress);
}

void RuneHandler::UpdateReadyTickFromCurrentRuneType(const int index,
                                                     const float progress) {
  UpdateReadyTick(index, progress, CurrentRuneRegenRate(index));
}

void RuneHandler::UpdateReadyTick(const int index, const float progress,
                                  const float regen_rate) {
  auto& rune = runes_[static_cast<std::size_t>(index)];
  if (regen_rate >= kMinRuneRegenRate || std::isnan(regen_rate)) {
    const float remaining_ms =
        ((1.0f - ClampProgress(progress)) / regen_rate) * 1000.0f;
    rune.cooldown_ready_at_ms =
        CurrentTimeMs() + TruncateRetailNonNegativeMilliseconds(remaining_ms);
  } else {
    rune.cooldown_ready_at_ms = 0;
  }
}

float RuneHandler::CurrentRuneRegenRate(const int index) const {
  if (index < 0 || index >= kClientTrackedRuneSlots) {
    return 0.0f;
  }
  if (rune_regen_rate_fn_) {
    return rune_regen_rate_fn_(runes_[static_cast<std::size_t>(index)].type);
  }
  return cooldown_duration_ > 0.0f ? (1.0f / cooldown_duration_) : 0.0f;
}

std::uint32_t RuneHandler::CurrentTimeMs() const {
  return client_time_fn_ ? client_time_fn_() : 0;
}

float RuneHandler::ClampProgress(const float progress) {
  return std::clamp(progress, 0.0f, 1.0f);
}

}

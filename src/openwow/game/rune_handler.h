
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "openwow/game/packet_reader.h"

namespace openwow::game {

enum class RuneType : std::uint8_t {
  kBlood  = 0,
  kUnholy = 1,
  kFrost  = 2,
  kDeath  = 3,
};

static constexpr int kMaxRunes = 6;
static constexpr int kClientTrackedRuneSlots = 8;

inline constexpr std::array<RuneType, kMaxRunes> kDefaultRuneLayout = {
  RuneType::kBlood, RuneType::kBlood,
  RuneType::kUnholy, RuneType::kUnholy,
  RuneType::kFrost, RuneType::kFrost,
};

struct RuneState {
  RuneType type = RuneType::kBlood;
  RuneType base_type = RuneType::kBlood;
  float cooldown_pct = 1.0f;
  std::uint32_t cooldown_start_ms = 0;
  std::uint32_t cooldown_ready_at_ms = 0;
};

struct RuneCost {
  std::uint8_t blood  = 0;
  std::uint8_t unholy = 0;
  std::uint8_t frost  = 0;

};

class RuneHandler {
 public:
  using ClientTimeFn = std::function<std::uint32_t()>;
  using RuneRegenRateFn = std::function<float(RuneType)>;

  RuneHandler();

  struct CooldownQuery {
    std::uint32_t start_ms = 0;
    float duration_seconds = 0.0f;
    bool ready = true;
  };

  struct SpellCooldownWindow {
    std::uint32_t start_tick_ms = 0;
    std::uint32_t duration_ms = 0;
    std::uint32_t ready_tick_ms = 0;
  };

  bool HandleConvertRune(const std::uint8_t* data, std::size_t len);
  bool HandleResyncRunes(const std::uint8_t* data, std::size_t len);
  bool HandleAddRunePower(const std::uint8_t* data, std::size_t len);
  void HandleSpellGoRuneData(std::uint8_t before_mask, std::uint8_t after_mask,
                             const std::vector<std::uint8_t>& cooldown_bytes);
  [[nodiscard]] std::uint32_t HandleRuneRegenRateChanged(
      std::uint8_t rune_type, float old_regen_rate, float new_regen_rate);
  void SetClientTimeFn(ClientTimeFn fn);
  void SetRuneRegenRateFn(RuneRegenRateFn fn);

  [[nodiscard]] const std::array<RuneState, kClientTrackedRuneSlots>& runes() const {
    return runes_;
  }
  [[nodiscard]] std::uint32_t ready_mask() const { return ready_mask_; }
  [[nodiscard]] int convert_count() const { return convert_count_; }
  [[nodiscard]] int resync_count() const { return resync_count_; }
  [[nodiscard]] std::uint32_t active_rune_count() const { return active_rune_count_; }

  [[nodiscard]] const RuneState& GetRune(int index) const;

  [[nodiscard]] RuneType GetRuneType(int index) const;
  [[nodiscard]] RuneType GetBaseRuneType(int index) const;

  [[nodiscard]] float GetRuneCooldown(int index) const;
  [[nodiscard]] CooldownQuery GetLuaCooldown(int index, float regen_rate) const;
  [[nodiscard]] bool IsLuaSlotValid(int index) const;
  [[nodiscard]] std::uint32_t GetReadyTickIfSpent(int index) const;

  [[nodiscard]] bool IsRuneReady(int index) const;

  [[nodiscard]] int CountReadyRunes(RuneType type) const;

  [[nodiscard]] int CountAllReady() const;

  [[nodiscard]] bool IsDeathRune(int index) const;

  [[nodiscard]] bool CanAfford(const RuneCost& cost,
                               std::int32_t cost_pct = 100) const;
  [[nodiscard]] std::optional<SpellCooldownWindow> PredictSpellCooldown(
      const RuneCost& cost, std::int32_t cost_pct = 100) const;

  void SetCooldownDuration(float seconds);
  [[nodiscard]] float GetCooldownDuration() const;

  [[nodiscard]] std::uint32_t Update(float dt);

  void SpendRune(int index);

  [[nodiscard]] static const char* RuneTypeName(RuneType type);

  void ResetToDefault();

  void Clear();

 private:
  void SetCooldownProgress(int index, float progress);
  void ClearCooldown(int index);
  void StampCooldownFromProgress(int index, float progress);
  void UpdateReadyTickFromCurrentRuneType(int index, float progress);
  void UpdateReadyTick(int index, float progress, float regen_rate);
  [[nodiscard]] float CurrentRuneRegenRate(int index) const;
  [[nodiscard]] std::uint32_t CurrentTimeMs() const;
  [[nodiscard]] static float ClampProgress(float progress);

  std::array<RuneState, kClientTrackedRuneSlots> runes_{};
  std::uint32_t ready_mask_ = 0xFFFFFFFFu;
  int convert_count_ = 0;
  int resync_count_ = 0;
  std::uint32_t active_rune_count_ = kMaxRunes;

  float cooldown_duration_ = 10.0f;

  ClientTimeFn client_time_fn_;
  RuneRegenRateFn rune_regen_rate_fn_;
};

}

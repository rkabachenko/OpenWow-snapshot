#pragma once

#include "openwow/game/rune_handler.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct RuneSlotInfo {
  RuneType type{RuneType::kBlood};
  RuneType baseType{RuneType::kBlood};
  bool isReady{true};
  float cooldownRemaining{0.0f};
  float cooldownTotal{0.0f};
};

class RuneSystem {
 public:
  RuneSystem();

  void SetRune(std::uint32_t slotIndex, RuneType baseType);
  [[nodiscard]] std::optional<RuneSlotInfo> GetRune(
      std::uint32_t slotIndex) const;
  [[nodiscard]] std::vector<RuneSlotInfo> GetAllRunes() const;

  void SetRuneReady(std::uint32_t slotIndex, bool ready);
  [[nodiscard]] bool IsRuneReady(std::uint32_t slotIndex) const;

  void StartCooldown(std::uint32_t slotIndex, float duration);
  [[nodiscard]] float GetCooldownRemaining(std::uint32_t slotIndex) const;
  [[nodiscard]] float GetCooldownProgress(std::uint32_t slotIndex) const;

  void ConvertToDeath(std::uint32_t slotIndex);
  void RevertFromDeath(std::uint32_t slotIndex);

  [[nodiscard]] std::uint32_t GetReadyRuneCount(RuneType type) const;
  [[nodiscard]] std::uint32_t GetTotalRuneCount(RuneType type) const;

  void Update(float dt);

  [[nodiscard]] static std::uint32_t GetRuneColor(RuneType type);
  [[nodiscard]] static std::string GetRuneTypeName(RuneType type);

  void InitializeDefaultLayout();

  void Reset();

 private:
  std::array<RuneSlotInfo, kMaxRunes> runes_;
};

}

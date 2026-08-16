#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace openwow::game {

struct CooldownInfo {
  std::uint32_t id = 0;
  std::uint32_t duration = 0;
  std::uint32_t start_time = 0;
  std::uint32_t category_id = 0;
  std::uint32_t category_duration = 0;

  [[nodiscard]] float GetRemainingTime(std::uint32_t current_time) const;

  [[nodiscard]] float GetProgress(std::uint32_t current_time) const;

  [[nodiscard]] bool IsReady(std::uint32_t current_time) const;
};

class CooldownTracker {
 public:
  static CooldownTracker& Get();

  void SetSpellCooldown(std::uint32_t spell_id, std::uint32_t duration,
                        std::uint32_t start_time = 0,
                        std::uint32_t category_id = 0);
  void SetCategoryCooldown(std::uint32_t category_id, std::uint32_t duration,
                           std::uint32_t start_time = 0);
  void ClearSpellCooldown(std::uint32_t spell_id);
  void ClearAllCooldowns();

  void AdjustSpellCooldown(std::uint32_t spell_id, std::int32_t delta_ms);

  [[nodiscard]] const CooldownInfo* GetSpellCooldown(
      std::uint32_t spell_id) const;
  [[nodiscard]] float GetSpellCooldownRemaining(
      std::uint32_t spell_id, std::uint32_t current_time) const;

  [[nodiscard]] float GetCategoryCooldownRemaining(
      std::uint32_t category_id, std::uint32_t current_time) const;
  [[nodiscard]] bool IsSpellReady(std::uint32_t spell_id,
                                  std::uint32_t current_time) const;

  void SetItemCooldown(std::uint32_t item_id, std::uint32_t duration,
                       std::uint32_t start_time = 0);
  [[nodiscard]] const CooldownInfo* GetItemCooldown(
      std::uint32_t item_id) const;
  [[nodiscard]] float GetItemCooldownRemaining(
      std::uint32_t item_id, std::uint32_t current_time) const;
  [[nodiscard]] bool IsItemReady(std::uint32_t item_id,
                                 std::uint32_t current_time) const;

  [[nodiscard]] bool IsItemUseReady(std::uint32_t spell_id,
                                    std::uint32_t item_id,
                                    std::uint32_t category_id,
                                    std::uint32_t current_time) const;

  void ForEachCooldown(
      const std::function<void(std::uint32_t id, const CooldownInfo&)>& fn)
      const;

  void PruneExpiredCooldowns(std::uint32_t current_time);

  void Reset();

 private:
  CooldownTracker() = default;

  std::unordered_map<std::uint32_t, CooldownInfo> spell_cooldowns_;
  std::unordered_map<std::uint32_t, CooldownInfo> item_cooldowns_;
  std::unordered_map<std::uint32_t, CooldownInfo> category_cooldowns_;
  mutable std::mutex mutex_;
};

}

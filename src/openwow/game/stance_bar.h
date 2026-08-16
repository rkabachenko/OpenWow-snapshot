
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

namespace StanceId {
  inline constexpr uint32_t None            = 0;
  inline constexpr uint32_t BattleStance    = 1;
  inline constexpr uint32_t DefensiveStance = 2;
  inline constexpr uint32_t BerserkerStance = 3;
  inline constexpr uint32_t BearForm        = 5;
  inline constexpr uint32_t AquaticForm     = 6;
  inline constexpr uint32_t CatForm         = 7;
  inline constexpr uint32_t TravelForm      = 8;
  inline constexpr uint32_t TreeOfLife       = 14;
  inline constexpr uint32_t MoonkinForm     = 16;
  inline constexpr uint32_t Stealth         = 26;
  inline constexpr uint32_t FlightForm      = 27;
  inline constexpr uint32_t ShadowForm      = 28;
  inline constexpr uint32_t SwiftFlightForm = 29;

  inline constexpr uint32_t GhostWolf       = 8;
}

struct StanceSlot {
  uint32_t slotIndex = 0;
  uint32_t stanceId  = StanceId::None;
  uint32_t spellId   = 0;
  bool     isActive  = false;
  bool     isUsable  = true;
  float    cooldown  = 0.0f;
};

class StanceBar {
 public:
  void SetSlots(const std::vector<StanceSlot>& slots);
  [[nodiscard]] const std::vector<StanceSlot>& GetSlots() const;
  [[nodiscard]] uint32_t GetSlotCount() const;
  [[nodiscard]] std::optional<StanceSlot> GetSlot(uint32_t index) const;

  void SetActive(uint32_t stanceId);
  [[nodiscard]] uint32_t GetActive() const;
  [[nodiscard]] bool IsInStance() const;
  [[nodiscard]] int32_t GetActiveSlotIndex() const;

  void SetCooldown(uint32_t slotIndex, float remaining);
  [[nodiscard]] float GetCooldown(uint32_t slotIndex) const;

  [[nodiscard]] bool IsUsable(uint32_t slotIndex) const;
  void SetUsable(uint32_t slotIndex, bool usable);

  [[nodiscard]] static std::string GetStanceName(uint32_t stanceId);

  void Update(float dt);

  [[nodiscard]] bool IsVisible() const;
  void SetVisible(bool visible);

  void Reset();

 private:
  std::vector<StanceSlot> slots_;
  uint32_t                activeStanceId_ = StanceId::None;
  bool                    visible_        = false;
};

}

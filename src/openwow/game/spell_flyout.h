
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class FlyoutDirection : uint8_t {
  Up    = 0,
  Down  = 1,
  Left  = 2,
  Right = 3,
};

struct FlyoutSlot {
  uint32_t spellId           = 0;
  bool     isKnown           = false;
  float    cooldownRemaining = 0.0f;
};

struct FlyoutData {
  std::vector<FlyoutSlot> slots;
  FlyoutDirection         direction = FlyoutDirection::Up;
};

class SpellFlyout {
 public:
  uint32_t CreateFlyout(uint32_t flyoutId, const std::vector<FlyoutSlot>& slots,
                        FlyoutDirection dir);
  void RemoveFlyout(uint32_t flyoutId);

  [[nodiscard]] std::optional<FlyoutData> GetFlyout(uint32_t flyoutId) const;
  [[nodiscard]] uint32_t GetFlyoutCount() const;

  void SetSlots(uint32_t flyoutId, const std::vector<FlyoutSlot>& slots);
  [[nodiscard]] std::vector<FlyoutSlot> GetSlots(uint32_t flyoutId) const;
  [[nodiscard]] uint32_t GetSlotCount(uint32_t flyoutId) const;

  void SetDirection(uint32_t flyoutId, FlyoutDirection dir);
  [[nodiscard]] FlyoutDirection GetDirection(uint32_t flyoutId) const;

  void SetCooldown(uint32_t flyoutId, uint32_t slotIndex, float remaining);

  [[nodiscard]] bool IsOpen(uint32_t flyoutId) const;
  void OpenFlyout(uint32_t flyoutId);
  void CloseAllFlyouts();
  [[nodiscard]] std::optional<uint32_t> GetOpenFlyout() const;

  void Update(float dt);

  void Reset();

 private:
  struct FlyoutEntry {
    std::vector<FlyoutSlot> slots;
    FlyoutDirection         direction = FlyoutDirection::Up;
  };

  std::unordered_map<uint32_t, FlyoutEntry> flyouts_;
  std::optional<uint32_t>                   openFlyoutId_;
};

}

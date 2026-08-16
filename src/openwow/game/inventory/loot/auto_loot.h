
#pragma once

#include <cstdint>

namespace openwow::game {

enum class AutoLootMode : std::uint8_t {
  Disabled = 0,
  KeyModified = 1,
  Always = 2,
};

class AutoLootConfig {
 public:
  void SetMode(AutoLootMode mode);
  [[nodiscard]] AutoLootMode GetMode() const;
  void SetModifierKey(std::uint8_t key);
  [[nodiscard]] std::uint8_t GetModifierKey() const;

  [[nodiscard]] bool ShouldAutoLoot(bool modifierPressed) const;

  void SetAutoLootCorpse(bool v);
  [[nodiscard]] bool GetAutoLootCorpse() const;
  void SetAutoLootQuest(bool v);
  [[nodiscard]] bool GetAutoLootQuest() const;
  void SetAutoLootGather(bool v);
  [[nodiscard]] bool GetAutoLootGather() const;
  void SetAutoLootFishing(bool v);
  [[nodiscard]] bool GetAutoLootFishing() const;

  void SetSkipJunk(bool v);
  [[nodiscard]] bool GetSkipJunk() const;
  void SetMinQuality(std::uint8_t quality);
  [[nodiscard]] std::uint8_t GetMinQuality() const;

  [[nodiscard]] bool IsEnabled() const;

  void Reset();

 private:
  AutoLootMode mode_ = AutoLootMode::Disabled;
  std::uint8_t modifierKey_ = 1;
  bool autoCorpse_ = false;
  bool autoQuest_ = false;
  bool autoGather_ = false;
  bool autoFishing_ = false;
  bool skipJunk_ = false;
  std::uint8_t minQuality_ = 0;
};

}

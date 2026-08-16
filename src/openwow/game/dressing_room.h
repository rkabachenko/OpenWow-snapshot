
#pragma once

#include "openwow/game/object_guid.h"

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

namespace openwow::game {

class DressingRoom {
 public:
  static DressingRoom& Get();

  static constexpr std::uint32_t kNumPreviewSlots = 19;

  void Open();
  void Close();
  [[nodiscard]] bool IsOpen() const;

  void SetModelUnit(ObjectGuid guid);
  [[nodiscard]] ObjectGuid GetModelUnit() const;

  void EquipItem(std::uint32_t slot_id, std::uint32_t item_id);
  void UnequipItem(std::uint32_t slot_id);
  [[nodiscard]] std::uint32_t GetPreviewItem(std::uint32_t slot_id) const;

  void ResetToEquipped();
  void Undress();
  [[nodiscard]] bool IsUndressed() const;
  [[nodiscard]] bool HasChanges() const;

  [[nodiscard]] std::uint32_t GetNumPreviewSlots() const { return kNumPreviewSlots; }

  void ForEachPreviewItem(
      const std::function<void(std::uint32_t slot, std::uint32_t item)>& fn) const;

  void SetBackground(std::uint32_t bg_id);
  [[nodiscard]] std::uint32_t GetBackground() const;

  [[nodiscard]] static bool CanPreviewItem(std::uint32_t item_id);

  void SetActualEquipped(std::uint32_t slot_id, std::uint32_t item_id);

  void Reset();

 private:
  DressingRoom() = default;

  mutable std::mutex mutex_;
  bool open_{false};
  ObjectGuid model_unit_;
  std::uint32_t background_id_{0};

  std::array<std::uint32_t, kNumPreviewSlots> preview_items_{};
  std::array<std::uint32_t, kNumPreviewSlots> actual_items_{};
};

}

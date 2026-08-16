#pragma once

#include "openwow/game/actions/macros/model/macro_document.h"
#include "openwow/game/actions/macros/model/macro_id.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::game {

class MacroCatalog;

namespace actions::macros {

class MacroSlotIndex {
 public:
  static constexpr std::size_t kSlotCount = 72;

  [[nodiscard]] static constexpr std::optional<MacroSlotIndex> FromZeroBased(
      const std::size_t value) noexcept {
    return value < kSlotCount
               ? std::optional<MacroSlotIndex>(MacroSlotIndex(value))
               : std::nullopt;
  }

  [[nodiscard]] constexpr std::size_t value() const noexcept {
    return value_;
  }

 private:
  explicit constexpr MacroSlotIndex(const std::size_t value) noexcept
      : value_(value) {}

  std::size_t value_;
};

class MacroStore {
 private:
  friend class ::openwow::game::MacroCatalog;

  static constexpr std::size_t kSlotCount = 72;
  static constexpr std::size_t kAccountSlotCount = 36;
  static constexpr std::size_t kCharacterSlotCount = 18;
  static constexpr std::size_t kCharacterSlotOffset = 36;

  [[nodiscard]] const MacroDocument* Find(MacroId id) const;
  [[nodiscard]] MacroDocument* FindMutable(MacroId id);
  [[nodiscard]] const MacroDocument* FindByName(
      std::string_view name) const;
  [[nodiscard]] std::optional<MacroSlotIndex> FindSlot(MacroId id) const;
  [[nodiscard]] std::optional<MacroDocument> AtSlot(
      MacroSlotIndex slot) const;
  bool Update(
      MacroId id,
      const std::function<void(MacroDocument&)>& update);
  bool UpdateAtSlot(
      MacroSlotIndex slot,
      const std::function<void(MacroDocument&)>& update);
  [[nodiscard]] std::vector<MacroDocument> Snapshot(
      MacroScope scope) const;
  void PromoteLookup(MacroId id);
  void RemoveLookup(MacroId id);
  void RebuildSlots();

  std::array<std::optional<MacroId>, kSlotCount> slots_{};
  std::unordered_map<MacroId, MacroDocument> documents_;
  std::vector<MacroId> lookup_order_;
  std::uint32_t account_count_{0};
  std::uint32_t character_count_{0};
  MacroId next_id_{1};
  bool dirty_{false};
};

}
}

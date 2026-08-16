#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace openwow::game::actions {

class ActionSlot {
 public:
  static constexpr std::size_t kCount = 144;

  [[nodiscard]] static constexpr std::optional<ActionSlot> FromZeroBased(
      std::size_t value) noexcept {
    if (value >= kCount) {
      return std::nullopt;
    }
    return ActionSlot(value);
  }

  [[nodiscard]] static constexpr std::optional<ActionSlot> FromLuaIndex(
      std::int64_t value) noexcept {
    if (value < 1 || value > static_cast<std::int64_t>(kCount)) {
      return std::nullopt;
    }
    return ActionSlot(static_cast<std::size_t>(value - 1));
  }

  [[nodiscard]] constexpr std::size_t zero_based() const noexcept {
    return value_;
  }

  [[nodiscard]] constexpr std::uint16_t lua_index() const noexcept {
    return static_cast<std::uint16_t>(value_ + 1);
  }

  [[nodiscard]] constexpr std::uint8_t wire_value() const noexcept {
    return static_cast<std::uint8_t>(value_);
  }

  auto operator<=>(const ActionSlot&) const = default;

 private:
  explicit constexpr ActionSlot(std::size_t value) noexcept : value_(value) {}

  std::size_t value_;
};

enum class ActionKind : std::uint8_t {
  kSpell = 0x00,
  kClick = 0x01,
  kPet = 0x10,
  kEquipmentSet = 0x20,
  kMacro = 0x40,
  kCompanionMacro = 0x41,
  kItem = 0x80,
  kCompanion = 0xC0,
};

class Action {
 public:

  constexpr Action() noexcept = default;

  [[nodiscard]] static constexpr Action Empty() noexcept { return {}; }

  [[nodiscard]] static constexpr std::optional<Action> Create(
      ActionKind kind, std::uint32_t identifier) noexcept {
    if (identifier == 0 || identifier > PayloadMask(kind)) {
      return std::nullopt;
    }
    return Action(kind, identifier);
  }

  [[nodiscard]] static constexpr Action Decode(std::uint32_t packed) noexcept {
    if (packed == 0) {
      return Empty();
    }

    const auto kind = DecodeKind(packed);
    const auto identifier = packed & PayloadMask(kind);
    return identifier == 0 ? Empty() : Action(kind, identifier);
  }

  [[nodiscard]] constexpr std::uint32_t Encode() const noexcept {
    if (empty()) {
      return 0;
    }
    return (static_cast<std::uint32_t>(kind_) << 24) | identifier_;
  }

  [[nodiscard]] constexpr bool empty() const noexcept {
    return identifier_ == 0;
  }

  [[nodiscard]] constexpr ActionKind kind() const noexcept { return kind_; }

  [[nodiscard]] constexpr std::uint32_t identifier() const noexcept {
    return identifier_;
  }

 auto operator<=>(const Action&) const = default;

 private:
  constexpr Action(ActionKind kind, std::uint32_t identifier) noexcept
      : kind_(kind), identifier_(identifier) {}

  [[nodiscard]] static constexpr ActionKind DecodeKind(
      std::uint32_t packed) noexcept {
    switch ((packed >> 28) & 0xFu) {
      case 0x1:
        return ActionKind::kPet;
      case 0x2:
      case 0x3:
        return ActionKind::kEquipmentSet;
      case 0x4:
      case 0x5:
      case 0x6:
      case 0x7:
        return ActionKind::kMacro;
      case 0x8:
      case 0x9:
      case 0xA:
      case 0xB:
        return ActionKind::kItem;
      case 0xC:
      case 0xD:
      case 0xE:
      case 0xF:
        return ActionKind::kCompanion;
      default:
        return ActionKind::kSpell;
    }
  }

  [[nodiscard]] static constexpr std::uint32_t PayloadMask(
      ActionKind kind) noexcept {
    switch (kind) {
      case ActionKind::kSpell:
      case ActionKind::kPet:
        return 0x0FFFFFFFu;
      case ActionKind::kClick:
      case ActionKind::kCompanionMacro:
        return 0x00FFFFFFu;
      case ActionKind::kEquipmentSet:
        return 0x1FFFFFFFu;
      case ActionKind::kMacro:
      case ActionKind::kCompanion:
        return 0x3FFFFFFFu;
      case ActionKind::kItem:
        return 0x7FFFFFFFu;
    }
    return 0;
  }

  ActionKind kind_{ActionKind::kSpell};
  std::uint32_t identifier_{0};
};

enum class ActionAssignmentSyncState : std::uint8_t {
  kInitial = 0,
  kUpdate = 1,
  kBeginSync = 2,
};

class ActionAssignments {
 public:
  using Storage = std::array<Action, ActionSlot::kCount>;

  [[nodiscard]] const Action& Get(ActionSlot slot) const noexcept;
  [[nodiscard]] bool Assign(ActionSlot slot, Action action) noexcept;
  [[nodiscard]] bool Clear(ActionSlot slot) noexcept;
  [[nodiscard]] bool ClearAll() noexcept;
  void Reset() noexcept;
  void MarkReferencedContentChanged() noexcept { ++revision_; }

  [[nodiscard]] bool ApplyServerSnapshot(
      ActionAssignmentSyncState state,
      std::span<const std::uint32_t, ActionSlot::kCount> packed_actions) noexcept;
  [[nodiscard]] bool ApplyServerSnapshot(
      ActionAssignmentSyncState state, const Storage& actions) noexcept;
  void BeginServerSync() noexcept;

  [[nodiscard]] std::vector<ActionSlot> SlotsReferencing(
      ActionKind kind, std::uint32_t identifier) const;
  [[nodiscard]] std::vector<std::uint32_t> MacroIdentifiers() const;

  [[nodiscard]] ActionAssignmentSyncState sync_state() const noexcept {
    return sync_state_;
  }
  [[nodiscard]] bool server_sync_pending() const noexcept {
    return server_sync_pending_;
  }
  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
  [[nodiscard]] const Storage& values() const noexcept { return values_; }

 private:
  Storage values_{};
  ActionAssignmentSyncState sync_state_{ActionAssignmentSyncState::kInitial};
  bool server_sync_pending_{false};
  std::uint64_t revision_{0};
};

}

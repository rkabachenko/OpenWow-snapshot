#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace openwow::game {

class BindingKey {
 public:
  explicit BindingKey(std::string value) : value_(std::move(value)) {}
  explicit BindingKey(std::string_view value) : value_(value) {}
  explicit BindingKey(const char* value) : value_(value) {}

  [[nodiscard]] const std::string& value() const noexcept { return value_; }
  [[nodiscard]] bool empty() const noexcept { return value_.empty(); }
  auto operator<=>(const BindingKey&) const = default;

 private:
  std::string value_;
};

class BindingChord {
 public:
  explicit BindingChord(std::string value) : value_(std::move(value)) {}
  explicit BindingChord(std::string_view value) : value_(value) {}
  explicit BindingChord(const char* value) : value_(value) {}

  [[nodiscard]] const std::string& value() const noexcept { return value_; }
  [[nodiscard]] bool empty() const noexcept { return value_.empty(); }
  auto operator<=>(const BindingChord&) const = default;

 private:
  std::string value_;
};

class BindingCommand {
 public:
  explicit BindingCommand(std::string value) : value_(std::move(value)) {}
  explicit BindingCommand(std::string_view value) : value_(value) {}
  explicit BindingCommand(const char* value) : value_(value) {}

  [[nodiscard]] const std::string& value() const noexcept { return value_; }
  [[nodiscard]] bool empty() const noexcept { return value_.empty(); }
  auto operator<=>(const BindingCommand&) const = default;

 private:
  std::string value_;
};

class ModifiedClickAction {
 public:
  explicit ModifiedClickAction(std::string value)
      : value_(std::move(value)) {}
  explicit ModifiedClickAction(std::string_view value) : value_(value) {}

  [[nodiscard]] const std::string& value() const noexcept { return value_; }
  auto operator<=>(const ModifiedClickAction&) const = default;

 private:
  std::string value_;
};

enum class BindingProfileScope : std::uint8_t {
  kDefault = 0,
  kAccount = 1,
  kCharacter = 2,
  kActive = 3,
};

class BindingProfileLoadGeneration {
 public:
  explicit constexpr BindingProfileLoadGeneration(
      const std::uint16_t value) noexcept
      : value_(value) {}

  [[nodiscard]] constexpr std::uint16_t value() const noexcept {
    return value_;
  }
  auto operator<=>(const BindingProfileLoadGeneration&) const = default;

 private:
  std::uint16_t value_;
};

class BindingSlot {
 public:
  [[nodiscard]] static constexpr std::optional<BindingSlot> FromValue(
      std::uint8_t value) noexcept {
    return value < 4 ? std::optional(BindingSlot(value)) : std::nullopt;
  }
  [[nodiscard]] static constexpr BindingSlot Primary() noexcept {
    return BindingSlot(0);
  }
  [[nodiscard]] constexpr std::uint8_t value() const noexcept {
    return value_;
  }
  auto operator<=>(const BindingSlot&) const = default;

 private:
  explicit constexpr BindingSlot(std::uint8_t value) noexcept : value_(value) {}
  std::uint8_t value_;
};

struct BindingAssignment {
  BindingAssignment(BindingChord assigned_chord,
                    BindingCommand assigned_command,
                    BindingProfileScope assigned_scope,
                    BindingSlot assigned_slot,
                    int assigned_index)
      : chord(std::move(assigned_chord)),
        command(std::move(assigned_command)),
        scope(assigned_scope),
        slot(assigned_slot),
        index(assigned_index) {}

  BindingChord chord;
  BindingCommand command;
  BindingProfileScope scope{BindingProfileScope::kDefault};
  BindingSlot slot{BindingSlot::Primary()};
  int index{0};
};

struct BindingResolution {
  BindingCommand command;
  BindingChord matched_chord;
};

struct BindingInvocation {
  bool pressed{false};
  float pressure{0.0f};
  float angle{-1.0f};
  float precision{0.0f};
  std::optional<std::uint16_t> modifier_state;
};

struct ModifiedClickBindingState {
  std::uint8_t modifier_bits{0};
  std::uint8_t button_index{0};
  bool has_button_token{false};
  bool skip_serialization{true};
};

struct ModifiedClickInputState {
  std::uint8_t modifier_bits{0};
  std::optional<std::uint8_t> button_index;
};

struct ModifiedClickAssignment {
  ModifiedClickAction action;
  ModifiedClickBindingState state;
};

}

namespace std {

template <>
struct hash<openwow::game::BindingKey> {
  std::size_t operator()(const openwow::game::BindingKey& key) const noexcept {
    return std::hash<std::string>{}(key.value());
  }
};

template <>
struct hash<openwow::game::BindingChord> {
  std::size_t operator()(
      const openwow::game::BindingChord& chord) const noexcept {
    return std::hash<std::string>{}(chord.value());
  }
};

template <>
struct hash<openwow::game::BindingCommand> {
  std::size_t operator()(
      const openwow::game::BindingCommand& command) const noexcept {
    return std::hash<std::string>{}(command.value());
  }
};

template <>
struct hash<openwow::game::ModifiedClickAction> {
  std::size_t operator()(
      const openwow::game::ModifiedClickAction& action) const noexcept {
    return std::hash<std::string>{}(action.value());
  }
};

}

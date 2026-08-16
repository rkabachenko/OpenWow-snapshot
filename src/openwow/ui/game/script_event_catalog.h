#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::ui::game {

enum class ScriptEventDomain : std::uint8_t {
  Unit,
  Player,
  Chat,
  AddOn,
  Combat,
  Battlenet,
  Cinematic,
  Vehicle,
  Group,
  Loot,
  Trade,
  Quest,
  System,
};

enum class ScriptEventPayloadKind : std::uint8_t {
  None,
  UnitToken,
  ChatMessage,
  AddOnMessage,
  AddOnName,
  CombatLog,
  Message,
  CVarUpdate,
  BagId,
  LootSlot,
  Money,
  Spellcast,
};

struct ScriptEventDescriptor {
  std::uint16_t id{};
  std::string_view name;
  ScriptEventDomain domain{ScriptEventDomain::System};
  ScriptEventPayloadKind payload{ScriptEventPayloadKind::None};
  std::uint8_t min_payload_args{};
  bool battlenet{};
  bool registerable_by_frame{true};
};

class ScriptEventCatalog {
 public:
  static const ScriptEventCatalog& Instance();

  [[nodiscard]] const std::vector<ScriptEventDescriptor>& events() const {
    return events_;
  }

  [[nodiscard]] const ScriptEventDescriptor* Find(std::string_view name) const;
  [[nodiscard]] const ScriptEventDescriptor* FindById(std::uint16_t id) const;
  [[nodiscard]] std::optional<std::uint16_t> Intern(std::string_view name) const;
  [[nodiscard]] bool IsKnown(std::string_view name) const;
  [[nodiscard]] bool IsPayloadShapeValid(std::string_view name,
                                         std::size_t payload_arg_count) const;

 private:
  struct AsciiNoCaseHash {
    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept;
  };

  struct AsciiNoCaseEqual {
    [[nodiscard]] bool operator()(std::string_view lhs,
                                  std::string_view rhs) const noexcept;
  };

  ScriptEventCatalog();

  std::vector<ScriptEventDescriptor> events_;
  std::unordered_map<std::string_view, std::size_t, AsciiNoCaseHash,
                     AsciiNoCaseEqual>
      lookup_;
  std::vector<std::size_t> id_lookup_;
};

}

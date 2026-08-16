#pragma once

#include "openwow/game/actions/bindings/model/binding_types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::game::actions::bindings::adapters::persistence {

struct ParsedBindingAssignment {
  BindingSlot slot;
  BindingChord chord;
  BindingCommand command;
};

struct ParsedModifiedClickAssignment {
  ModifiedClickAction action;
  std::string binding;
};

struct ParsedBindingDocument {
  std::vector<ParsedBindingAssignment> bindings;
  std::vector<ParsedModifiedClickAssignment> modified_clicks;
};

[[nodiscard]] std::string NormalizeRetailBindingChord(std::string_view chord);

[[nodiscard]] ParsedBindingDocument ParseRetailBindingDocument(
    std::string_view text);
[[nodiscard]] std::string SerializeRetailModifiedClick(
    std::uint8_t modifier_bits,
    std::uint8_t button_index,
    bool has_button_token);

}

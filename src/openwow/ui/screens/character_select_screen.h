#pragma once

#include "openwow/net/wotlk/protocol/world_protocol.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace openwow::ui::screens {

class CharacterSelectScreen {
 public:
  void SetCharacters(const std::vector<openwow::net::wotlk::CharacterSummary>& characters);
  void AppendCharacter(const openwow::net::wotlk::CharacterSummary& character);
  bool SelectCharacterById(std::uint64_t id);
  void MoveSelection(int delta);
  openwow::net::wotlk::CharacterSummary CreateCharacter(const std::string& name, int level);
  std::optional<openwow::net::wotlk::CharacterSummary> selected_character() const;
  std::optional<std::uint64_t> selected_character_id() const;
  std::size_t selected_index() const;
  const std::vector<openwow::net::wotlk::CharacterSummary>& characters() const;
 private:
  std::vector<openwow::net::wotlk::CharacterSummary> characters_;
  std::optional<std::uint64_t> selected_character_id_;
  std::size_t selected_index_{0};
  std::uint64_t next_character_id_{1000};
};

}

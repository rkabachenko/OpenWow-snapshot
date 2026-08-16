#include "openwow/ui/screens/character_select_screen.h"

#include <algorithm>

namespace openwow::ui::screens {

void CharacterSelectScreen::SetCharacters(
    const std::vector<openwow::net::wotlk::CharacterSummary>& characters) {
  characters_ = characters;
  selected_index_ = 0;
  if (!characters_.empty()) {
    selected_character_id_ = characters_[0].id;
  } else {
    selected_character_id_.reset();
  }
  for (const auto& c : characters_) {
    next_character_id_ = std::max(next_character_id_, c.id + 1);
  }
}

void CharacterSelectScreen::AppendCharacter(
    const openwow::net::wotlk::CharacterSummary& character) {
  if (character.id == 0 || character.name.empty()) {
    return;
  }
  characters_.push_back(character);
  selected_index_ = characters_.size() - 1;
  selected_character_id_ = character.id;
  next_character_id_ = std::max(next_character_id_, character.id + 1);
}

bool CharacterSelectScreen::SelectCharacterById(std::uint64_t id) {
  for (std::size_t i = 0; i < characters_.size(); ++i) {
    const auto& c = characters_[i];
    if (c.id == id) {
      selected_character_id_ = id;
      selected_index_ = i;
      return true;
    }
  }
  return false;
}

void CharacterSelectScreen::MoveSelection(int delta) {
  if (characters_.empty() || delta == 0) {
    return;
  }
  const int max_index = static_cast<int>(characters_.size()) - 1;
  int next = static_cast<int>(selected_index_) + delta;
  next = std::clamp(next, 0, max_index);
  selected_index_ = static_cast<std::size_t>(next);
  selected_character_id_ = characters_[selected_index_].id;
}

openwow::net::wotlk::CharacterSummary CharacterSelectScreen::CreateCharacter(const std::string& name,
                                                                             int level) {
  const int safe_level = std::clamp(level, 1, 80);
  openwow::net::wotlk::CharacterSummary created{
      .id = next_character_id_++,
      .name = name.empty() ? "NewCharacter" : name,
      .level = safe_level,
  };
  characters_.push_back(created);
  selected_index_ = characters_.size() - 1;
  selected_character_id_ = created.id;
  return created;
}

std::optional<openwow::net::wotlk::CharacterSummary> CharacterSelectScreen::selected_character() const {
  if (characters_.empty() || selected_index_ >= characters_.size()) {
    return std::nullopt;
  }
  return characters_[selected_index_];
}

std::optional<std::uint64_t> CharacterSelectScreen::selected_character_id() const {
  return selected_character_id_;
}

std::size_t CharacterSelectScreen::selected_index() const {
  return selected_index_;
}

const std::vector<openwow::net::wotlk::CharacterSummary>& CharacterSelectScreen::characters() const {
  return characters_;
}

}

#include "openwow/ui/screens/realm_list_screen.h"

#include <algorithm>

namespace openwow::ui::screens {

void RealmListScreen::SetRealms(const std::vector<openwow::net::wotlk::RealmInfo>& realms) {
  realms_ = realms;
  if (selected_index_ >= realms_.size()) {
    selected_index_ = realms_.empty() ? 0 : realms_.size() - 1;
  }
}

const std::vector<openwow::net::wotlk::RealmInfo>& RealmListScreen::realms() const {
  return realms_;
}

void RealmListScreen::MoveSelection(int delta) {
  if (realms_.empty() || delta == 0) {
    return;
  }
  const int max_index = static_cast<int>(realms_.size()) - 1;
  int next = static_cast<int>(selected_index_) + delta;
  next = std::clamp(next, 0, max_index);
  selected_index_ = static_cast<std::size_t>(next);
}

std::optional<openwow::net::wotlk::RealmInfo> RealmListScreen::selected_realm() const {
  if (realms_.empty() || selected_index_ >= realms_.size()) {
    return std::nullopt;
  }
  return realms_[selected_index_];
}

std::size_t RealmListScreen::selected_index() const {
  return selected_index_;
}

}

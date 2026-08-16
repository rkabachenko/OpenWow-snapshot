#include "openwow/game/actions/macros/application/macro_icon_library.h"

#include <utility>

namespace openwow::game::actions::macros {

void MacroIconLibrary::LoadIfNeeded(const Loader& loader) {
  {
    std::lock_guard lock(mutex_);
    if (loaded_) {
      return;
    }
  }

  std::vector<std::string> macro_icons;
  std::vector<std::string> item_icons;
  loader(macro_icons, item_icons);

  std::lock_guard lock(mutex_);
  if (!loaded_) {
    macro_icons_ = std::move(macro_icons);
    item_icons_ = std::move(item_icons);
    loaded_ = true;
  }
}

void MacroIconLibrary::Reset() {
  std::lock_guard lock(mutex_);
  macro_icons_.clear();
  item_icons_.clear();
  loaded_ = false;
}

std::uint32_t MacroIconLibrary::MacroIconCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(macro_icons_.size());
}

std::uint32_t MacroIconLibrary::ItemIconCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(item_icons_.size());
}

std::optional<std::string> MacroIconLibrary::MacroIcon(
    const std::uint32_t one_based_index) const {
  std::lock_guard lock(mutex_);
  if (one_based_index == 0 || one_based_index > macro_icons_.size()) {
    return std::nullopt;
  }
  return macro_icons_[one_based_index - 1];
}

std::optional<std::string> MacroIconLibrary::ItemIcon(
    const std::uint32_t one_based_index) const {
  std::lock_guard lock(mutex_);
  if (one_based_index == 0 || one_based_index > item_icons_.size()) {
    return std::nullopt;
  }
  return item_icons_[one_based_index - 1];
}

std::vector<std::string> MacroIconLibrary::MacroIcons() const {
  std::lock_guard lock(mutex_);
  return macro_icons_;
}

}

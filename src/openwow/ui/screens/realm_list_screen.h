#pragma once

#include "openwow/net/wotlk/realm_list.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace openwow::ui::screens {

class RealmListScreen {
 public:
  void SetRealms(const std::vector<openwow::net::wotlk::RealmInfo>& realms);
  const std::vector<openwow::net::wotlk::RealmInfo>& realms() const;
  void MoveSelection(int delta);
  std::optional<openwow::net::wotlk::RealmInfo> selected_realm() const;
  std::size_t selected_index() const;
 private:
  std::vector<openwow::net::wotlk::RealmInfo> realms_;
  std::size_t selected_index_{0};
};

}

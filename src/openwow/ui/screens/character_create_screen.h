#pragma once

#include "openwow/ui/glue/glue_game_state.h"

#include <string>

namespace openwow::ui::screens {

class CharacterCreateScreen {
 public:
  void SyncFromGameState(const openwow::ui::glue::GlueGameState& gs);

  int selected_race() const { return selected_race_; }
  int selected_class() const { return selected_class_; }
  int selected_sex() const { return selected_sex_; }
  const std::string& race_name() const { return race_name_; }
  const std::string& class_name() const { return class_name_; }
  const std::string& faction_label() const { return faction_label_; }

 private:
  int selected_race_{1};
  int selected_class_{1};
  int selected_sex_{0};
  std::string race_name_{"Human"};
  std::string class_name_{"Warrior"};
  std::string faction_label_{"Alliance"};
};

}

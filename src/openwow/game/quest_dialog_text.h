
#pragma once

#include <string>
#include <string_view>

namespace openwow::game {

class WorldSession;

std::string ExpandQuestDialogText(std::string_view raw_text,
                                  bool empty_as_space);
std::string ExpandQuestDialogText(const WorldSession& session,
                                  std::string_view raw_text,
                                  bool empty_as_space);

}

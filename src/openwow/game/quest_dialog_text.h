
#pragma once

#include <string>
#include <string_view>

namespace openwow::data::dbc { class DbcLoader; }

namespace openwow::game {

class WorldSession;
class ObjectManager;
class ObjectGuid;

std::string ExpandQuestDialogText(std::string_view raw_text,
                                  bool empty_as_space);
std::string ExpandQuestDialogText(const WorldSession& session,
                                  std::string_view raw_text,
                                  bool empty_as_space);

[[nodiscard]] bool ExpandServerTextTokens(
    const ObjectManager& objects,
    const data::dbc::DbcLoader* dbc_loader,
    ObjectGuid subject_guid,
    std::string_view raw_text,
    std::string& expanded);

}

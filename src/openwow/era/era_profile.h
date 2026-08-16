#pragma once

#include <optional>
#include <string>
#include <vector>

namespace openwow::era {

struct EraProfile {
  std::string id;
  std::string display_name;
  std::string protocol_version;
  std::string ui_scripting_version;
};

std::vector<EraProfile> GetAllProfiles();
std::optional<EraProfile> FindProfileById(const std::string& id);

}

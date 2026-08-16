#include "openwow/era/era_profile.h"

#include <optional>
#include <vector>

namespace openwow::era {

namespace {
const std::vector<EraProfile> kProfiles = {
    {.id = "wotlk-3.3.5", .display_name = "Wrath of the Lich King 3.3.5", .protocol_version = "3.3.5", .ui_scripting_version = "lua-5.1"},
};
}

std::vector<EraProfile> GetAllProfiles() {
  return kProfiles;
}

std::optional<EraProfile> FindProfileById(const std::string& id) {
  for (const auto& profile : kProfiles) {
    if (profile.id == id) {
      return profile;
    }
  }
  return std::nullopt;
}

}

#pragma once

#include <string>

namespace openwow::ui::game {
class CVarSystem;
}

namespace openwow::render {

void RegisterSkyCVarDefaults(openwow::ui::game::CVarSystem& cvars);
void ApplyCurrentSkyCVarState(openwow::ui::game::CVarSystem& cvars);

bool CVar_SkyCloudLOD_Callback(const std::string& name, const std::string& oldValue,
                               const std::string& newValue);

}

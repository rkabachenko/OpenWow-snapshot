#pragma once

#include "openwow/ui/game/ui_load_status.h"

#include <string>
#include <string_view>
#include <vector>

struct lua_State;

namespace openwow::ui::game {

enum class SavedVariableRegistrationScope {
  kAccount,
  kPerCharacter,
};

[[nodiscard]] std::vector<std::string> GetSavedVariableNames();

[[nodiscard]] std::vector<std::string> GetSavedVariableNamesPerChar();

void RegisterSavedVariableName(SavedVariableRegistrationScope scope, std::string_view name);

std::string SerializeLuaValue(lua_State *L, int index, int depth = 0);

bool LoadAllSavedVariables(lua_State *L, const std::string &account_name,
                           const std::string &realm_name, const std::string &char_name,
                           UiLoadStatusSink *status_sink = nullptr);

void SaveAllSavedVariables(lua_State *L, const std::string &account_name,
                           const std::string &realm_name, const std::string &char_name);

void ClearSavedVariableRegistrations();

}

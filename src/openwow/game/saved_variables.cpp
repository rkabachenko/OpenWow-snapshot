
#include "openwow/game/saved_variables.h"

#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace openwow::game {

SavedVariables& SavedVariables::Get() {
  static SavedVariables instance;
  return instance;
}

bool SavedVariables::LoadAccountVariables(const std::string& addonName,
                                          const std::string& accountName) {
  const std::string path =
      GetAccountPath(accountName) + "/SavedVariables/" + addonName + ".lua";
  return LoadFromFile(path, addonName);
}

bool SavedVariables::SaveAccountVariables(const std::string& addonName,
                                          const std::string& accountName) {
  const std::string dir = GetAccountPath(accountName) + "/SavedVariables";
  const std::string path = dir + "/" + addonName + ".lua";

  std::lock_guard lock(mutex_);
  auto it = addons_.find(addonName);
  if (it == addons_.end()) return false;

  return SaveToFile(path, addonName, it->second.registered_names);
}

bool SavedVariables::LoadCharacterVariables(const std::string& addonName,
                                            const std::string& accountName,
                                            const std::string& realmName,
                                            const std::string& charName) {
  const std::string path =
      GetCharacterPath(accountName, realmName, charName) +
      "/SavedVariables/" + addonName + ".lua";
  return LoadFromFile(path, addonName);
}

bool SavedVariables::SaveCharacterVariables(const std::string& addonName,
                                            const std::string& accountName,
                                            const std::string& realmName,
                                            const std::string& charName) {
  const std::string dir =
      GetCharacterPath(accountName, realmName, charName) + "/SavedVariables";
  const std::string path = dir + "/" + addonName + ".lua";

  std::lock_guard lock(mutex_);
  auto it = addons_.find(addonName);
  if (it == addons_.end()) return false;

  return SaveToFile(path, addonName, it->second.registered_per_char);
}

void SavedVariables::SetVariable(const std::string& addonName,
                                 const std::string& varName,
                                 const std::string& luaValue) {
  std::lock_guard lock(mutex_);
  auto& addon = addons_[addonName];
  addon.variables[varName] = luaValue;
  addon.dirty = true;
}

std::string SavedVariables::GetVariable(const std::string& addonName,
                                        const std::string& varName) const {
  std::lock_guard lock(mutex_);
  auto ait = addons_.find(addonName);
  if (ait == addons_.end()) return {};
  auto vit = ait->second.variables.find(varName);
  if (vit == ait->second.variables.end()) return {};
  return vit->second;
}

bool SavedVariables::HasVariable(const std::string& addonName,
                                 const std::string& varName) const {
  std::lock_guard lock(mutex_);
  auto ait = addons_.find(addonName);
  if (ait == addons_.end()) return false;
  return ait->second.variables.count(varName) > 0;
}

void SavedVariables::RegisterSavedVariable(const std::string& addonName,
                                           const std::string& varName,
                                           bool perCharacter) {
  std::lock_guard lock(mutex_);
  auto& addon = addons_[addonName];
  auto& target = perCharacter ? addon.registered_per_char : addon.registered_names;

  if (std::find(target.begin(), target.end(), varName) == target.end()) {
    target.push_back(varName);
  }
}

void SavedVariables::SaveAll(const std::string& accountName,
                             const std::string& realmName,
                             const std::string& charName) {

  std::vector<std::string> dirty_addons;
  {
    std::lock_guard lock(mutex_);
    for (auto& [name, vars] : addons_) {
      if (vars.dirty) {
        dirty_addons.push_back(name);
      }
    }
  }

  for (const auto& addonName : dirty_addons) {
    SaveAccountVariables(addonName, accountName);
    if (!realmName.empty() && !charName.empty()) {
      SaveCharacterVariables(addonName, accountName, realmName, charName);
    }

    std::lock_guard lock(mutex_);
    auto it = addons_.find(addonName);
    if (it != addons_.end()) {
      it->second.dirty = false;
    }
  }
}

void SavedVariables::MarkDirty(const std::string& addonName) {
  std::lock_guard lock(mutex_);
  addons_[addonName].dirty = true;
}

bool SavedVariables::IsDirty(const std::string& addonName) const {
  std::lock_guard lock(mutex_);
  auto it = addons_.find(addonName);
  if (it == addons_.end()) return false;
  return it->second.dirty;
}

std::string SavedVariables::GetAccountPath(const std::string& accountName) {
  return "WTF/Account/" + accountName;
}

std::string SavedVariables::GetCharacterPath(const std::string& accountName,
                                             const std::string& realm,
                                             const std::string& character) {
  return "WTF/Account/" + accountName + "/" + realm + "/" + character;
}

void SavedVariables::Reset() {
  std::lock_guard lock(mutex_);
  addons_.clear();
}

std::string SavedVariables::SerializeVariable(const std::string& name,
                                              const std::string& value) {
  return name + " = " + value + "\n";
}

bool SavedVariables::ParseLuaFile(
    const std::string& content,
    std::unordered_map<std::string, std::string>& vars) {

  std::istringstream stream(content);
  std::string line;

  while (std::getline(stream, line)) {

    auto start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) continue;
    line = line.substr(start);

    if (line.empty() || line[0] == '-') continue;

    auto eq_pos = line.find('=');
    if (eq_pos == std::string::npos) continue;

    std::string lhs = line.substr(0, eq_pos);
    auto lhs_end = lhs.find_last_not_of(" \t");
    if (lhs_end == std::string::npos) continue;
    lhs = lhs.substr(0, lhs_end + 1);

    bool valid_id = true;
    for (char c : lhs) {
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
        valid_id = false;
        break;
      }
    }
    if (!valid_id || lhs.empty()) continue;

    std::string rhs = line.substr(eq_pos + 1);
    auto rhs_start = rhs.find_first_not_of(" \t");
    if (rhs_start != std::string::npos) {
      rhs = rhs.substr(rhs_start);
    } else {
      rhs.clear();
    }

    int depth = 0;
    for (char c : rhs) {
      if (c == '{') ++depth;
      else if (c == '}') --depth;
    }

    while (depth > 0 && std::getline(stream, line)) {
      rhs += "\n" + line;
      for (char c : line) {
        if (c == '{') ++depth;
        else if (c == '}') --depth;
      }
    }

    vars[lhs] = rhs;
  }

  return true;
}

bool SavedVariables::LoadFromFile(const std::string& path,
                                  const std::string& addonName) {
  std::ifstream file(path);
  if (!file.is_open()) return false;

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

  std::unordered_map<std::string, std::string> parsed;
  if (!ParseLuaFile(content, parsed)) return false;

  std::lock_guard lock(mutex_);
  auto& addon = addons_[addonName];
  for (auto& [name, value] : parsed) {
    addon.variables[name] = std::move(value);
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "SavedVariables: loaded " + std::to_string(parsed.size()) +
                         " vars for " + addonName + " from " + path);
  return true;
}

bool SavedVariables::SaveToFile(const std::string& path,
                                const std::string& addonName,
                                const std::vector<std::string>& varNames) {

  auto it = addons_.find(addonName);
  if (it == addons_.end()) return false;

  std::error_code ec;
  auto parent = std::filesystem::path(path).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "SavedVariables: failed to create dir " +
                             parent.string() + ": " + ec.message());
      return false;
    }
  }

  std::ofstream file(path);
  if (!file.is_open()) return false;

  file << "\n";
  const auto& vars = it->second.variables;

  if (varNames.empty()) {

    for (const auto& [name, value] : vars) {
      file << SerializeVariable(name, value);
    }
  } else {

    for (const auto& name : varNames) {
      auto vit = vars.find(name);
      if (vit != vars.end()) {
        file << SerializeVariable(name, vit->second);
      }
    }
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "SavedVariables: saved " + addonName + " to " + path);
  return file.good();
}

}


#include "openwow/game/addon_saved_vars.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace openwow::game {

const SavedVarEntry* AddonSavedVars::FindEntry(const std::string& addon,
                                                const std::string& var) const {
  for (const auto& e : entries_) {
    if (e.addonName == addon && e.varName == var) return &e;
  }
  return nullptr;
}

SavedVarEntry* AddonSavedVars::FindEntry(const std::string& addon,
                                          const std::string& var) {
  for (auto& e : entries_) {
    if (e.addonName == addon && e.varName == var) return &e;
  }
  return nullptr;
}

void AddonSavedVars::SetVar(const std::string& addon, const std::string& var,
                             SavedVarValue value, SavedVarScope scope) {
  if (auto* existing = FindEntry(addon, var)) {
    existing->value = std::move(value);
    existing->scope = scope;
    return;
  }
  entries_.push_back(
      SavedVarEntry{addon, var, std::move(value), scope});
}

std::optional<SavedVarValue> AddonSavedVars::GetVar(
    const std::string& addon, const std::string& var) const {
  if (const auto* e = FindEntry(addon, var)) {
    return e->value;
  }
  return std::nullopt;
}

bool AddonSavedVars::RemoveVar(const std::string& addon,
                                const std::string& var) {
  auto it = std::find_if(entries_.begin(), entries_.end(),
                          [&](const SavedVarEntry& e) {
                            return e.addonName == addon && e.varName == var;
                          });
  if (it == entries_.end()) return false;
  entries_.erase(it);
  return true;
}

std::vector<SavedVarEntry> AddonSavedVars::GetAllVarsForAddon(
    const std::string& addon) const {
  std::vector<SavedVarEntry> result;
  for (const auto& e : entries_) {
    if (e.addonName == addon) result.push_back(e);
  }
  return result;
}

std::vector<std::string> AddonSavedVars::GetAddonNames() const {
  std::set<std::string> names;
  for (const auto& e : entries_) {
    names.insert(e.addonName);
  }
  return {names.begin(), names.end()};
}

void AddonSavedVars::ClearAddon(const std::string& addon) {
  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                      [&](const SavedVarEntry& e) {
                        return e.addonName == addon;
                      }),
      entries_.end());
}

size_t AddonSavedVars::GetVarCount(const std::string& addon) const {
  return static_cast<size_t>(std::count_if(
      entries_.begin(), entries_.end(),
      [&](const SavedVarEntry& e) { return e.addonName == addon; }));
}

size_t AddonSavedVars::GetTotalVarCount() const { return entries_.size(); }

void AddonSavedVars::SetCharacterName(const std::string& name) {
  characterName_ = name;
}

const std::string& AddonSavedVars::GetCharacterName() const {
  return characterName_;
}

std::vector<SavedVarEntry> AddonSavedVars::GetCharacterVars(
    const std::string& addon) const {
  std::vector<SavedVarEntry> result;
  for (const auto& e : entries_) {
    if (e.addonName == addon && e.scope == SavedVarScope::Character) {
      result.push_back(e);
    }
  }
  return result;
}

std::vector<SavedVarEntry> AddonSavedVars::GetAccountVars(
    const std::string& addon) const {
  std::vector<SavedVarEntry> result;
  for (const auto& e : entries_) {
    if (e.addonName == addon && e.scope == SavedVarScope::Account) {
      result.push_back(e);
    }
  }
  return result;
}

namespace {

std::string SerializeValue(const SavedVarValue& val) {
  return std::visit(
      [](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
          return "nil";
        } else if constexpr (std::is_same_v<T, bool>) {
          return v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, double>) {
          std::ostringstream oss;
          oss << v;
          return oss.str();
        } else if constexpr (std::is_same_v<T, std::string>) {

          std::string escaped;
          escaped.reserve(v.size() + 2);
          escaped.push_back('"');
          for (char c : v) {
            if (c == '"') escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else escaped.push_back(c);
          }
          escaped.push_back('"');
          return escaped;
        }
      },
      val);
}

}

std::string AddonSavedVars::Serialize() const {
  if (entries_.empty()) return "";

  std::set<std::string> addonNames;
  for (const auto& e : entries_) addonNames.insert(e.addonName);

  std::ostringstream oss;
  for (const auto& addon : addonNames) {
    oss << addon << "_SavedVars = {\n";
    for (const auto& e : entries_) {
      if (e.addonName != addon) continue;
      oss << "  [\"" << e.varName << "\"] = " << SerializeValue(e.value)
          << ",\n";
    }
    oss << "}\n";
  }
  return oss.str();
}

void AddonSavedVars::Reset() {
  entries_.clear();
  characterName_.clear();
}

}

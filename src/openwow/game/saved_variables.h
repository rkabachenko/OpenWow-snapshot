#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

class SavedVariables {
 public:
  static SavedVariables& Get();

  bool LoadAccountVariables(const std::string& addonName,
                            const std::string& accountName);
  bool SaveAccountVariables(const std::string& addonName,
                            const std::string& accountName);

  bool LoadCharacterVariables(const std::string& addonName,
                              const std::string& accountName,
                              const std::string& realmName,
                              const std::string& charName);
  bool SaveCharacterVariables(const std::string& addonName,
                              const std::string& accountName,
                              const std::string& realmName,
                              const std::string& charName);

  void SetVariable(const std::string& addonName, const std::string& varName,
                   const std::string& luaValue);
  std::string GetVariable(const std::string& addonName,
                          const std::string& varName) const;
  bool HasVariable(const std::string& addonName,
                   const std::string& varName) const;

  void RegisterSavedVariable(const std::string& addonName,
                             const std::string& varName, bool perCharacter);

  void SaveAll(const std::string& accountName,
               const std::string& realmName = "",
               const std::string& charName = "");

  void MarkDirty(const std::string& addonName);
  bool IsDirty(const std::string& addonName) const;

  static std::string GetAccountPath(const std::string& accountName);
  static std::string GetCharacterPath(const std::string& accountName,
                                      const std::string& realm,
                                      const std::string& character);

  void Reset();

 private:
  SavedVariables() = default;

  struct AddonVars {
    std::unordered_map<std::string, std::string> variables;
    std::vector<std::string> registered_names;
    std::vector<std::string> registered_per_char;
    bool dirty = false;
  };

  std::unordered_map<std::string, AddonVars> addons_;
  mutable std::mutex mutex_;

  static std::string SerializeVariable(const std::string& name,
                                       const std::string& value);

  static bool ParseLuaFile(
      const std::string& content,
      std::unordered_map<std::string, std::string>& vars);

  bool LoadFromFile(const std::string& path, const std::string& addonName);
  bool SaveToFile(const std::string& path, const std::string& addonName,
                  const std::vector<std::string>& varNames);
};

}

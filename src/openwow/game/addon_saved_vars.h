#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace openwow::game {

using SavedVarValue = std::variant<std::nullptr_t, bool, double, std::string>;

enum class SavedVarScope : uint8_t {
  Account = 0,
  Character = 1,
};

struct SavedVarEntry {
  std::string addonName;
  std::string varName;
  SavedVarValue value;
  SavedVarScope scope = SavedVarScope::Account;
};

class AddonSavedVars {
 public:
  AddonSavedVars() = default;

  void SetVar(const std::string& addon, const std::string& var,
              SavedVarValue value, SavedVarScope scope = SavedVarScope::Account);

  [[nodiscard]] std::optional<SavedVarValue> GetVar(const std::string& addon,
                                                     const std::string& var) const;

  bool RemoveVar(const std::string& addon, const std::string& var);

  [[nodiscard]] std::vector<SavedVarEntry> GetAllVarsForAddon(
      const std::string& addon) const;

  [[nodiscard]] std::vector<std::string> GetAddonNames() const;

  void ClearAddon(const std::string& addon);

  [[nodiscard]] size_t GetVarCount(const std::string& addon) const;
  [[nodiscard]] size_t GetTotalVarCount() const;

  void SetCharacterName(const std::string& name);
  [[nodiscard]] const std::string& GetCharacterName() const;

  [[nodiscard]] std::vector<SavedVarEntry> GetCharacterVars(
      const std::string& addon) const;

  [[nodiscard]] std::vector<SavedVarEntry> GetAccountVars(
      const std::string& addon) const;

  [[nodiscard]] std::string Serialize() const;

  void Reset();

 private:

  struct VarRecord {
    SavedVarValue value;
    SavedVarScope scope = SavedVarScope::Account;
  };

  std::vector<SavedVarEntry> entries_;
  std::string characterName_;

  [[nodiscard]] const SavedVarEntry* FindEntry(const std::string& addon,
                                                const std::string& var) const;
  [[nodiscard]] SavedVarEntry* FindEntry(const std::string& addon,
                                          const std::string& var);
};

}

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::core {

class WTFConfig {
 public:
  WTFConfig() = default;

  bool ParseFile(const std::string& content);

  [[nodiscard]] std::string GenerateFile() const;

  void Set(const std::string& key, const std::string& value);
  [[nodiscard]] std::optional<std::string> Get(const std::string& key) const;
  [[nodiscard]] std::optional<int> GetInt(const std::string& key) const;
  [[nodiscard]] std::optional<float> GetFloat(const std::string& key) const;
  [[nodiscard]] std::optional<bool> GetBool(const std::string& key) const;

  void Remove(const std::string& key);
  [[nodiscard]] bool Has(const std::string& key) const;

  [[nodiscard]] std::vector<std::string> GetKeys() const;
  [[nodiscard]] std::vector<std::pair<std::string, std::string>> GetAll() const;
  [[nodiscard]] uint32_t GetEntryCount() const;

  void Clear();

  void SetAccountName(const std::string& name);
  [[nodiscard]] const std::string& GetAccountName() const;

  void SetRealmName(const std::string& name);
  [[nodiscard]] const std::string& GetRealmName() const;

  void SetCharacterName(const std::string& name);
  [[nodiscard]] const std::string& GetCharacterName() const;

  [[nodiscard]] std::string GetConfigPath() const;

  [[nodiscard]] bool IsModified() const;
  void MarkSaved();

 private:

  std::vector<std::string> order_;
  std::unordered_map<std::string, std::string> entries_;

  std::string account_name_;
  std::string realm_name_;
  std::string character_name_;

  bool modified_ = false;
};

}

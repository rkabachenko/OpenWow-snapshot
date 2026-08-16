#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

class SlashCommands {
 public:
  static SlashCommands& Get();

  using CommandHandler = std::function<bool(const std::string& args)>;
  void RegisterCommand(const std::string& command, CommandHandler handler);
  void RegisterAlias(const std::string& alias, const std::string& command);

  bool ProcessInput(const std::string& input);

  [[nodiscard]] std::vector<std::string> GetCommands() const;
  [[nodiscard]] bool IsCommand(const std::string& input) const;
  [[nodiscard]] std::vector<std::string> GetCompletions(
      const std::string& partial) const;

  void Reset();

 private:
  SlashCommands() = default;

  static std::pair<std::string, std::string> ParseInput(
      const std::string& input);

  std::unordered_map<std::string, CommandHandler> commands_;
  std::unordered_map<std::string, std::string> aliases_;
  mutable std::mutex mutex_;
};

}

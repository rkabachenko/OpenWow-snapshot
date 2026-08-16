#include "openwow/game/slash_commands.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace openwow::game {

namespace {

std::string LowercaseCommand(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

}

SlashCommands& SlashCommands::Get() {
  static SlashCommands instance;
  return instance;
}

std::pair<std::string, std::string> SlashCommands::ParseInput(
    const std::string& input) {
  if (input.empty() || input[0] != '/') {
    return {"", input};
  }

  const std::size_t space_pos = input.find(' ', 1);
  std::string command;
  std::string args;
  if (space_pos == std::string::npos) {
    command = input.substr(1);
  } else {
    command = input.substr(1, space_pos - 1);
    args = input.substr(space_pos + 1);
  }
  return {LowercaseCommand(std::move(command)), args};
}

void SlashCommands::RegisterCommand(const std::string& command,
                                    CommandHandler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  commands_[LowercaseCommand(command)] = std::move(handler);
}

void SlashCommands::RegisterAlias(const std::string& alias,
                                  const std::string& command) {
  std::lock_guard<std::mutex> lock(mutex_);
  aliases_[LowercaseCommand(alias)] = LowercaseCommand(command);
}

bool SlashCommands::ProcessInput(const std::string& input) {
  auto [command, args] = ParseInput(input);
  if (command.empty()) {
    return false;
  }

  CommandHandler handler;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string resolved = command;
    if (const auto alias_it = aliases_.find(command); alias_it != aliases_.end()) {
      resolved = alias_it->second;
    }

    const auto command_it = commands_.find(resolved);
    if (command_it == commands_.end()) {
      return false;
    }
    handler = command_it->second;
  }

  return handler ? handler(args) : false;
}

std::vector<std::string> SlashCommands::GetCommands() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> result;
  result.reserve(commands_.size() + aliases_.size());
  for (const auto& [command, _] : commands_) {
    result.push_back("/" + command);
  }
  for (const auto& [alias, _] : aliases_) {
    result.push_back("/" + alias);
  }
  std::sort(result.begin(), result.end());
  return result;
}

bool SlashCommands::IsCommand(const std::string& input) const {
  auto [command, args] = ParseInput(input);
  if (command.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  std::string resolved = command;
  if (const auto alias_it = aliases_.find(command); alias_it != aliases_.end()) {
    resolved = alias_it->second;
  }
  return commands_.find(resolved) != commands_.end();
}

std::vector<std::string> SlashCommands::GetCompletions(
    const std::string& partial) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::string lower = partial;
  if (!lower.empty() && lower[0] == '/') {
    lower = lower.substr(1);
  }
  lower = LowercaseCommand(std::move(lower));

  std::vector<std::string> result;
  for (const auto& [command, _] : commands_) {
    if (command.size() >= lower.size() && command.substr(0, lower.size()) == lower) {
      result.push_back("/" + command);
    }
  }
  for (const auto& [alias, _] : aliases_) {
    if (alias.size() >= lower.size() && alias.substr(0, lower.size()) == lower) {
      result.push_back("/" + alias);
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

void SlashCommands::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  commands_.clear();
  aliases_.clear();
}

}

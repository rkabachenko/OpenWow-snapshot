#include "openwow/debug/diagnostics/debug_console.h"

#include <algorithm>
#include <cctype>
#include <exception>

namespace openwow::debug {

namespace {

constexpr std::size_t kMaxCommandLineLength = 16 * 1024;
constexpr std::size_t kMaxArgumentLength = 4 * 1024;
constexpr std::size_t kMaxArgumentCount = 256;
constexpr std::size_t kMaxOutputLines = 1'000;
constexpr std::size_t kMaxOutputLineLength = 16 * 1024;
constexpr std::uint32_t kMaxHistoryLines = 1'000;

char ToLowerAscii(const unsigned char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A'))
                              : static_cast<char>(c);
}

std::string CanonicalName(std::string_view name) {
  if (name.empty()) {
    return {};
  }
  std::string result;
  result.reserve(name.size());
  for (const unsigned char c : name) {
    if (std::isspace(c) || std::iscntrl(c)) {
      return {};
    }
    result.push_back(ToLowerAscii(c));
  }
  return result;
}

}

DebugConsole& DebugConsole::Get() {
  static DebugConsole instance;
  return instance;
}

DebugConsole::DebugConsole() { RegisterDefaultCommands(); }

bool DebugConsole::Tokenize(const std::string_view line,
                            std::vector<std::string>& tokens,
                            std::string_view& raw_args,
                            std::string& error) const {
  tokens.clear();
  raw_args = {};
  error.clear();
  if (line.size() > kMaxCommandLineLength) {
    error = "command line too long";
    return false;
  }

  std::size_t offset = 0;
  while (offset < line.size()) {
    while (offset < line.size() &&
           std::isspace(static_cast<unsigned char>(line[offset]))) {
      ++offset;
    }
    if (offset == line.size()) {
      break;
    }
    if (tokens.size() == kMaxArgumentCount) {
      error = "too many arguments";
      return false;
    }

    std::string token;
    char quote = '\0';
    bool escaping = false;
    while (offset < line.size()) {
      const char c = line[offset];
      if (escaping) {
        token.push_back(c);
        escaping = false;
        ++offset;
      } else if (c == '\\') {
        escaping = true;
        ++offset;
      } else if (quote != '\0') {
        if (c == quote) {
          quote = '\0';
        } else {
          token.push_back(c);
        }
        ++offset;
      } else if (c == '\'' || c == '"') {
        quote = c;
        ++offset;
      } else if (std::isspace(static_cast<unsigned char>(c))) {
        break;
      } else {
        token.push_back(c);
        ++offset;
      }
      if (token.size() > kMaxArgumentLength) {
        error = "argument too long";
        return false;
      }
    }
    if (escaping) {
      error = "trailing escape";
      return false;
    }
    if (quote != '\0') {
      error = "unmatched quote";
      return false;
    }
    tokens.push_back(std::move(token));

    if (tokens.size() == 1) {
      std::size_t raw_offset = offset;
      while (raw_offset < line.size() &&
             std::isspace(static_cast<unsigned char>(line[raw_offset]))) {
        ++raw_offset;
      }
      raw_args = line.substr(raw_offset);
    }
  }
  return true;
}

std::uint64_t DebugConsole::RegisterCommand(const std::string& name,
                                            const std::string& description,
                                            CommandHandler handler,
                                            std::uint32_t min_args,
                                            const std::string& usage,
                                            const int retail_category) {
  std::lock_guard lock(mutex_);
  ConsoleCommand cmd;
  cmd.name = CanonicalName(name);
  if (cmd.name.empty() || !handler) {
    return 0;
  }
  cmd.description = description;
  cmd.handler = std::move(handler);
  cmd.min_args = min_args;
  cmd.usage = usage;
  cmd.retail_category = retail_category;
  cmd.registration_id = next_registration_id_++;
  if (next_registration_id_ == 0) {
    next_registration_id_ = 1;
  }
  const std::uint64_t registration_id = cmd.registration_id;
  commands_[cmd.name] = std::move(cmd);
  return registration_id;
}

std::uint64_t DebugConsole::RegisterRawCommand(const std::string& name,
                                               const std::string& description,
                                               RawCommandHandler handler,
                                               const std::string& usage,
                                               const int retail_category) {
  std::lock_guard lock(mutex_);
  ConsoleCommand cmd;
  cmd.name = CanonicalName(name);
  if (cmd.name.empty() || !handler) {
    return 0;
  }
  cmd.description = description;
  cmd.raw_handler = std::move(handler);
  cmd.usage = usage;
  cmd.retail_category = retail_category;
  cmd.registration_id = next_registration_id_++;
  if (next_registration_id_ == 0) {
    next_registration_id_ = 1;
  }
  const std::uint64_t registration_id = cmd.registration_id;
  commands_[cmd.name] = std::move(cmd);
  return registration_id;
}

void DebugConsole::UnregisterCommand(const std::string& name) {
  std::lock_guard lock(mutex_);
  commands_.erase(CanonicalName(name));
}

bool DebugConsole::UnregisterCommandIfCurrent(
    const std::string& name, const std::uint64_t registration_id) {
  std::lock_guard lock(mutex_);
  const auto it = commands_.find(CanonicalName(name));
  if (it == commands_.end() ||
      it->second.registration_id != registration_id) {
    return false;
  }
  commands_.erase(it);
  return true;
}

bool DebugConsole::HasCommand(const std::string& name) const {
  std::lock_guard lock(mutex_);
  return commands_.count(CanonicalName(name)) > 0;
}

std::vector<std::string> DebugConsole::GetCommandNames() const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> names;
  names.reserve(commands_.size());
  for (const auto& [key, cmd] : commands_) {
    names.push_back(cmd.name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::string DebugConsole::GetCommandDescription(const std::string& name) const {
  std::lock_guard lock(mutex_);
  auto it = commands_.find(CanonicalName(name));
  return it != commands_.end() ? it->second.description : "";
}

std::string DebugConsole::GetCommandUsage(const std::string& name) const {
  std::lock_guard lock(mutex_);
  auto it = commands_.find(CanonicalName(name));
  return it != commands_.end() ? it->second.usage : "";
}

std::vector<std::string> DebugConsole::GetCommandNamesForRetailCategory(
    const int retail_category) const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> names;
  for (const auto& [_, command] : commands_) {
    if (command.retail_category == retail_category) {
      names.push_back(command.name);
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::string DebugConsole::Execute(const std::string& command_line,
                                  const bool record_history) {
  std::vector<std::string> tokens;
  std::string_view raw_args;
  std::string parse_error;
  if (!Tokenize(command_line, tokens, raw_args, parse_error)) {
    return "Parse error: " + parse_error;
  }
  if (tokens.empty()) {
    return "";
  }

  std::lock_guard lock(mutex_);
  if (record_history) {
    AddToHistory(command_line);
  }

  const std::string cmd_name = CanonicalName(tokens[0]);
  const auto it = commands_.find(cmd_name);
  if (it == commands_.end()) {
    return "Unknown command: " + tokens[0];
  }
  const ConsoleCommand cmd = it->second;

  if (tokens.size() - 1 < cmd.min_args) {
    return "Usage: " + (cmd.usage.empty() ? cmd.name : cmd.usage);
  }
  try {
    if (cmd.raw_handler) {
      return cmd.raw_handler(raw_args);
    }
    if (cmd.handler) {
      return cmd.handler(tokens);
    }
  } catch (const std::exception& exception) {
    return "Command failed: " + std::string(exception.what());
  } catch (...) {
    return "Command failed: unknown error";
  }
  return "Command failed: no handler";
}

std::vector<std::string> DebugConsole::GetHistory() const {
  std::lock_guard lock(mutex_);
  return {history_.begin(), history_.end()};
}

void DebugConsole::AddToHistory(const std::string& command) {
  std::lock_guard lock(mutex_);
  if (command.empty() || max_history_ == 0) {
    return;
  }
  while (history_.size() >= max_history_) {
    history_.pop_front();
  }
  history_.push_back(command.substr(0, kMaxCommandLineLength));
}

std::uint32_t DebugConsole::GetHistorySize() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(history_.size());
}

void DebugConsole::ClearHistory() {
  std::lock_guard lock(mutex_);
  history_.clear();
}

void DebugConsole::SetMaxHistory(std::uint32_t max) {
  std::lock_guard lock(mutex_);
  max_history_ = std::min(max, kMaxHistoryLines);
  while (history_.size() > max_history_) {
    history_.pop_front();
  }
}

std::vector<std::string> DebugConsole::AutoComplete(
    const std::string& partial) const {
  std::lock_guard lock(mutex_);
  std::size_t offset = 0;
  while (offset < partial.size() &&
         std::isspace(static_cast<unsigned char>(partial[offset]))) {
    ++offset;
  }
  const std::string prefix = CanonicalName(
      std::string_view(partial).substr(offset));
  if (prefix.empty() && offset != partial.size()) {
    return {};
  }
  std::vector<std::string> matches;
  for (const auto& [key, cmd] : commands_) {
    if (key.size() >= prefix.size() &&
        key.compare(0, prefix.size(), prefix) == 0) {
      matches.push_back(cmd.name);
    }
  }
  std::sort(matches.begin(), matches.end());
  return matches;
}

bool DebugConsole::IsVisible() const {
  std::lock_guard lock(mutex_);
  return visible_;
}

void DebugConsole::SetVisible(bool v) {
  std::lock_guard lock(mutex_);
  visible_ = v;
}

void DebugConsole::SetOutputCallback(OutputCallback cb) {
  std::lock_guard lock(mutex_);
  output_callback_ = std::move(cb);
}

std::vector<ConsoleOutputLine> DebugConsole::GetOutput() const {
  std::lock_guard lock(mutex_);
  return {output_.begin(), output_.end()};
}

void DebugConsole::Write(const std::string& text, ConsoleColor color) {
  ConsoleOutputLine line;
  line.text = text.substr(0, kMaxOutputLineLength);
  line.color = color;
  line.timestamp = std::chrono::steady_clock::now();
  std::lock_guard lock(mutex_);
  while (output_.size() >= kMaxOutputLines) {
    output_.pop_front();
  }
  output_.push_back(line);
  const OutputCallback callback = output_callback_;
  if (callback) {
    callback(line);
  }
}

std::uint32_t DebugConsole::GetOutputSize() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(output_.size());
}

void DebugConsole::ClearOutput() {
  std::lock_guard lock(mutex_);
  output_.clear();
}

void DebugConsole::Reset() {
  std::lock_guard lock(mutex_);
  commands_.clear();
  history_.clear();
  output_.clear();
  output_callback_ = nullptr;
  visible_ = false;
  max_history_ = 100;
  RegisterDefaultCommands();
}

void DebugConsole::RegisterDefaultCommands() {
  RegisterCommand("help", "List available commands",
                  [this](const std::vector<std::string>& ) -> std::string {
                    auto names = GetCommandNames();
                    std::string result;
                    for (const auto& n : names) {
                      auto desc = GetCommandDescription(n);
                      result += n + " — " + desc + "\n";
                    }
                    return result;
                  });

  RegisterCommand("clear", "Clear console output",
                  [this](const std::vector<std::string>& ) -> std::string {
                    ClearOutput();
                    return "";
                  });

  RegisterCommand("version", "Show client version",
                  [](const std::vector<std::string>& ) -> std::string {
                    return "OpenWoW v0.1.0 (WotLK 3.3.5a compatible)";
                  });

}

}

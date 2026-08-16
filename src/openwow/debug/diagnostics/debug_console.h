#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::debug {

struct ConsoleColor {
  float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

  static ConsoleColor White() { return {1, 1, 1, 1}; }
  static ConsoleColor Red() { return {1, 0, 0, 1}; }
  static ConsoleColor Yellow() { return {1, 1, 0, 1}; }
  static ConsoleColor Green() { return {0, 1, 0, 1}; }
  static ConsoleColor Cyan() { return {0, 1, 1, 1}; }

  bool operator==(const ConsoleColor& o) const = default;
};
struct ConsoleOutputLine {
  std::string text;
  ConsoleColor color;
  std::chrono::steady_clock::time_point timestamp;
};

using CommandHandler =
    std::function<std::string(const std::vector<std::string>& args)>;
using RawCommandHandler =
    std::function<std::string(std::string_view raw_args)>;
struct ConsoleCommand {
  std::string name;
  std::string description;
  CommandHandler handler;
  RawCommandHandler raw_handler;
  std::uint32_t min_args = 0;
  std::string usage;
  int retail_category = -1;
  std::uint64_t registration_id = 0;
};

using OutputCallback = std::function<void(const ConsoleOutputLine& line)>;

class DebugConsole {
 public:
  static DebugConsole& Get();

  std::uint64_t RegisterCommand(
      const std::string& name, const std::string& description,
      CommandHandler handler, std::uint32_t min_args = 0,
      const std::string& usage = "", int retail_category = -1);
  std::uint64_t RegisterRawCommand(
      const std::string& name, const std::string& description,
      RawCommandHandler handler, const std::string& usage = "",
      int retail_category = -1);
  void UnregisterCommand(const std::string& name);
  bool UnregisterCommandIfCurrent(const std::string& name,
                                  std::uint64_t registration_id);
  [[nodiscard]] bool HasCommand(const std::string& name) const;
  [[nodiscard]] std::vector<std::string> GetCommandNames() const;
  [[nodiscard]] std::string GetCommandDescription(
      const std::string& name) const;
  [[nodiscard]] std::string GetCommandUsage(const std::string& name) const;
  [[nodiscard]] std::vector<std::string> GetCommandNamesForRetailCategory(
      int retail_category) const;

  std::string Execute(const std::string& command_line, bool record_history = true);

  [[nodiscard]] std::vector<std::string> GetHistory() const;
  void AddToHistory(const std::string& command);
  [[nodiscard]] std::uint32_t GetHistorySize() const;
  void ClearHistory();
  void SetMaxHistory(std::uint32_t max);

  [[nodiscard]] std::vector<std::string> AutoComplete(
      const std::string& partial) const;

  [[nodiscard]] bool IsVisible() const;
  void SetVisible(bool v);

  void SetOutputCallback(OutputCallback cb);
  [[nodiscard]] std::vector<ConsoleOutputLine> GetOutput() const;
  void Write(const std::string& text,
             ConsoleColor color = ConsoleColor::White());
  [[nodiscard]] std::uint32_t GetOutputSize() const;
  void ClearOutput();

  void Reset();

 private:
  DebugConsole();

  void RegisterDefaultCommands();
  [[nodiscard]] bool Tokenize(std::string_view line,
                              std::vector<std::string>& tokens,
                              std::string_view& raw_args,
                              std::string& error) const;

  mutable std::recursive_mutex mutex_;
  std::unordered_map<std::string, ConsoleCommand> commands_;
  std::uint64_t next_registration_id_ = 1;
  std::deque<std::string> history_;
  std::uint32_t max_history_ = 100;
  std::deque<ConsoleOutputLine> output_;
  OutputCallback output_callback_;
  bool visible_ = false;
};

}

#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace openwow::game::actions::macros::rules {

struct SecureCommandOptionResult {
  std::string value;
  std::string target;
  bool matched{false};
};

struct SecureConditionBlockResult {
  std::string target;
  bool matched{true};
};

class SecureCommandOptionParser {
 public:
  using ConditionEvaluator =
      std::function<SecureConditionBlockResult(std::string_view)>;

  [[nodiscard]] SecureCommandOptionResult Parse(
      std::string_view options,
      const ConditionEvaluator& evaluate_condition);
  void Reset();

 private:
  [[nodiscard]] std::string RememberCondition(
      std::string_view condition);

  std::unordered_map<std::string, std::string>
      condition_text_by_folded_key_;
  std::mutex mutex_;
};

}

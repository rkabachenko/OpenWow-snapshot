#pragma once

#include <string>
#include <vector>

namespace openwow::data {

struct ValidationResult {
  bool ok{false};
  std::vector<std::string> errors;
};

ValidationResult ValidateWotlkGameDataPath(const std::string& path);

}

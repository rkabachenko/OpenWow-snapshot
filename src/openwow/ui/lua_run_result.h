#pragma once

#include <string>

namespace openwow::ui {

struct LuaRunResult {
  bool ok{false};
  std::string error;
};

}

#pragma once

namespace openwow::ui {

inline constexpr int kDefaultFrameXMLDebugLevel = -1;

void SetFrameXMLDebugLevel(int level);
[[nodiscard]] int GetFrameXMLDebugLevel();

}

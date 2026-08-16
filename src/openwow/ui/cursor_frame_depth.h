
#pragma once

namespace openwow::ui {

void SetCursorFrameDepth(float depth) noexcept;

[[nodiscard]] float GetCursorFrameDepth() noexcept;

[[nodiscard]] bool IsCursorFrameDepthActive() noexcept;

}

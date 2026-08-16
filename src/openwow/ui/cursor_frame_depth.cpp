
#include "openwow/ui/cursor_frame_depth.h"

#include "openwow/render/backend/bgfx/renderer_context_services.h"

namespace openwow::ui {

namespace {
float s_cursorFrameDepth = 0.0f;
}

void SetCursorFrameDepth(float depth) noexcept {
  s_cursorFrameDepth = depth;
}

float GetCursorFrameDepth() noexcept {
  return s_cursorFrameDepth;
}

bool IsCursorFrameDepthActive() noexcept {
  return openwow::render::IsRendererContextActive();
}

}

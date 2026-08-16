#pragma once

#include "openwow/ui/widgets/simple_frame.h"

#include <utility>

namespace openwow::render {
class WorldFrame;
}

namespace openwow::ui::widgets {

class CSimpleWorldFrame : public CSimpleFrame {
 public:
  using DeferredWorldRenderCallback = SimpleRenderBatchSink::DeferredRenderCallback;

  CSimpleWorldFrame() : CSimpleFrame(ScriptObjectType::WorldFrame) {
    SetFrameStrata(FrameStrata::World);
  }
  void BindWorldFrame(openwow::render::WorldFrame& world_frame) noexcept {
    world_frame_ = &world_frame;
  }

  void FireOnLeave(bool motion = false, bool clearDragState = true) override;

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::WorldFrame || CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "WorldFrame") || CSimpleFrame::IsTypeOf(typeName);
  }
  void RegisterLayerRenderCallbacks(SimpleRenderBatchSink& sink,
                                    int layerIndex) override {
    CSimpleFrame::RegisterLayerRenderCallbacks(sink, layerIndex);
    if (layerIndex != static_cast<int>(DrawLayer::Background) ||
        !deferredWorldRenderCallback_) {
      return;
    }

    sink.AddDeferredRenderCallback(deferredWorldRenderCallback_);
  }

  void SetDeferredWorldRenderCallback(DeferredWorldRenderCallback callback) {
    deferredWorldRenderCallback_ = std::move(callback);
  }

  [[nodiscard]] int GetRenderState() const noexcept { return renderState_; }
  void SetRenderState(int state) noexcept { renderState_ = state; }

  [[nodiscard]] bool IsInputEnabled() const noexcept { return inputEnabled_; }

 private:
  openwow::render::WorldFrame* world_frame_{nullptr};
  DeferredWorldRenderCallback deferredWorldRenderCallback_{};

  int renderState_{5};

  bool inputEnabled_{true};
};

}

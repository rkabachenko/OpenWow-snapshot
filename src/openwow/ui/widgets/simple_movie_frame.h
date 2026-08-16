#pragma once

#include "openwow/ui/widgets/simple_frame.h"

namespace openwow::ui::widgets {

class CSimpleMovieFrame final : public CSimpleFrame {
 public:
  CSimpleMovieFrame() : CSimpleFrame(ScriptObjectType::MovieFrame) {}

  [[nodiscard]] bool IsKindOf(ScriptObjectType type) const noexcept override {
    return type == ScriptObjectType::MovieFrame ||
           CSimpleFrame::IsKindOf(type);
  }

  [[nodiscard]] bool IsTypeOf(const char* type_name) const noexcept override {
    return StrCaseEq(type_name, "MovieFrame") ||
           CSimpleFrame::IsTypeOf(type_name);
  }

  [[nodiscard]] static constexpr const char* GetTypeName() noexcept {
    return "MovieFrame";
  }
};

}

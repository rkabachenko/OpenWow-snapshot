#pragma once

#include "openwow/ui/widgets/simple_frame.h"

namespace openwow::ui::widgets {

class CSimpleQuestPOIFrame : public CSimpleFrame {
 public:
  CSimpleQuestPOIFrame()
      : CSimpleFrame(ScriptObjectType::QuestPOIFrame) {}

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::QuestPOIFrame || CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "QuestPOIFrame") ||
           CSimpleFrame::IsTypeOf(typeName);
  }
};

}

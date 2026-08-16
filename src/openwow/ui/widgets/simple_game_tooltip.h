#pragma once

#include "openwow/ui/widgets/simple_frame.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui::widgets {

struct TooltipLine {
  std::string leftText;
  float leftR{1.0f}, leftG{1.0f}, leftB{1.0f};
  std::string rightText;
  float rightR{1.0f}, rightG{1.0f}, rightB{1.0f};
  bool isHeader{false};
};

class CSimpleGameTooltip : public CSimpleFrame {
 public:
  CSimpleGameTooltip() : CSimpleFrame(ScriptObjectType::GameTooltip) {}
  ~CSimpleGameTooltip() override;
  void SetOwnsDefaultTooltipState(const bool owns) noexcept {
    owns_default_tooltip_state_ = owns;
  }
  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::GameTooltip || CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "GameTooltip") || CSimpleFrame::IsTypeOf(typeName);
  }

  void ClearLines() { lines_.clear(); }
  void AddLine(const std::string& text, float r = 1.0f, float g = 1.0f,
               float b = 1.0f) {
    lines_.push_back({text, r, g, b, "", 1, 1, 1, false});
  }
  void AddDoubleLine(const std::string& left, const std::string& right,
                     float lr = 1.0f, float lg = 1.0f, float lb = 1.0f,
                     float rr = 1.0f, float rg = 1.0f, float rb = 1.0f) {
    lines_.push_back({left, lr, lg, lb, right, rr, rg, rb, false});
  }
  [[nodiscard]] size_t NumLines() const noexcept { return lines_.size(); }
  [[nodiscard]] const std::vector<TooltipLine>& GetLines() const noexcept {
    return lines_;
  }

  void SetOwner(CSimpleFrame* owner, const std::string& anchor = "ANCHOR_RIGHT") {
    owner_ = owner;
    anchor_ = anchor;
  }
  [[nodiscard]] CSimpleFrame* GetOwner() const noexcept { return owner_; }

  void SetUnit(const std::string& unit) { unitTarget_ = unit; }
  [[nodiscard]] const std::string& GetUnit() const noexcept {
    return unitTarget_;
  }

  void SetItem(uint32_t itemId) noexcept { itemId_ = itemId; }
  void SetSpell(uint32_t spellId) noexcept { spellId_ = spellId; }
  [[nodiscard]] uint32_t GetItemId() const noexcept { return itemId_; }
  [[nodiscard]] uint32_t GetSpellId() const noexcept { return spellId_; }

  void SetMinimumWidth(float w, bool force = false) noexcept {
    minWidth_ = w;
    forceMinWidth_ = force;
  }
  [[nodiscard]] float GetMinimumWidth() const noexcept { return minWidth_; }
  [[nodiscard]] bool IsForceMinWidth() const noexcept { return forceMinWidth_; }

  void SetPadding(float p) noexcept { padding_ = p; }
  [[nodiscard]] float GetPadding() const noexcept { return padding_; }

 private:
  bool owns_default_tooltip_state_{false};
  std::vector<TooltipLine> lines_;
  CSimpleFrame* owner_{nullptr};
  std::string anchor_{"ANCHOR_RIGHT"};
  std::string unitTarget_;
  uint32_t itemId_{0};
  uint32_t spellId_{0};
  float minWidth_{0.0f};

  bool forceMinWidth_{false};

  float padding_{0.0f};

};

}

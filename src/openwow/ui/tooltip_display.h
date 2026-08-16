#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui {

struct TooltipDisplayLine {
    std::string text;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;

    bool isDoubleWidth = false;
    std::string rightText;
    float rightR = 1.0f;
    float rightG = 1.0f;
    float rightB = 1.0f;
};

enum class TooltipAnchorMode : std::uint8_t {
    Mouse   = 0,
    Cursor  = 1,
    Frame   = 2,
    Default = 3,
};

class TooltipDisplay {
public:

    void ClearLines();

    void AddLine(const std::string& text,
                 float r = 1.0f, float g = 1.0f, float b = 1.0f);

    void AddDoubleLine(const std::string& leftText,
                       const std::string& rightText,
                       float lr = 1.0f, float lg = 1.0f, float lb = 1.0f,
                       float rr = 1.0f, float rg = 1.0f, float rb = 1.0f);

    [[nodiscard]] const std::vector<TooltipDisplayLine>& GetLines() const;
    [[nodiscard]] std::size_t GetLineCount() const;

    void SetOwner(const std::string& frameName, TooltipAnchorMode mode);
    [[nodiscard]] const std::string& GetOwner() const;
    [[nodiscard]] TooltipAnchorMode GetAnchorMode() const;

    [[nodiscard]] bool IsShown() const;
    void Show();
    void Hide();

    void SetItemTooltip(std::uint32_t itemId);
    void SetSpellTooltip(std::uint32_t spellId);
    void SetUnitTooltip(openwow::game::ObjectGuid guid);

    [[nodiscard]] std::string GetTooltipType() const;

    [[nodiscard]] std::uint32_t GetTooltipId() const;

    void FadeIn();
    void FadeOut();
    [[nodiscard]] bool IsFadingIn() const;
    [[nodiscard]] bool IsFadingOut() const;

    void Reset();

private:
    std::vector<TooltipDisplayLine> lines_;
    std::string ownerFrame_;
    TooltipAnchorMode anchorMode_ = TooltipAnchorMode::Default;
    bool shown_ = false;

    enum class TypeTag : std::uint8_t { None, Item, Spell, Unit };
    TypeTag typeTag_ = TypeTag::None;
    std::uint32_t typeId_ = 0;
    openwow::game::ObjectGuid unitGuid_;

    bool fadingIn_ = false;
    bool fadingOut_ = false;
};

}

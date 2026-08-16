
#pragma once

#include "openwow/ui/widgets/simple_font_string.h"

#include <cstdint>
#include <memory>

namespace openwow::ui::widgets {

class CSimpleFontStringLine;

struct CSimpleMessageScrollFrameDisplayNode {

    CSimpleMessageScrollFrameDisplayNode();
    ~CSimpleMessageScrollFrameDisplayNode();

    CSimpleMessageScrollFrameDisplayNode(const CSimpleMessageScrollFrameDisplayNode& other);
    CSimpleMessageScrollFrameDisplayNode& operator=(const CSimpleMessageScrollFrameDisplayNode&) = delete;

    CSimpleMessageScrollFrameDisplayNode(CSimpleMessageScrollFrameDisplayNode&& other) noexcept;
    CSimpleMessageScrollFrameDisplayNode& operator=(CSimpleMessageScrollFrameDisplayNode&&) = delete;

    [[nodiscard]] CSimpleFontStringLine* GetFontString() const noexcept {
        return fontString_;
    }

    void SetMessageLine(const void* line) noexcept { messageLine_ = line; }
    [[nodiscard]] const void* GetMessageLine() const noexcept {
        return messageLine_;
    }

private:

    CSimpleFontStringLine* fontString_{nullptr};

    const void* messageLine_{nullptr};
};

}

#include "openwow/ui/widgets/ui_script_helpers.h"
#include "openwow/ui/widgets/simple_font_string.h"

#include <cstdint>

namespace openwow::ui::widgets {

bool SetAlphaGradient(void* font_string, int alpha_start, int alpha_length) {
    if (!font_string) return false;
    return static_cast<CSimpleFontString*>(font_string)->SetAlphaGradient(
        alpha_start, alpha_length);
}

const char* GetFontPath(const void* font_string) {
    if (!font_string) return nullptr;

    const auto* fs = static_cast<const CSimpleFontString*>(font_string);
    const auto& face = fs->GetRender().GetFontFace();
    return face ? face->path().c_str() : nullptr;
}

void SetTextHeight(void* font_string, float height) {
    if (!font_string) return;
    static_cast<CSimpleFontString*>(font_string)->SetTextHeight(height);
}

void SetShadowOffset(void* font_string, const float offset[2]) {
    if (!font_string || !offset) return;
    auto* fs = static_cast<CSimpleFontString*>(font_string);
    fs->SetShadowOffset(offset[0], offset[1]);
}

}

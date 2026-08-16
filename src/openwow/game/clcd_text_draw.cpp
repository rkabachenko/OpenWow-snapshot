
#include "openwow/game/clcd_text_draw.h"

#include "openwow/game/clcd_gfx_base.h"

#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace openwow::game {

int CLCDText_RenderText(std::uint32_t* self, CLCDGfxBase* gfx) noexcept {

    auto width  = static_cast<std::int32_t>(self[kCLCDTextFieldRenderWidth]);
    auto height = static_cast<std::int32_t>(self[kCLCDTextFieldRenderHeight]);

    RECT rc;
    rc.left   = 0;
    rc.top    = 0;
    rc.right  = width;
    rc.bottom = height;

    int result = 0;

#ifdef _WIN32

    HDC hdc = gfx->GetHDC();

    result = ::DrawTextExW(
        hdc,
        reinterpret_cast<LPWSTR>(&self[kCLCDTextFieldTextBuffer]),
        static_cast<int>(self[kCLCDTextFieldTextLength]),
        &rc,
        static_cast<UINT>(self[kCLCDTextFieldFormatFlags]),
        reinterpret_cast<LPDRAWTEXTPARAMS>(&self[kCLCDTextFieldDrawTextParams]));

    if (self[kCLCDTextFieldInvert] != 0) {
        HDC hdc2 = gfx->GetHDC();
        result = ::InvertRect(hdc2, &rc);
    }
#else

    (void)gfx;
    (void)self[kCLCDTextFieldTextBuffer];
    (void)self[kCLCDTextFieldTextLength];
    (void)self[kCLCDTextFieldFormatFlags];
    (void)self[kCLCDTextFieldDrawTextParams];
    (void)self[kCLCDTextFieldInvert];
#endif

    return result;
}

}

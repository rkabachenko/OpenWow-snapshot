
#pragma once

#include "openwow/game/clcd_gfx_base.h"

namespace openwow::game {

inline constexpr int kLCDColorWidth  = 320;
inline constexpr int kLCDColorHeight = 240;
inline constexpr int kLCDColorBPP    = 32;

class CLCDGfxColor : public CLCDGfxBase {
public:
    CLCDGfxColor();

    ~CLCDGfxColor() override;

    int32_t Initialize() override;

    int32_t CopyPixels() override;

private:

    uint32_t m_nPixelFormatType = 0;

    uint8_t  m_aPixelBuffer[kLCDColorWidth * kLCDColorHeight * 4] = {};
};

}

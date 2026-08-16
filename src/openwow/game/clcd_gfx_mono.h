
#pragma once

#include "openwow/game/clcd_gfx_base.h"

namespace openwow::game {

inline constexpr int kLCDMonoWidth  = 160;
inline constexpr int kLCDMonoHeight = 43;
inline constexpr int kLCDMonoBPP    = 8;

class CLCDGfxMono : public CLCDGfxBase {
public:
    CLCDGfxMono();

    ~CLCDGfxMono() override;

    int32_t Initialize() override;

    int32_t Clear() override;

    int32_t CopyPixels() override;

private:

    uint32_t m_nPixelFormatType = 0;

    uint8_t  m_aPixelBuffer[kLCDMonoWidth * kLCDMonoHeight] = {};
};

}

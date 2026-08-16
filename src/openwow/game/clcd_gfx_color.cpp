
#include "openwow/game/clcd_gfx_color.h"

#include <cstring>

namespace openwow::game {

CLCDGfxColor::CLCDGfxColor()
    : CLCDGfxBase()
    , m_nPixelFormatType(3)
{
    m_pExternalBuffer = reinterpret_cast<uint8_t*>(&m_nPixelFormatType);
}

CLCDGfxColor::~CLCDGfxColor() = default;

int32_t CLCDGfxColor::Initialize()
{
    Shutdown();

    m_nWidth  = kLCDColorWidth;
    m_nHeight = kLCDColorHeight;

    int32_t hr = CLCDGfxBase::Initialize();
    if (hr < 0) {
        return hr;
    }

    hr = CreateBitmap(kLCDColorBPP);
    return (hr >= 0) ? kLCDGfxOK : hr;
}

int32_t CLCDGfxColor::CopyPixels()
{
    const size_t byteCount =
        static_cast<size_t>(4) *
        static_cast<size_t>(m_nWidth) *
        static_cast<size_t>(m_nHeight);

    m_nPixelFormatType = 3;
    std::memcpy(m_aPixelBuffer, m_pPixelBits, byteCount);
    return static_cast<int32_t>(reinterpret_cast<uintptr_t>(m_pExternalBuffer));
}

}

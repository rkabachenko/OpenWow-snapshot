
#include "openwow/game/clcd_gfx_mono.h"

#include <cstring>

namespace openwow::game {

CLCDGfxMono::CLCDGfxMono()
    : CLCDGfxBase()
    , m_nPixelFormatType(1)
{
    m_pExternalBuffer = reinterpret_cast<uint8_t*>(&m_nPixelFormatType);
}

CLCDGfxMono::~CLCDGfxMono() = default;

int32_t CLCDGfxMono::Initialize()
{
    Shutdown();

    m_nWidth  = kLCDMonoWidth;
    m_nHeight = kLCDMonoHeight;

    int32_t hr = CLCDGfxBase::Initialize();
    if (hr < 0) {
        return hr;
    }

    hr = CreateBitmap(kLCDMonoBPP);
    return (hr >= 0) ? kLCDGfxOK : hr;
}

int32_t CLCDGfxMono::Clear()
{
    std::memset(m_pPixelBits, 0,
                static_cast<size_t>(m_nWidth) * static_cast<size_t>(m_nHeight));
    return CLCDGfxBase::Clear();
}

int32_t CLCDGfxMono::CopyPixels()
{
    const size_t byteCount =
        static_cast<size_t>(m_nWidth) * static_cast<size_t>(m_nHeight);

    m_nPixelFormatType = 1;
    std::memcpy(m_aPixelBuffer, m_pPixelBits, byteCount);
    return static_cast<int32_t>(reinterpret_cast<uintptr_t>(m_pExternalBuffer));
}

}

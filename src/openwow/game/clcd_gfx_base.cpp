
#include "openwow/game/clcd_gfx_base.h"

#include <cstring>
#include <new>

namespace openwow::game {

CLCDGfxBase::CLCDGfxBase()
    : m_pExternalBuffer(nullptr)
    , m_nWidth(0)
    , m_nHeight(0)
    , m_pBitmapInfo(nullptr)
    , m_hDC(nullptr)
    , m_hBitmap(nullptr)
    , m_hPrevSelectedObj(nullptr)
    , m_pPixelBits(nullptr)
{
}

CLCDGfxBase::~CLCDGfxBase()
{
    Shutdown();
}

int32_t CLCDGfxBase::Initialize()
{
#ifdef _WIN32
    HDC hDC = ::CreateCompatibleDC(nullptr);
    m_hDC = hDC;
    if (hDC != nullptr) {
        return kLCDGfxOK;
    }
    Shutdown();
    return kLCDGfxFail;
#else

    return kLCDGfxFail;
#endif
}

int32_t CLCDGfxBase::Shutdown()
{
    int32_t result = 0;

#ifdef _WIN32

    if (m_hBitmap != nullptr) {
        ::DeleteObject(m_hBitmap);
        m_hBitmap = nullptr;
        m_pPixelBits = nullptr;
    }

    m_hPrevSelectedObj = nullptr;
    if (m_pBitmapInfo != nullptr) {
        delete[] reinterpret_cast<uint8_t*>(m_pBitmapInfo);
        m_pBitmapInfo = nullptr;
    }

    if (m_hDC != nullptr) {
        result = ::DeleteDC(m_hDC) ? 0 : -1;
        m_hDC = nullptr;
    }
#else

    m_hBitmap = nullptr;
    m_pPixelBits = nullptr;
    m_hPrevSelectedObj = nullptr;
    if (m_pBitmapInfo != nullptr) {
        delete[] reinterpret_cast<uint8_t*>(m_pBitmapInfo);
        m_pBitmapInfo = nullptr;
    }
    m_hDC = nullptr;
#endif

    m_nHeight = 0;
    m_nWidth  = 0;
    return result;
}

int32_t CLCDGfxBase::Clear()
{
#ifdef _WIN32
    RECT rc;
    rc.left   = 0;
    rc.top    = 0;
    rc.right  = m_nWidth;
    rc.bottom = m_nHeight;
    HBRUSH hBrush = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    return ::FillRect(m_hDC, &rc, hBrush);
#else
    return 0;
#endif
}

void CLCDGfxBase::BeginDraw()
{
#ifdef _WIN32
    if (m_hPrevSelectedObj == nullptr) {
        HGDIOBJ hOld = ::SelectObject(m_hDC, m_hBitmap);
        m_hPrevSelectedObj = hOld;
        ::SetTextColor(m_hDC, 0x00FFFFFFu);
        ::SetBkColor(m_hDC, 0x00000000u);
    }
#endif
}

void CLCDGfxBase::EndDraw()
{
#ifdef _WIN32
    if (m_hPrevSelectedObj != nullptr) {
        ::GdiFlush();
        ::SelectObject(m_hDC, m_hPrevSelectedObj);
        m_hPrevSelectedObj = nullptr;
    }
#endif
}

HDC CLCDGfxBase::GetHDC() const
{
    return m_hDC;
}

int32_t CLCDGfxBase::CopyPixels()
{
    if (m_pExternalBuffer == nullptr) {
        return 0;
    }
    const size_t byteCount =
        static_cast<size_t>(4) * static_cast<size_t>(m_nWidth) *
        static_cast<size_t>(m_nHeight);
    std::memcpy(m_pExternalBuffer + 4, m_pPixelBits, byteCount);
    return static_cast<int32_t>(reinterpret_cast<uintptr_t>(m_pExternalBuffer));
}

BITMAPINFO* CLCDGfxBase::GetBitmapInfo() const
{
    return m_pBitmapInfo;
}

HBITMAP CLCDGfxBase::GetHBITMAP() const
{
    return m_hBitmap;
}

int32_t CLCDGfxBase::CreateBitmap(int bitsPerPixel)
{
#ifdef _WIN32
    constexpr size_t kBitmapInfoAllocSize = 1068;
    auto* rawMem = new (std::nothrow) uint8_t[kBitmapInfoAllocSize]();
    m_pBitmapInfo = reinterpret_cast<BITMAPINFO*>(rawMem);
    if (m_pBitmapInfo == nullptr) {
        Shutdown();
        return kLCDGfxOutOfMem;
    }

    std::memset(rawMem, 0, kBitmapInfoAllocSize);
    m_pBitmapInfo->bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    m_pBitmapInfo->bmiHeader.biWidth       = m_nWidth;
    m_pBitmapInfo->bmiHeader.biHeight      = -m_nHeight;
    m_pBitmapInfo->bmiHeader.biPlanes      = 1;
    m_pBitmapInfo->bmiHeader.biBitCount    = static_cast<uint16_t>(bitsPerPixel);
    m_pBitmapInfo->bmiHeader.biCompression = 0;
    m_pBitmapInfo->bmiHeader.biSizeImage   =
        static_cast<uint32_t>(m_nWidth) *
        static_cast<uint32_t>(m_nHeight) *
        static_cast<uint32_t>(bitsPerPixel) / 8u;
    m_pBitmapInfo->bmiHeader.biXPelsPerMeter = 3200;
    m_pBitmapInfo->bmiHeader.biYPelsPerMeter = 3200;
    m_pBitmapInfo->bmiHeader.biClrUsed       = 256;
    m_pBitmapInfo->bmiHeader.biClrImportant  = 256;

    for (int i = 0; i < 256; ++i) {
        uint8_t value = (i <= 128) ? 0x00u : 0xFFu;
        m_pBitmapInfo->bmiColors[i].rgbBlue     = value;
        m_pBitmapInfo->bmiColors[i].rgbGreen    = value;
        m_pBitmapInfo->bmiColors[i].rgbRed      = value;
        m_pBitmapInfo->bmiColors[i].rgbReserved  = 0;
    }

    void* pPixelBits = nullptr;
    HBITMAP hBitmap = ::CreateDIBSection(
        m_hDC,
        m_pBitmapInfo,
        DIB_RGB_COLORS,
        &pPixelBits,
        nullptr,
        0);
    m_hBitmap = hBitmap;

    if (hBitmap == nullptr) {
        Shutdown();
        return kLCDGfxFail;
    }

    m_pPixelBits = pPixelBits;
    return kLCDGfxOK;

#else

    (void)bitsPerPixel;
    Shutdown();
    return kLCDGfxFail;
#endif
}

}

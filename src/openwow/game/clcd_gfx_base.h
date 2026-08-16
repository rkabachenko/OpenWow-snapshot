
#pragma once

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else

using HDC      = void*;
using HBITMAP  = void*;
using HGDIOBJ  = void*;
using HBRUSH   = void*;

struct BITMAPINFOHEADER {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};

struct RGBQUAD {
    uint8_t rgbBlue;
    uint8_t rgbGreen;
    uint8_t rgbRed;
    uint8_t rgbReserved;
};

struct BITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD          bmiColors[1];
};

struct RECT {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};
#endif

namespace openwow::game {

inline constexpr int32_t kLCDGfxOK         = 0;
inline constexpr int32_t kLCDGfxFail       = static_cast<int32_t>(0x80004005u);
inline constexpr int32_t kLCDGfxOutOfMem   = static_cast<int32_t>(0x8007000Eu);

class CLCDGfxBase {
public:
    CLCDGfxBase();

    virtual ~CLCDGfxBase();

    virtual int32_t Initialize();

    virtual int32_t Shutdown();

    virtual int32_t Clear();

    virtual void BeginDraw();

    virtual void EndDraw();

    virtual HDC GetHDC() const;

    virtual int32_t CopyPixels();

    virtual BITMAPINFO* GetBitmapInfo() const;

    virtual HBITMAP GetHBITMAP() const;

    [[nodiscard]] int GetWidth()  const { return m_nWidth; }
    [[nodiscard]] int GetHeight() const { return m_nHeight; }
    [[nodiscard]] void* GetPixelBits() const { return m_pPixelBits; }

protected:

    int32_t CreateBitmap(int bitsPerPixel);

    uint8_t*      m_pExternalBuffer = nullptr;

    int           m_nWidth  = 0;
    int           m_nHeight = 0;

    BITMAPINFO*   m_pBitmapInfo = nullptr;

    HDC           m_hDC = nullptr;

    HBITMAP       m_hBitmap = nullptr;

    HGDIOBJ       m_hPrevSelectedObj = nullptr;

    void*         m_pPixelBits = nullptr;
};

static_assert(sizeof(CLCDGfxBase) <= 64,
              "CLCDGfxBase must fit within expected IDA size "
              "(vtable + 8 fields = 36 bytes in original, "
              "C++ class may be slightly larger due to vptr)");

}

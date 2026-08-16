
#include "openwow/game/cez_lcd_page_draw.h"

#include "openwow/game/clcd_gfx_base.h"

#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace openwow::game {

namespace {

inline std::uint32_t& Field(void* obj, int index) {
    return reinterpret_cast<std::uint32_t*>(obj)[index];
}

inline std::uint32_t Field(const void* obj, int index) {
    return reinterpret_cast<const std::uint32_t*>(obj)[index];
}

inline bool NodeIsShown(const void* node) {
    return Field(node, kDrawFieldShowFlag) != 0;
}

#ifdef _WIN32

inline std::int32_t NodeGetWidth(const void* node) {
    return static_cast<std::int32_t>(Field(node, kDrawFieldWidth));
}

inline std::int32_t NodeGetHeight(const void* node) {
    return static_cast<std::int32_t>(Field(node, kDrawFieldHeight));
}

inline std::int32_t NodeGetPosX(const void* node) {
    return static_cast<std::int32_t>(Field(node, kDrawFieldPosX));
}

inline std::int32_t NodeGetPosY(const void* node) {
    return static_cast<std::int32_t>(Field(node, kDrawFieldPosY));
}

inline std::int32_t NodeGetScrollX(const void* node) {
    return static_cast<std::int32_t>(Field(node, kDrawFieldScrollX));
}

inline std::int32_t NodeGetScrollY(const void* node) {
    return static_cast<std::int32_t>(Field(node, kDrawFieldScrollY));
}
#endif

inline bool IsSentinelOrNull(std::uint32_t ptr) {
    return (ptr & 1u) != 0 || ptr == 0;
}

inline void* ListNodeGetChild(std::uint32_t nodeAddr) {
    return reinterpret_cast<void*>(
        *reinterpret_cast<std::uint32_t*>(nodeAddr + kChildNodeDataOffset));
}

inline std::uint32_t ListNodeGetNext(std::uint32_t nodeAddr) {
    return *reinterpret_cast<std::uint32_t*>(nodeAddr + kChildNodeNextOffset);
}

}

int CEzLcdPageNode_Draw(void* node, CLCDGfxBase* gfx) noexcept {
    if (!NodeIsShown(node)) {
        return 0;
    }

    if (Field(node, kDrawFieldHasSubscreen) != 0) {

        auto* embeddedNode =
            reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(node) +
                kDrawFieldEmbeddedVtable * sizeof(std::uint32_t));
        CEzLcdPageNode_Draw(embeddedNode, gfx);
    }
    else if (Field(node, kDrawFieldHasBgColor) != 0) {
#ifdef _WIN32
        COLORREF bgColor = static_cast<COLORREF>(Field(node, kDrawFieldBgColor));
        HBRUSH hBrush = ::CreateSolidBrush(bgColor);

        HDC hdc = gfx->GetHDC();
        HGDIOBJ hOldBrush = ::SelectObject(hdc, hBrush);

        std::int32_t width  = NodeGetWidth(node);
        std::int32_t height = NodeGetHeight(node);
        hdc = gfx->GetHDC();
        ::Rectangle(hdc, 0, 0, width, height);

        hdc = gfx->GetHDC();
        ::SelectObject(hdc, hOldBrush);
        ::DeleteObject(hBrush);
#endif
    }

    std::uint32_t iter = Field(node, kDrawFieldChildListTail);

    if (IsSentinelOrNull(iter)) {
        iter = 0;
    }

    while (!IsSentinelOrNull(iter)) {
        void* child = ListNodeGetChild(iter);

        if (NodeIsShown(child)) {
#ifdef _WIN32

            std::int32_t childPosX  = NodeGetPosX(child);
            std::int32_t childPosY  = NodeGetPosY(child);
            std::int32_t parentPosX = NodeGetPosX(node);
            std::int32_t parentPosY = NodeGetPosY(node);
            std::int32_t childW     = NodeGetWidth(child);
            std::int32_t childH     = NodeGetHeight(child);

            std::int32_t absX = childPosX + parentPosX;
            std::int32_t absY = childPosY + parentPosY;
            std::int32_t absRight  = absX + childW;
            std::int32_t absBottom = absY + childH;

            HRGN clipRgn = ::CreateRectRgn(absX, absY, absRight, absBottom);

            HDC hdc = gfx->GetHDC();
            ::SelectClipRgn(hdc, clipRgn);
            ::DeleteObject(clipRgn);

            POINT savedPt = {0, 0};
            hdc = gfx->GetHDC();
            ::SetViewportOrgEx(hdc, absX, absY, &savedPt);

            std::int32_t scrollX = NodeGetScrollX(child);
            std::int32_t scrollY = NodeGetScrollY(child);
            hdc = gfx->GetHDC();
            ::OffsetViewportOrgEx(hdc, scrollX, scrollY, nullptr);
#endif

            CEzLcdPageNode_Draw(child, gfx);

#ifdef _WIN32
            hdc = gfx->GetHDC();
            ::SelectClipRgn(hdc, nullptr);

            hdc = gfx->GetHDC();
            ::SetViewportOrgEx(hdc, savedPt.x, savedPt.y, nullptr);

            hdc = gfx->GetHDC();
            ::OffsetViewportOrgEx(hdc, 0, 0, nullptr);
#endif
        }

        iter = ListNodeGetNext(iter);
    }

    return static_cast<int>(iter);
}

}

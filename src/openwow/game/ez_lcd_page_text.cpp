
#include "openwow/game/ez_lcd_page_text.h"

#include "openwow/core/storm_string.h"
#include "openwow/game/clcd_base.h"
#include "openwow/game/lcd_system.h"

#include <cstdint>
#include <cstring>

namespace openwow::game {

namespace {

static void CLCDText_Construct(std::uint32_t* self) noexcept {
    std::memset(self, 0, kCLCDTextAllocSize);

    self[5]  = kCLCDBaseDefaultShowFlag;
    self[11] = kCLCDBaseDefaultAlignment;
    self[12] = kCLCDBaseDefaultVisibleMode;
    self[14] = kCLCDBaseDefaultBackgroundColor;

    self[146] = 2048;
    self[147] = 1;
    self[148] = 20;
}

inline void VCall_SetOwner(void* , void* , void* ) {
}

inline void VCall_SetOrigin(void* , int , int ) {
}

inline void VCall_SetFontFaceName(void* , const char* ) {
}

inline void VCall_SetForegroundColor(void* , int ) {
}

inline void VCall_SetAlignment(void* , int ) {
}

inline void VCall_SetFontWeight(void* , int ) {
}

inline void VCall_SetPos(void* , int , int ) {
}

inline void VCall_SetFontPointSize(void* , int ) {
}

inline void VCall_SetLogicalOrigin(void* , int , int ) {
}

inline void VCall_SetVisible(void* , int ) {
}

inline void VCall_SetEndEllipsis(void* , int ) {
}

inline void VCall_SetText(void* , const char* ) {
}

}

void* CEzLcdPage_AddNewMonoText(
    [[maybe_unused]] void* page,
    void* owner1,
    void* owner2,
    int deviceType,
    int fontSizeCategory,
    int fgColor,
    int yPos,
    const char* text,
    int numLines)
{
    int fontSize = 8;
    int yAdjust  = -1;

    if (deviceType != kDeviceTypeMono) {
        return nullptr;
    }

    auto* raw = static_cast<std::uint8_t*>(
        core::SMemAlloc(kCLCDTextAllocSize, ".\\EZ_LCD_Page.cpp", 0x298, 0));
    if (!raw) {
        return nullptr;
    }
    CLCDText_Construct(reinterpret_cast<std::uint32_t*>(raw));
    void* textObj = raw;

    VCall_SetOwner(textObj, owner1, owner2);

    VCall_SetOrigin(textObj, 0, 0);

    VCall_SetFontFaceName(textObj, kDefaultFontFace);

    VCall_SetForegroundColor(textObj, fgColor);

    VCall_SetAlignment(textObj, kTextAlignRight);

    switch (fontSizeCategory) {
        case 6:
        case 7:
            fontSize = 7;
            yAdjust  = -3;
            break;

        case 8:
            fontSize = 8;
            yAdjust  = -1;
            break;

        case 12:
            VCall_SetFontWeight(textObj, kFontWeightBold);
            fontSize = 12;
            yAdjust  = -2;
            break;

        default:

            break;
    }

    VCall_SetPos(textObj, yPos, 0);

    VCall_SetFontPointSize(textObj, fontSize);

    VCall_SetLogicalOrigin(textObj, 0, yAdjust);

    VCall_SetVisible(textObj, 1);

    if (numLines > 1) {
        VCall_SetEndEllipsis(textObj, 1);
    }

    VCall_SetText(textObj, text);

    return textObj;
}

void* CEzLcdPage_AddNewColorText(
    [[maybe_unused]] void* page,
    void* owner1,
    void* owner2,
    int deviceType,
    int fontSizeCategory,
    int fgColor,
    int yPos,
    const char* text,
    int numLines,
    int lineCount)
{
    int height   = 24;
    int fontSize = 16;
    int yAdjust  = -3;

    if (deviceType != kDeviceTypeMono) {
        return nullptr;
    }

    auto* raw = static_cast<std::uint8_t*>(
        core::SMemAlloc(kCLCDTextAllocSize, ".\\EZ_LCD_Page.cpp", 0x298, 0));
    if (!raw) {
        return nullptr;
    }
    CLCDText_Construct(reinterpret_cast<std::uint32_t*>(raw));
    void* textObj = raw;

    VCall_SetOwner(textObj, owner1, owner2);

    VCall_SetOrigin(textObj, 0, 0);

    VCall_SetFontFaceName(textObj, kDefaultFontFace);

    VCall_SetForegroundColor(textObj, fgColor);

    VCall_SetAlignment(textObj, kTextAlignLeft);

    switch (fontSizeCategory) {
        case 6:
            height   = 14 * lineCount;
            fontSize = 8;
            yAdjust  = 0;
            break;

        case 7:
            height   = 20 * lineCount;
            fontSize = 12;
            yAdjust  = -2;
            break;

        case 8:
            height   = 24 * lineCount;
            fontSize = 16;
            yAdjust  = -3;
            break;

        case 12:
            height   = 37 * lineCount;
            fontSize = 24;
            yAdjust  = -3;
            break;

        default:
            break;
    }

    VCall_SetPos(textObj, yPos, height);

    VCall_SetFontPointSize(textObj, fontSize);

    VCall_SetLogicalOrigin(textObj, 0, yAdjust);

    VCall_SetVisible(textObj, 1);

    VCall_SetText(textObj, text);

    if (numLines > 1) {
        VCall_SetEndEllipsis(textObj, 1);
    }

    return textObj;
}

void* CEzLcd_AddNewText(
    void* ezLcd,
    void* owner1,
    int fontSizeCategory,
    int fgColor,
    int yPos,
    const char* text,
    int numLines,
    int lineCount,

    [[maybe_unused]] int extra)
{
    if (!ezLcd) {
        return nullptr;
    }

    auto* ctx = static_cast<std::uint32_t*>(ezLcd);

    const auto* connection =
        reinterpret_cast<const EZLCDDisplayConnectionStorage*>(&ctx[66]);

    const auto monoDevice   = EZ_LCD_DisplayConnection_GetMonochromeDevice(*connection);
    const auto colorDevice  = EZ_LCD_DisplayConnection_GetColorDevice(*connection);
    const auto activeDevice = ctx[CEzLcdFields::kActiveDevice];

    void* pageRoot = nullptr;
    bool isMono = false;

    if (activeDevice == monoDevice) {
        pageRoot = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(ctx[CEzLcdFields::kMonoPage]));
        isMono = true;
    } else if (activeDevice == colorDevice) {
        pageRoot = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(ctx[CEzLcdFields::kColorPage]));
    } else {
        return nullptr;
    }

    if (!pageRoot) {
        return nullptr;
    }

    if (isMono) {
        return CEzLcdPage_AddNewMonoText(
            pageRoot, owner1, pageRoot, 1,
            fontSizeCategory, fgColor, yPos, text, numLines);
    }

    return CEzLcdPage_AddNewColorText(
        pageRoot, owner1, pageRoot, 1,
        fontSizeCategory, fgColor, yPos, text, numLines, lineCount);
}

}

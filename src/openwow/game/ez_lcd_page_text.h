
#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::game {

static constexpr std::size_t kCLCDTextAllocSize = 664;

enum class EZLCDFontSizeCategory : int {
    kSmall6  = 6,
    kSmall7  = 7,
    kMedium  = 8,
    kBig     = 12,
};

static constexpr int kFontWeightNormal = 0;
static constexpr int kFontWeightBold   = 700;

static constexpr int kTextAlignLeft   = 1;
static constexpr int kTextAlignRight  = 2;

static constexpr int kDeviceTypeMono  = 1;

static constexpr const char* kDefaultFontFace = "Microsoft Sans Serif";

void* CEzLcdPage_AddNewMonoText(
    void* page,
    void* owner1,
    void* owner2,
    int deviceType,
    int fontSizeCategory,
    int fgColor,
    int yPos,
    const char* text,
    int numLines);

void* CEzLcdPage_AddNewColorText(
    void* page,
    void* owner1,
    void* owner2,
    int deviceType,
    int fontSizeCategory,
    int fgColor,
    int yPos,
    const char* text,
    int numLines,
    int lineCount);

void* CEzLcd_AddNewText(
    void* ezLcd,
    void* owner1,
    int fontSizeCategory,
    int fgColor,
    int yPos,
    const char* text,
    int numLines,
    int lineCount,
    int extra);

}

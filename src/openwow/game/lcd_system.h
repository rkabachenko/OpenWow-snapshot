
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "openwow/ui/xml/xml_tree.h"
#include <vector>

namespace openwow::game {

class ObjectManager;

struct LCDElementEntry {
    uint32_t  nameHash;
    uint32_t  reserved1;
    uint32_t  reserved2;
    uint32_t  reserved3;
    uint32_t  reserved4;
    const char* name;
    void*     lcdHandle;
    int32_t   posX;
    int32_t   posY;
    std::string text;
};

struct LCDHandle {
    uint32_t  vtable;
    uint32_t  field_04;
    uint32_t  field_08;
    uint32_t  field_0C;
    uint32_t  field_10;
    uint32_t  field_14;
    uint32_t  field_18;
    uint32_t  field_1C;
    uint32_t  field_20;
    void*     rcstring;
    uint32_t  field_28;

};

struct ChatMessageEntry {
    uint32_t  linkPrev;
    uint32_t  linkNext;
    char      data[72];
};

static_assert(sizeof(ChatMessageEntry) == 80, "ChatMessageEntry must be 80 bytes");

struct BitmapByIconEntry {
    uint32_t  field_00;
    uint32_t  field_04;
    uint32_t  field_08;
    uint32_t  field_0C;
    uint32_t  field_10;
    uint32_t  reserved1;
    uint32_t  reserved2;
    uint32_t  field_1C;
    uint32_t  field_20;
};

static_assert(sizeof(BitmapByIconEntry) == 36, "BitmapByIconEntry must be 36 bytes");

struct UMapData {
    int32_t   sizeX    = 0;
    int32_t   sizeY    = 0;
    int32_t   offsetX  = 0;
    int32_t   offsetY  = 0;
};

struct POIIconCacheEntry {
    int32_t   iconIndex;
    uint32_t  reserved1;
    uint32_t  reserved2;
    uint32_t  reserved3;
    uint32_t  reserved4;
    uint32_t  reserved5;
    void*     textureHandle;
    int32_t   texCoordA;
    int32_t   texCoordB;
};

static constexpr int kPOIAtlasColumns = 14;
static constexpr int kPOIAtlasTileSize = 18;
static constexpr int kPOIAtlasTileBorder = 1;

struct EZLCDBitmapElement {
    uint32_t  fields_00_3C[16] = {};
    uint32_t  textureHandle = 0;
    uint32_t  bitmapInfo    = 0;
    float     opacity       = 1.0f;
    uint32_t  alpha         = 1;
    uint32_t  fields_50_54[2] = {};
};

static_assert(sizeof(EZLCDBitmapElement) == 88,
              "EZLCDBitmapElement must be 88 bytes (IDA SMemAlloc size)");
static_assert(offsetof(EZLCDBitmapElement, textureHandle) == 0x40,
              "textureHandle must be at IDA offset +0x40");
static_assert(offsetof(EZLCDBitmapElement, alpha) == 0x4C,
              "alpha must be at IDA offset +0x4C");

static constexpr int32_t kEZLCDResultOK   = 0;
static constexpr int32_t kEZLCDResultFail = static_cast<int32_t>(0x80004005u);

static constexpr uint32_t kLCDFontBig    = 12;
static constexpr uint32_t kLCDFontMedium = 8;
static constexpr uint32_t kLCDFontSmall  = 7;

static constexpr int kLCDAlignLeft   = 0;
static constexpr int kLCDAlignCenter = 1;
static constexpr int kLCDAlignRight  = 2;

using EZLCDDeviceHandle = std::uint32_t;

enum class EZLCDDeviceSupport : std::uint32_t {
    MonochromeOnly = 0,
    ColorOnly = 1,
    MonochromeAndColor = 2,
};

struct EZLCDDisplayConnectionStorage {
    std::uint32_t reserved_00 = 0;
    std::uint32_t reserved_04 = 0;
    std::uint32_t reserved_08 = 0;
    std::uint32_t reserved_0C = 0;
    std::uint32_t reserved_10 = 0;
    std::uint32_t reserved_14 = 0;
    std::uint32_t reserved_18 = 0;
    std::uint32_t reserved_1C = 0;
    std::uint32_t reserved_20 = 0;
    std::uint32_t reserved_24 = 0;
    std::uint32_t reserved_28 = 0;
    std::uint32_t reserved_2C = 0;
    EZLCDDeviceHandle color_device = 0;
    std::uint32_t reserved_34 = 0;
    EZLCDDeviceHandle monochrome_device = 0;
};

static_assert(offsetof(EZLCDDisplayConnectionStorage, color_device) == 0x30,
              "color_device must stay at IDA offset +0x30");
static_assert(
    offsetof(EZLCDDisplayConnectionStorage, monochrome_device) == 0x38,
    "monochrome_device must stay at IDA offset +0x38");
static_assert(sizeof(EZLCDDisplayConnectionStorage) == 0x3C,
              "EZLCDDisplayConnectionStorage must match the IDA slice size");

struct EZLCDPageExtent {
    int width = 0;
    int height = 0;
};

static constexpr EZLCDPageExtent kEZLCDColorPageExtent{320, 240};

struct LCDRuntimeStateSnapshot {
    bool has_context = false;
    bool initialized = false;
    bool deferred_init_pending = false;
    bool active = false;
    bool battlefield_mode = false;
    bool update_handler_registered = false;
    std::size_t element_count = 0;
    std::size_t poi_icon_cache_count = 0;
    int chat_alert_count = 0;
    int previous_texture_handle = 0;
    int european_numbers = 0;
    int current_page = 0;

    int previous_page = 0;

    int display_mode = 0;

    bool is_wand_class = false;

};

inline EZLCDDeviceHandle EZ_LCD_DisplayConnection_GetColorDevice(
    const EZLCDDisplayConnectionStorage& connection) {
    return connection.color_device;
}

inline EZLCDDeviceHandle EZ_LCD_DisplayConnection_GetMonochromeDevice(
    const EZLCDDisplayConnectionStorage& connection) {
    return connection.monochrome_device;
}

inline EZLCDDeviceHandle EZ_LCD_SelectInitialDevice(
    const EZLCDDisplayConnectionStorage& connection,
    EZLCDDeviceSupport support) {
    EZLCDDeviceHandle active_device = 0;
    const auto raw_support = static_cast<std::uint32_t>(support);

    if (raw_support == 1u || raw_support == 2u) {
        active_device = EZ_LCD_DisplayConnection_GetColorDevice(connection);
    }
    if (raw_support == 0u || raw_support == 2u) {
        active_device =
            EZ_LCD_DisplayConnection_GetMonochromeDevice(connection);
    }

    return active_device;
}

inline bool EZ_LCD_UsesColorDevice(
    const EZLCDDisplayConnectionStorage& connection,
    EZLCDDeviceHandle active_device) {
    return active_device == EZ_LCD_DisplayConnection_GetColorDevice(connection);
}

[[nodiscard]] inline const char* LCD_GetAmmoFieldNameForClassId(std::uint8_t class_id) {
    switch (class_id) {
        case 1:
        case 3:
        case 4:
            return "AMMO";
        case 9:
            return "SOUL_SHARD";
        default:
            return "";
    }
}

void LCD_Initialize();
void LCD_InitializeStrings(const ObjectManager& objects);
void LCD_Shutdown();
[[nodiscard]] LCDRuntimeStateSnapshot LCD_GetRuntimeStateSnapshot();
void LCD_OnLoginUpdate();
void LCD_OnAuctionOutbidNotification(const char* message);
void LCD_OnAuctionWonNotification(const char* message);
void LCD_OnAuctionSoldNotification(const char* message);
void LCD_OnAuctionExpiredNotification(const char* message);
void LCD_OnIdleMessage();
void LCD_OnBattlefieldStatus(const ObjectManager& objects);
void LCD_OnAcceptBattlefieldPort();
void LCD_OnBattlefieldStatusCleared();
void LCD_OnPlayerEnterWorld(const ObjectManager& objects);
void LCD_OnPlayerLeaveWorld();
void LCD_OnWorldLogout();
void LCD_SetupPlayerClassDisplay(const ObjectManager& objects);

void LCD_UpdateBattlefieldScoreboard(const ObjectManager& objects);

int LCD_SetEuropeanNumbers(int value);

void LCD_ApplyEuropeanNumberFormat(char* str);

void ULCDHANDLE_SetText(LCDElementEntry& entry, const char* text);

void LCD_SetElementTextUTF8(void* lcdHandle, const char* utf8Text);

void LCD_SetFloatStatField(const char* fieldName, float value, bool asPercent);

void LCD_SetDamageRangeField(const char* fieldName, int minDmg, int maxDmg);

void LCD_SetStringStatField(const char* fieldName, const char* value);

void LCD_SetLabelOnly(const char* fieldName, const char* utf8Text);

[[nodiscard]] int32_t EZ_LCD_Page_SetBitmapAlpha(EZLCDBitmapElement* element,
                                                  uint32_t alpha);

[[nodiscard]] int32_t EZ_LCD_Page_SetBitmapTexture(EZLCDBitmapElement* element,
                                                    uint32_t texture);

[[nodiscard]] int32_t EZ_LCD_Page_SetElementSize(void* element,
                                                  int32_t width, int32_t height);

[[nodiscard]] int32_t EZ_LCD_Page_SetBitmapPosition(EZLCDBitmapElement* element,
                                                     int32_t x, int32_t y);

[[nodiscard]] EZLCDBitmapElement* EZ_LCD_AddNewBitmap(
    void* ez_lcd_context,
    int32_t origin_x, int32_t origin_y,
    int32_t size_a, int32_t size_b);

int LCD_ScreenNameToId(const char* screenName);

bool LCD_ParseGlobals(const ui::xml::CXMLNode* xmlNode, void* errorCtx);

bool LCD_ParseScreen(const ui::xml::CXMLNode* xmlNode, void* errorCtx);

int32_t CEzLcd_CreateSubscreen(void* ez_lcd_context, int32_t screenId);

void CEzLcd_SelectPage(void* ez_lcd_context, int32_t screenId);

void CEzLcd_SetActivePage(void* ez_lcd_context, int32_t screenId);

void CEzLcd_SetPageBGColor(void* ez_lcd_context, uint32_t color);

void CEzLcd_RenderActivePage(void* ez_lcd_context, int32_t pageIndex);

void CEzLcd_SetUpdateFlags(void* ez_lcd_context, uint32_t flags);

struct CEzLcdConnectParams {
    const char* app_name = nullptr;
    EZLCDDeviceSupport device_support = EZLCDDeviceSupport::MonochromeAndColor;
    int32_t config_a = 0;
    int32_t config_b = 0;
    const EZLCDPageExtent* page_extent = nullptr;

    struct CallbackPair {
        std::uint32_t function = 0;
        std::uint32_t context = 0;
    };
    const CallbackPair* callback = nullptr;
};

namespace CEzLcdFields {
    inline constexpr std::size_t kDeviceSupport    = 93;
    inline constexpr std::size_t kMonoPage         = 102;
    inline constexpr std::size_t kColorPage        = 103;
    inline constexpr std::size_t kBusyFlag1        = 106;
    inline constexpr std::size_t kBusyFlag2        = 107;
    inline constexpr std::size_t kReserved1B0      = 108;
    inline constexpr std::size_t kReserved1B4      = 109;
    inline constexpr std::size_t kConnecting       = 110;
    inline constexpr std::size_t kConnected        = 111;
    inline constexpr std::size_t kPageExtentPtr    = 112;
    inline constexpr std::size_t kConfigB          = 113;
    inline constexpr std::size_t kConfigA          = 114;
    inline constexpr std::size_t kSentinel1CC      = 115;
    inline constexpr std::size_t kSentinel1D0      = 116;
    inline constexpr std::size_t kCallbackFunction = 128;
    inline constexpr std::size_t kCallbackContext  = 129;
    inline constexpr std::size_t kActiveDevice     = 130;

    inline constexpr std::size_t kMonoPageCapacity  = 94;
    inline constexpr std::size_t kMonoPageCount     = 95;
    inline constexpr std::size_t kMonoPageData      = 96;
    inline constexpr std::size_t kMonoGrowthStep    = 97;

    inline constexpr std::size_t kColorPageCapacity = 98;
    inline constexpr std::size_t kColorPageCount    = 99;
    inline constexpr std::size_t kColorPageData     = 100;
    inline constexpr std::size_t kColorGrowthStep   = 101;

    inline constexpr std::size_t kMonoPageArrayCapacity  = 7;
    inline constexpr std::size_t kColorPageArrayCapacity = 13;
}

[[nodiscard]] int32_t CEzLcd_Connect(void* ez_lcd_context,
                                     const CEzLcdConnectParams& params);

void LCD_SetPageStateForTesting(int currentPage, int previousPage);

struct CEzLcdPageArraySnapshot {
    std::size_t mono_count  = 0;
    std::size_t color_count = 0;
    std::size_t mono_capacity  = 0;
    std::size_t color_capacity = 0;
    std::size_t total_allocated_subscreens = 0;

    void* mono_page_at(int index) const;

    void* color_page_at(int index) const;

    const std::vector<void*>* mono_pages_ptr  = nullptr;
    const std::vector<void*>* color_pages_ptr = nullptr;
};

[[nodiscard]] CEzLcdPageArraySnapshot LCD_GetPageArraySnapshot();

}

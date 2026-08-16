
#include "openwow/game/lcd_system.h"

#include "openwow/core/decimal_parse.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/storm_string.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/cez_lcd_page_timer.h"
#include "openwow/game/cez_lcd_subscreen_ctor.h"
#include "openwow/game/object_manager.h"
#include "openwow/platform/adapters/win32/win32_compat.h"
#include "openwow/ui/xml/xml_tree.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define OPENWOW_STRTOK_R strtok_s
#else
#define OPENWOW_STRTOK_R strtok_r
#endif

namespace openwow::game {

using openwow::core::SStrCmpNoCase;
using openwow::core::SStrCopy;
using openwow::core::SStrHashCI;

void LCD_UpdateSoulShardDisplay(const char* fieldName, int count);

namespace {

constexpr std::size_t kLCDContextSize = 524;

using LCDElementRegistry = std::unordered_map<std::string, LCDElementEntry>;
using LCDPOIIconTextureCache = std::unordered_map<int, POIIconCacheEntry>;

std::unique_ptr<std::byte[]> g_lcdContextStorage;

LCDElementRegistry g_lcdElementRegistry;

std::vector<UMapData> g_lcdMapDataEntries;
std::vector<int> g_lcdPOIIconEntries;
LCDPOIIconTextureCache g_lcdPOIIconTextureCache;

std::vector<void*> g_monoPages;
std::vector<void*> g_colorPages;
std::vector<std::unique_ptr<char[]>> g_allocatedSubscreens;

void LCD_ClearPOIIconTextureCache() {
    g_lcdPOIIconTextureCache.clear();
}

}

static void* g_lcdContext = nullptr;

static bool g_lcdInitialized = false;

static bool g_lcdDeferredInit = false;

static bool g_lcdActive = false;

static bool g_lcdBattlefieldMode = false;

static std::uint32_t g_lcdBattlefieldStartTick = 0;

static const void* g_activePlayerUnit = nullptr;

static bool g_lcdIsWandClass = false;

static int g_lcdDisplayMode = 0;

static int g_chatAlertCount = 0;

static int g_prevTextureHandle = 0;

static int g_europeanNumbers = 0;

static bool g_lcdUpdateHandlerRegistered = false;

static int g_lcdCurrentPage = 0;

static int g_lcdPreviousPage = 0;

static void* LCD_LookupElementHandle(const char* name) {
    auto it = g_lcdElementRegistry.find(name);
    if (it != g_lcdElementRegistry.end()) {
        return it->second.lcdHandle;
    }
    return nullptr;
}

static void CEzLcdPage_SetElementVisible(void* , bool ) {

}

static void LCD_SetElementVisible(const char* elementName, bool visible) {
    void* handle = LCD_LookupElementHandle(elementName);
    if (handle) {
        CEzLcdPage_SetElementVisible(handle, visible);
    }
}

static void LCD_SetRegisteredElementText(const char* elementName) {
    void* handle = LCD_LookupElementHandle(elementName);
    if (handle) {
        auto it = g_lcdElementRegistry.find(elementName);
        if (it != g_lcdElementRegistry.end()) {
            LCD_SetElementTextUTF8(handle, it->second.name);
        }
    }
}

static void LCD_SetPage(int pageId) {
    if (pageId == g_lcdCurrentPage) return;
    g_lcdPreviousPage = g_lcdCurrentPage;
    g_lcdCurrentPage = pageId;
}

void LCD_OnLoginUpdate() {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;
    LCD_SetPage(0);
}

void LCD_OnWorldLogout() {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;

    g_lcdActive = false;
    LCD_SetPage(0);
    g_lcdBattlefieldMode = false;
    g_activePlayerUnit = nullptr;
}

void LCD_OnPlayerLeaveWorld() {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;

    g_lcdActive = false;
    g_activePlayerUnit = nullptr;
}

void LCD_OnPlayerEnterWorld(const ObjectManager& objects) {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;

    g_lcdActive = true;

    g_activePlayerUnit = objects.GetActivePlayer();
}

void LCD_SetupPlayerClassDisplay(const ObjectManager& objects) {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;

    g_activePlayerUnit = objects.GetActivePlayer();

    LCD_SetPage(1);

    CEzLcd_RenderActivePage(g_lcdContext, 0);

    g_lcdDisplayMode = 0;

    g_lcdActive = true;

    auto* player = static_cast<const CGPlayer_C*>(g_activePlayerUnit);
    const auto classId = player ? player->State().GetClass() : 0u;

    constexpr std::uint8_t kClassPriest  = 5;
    constexpr std::uint8_t kClassMage    = 8;
    constexpr std::uint8_t kClassWarlock = 9;
    g_lcdIsWandClass = (classId == kClassPriest ||
                        classId == kClassMage ||
                        classId == kClassWarlock);

    bool showAmmo = false;
    bool showSoulShard = false;

    constexpr std::uint8_t kClassWarrior = 1;
    constexpr std::uint8_t kClassHunter  = 3;
    constexpr std::uint8_t kClassRogue   = 4;

    switch (classId) {
        case kClassWarrior:
        case kClassHunter:
        case kClassRogue:
            showAmmo = true;
            break;
        case kClassWarlock:
            showSoulShard = true;
            break;
        default:
            break;
    }

    LCD_SetElementVisible("AMMO", showAmmo);
    LCD_SetElementVisible("AMMO_FLD", showAmmo);
    LCD_SetElementVisible("SOUL_SHARD", showSoulShard);
    LCD_SetElementVisible("SOUL_SHARD_FLD", showSoulShard);

    LCD_SetRegisteredElementText("BF_NOT_IN");
}

void LCD_OnAcceptBattlefieldPort() {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;
    if (g_lcdCurrentPage == 2) {
        LCD_SetPage(g_lcdPreviousPage);
    }
}

void LCD_OnChatMessage(const char* ) {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;
}

void LCD_OnIdleMessage() {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;
}

void LCD_OnAuctionOutbidNotification(const char* ) {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;
}

void LCD_OnAuctionWonNotification(const char* ) {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;
}

void LCD_OnAuctionSoldNotification(const char* ) {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;
}

void LCD_OnAuctionExpiredNotification(const char* ) {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;
}

void LCD_OnBattlefieldStatus(const ObjectManager& objects) {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;

    g_lcdBattlefieldMode = true;
    g_lcdBattlefieldStartTick =
        BattlefieldInfo::Get().GetBattlefieldInstanceStartTick();
    LCD_UpdateBattlefieldScoreboard(objects);
    LCD_SetPage(3);

}

void LCD_OnBattlefieldStatusCleared() {
    if (!g_lcdContext) return;
    if (!g_lcdInitialized) return;

    g_lcdBattlefieldMode = false;
    LCD_SetPage(1);

}

void LCD_UpdateBattlefieldScoreboard(const ObjectManager& objects) {
    const auto playerGuid = objects.GetActivePlayerGuid();
    if (playerGuid.IsEmpty()) {
        return;
    }

    const auto& bf = BattlefieldInfo::Get();
    const auto& scores = bf.GetScoreEntries();
    const auto scoreCount = static_cast<int>(scores.size());

    int allianceCount = 0;
    int hordeCount = 0;

    for (int i = 0; i < scoreCount; ++i) {
        const auto& entry = scores[static_cast<std::size_t>(i)];

        if (entry.player_guid == playerGuid) {
            LCD_UpdateSoulShardDisplay("BF_KILLBLOWS",
                                       static_cast<int>(entry.killing_blows));
            LCD_UpdateSoulShardDisplay("BF_DEATHS",
                                       static_cast<int>(entry.deaths));
            LCD_UpdateSoulShardDisplay("BF_HONOR_KILLS",
                                       static_cast<int>(entry.honorable_kills));
            LCD_UpdateSoulShardDisplay("BF_DAMAGE",
                                       static_cast<int>(entry.damage_done));
            LCD_UpdateSoulShardDisplay("BF_HEALING",
                                       static_cast<int>(entry.healing_done));
        }

        if (entry.faction != 0) {
            ++allianceCount;
        } else {
            ++hordeCount;
        }
    }

    if (scoreCount == 0) {
        LCD_UpdateSoulShardDisplay("BF_KILLBLOWS", 0);
        LCD_UpdateSoulShardDisplay("BF_DEATHS", 0);
        LCD_UpdateSoulShardDisplay("BF_HONOR_KILLS", 0);
        LCD_UpdateSoulShardDisplay("BF_DAMAGE", 0);
        LCD_UpdateSoulShardDisplay("BF_HEALING", 0);
    }

    LCD_UpdateSoulShardDisplay("BF_ALLIANCE", allianceCount);
    LCD_UpdateSoulShardDisplay("BF_HORDE", hordeCount);

    const auto now = openwow::core::GameClock::GetTickCount32();
    const auto elapsedMs = now - g_lcdBattlefieldStartTick;
    LCD_UpdateSoulShardDisplay("BF_TIME",
                               static_cast<int>(elapsedMs / 60000u));

}

void LCD_InitializeStrings(const ObjectManager& objects) {

    g_lcdInitialized = true;

    if (g_lcdDeferredInit) {
        LCD_SetupPlayerClassDisplay(objects);
        g_lcdDeferredInit = false;
    }
}

void LCD_Shutdown() {
    g_lcdElementRegistry.clear();
    g_lcdContext = nullptr;
    g_lcdContextStorage.reset();
    g_lcdUpdateHandlerRegistered = false;
    g_lcdMapDataEntries.clear();
    g_lcdPOIIconEntries.clear();
    g_lcdInitialized = false;
    g_lcdActive = false;
    g_lcdDisplayMode = 0;
    g_lcdIsWandClass = false;
    g_activePlayerUnit = nullptr;
    LCD_ClearPOIIconTextureCache();
    g_chatAlertCount = 0;

    g_monoPages.clear();
    g_colorPages.clear();
    g_allocatedSubscreens.clear();
}

void LCD_Initialize() {

    LCD_Shutdown();
    g_lcdContextStorage = std::make_unique<std::byte[]>(kLCDContextSize);
    std::memset(g_lcdContextStorage.get(), 0, kLCDContextSize);
    g_lcdContext = g_lcdContextStorage.get();

    g_monoPages.assign(CEzLcdFields::kMonoPageArrayCapacity, nullptr);
    g_colorPages.assign(CEzLcdFields::kColorPageArrayCapacity, nullptr);
    g_allocatedSubscreens.clear();
}

LCDRuntimeStateSnapshot LCD_GetRuntimeStateSnapshot() {
    LCDRuntimeStateSnapshot snapshot{};
    snapshot.has_context = g_lcdContext != nullptr;
    snapshot.initialized = g_lcdInitialized;
    snapshot.deferred_init_pending = g_lcdDeferredInit;
    snapshot.active = g_lcdActive;
    snapshot.battlefield_mode = g_lcdBattlefieldMode;
    snapshot.update_handler_registered = g_lcdUpdateHandlerRegistered;
    snapshot.element_count = g_lcdElementRegistry.size();
    snapshot.poi_icon_cache_count = g_lcdPOIIconTextureCache.size();
    snapshot.chat_alert_count = g_chatAlertCount;
    snapshot.previous_texture_handle = g_prevTextureHandle;
    snapshot.european_numbers = g_europeanNumbers;
    snapshot.current_page = g_lcdCurrentPage;
    snapshot.previous_page = g_lcdPreviousPage;
    snapshot.display_mode = g_lcdDisplayMode;
    snapshot.is_wand_class = g_lcdIsWandClass;
    return snapshot;
}

void LCD_SetPageStateForTesting(int currentPage, int previousPage) {
    g_lcdCurrentPage = currentPage;
    g_lcdPreviousPage = previousPage;
}

void* CEzLcdPageArraySnapshot::mono_page_at(int index) const {
    if (!mono_pages_ptr || index < 0 ||
        static_cast<std::size_t>(index) >= mono_pages_ptr->size()) {
        return nullptr;
    }
    return (*mono_pages_ptr)[static_cast<std::size_t>(index)];
}

void* CEzLcdPageArraySnapshot::color_page_at(int index) const {
    if (!color_pages_ptr || index < 0 ||
        static_cast<std::size_t>(index) >= color_pages_ptr->size()) {
        return nullptr;
    }
    return (*color_pages_ptr)[static_cast<std::size_t>(index)];
}

CEzLcdPageArraySnapshot LCD_GetPageArraySnapshot() {
    CEzLcdPageArraySnapshot snap{};
    snap.mono_count    = g_monoPages.size();
    snap.color_count   = g_colorPages.size();
    snap.mono_capacity = g_monoPages.capacity();
    snap.color_capacity = g_colorPages.capacity();
    snap.total_allocated_subscreens = g_allocatedSubscreens.size();
    snap.mono_pages_ptr  = &g_monoPages;
    snap.color_pages_ptr = &g_colorPages;
    return snap;
}

int LCD_SetEuropeanNumbers(int value) {
    g_europeanNumbers = (value != 0) ? 1 : 0;
    return g_europeanNumbers;
}

void LCD_ApplyEuropeanNumberFormat(char* str) {
    if (!g_europeanNumbers) return;

    char* end = str + std::strlen(str);
    char* p = str;

    while (p < end && *p) {
        if (*p < '0' || *p > '9') {
            ++p;
            continue;
        }

        while (true) {
            char c = *p;
            if (c == ',') {
                if (p[1] >= '0' && p[1] <= '9')
                    *p = '.';
                ++p;
            } else if (c == '.') {
                if (p[1] >= '0' && p[1] <= '9')
                    *p = ',';
                ++p;
            } else if (c >= '0' && c <= '9') {
                ++p;
            } else {
                break;
            }
        }
    }
}

int MinimapPOI_GetIconTexture(int , int* outTexCoords) {

    if (outTexCoords) {
        outTexCoords[0] = 0;
        outTexCoords[1] = 0;
    }
    return 0;
}

void MinimapPOI_InitIconCache() {

}

int MinimapPOI_CheckCacheGrowth(int ) {

    return 0;
}

void ChatMessage_Resize(void* , uint32_t ) {

}

void POIIcon_DestroyAll(void* ) {

    g_lcdPOIIconEntries.clear();
}

void MapData_DestroyAll() {
    g_lcdMapDataEntries.clear();
}

void* LCDHandle_Alloc(int , bool ) {

    return nullptr;
}

void* BitmapByIcon_Alloc(int , bool ) {

    return nullptr;
}

void BitmapByIconHashTable_Clear() {

    g_lcdPOIIconTextureCache.clear();
}

void LCDHandleList_Destroy(void* ) {
}

void ULCDHANDLE_SetText(LCDElementEntry& entry, const char* text) {
    entry.text = text ? text : "";
}

bool LCD_ParseTextElement(int , const ui::xml::CXMLNode* ,
                          void* , int ) {

    return true;
}

bool LCD_ParseBitmapElement(int , const ui::xml::CXMLNode* xmlNode,
                            void* ) {
    using openwow::core::ParseSignedDecimalLikeSub76F0D0;

    uint32_t posX = 0;
    uint32_t posY = 0;
    uint32_t sizeW = 0;
    uint32_t sizeH = 0;
    uint32_t miplevel = 0;

    const char* name = ui::xml::XMLNode_GetAttributeValue(xmlNode, "name");
    if (!name || !*name) {
        return false;
    }

    uint32_t texCoordX = 0;
    uint32_t texCoordY = 0;
    int texCoordBottom = 0;
    int texCoordRight = 0;

    if (g_lcdElementRegistry.count(name)) {

        return false;
    }

    int bitmapType = 0;
    char texturePath[256] = {};

    const char* typeStr = ui::xml::XMLNode_GetAttributeValue(xmlNode, "type");
    if (typeStr && *typeStr) {
        if (SStrCmpNoCase("texture", typeStr,
                          std::numeric_limits<size_t>::max()) == 0) {
            bitmapType = 2;
            const char* dataStr =
                ui::xml::XMLNode_GetAttributeValue(xmlNode, "data");
            if (!dataStr || !*dataStr) {

                return false;
            }
            SStrCopy(texturePath, dataStr, std::numeric_limits<int>::max());
        } else if (SStrCmpNoCase("prevTexture", typeStr,
                                 std::numeric_limits<size_t>::max()) == 0) {
            bitmapType = 3;
        }
    }

    const char* mipStr =
        ui::xml::XMLNode_GetAttributeValue(xmlNode, "miplevel");
    if (mipStr && *mipStr) {
        miplevel = ParseSignedDecimalLikeSub76F0D0(mipStr);
    }

    uint32_t alpha = 1;
    const char* alphaStr =
        ui::xml::XMLNode_GetAttributeValue(xmlNode, "alpha");
    if (alphaStr && *alphaStr) {
        alpha = ParseSignedDecimalLikeSub76F0D0(alphaStr);
    }

    uint32_t iconIndex = static_cast<uint32_t>(-1);
    const char* iconStr =
        ui::xml::XMLNode_GetAttributeValue(xmlNode, "icon");
    if (iconStr && *iconStr) {
        iconIndex = ParseSignedDecimalLikeSub76F0D0(iconStr);
        if (iconIndex != static_cast<uint32_t>(-1)) {
            bitmapType = 4;
        }
    }

    for (const ui::xml::CXMLNode* child = xmlNode->first_child;
         child != nullptr;
         child = child->right_sibling) {
        const char* childTag = child->tag.c_str();

        if (SStrCmpNoCase(childTag, "Origin",
                          std::numeric_limits<size_t>::max()) == 0) {
            const char* xVal =
                ui::xml::XMLNode_GetAttributeValue(child, "x");
            posX = (xVal && *xVal) ? ParseSignedDecimalLikeSub76F0D0(xVal) : 0;

            const char* yVal =
                ui::xml::XMLNode_GetAttributeValue(child, "y");
            posY = (yVal && *yVal) ? ParseSignedDecimalLikeSub76F0D0(yVal) : 0;

        } else if (SStrCmpNoCase(childTag, "RelOrigin",
                                 std::numeric_limits<size_t>::max()) == 0) {

            const char* relTo =
                ui::xml::XMLNode_GetAttributeValue(child, "RelTo");
            auto relIt = g_lcdElementRegistry.find(relTo ? relTo : "");
            if (relIt == g_lcdElementRegistry.end()) {

                return false;
            }
            const auto& relEntry = relIt->second;

            const char* xVal =
                ui::xml::XMLNode_GetAttributeValue(child, "x");
            uint32_t xOff = 0;
            if (xVal && *xVal) {
                xOff = ParseSignedDecimalLikeSub76F0D0(xVal);
            }

            const char* yVal =
                ui::xml::XMLNode_GetAttributeValue(child, "y");
            uint32_t yOff = 0;
            if (yVal && *yVal) {
                yOff = ParseSignedDecimalLikeSub76F0D0(yVal);
            }

            posX = static_cast<uint32_t>(relEntry.posX) + xOff;
            posY = static_cast<uint32_t>(relEntry.posY) + yOff;

        } else if (SStrCmpNoCase(childTag, "Size",
                                 std::numeric_limits<size_t>::max()) == 0) {
            const char* xVal =
                ui::xml::XMLNode_GetAttributeValue(child, "x");
            sizeW = (xVal && *xVal) ? ParseSignedDecimalLikeSub76F0D0(xVal) : 0;

            const char* yVal =
                ui::xml::XMLNode_GetAttributeValue(child, "y");
            sizeH = (yVal && *yVal) ? ParseSignedDecimalLikeSub76F0D0(yVal) : 0;

        } else if (SStrCmpNoCase(childTag, "TexCoord",
                                 std::numeric_limits<size_t>::max()) == 0) {

            const char* xVal =
                ui::xml::XMLNode_GetAttributeValue(child, "x");
            if (xVal && *xVal) {
                texCoordX = ParseSignedDecimalLikeSub76F0D0(xVal);
            }

            const char* yVal =
                ui::xml::XMLNode_GetAttributeValue(child, "y");
            if (yVal && *yVal) {
                texCoordY = ParseSignedDecimalLikeSub76F0D0(yVal);
            }

            const char* wVal =
                ui::xml::XMLNode_GetAttributeValue(child, "width");
            if (wVal && *wVal) {
                texCoordRight = static_cast<int>(
                    texCoordX + ParseSignedDecimalLikeSub76F0D0(wVal));
            }

            const char* hVal =
                ui::xml::XMLNode_GetAttributeValue(child, "height");
            if (hVal && *hVal) {
                texCoordBottom = static_cast<int>(
                    texCoordY + ParseSignedDecimalLikeSub76F0D0(hVal));
            }
        }
    }

    int textureHandle = 0;
    int texDimA = 0;
    int texDimB = 0;

    switch (bitmapType) {
        case 1:

            break;
        case 2:

            (void)texturePath;
            (void)miplevel;
            (void)texCoordRight;
            (void)texCoordBottom;
            break;
        case 3:
            textureHandle = g_prevTextureHandle;
            break;
        case 4: {
            int texCoords[2] = {};
            textureHandle =
                MinimapPOI_GetIconTexture(static_cast<int>(iconIndex),
                                          texCoords);
            texDimA = texCoords[0];
            texDimB = texCoords[1];
            break;
        }
        default:
            break;
    }

    EZLCDBitmapElement* bmpElement = EZ_LCD_AddNewBitmap(
        g_lcdContext, static_cast<int32_t>(sizeW), static_cast<int32_t>(sizeH),
        texDimA, texDimB);

    if (bmpElement) {
        (void)EZ_LCD_Page_SetBitmapTexture(
            bmpElement, static_cast<uint32_t>(textureHandle));
    }

    g_prevTextureHandle = textureHandle;

    if (bmpElement) {
        (void)EZ_LCD_Page_SetBitmapPosition(bmpElement,
                                            static_cast<int32_t>(posX),
                                            static_cast<int32_t>(posY));
    }

    if (bmpElement) {
        (void)EZ_LCD_Page_SetBitmapAlpha(bmpElement, alpha);
    }

    LCDElementEntry entry{};
    entry.nameHash = SStrHashCI(name);
    entry.name = name;
    entry.lcdHandle = bmpElement;
    entry.posX = static_cast<int32_t>(posX);
    entry.posY = static_cast<int32_t>(posY);
    g_lcdElementRegistry[name] = entry;

    return true;
}

bool LCD_ParseMapElement(int ,
                         const ui::xml::CXMLNode *xmlNode) {
    UMapData entry{};

    for (const ui::xml::CXMLNode *child = xmlNode->first_child;
         child != nullptr;
         child = child->right_sibling) {
        if (SStrCmpNoCase(child->tag.c_str(), "Size",
                          std::numeric_limits<size_t>::max()) == 0) {
            const char *xVal =
                ui::xml::XMLNode_GetAttributeValue(child, "x");
            entry.sizeX = (xVal && *xVal)
                              ? static_cast<int32_t>(
                                    core::ParseSignedDecimalLikeSub76F0D0(xVal))
                              : 0;

            const char *yVal =
                ui::xml::XMLNode_GetAttributeValue(child, "y");
            entry.sizeY = (yVal && *yVal)
                              ? static_cast<int32_t>(
                                    core::ParseSignedDecimalLikeSub76F0D0(yVal))
                              : 0;
        } else if (SStrCmpNoCase(child->tag.c_str(), "Offset",
                                 std::numeric_limits<size_t>::max()) == 0) {
            const char *xVal =
                ui::xml::XMLNode_GetAttributeValue(child, "x");
            entry.offsetX =
                (xVal && *xVal)
                    ? static_cast<int32_t>(
                          core::ParseSignedDecimalLikeSub76F0D0(xVal))
                    : 0;

            const char *yVal =
                ui::xml::XMLNode_GetAttributeValue(child, "y");
            entry.offsetY =
                (yVal && *yVal)
                    ? static_cast<int32_t>(
                          core::ParseSignedDecimalLikeSub76F0D0(yVal))
                    : 0;
        }
    }

    g_lcdMapDataEntries.push_back(entry);
    return true;
}

bool LCD_ParseElement(int screenId, const ui::xml::CXMLNode *xmlNode,
                      void *errorCtx, int pageId) {
    if (xmlNode == nullptr) {
        return false;
    }

    const char *tagName = xmlNode->tag.c_str();
    if (!tagName || !*tagName) {
        return false;
    }

    if (SStrCmpNoCase(tagName, "LCDText",
                      std::numeric_limits<size_t>::max()) == 0) {
        return LCD_ParseTextElement(screenId, xmlNode, errorCtx, pageId);
    }
    if (SStrCmpNoCase(tagName, "LCDBitmap",
                      std::numeric_limits<size_t>::max()) == 0) {
        return LCD_ParseBitmapElement(screenId, xmlNode, errorCtx);
    }
    if (SStrCmpNoCase(tagName, "LCDMap",
                      std::numeric_limits<size_t>::max()) == 0) {
        return LCD_ParseMapElement(screenId, xmlNode);
    }

    return false;
}

bool LCD_ParseGlobals(const ui::xml::CXMLNode* xmlNode, void* errorCtx) {
    for (const ui::xml::CXMLNode* child = xmlNode->first_child;
         child != nullptr;
         child = child->right_sibling) {
        LCD_ParseElement(-1, child, errorCtx, -1);
    }
    return true;
}

int LCD_ScreenNameToId(const char* screenName) {
    static constexpr struct {
        const char* name;
        int id;
    } kScreenNames[] = {
        {"WoWTitleScreen",                0},
        {"PlayerInfoScreen",              1},
        {"BattlefieldAlertScreen",        2},
        {"BattlefieldStatusScreen",       3},
        {"NotInBattlefieldScreen",        4},
        {"WaitQueueScreen",               5},
        {"ChatAlertScreen",               6},
        {"ArathiBasinMapScreen",          7},
        {"WarsongMapScreen",              8},
        {"EyeOfTheStormMapScreen",        9},
        {"StrandOfTheAncientsMapScreen", 10},
        {"AlteracValleyMapScreen",       11},
        {"IsleOfConquestMapScreen",      12},
    };

    for (const auto& entry : kScreenNames) {
        if (SStrCmpNoCase(screenName, entry.name,
                          std::numeric_limits<size_t>::max()) == 0) {
            return entry.id;
        }
    }
    return -1;
}

int32_t CEzLcd_CreateSubscreen(void* ez_lcd_context, int32_t screenId) {
    if (!ez_lcd_context) return 0;

    auto* ctx = reinterpret_cast<std::uint32_t*>(ez_lcd_context);

    auto allocation = std::make_unique<char[]>(kCEzLcdSubscreenSize);
    auto* self = reinterpret_cast<std::uint32_t*>(allocation.get());

    CEzLcdSubscreen_Construct(self, reinterpret_cast<std::uintptr_t>(ctx));

    CEzLcdPageNode_StartShowTimer(self, 0xFFFFFFFF);

    const auto activeDevice     = ctx[CEzLcdFields::kActiveDevice];
    const auto colorDevice      = ctx[78];
    const auto monochromeDevice = ctx[80];

    if (activeDevice == colorDevice) {

        self[1] = static_cast<std::uint32_t>(kEZLCDColorPageExtent.width);
        self[2] = static_cast<std::uint32_t>(kEZLCDColorPageExtent.height);
    }

    const bool isMonoDevice = (activeDevice == monochromeDevice);

    std::vector<void*>& pageArray = isMonoDevice ? g_monoPages : g_colorPages;

    if (screenId >= 0 && static_cast<std::size_t>(screenId) < pageArray.size()) {
        pageArray[static_cast<std::size_t>(screenId)] = self;
    }

    g_allocatedSubscreens.push_back(std::move(allocation));

    return static_cast<int32_t>(pageArray.size());
}

void CEzLcd_SelectPage(void* , int32_t ) {

}

void CEzLcd_SetActivePage(void* , int32_t ) {

}

void CEzLcd_SetPageBGColor(void* , uint32_t ) {

}

bool LCD_ParseScreen(const ui::xml::CXMLNode* xmlNode, void* errorCtx) {
    using openwow::core::ParseSignedDecimalLikeSub76F0D0;

    const char* nameAttr =
        ui::xml::XMLNode_GetAttributeValue(xmlNode, "name");

    if (!nameAttr || !*nameAttr) {

        return false;
    }

    int screenId = LCD_ScreenNameToId(nameAttr);

    CEzLcd_CreateSubscreen(g_lcdContext, screenId);

    CEzLcd_SelectPage(g_lcdContext, screenId);

    CEzLcd_SetActivePage(g_lcdContext, screenId);

    const char* bgColorAttr =
        ui::xml::XMLNode_GetAttributeValue(xmlNode, "BGColor");

    if (bgColorAttr && *bgColorAttr) {

        char colorBuf[64];
        std::size_t len = std::strlen(bgColorAttr);
        if (len >= sizeof(colorBuf)) len = sizeof(colorBuf) - 1;
        std::memcpy(colorBuf, bgColorAttr, len);
        colorBuf[len] = '\0';

        char tokenR[8] = {};
        char tokenG[8] = {};
        char tokenB[8] = {};

        char* savePtr = nullptr;
        const char* tok = OPENWOW_STRTOK_R(colorBuf, ",", &savePtr);
        if (tok) {
            std::strncpy(tokenR, tok, 7);
            tok = OPENWOW_STRTOK_R(nullptr, ",", &savePtr);
        }
        if (tok) {
            std::strncpy(tokenG, tok, 7);
            tok = OPENWOW_STRTOK_R(nullptr, ",", &savePtr);
        }
        if (tok) {
            std::strncpy(tokenB, tok, 7);
        }

        uint32_t b = static_cast<uint8_t>(
            ParseSignedDecimalLikeSub76F0D0(tokenB));
        uint32_t g = static_cast<uint8_t>(
            ParseSignedDecimalLikeSub76F0D0(tokenG));
        uint32_t r = static_cast<uint8_t>(
            ParseSignedDecimalLikeSub76F0D0(tokenR));
        uint32_t color = (b << 16) | (g << 8) | r;

        CEzLcd_SetPageBGColor(g_lcdContext, color);
    }

    for (const ui::xml::CXMLNode* child = xmlNode->first_child;
         child != nullptr;
         child = child->right_sibling) {

        if (SStrCmpNoCase(child->tag.c_str(), "LCDSubscreen",
                          std::numeric_limits<size_t>::max()) == 0) {
            const char* idAttr =
                ui::xml::XMLNode_GetAttributeValue(child, "id");

            if (!idAttr || !*idAttr) {
                continue;
            }

            int subscreenId = static_cast<int>(
                ParseSignedDecimalLikeSub76F0D0(idAttr));

            for (const ui::xml::CXMLNode* subChild = child->first_child;
                 subChild != nullptr;
                 subChild = subChild->right_sibling) {
                if (!LCD_ParseElement(screenId, subChild, errorCtx,
                                      subscreenId)) {
                    return false;
                }
            }
        } else {
            if (!LCD_ParseElement(screenId, child, errorCtx, -1)) {
                return false;
            }
        }
    }

    return true;
}

void LCD_RenderMinimapFlags() {

}

void LCD_UpdateSoulShardDisplay(const char* fieldName, int count) {
    auto it = g_lcdElementRegistry.find(fieldName);
    if (it == g_lcdElementRegistry.end()) {
        return;
    }

    LCD_SetElementTextUTF8(it->second.lcdHandle, it->second.name);

    char valueBuf[32];
    std::snprintf(valueBuf, sizeof(valueBuf), "%d", count);

    char fldName[32];
    std::snprintf(fldName, sizeof(fldName), "%s_FLD", fieldName);
    auto fldIt = g_lcdElementRegistry.find(fldName);
    if (fldIt != g_lcdElementRegistry.end() && fldIt->second.lcdHandle) {
        LCD_SetElementTextUTF8(fldIt->second.lcdHandle, valueBuf);
    }
}

void LCD_SetElementTextUTF8(void* , const char* ) {

}

void LCD_SetFloatStatField(const char* fieldName, float value, bool asPercent) {

    bool showPercent = asPercent;

    auto it = g_lcdElementRegistry.find(fieldName);
    if (it == g_lcdElementRegistry.end()) {
        return;
    }

    void* labelHandle = it->second.lcdHandle;
    LCD_SetElementTextUTF8(labelHandle, it->second.name);

    char valueBuf[32];
    if (showPercent) {
        std::snprintf(valueBuf, sizeof(valueBuf), "%.2f%%", static_cast<double>(value));
    } else {
        std::snprintf(valueBuf, sizeof(valueBuf), "%.2f", static_cast<double>(value));
    }

    char fldName[32];
    std::snprintf(fldName, sizeof(fldName), "%s_FLD", fieldName);
    auto fldIt = g_lcdElementRegistry.find(fldName);
    if (fldIt != g_lcdElementRegistry.end() && fldIt->second.lcdHandle) {
        LCD_SetElementTextUTF8(fldIt->second.lcdHandle, valueBuf);
    }
}

void LCD_SetDamageRangeField(const char* fieldName, int minDmg, int maxDmg) {
    auto it = g_lcdElementRegistry.find(fieldName);
    if (it == g_lcdElementRegistry.end()) {
        return;
    }

    LCD_SetElementTextUTF8(it->second.lcdHandle, it->second.name);

    char valueBuf[32];
    std::snprintf(valueBuf, sizeof(valueBuf), "%d-%d", minDmg, maxDmg);

    char fldName[32];
    std::snprintf(fldName, sizeof(fldName), "%s_FLD", fieldName);
    auto fldIt = g_lcdElementRegistry.find(fldName);
    if (fldIt != g_lcdElementRegistry.end() && fldIt->second.lcdHandle) {
        LCD_SetElementTextUTF8(fldIt->second.lcdHandle, valueBuf);
    }
}

void LCD_SetStringStatField(const char* fieldName, const char* value) {
    auto it = g_lcdElementRegistry.find(fieldName);
    if (it == g_lcdElementRegistry.end()) {
        return;
    }

    LCD_SetElementTextUTF8(it->second.lcdHandle, it->second.name);

    char valueBuf[32];
    std::snprintf(valueBuf, sizeof(valueBuf), "%s", value);

    char fldName[32];
    std::snprintf(fldName, sizeof(fldName), "%s_FLD", fieldName);
    auto fldIt = g_lcdElementRegistry.find(fldName);
    if (fldIt != g_lcdElementRegistry.end() && fldIt->second.lcdHandle) {
        LCD_SetElementTextUTF8(fldIt->second.lcdHandle, valueBuf);
    }
}

void LCD_SetLabelOnly(const char* fieldName, const char* utf8Text) {
    auto it = g_lcdElementRegistry.find(fieldName);
    if (it == g_lcdElementRegistry.end()) {
        return;
    }
    LCD_SetElementTextUTF8(it->second.lcdHandle, utf8Text);
}

void LCD_UpdateCharacterStats() {

}

void LCD_UpdateDefensiveStats() {

}

int32_t EZ_LCD_Page_SetBitmapAlpha(EZLCDBitmapElement* element,
                                    uint32_t alpha) {
    if (!element) {
        return kEZLCDResultFail;
    }
    element->alpha = alpha;
    return kEZLCDResultOK;
}

int32_t EZ_LCD_Page_SetBitmapTexture(EZLCDBitmapElement* element,
                                      uint32_t texture) {
    if (!texture || !element) {
        return kEZLCDResultFail;
    }
    element->textureHandle = texture;
    return kEZLCDResultOK;
}

int32_t EZ_LCD_Page_SetElementSize(void* element, int32_t width, int32_t height) {
    if (!element) {
        return kEZLCDResultFail;
    }

    auto* fields = static_cast<uint32_t*>(element);
    fields[5] = static_cast<uint32_t>(width);
    fields[6] = static_cast<uint32_t>(height);
    return kEZLCDResultOK;
}

int32_t EZ_LCD_Page_SetBitmapPosition(EZLCDBitmapElement* element,
                                       int32_t x, int32_t y) {
    if (!element) {
        return kEZLCDResultFail;
    }

    element->fields_00_3C[3] = static_cast<uint32_t>(x);
    element->fields_00_3C[4] = static_cast<uint32_t>(y);
    return kEZLCDResultOK;
}

EZLCDBitmapElement* EZ_LCD_AddNewBitmap(
    void* ez_lcd_context,
    int32_t origin_x, int32_t origin_y,
    int32_t size_a, int32_t size_b) {

    if (!ez_lcd_context) {
        return nullptr;
    }

    auto* ctx = static_cast<uint32_t*>(ez_lcd_context);

    const auto* connection =
        reinterpret_cast<const EZLCDDisplayConnectionStorage*>(&ctx[66]);
    EZLCDDeviceHandle mono_device =
        EZ_LCD_DisplayConnection_GetMonochromeDevice(*connection);

    uint32_t active_page_index = ctx[130];

    uint32_t page_root_raw;
    if (active_page_index == mono_device) {
        page_root_raw = ctx[102];
    } else {
        page_root_raw = ctx[103];
    }

    if (page_root_raw == 0) {
        return nullptr;
    }

    auto* element = new EZLCDBitmapElement();

    element->fields_00_3C[3] = 0;
    element->fields_00_3C[4] = 0;
    element->fields_00_3C[6] = static_cast<uint32_t>(origin_x);
    element->fields_00_3C[7] = static_cast<uint32_t>(origin_y);

    element->fields_50_54[0] = static_cast<uint32_t>(size_a);
    element->fields_50_54[1] = static_cast<uint32_t>(size_b);

    return element;
}

void CEzLcd_RenderActivePage(void* ez_lcd_context, int32_t pageIndex) {
    if (!ez_lcd_context) {
        return;
    }

    auto* ctx = static_cast<uint32_t*>(ez_lcd_context);

    const auto* connection =
        reinterpret_cast<const EZLCDDisplayConnectionStorage*>(&ctx[66]);
    EZLCDDeviceHandle mono_device =
        EZ_LCD_DisplayConnection_GetMonochromeDevice(*connection);

    uint32_t active_device = ctx[130];

    void* page;
    if (active_device == mono_device) {
        page = reinterpret_cast<void*>(static_cast<std::uintptr_t>(ctx[102]));
    } else {
        page = reinterpret_cast<void*>(static_cast<std::uintptr_t>(ctx[103]));
    }

    if (!page) {
        return;
    }

    (void)pageIndex;
}

void CEzLcd_SetUpdateFlags(void* ez_lcd_context, uint32_t flags) {
    if (!ez_lcd_context) {
        return;
    }

    auto* ctx = static_cast<uint8_t*>(ez_lcd_context);

    auto& cs = *reinterpret_cast<openwow::platform::StormCriticalSection*>(ctx + 476);

    auto& accumulated_flags = *reinterpret_cast<uint32_t*>(ctx + 500);

    cs.Enter();
    accumulated_flags |= flags;
    cs.Leave();
}

int32_t CEzLcd_Connect(void* ez_lcd_context,
                       const CEzLcdConnectParams& params) {
    using namespace CEzLcdFields;

    if (!ez_lcd_context) {
        return kEZLCDResultFail;
    }

    auto* ctx = static_cast<uint32_t*>(ez_lcd_context);

    if (ctx[kBusyFlag1] != 0 || ctx[kBusyFlag2] != 0) {
        return kEZLCDResultFail;
    }

    auto* name_buffer = reinterpret_cast<char*>(&ctx[1]);
    if (params.app_name) {
        openwow::core::SStrCopy(name_buffer, params.app_name, 260);
    } else {
        name_buffer[0] = '\0';
    }

    const auto sentinel = static_cast<uint32_t>(0x80000000u);
    ctx[kSentinel1CC]   = sentinel;
    ctx[kSentinel1D0]   = sentinel;
    ctx[kConnecting]    = 1;
    ctx[kConnected]     = 0;
    ctx[kPageExtentPtr] = 0;
    ctx[kConfigB]       = static_cast<uint32_t>(params.config_b);
    ctx[kConfigA]       = static_cast<uint32_t>(params.config_a);
    ctx[kColorPage]     = 0;
    ctx[kMonoPage]      = 0;
    ctx[kReserved1B4]   = 0;
    ctx[kReserved1B0]   = 0;

    uint32_t extent_x = 0;
    uint32_t extent_y = 0;
    if (params.page_extent) {
        extent_x = static_cast<uint32_t>(params.page_extent->width);
        extent_y = static_cast<uint32_t>(params.page_extent->height);
    }

    const auto raw_support = static_cast<uint32_t>(params.device_support);
    uint32_t open_type_flag = 0;
    switch (raw_support) {
        case 0: open_type_flag = 1; break;
        case 1: open_type_flag = 2; break;
        case 2: open_type_flag = 3; break;
        default: break;

    }

    ctx[kDeviceSupport] = raw_support;

    if (params.callback) {
        ctx[kCallbackFunction] = params.callback->function;
        ctx[kCallbackContext]  = params.callback->context;
    } else {

        ctx[kCallbackFunction] = 0;
        ctx[kCallbackContext]  = 0;
    }

    (void)open_type_flag;
    (void)extent_x;
    (void)extent_y;
    const bool open_succeeded = false;

    if (!open_succeeded) {
        ctx[kConnected] = 0;
        return kEZLCDResultFail;
    }

    ctx[kConnected] = 1;

    const auto* connection =
        reinterpret_cast<const EZLCDDisplayConnectionStorage*>(&ctx[66]);
    ctx[kActiveDevice] = static_cast<uint32_t>(
        EZ_LCD_SelectInitialDevice(*connection, params.device_support));

    ctx[kConnecting] = 0;

    return kEZLCDResultOK;
}

}

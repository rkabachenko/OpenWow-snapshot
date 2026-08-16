#pragma once

#include <cstdint>

namespace openwow::game {

inline constexpr int32_t kBackpackSlotStart  = 23;
inline constexpr int32_t kBackpackSlotEnd    = 38;

inline constexpr int32_t kBankSlotStart      = 39;
inline constexpr int32_t kBankSlotEnd        = 66;

inline constexpr int32_t kKeyringSlotStart   = 86;
inline constexpr int32_t kKeyringSlotEnd     = 117;

inline constexpr int32_t kBagSlotStart       = 19;
inline constexpr int32_t kNumEquippedBags    = 4;
inline constexpr int32_t kBankBagSlotStart   = 63;

inline const char* GetShapeshiftSlotName(int32_t slot) {
    static constexpr const char* kInventorySlotGlobalKeys[] = {
        "HEADSLOT",     "NECKSLOT",          "SHOULDERSLOT", "SHIRTSLOT",       "CHESTSLOT",
        "WAISTSLOT",    "LEGSSLOT",          "FEETSLOT",     "WRISTSLOT",       "HANDSSLOT",
        "FINGER0SLOT",  "FINGER1SLOT",       "TRINKET0SLOT", "TRINKET1SLOT",    "BACKSLOT",
        "MAINHANDSLOT", "SECONDARYHANDSLOT", "RANGEDSLOT",   "TABARDSLOT",
    };

    if (slot < 0 || slot >= static_cast<int32_t>(sizeof(kInventorySlotGlobalKeys) /
                                                 sizeof(kInventorySlotGlobalKeys[0]))) {
        return nullptr;
    }

    switch (slot) {
        case 10: return "FINGER0SLOT_UNIQUE";
        case 11: return "FINGER1SLOT_UNIQUE";
        case 12: return "TRINKET0SLOT_UNIQUE";
        case 13: return "TRINKET1SLOT_UNIQUE";
        default: return kInventorySlotGlobalKeys[slot];
    }
}

struct ContainerSlotMapping {
    int32_t  slot       = 0;
    bool     isValid    = false;
    bool     isBankSlot = false;
    uint32_t numSlots   = 0;
};

inline ContainerSlotMapping ResolveContainerSlot(int32_t containerIndex,
                                                  int32_t slotIndex) {
    ContainerSlotMapping result;

    if (containerIndex < 0) {

        switch (containerIndex) {
            case -1:
                result.slot = slotIndex + kBackpackSlotStart;
                if (result.slot > kBackpackSlotEnd) return result;
                break;
            case -2:
                result.slot = slotIndex + kBankSlotStart;
                if (result.slot > kBankSlotEnd) return result;
                result.isBankSlot = true;
                break;
            case -3:
                result.slot = slotIndex + kKeyringSlotStart;
                if (result.slot > kKeyringSlotEnd) return result;
                break;
            case -5:

                return result;
            default:
                return result;
        }
        result.isValid = true;
        return result;
    }

    if (containerIndex < kNumEquippedBags) {
        result.slot = slotIndex;
        result.isValid = true;
    } else if (containerIndex < 11) {
        result.slot = slotIndex;
        result.isBankSlot = true;
        result.isValid = true;
    }

    return result;
}

}

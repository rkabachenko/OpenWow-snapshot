
#pragma once

#include <cstdint>

namespace openwow::core {

inline std::uint32_t ObjectType_GetFieldSection(std::uint32_t type_mask,
                                                 std::uint32_t section_index) {
    switch (type_mask) {
        case 0x03:
        case 0x07:
            if (section_index == 0) return 1;
            if (section_index == 1) return 2;
            return 8;

        case 0x09:
        case 0x19:
            if (section_index == 0) return 3;
            if (section_index == 3) return 4;
            return 8;

        case 0x21:
            if (section_index == 0) return 5;
            return 8;

        case 0x41:
            if (section_index == 0) return 6;
            return 8;

        case 0x81:
            if (section_index == 0) return 7;
            return 8;

        default:
            return 8;
    }
}

inline std::uint32_t ObjectType_GetTotalFieldCount(
    std::uint32_t type_mask, std::uint64_t guid,
    std::uint64_t active_player_guid) {
    switch (type_mask) {
        case 0x01:  return 6;
        case 0x03:  return 64;
        case 0x07:  return 138;
        case 0x09:  return 148;
        case 0x19:
            if (guid == active_player_guid)
                return 1326;
            else
                return 324;
        case 0x21:  return 18;
        case 0x41:  return 12;
        case 0x81:  return 36;
        default:    return 0;
    }
}

inline void ObjectMgr_InitFieldDescriptorTables() {}

}

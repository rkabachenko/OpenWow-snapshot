#pragma once

#include <cstdint>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class WorldSession;

inline constexpr int kBagFamilySystemMessageId = 296;

[[nodiscard]] bool DisplayBagFamilyText(const WorldSession &session,
                                        std::uint8_t bag_type_subclass);

[[nodiscard]] int BagFamilyMaskToDbcId(std::uint32_t bag_family_mask);

}
